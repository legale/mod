/* dbg_tracer.c
 * build: gcc -O2 -g -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -pthread -rdynamic -o tracer dbg_tracer.c
 */

#define _GNU_SOURCE
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

/* async-signal-safe io */
static inline void xwrite(int fd, const void *buf, size_t len) {
  ssize_t r = write(fd, buf, len);
  (void)r;
}

static inline pid_t gettid_fast(void) {
  static __thread pid_t tid = 0;
  if (!tid) tid = syscall(SYS_gettid);
  return tid;
}
static void wrs(int fd, const char *s) { xwrite(fd, s, strlen(s)); }
static void wrhex_u64(int fd, unsigned long v) {
  char b[32];
  int i = 31;
  b[i--] = 0;
  if (!v) {
    b[i--] = '0';
  }
  const char *h = "0123456789abcdef";
  while (v && i >= 0) {
    unsigned d = v & 0xf;
    b[i--] = h[d];
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
  if (!v) {
    b[i--] = '0';
  }
  while (v && i >= 0) {
    b[i--] = '0' + (v % 10);
    v /= 10;
  }
  xwrite(fd, b + i + 1, 31 - i);
}

/* safe snprintf that trips immediately */
int safe_snprintf(char *dst, size_t cap, const char *fmt, ...) {
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

/* trace registry shared across threads */
#define TRACE_N 256
#define MAX_THR 64

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
static __thread trace_slot_t *my_slot;

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static void trace_reg_init(void) {
  if (gtr) return;
  void *m = mmap(NULL, sizeof(trace_reg_t), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (m == MAP_FAILED) _exit(111);
  gtr = (trace_reg_t *)m;
  memset(gtr, 0, sizeof(*gtr));
  atomic_store(&gtr->inited, 1);
}

void trace_reg_attach_name(const char *name) {
  if (!gtr) trace_reg_init();

  if (my_slot && atomic_load(&my_slot->used)) {
    if (name && *name) {
      prctl(PR_SET_NAME, name);
      strncpy(my_slot->name, name, sizeof(my_slot->name) - 1);
      my_slot->name[sizeof(my_slot->name) - 1] = '\0';
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
    strncpy(tname, name, sizeof(tname) - 1);
    tname[sizeof(tname) - 1] = '\0';
  } else {
    safe_snprintf(tname, sizeof(tname), "wrk%d", id);
  }

  prctl(PR_SET_NAME, tname);
  strncpy(s->name, tname, sizeof(s->name) - 1);
  s->name[sizeof(s->name) - 1] = '\0';

  atomic_store(&s->idx, 0);
  atomic_store(&s->used, 1);
  my_slot = s;
}

void trace_set_thread_name(const char *name) {
  if (!name || !*name) return;
  if (!my_slot || !atomic_load(&my_slot->used)) {
    trace_reg_attach_name(name);
    return;
  }
  prctl(PR_SET_NAME, name);
  strncpy(my_slot->name, name, sizeof(my_slot->name) - 1);
  my_slot->name[sizeof(my_slot->name) - 1] = '\0';
}

void trace_reg_attach(void) { trace_reg_attach_name(NULL); }

#define STR1(x) #x
#define STR2(x) STR1(x)

#define TRACE()                                                                        \
  do {                                                                                 \
    if (!my_slot || !atomic_load(&my_slot->used)) trace_reg_attach();                  \
    unsigned k = atomic_fetch_add(&my_slot->idx, 1);                                   \
    trace_ent_t *e = &my_slot->ring[k % TRACE_N];                                      \
    safe_snprintf(e->site, sizeof(e->site), "%s:%d:%s", __FILE__, __LINE__, __func__); \
    e->t = now_ms();                                                                   \
  } while (0)

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
    unsigned start = (n > TRACE_N) ? (n - TRACE_N) : 0;
    for (unsigned j = start; j < n; j++) {
      trace_ent_t *e = &s->ring[j % TRACE_N];
      if (!e->site) continue;
      wrs(fd, "  ");
      wrdec_u64(fd, (unsigned long)(e->t));
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

/* print ready addr2line hint: module, base, pc, off, full cmd */
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
    size_t L = j - i;
    if (L > 0) {
      const char *line = buf + i;

      unsigned long start = 0, end = 0;
      size_t k = 0;
      while (k < L && ((line[k] >= '0' && line[k] <= '9') || (line[k] >= 'a' && line[k] <= 'f'))) {
        start = (start << 4) | (unsigned long)(line[k] <= '9' ? line[k] - '0' : 10 + line[k] - 'a');
        k++;
      }
      if (k < L && line[k] == '-')
        k++;
      else {
        i = j + 1;
        continue;
      }
      while (k < L && ((line[k] >= '0' && line[k] <= '9') || (line[k] >= 'a' && line[k] <= 'f'))) {
        end = (end << 4) | (unsigned long)(line[k] <= '9' ? line[k] - '0' : 10 + line[k] - 'a');
        k++;
      }

      while (k < L && line[k] != ' ')
        k++;
      while (k < L && line[k] == ' ')
        k++;
      int r_xp = 0;
      if (k + 3 < L && line[k] == 'r' && line[k + 2] == 'x' && line[k + 3] == 'p') r_xp = 1;

      const char *path = NULL;
      size_t p = L;
      while (p > 0 && line[p - 1] == ' ')
        p--;
      size_t e = p;
      while (p > 0 && line[p - 1] != ' ')
        p--;
      if (e > p && p < L) path = line + p;

      if (r_xp && pc >= start && pc < end && path) {
        unsigned long off = pc - start;

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
        wrhex_u64(2, off);
        wrs(2, "\n");

        wrs(2, "addr2line_cmd=addr2line -Cfie ");
        xwrite(2, path, e - p);
        wrs(2, " ");
        wrhex_u64(2, off);
        wrs(2, "\n");
        break;
      }
    }
    i = j + 1;
  }
}

/* segv handler */
static void segv_handler(int sig, siginfo_t *si, void *uctx) {
  ucontext_t *uc = (ucontext_t *)uctx;
  char name[16] = {0};
  prctl(PR_GET_NAME, name);
#if defined(__x86_64__)
  unsigned long pc = uc->uc_mcontext.gregs[REG_RIP];
  unsigned long sp = uc->uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
  unsigned long pc = uc->uc_mcontext.pc;
  unsigned long sp = uc->uc_mcontext.sp;
#else
  unsigned long pc = 0, sp = 0;
#endif
  wrs(2, "segv tid=");
  wrdec_u64(2, (unsigned long)gettid_fast());
  wrs(2, " name=");
  wrs(2, name);
  wrs(2, " addr=");
  wrhex_u64(2, (unsigned long)si->si_addr);
  wrs(2, " pc=");
  wrhex_u64(2, pc);
  wrs(2, " sp=");
  wrhex_u64(2, sp);
  wrs(2, "\n");

  /* print ready-to-run addr2line command and details */
  segv_print_addr2line_hint(pc);

  dump_maps(2);
  trace_dump_all(2);
  _exit(128 + sig);
}

void tracer_setup(void) {
  trace_reg_init();
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGSEGV, &sa, NULL);
}

/* header-like exports */
#undef TRACER_EXPORTS
#define TRACER_EXPORTS
#ifdef TRACER_EXPORTS
extern void tracer_setup(void);
extern void trace_reg_attach(void);
extern int safe_snprintf(char *dst, size_t cap, const char *fmt, ...);
extern void *demo_thread_fn(void *arg);
#endif

/* demo */

#include <errno.h>

static void sleep_ms(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) && errno == EINTR) {
  }
}

void *worker(void *arg) {
  trace_reg_attach();
  int id = (int)(uintptr_t)arg;
  char name[16];
  snprintf(name, sizeof(name), "wrk%u", (unsigned)id);
  prctl(PR_SET_NAME, name);
  unsigned t = 0;
  for (;;) {
    TRACE();
    char buf[32];
    safe_snprintf(buf, sizeof(buf), "thr=%d tick=%u\n", id, t);
    xwrite(1, buf, strlen(buf));
    if (id == 2 && t == 5) {
      /* trigger: intentional overflow */
      char small[8];
      safe_snprintf(small, sizeof(small), "boom-%u-long-long-msg", t);
    }
    t++;
    sleep_ms(1000);
  }
  return NULL;
}

int main(int argc, char **argv) {
  tracer_setup();
  pthread_t th[3];
  for (int i = 0; i < 3; i++)
    pthread_create(&th[i], NULL, worker, (void *)(uintptr_t)i);
  for (int i = 0; i < 3; i++)
    pthread_join(th[i], NULL);
  return 0;
}
