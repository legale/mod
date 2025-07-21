#include "../syslog2/syslog2.h"
#include "nlmon.h"

#include <stdint.h>
#include <syslog.h>
#include <unistd.h>

// Колбек для обработки событий
void my_cb(const char *ifname, uint32_t event, void *arg) {
  (void)arg;
  syslog2(LOG_INFO, "THIS IS CALLBACK!!! EVENT: interface=%s state=0x%02x", ifname, event);
}

int main(void) {
  syslog2(LOG_INFO, "Starting interface monitor...");

  if (nlmon_run() != 0) {
    syslog2(LOG_ERR, "Failed to start monitoring");
    return 1;
  }

  syslog2(LOG_INFO, "Monitor started. Adding filters...");

  int ifaces[] = {
      if_nametoindex("eth0"),
      if_nametoindex("wlan0"),
      if_nametoindex("veth0"),
      if_nametoindex("veth1"),
  };
  nlmon_filter_t filter = {
      .events = NLMON_EVENT_LINK_UP,
      .cb = my_cb,
      .arg = NULL,
  };
  memcpy(filter.idx, ifaces, sizeof(ifaces));

  nlmon_filter_t filter2 = {
      .events = NLMON_EVENT_LINK_DOWN,
      .cb = my_cb,
      .arg = NULL,
  };
  memcpy(filter.idx, ifaces, sizeof(ifaces));

  if (nlmon_add_filter(&filter) != 0) {
    syslog2(LOG_ERR, "Failed to add filter");
    nlmon_stop();
    return 1;
  }

  if (nlmon_add_filter(&filter2) != 0) {
    syslog2(LOG_ERR, "Failed to add filter2");
    nlmon_stop();
    return 1;
  }

  nlmon_list_filters();
  int timeout = 300;
  syslog2(LOG_INFO, "Monitoring for %d seconds...", timeout);
  sleep(timeout);

  syslog2(LOG_INFO, "Stopping monitor...");
  nlmon_stop();

  syslog2(LOG_INFO, "Monitoring finished.");
  return 0;
}
