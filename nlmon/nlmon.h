#ifndef NL_MON_H
#define NL_MON_H

#include <stdint.h>
#include "../uevent/uevent.h"

// Тип указателя на функцию для паузы
typedef void (*pause_fn_t)(void);

// Структура для аргументов callback'а
typedef struct {
  const char *ifnames;  /* comma separated list of interfaces, NULL for all */
  uint32_t groups;      /* mask of netlink multicast groups */
  pause_fn_t tu_pause_start;
  pause_fn_t tu_pause_end;
} nl_cb_arg_t;

// Инициализация Netlink-мониторинга. ifnames can be NULL and should be either
// an array of interface names or a comma separated string. groups specifies the
// netlink multicast groups (e.g. RTMGRP_LINK). If groups is 0 RTMGRP_LINK is
// used by default.
int init_netlink_monitor(const char *ifnames, uint32_t groups);

// Деинициализация Netlink-мониторинга
void deinit_netlink_monitor(int fd);

// Callback для обработки Netlink-событий
void nl_handler_cb(uevent_t *ev, int fd, short events, void *arg);

#endif // NL_MON_H
