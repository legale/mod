#include "../syslog2/syslog2.h"
#include "libnl_getlink.h"
#include <errno.h>
#include <stdio.h>
#include <syslog.h>

int main(void) {
  setup_syslog2("main_syslog2", LOG_DEBUG, false);

  struct slist_head list;
  INIT_SLIST_HEAD(&list);
  get_netdev(&list);

  netdev_item_t *item;
  netdev_item_t *master_dev, *link_dev;

  slist_for_each_entry(item, &list, list) {

    if (item->master_idx > 0) {
      master_dev = ll_get_by_index(&list, item->master_idx);
    } else {
      master_dev = NULL;
    }

    if (item->ifla_link_idx > 0) {
      link_dev = ll_get_by_index(&list, item->ifla_link_idx);
    } else {
      link_dev = NULL;
    }

    uint8_t *addr_raw = item->ll_addr;
    printf("%3d: "                                 // индекс (3 символа)
           "master: %3d %-10s "                    // master id и имя (3 знака и 10 символов)
           "ifla_link: %3d %-10s "                 // ifla_link_idx и имя (3 знака и 10 символов)
           "is_bridge: %-5d "                      // is_bridge (5 символов)
           "kind: %-15s "                          // kind (15 символов)
           "name: %-15s "                          // name (15 символов)
           "MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", // MAC-адрес (стандартный формат)
           item->index,
           item->master_idx, master_dev ? master_dev->name : "EMPTY",
           item->ifla_link_idx, link_dev ? link_dev->name : "",
           item->is_bridge,
           item->kind, item->name,
           addr_raw[0], addr_raw[1], addr_raw[2], addr_raw[3], addr_raw[4], addr_raw[5]);
  }
  // free_netdev_list(&list);
  return 0;
}
