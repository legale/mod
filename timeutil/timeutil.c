#include "timeutil.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

static timeutil_get_time_fn_t get_time_ptr = clock_gettime;
static timeutil_sleep_fn_t sleep_fn_ptr = nanosleep;
static timeutil_log_fn_t log_hook_ptr = NULL;

static struct timespec offset_global_ts;
static struct timespec pause_accum_global;
static struct timespec pause_start_global;
static volatile bool pause_active_global;

int timeutil_mod_init(const timeutil_mod_init_args_t *args) {
  if (!args) {
    get_time_ptr = clock_gettime;
    sleep_fn_ptr = nanosleep;
    log_hook_ptr = NULL;
    return 0;
  }

  get_time_ptr = args->get_time ? args->get_time : clock_gettime;
  sleep_fn_ptr = args->sleep_fn ? args->sleep_fn : nanosleep;
  log_hook_ptr = args->log_hook;

  return 0;
}

int msleep(uint64_t ms) {
  struct timespec ts;
  ts.tv_sec = ms / MS_PER_SEC;
  ts.tv_nsec = (ms % MS_PER_SEC) * NS_PER_MS;
  return sleep_fn_ptr(&ts, NULL);
}

static inline void timespec_add(struct timespec *dst, const struct timespec *src) {
  dst->tv_sec += src->tv_sec;
  dst->tv_nsec += src->tv_nsec;
  if (dst->tv_nsec >= NS_PER_SEC) {
    dst->tv_sec += 1;
    dst->tv_nsec -= NS_PER_SEC;
  }
}

static inline void timespec_sub(const struct timespec *end, const struct timespec *start, struct timespec *result) {
  result->tv_sec = end->tv_sec - start->tv_sec;
  result->tv_nsec = end->tv_nsec - start->tv_nsec;
  if (result->tv_nsec < 0) {
    result->tv_sec -= 1;
    result->tv_nsec += NS_PER_SEC;
  }
}

void tu_update_offset(void) {
  struct timespec realtime, monotonic, diff;

  get_time_ptr(CLOCK_REALTIME, &realtime);
  get_time_ptr(CLOCK_MONOTONIC_RAW, &monotonic);

  tu_diff_ts(&diff, &monotonic, &realtime);

  offset_global_ts.tv_sec = (int64_t)diff.tv_sec;
  offset_global_ts.tv_nsec = diff.tv_nsec;
}

void tu_init(void) {
  tu_update_offset();

  pause_accum_global.tv_sec = 0;
  pause_accum_global.tv_nsec = 0;

  pause_active_global = false;
}

int tu_clock_gettime_fast_internal(struct timespec *ts) {
  int ret = get_time_ptr(CLOCK_MONOTONIC_RAW, ts);
  if (ret == 0) {
    ts->tv_sec -= pause_accum_global.tv_sec;
    ts->tv_nsec -= pause_accum_global.tv_nsec;
    if (ts->tv_nsec < 0) {
      ts->tv_sec -= 1;
      ts->tv_nsec += NS_PER_SEC;
    }
  }
  return ret;
}

int tu_clock_gettime_realtime_fast(struct timespec *ts) {
  int ret = tu_clock_gettime_fast_internal(ts);

  ts->tv_sec += offset_global_ts.tv_sec;
  ts->tv_nsec += offset_global_ts.tv_nsec;

  if (ts->tv_nsec >= NS_PER_SEC) {
    ts->tv_sec += 1;
    ts->tv_nsec -= NS_PER_SEC;
  }

  return ret;
}

void tu_pause_start(void) {
  if (!pause_active_global) {
    get_time_ptr(CLOCK_MONOTONIC_RAW, &pause_start_global);
    __atomic_store_n(&pause_active_global, true, __ATOMIC_RELEASE);
  }
}

void tu_pause_end(void) {
  if (__atomic_load_n(&pause_active_global, __ATOMIC_ACQUIRE)) {
    struct timespec now, diff;
    get_time_ptr(CLOCK_MONOTONIC_RAW, &now);
    timespec_sub(&now, &pause_start_global, &diff);
    timespec_add(&pause_accum_global, &diff);
    __atomic_store_n(&pause_active_global, false, __ATOMIC_RELEASE);
  }
}

uint64_t tu_clock_gettime_monotonic_fast_ms(void) {
  struct timespec ts;
  if (tu_clock_gettime_fast_internal(&ts) != 0)
    return 0;
  return (uint64_t)ts.tv_sec * MS_PER_SEC + (uint64_t)(ts.tv_nsec / NS_PER_MS);
}

void atomic_ts_load(atomic_timespec_t *src, struct timespec *dest) {
  dest->tv_sec = atomic_load_explicit(&src->tv_sec, memory_order_relaxed);
  dest->tv_nsec = atomic_load_explicit(&src->tv_nsec, memory_order_relaxed);
}

void atomic_ts_store(atomic_timespec_t *dest, struct timespec *src) {
  atomic_store_explicit(&dest->tv_sec, src->tv_sec, memory_order_relaxed);
  atomic_store_explicit(&dest->tv_nsec, src->tv_nsec, memory_order_relaxed);
}

void atomic_ts_cpy(atomic_timespec_t *dest, atomic_timespec_t *src) {
  struct timespec tmp;
  atomic_ts_load(src, &tmp);
  atomic_ts_store(dest, &tmp);
}

time_t tu_get_timezone_offset(void) {
  time_t now = time(NULL);
  struct tm local_tm;
  struct tm gm_tm;

  if (now == (time_t)-1)
    return 0;

  localtime_r(&now, &local_tm);
  gmtime_r(&now, &gm_tm);

  time_t local_sec = mktime(&local_tm);
  time_t gm_sec = mktime(&gm_tm);

  return local_sec - gm_sec;
}
