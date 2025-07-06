#include "nlmon.h"
#include "../syslog2/syslog2.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

// Размер буфера для Netlink
#define DEFROUTE_BUF_SIZE 8192

// Инициализация Netlink-сокета для мониторинга
int init_netlink_monitor() {
  FUNC_START_DEBUG;
  int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0) {
    syslog2(LOG_ERR, "socket failed: %s", strerror(errno));
    return -errno;
  }

  struct sockaddr_nl sa = {
      .nl_family = AF_NETLINK,
      .nl_groups = RTMGRP_LINK // Мониторим состояние интерфейса
  };

  if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    int err = errno;
    syslog2(LOG_ERR, "bind failed: %s", strerror(err));
    close(fd);
    return -err;
  }

  // Устанавливаем неблокирующий режим для epoll
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    int err = errno;
    syslog2(LOG_ERR, "fcntl failed: %s", strerror(err));
    close(fd);
    return -err;
  }

  return fd; // Возвращаем fd для epoll
}

// Деинициализация Netlink-сокета
void deinit_netlink_monitor(int fd) {
  FUNC_START_DEBUG;

  if (fd >= 0) {
    close(fd);
    syslog(LOG_NOTICE, "netlink socket closed");
  }
}

// Callback для обработки Netlink-событий
void nl_handler_cb(uevent_t *ev, int fd, short events, nlmon_filter_t *filters,
                   size_t filter_cnt) {
  FUNC_START_DEBUG;
  if (!filters || filter_cnt == 0) {
    syslog2(LOG_ERR, "error: EINVAL");
    return;
  }

  char buf[DEFROUTE_BUF_SIZE];
  int len = 0;
#ifdef TESTRUN
  if (ev) {
    struct nlmon_test_msg {
      char *buf;
      size_t len;
    } *msg = (struct nlmon_test_msg *)ev;
    if (msg->buf) {
      len = (int)((msg->len > sizeof(buf)) ? sizeof(buf) : msg->len);
      memcpy(buf, msg->buf, len);
    }
  }
  if (len == 0)
#endif
    len = recv(fd, buf, sizeof(buf), 0);
  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      syslog2(LOG_ERR, "recv failed: %s", strerror(errno));
    }
    return;
  }

  char ifnamebuf[IF_NAMESIZE];
  for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len);
       nh = NLMSG_NEXT(nh, len)) {
    if (nh->nlmsg_type != RTM_NEWLINK) {
      continue;
    }

    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nh);
    const char *ifname = if_indextoname(ifi->ifi_index, ifnamebuf);
    if (!ifname) {
      continue;
    }

    uint32_t ev_mask =
        (ifi->ifi_flags & IFF_UP && ifi->ifi_flags & IFF_RUNNING)
            ? NLMON_EVENT_LINK_UP
            : NLMON_EVENT_LINK_DOWN;

    for (size_t i = 0; i < filter_cnt; ++i) {
      nlmon_filter_t *f = &filters[i];
      if (!(f->events & ev_mask)) {
        continue;
      }

      bool match = true;
      if (f->ifnames) {
        match = false;
        for (const char **n = f->ifnames; *n; ++n) {
          if (strcmp(*n, ifname) == 0) {
            match = true;
            break;
          }
        }
      }
      if (!match) {
        continue;
      }

      if (f->cb) {
        f->cb(ifname, ev_mask, f->arg);
      }
    }
  }
}
