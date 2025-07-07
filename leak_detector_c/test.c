#include "leak_detector.h"
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "../test_util.h"

int main(void) {
  leak_detector_mod_init(NULL);
#ifdef LEAKCHECK
  PRINT_TEST_START("basic leak detector");
  char *tmp = malloc(10);
  (void)tmp;
  report_mem_leak();
  int fd = open(OUTPUT_FILE, O_RDONLY);
  assert(fd != -1);
  close(fd);
  PRINT_TEST_PASSED();
#else
  PRINT_TEST_START("leak detector disabled");
  PRINT_TEST_PASSED();
#endif
  return 0;
}

