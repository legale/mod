// test.c
#include "syslog2.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  setup_syslog2("test_syslog2", LOG_DEBUG, false);

  syslog2(LOG_INFO, "Starting test");
  syslog2(LOG_DEBUG, "Debug message: %d", 42);
  syslog2_nonl(LOG_WARNING, "Warning message without newline");
  syslog2(LOG_WARNING, " - continued");

  PTHREAD_SET_NAME;

  // Simulate function tracking
  SET_CURRENT_FUNCTION();
  syslog2(LOG_INFO, "Function tracking test");

  return 0;
}
