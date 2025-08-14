#include "dbg_tracer.h"
#include <pthread.h>

static void *thread_fn(void *arg) {
  (void)arg;
  trace_reg_attach();
  return NULL;
}

int main(void) {
  tracer_setup();
  pthread_t th;
  pthread_create(&th, NULL, thread_fn, NULL);
  pthread_join(th, NULL);
  return 0;
}
