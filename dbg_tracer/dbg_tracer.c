/* dbg_tracer.c
 * build: gcc -O2 -g -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -pthread -rdynamic -fPIC -shared -o libdbg_tracer.so dbg_tracer.c
 * link:  -ldl if needed
 */

#define _GNU_SOURCE
#include "dbg_tracer.h"
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

/* async signal safe io */

static inline void xwrite(int fd, const void *buf, size_t len) {
  ssize_t r = write(fd, buf, len);
  (void)r;
}

static inline pid_t gettid_fast(void) {
  static __thread pid_t tid;
  if (!tid) tid = syscall(SYS_gettid);
  return tid;
}

static void wrs(int fd, const char *s) { xwrite(fd, s, strlen(s)); }

static void wrhex_u64(int fd, unsigned long v) {
  char b[32];
  int i = 31;
  b[i--] = 0;
  if (!v) b[i--] = '0';
  const char *h = "0123456789abcdef";
  while (v && i >= 0) {
    b[i--] = h[v & 0xf];
    v >>= 4;
  }
  b[i--] = 'x';
  b[i--] = '0';
  xwrite(fd, b + i + 1, 31 - i);
}

static void wrdec_u64(int fd, unsigned long v) {
  char b[32];
  int i = 31;
  b[i--] = 0;
  if (!v) b[i--] = '0';
  while (v && i >= 0) {
    b[i--] = '0' + (v % 10);
    v /= 10;
  }
  xwrite(fd, b + i + 1, 31 - i);
}

EXPORT_API int safe_snprintf(char *dst, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(dst, cap, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= cap) {
    wrs(2, "snprintf_overflow\n");
    raise(SIGSEGV);
  }
  return n;
}

/* trace registry */

#define TRACE_N 256
#define MAX_THR 128

typedef struct {
  char site[96];
  uint64_t t;
} trace_ent_t;

typedef struct {
  _Atomic int used;
  _Atomic unsigned idx;
  pid_t tid;
  char name[16];
  trace_ent_t ring[TRACE_N];
} trace_slot_t;

typedef struct {
  _Atomic int inited;
  _Atomic int next;
  trace_slot_t slots[MAX_THR];
} trace_reg_t;

static trace_reg_t *gtr;
static __thread trace_slot_t *trace_slot;

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void trace_reg_init(void) {
  if (gtr) return;
  void *m = mmap(NULL, sizeof(trace_reg_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (m == MAP_FAILED) _exit(111);
  gtr = (trace_reg_t *)m;
  memset(gtr, 0, sizeof(*gtr));
  atomic_store(&gtr->inited, 1);
}

EXPORT_API void trace_reg_attach_name(const char *name) {
  if (!gtr) trace_reg_init();

  if (trace_slot && atomic_load(&trace_slot->used)) {
    if (name && *name) {
      prctl(PR_SET_NAME, name);
      strncpy(trace_slot->name, name, sizeof(trace_slot->name) - 1);
      trace_slot->name[sizeof(trace_slot->name) - 1] = 0;
    }
    return;
  }

  int id = atomic_fetch_add(&gtr->next, 1);
  if (id >= MAX_THR) id = id % MAX_THR;

  trace_slot_t *s = &gtr->slots[id];
  memset(s, 0, sizeof(*s));
  s->tid = gettid_fast();

  char tname[16];
  if (name && *name) {
    snprintf(tname, sizeof(tname), "%s", name);
  } else {
    safe_snprintf(tname, sizeof(tname), "wrk%d", id);
  }

  prctl(PR_SET_NAME, tname);
  snprintf(s->name, sizeof(s->name), "%s", tname);

  atomic_store(&s->idx, 0u);
  atomic_store(&s->used, 1);
  trace_slot = s;
}

EXPORT_API void trace_reg_attach(void) { trace_reg_attach_name(NULL); }

EXPORT_API void trace_set_thread_name(const char *name) {
  if (!name || !*name) return;
  if (!trace_slot || !atomic_load(&trace_slot->used)) {
    trace_reg_attach_name(name);
    return;
  }
  prctl(PR_SET_NAME, name);
  snprintf(trace_slot->name, sizeof(trace_slot->name), "%s", name);
}

/* the only write entry called by header macro */
EXPORT_API void tracer_mark_point(const char *file, int line, const char *func) {
  if (!trace_slot || !atomic_load(&trace_slot->used)) trace_reg_attach();
  unsigned k = atomic_fetch_add(&trace_slot->idx, 1);
  trace_ent_t *e = &trace_slot->ring[k % TRACE_N];
  safe_snprintf(e->site, sizeof(e->site), "%s:%d:%s", file, line, func);
  e->t = now_ms();
}

static void trace_dump_all(int fd) {
  if (!gtr) return;
  for (int i = 0; i < MAX_THR; i++) {
    trace_slot_t *s = &gtr->slots[i];
    if (!atomic_load(&s->used)) continue;
    wrs(fd, "thr tid=");
    wrdec_u64(fd, (unsigned long)s->tid);
    wrs(fd, " name=");
    wrs(fd, s->name);
    wrs(fd, "\n");
    unsigned n = atomic_load(&s->idx);
    unsigned start = n > TRACE_N ? n - TRACE_N : 0u;
    for (unsigned j = start; j < n; j++) {
      trace_ent_t *e = &s->ring[j % TRACE_N];
      if (!e->site[0]) continue;
      wrs(fd, "  ");
      wrdec_u64(fd, (unsigned long)e->t);
      wrs(fd, " ");
      wrs(fd, e->site);
      wrs(fd, "\n");
    }
  }
}

/* maps dump */

static void dump_maps(int fd) {
  int mfd = open("/proc/self/maps", O_RDONLY);
  if (mfd < 0) return;
  char buf[4096];
  ssize_t n;
  while ((n = read(mfd, buf, sizeof(buf))) > 0)
    xwrite(fd, buf, n);
  close(mfd);
}

/* addr2line hint */

static void segv_print_addr2line_hint(unsigned long pc) {
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return;

  static char buf[65536];
  size_t len = 0;
  for (;;) {
    ssize_t n = read(fd, buf + len, sizeof(buf) - len);
    if (n <= 0) break;
    len += (size_t)n;
    if (len == sizeof(buf)) break;
  }
  close(fd);

  size_t i = 0;
  while (i < len) {
    size_t j = i;
    while (j < len && buf[j] != '\n')
      j++;
    const char *line = buf + i;
    size_t L = j - i;

    if (L > 0) {
      unsigned long start = 0, end = 0, pgoff = 0;
      size_t k = 0;

      while (k < L && ((line[k] >= '0' && line[k] <= '9') || (line[k] >= 'a' && line[k] <= 'f'))) {
        start = (start << 4) | (unsigned long)(line[k] <= '9' ? line[k] - '0' : 10 + line[k] - 'a');
        k++;
      }
      if (k >= L || line[k] != '-') goto next;
      k++;
      while (k < L && ((line[k] >= '0' && line[k] <= '9') || (line[k] >= 'a' && line[k] <= 'f'))) {
        end = (end << 4) | (unsigned long)(line[k] <= '9' ? line[k] - '0' : 10 + line[k] - 'a');
        k++;
      }
      while (k < L && line[k] == ' ')
        k++;
      if (k + 3 >= L) goto next;
      int r_xp = (line[k] == 'r' && line[k + 2] == 'x' && line[k + 3] == 'p');
      while (k < L && line[k] != ' ')
        k++;
      while (k < L && line[k] == ' ')
        k++;
      if (k >= L) goto next;
      while (k < L && line[k] == '0')
        k++;
      while (k < L && ((line[k] >= '0' && line[k] <= '9') || (line[k] >= 'a' && line[k] <= 'f'))) {
        pgoff = (pgoff << 4) | (unsigned long)(line[k] <= '9' ? line[k] - '0' : 10 + line[k] - 'a');
        k++;
      }
      const char *path = NULL;
      size_t p = L;
      while (p > 0 && line[p - 1] == ' ')
        p--;
      size_t e = p;
      while (p > 0 && line[p - 1] != ' ')
        p--;
      if (e > p && p < L) path = line + p;

      if (r_xp && pc >= start && pc < end && path) {
        unsigned long file_off = (pc - start) + pgoff;
        wrs(2, "addr2line_mod=");
        xwrite(2, path, e - p);
        wrs(2, "\n");
        wrs(2, "addr2line_base=");
        wrhex_u64(2, start);
        wrs(2, "\n");
        wrs(2, "addr2line_pc=");
        wrhex_u64(2, pc);
        wrs(2, "\n");
        wrs(2, "addr2line_off=");
        wrhex_u64(2, file_off);
        wrs(2, "\n");
        wrs(2, "addr2line_cmd=addr2line -Cfie ");
        xwrite(2, path, e - p);
        wrs(2, " ");
        wrhex_u64(2, file_off);
        wrs(2, "\n");
        break;
      }
    }
  next:
    i = j + 1;
  }
}

/* dump */

static void tracer_do_dump(int fd, unsigned long pc, unsigned long sp, void *addr) {
  char name[16] = {0};
  prctl(PR_GET_NAME, name);
  wrs(fd, "trace tid=");
  wrdec_u64(fd, (unsigned long)gettid_fast());
  wrs(fd, " name=");
  wrs(fd, name);
  if (addr) {
    wrs(fd, " addr=");
    wrhex_u64(fd, (unsigned long)addr);
  }
  wrs(fd, " pc=");
  wrhex_u64(fd, pc);
  wrs(fd, " sp=");
  wrhex_u64(fd, sp);
  wrs(fd, "\n");
  segv_print_addr2line_hint(pc);
  dump_maps(fd);
  trace_dump_all(fd);
}

EXPORT_API void tracer_dump_now_fd(int fd) {
#if defined(__x86_64__)
  unsigned long pc = (unsigned long)__builtin_return_address(0);
  unsigned long sp = (unsigned long)__builtin_frame_address(0);
#elif defined(__aarch64__)
  unsigned long pc = (unsigned long)__builtin_return_address(0);
  unsigned long sp = (unsigned long)__builtin_frame_address(0);
#else
  unsigned long pc = 0, sp = 0;
#endif
  tracer_do_dump(fd, pc, sp, NULL);
}

EXPORT_API void tracer_dump_now(void) { tracer_dump_now_fd(2); }

/* segv handler */

static void segv_handler(int sig, siginfo_t *si, void *uctx) {
  ucontext_t *uc = (ucontext_t *)uctx;
#if defined(__x86_64__)
  unsigned long pc = uc->uc_mcontext.gregs[REG_RIP];
  unsigned long sp = uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
  unsigned long pc = uc->uc_mcontext.pc;
  unsigned long sp = uc->uc_mcontext.sp;
#else
  unsigned long pc = 0, sp = 0;
#endif
  tracer_do_dump(2, pc, sp, si->si_addr);
  _exit(128 + sig);
}

EXPORT_API void tracer_setup(void) {
  trace_reg_init();
}

EXPORT_API void tracer_install_segv(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGSEGV, &sa, NULL);
}
