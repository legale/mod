#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#include "timeutil.h"

#define KNRM "\x1B[0m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KCYN "\x1B[36m"

#define PRINT_TEST_START(name) \
  printf(KCYN "%s:%d --- Starting Test: %s ---\n" KNRM, __FILE__, __LINE__, name)
#define PRINT_TEST_PASSED() printf(KGRN "--- Test Passed ---\n\n" KNRM)
#define PRINT_TEST_INFO(fmt, ...) \
  printf(KYEL "%s:%d [INFO] " fmt "\n" KNRM, __FILE__, __LINE__, ##__VA_ARGS__)

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

#ifdef TESTRUN
  /* manipulate offset to trigger overflow path */
  tu_set_offset_nsec(NS_PER_SEC - 1);
  struct timespec tmp;
  tu_clock_gettime_realtime_fast(&tmp);
  tu_update_offset();
#endif

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

static void test_pause_edge_cases(void) {
  PRINT_TEST_START("pause edge cases");

  struct timespec ts_before, ts_after;
  int ret = tu_clock_gettime_monotonic_fast(&ts_before);
  assert(ret == 0);

  /* calling end without start should have no effect */
  tu_pause_end();

  /* pause crossing a second boundary to hit negative tv_nsec path */
  struct timespec raw;
  clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
  uint64_t to_boundary_ms = (NS_PER_SEC - raw.tv_nsec) / NS_PER_MS + 5;

  tu_pause_start();
  tu_pause_start(); /* repeated start should be ignored */
  msleep(to_boundary_ms);
  tu_pause_end();
  tu_pause_end(); /* repeated end should be ignored */

  /* second pause to trigger timespec_add overflow */
  tu_pause_start();
  msleep(990);
  tu_pause_end();

  ret = tu_clock_gettime_monotonic_fast(&ts_after);
  assert(ret == 0);

  uint64_t delta = (ts_after.tv_sec - ts_before.tv_sec) * 1000 +
                   (ts_after.tv_nsec - ts_before.tv_nsec) / 1000000;
  PRINT_TEST_INFO("delta with multiple pauses: %" PRIu64 " ms", delta);
  assert(delta < 20);

  PRINT_TEST_PASSED();
}

static void test_atomic_timespec(void) {
  PRINT_TEST_START("atomic timespec helpers");

  struct timespec ts = {123, 456789};
  atomic_timespec_t at1 = {0}, at2 = {0};
  struct timespec out;

  atomic_ts_store(&at1, &ts);
  atomic_ts_load(&at1, &out);
  assert(out.tv_sec == ts.tv_sec && out.tv_nsec == ts.tv_nsec);

  atomic_ts_cpy(&at2, &at1);
  atomic_ts_load(&at2, &out);
  assert(out.tv_sec == ts.tv_sec && out.tv_nsec == ts.tv_nsec);

  PRINT_TEST_PASSED();
}

static void test_tu_clock_gettime_monotonic_fast_ms_case(void) {
  PRINT_TEST_START("tu_clock_gettime_monotonic_fast_ms accuracy");

  uint64_t t1 = tu_clock_gettime_monotonic_fast_ms();
  msleep(100);
  uint64_t t2 = tu_clock_gettime_monotonic_fast_ms();

  PRINT_TEST_INFO("elapsed ms: %" PRIu64, t2 - t1);
  assert(t2 - t1 >= 100);

#ifdef TESTRUN
  tu_set_fail_clock(true);
  assert(tu_clock_gettime_monotonic_fast_ms() == 0);
  tu_set_fail_clock(false);
#endif

  PRINT_TEST_PASSED();
}

static void test_tu_diff_ts_case(void) {
  PRINT_TEST_START("tu_diff_ts branches");

  struct timespec start = {1, 900000000}, end = {3, 100000000}, diff;
  tu_diff_ts(&diff, &start, &end);
  assert(diff.tv_sec == 1 && diff.tv_nsec == 200000000);

  start.tv_sec = 2; start.tv_nsec = 100000000;
  end.tv_sec = 3; end.tv_nsec = 300000000;
  tu_diff_ts(&diff, &start, &end);
  assert(diff.tv_sec == 1 && diff.tv_nsec == 200000000);

#ifdef TESTRUN
  /* trigger else branch inside timeutil.c for coverage */
  tu_trigger_diff_ts_else();
#endif

  PRINT_TEST_PASSED();
}

int main(void) {
  tu_init();

  test_msleep_accuracy();
  test_tu_clock_realtime_fast();
  test_tu_clock_monotonic_fast();
  test_pause_resume();

  /* new tests */
  test_atomic_timespec();
  test_pause_edge_cases();
  test_tu_clock_gettime_monotonic_fast_ms_case();
  test_tu_diff_ts_case();

  printf(KGRN "====== All timeutil tests passed! ======\n" KNRM);
  return 0;
}
