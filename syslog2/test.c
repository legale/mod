// test.c
#include "syslog2.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  char buf[16];

  setup_syslog2("test_syslog2", LOG_DEBUG, false);

  /* Check that log mask filtering works */
  setlogmask2(LOG_UPTO(LOG_INFO));
  syslog2(LOG_DEBUG, "This debug should be filtered");
  setlogmask2(LOG_UPTO(LOG_DEBUG));

  syslog2(LOG_INFO, "Starting test");
  syslog2(LOG_DEBUG, "Debug message: %d", 42);
  syslog2_nonl(LOG_WARNING, "Warning message without newline");
  syslog2(LOG_WARNING, " - continued");

  /* Set explicit thread name and verify */
  inline_pthread_set_name("syslog2_test", strlen("syslog2_test"));
  pthread_getname_np(pthread_self(), buf, sizeof(buf));
  syslog2(LOG_INFO, "Thread name: %s", buf);

  /* Also test the macro version */
  PTHREAD_SET_NAME;
  pthread_getname_np(pthread_self(), buf, sizeof(buf));
  syslog2(LOG_INFO, "Thread name macro: %s", buf);

  /* Simulate function tracking */
  SET_CURRENT_FUNCTION();
  syslog2(LOG_INFO, "Function tracking test");

  /* printf style */
  syslog2_printf(LOG_NOTICE, "printf test\n");

  /* debug output */
  debug("debug output\n");

  print_last_functions();

  return 0;
}
