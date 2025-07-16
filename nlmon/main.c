#include "nlmon.h"
#include "../syslog2/syslog2.h"

#include <stdint.h>
#include <syslog.h>

// Колбек для обработки событий
void my_cb(const char *ifname, uint32_t event, void *arg) {
  (void)arg;
  const char *evstr = (event == NLMON_EVENT_LINK_UP) ? "UP" : "DOWN";
  syslog2(LOG_INFO, "THIS IS CALLBACK!!! EVENT: interface=%s state=%s", ifname, evstr);
}

int main(void) {
  // Список интерфейсов для мониторинга (NULL-терминированный)
  const char *ifaces[] = {"eth0", "wlan0", NULL};

  // Описываем фильтр: какие интерфейсы, какие события, какой колбек
  nlmon_filter_t filter = {
      .ifnames = ifaces,
      .events = NLMON_EVENT_LINK_UP | NLMON_EVENT_LINK_DOWN,
      .cb = my_cb,
      .arg = NULL,
  };

  int timeout_sec = 10; // мониторить 10 секунд

  syslog2(LOG_INFO, "Monitoring interfaces for %d seconds...", timeout_sec);

  if (nlmon_add_filter(&filter) != 0) {
    syslog2(LOG_ERR, "Failed to add filter");
    return 1;
  }

  nlmon_list_filters();

  int rc = nlmon_run(timeout_sec);
  if (rc == 0) {
    syslog2(LOG_INFO, "Monitoring finished.");
  } else {
    syslog2(LOG_ERR, "Monitoring failed.");
  }
  return rc;
}