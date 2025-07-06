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

  unsigned int ifindex = 0;
  const char *ifname = NULL;
  char ifnamebuf[IF_NAMESIZE] = {0};
  if (cb_arg->ifname) {
    ifindex = if_nametoindex(cb_arg->ifname);
    if (ifindex == 0) {
      syslog2(LOG_ERR, "invalid ifname=%s: %s", cb_arg->ifname, strerror(errno));
      return;
    }
	ifname = cb_arg->ifname;
  }

  for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
    if (nh->nlmsg_type != RTM_NEWLINK) {
      continue;
    }

    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nh);
    if (cb_arg->ifname && ifi->ifi_index != (int)ifindex) {
      continue;
    }

	if(ifindex == 0){
		ifname = if_indextoname(ifi->ifi_index, ifnamebuf);
	}

    if (ifi->ifi_flags & IFF_UP && ifi->ifi_flags & IFF_RUNNING) {
      syslog2(LOG_NOTICE, "idx=%d ifname=%s is UP and RUNNING", ifi->ifi_index, ifname);
      cb_arg->tu_pause_end(); // Снимаем паузу при восстановлении интерфейса
    } else {
      syslog2(LOG_NOTICE, "idx=%d ifname=%s is DOWN flags=0x%x", ifi->ifi_index, ifname, ifi->ifi_flags);
    }
  }
}
