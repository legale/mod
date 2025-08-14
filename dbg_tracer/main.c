#include "dbg_tracer.h"
#include <pthread.h>

static void *thread_fn(void *arg) {
  (void)arg;
  TRACE();
  return NULL;
}

int main(void) {
  tracer_setup();
  pthread_t th;
  pthread_create(&th, NULL, thread_fn, NULL);
  pthread_join(th, NULL);
  tracer_dump_now();
  return 0;
}
