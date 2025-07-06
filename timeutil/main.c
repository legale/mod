#include "timeutil.h"
#include <inttypes.h>
#include <stdio.h>

int main(void) {
  tu_init();

  printf("TIME FUNCTIONS DEMONSTRATION\n\n");
  printf("tu_clock_gettime_monotonic_fast_ms():\n");
  uint64_t t1 = tu_clock_gettime_monotonic_fast_ms();
  printf("Current monotonic time (ms): %" PRIu64 "\n", t1);

  printf("Sleeping 200 ms...\n");
  msleep(200);

  uint64_t t2 = tu_clock_gettime_monotonic_fast_ms();
  printf("tu_clock_gettime_monotonic_fast_ms Monotonic time after sleep (ms): %" PRIu64 "\n", t2);
  printf("Elapsed time (ms): %" PRIu64 "\n\n", t2 - t1);

  printf("pause_start and pause_end:\n");
  printf("Starting pause...\n");
  tu_pause_start();
  msleep(300);
  printf("Ending pause...\n");
  tu_pause_end();

  uint64_t t3 = tu_clock_gettime_monotonic_fast_ms();
  printf("tu_clock_gettime_monotonic_fast_ms Monotonic time after pause (ms): %" PRIu64 "\n", t3);
  printf("time since prev. check excluded pause (expected 0 ms): %" PRIu64 "\n", t3 - t2);

  return 0;
}
