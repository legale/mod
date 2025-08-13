#include "tap.h"
#include "minheap.h"
#include <pthread.h>
#include <stdlib.h>

static void *worker(void *arg){
  minheap_t *h = arg;
  for(int i=0;i<1000;i++){
    minheap_node_t *n = malloc(sizeof(*n));
    n->key = (uint64_t)i;
    mh_insert(h, n);
  }
  return NULL;
}

int run_thread_tests(void){
  minheap_t *h = mh_create(8192);
  pthread_t th[4];
  for(int i=0;i<4;i++) pthread_create(&th[i], 0, worker, h);
  for(int i=0;i<4;i++) pthread_join(th[i], 0);
  tap_plan(1);
  tap_ok(mh_get_size(h) == 4000, "concurrent insert");
  mh_free(h);
  return tap_status();
}
