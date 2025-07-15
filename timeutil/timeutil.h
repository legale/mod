#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif //_POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif

typedef int (*timeutil_get_time_fn_t)(clockid_t, struct timespec *);
typedef int (*timeutil_sleep_fn_t)(const struct timespec *, struct timespec *);
typedef void (*timeutil_log_fn_t)(const char *);

typedef struct {
  timeutil_get_time_fn_t get_time;
  timeutil_sleep_fn_t sleep_fn;
  timeutil_log_fn_t log_hook;
} timeutil_mod_init_args_t;

// compiler macros
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// unit conversion macros
#define NS_PER_USEC 1000U
#define USEC_PER_MS 1000U
#define MS_PER_SEC 1000U
#define NS_PER_MS (USEC_PER_MS * NS_PER_USEC)
#define USEC_PER_SEC (MS_PER_SEC * USEC_PER_MS)
#define NS_PER_SEC (MS_PER_SEC * NS_PER_MS)

/* atomic_timespec struct */
typedef struct atomic_timespec {
  _Atomic long tv_sec;
  _Atomic long tv_nsec;
} atomic_timespec_t;

EXPORT_API void tu_init();
EXPORT_API void tu_update_offset();
EXPORT_API void tu_update_mono_real_offset();
EXPORT_API int tu_clock_gettime_local(struct timespec *ts);
EXPORT_API int tu_clock_gettime_local_mono(struct timespec *ts);


EXPORT_API uint64_t tu_clock_gettime_monotonic_ms(void);
EXPORT_API int64_t tu_get_cached_tz_off();
EXPORT_API int msleep(uint64_t ms);
EXPORT_API void atomic_ts_load(atomic_timespec_t *src, struct timespec *dest);
EXPORT_API void atomic_ts_store(atomic_timespec_t *dest, struct timespec *src);
EXPORT_API void atomic_ts_cpy(atomic_timespec_t *dest, atomic_timespec_t *src);

static inline void tu_diff_ts(struct timespec *diff, const struct timespec *start, const struct timespec *end) {
  if (end->tv_nsec < start->tv_nsec) {
    diff->tv_sec = end->tv_sec - start->tv_sec - 1;
    diff->tv_nsec = NS_PER_SEC + end->tv_nsec - start->tv_nsec;
  } else {
    diff->tv_sec = end->tv_sec - start->tv_sec;
    diff->tv_nsec = end->tv_nsec - start->tv_nsec;
  }
}

#endif // TIMEUTIL_H
