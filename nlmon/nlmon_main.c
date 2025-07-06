#include "../syslog2/syslog2.h"
#include "../timeutil/timeutil.h"
#include "nlmon.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#define KNRM "\x1B[0m"
#define KYEL "\x1B[33m"
#define PRINT_INFO(fmt, ...) \
  printf(KYEL "[INFO] " fmt "\n" KNRM, ##__VA_ARGS__)

// Флаг для обработки SIGINT
static volatile sig_atomic_t running = 1;

// Функции паузы
static void tu_pause_start_wrapper(void) {
  tu_pause_start();
  PRINT_INFO("tu_pause_start called");
}

static void tu_pause_end_wrapper(void) {
  tu_pause_end();
  PRINT_INFO("tu_pause_end called");
}

static void link_event_cb(const char *ifname, uint32_t events, void *arg) {
  (void)ifname;
  nl_cb_arg_t *cb_arg = arg;
  if (!cb_arg) {
    return;
  }
  if (events & NLMON_EVENT_LINK_UP) {
    cb_arg->tu_pause_end();
  } else if (events & NLMON_EVENT_LINK_DOWN) {
    if (cb_arg->tu_pause_start) {
      cb_arg->tu_pause_start();
    }
  }
}

// Обработчик SIGINT (Ctrl+C)
static void sigint_handler(int sig) {
  (void)sig;
  running = 0;
}

int main(void) {
#ifdef DEBUG
  setup_syslog2("uevent_test", LOG_DEBUG, false);
#else
  setup_syslog2("uevent_test", LOG_NOTICE, false);
#endif
  tu_init();
  syslog2(LOG_NOTICE, "NETLINK MONITOR DEMONSTRATION");

  const char *ifname = NULL; // Можно заменить на "wlan0"
  nl_cb_arg_t cb_arg = {
      .ifname = ifname,
      .tu_pause_start = tu_pause_start_wrapper,
      .tu_pause_end = tu_pause_end_wrapper};

  const char *ifnames[] = {ifname, NULL};
  nlmon_filter_t filter = {
      .ifnames = ifname ? ifnames : NULL,
      .events = NLMON_EVENT_LINK_UP | NLMON_EVENT_LINK_DOWN,
      .cb = link_event_cb,
      .arg = &cb_arg};
  nlmon_install_filters(&filter, 1);

  // Инициализация Netlink-мониторинга
  int fd = init_netlink_monitor();
  if (fd < 0) {
    syslog2(LOG_ERR, "failed to initialize Netlink monitor: %s\n", strerror(-fd));
    return 1;
  }
  PRINT_INFO("Netlink monitor initialized, fd: %d", fd);

  // Настройка обработчика SIGINT
  struct sigaction sa = {.sa_handler = sigint_handler, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    syslog2(LOG_ERR, "failed to set SIGINT handler: %s\n", strerror(errno));
    deinit_netlink_monitor(fd);
    return 1;
  }

  // Настройка epoll
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    syslog2(LOG_ERR, "failed to create epoll instance: %s\n", strerror(errno));
    deinit_netlink_monitor(fd);
    return 1;
  }

  struct epoll_event ep_ev = {.events = EPOLLIN, .data.fd = fd};
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ep_ev) < 0) {
    syslog2(LOG_ERR, "failed to add fd to epoll: %s\n", strerror(errno));
    close(epoll_fd);
    deinit_netlink_monitor(fd);
    return 1;
  }

  // Имитация работы интерфейса
  PRINT_INFO("Starting pause manually...");
  tu_pause_start();
  PRINT_INFO("Sleeping 2 seconds to wait for interface events...");
  msleep(2000);

  // Основной цикл (выход по Ctrl+C)
  struct epoll_event events[1];
  PRINT_INFO("Running event loop (press Ctrl+C to exit)...");

  while (running) {
    int nfds = epoll_wait(epoll_fd, events, 1, 1000);
    if (nfds < 0) {
      if (errno == EINTR) {
        continue; // Прерывание от сигнала, продолжаем
      }
      syslog2(LOG_ERR, "epoll_wait failed: %s\n", strerror(errno));
      break;
    }
    for (int i = 0; i < nfds; i++) {
      if (events[i].data.fd == fd) {
        nl_handler_cb(NULL, fd, EPOLLIN);
      }
    }
  }

  // Очистка
  PRINT_INFO("Cleaning up...");
  close(epoll_fd);
  deinit_netlink_monitor(fd);
  nlmon_clear_filters();

  printf("====== Netlink monitor demo completed! ======\n");
  return 0;
}
