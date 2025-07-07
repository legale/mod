#include "libnl_getlink.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  netlink_getlink_mod_init(NULL);
  struct slist_head list;
  INIT_SLIST_HEAD(&list);

  int ret = get_netdev(&list);
  assert(ret == 0 && "get_netdev should succeed");
  assert(slist_empty(&list) == 0 && "netdev list should not be empty");

  free_netdev_list(&list);
  printf("====== All netlink_getlink tests passed! ======\n");
  return 0;
}
