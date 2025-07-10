#ifndef NETLINK_GET_ADDR_LIBNL_GETLINK_H
#define NETLINK_GET_ADDR_LIBNL_GETLINK_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <stdarg.h>      // va_list, va_start(), va_end()
#include <stdio.h>       // printf()
#include <sys/syscall.h> // SYS_gettid
#include <unistd.h>      // syscall()

// #include "../syslog2/syslog2.h"

#include <time.h>

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
  int master_idx;          /* master device */
  int ifla_link_idx;       /* ifla_link index */
  char kind[IFNAMSIZ + 1]; /* vlan, bridge, etc. IFLA_INFO_KIND nested in rtattr
                              IFLA_LINKINFO  */
  bool is_bridge;
  char name[IFNAMSIZ + 1];
  uint8_t ll_addr[ETH_ALEN];
} netdev_item_t;

typedef struct nl_req {
  struct nlmsghdr hdr;
  struct rtgenmsg gen;
} nl_req_s;


typedef void (*syslog2_fn_t)(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, va_list ap);

typedef struct netlink_getlink_mod_init_args_t {
  syslog2_fn_t syslog2_func;
} netlink_getlink_mod_init_args_t;

int netlink_getlink_mod_init(const netlink_getlink_mod_init_args_t *args);

int get_netdev(struct slist_head *list);
netdev_item_t *ll_get_by_index(struct slist_head *list, int index);
void free_netdev_list(struct slist_head *list);

#endif // NETLINK_GET_ADDR_LIBNL_GETLINK_H
