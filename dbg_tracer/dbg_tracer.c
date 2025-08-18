/* dbg_tracer.c
 * build: gcc -O2 -g -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=2 -pthread -rdynamic -fPIC -shared -o libdbg_tracer.so dbg_tracer.c
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <features.h> //to detect musl libc

#include "dbg_tracer.h"
#include <errno.h>
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
  const char *h = "0123456789abcdef";

  /* handle zero explicitly */
  if (!v) {
    b[i--] = '0';
  } else {
    while (v && i >= 0) {
      b[i--] = h[v & 0xf];
      v >>= 4;
    }
  }
  b[i--] = 'x';
  b[i--] = '0';

  /* write from first filled char */
  xwrite(fd, b + i + 1, 31 - i);
}

static void wrdec_u64(int fd, unsigned long v) {
  char b[32];
  int i = 31;

  /* handle zero explicitly */
  if (!v) {
    b[i--] = '0';
  } else {
    while (v && i >= 0) {
      b[i--] = '0' + (v % 10);
      v /= 10;
    }
  }

  /* write from first filled char */
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
  for (;;) {
    n = read(mfd, buf, sizeof(buf));
    if (n > 0) {
      xwrite(fd, buf, (size_t)n);
      continue;
    }
    if (n == 0) break;
    if (errno == EINTR) continue;
    break;
  }
  close(mfd);
}

/* addr2line hint to same fd */

#include <elf.h>
#include <sys/stat.h>

/* 0 = ET_EXEC (not PIE), 1 = ET_DYN (PIE) */
static int is_pie_binary(void) {
  int fd = open("/proc/self/exe", O_RDONLY);
  if (fd < 0) return 1;
#if UINTPTR_MAX == 0xffffffffu || defined(__arm__)
  Elf32_Ehdr hdr;
  if (read(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
    close(fd);
    return 1;
  }
#else
  Elf64_Ehdr hdr;
  if (read(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
    close(fd);
    return 1;
  }
#endif
  close(fd);
  return hdr.e_type == ET_DYN;
}

static void segv_print_addr2line_hint(int fd, unsigned long pc) {
  int mfd = open("/proc/self/maps", O_RDONLY);
  if (mfd < 0) return;

  static char buf[65536];
  size_t len = 0;
  for (;;) {
    ssize_t n = read(mfd, buf + len, sizeof(buf) - len);
    if (n <= 0) break;
    len += (size_t)n;
    if (len == sizeof(buf)) break;
  }
  close(mfd);

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

      int pie = is_pie_binary();
      if (r_xp && pc >= start && pc < end && path) {
        unsigned long file_off = pie ? (pc - start + pgoff) : pc;
        wrs(fd, "addr2line_mod=");
        xwrite(fd, path, e - p);
        wrs(fd, "\n");
        wrs(fd, "addr2line_base=");
        wrhex_u64(fd, start);
        wrs(fd, "\n");
        wrs(fd, "addr2line_pc=");
        wrhex_u64(fd, pc);
        wrs(fd, "\n");
        wrs(fd, "addr2line_off=");
        wrhex_u64(fd, file_off);
        wrs(fd, "\n");
        wrs(fd, "addr2line_cmd=addr2line -Cfie ");
        xwrite(fd, path, e - p);
        wrs(fd, " ");
        wrhex_u64(fd, file_off);
        wrs(fd, "\n");
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

  segv_print_addr2line_hint(fd, pc);
  dump_maps(fd);
  trace_dump_all(fd);

  /* ensure visibility for readers of files and pipes */
  fsync(fd);
}

/* add verbose debug prints to see which path is taken and what pc/sp are */

static int extract_pc_sp(const ucontext_t *uc, unsigned long *pc, unsigned long *sp) {
#if defined(__GLIBC__)
  wrs(2, "[dbg] libc=glibc\n");
#elif defined(__UCLIBC__)
  wrs(2, "[dbg] libc=uclibc\n");
#elif defined(__DEFINED_siginfo_t) && defined(__DEFINED_ucontext_t)
  wrs(2, "[dbg] libc=musl (via features.h)\n");
#elif defined(__linux__) && !defined(__GLIBC__) && !defined(__UCLIBC__)
  wrs(2, "[dbg] libc=musl (heuristic)\n");
#else
  wrs(2, "[dbg] libc=unknown\n");
#endif

#if defined(REG_RIP) && defined(REG_RSP)
  wrs(2, "[dbg] extract path: gregs[REG_RIP,REG_RSP]\n");
  *pc = (unsigned long)uc->uc_mcontext.gregs[REG_RIP];
  *sp = (unsigned long)uc->uc_mcontext.gregs[REG_RSP];
  return 1;
#elif defined(REG_EIP) && defined(REG_ESP)
  wrs(2, "[dbg] extract path: gregs[REG_EIP,REG_ESP]\n");
  *pc = (unsigned long)uc->uc_mcontext.gregs[REG_EIP];
  *sp = (unsigned long)uc->uc_mcontext.gregs[REG_ESP];
  return 1;
#elif defined(REG_EPC) && defined(REG_SP)
  wrs(2, "[dbg] extract path: gregs[REG_EPC,REG_SP]\n");
  *pc = (unsigned long)uc->uc_mcontext.gregs[REG_EPC];
  *sp = (unsigned long)uc->uc_mcontext.gregs[REG_SP];
  return 1;
#elif defined(__mips__)
  wrs(2, "[dbg] extract path: mips mcontext.pc + gregs[29]\n");
  *pc = (unsigned long)uc->uc_mcontext.pc;
  *pc &= ~(unsigned long)1; /* убираем micromips бит */
  *sp = (unsigned long)uc->uc_mcontext.gregs[29];
  return 1;
#elif defined(__arm__)
  wrs(2, "[dbg] extract path: arm arm_pc/arm_sp\n");
  *pc = (unsigned long)uc->uc_mcontext.arm_pc;
  *sp = (unsigned long)uc->uc_mcontext.arm_sp;
  return 1;
#else
  wrs(2, "[dbg] extract path: unknown layout\n");
  (void)uc;
  (void)pc;
  (void)sp;
  return 0;
#endif
}

EXPORT_API void tracer_dump_now_fd(int fd) {
#if UINTPTR_MAX == 0xffffffffffffffffull
  wrs(2, "[dbg] tracer_dump_now_fd: 64-bit branch\n");
  unsigned long pc = (unsigned long)__builtin_return_address(0);
  unsigned long sp = (unsigned long)__builtin_frame_address(0);
#elif UINTPTR_MAX == 0xffffffffu
  wrs(2, "[dbg] tracer_dump_now_fd: 32-bit branch\n");
  unsigned long pc = 0, sp = 0;
#if defined(__mips__)
  wrs(2, "[dbg] tracer_dump_now_fd: mips inline asm path\n");
  asm volatile("move %0, $sp" : "=r"(sp));
  void *_here = &&lbl_here_tr_dump;
  pc = (unsigned long)_here;
lbl_here_tr_dump:;
#else
  wrs(2, "[dbg] tracer_dump_now_fd: generic builtin path\n");
  pc = (unsigned long)__builtin_return_address(0);
  sp = (unsigned long)__builtin_frame_address(0);
#endif
#else
  wrs(2, "[dbg] tracer_dump_now_fd: unknown pointer size\n");
  unsigned long pc = 0, sp = 0;
#endif
  wrs(2, "[dbg] tracer_dump_now_fd values: pc=");
  wrhex_u64(2, pc);
  wrs(2, " sp=");
  wrhex_u64(2, sp);
  wrs(2, "\n");
  tracer_do_dump(fd, pc, sp, NULL);
}

static void segv_handler(int sig, siginfo_t *si, void *uctx) {
  wrs(2, "[dbg] segv_handler enter\n");
  ucontext_t *uc = (ucontext_t *)uctx;

  unsigned long pc = 0, sp = 0;
  if (!extract_pc_sp(uc, &pc, &sp)) {
    wrs(2, "[dbg] segv_handler: extract_pc_sp failed, trying inline fallback\n");
#if UINTPTR_MAX == 0xffffffffu
#if defined(__mips__)
    asm volatile("move %0, $sp" : "=r"(sp));
    void *_here = &&lbl_here_segv;
    pc = (unsigned long)_here;
  lbl_here_segv:;
#else
    pc = (unsigned long)__builtin_return_address(0);
    sp = (unsigned long)__builtin_frame_address(0);
#endif
#endif
  }

  wrs(2, "[dbg] segv_handler values: pc=");
  wrhex_u64(2, pc);
  wrs(2, " sp=");
  wrhex_u64(2, sp);
  wrs(2, " si_addr=");
  wrhex_u64(2, (unsigned long)si->si_addr);
  wrs(2, "\n");

  tracer_do_dump(2, pc, sp, si->si_addr);
  _exit(128 + sig);
}

EXPORT_API void tracer_dump_now(void) {
  tracer_dump_now_fd(2);
}

/* segv handler */

EXPORT_API void tracer_setup(void) {
  trace_reg_init();
  tracer_install_segv();
}

EXPORT_API void tracer_install_segv(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
}
