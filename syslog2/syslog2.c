// syslog2.c
#include "syslog2.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <unistd.h>

// forward declaration
void tu_init(void);
static void syslog2_internal(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, va_list ap);

static inline int clock_gettime_realtime(struct timespec *ts) {
  return clock_gettime(CLOCK_REALTIME, ts);
}

static realtime_fn_t realtime_func = clock_gettime_realtime;
static get_tz_off_fn_t get_tz_off_func = (get_tz_off_fn_t)NULL;
static syslog2_fn_t syslog2_func = syslog2_internal;

const char *last_function[MAX_THREADS] = {NULL};
pid_t thread_ids[MAX_THREADS] = {0};
pthread_t pthread_ids[MAX_THREADS] = {0};

static bool log_syslog = true;
int cached_mask = -1;

int syslog2_mod_init(const syslog2_mod_init_args_t *args) {
  if (!args) {
    realtime_func = clock_gettime_realtime;
    get_tz_off_func = (get_tz_off_fn_t)NULL;
    syslog2_func = syslog2_internal;
  } else {
    if (args->realtime_func) realtime_func = args->realtime_func;
    if (args->get_tz_off_func) get_tz_off_func = args->get_tz_off_func;
    if (args->syslog2_func) syslog2_func = args->syslog2_func;
  }
  return 0;
}

static const char *priority_texts[] = {"M", "A", "C", "E", "W", "N", "I", "D"};

static inline const char *strprio(int pri) {
  return (pri >= LOG_EMERG && pri <= LOG_DEBUG) ? priority_texts[pri] : "?";
}

// Устанавливает имя потока по строковому литералу, обрезая до 15 символов
void inline_pthread_set_name(const char *fname_str, size_t len) {
  char buf[16];
  if (len > 15)
    len = 15;
  memcpy(buf, fname_str, len);
  buf[len] = '\0';
  pthread_setname_np(pthread_self(), buf);
}

int setlogmask2(int level) {
  cached_mask = level;
  return setlogmask(level);
}

void setup_syslog2(const char *ident, int level, bool use_syslog) {
  openlog(ident, LOG_CONS | LOG_NDELAY, LOG_LOCAL1);
  setlogmask2(LOG_UPTO(level));
  log_syslog = use_syslog;
}

void print_last_functions() {
  syslog2(LOG_ALERT, "last called functions by threads:");
  for (size_t index = 0; index < MAX_THREADS; index++) {
    if (last_function[index] != NULL) {
      syslog2(LOG_ALERT, "idx=%zu tid=%lu pthread_id=%lu last_func=%s", index,
              (long unsigned)thread_ids[index], (long unsigned)pthread_ids[index], last_function[index]);
    }
  }
}

static int64_t get_tz_off_func_mock(void) {
  time_t t = time(NULL);
  struct tm local_tm = {0};
  localtime_r(&t, &local_tm);
  return local_tm.tm_gmtoff;
}

static void current_time_str(char *buf, size_t sz, int pri) {
  struct timespec ts;
  struct tm tm;
  realtime_func(&ts);
  time_t t = get_tz_off_func ? ts.tv_sec + get_tz_off_func() : ts.tv_sec + get_tz_off_func_mock();

  gmtime_r(&t, &tm);
  snprintf(buf, sz, "%02d-%02d-%04d %02d:%02d:%02d.%03ld %s", tm.tm_mday,
           tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec,
           ts.tv_nsec / 1000000, strprio(pri));
}

static void syslog2_internal(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, va_list ap) {
  if (!(cached_mask & LOG_MASK(pri))) return;

  static __thread pid_t tid = 0;
  if (!tid) tid = syscall(SYS_gettid);

  char msg[4096];
  vsnprintf(msg, sizeof(msg), fmt, ap);

  if (log_syslog) {
    syslog2(pri, "[%d] %s:%d %s: %s%s", tid, file, line, func, msg, nl ? "\n" : "");
  }

  char tbuf[64];
  current_time_str(tbuf, sizeof(tbuf), pri);
  printf("[%s] [%d] %s:%d %s: %s%s", tbuf, tid, file, line, func, msg, nl ? "\n" : "");
}

void syslog2_(int pri, const char *func, const char *file,
              int line, const char *fmt, bool nl, ...) {
  va_list ap;
  va_start(ap, nl);

  if (syslog2_func && syslog2_func != syslog2_internal) {
    syslog2_func(pri, func, file, line, fmt, nl, ap);
  } else {
    syslog2_internal(pri, func, file, line, fmt, nl, ap);
  }

  va_end(ap);
}

void syslog2_printf_(int pri, const char *func, const char *file, int line, const char *fmt, ...) {
  if (!(cached_mask & LOG_MASK(pri)))
    return;

  char msg[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  if (log_syslog) {
    syslog2(pri, "%s", msg);
  }
  printf("%s", msg);
}
