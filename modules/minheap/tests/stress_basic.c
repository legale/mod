#include "tap.h"
#include "minheap.h"
#include <stdlib.h>

int run_stress_tests(void){
  minheap_t *h = mh_create(20000);
  for(int i=0;i<10000;i++){
    minheap_node_t *n = malloc(sizeof(*n));
    n->key = (uint64_t)i;
    mh_insert(h, n);
  }
  tap_plan(1);
  tap_ok(mh_get_size(h) == 10000, "stress insert");
  mh_free(h);
  return tap_status();
}
