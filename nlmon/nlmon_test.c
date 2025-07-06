#include "../timeutil/timeutil.h"
#include "nlmon.h"
#include "../uevent/uevent.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define KNRM "\x1B[0m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KCYN "\x1B[36m"

#define PRINT_TEST_START(name) \
  printf(KCYN "%s:%d --- Starting Test: %s ---\n" KNRM, __FILE__, __LINE__, name)
#define PRINT_TEST_PASSED() printf(KGRN "--- Test Passed ---\n\n" KNRM)
#define PRINT_TEST_INFO(fmt, ...) \
  printf(KYEL "%s:%d [INFO] " fmt "\n" KNRM, __FILE__, __LINE__, ##__VA_ARGS__)

// Заглушка для tu_pause_start и tu_pause_end
static int pause_end_called = 0;
static void mock_tu_pause_start(void) {
  PRINT_TEST_INFO("mock_tu_pause_start called");
}
static void mock_tu_pause_end(void) {
  pause_end_called = 1;
  PRINT_TEST_INFO("mock_tu_pause_end called");
}

// Тест инициализации Netlink-сокета
static void test_init_netlink_monitor(void) {
  PRINT_TEST_START("init_netlink_monitor");

  const char *ifname = "lo"; // Используем "lo" как тестовый интерфейс
  int fd = init_netlink_monitor(ifname, RTMGRP_LINK);
  assert(fd >= 0);
  PRINT_TEST_INFO("Netlink socket fd: %d", fd);

  // Проверяем, что сокет валидный
  struct sockaddr_nl sa;
  socklen_t len = sizeof(sa);
  assert(getsockname(fd, (struct sockaddr *)&sa, &len) == 0);
  assert(sa.nl_family == AF_NETLINK);
  assert(sa.nl_groups & RTMGRP_LINK);

  deinit_netlink_monitor(fd);
  PRINT_TEST_PASSED();
}

// Тест деинициализации Netlink-сокета
static void test_deinit_netlink_monitor(void) {
  PRINT_TEST_START("deinit_netlink_monitor");

  const char *ifname = "lo";
  int fd = init_netlink_monitor(ifname, RTMGRP_LINK);
  assert(fd >= 0);

  deinit_netlink_monitor(fd);
  // Проверяем, что сокет закрыт
  assert(close(fd) == -1 && errno == EBADF);
  PRINT_TEST_PASSED();
}

// Тест callback'а с эмуляцией Netlink-сообщения
static void test_nl_handler_cb(void) {
  PRINT_TEST_START("nl_handler_cb");

  const char *ifname = "lo";
  nl_cb_arg_t cb_arg = {
      .ifnames = ifname,
      .groups = RTMGRP_LINK,
      .tu_pause_start = mock_tu_pause_start,
      .tu_pause_end = mock_tu_pause_end};
  uevent_t ev = {0};
  pause_end_called = 0;

  // Создаём Netlink-сокет
  int fd = init_netlink_monitor(ifname, RTMGRP_LINK);
  assert(fd >= 0);

  // Просто вызываем callback без отправки сообщения
  nl_handler_cb(&ev, fd, EPOLLIN, &cb_arg);
  assert(pause_end_called == 0); // Нет событий -> tu_pause_end не вызывается

  deinit_netlink_monitor(fd);
  PRINT_TEST_PASSED();
}

// Тест обработки некорректного имени интерфейса
static void test_nl_handler_cb_invalid_ifname(void) {
  PRINT_TEST_START("nl_handler_cb_invalid_ifname");

  nl_cb_arg_t cb_arg = {
      .ifnames = "invalid_ifname_123",
      .groups = RTMGRP_LINK,
      .tu_pause_start = mock_tu_pause_start,
      .tu_pause_end = mock_tu_pause_end};
  uevent_t ev = {0};
  pause_end_called = 0;

  int fd = init_netlink_monitor("lo", RTMGRP_LINK);
  assert(fd >= 0);

  // Вызываем callback с некорректным ifname
  nl_handler_cb(&ev, fd, EPOLLIN, &cb_arg);
  assert(pause_end_called == 0); // tu_pause_end не должен быть вызван

  deinit_netlink_monitor(fd);
  PRINT_TEST_PASSED();
}

int main(void) {
  tu_init();

  test_init_netlink_monitor();
  test_deinit_netlink_monitor();
  test_nl_handler_cb();
  test_nl_handler_cb_invalid_ifname();

  printf(KGRN "====== All nlmon tests passed! ======\n" KNRM);
  return 0;
}

