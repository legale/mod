#include "nlmon.h"
#include "../syslog2/syslog2.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

// Размер буфера для Netlink
#define DEFROUTE_BUF_SIZE 8192

// Инициализация Netlink-сокета для мониторинга
int init_netlink_monitor(const char *ifnames, uint32_t groups) {
  FUNC_START_DEBUG;
  int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0) {
    syslog2(LOG_ERR, "socket failed: %s", strerror(errno));
    return -errno;
  }

  struct sockaddr_nl sa = {
      .nl_family = AF_NETLINK,
      .nl_groups = groups ? groups : RTMGRP_LINK
  };

  /* validate interface names if provided */
  if (ifnames) {
    char *names = strdup(ifnames);
    if (!names) {
      int err = errno;
      close(fd);
      return -err;
    }
    char *saveptr = NULL;
    for (char *n = strtok_r(names, ",", &saveptr); n; n = strtok_r(NULL, ",", &saveptr)) {
      while (*n == ' ') n++;
      if (*n && if_nametoindex(n) == 0) {
        syslog2(LOG_ERR, "invalid ifname=%s: %s", n, strerror(errno));
        free(names);
        close(fd);
        return -EINVAL;
      }
    }
    free(names);
  }

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
void nl_handler_cb(uevent_t *ev, int fd, short events, void *arg) {
  FUNC_START_DEBUG;
  nl_cb_arg_t *cb_arg = (nl_cb_arg_t *)arg;
  if (!cb_arg || !cb_arg->tu_pause_end) {
    syslog2(LOG_ERR, "error: EINVAL");
    return;
  }

  char buf[DEFROUTE_BUF_SIZE];
  int len = recv(fd, buf, sizeof(buf), 0);
  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      syslog2(LOG_ERR, "recv failed: %s", strerror(errno));
    }
    return;
  }

  const char *names[16];
  size_t names_cnt = 0;
  if (cb_arg->ifnames) {
    char *tmp = strdup(cb_arg->ifnames);
    if (!tmp) {
      syslog2(LOG_ERR, "strdup failed: %s", strerror(errno));
      return;
    }
    char *saveptr = NULL;
    for (char *n = strtok_r(tmp, ",", &saveptr); n && names_cnt < 16; n = strtok_r(NULL, ",", &saveptr)) {
      while (*n == ' ') n++;
      if (*n)
        names[names_cnt++] = strdup(n);
    }
    free(tmp);
  }

  uint32_t groups = cb_arg->groups ? cb_arg->groups : RTMGRP_LINK;

  for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
    if ((nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK)) {
      if (!(groups & RTMGRP_LINK))
        continue;
    } else {
      continue;
    }

    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nh);
    char ifnamebuf[IF_NAMESIZE] = {0};
    const char *ifname = if_indextoname(ifi->ifi_index, ifnamebuf);
    bool match = (names_cnt == 0);
    for (size_t i = 0; !match && i < names_cnt; ++i) {
      if (strcmp(ifname, names[i]) == 0)
        match = true;
    }
    if (!match)
      continue;

    if (ifi->ifi_flags & IFF_UP && ifi->ifi_flags & IFF_RUNNING) {
      syslog2(LOG_NOTICE, "idx=%d ifname=%s is UP and RUNNING", ifi->ifi_index, ifname);
      cb_arg->tu_pause_end();
    } else {
      syslog2(LOG_NOTICE, "idx=%d ifname=%s is DOWN flags=0x%x", ifi->ifi_index, ifname, ifi->ifi_flags);
    }
  }

  for (size_t i = 0; i < names_cnt; ++i) {
    free((void *)names[i]);
  }
}
