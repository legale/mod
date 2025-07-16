#ifndef _LIB_NL_ARP_CACHE
#define _LIB_NL_ARP_CACHE

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> /* close() */

#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif


/* structure to store arp entries */
typedef struct _arp_cache {
  struct nlmsghdr *nl_hdr;
  struct rtattr *tb[NDA_MAX + 1];
  uint8_t ndm_family;
  uint16_t ndm_state; /* ndmsg structure variable */
} arp_cache;

EXPORT_API void parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, unsigned len);
EXPORT_API ssize_t send_recv(const void *send_buf, size_t send_buf_len, void **recv_buf);
EXPORT_API ssize_t get_arp_cache(void **buf_ptr);
EXPORT_API ssize_t parse_arp_cache(void *buf, ssize_t buf_size, arp_cache cache[]);

#endif /* _LIB_NL_ARP_CACHE */