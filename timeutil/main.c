#include "timeutil.h"
#include <inttypes.h>
#include <stdio.h>

int main(void) {
  tu_init();

  printf("TIME FUNCTIONS DEMONSTRATION\n\n");
  printf("tu_clock_gettime_monotonic_fast_ms():\n");
  uint64_t t1 = tu_clock_gettime_monotonic_ms();
  printf("Current monotonic time (ms): %" PRIu64 "\n", t1);

  printf("Sleeping 200 ms...\n");
  msleep(200);

  uint64_t t2 = tu_clock_gettime_monotonic_ms();
  printf("tu_clock_gettime_monotonic_fast_ms Monotonic time after sleep (ms): %" PRIu64 "\n", t2);
  printf("Elapsed time (ms): %" PRIu64 "\n\n", t2 - t1);

  uint64_t t3 = tu_clock_gettime_monotonic_ms();
  printf("tu_clock_gettime_monotonic_fast_ms: %" PRIu64 "\n", t3);
  printf("time since prev. check excluded pause (expected 0 ms): %" PRIu64 "\n", t3 - t2);

  uint64_t tz_off = tu_get_cached_tz_off();
  printf("tz_offset=%" PRIu64 "\n", tz_off);

  struct timespec ts;
  time_t now = time(NULL);
  struct tm tm = {0};
  localtime_r(&now, &tm);
  time_t now_local = now + tm.tm_gmtoff;
  tu_clock_gettime_local(&ts);

  time_t now2 = ts.tv_sec;
  printf("now=%" PRId64 " now_local=%" PRId64 " now2=%" PRId64 " diff=%" PRId64 "\n", (int64_t)now, (int64_t)now_local, (int64_t)now2, (int64_t)now2 - (int64_t)now_local);

  return 0;
}
