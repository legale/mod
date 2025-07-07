#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#ifdef TESTRUN
#include <errno.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "timeutil.h"

#include "../test_util.h"

#ifdef TESTRUN
static bool fail_clock_gettime = false;
static struct timespec *fake_times = NULL;
static int fake_times_len = 0;
static int fake_times_pos = 0;
static bool fail_nanosleep = false;
static bool fail_time_call = false;

void set_clock_gettime_fail(int enable) { fail_clock_gettime = enable; }
void set_clock_gettime_sequence(struct timespec *seq, int len) {
  fake_times = seq;
  fake_times_len = len;
  fake_times_pos = 0;
}
void reset_clock_gettime_sequence(void) {
  fake_times = NULL;
  fake_times_len = 0;
  fake_times_pos = 0;
  fail_clock_gettime = false;
}

void set_nanosleep_fail(int enable) { fail_nanosleep = enable; }
void set_time_fail(int enable) { fail_time_call = enable; }

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  if (fail_clock_gettime) {
    errno = EINVAL;
    return -1;
  }
  if (fake_times && fake_times_pos < fake_times_len) {
    *tp = fake_times[fake_times_pos++];
    return 0;
  }
  return syscall(SYS_clock_gettime, clk_id, tp);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (fail_nanosleep) {
    if (rem) {
      *rem = *req;
    }
    errno = EINTR;
    return -1;
  }
  return syscall(SYS_nanosleep, req, rem);
}

time_t time(time_t *tloc) {
  if (fail_time_call) {
    if (tloc)
      *tloc = (time_t)-1;
    return (time_t)-1;
  }
  return syscall(SYS_time, tloc);
}
#endif

static void test_msleep_accuracy(void) {
  PRINT_TEST_START("msleep accuracy");

  struct timespec start, end;
  uint64_t elapsed_ms;

  for (uint64_t delay = 1; delay <= 1500; delay *= 10) {
    clock_gettime(CLOCK_MONOTONIC, &start);
    int ret = msleep(delay);
    clock_gettime(CLOCK_MONOTONIC, &end);

    assert(ret == 0);

    elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                 (end.tv_nsec - start.tv_nsec) / 1000000;

    PRINT_TEST_INFO("Requested sleep: %" PRIu64 " ms, actual sleep: %" PRIu64 " ms", delay, elapsed_ms);
    assert(elapsed_ms >= delay);
  }

  PRINT_TEST_PASSED();
}

static void test_msleep_failure(void) {
  PRINT_TEST_START("msleep failure path");
#ifdef TESTRUN
  set_nanosleep_fail(1);
  errno = 0;
  assert(msleep(10) == -1);
  assert(errno == EINTR);
  set_nanosleep_fail(0);
#endif
  PRINT_TEST_PASSED();
}

static void test_tu_clock_monotonic_fast(void) {
  // update offset values
  tu_update_offset();
  PRINT_TEST_START("tu_clock_gettime_monotonic_fast vs CLOCK_MONOTONIC_RAW");

  struct timespec ts_sys1, ts_sys2;
  struct timespec ts_fast1, ts_fast2;
  int ret_sys, ret_fast;

  ret_sys = clock_gettime(CLOCK_MONOTONIC_RAW, &ts_sys1);
  ret_fast = tu_clock_gettime_monotonic_fast(&ts_fast1);
  printf("System CLOCK_MONOTONIC_RAW 1: %ld.%09ld\n", ts_sys1.tv_sec, ts_sys1.tv_nsec);
  printf("Fast monotonic time 1       : %ld.%09ld\n", ts_fast1.tv_sec, ts_fast1.tv_nsec);

  ret_sys = clock_gettime(CLOCK_MONOTONIC_RAW, &ts_sys2);
  ret_fast = tu_clock_gettime_monotonic_fast(&ts_fast2);
  printf("System CLOCK_MONOTONIC_RAW 2: %ld.%09ld\n", ts_sys2.tv_sec, ts_sys2.tv_nsec);
  printf("Fast monotonic time 2       : %ld.%09ld\n", ts_fast2.tv_sec, ts_fast2.tv_nsec);

  assert(ret_sys == 0 && ret_fast == 0);

  int64_t diff1 = (int64_t)ts_fast1.tv_sec * NS_PER_SEC + ts_fast1.tv_nsec -
                  ((int64_t)ts_sys1.tv_sec * NS_PER_SEC + ts_sys1.tv_nsec);
  int64_t diff2 = (int64_t)ts_fast2.tv_sec * NS_PER_SEC + ts_fast2.tv_nsec -
                  ((int64_t)ts_sys2.tv_sec * NS_PER_SEC + ts_sys2.tv_nsec);

  diff1 = diff1 < 0 ? -diff1 : diff1;
  diff2 = diff2 < 0 ? -diff2 : diff2;

  printf("Difference 1 (ns): %" PRId64 "\n", diff1);
  printf("Difference 2 (ns): %" PRId64 "\n", diff2);

  assert(diff1 < (2 * NS_PER_MS));
  assert(diff2 < (2 * NS_PER_MS));

  PRINT_TEST_PASSED();
}

static void test_tu_clock_realtime_fast(void) {
  PRINT_TEST_START("tu_clock_gettime_realtime_fast vs CLOCK_REALTIME");

  struct timespec ts_sys1, ts_sys2;
  struct timespec ts_fast1, ts_fast2;
  int ret_sys, ret_fast;

  ret_sys = clock_gettime(CLOCK_REALTIME, &ts_sys1);
  ret_fast = tu_clock_gettime_realtime_fast(&ts_fast1);
  printf("System CLOCK_REALTIME 1: %ld.%09ld\n", ts_sys1.tv_sec, ts_sys1.tv_nsec);
  printf("Fast realtime time 1   : %ld.%09ld\n", ts_fast1.tv_sec, ts_fast1.tv_nsec);

  ret_sys = clock_gettime(CLOCK_REALTIME, &ts_sys2);
  ret_fast = tu_clock_gettime_realtime_fast(&ts_fast2);
  printf("System CLOCK_REALTIME 2: %ld.%09ld\n", ts_sys2.tv_sec, ts_sys2.tv_nsec);
  printf("Fast realtime time 2   : %ld.%09ld\n", ts_fast2.tv_sec, ts_fast2.tv_nsec);

  assert(ret_sys == 0 && ret_fast == 0);

  int64_t diff1 = (int64_t)ts_fast1.tv_sec * NS_PER_SEC + ts_fast1.tv_nsec -
                  ((int64_t)ts_sys1.tv_sec * NS_PER_SEC + ts_sys1.tv_nsec);
  int64_t diff2 = (int64_t)ts_fast2.tv_sec * NS_PER_SEC + ts_fast2.tv_nsec -
                  ((int64_t)ts_sys2.tv_sec * NS_PER_SEC + ts_sys2.tv_nsec);

  diff1 = diff1 < 0 ? -diff1 : diff1;
  diff2 = diff2 < 0 ? -diff2 : diff2;

  printf("Difference 1 (ns): %" PRId64 "\n", diff1);
  printf("Difference 2 (ns): %" PRId64 "\n", diff2);

  assert(diff1 < (2 * NS_PER_MS));
  assert(diff2 < (2 * NS_PER_MS));

  PRINT_TEST_PASSED();
}

static void test_pause_resume(void) {
  PRINT_TEST_START("pause_start and pause_end effects");

  struct timespec ts_before, ts_during, ts_after;
  int ret;

  ret = tu_clock_gettime_monotonic_fast(&ts_before);
  assert(ret == 0);

  tu_pause_start();
  msleep(100);
  tu_pause_end();

  ret = tu_clock_gettime_monotonic_fast(&ts_during);
  assert(ret == 0);

  msleep(100);

  ret = tu_clock_gettime_monotonic_fast(&ts_after);
  assert(ret == 0);

  uint64_t delta1 = (ts_during.tv_sec - ts_before.tv_sec) * 1000 + (ts_during.tv_nsec - ts_before.tv_nsec) / 1000000;
  uint64_t delta2 = (ts_after.tv_sec - ts_during.tv_sec) * 1000 + (ts_after.tv_nsec - ts_during.tv_nsec) / 1000000;

  PRINT_TEST_INFO("delta with pause (should be ~0): %" PRIu64 " ms", delta1);
  PRINT_TEST_INFO("delta after pause (should be ~100): %" PRIu64 " ms", delta2);

  assert(delta1 < 20);  // Pause time ignored
  assert(delta2 >= 90); // Normal time

  PRINT_TEST_PASSED();
}

static void test_clock_gettime_failure(void) {
  PRINT_TEST_START("tu_clock_gettime failure paths");
#ifdef TESTRUN
  struct timespec ts;
  set_clock_gettime_fail(1);
  assert(tu_clock_gettime_monotonic_fast(&ts) != 0);
  assert(tu_clock_gettime_realtime_fast(&ts) != 0);
  assert(tu_clock_gettime_monotonic_fast_ms() == 0);
  set_clock_gettime_fail(0);
#endif
  PRINT_TEST_PASSED();
}

static void test_pause_branches(void) {
  PRINT_TEST_START("pause_start/pause_end branch coverage");
#ifdef TESTRUN
  struct timespec seq[] = {
      {0, 900000000}, {1, 100000000}, // first pause diff=200ms (sub branch)
      {1, 200000000}, {2, 100000000}  // second pause diff=900ms (sub branch + add overflow)
  };
  set_clock_gettime_sequence(seq, 4);
#endif
  tu_pause_start();       // uses seq[0]
  tu_pause_start();       // already active
  tu_pause_end();         // uses seq[1]
  tu_pause_start();       // uses seq[2]
  tu_pause_end();         // uses seq[3]
  tu_pause_end();         // no-op when not active
#ifdef TESTRUN
  reset_clock_gettime_sequence();
#endif
  PRINT_TEST_PASSED();
}

static void test_monotonic_negative_and_realtime_overflow(void) {
  PRINT_TEST_START("monotonic negative and realtime overflow");
#ifdef TESTRUN
  struct timespec seq_off[] = {{5, 500000000}, {4, 700000000}}; // realtime then monotonic
  set_clock_gettime_sequence(seq_off, 2);
#endif
  tu_update_offset();
#ifdef TESTRUN
  reset_clock_gettime_sequence();
  struct timespec seq_calls[] = {{11, 50000000}, {12, 600000000}, {13, 0}};
  set_clock_gettime_sequence(seq_calls, 3);
#endif
  struct timespec ts;
  assert(tu_clock_gettime_monotonic_fast(&ts) == 0);
  assert(tu_clock_gettime_realtime_fast(&ts) == 0);
  uint64_t ms = tu_clock_gettime_monotonic_fast_ms();
#ifdef TESTRUN
  reset_clock_gettime_sequence();
#endif
  assert(ms > 0);
  PRINT_TEST_PASSED();
}

static void test_atomic_ts_ops(void) {
  PRINT_TEST_START("atomic_ts operations");

  atomic_timespec_t src = {.tv_sec = 0, .tv_nsec = 0};
  atomic_timespec_t dest = {.tv_sec = 0, .tv_nsec = 0};
  struct timespec ts, out;

  ts.tv_sec = 42;
  ts.tv_nsec = 123456789;

  atomic_ts_store(&src, &ts);
  ts.tv_sec = 0;
  ts.tv_nsec = 0;

  atomic_ts_load(&src, &out);
  assert(out.tv_sec == 42 && out.tv_nsec == 123456789);

  atomic_ts_cpy(&dest, &src);
  out.tv_sec = 0;
  out.tv_nsec = 0;
  atomic_ts_load(&dest, &out);
  assert(out.tv_sec == 42 && out.tv_nsec == 123456789);

  ts.tv_sec = 7;
  ts.tv_nsec = 987654321;
  atomic_ts_store(&src, &ts);
  atomic_ts_cpy(&dest, &src);
  atomic_ts_load(&dest, &out);
  assert(out.tv_sec == 7 && out.tv_nsec == 987654321);

  PRINT_TEST_PASSED();
}

static void test_timezone_offset(void) {
  PRINT_TEST_START("tu_get_timezone_offset");

  time_t off = tu_get_timezone_offset();
  PRINT_TEST_INFO("timezone offset: %ld", (long)off);

  assert(off >= -(14 * 3600) && off <= 14 * 3600);
  assert(off % 60 == 0);

  PRINT_TEST_PASSED();
}

static void test_timezone_offset_fail(void) {
  PRINT_TEST_START("tu_get_timezone_offset failure branch");
#ifdef TESTRUN
  set_time_fail(1);
  time_t off = tu_get_timezone_offset();
  assert(off == 0);
  set_time_fail(0);
#else
  (void)tu_get_timezone_offset();
#endif
  PRINT_TEST_PASSED();
}

int main(void) {
  timeutil_mod_init(NULL);
  tu_init();

  test_msleep_accuracy();
  test_msleep_failure();
  test_tu_clock_realtime_fast();
  test_tu_clock_monotonic_fast();
  test_clock_gettime_failure();
  test_pause_resume();
  test_pause_branches();
  test_monotonic_negative_and_realtime_overflow();
  test_atomic_ts_ops();
  test_timezone_offset();
  test_timezone_offset_fail();

  printf(KGRN "====== All timeutil tests passed! ======\n" KNRM);
  return 0;
}
