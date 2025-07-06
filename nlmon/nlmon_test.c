#include "../timeutil/timeutil.h"
#include "nlmon.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
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

// Заглушка для uevent_t
struct uevent_s {
  int dummy;
};
typedef struct uevent_s uevent_t;

// Заглушка для tu_pause_start и tu_pause_end
static int pause_end_called = 0;
static void mock_tu_pause_start(void) {
  PRINT_TEST_INFO("mock_tu_pause_start called");
}
static void mock_tu_pause_end(void) {
  pause_end_called = 1;
  PRINT_TEST_INFO("mock_tu_pause_end called");
}

static void filter_cb(const char *ifname, uint32_t events, void *arg) {
  (void)ifname;
  nl_cb_arg_t *cb_arg = arg;
  if (!cb_arg)
    return;
  if (events & NLMON_EVENT_LINK_UP) {
    cb_arg->tu_pause_end();
  }
}

static int cb1_called = 0;
static int cb2_called = 0;
static void cb1(const char *ifname, uint32_t events, void *arg) {
  (void)ifname;
  (void)events;
  (void)arg;
  cb1_called++;
}
static void cb2(const char *ifname, uint32_t events, void *arg) {
  (void)ifname;
  (void)events;
  (void)arg;
  cb2_called++;
}

static int multi_up = 0;
static int multi_down = 0;
static void multi_cb(const char *ifname, uint32_t events, void *arg) {
  (void)ifname;
  (void)arg;
  if (events & NLMON_EVENT_LINK_UP)
    multi_up++;
  if (events & NLMON_EVENT_LINK_DOWN)
    multi_down++;
}

// Тест инициализации Netlink-сокета
static void test_init_netlink_monitor(void) {
  PRINT_TEST_START("init_netlink_monitor");

  int fd = init_netlink_monitor();
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

  int fd = init_netlink_monitor();
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
      .ifname = ifname,
      .tu_pause_start = mock_tu_pause_start,
      .tu_pause_end = mock_tu_pause_end};
  uevent_t ev = {.dummy = 0};
  pause_end_called = 0;

  const char *names[] = {ifname, NULL};
  nlmon_filter_t filter = {
      .ifnames = names,
      .events = NLMON_EVENT_LINK_UP,
      .cb = filter_cb,
      .arg = &cb_arg};
  nlmon_install_filters(&filter, 1);

  // Создаём Netlink-сокет
  int fd = init_netlink_monitor();
  assert(fd >= 0);

  // Эмулируем Netlink-сообщение RTM_NEWLINK
  char buf[8192];
  struct nlmsghdr *nh = (struct nlmsghdr *)buf;
  struct ifinfomsg *ifi = (struct ifinfomsg *)(buf + NLMSG_HDRLEN);
  unsigned int ifindex = if_nametoindex(ifname);
  assert(ifindex != 0);

  nh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  nh->nlmsg_type = RTM_NEWLINK;
  nh->nlmsg_flags = 0;
  nh->nlmsg_seq = 0;
  nh->nlmsg_pid = 0;
  ifi->ifi_family = AF_UNSPEC;
  ifi->ifi_index = ifindex;
  ifi->ifi_flags = IFF_UP | IFF_RUNNING;
  ifi->ifi_type = 0;
  ifi->ifi_change = 0;

  // Отправляем сообщение в сокет
  assert(write(fd, buf, nh->nlmsg_len) == nh->nlmsg_len);

  nlmon_test_msg_t msg = {.buf = buf, .len = nh->nlmsg_len};
  // Вызываем callback
  nl_handler_cb((uevent_t *)&msg, fd, EPOLLIN);
  assert(pause_end_called == 1); // Проверяем, что tu_pause_end был вызван

  deinit_netlink_monitor(fd);
  nlmon_clear_filters();
  PRINT_TEST_PASSED();
}

// Тест обработки некорректного имени интерфейса
static void test_nl_handler_cb_invalid_ifname(void) {
  PRINT_TEST_START("nl_handler_cb_invalid_ifname");

  nl_cb_arg_t cb_arg = {
      .ifname = "invalid_ifname_123",
      .tu_pause_start = mock_tu_pause_start,
      .tu_pause_end = mock_tu_pause_end};
  const char *names[] = {"invalid_ifname_123", NULL};
  nlmon_filter_t filter = {
      .ifnames = names,
      .events = NLMON_EVENT_LINK_UP,
      .cb = filter_cb,
      .arg = &cb_arg};
  uevent_t ev = {.dummy = 0};
  pause_end_called = 0;

  int fd = init_netlink_monitor();
  assert(fd >= 0);

  nlmon_test_msg_t msg = {.buf = NULL, .len = 0};
  // Вызываем callback с некорректным ifname
  nl_handler_cb((uevent_t *)&msg, fd, EPOLLIN);
  assert(pause_end_called == 0); // tu_pause_end не должен быть вызван

  deinit_netlink_monitor(fd);
  nlmon_clear_filters();
  PRINT_TEST_PASSED();
}

static void test_nl_handler_cb_multiple_filters(void) {
  PRINT_TEST_START("nl_handler_cb_multiple_filters");

  const char *ifname = "lo";
  const char *names[] = {ifname, NULL};
  nlmon_filter_t filters[2] = {
      {.ifnames = names, .events = NLMON_EVENT_LINK_UP, .cb = cb1, .arg = NULL},
      {.ifnames = NULL, .events = NLMON_EVENT_LINK_DOWN, .cb = cb2, .arg = NULL}};
  nlmon_install_filters(filters, 2);

  uevent_t ev = {.dummy = 0};
  cb1_called = cb2_called = 0;

  int fd = init_netlink_monitor();
  assert(fd >= 0);

  char buf[8192];
  struct nlmsghdr *nh = (struct nlmsghdr *)buf;
  struct ifinfomsg *ifi = (struct ifinfomsg *)(buf + NLMSG_HDRLEN);
  unsigned int ifindex = if_nametoindex(ifname);
  assert(ifindex != 0);

  nh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  nh->nlmsg_type = RTM_NEWLINK;
  nh->nlmsg_flags = 0;
  nh->nlmsg_seq = 0;
  nh->nlmsg_pid = 0;
  ifi->ifi_family = AF_UNSPEC;
  ifi->ifi_index = ifindex;
  ifi->ifi_type = 0;
  ifi->ifi_change = 0;

  // UP event
  ifi->ifi_flags = IFF_UP | IFF_RUNNING;
  assert(write(fd, buf, nh->nlmsg_len) == nh->nlmsg_len);
  nlmon_test_msg_t msg = {.buf = buf, .len = nh->nlmsg_len};
  nl_handler_cb((uevent_t *)&msg, fd, EPOLLIN);
  assert(cb1_called == 1 && cb2_called == 0);

  // DOWN event
  ifi->ifi_flags = 0;
  assert(write(fd, buf, nh->nlmsg_len) == nh->nlmsg_len);
  msg.buf = buf;
  msg.len = nh->nlmsg_len;
  nl_handler_cb((uevent_t *)&msg, fd, EPOLLIN);
  assert(cb1_called == 1 && cb2_called == 1);

  deinit_netlink_monitor(fd);
  nlmon_clear_filters();
  PRINT_TEST_PASSED();
}

static void test_nl_handler_cb_multi_ifnames_events(void) {
  PRINT_TEST_START("nl_handler_cb_multi_ifnames_events");

  const char *names[] = {"lo", "eth0", NULL};
  nlmon_filter_t filter = {
      .ifnames = names,
      .events = NLMON_EVENT_LINK_UP | NLMON_EVENT_LINK_DOWN,
      .cb = multi_cb,
      .arg = NULL};
  multi_up = multi_down = 0;
  nlmon_install_filters(&filter, 1);

  int fd = init_netlink_monitor();
  assert(fd >= 0);

  char buf[8192];
  struct nlmsghdr *nh = (struct nlmsghdr *)buf;
  struct ifinfomsg *ifi = (struct ifinfomsg *)(buf + NLMSG_HDRLEN);

  nh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  nh->nlmsg_type = RTM_NEWLINK;
  nh->nlmsg_flags = 0;
  nh->nlmsg_seq = 0;
  nh->nlmsg_pid = 0;
  ifi->ifi_family = AF_UNSPEC;
  ifi->ifi_type = 0;
  ifi->ifi_change = 0;

  unsigned int ifindex = if_nametoindex("lo");
  assert(ifindex != 0);
  ifi->ifi_index = ifindex;
  ifi->ifi_flags = IFF_UP | IFF_RUNNING;
  assert(write(fd, buf, nh->nlmsg_len) == nh->nlmsg_len);
  nlmon_test_msg_t msg = {.buf = buf, .len = nh->nlmsg_len};
  nl_handler_cb((uevent_t *)&msg, fd, EPOLLIN);

  ifindex = if_nametoindex("eth0");
  assert(ifindex != 0);
  ifi->ifi_index = ifindex;
  ifi->ifi_flags = 0;
  assert(write(fd, buf, nh->nlmsg_len) == nh->nlmsg_len);
  msg.buf = buf;
  msg.len = nh->nlmsg_len;
  nl_handler_cb((uevent_t *)&msg, fd, EPOLLIN);

  assert(multi_up == 1 && multi_down == 1);

  deinit_netlink_monitor(fd);
  nlmon_clear_filters();
  PRINT_TEST_PASSED();
}

int main(void) {
  tu_init();

  test_init_netlink_monitor();
  test_deinit_netlink_monitor();
  test_nl_handler_cb();
  test_nl_handler_cb_invalid_ifname();
  test_nl_handler_cb_multiple_filters();
  test_nl_handler_cb_multi_ifnames_events();

  printf(KGRN "====== All nlmon tests passed! ======\n" KNRM);
  return 0;
}
