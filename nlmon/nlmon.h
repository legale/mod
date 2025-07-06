#ifndef NL_MON_H
#define NL_MON_H

#include <stdint.h>

// Заглушка для uevent_t, предполагаем, что определена где-то в коде
typedef struct uevent_s uevent_t;

// Тип указателя на функцию для паузы
typedef void (*pause_fn_t)(void);

// Структура для аргументов callback'а
typedef struct {
  const char *ifname;
  pause_fn_t tu_pause_start;
  pause_fn_t tu_pause_end;
} nl_cb_arg_t;

// Инициализация Netlink-мониторинга
int init_netlink_monitor();

// Деинициализация Netlink-мониторинга
void deinit_netlink_monitor(int fd);

// Callback для обработки Netlink-событий
void nl_handler_cb(uevent_t *ev, int fd, short events, void *arg);

#endif // NL_MON_H
