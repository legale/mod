#include "leak_detector.h"
#include <stdio.h>
#include "../test_util.h"

int main(void) {
#ifdef LEAKCHECK
  char *buf = malloc(32);
  (void)buf;
  report_mem_leak();
  printf(KGRN "Leak detection finished\n" KNRM);
#else
  printf(KGRN "Leak detector disabled\n" KNRM);
#endif
  return 0;
}

