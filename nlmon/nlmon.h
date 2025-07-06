#ifndef NL_MON_H
#define NL_MON_H

#include <stdint.h>
#include <stddef.h>

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

typedef struct {
    const char **ifnames;  // NULL-terminated array, may be NULL/empty
    uint32_t events;       // bit mask of monitored events
    void (*cb)(const char *ifname, uint32_t events, void *arg);
    void *arg;
} nlmon_filter_t;

#ifdef TESTRUN
typedef struct nlmon_test_msg {
  char *buf;
  size_t len;
} nlmon_test_msg_t;
#endif

#define NLMON_EVENT_LINK_UP 0x1
#define NLMON_EVENT_LINK_DOWN 0x2

// Инициализация Netlink-мониторинга
int init_netlink_monitor();

// Деинициализация Netlink-мониторинга
void deinit_netlink_monitor(int fd);

// Callback для обработки Netlink-событий
void nl_handler_cb(uevent_t *ev, int fd, short events, nlmon_filter_t *filters,
                   size_t filter_cnt);

#endif // NL_MON_H
