#include "udeque.h"
#include "../test_util.h"
#include <assert.h>
#include <stdlib.h>

static void test_basic(void) {
  PRINT_TEST_START("basic deque operations");
  deq_t *dq = deque_create();
  assert(dq);
  int a = 10, b = 20;
  deq_push_head(dq, &a);
  deq_push_tail(dq, &b);
  assert(!deq_isempty(dq));
  deq_entry_t *e = deq_get_head(dq);
  assert(*(int *)e->data == 10);
  e = deq_get_tail(dq);
  assert(*(int *)e->data == 20);
  e = deq_pop_head(dq);
  assert(*(int *)e->data == 10);
  free(e);
  e = deq_pop_tail(dq);
  assert(*(int *)e->data == 20);
  free(e);
  assert(deq_isempty(dq));
  deq_free(dq);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {{"basic", test_basic}};
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All udeque tests passed! ======\n" KNRM);
  return rc;
}
