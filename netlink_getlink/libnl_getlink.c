
#include "slist.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <net/if_arp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h> // fchmod
#include <sys/time.h> /* timeval_t struct */
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "libnl_getlink.h"

// logger fallback
#ifdef IS_DYNAMIC_LIB
#include "../syslog2/syslog2.h" // жёсткая зависимость
#endif

#ifndef FUNC_START_DEBUG
#define FUNC_START_DEBUG syslog2(LOG_DEBUG, "START")
#endif

#ifndef IS_DYNAMIC_LIB
#define syslog2(pri, fmt, ...) syslog2_(pri, __func__, __FILE__, __LINE__, fmt, true, ##__VA_ARGS__)

__attribute__((weak)) void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...) {
  char buf[4096];
  size_t sz = sizeof(buf);
  va_list ap;

  va_start(ap, nl);
  int len = snprintf(buf, sz, "[%d] %s:%d %s: ", pri, file, line, func);
  len += vsnprintf(buf + len, sz - len, fmt, ap);
  va_end(ap);

  // ограничиваем длину если переполнено
  if (len >= (int)sz) len = sz - 1;

  // добавляем \n если нужно
  if (nl && len < (int)sz - 1) buf[len++] = '\n';

  ssize_t written = write(STDOUT_FILENO, buf, len);
  (void)written;
}
#endif // IS_DYNAMIC_LIB
// logger END

#define parse_rtattr_nested(tb, max, rta) \
  (parse_rtattr((tb), (max), RTA_DATA(rta), RTA_PAYLOAD(rta)))

static int parse_rtattr_flags(struct rtattr *tb[], int max, struct rtattr *rta, int len, unsigned short flags) {
  unsigned short type;
  if (!tb || max <= 0) return -1; // паранойя: защита от неинициализированных массивов
  while (RTA_OK(rta, len)) {
    type = rta->rta_type & ~flags;
    if (type > 0 && type <= max && !tb[type])
      tb[type] = rta;
    rta = RTA_NEXT(rta, len);
  }
  if (len) syslog2(LOG_ERR, "!!!deficit %d", len);
  return 0;
}

static int parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta,
                        int len) {
  return parse_rtattr_flags(tb, max, rta, len, 0);
}

// parse netlink message
static ssize_t parse_nlbuf(struct nlmsghdr *nh, struct rtattr **tb) {
  // FUNC_START_DEBUG;
  if (!nh || !tb) return -1;                                    // paranoia guard
  unsigned int nlen = nh->nlmsg_len;                            // total nl msg len incl header
  if (nlen < NLMSG_LENGTH(sizeof(struct ifinfomsg))) return -1; // header too short

  struct ifinfomsg *msg = NLMSG_DATA(nh); // ptr right after header
  (void)msg;                              // msg is not used besides type in caller

  uint32_t msg_len = NLMSG_LENGTH(sizeof(*msg)); // aligned hdr+ifinfo
  if (nlen < msg_len) return -1;                 // inconsistent lengths

  char *p = (char *)nh;                                // base ptr
  struct rtattr *rta = (struct rtattr *)(p + msg_len); // first rta
  unsigned int alen = nlen - msg_len;                  // attrs payload len
  if (alen > 0) parse_rtattr(tb, IFLA_MAX, rta, (int)alen);
  return nlen;
}

static int addattr_l(struct nlmsghdr *n, uint32_t maxlen, int type, const void *data, uint32_t alen) {
  // minimal guards to avoid segv without bloating code
  if (!n) return -1;            // null nlmsg
  if (alen && !data) return -1; // nonzero len with null data

  uint32_t len = RTA_LENGTH(alen);
  if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
    syslog2(LOG_ERR, "addattr_l ERROR: message exceeded bound of %u", maxlen);
    return -1;
  }
  struct rtattr *rta = NLMSG_TAIL(n);
  rta->rta_type = type;
  rta->rta_len = len;
  if (alen) memcpy(RTA_DATA(rta), data, alen);
  n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
  return 0;
}

static int addattr32(struct nlmsghdr *n, unsigned int maxlen, int type, uint32_t data) {
  return addattr_l(n, maxlen, type, &data, sizeof(uint32_t));
}

void free_netdev_list(struct slist_head *list) {
  FUNC_START_DEBUG;
  if (!list) return; // guard
  netdev_item_t *item = NULL;
  netdev_item_t *tmp = NULL;

  slist_for_each_entry_safe(item, tmp, list, list) {
    slist_del_node(&item->list, list);
    free(item);
  }
}

netdev_item_t *ll_get_by_index(struct slist_head *list, int index) {
  // FUNC_START_DEBUG;
  if (!list) return NULL; // guard
  netdev_item_t *item;
  slist_for_each_entry(item, list, list) {
    if (item->index == index) return item;
  }
  return NULL;
}

static int send_msg() {
  // FUNC_START_DEBUG;
  ssize_t status;
  struct {
    struct nlmsghdr nlh;
    struct ifinfomsg m;
    char buf[256]; // fixed-size buffer, not char* to avoid UB
  } req = {
      .nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
      .nlh.nlmsg_type = RTM_GETLINK,
      .nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ACK,
      .nlh.nlmsg_pid = 0,
      .nlh.nlmsg_seq = 1,
  };

  if (addattr32(&req.nlh, sizeof(req), IFLA_EXT_MASK, RTEXT_FILTER_VF) != 0) {
    syslog2(LOG_ERR, "%s addattr32", strerror(errno));
    return -1;
  }

  int sd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sd < 0) {
    syslog2(LOG_ERR, "%s socket()", strerror(errno));
    return -1;
  }

  if (fchmod(sd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0) {
    syslog2(LOG_WARNING, "%s fchmod()", strerror(errno));
  }

  int flags = fcntl(sd, F_GETFL, 0);
  if (flags != -1) fcntl(sd, F_SETFL, flags | O_NONBLOCK);

  struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
  if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    syslog2(LOG_ERR, "%s setsockopt()", strerror(errno));
    close(sd);
    return -2;
  }

  status = send(sd, &req, req.nlh.nlmsg_len, 0);
  if (status < 0) {
    syslog2(LOG_ERR, "send() error=%s", strerror(errno));
    close(sd);
    return -1;
  }
  return sd;
}

static ssize_t recv_msg(int sd, void **buf) {
  // FUNC_START_DEBUG;
  if (sd < 0 || !buf) return -1;

  ssize_t bufsize = 512;
  *buf = malloc(bufsize);
  if (!*buf) return -1;

  struct iovec iov = {.iov_base = *buf, .iov_len = bufsize};
  struct sockaddr_nl sa;
  struct msghdr msg = {
      .msg_name = &sa,
      .msg_namelen = sizeof(sa),
      .msg_iov = &iov,
      .msg_iovlen = 1,
  };

  fd_set readset;
  FD_ZERO(&readset);
  FD_SET(sd, &readset);
  struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
  int ret = select(sd + 1, &readset, NULL, NULL, &timeout);
  if (ret == 0) return 0; // timeout
  if (ret < 0) {
    if (errno == EINTR) {
      syslog2(LOG_WARNING, "select EINTR");
    } else {
      syslog2(LOG_ERR, "select error=%s", strerror(errno));
    }
    return ret;
  }

  ssize_t len = recvmsg(sd, &msg, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
  if (len <= 0) return len;

  if (len > bufsize) {
    void *newbuf = realloc(*buf, len);
    if (!newbuf) {
      free(*buf);
      *buf = NULL;
      return -1;
    }
    *buf = newbuf;
    bufsize = len;
    iov.iov_base = *buf;
    iov.iov_len = bufsize;
  }

  len = recvmsg(sd, &msg, MSG_DONTWAIT);
  return len;
}

static int fill_from_rta(void *dst, size_t dst_sz, struct rtattr *rta) {
  // copy payload from rta into dst with bounds and basic sanity checks
  if (!dst || !rta || dst_sz == 0) return -1;

  unsigned short rta_len = rta->rta_len;
  if (rta_len < RTA_LENGTH(0)) return -1; // invalid length

  size_t plen = rta_len - RTA_LENGTH(0);
  size_t n = plen < dst_sz ? plen : dst_sz;

  if (n) memcpy(dst, RTA_DATA(rta), n);
  if (n < dst_sz) memset((char *)dst + n, 0, dst_sz - n); // zero tail (helps strings)

  return 0;
}

// return 1 if msg is not finished yet,
// 0 - on NLMSG_DONE,
// -EINVAL on NLMSG_ERROR
static int parse_recv_chunk(void *buf, ssize_t len, struct slist_head *list) {
  FUNC_START_DEBUG;
  if (!buf || !list || len <= 0) {
    syslog2(LOG_ERR, "parse_recv_chunk: invalid args");
    return -1;
  }

  size_t counter = 0;
  struct nlmsghdr *nh;

  for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
    if (counter++ > 100) {
      syslog2(LOG_ALERT, "counter=%zu > 100", counter);
      break;
    }

    if (nh->nlmsg_type == NLMSG_ERROR) return -EINVAL;
    if (nh->nlmsg_type == NLMSG_DONE) return 0;

    struct rtattr *tb[IFLA_MAX + 1] = {0};
    if (parse_nlbuf(nh, tb) < 0) continue;

    struct ifinfomsg *msg = NLMSG_DATA(nh);
    if (!msg || msg->ifi_type != ARPHRD_ETHER) continue;

    netdev_item_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
      syslog2(LOG_ALERT, "failed to allocate netdev_item_t");
      return -1;
    }
    dev->index = msg->ifi_index;

    if (tb[IFLA_LINKINFO]) {
      struct rtattr *linkinfo[IFLA_INFO_MAX + 1] = {0};
      parse_rtattr_nested(linkinfo, IFLA_INFO_MAX, tb[IFLA_LINKINFO]);
      if (linkinfo[IFLA_INFO_KIND] &&
          fill_from_rta(dev->kind, IFNAMSIZ, linkinfo[IFLA_INFO_KIND]) == 0) {
        if (strcmp("bridge", dev->kind) == 0) dev->is_bridge = true;
      }
    }

    if (!tb[IFLA_IFNAME] ||
        fill_from_rta(dev->name, IFNAMSIZ, tb[IFLA_IFNAME]) != 0) {
      syslog2(LOG_WARNING, "IFLA_IFNAME missing or invalid");
      free(dev);
      continue;
    }

    if (!tb[IFLA_ADDRESS] ||
        fill_from_rta(dev->ll_addr, ETH_ALEN, tb[IFLA_ADDRESS]) != 0) {
      syslog2(LOG_WARNING, "IFLA_ADDRESS missing or invalid");
      free(dev);
      continue;
    }

    if (tb[IFLA_LINK] && RTA_PAYLOAD(tb[IFLA_LINK]) >= sizeof(uint32_t))
      dev->ifla_link_idx = *(uint32_t *)RTA_DATA(tb[IFLA_LINK]);

    if (tb[IFLA_MASTER] && RTA_PAYLOAD(tb[IFLA_MASTER]) >= sizeof(uint32_t))
      dev->master_idx = *(uint32_t *)RTA_DATA(tb[IFLA_MASTER]);

    slist_add_tail(&dev->list, list);
  }
  return 1;
}

int get_netdev(struct slist_head *list) {
  FUNC_START_DEBUG;
  if (!list) return -1;

  int sd = send_msg();
  if (sd < 0) return -1;

  int ret = 0;
  for (;;) {
    void *buf = NULL;
    ssize_t len = recv_msg(sd, &buf);

    if (len <= 0) {       // ошибка или timeout/конец
      if (buf) free(buf); // recv_msg мог выделить, но вернуть <=0
      ret = (len == 0) ? 0 : -1;
      break;
    }

    ret = parse_recv_chunk(buf, len, list);
    free(buf);

    if (ret <= 0) break; // ошибка или NLMSG_DONE
    // иначе продолжаем читать
  }

  close(sd);
  return ret;
}