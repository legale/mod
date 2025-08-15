// test.c
#include "syslog2.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define THREAD_COUNT 4



void *thread_func(void *arg) {
  int id = (int)(intptr_t)arg;
  for (int i = 0; i < 5; i++) {
    syslog2(LOG_INFO, "thread %d iteration %d", id, i);
    usleep(10000);
  }

  return NULL;
}

void test_auto_init(void) {
  syslog2(LOG_NOTICE, "auto-init test notice");
  syslog2(LOG_INFO, "auto-init test info");
}

void test_setup_syslog2(void) {
  setup_syslog2("syslog2_test", LOG_DEBUG, true);
  syslog2(LOG_ERR, "error after setup_syslog2");
  syslog2(LOG_DEBUG, "debug after setup_syslog2");
}

void test_no_syslog(void) {
  setup_syslog2("syslog2_test", LOG_INFO, false);
  syslog2(LOG_WARNING, "warning with syslog disabled");
}

void test_threads(void) {
  pthread_t threads[THREAD_COUNT];

  for (int i = 0; i < THREAD_COUNT; i++) {
    pthread_create(&threads[i], NULL, thread_func, (void *)(intptr_t)i);
  }
  for (int i = 0; i < THREAD_COUNT; i++) {
    pthread_join(threads[i], NULL);
  }
}



int main(void) {
  syslog2(LOG_NOTICE, "starting syslog2 tests");

  test_auto_init();
  test_setup_syslog2();
  test_no_syslog();
  test_threads();

  syslog2(LOG_NOTICE, "finished syslog2 tests");
  return 0;
}
