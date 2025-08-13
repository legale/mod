#include "tap.h"
#include "minheap.h"

static int test_insert_extract(void){
  minheap_t *h = mh_create(4);
  minheap_node_t n1 = { .key = 1 };
  minheap_node_t n2 = { .key = 2 };
  mh_insert(h, &n2);
  mh_insert(h, &n1);
  minheap_node_t *m = mh_extract_min(h);
  tap_ok(m == &n1, "extract min");
  mh_free(h);
  return 0;
}

int run_unit_tests(void){
  tap_plan(1);
  test_insert_extract();
  return tap_status();
}
