// syslog2.c
#include "syslog2.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h> // for snprintf, vsnprintf
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define MSG_BUF_SZ 4096
#define TIME_BUF_SZ 64
#define OUT_BUF_SZ (MSG_BUF_SZ + TIME_BUF_SZ + 128)

const char *last_function[MAX_THREADS] = {0};
pid_t thread_ids[MAX_THREADS] = {0};
pthread_t pthread_ids[MAX_THREADS] = {0};

static int syslog2_level = LOG_INFO;
static bool log_syslog = true;
static pthread_once_t syslog2_once = PTHREAD_ONCE_INIT;
static _Atomic int stdout_lock = 0;

static const char *priority_texts[] = {"M", "A", "C", "E", "W", "N", "I", "D"};

#ifdef IS_DYNAMIC_LIB
#include "../timeutil/timeutil.h" // жёсткая зависимость
#endif

// заглушки для функций связанного модуля времени, чтобы тестировать без него
#ifndef IS_DYNAMIC_LIB
__attribute__((weak)) void tu_init(void);
void tu_init() {}

__attribute__((weak)) int tu_clock_gettime_local(struct timespec *ts);
int tu_clock_gettime_local(struct timespec *ts) {
  if (clock_gettime(CLOCK_REALTIME, ts) != 0) return EINVAL;
  struct tm local_tm;
  localtime_r(&ts->tv_sec, &local_tm);

#if defined(_GNU_SOURCE)
  ts->tv_sec += local_tm.tm_gmtoff;
  // for debug only!!!!
  //  ts->tv_sec = 0;
#else
  time_t utc = ts->tv_sec;
  struct tm gm_tm;
  gmtime_r(&utc, &gm_tm);
  time_t local_secs = mktime(&local_tm);
  time_t gm_secs = mktime(&gm_tm);
  ts->tv_sec += (local_secs - gm_secs);
#endif
  return 0;
}
#endif // IS_DYNAMIC_LIB

static inline const char *strprio(int pri) {
  return (pri >= LOG_EMERG && pri <= LOG_DEBUG) ? priority_texts[pri] : "?";
}

static void current_time_str(char *buf, size_t sz, int pri) {
  struct timespec ts;
  struct tm tm;
  tu_clock_gettime_local(&ts);
  gmtime_r(&ts.tv_sec, &tm);

  snprintf(buf, sz, "%02d-%02d-%04d %02d:%02d:%02d.%03ld %s",
           tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
           tm.tm_hour, tm.tm_min, tm.tm_sec,
           ts.tv_nsec / 1000000, strprio(pri));
}

static void syslog2_init_once(void) {
  openlog("app", LOG_CONS | LOG_NDELAY, LOG_LOCAL1);
  setlogmask(LOG_UPTO(syslog2_level));
  tu_init();
}

static void lock_stdout(void) {
  while (__atomic_test_and_set(&stdout_lock, __ATOMIC_ACQUIRE))
    ;
}

static void unlock_stdout(void) {
  __atomic_clear(&stdout_lock, __ATOMIC_RELEASE);
}

void setup_syslog2(const char *ident, int level, bool use_syslog) {
  pthread_once(&syslog2_once, syslog2_init_once);

  openlog(ident ? ident : "app", LOG_CONS | LOG_NDELAY, LOG_LOCAL1);
  setlogmask(LOG_UPTO(level));
  syslog2_level = level;
  log_syslog = use_syslog;
}

void syslog2_print_last_functions(void) {
  syslog2_printf(LOG_ALERT, "last called functions by threads:\n");
  for (size_t index = 0; index < MAX_THREADS; index++) {
    if (last_function[index] != NULL && pthread_ids[index] != 0) {
      char name[16] = {0}; // pthread names max 16 байт включая \0
      // try get thread name, fallback to "unknown" if error
      if (pthread_getname_np(pthread_ids[index], name, sizeof(name)) != 0) {
        strncpy(name, "unknown", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
      }
      syslog2_printf(LOG_ALERT,
                     "idx=%zu tid=%d pthread_id=%lu name=%s last_func=%s\n",
                     index,
                     thread_ids[index],
                     (unsigned long)pthread_ids[index],
                     name,
                     last_function[index]);
    }
  }
}

void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...) {
  pthread_once(&syslog2_once, syslog2_init_once);

  static __thread pid_t tid = 0;
  if (!tid) tid = syscall(SYS_gettid);
  static __thread pthread_t pthid = 0;
  if (!pthid) pthid = pthread_self();

  size_t index = tid % MAX_THREADS;
  last_function[index] = func;
  thread_ids[index] = tid;
  pthread_ids[index] = pthid;

  if (!(LOG_MASK(pri) & LOG_UPTO(syslog2_level))) return;

  char msg[MSG_BUF_SZ];
  va_list ap;
  va_start(ap, nl);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  if (log_syslog) {
    syslog(pri, "[%d] %s:%d %s: %s%s", tid, file, line, func, msg, nl ? "\n" : "");
  }

  char tbuf[TIME_BUF_SZ];
  current_time_str(tbuf, sizeof(tbuf), pri);

  char outbuf[OUT_BUF_SZ];
  int len = snprintf(outbuf, sizeof(outbuf),
                     "[%s] [%d] %s:%d %s: %s%s",
                     tbuf, tid, file, line, func, msg, nl ? "\n" : "");
  if (len > 0) {
    lock_stdout();
    ssize_t written = write(STDOUT_FILENO, outbuf, len);
    (void)written;
    unlock_stdout();
  }
}

void syslog2_printf_(int pri, const char *func, const char *file, int line, const char *fmt, ...) {
  pthread_once(&syslog2_once, syslog2_init_once);
  if (!(LOG_MASK(pri) & LOG_UPTO(syslog2_level)))
    return;

  char msg[MSG_BUF_SZ];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  if (log_syslog) {
    syslog(pri, "%s", msg);
  }

  int len = strlen(msg);
  if (len > 0) {
    lock_stdout();
    ssize_t w = write(STDOUT_FILENO, msg, len);
    (void)w;
    unlock_stdout();
  }
}

int syslog2_get_pri() {
  return syslog2_level;
}