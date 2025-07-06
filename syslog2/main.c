// main.c
#include "syslog2.h"
#include <stdio.h>

int main(void) {
  setup_syslog2("main_syslog2", LOG_DEBUG, false);

  syslog2(LOG_NOTICE, "notice from main");
  syslog2(LOG_INFO, "info from main");
  syslog2(LOG_ERR, "error message example");
  syslog2(LOG_DEBUG, "debug message example");

  return 0;
}
