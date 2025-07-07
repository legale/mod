#ifndef NETLINK_GET_ADDR_LIBNL_GETLINK_H
#define NETLINK_GET_ADDR_LIBNL_GETLINK_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <stdarg.h>      // va_list, va_start(), va_end()
#include <stdio.h>       // printf()
#include <sys/syscall.h> // SYS_gettid
#include <unistd.h>      // syscall()

#include "../syslog2/syslog2.h"

#include "slist.h"

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#define NLMSG_TAIL(nmsg) \
  ((struct rtattr *)(((void *)(nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

typedef struct netdev_item {
  struct slist_node list;
  int index;
  int master_idx;              /* master device */
  int ifla_link_idx;       /* ifla_link index */
  char kind[IFNAMSIZ + 1]; /* vlan, bridge, etc. IFLA_INFO_KIND nested in rtattr IFLA_LINKINFO  */
  bool is_bridge;
  char name[IFNAMSIZ + 1];
  uint8_t ll_addr[ETH_ALEN];
} netdev_item_t;

typedef struct nl_req {
  struct nlmsghdr hdr;
  struct rtgenmsg gen;
} nl_req_s;

int get_netdev(struct slist_head *list);
netdev_item_t *ll_get_by_index(struct slist_head *list, int index);
void free_netdev_list(struct slist_head *list);

typedef struct {
  void *(*malloc_fn)(size_t);
  void (*free_fn)(void *);
  void (*log_fn)(const char *, ...);
} netlink_getlink_mod_init_args_t;

void netlink_getlink_mod_init(const netlink_getlink_mod_init_args_t *args);

#endif // NETLINK_GET_ADDR_LIBNL_GETLINK_H
