#include "nlmon.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

// logger fallback
#ifdef IS_DYNAMIC_LIB
#include "../syslog2/syslog2.h" // жёсткая зависимость
#endif

#include <stdarg.h>
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

#define MAX_FILTERS 32
#define DEFROUTE_BUF_SIZE 8192

static nlmon_filter_t g_filters[MAX_FILTERS];
static size_t g_filter_cnt = 0;

int nlmon_add_filter(const nlmon_filter_t *filter) {
  if (!filter || !filter->cb || g_filter_cnt >= MAX_FILTERS)
    return -1;
  g_filters[g_filter_cnt++] = *filter;
  return 0;
}

int nlmon_remove_filter(const nlmon_filter_t *filter) {
  if (!filter || !filter->cb)
    return -1;
  for (size_t i = 0; i < g_filter_cnt; ++i) {
    if (g_filters[i].cb == filter->cb &&
        g_filters[i].arg == filter->arg &&
        g_filters[i].events == filter->events &&
        g_filters[i].ifnames == filter->ifnames) {
      for (size_t j = i; j + 1 < g_filter_cnt; ++j)
        g_filters[j] = g_filters[j + 1];
      --g_filter_cnt;
      return 0;
    }
  }
  return -1;
}

int nlmon_list_filters(void) {
  syslog2(LOG_INFO, "Current filters (%zu):", g_filter_cnt);
  for (size_t i = 0; i < g_filter_cnt; ++i) {
    syslog2(LOG_INFO, "  [%zu] events=0x%x cb=%p arg=%p",
            i, g_filters[i].events, (void *)g_filters[i].cb, g_filters[i].arg);
    if (g_filters[i].ifnames) {
      char buf[256] = {0};
      size_t pos = 0;
      for (const char **n = g_filters[i].ifnames; n && *n; ++n) {
        int written = snprintf(buf + pos, sizeof(buf) - pos, "%s ", *n);
        if (written < 0 || (size_t)written >= sizeof(buf) - pos)
          break;
        pos += (size_t)written;
      }
      syslog2(LOG_INFO, "    ifnames: %s", buf);
    }
  }
  return (int)g_filter_cnt;
}

static int open_netlink_socket(void) {
  int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0)
    return -1;
  struct sockaddr_nl sa = {
      .nl_family = AF_NETLINK,
      .nl_groups = RTMGRP_LINK};
  if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(fd);
    return -1;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void handle_netlink_msg(const char *buf, int len) {
  char ifnamebuf[IF_NAMESIZE];
  for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len);
       nh = NLMSG_NEXT(nh, len)) {
    if (nh->nlmsg_type != RTM_NEWLINK)
      continue;
    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nh);
    const char *ifname = if_indextoname(ifi->ifi_index, ifnamebuf);
    if (!ifname)
      continue;
    uint32_t ev_mask =
        ((ifi->ifi_flags & IFF_UP) && (ifi->ifi_flags & IFF_RUNNING))
            ? NLMON_EVENT_LINK_UP
            : NLMON_EVENT_LINK_DOWN;
    for (size_t i = 0; i < g_filter_cnt; ++i) {
      nlmon_filter_t *f = &g_filters[i];
      if (!(f->events & ev_mask))
        continue;
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
      if (!match)
        continue;
      if (f->cb)
        f->cb(ifname, ev_mask, f->arg);
    }
  }
}

int nlmon_run(int timeout_sec) {
  if (g_filter_cnt == 0 || timeout_sec <= 0)
    return -1;
  int fd = open_netlink_socket();
  if (fd < 0)
    return -1;
  struct timeval tv;
  tv.tv_sec = timeout_sec;
  tv.tv_usec = 0;
  while (1) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    int rc = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      close(fd);
      return -1;
    }
    if (rc == 0)
      break;
    if (FD_ISSET(fd, &rfds)) {
      char buf[DEFROUTE_BUF_SIZE];
      int len = recv(fd, buf, sizeof(buf), 0);
      if (len > 0)
        handle_netlink_msg(buf, len);
    }
    break;
  }
  close(fd);
  return 0;
}