
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
  while (RTA_OK(rta, len)) {
    type = rta->rta_type & ~flags;
    if ((type <= max) && (!tb[type]))
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

/* parse netlink message */
static ssize_t parse_nlbuf(struct nlmsghdr *nh, struct rtattr **tb) {
  // FUNC_START_DEBUG;
  unsigned int len = nh->nlmsg_len;              // netlink message length including header
  struct ifinfomsg *msg = NLMSG_DATA(nh);        // macro to get a ptr right after header
  uint32_t msg_len = NLMSG_LENGTH(sizeof(*msg)); // netlink message length without header
  char *p = (char *)nh;                          // ptr to nh
  // this is very first rtnetlink attribute ptr
  struct rtattr *rta = (struct rtattr *)(p + msg_len); // move ptr forward
  len -= msg_len;                                      // count message length
  parse_rtattr(tb, IFLA_MAX, rta, len);                // fill tb attribute buffer
  return nh->nlmsg_len;
}

static int addattr_l(struct nlmsghdr *n, uint32_t maxlen, int type, const void *data, uint32_t alen) {
  uint32_t len = RTA_LENGTH(alen);
  struct rtattr *rta;
  if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > (unsigned)maxlen) {
    syslog2(LOG_ERR, "addattr_l ERROR: message exceeded bound of %d\n", maxlen);
    return -1;
  }
  rta = NLMSG_TAIL(n);
  rta->rta_type = type;
  rta->rta_len = len;
  memcpy(RTA_DATA(rta), data, alen);
  n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
  return 0;
}

static int addattr32(struct nlmsghdr *n, unsigned int maxlen, int type, uint32_t data) {
  return addattr_l(n, maxlen, type, &data, sizeof(uint32_t));
}

void free_netdev_list(struct slist_head *list) {
  FUNC_START_DEBUG;
  netdev_item_t *item = NULL;
  netdev_item_t *tmp = NULL;

  slist_for_each_entry_safe(item, tmp, list, list) {
    slist_del_node(&item->list, list);
    free(item);
  }
}

netdev_item_t *ll_get_by_index(struct slist_head *list, int index) {
  // FUNC_START_DEBUG;
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
    char *buf[256];
  } req = {
      .nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
      .nlh.nlmsg_type = RTM_GETLINK,
      .nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP | NLM_F_ACK,
      .nlh.nlmsg_pid = 0,
      .nlh.nlmsg_seq = 1,
  };
  int err = addattr32(&req.nlh, sizeof(req), IFLA_EXT_MASK, RTEXT_FILTER_VF);
  if (err) {
    syslog2(LOG_ERR, "%s addattr32(&req.nlh, sizeof(req), IFLA_EXT_MASK, RTEXT_FILTER_VF)", strerror(errno));
    return -1;
  }

  int sd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE); /* open socket */
  if (sd < 0) {
    syslog2(LOG_ERR, "%s socket()", strerror(errno));
    return -1;
  }
  fchmod(sd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

  // set socket nonblocking flag
  int flags = fcntl(sd, F_GETFL, 0);
  fcntl(sd, F_SETFL, flags | O_NONBLOCK);

  // set socket timeout 100ms
  struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
  if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setsockopt");
    return -2;
  }

  /* send message */
  status = send(sd, &req, req.nlh.nlmsg_len, 0);
  if (status < 0) {
    status = errno;
    syslog2(LOG_ERR, "send() error=%s", strerror(errno));
    close(sd); /* close socket */
    return -1;
  }
  return sd;
}

static ssize_t recv_msg(int sd, void **buf) {
  // FUNC_START_DEBUG;
  ssize_t bufsize = 512;
  *buf = malloc(bufsize);
  struct iovec iov = {.iov_base = *buf, .iov_len = bufsize};
  struct sockaddr_nl sa;
  struct msghdr msg = {.msg_name = &sa,
                       .msg_namelen = sizeof(sa),
                       .msg_iov = &iov,
                       .msg_iovlen = 1,
                       .msg_control = NULL,
                       .msg_controllen = 0,
                       .msg_flags = 0};

  fd_set readset;
  FD_ZERO(&readset);
  FD_SET(sd, &readset);
  struct timeval timeout = {.tv_sec = 1, .tv_usec = 0}; // 1 sec. timeout
  int ret = select(sd + 1, &readset, NULL, NULL, &timeout);
  if (ret == 0) {
    return ret;
  } else if (ret < 0) {
    if (errno == EINTR) {
      syslog2(LOG_WARNING, "select EINTR");
      return ret;
    }
    syslog2(LOG_ERR, "select error=%s", strerror(errno));
    return ret;
  }

  // MSG_DONTWAIT to enable non-blocking mode
  ssize_t len = recvmsg(sd, &msg, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
  if (len <= 0) return len;

  if (len > bufsize) {
    bufsize = len;
    *buf = realloc(*buf, bufsize);
    iov.iov_base = *buf;
    iov.iov_len = bufsize;
  }
  len = recvmsg(sd, &msg, MSG_DONTWAIT);
  return len;
}

static int fill_from_rta(void *dst, size_t dst_sz, struct rtattr *rta) {
  if (!rta || !RTA_OK(rta, rta->rta_len)) return -1;
  size_t plen = RTA_PAYLOAD(rta);
  if (plen > dst_sz) plen = dst_sz;
  if (plen) memcpy(dst, RTA_DATA(rta), plen);
  return 0;
}

//return 1 if msg is not finished yet, 0 - on NLMSG_DONE, -EINVAL on NLMSG_ERROR 
static int parse_recv_chunk(void *buf, ssize_t len, struct slist_head *list) {
  FUNC_START_DEBUG;
  size_t counter = 0;
  struct nlmsghdr *nh;
  if (len <= 0) {
    syslog2(LOG_ERR, "ret");
    return -1;
  }
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
    if (msg->ifi_type != ARPHRD_ETHER) continue;
    netdev_item_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
      syslog2(LOG_ALERT, "failed to allocate netdev_item_t");
      return -1;
    }
    dev->index = msg->ifi_index;
    if (tb[IFLA_LINKINFO]) {
      struct rtattr *linkinfo[IFLA_INFO_MAX + 1] = {0};
      parse_rtattr_nested(linkinfo, IFLA_INFO_MAX, tb[IFLA_LINKINFO]);
      if (fill_from_rta(dev->kind, IFNAMSIZ, linkinfo[IFLA_INFO_KIND]) == 0) {
        if (strcmp("bridge", dev->kind) == 0) dev->is_bridge = true;
      }
    }

    if (fill_from_rta(dev->name, IFNAMSIZ, tb[IFLA_IFNAME]) != 0) {
      syslog2(LOG_WARNING, "IFLA_IFNAME missing or invalid");
      free(dev);
      continue;
    }

    if (fill_from_rta(dev->ll_addr, ETH_ALEN, tb[IFLA_ADDRESS]) != 0) {
      syslog2(LOG_WARNING, "IFLA_ADDRESS missing or invalid");
      free(dev);
      continue;
    }

    if (tb[IFLA_LINK]) dev->ifla_link_idx = *(uint32_t *)RTA_DATA(tb[IFLA_LINK]);
    if (tb[IFLA_MASTER]) dev->master_idx = *(uint32_t *)RTA_DATA(tb[IFLA_MASTER]);

    slist_add_tail(&dev->list, list);
  }
  return 1;
}

int get_netdev(struct slist_head *list) {
  FUNC_START_DEBUG;
  int sd = send_msg();
  if (sd < 0) return -1;
  void *buf;
  ssize_t len;
  int ret = 0;
  while (1) {
    len = recv_msg(sd, &buf);
    if (len < 0) break; // ошибка или конец
    if (len == 0) {
      free(buf);
      break;
    } // больше нет данных
    ret = parse_recv_chunk(buf, len, list);
    free(buf);
    if (ret <= 0) break; // ошибка или конец NLMSG_DONE
    //продолжаем читать на положительном ret
  }

  close(sd);
  return ret;
}
