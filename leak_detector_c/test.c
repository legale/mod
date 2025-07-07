#include "leak_detector.h"
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "../test_util.h"

static void test_leak_detector(void) {
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
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {{"leak_detector", test_leak_detector}};
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All leak_detector_c tests passed! ======\n" KNRM);
  return rc;
}

