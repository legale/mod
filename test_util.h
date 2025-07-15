#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif //_POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE

#include <stdio.h>
#include <string.h>

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KCYN "\x1B[36m"

#define PRINT_TEST_START(name) \
  printf(KCYN "%s:%d --- Starting Test: %s ---\n" KNRM, __FILE__, __LINE__, name)
#define PRINT_TEST_PASSED() printf(KGRN "--- Test Passed ---\n\n" KNRM)
#define PRINT_TEST_INFO(fmt, ...) \
  printf(KYEL "%s:%d [INFO] " fmt "\n" KNRM, __FILE__, __LINE__, ##__VA_ARGS__)

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

struct test_entry {
  const char *name;
  void (*fn)(void);
};

static inline int run_named_test(const char *name,
                                 const struct test_entry *tests,
                                 size_t count) {
  if (name) {
    for (size_t i = 0; i < count; i++) {
      if (strcmp(name, tests[i].name) == 0) {
        tests[i].fn();
        return 0;
      }
    }
    fprintf(stderr, "Unknown test: %s\n", name);
    return 1;
  }

  for (size_t i = 0; i < count; i++) {
    tests[i].fn();
  }
  return 0;
}

#endif /* TEST_UTIL_H */
