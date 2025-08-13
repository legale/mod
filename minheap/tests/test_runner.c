#include "tap.h"

#include <stdio.h>

typedef int (*test_fn)(void);

typedef struct {
  const char *name;
  test_fn fn;
} test_case_t;

#ifdef TEST_THREAD
int test_thread_push_pop(void);
static test_case_t tests[] = {
  {"thread_push_pop", test_thread_push_pop},
};
#elif defined(TEST_STRESS)
int test_stress_random(void);
static test_case_t tests[] = {
  {"stress_random", test_stress_random},
};
#else
int test_create_free(void);
int test_insert_extract(void);
int test_insert_overflow(void);
int test_delete_node(void);
int test_get_min(void);
int test_get_node(void);
int test_is_empty_size(void);
static test_case_t tests[] = {
  {"create_free", test_create_free},
  {"insert_extract", test_insert_extract},
  {"insert_overflow", test_insert_overflow},
  {"delete_node", test_delete_node},
  {"get_min", test_get_min},
  {"get_node", test_get_node},
  {"is_empty_size", test_is_empty_size},
};
#endif

int main(void)
{
  int n = sizeof(tests) / sizeof(tests[0]);
  tap_plan(n);
  FILE *xml = fopen("test-results.xml", "w");
  if (xml) fprintf(xml, "<testsuite tests=\"%d\">\n", n);
  int fail = 0;
  for (int i = 0; i < n; i++) {
    int r = tests[i].fn();
    tap_ok(i + 1, r == 0, tests[i].name);
    if (xml) {
      if (r == 0) {
        fprintf(xml, "  <testcase name=\"%s\"/>\n", tests[i].name);
      } else {
        fprintf(xml, "  <testcase name=\"%s\"><failure/></testcase>\n", tests[i].name);
      }
    }
    if (r != 0) fail++;
  }
  if (xml) {
    fprintf(xml, "</testsuite>\n");
    fclose(xml);
  }
  return fail ? 1 : 0;
}
