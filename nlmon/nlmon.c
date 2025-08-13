#include "nlmon.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// logger fallback
#ifdef IS_DYNAMIC_LIB
#include "../syslog2/syslog2.h"
#endif

#ifndef IS_DYNAMIC_LIB
#include <stdarg.h>
#include <syslog.h>

#define FUNC_START_DEBUG syslog2(LOG_DEBUG, "START")
#define syslog2(pri, fmt, ...) syslog2_(pri, __func__, __FILE__, __LINE__, fmt, true, ##__VA_ARGS__)

__attribute__((weak)) void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...) {
  char buf[4096];
  size_t sz = sizeof(buf);
  va_list ap;

  va_start(ap, nl);
  int len = snprintf(buf, sz, "[%d] %s:%d %s: ", pri, file, line, func);
  len += vsnprintf(buf + len, sz - len, fmt, ap);
  va_end(ap);

  if (len >= (int)sz) len = sz - 1;
  if (nl && len < (int)sz - 1) buf[len++] = '\n';

  ssize_t ret = write(STDOUT_FILENO, buf, len);
  (void)ret;
}
#endif // IS_DYNAMIC_LIB

#define DEFROUTE_BUF_SIZE 8192

typedef struct {
  nlmon_filter_t filters[NLMON_MAX_FILTERS];
  size_t count;
} nlmon_filter_slot_t;

static _Atomic(nlmon_filter_slot_t *) g_filters = NULL;
static pthread_t g_thread;
static volatile atomic_bool g_run = false;

static int open_netlink_socket(void) {
  int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (fd < 0)
    return -1;

  struct sockaddr_nl sa = {
      .nl_family = AF_NETLINK,
      .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
                   RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE,
  };

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
  nlmon_filter_slot_t *current = atomic_load(&g_filters);
  if (!current) return;

  for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
    if (nh->nlmsg_type != RTM_NEWLINK) continue;

    struct ifinfomsg *ifi = (struct ifinfomsg *)NLMSG_DATA(nh);
    int ifidx = ifi->ifi_index;
    uint32_t ifi_flags = ifi->ifi_flags;
    uint32_t ifi_change = ifi->ifi_change;
    const char *ifname = if_indextoname(ifidx, ifnamebuf);
    if (!ifname) continue;

    syslog2(LOG_INFO, "iface=%s idx=%d nh_type=%d flags=0x%04x change=0x%04x changed=x%04x", ifname, ifidx, nh->nlmsg_type, ifi_flags, ifi_change, ifi_flags & ifi_change);

    for (size_t i = 0; i < current->count; ++i) {
      nlmon_filter_t *f = &current->filters[i];
      if (!(f->events & ifi_flags)) continue;

      bool match = false;
      for (int j = 0; j < NLMON_MAX_IFACES && f->idx[j]; ++j) {
        if (f->idx[j] == ifidx) {
          match = true;
          break;
        }
      }
      if (f->idx[0] == 0) match = true; // пустой список = все интерфейсы

      if (match && f->cb)
        f->cb(ifname, ifi_flags, f->arg);
    }
  }
}

static void *nlmon_thread(void *arg) {
  (void)arg;
  int fd = open_netlink_socket();
  if (fd < 0) return NULL;

  while (atomic_load(&g_run)) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    struct timeval tv = {1, 0};
    int rc = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (rc < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (rc == 0) continue;

    if (FD_ISSET(fd, &rfds)) {
      char buf[DEFROUTE_BUF_SIZE];
      int len = recv(fd, buf, sizeof(buf), 0);
      if (len > 0)
        handle_netlink_msg(buf, len);
    }
  }

  close(fd);
  return NULL;
}

int nlmon_run(void) {
  if (atomic_exchange(&g_run, true))
    return -1;
  return pthread_create(&g_thread, NULL, nlmon_thread, NULL);
}

void nlmon_stop(void) {
  if (!atomic_exchange(&g_run, false))
    return;
  pthread_join(g_thread, NULL);
  nlmon_filter_slot_t *old = atomic_exchange(&g_filters, NULL);
  free(old);
}

static nlmon_filter_slot_t *filters_clone_and_add(const nlmon_filter_t *f) {
  nlmon_filter_slot_t *old = atomic_load(&g_filters);
  nlmon_filter_slot_t *new = calloc(1, sizeof(*new));
  if (!new) {
    errno = ENOMEM;
    return NULL;
  }

  if (old) {
    memcpy(new, old, sizeof(*new));

    // проверка на дубликат
    for (size_t i = 0; i < new->count; ++i) {
      nlmon_filter_t *existing = &new->filters[i];
      if (existing->cb == f->cb &&
          existing->arg == f->arg &&
          existing->events == f->events &&
          memcmp(existing->idx, f->idx, sizeof(existing->idx)) == 0) {
        free(new);
        errno = EEXIST;
        return NULL;
      }
    }

    if (new->count >= NLMON_MAX_FILTERS) {
      free(new);
      errno = EINVAL;
      return NULL;
    }
  }

  // копируем фильтр
  memcpy(&new->filters[new->count], f, sizeof(nlmon_filter_t));
  new->count++;
  return new;
}

static nlmon_filter_slot_t *filters_clone_and_remove(const nlmon_filter_t *f) {
  nlmon_filter_slot_t *old = atomic_load(&g_filters);
  if (!old) return NULL;

  nlmon_filter_slot_t *new = calloc(1, sizeof(*new));
  if (!new) return NULL;

  size_t removed = 0;

  for (size_t i = 0; i < old->count; ++i) {
    if (old->filters[i].cb == f->cb &&
        old->filters[i].arg == f->arg &&
        old->filters[i].events == f->events &&
        memcmp(old->filters[i].idx, f->idx, sizeof(f->idx)) == 0) {
      removed++;
      continue;
    }
    new->filters[new->count++] = old->filters[i];
  }

  if (removed == 0) {
    free(new);
    return NULL;
  }
  return new;
}

int nlmon_add_filter(const nlmon_filter_t *filter) {
  if (!filter || !filter->cb)
    return EINVAL;
  nlmon_filter_slot_t *new = filters_clone_and_add(filter);
  if (!new) return EINVAL;
  nlmon_filter_slot_t *old = atomic_exchange(&g_filters, new);
  free(old);
  return 0;
}

int nlmon_remove_filter(const nlmon_filter_t *filter) {
  if (!filter || !filter->cb)
    return EINVAL;
  nlmon_filter_slot_t *new = filters_clone_and_remove(filter);
  if (!new) return EINVAL;
  nlmon_filter_slot_t *old = atomic_exchange(&g_filters, new);
  free(old);
  return 0;
}

void nlmon_clear_filters(void) {
  nlmon_filter_slot_t *old = atomic_exchange(&g_filters, NULL);
  if (old) {
    free(old);
  }
}

int nlmon_list_filters(void) {
  nlmon_filter_slot_t *current = atomic_load(&g_filters);
  if (!current) return 0;

  syslog2(LOG_INFO, "Current filters (%zu):", current->count);

  for (size_t i = 0; i < current->count; ++i) {
    nlmon_filter_t *f = &current->filters[i];

    syslog2(LOG_INFO, "  [%zu] events: 0x%x", i, f->events);
    syslog2(LOG_INFO, "    indices:");
    for (int j = 0; j < NLMON_MAX_IFACES && f->idx[j]; ++j)
      syslog2(LOG_INFO, "      idx=%d", f->idx[j]);
  }

  return (int)current->count;
}
