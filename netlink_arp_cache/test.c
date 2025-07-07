#include "libnlarpcache.h"

#include <assert.h>
#include <linux/rtnetlink.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  netlink_arp_cache_mod_init(NULL);
  unsigned char buf[256];

  struct rtattr *rta1 = (struct rtattr *)buf;
  rta1->rta_type = NDA_DST;
  rta1->rta_len = RTA_LENGTH(4);
  memset(RTA_DATA(rta1), 0xaa, 4);

  struct rtattr *rta2 = (struct rtattr *)(buf + RTA_SPACE(4));
  rta2->rta_type = NDA_LLADDR;
  rta2->rta_len = RTA_LENGTH(6);
  memset(RTA_DATA(rta2), 0xbb, 6);

  unsigned len = RTA_SPACE(4) + RTA_SPACE(6);
  struct rtattr *tb[NDA_MAX + 1] = {0};
  parse_rtattr(tb, NDA_MAX, rta1, len);

  assert(tb[NDA_DST] == rta1);
  assert(tb[NDA_LLADDR] == rta2);

  printf("All tests passed\n");
  return 0;
}
