/* test.c – unit- и perf-тесты для новой timeutil (weak-подмена) */

#include "../test_util.h"
#include "timeutil.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* ===== fault-инъекции через strong-override libc функций ===== */
static bool fail_clock_gettime = false;
static struct timespec *fake_seq = NULL;
static int fake_len = 0, fake_pos = 0;

static bool fail_nanosleep = false;
static bool fail_time_call = false;

void set_clock_gettime_fail(int en) { fail_clock_gettime = en; }
void set_clock_gettime_sequence(struct timespec *seq, int len) {
  fake_seq = seq;
  fake_len = len;
  fake_pos = 0;
}
void reset_clock_gettime_sequence(void) {
  fake_seq = NULL;
  fake_len = fake_pos = 0;
  fail_clock_gettime = false;
}

void set_nanosleep_fail(int en) { fail_nanosleep = en; }
void set_time_fail(int en) { fail_time_call = en; }

/* strong-версии системных вызовов */

int clock_gettime(clockid_t id, struct timespec *tp) {
  if (fail_clock_gettime) {
    errno = EINVAL;
    return -1;
  }
  if (fake_seq && fake_pos < fake_len) {
    *tp = fake_seq[fake_pos++];
    return 0;
  }
  return syscall(SYS_clock_gettime, id, tp);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (fail_nanosleep) {
    if (rem) *rem = *req;
    errno = EINTR;
    return -1;
  }
  return syscall(SYS_nanosleep, req, rem);
}

time_t time(time_t *tloc) {
  if (fail_time_call) {
    if (tloc) *tloc = (time_t)-1;
    return (time_t)-1;
  }
  return syscall(SYS_time, tloc);
}

/* ===== tests ===== */

static void test_msleep_accuracy(void) {
  PRINT_TEST_START("msleep accuracy");

  struct timespec s, e;
  for (uint64_t d = 1; d <= 1500; d *= 10) {
    clock_gettime(CLOCK_MONOTONIC, &s);
    assert(msleep(d) == 0);
    clock_gettime(CLOCK_MONOTONIC, &e);
    uint64_t el = (e.tv_sec - s.tv_sec) * 1000 + (e.tv_nsec - s.tv_nsec) / 1000000;
    PRINT_TEST_INFO("req=%" PRIu64 "ms act=%" PRIu64 "ms", d, el);
    assert(el >= d);
  }
  PRINT_TEST_PASSED();
}

static void test_msleep_failure(void) {
  PRINT_TEST_START("msleep failure");
  set_nanosleep_fail(1);
  errno = 0;
  assert(msleep(10) == -1 && errno == EINTR);
  set_nanosleep_fail(0);
  PRINT_TEST_PASSED();
}

static void test_monotonic_fast_ms(void) {
  PRINT_TEST_START("tu_clock_gettime_monotonic_fast_ms vs CLOCK_MONOTONIC_RAW");

  const int iters = 1e5;
  uint64_t sys_ns, fast_ns;
  struct timespec start, end, ts;

  /* system */
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  for (int i = 0; i < iters; i++)
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  sys_ns = (end.tv_sec - start.tv_sec) * NS_PER_SEC + (end.tv_nsec - start.tv_nsec);

  /* fast */
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  for (int i = 0; i < iters; i++)
    tu_clock_gettime_monotonic_ms();
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  fast_ns = (end.tv_sec - start.tv_sec) * NS_PER_SEC + (end.tv_nsec - start.tv_nsec);

  double avg_sys = (double)sys_ns / iters;
  double avg_fast = (double)fast_ns / iters;
  PRINT_TEST_INFO("sys %.2f ns  fast(ms) %.2f ns", avg_sys, avg_fast);
  PRINT_TEST_PASSED();
}

static void test_clock_gettime_failure(void) {
  PRINT_TEST_START("clock_gettime fault path");
  struct timespec ts;
  set_clock_gettime_fail(1);
  assert(tu_clock_gettime_local(&ts) != 0);
  assert(tu_clock_gettime_monotonic_ms() == 0);
  set_clock_gettime_fail(0);
  PRINT_TEST_PASSED();
}

static void test_atomic_ts(void) {
  PRINT_TEST_START("atomic_timespec helpers");
  atomic_timespec_t a = {0}, b = {0};
  struct timespec t = {1, 2}, o;
  atomic_ts_store(&a, &t);
  atomic_ts_cpy(&b, &a);
  atomic_ts_load(&b, &o);
  assert(o.tv_sec == 1 && o.tv_nsec == 2);
  PRINT_TEST_PASSED();
}

static void test_tz_offset(void) {
  PRINT_TEST_START("tz offset sane");
  int64_t off = tu_get_tz_off();
  PRINT_TEST_INFO("offset=%ld", (long)off);
  assert(off >= -(14 * 3600) && off <= 14 * 3600 && off % 60 == 0);
  PRINT_TEST_PASSED();
}

/* комплексный perf-тест: tu_clock_gettime_local vs stdlib наивная аналогичная реализация */
static void test_perf_overall(void) {
  PRINT_TEST_START("perf: tu_clock_gettime_local vs libc path");

  const int iters = 1e5;
  struct timespec start, end, ts;
  uint64_t sys_ns, fast_ns, fast_mono_ns;
  int i;

  /* naive libc path: clock_gettime REALTIME */
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  for (i = 0; i < iters; i++) {
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm = {0};
    localtime_r(&ts.tv_sec, &tm);
    ts.tv_sec += tm.tm_gmtoff;
  }
  PRINT_TEST_INFO("iter=%d  localtime_sys=%" PRId64 ".%" PRId64 "", i, (int64_t)ts.tv_sec, (int64_t)ts.tv_nsec);

  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  sys_ns = (end.tv_sec - start.tv_sec) * NS_PER_SEC + (end.tv_nsec - start.tv_nsec);

  /* fast local (UTC->local) */
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  for (i = 0; i < iters; i++) {
    tu_clock_gettime_local(&ts);
  }
  PRINT_TEST_INFO("iter=%d  localtime_lib=%" PRId64 ".%" PRId64 "", i, (int64_t)ts.tv_sec, (int64_t)ts.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  fast_ns = (end.tv_sec - start.tv_sec) * NS_PER_SEC + (end.tv_nsec - start.tv_nsec);

  /* fast local mono (UTC->local) */
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  for (i = 0; i < iters; i++) {
    tu_clock_gettime_local_mono(&ts);
  }
  PRINT_TEST_INFO("iter=%d localtime_mono=%" PRId64 ".%" PRId64 "", i, (int64_t)ts.tv_sec, (int64_t)ts.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC_RAW, &end);
  fast_mono_ns = (end.tv_sec - start.tv_sec) * NS_PER_SEC + (end.tv_nsec - start.tv_nsec);

  PRINT_TEST_INFO("avg_sys_ns=%.2f avg_lib_ns=%.2f avg_lib_mono_ns=%.2f", (double)sys_ns / iters, (double)fast_ns / iters, (double)fast_mono_ns / iters);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  tu_init();

  struct {
    const char *name;
    void (*fn)(void);
  } tbl[] = {
      {"msleep_accuracy", test_msleep_accuracy},
      {"msleep_failure", test_msleep_failure},
      {"monotonic_fast_ms", test_monotonic_fast_ms},
      {"clock_fail", test_clock_gettime_failure},
      {"atomic_ts", test_atomic_ts},
      {"tz_offset", test_tz_offset},
      {"perf_overall", test_perf_overall},
  };

  if (argc > 1) {
    for (size_t i = 0; i < ARRAY_SIZE(tbl); i++)
      if (!strcmp(argv[1], tbl[i].name)) {
        tbl[i].fn();
        return 0;
      }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 1;
  }
  for (size_t i = 0; i < ARRAY_SIZE(tbl); i++)
    tbl[i].fn();
  printf(KGRN "====== all timeutil tests passed ======\n" KNRM);
  return 0;
}
