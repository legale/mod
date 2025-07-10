// #include "../syslog2/syslog2.h"
#include "libnl_getlink.h"
#include <errno.h>
#include <stdio.h>
#include <syslog.h>




int main(void) {
  // setup_syslog2("main_syslog2", LOG_DEBUG, false);

  struct slist_head list;
  INIT_SLIST_HEAD(&list);

  if (get_netdev(&list) != 0) {
    printf("failed to get devices\n");
    return 1;
  }

  netdev_item_t *item;
  slist_for_each_entry(item, &list, list) {
    unsigned char *a = item->ll_addr;
    printf("%3d: name: %-15s MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           item->index, item->name,
           a[0], a[1], a[2], a[3], a[4], a[5]);
  }

  free_netdev_list(&list);
  return 0;
}
