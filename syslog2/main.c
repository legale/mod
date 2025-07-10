// main.c
#include "syslog2.h"
#include <stdio.h>

#define syslog syslog2

int main(void) {
  setup_syslog2("main_syslog2", LOG_DEBUG, false);

  syslog(LOG_NOTICE, "notice from main");
  syslog(LOG_INFO, "info from main");
  syslog(LOG_ERR, "error message example");
  syslog(LOG_DEBUG, "debug message example");

  return 0;
}
