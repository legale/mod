#include "libnlarpcache.h"
#include "../syslog2/syslog2.h"

#include <assert.h>
#include <linux/rtnetlink.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static int log_called = 0;
static int time_called = 0;

static void mock_log(int pri, const char *fmt, ...) {
  (void)pri;
  (void)fmt;
  log_called++;
}

static int mock_time(struct timespec *ts) {
  if (ts) {
    ts->tv_sec = 0;
    ts->tv_nsec = 0;
  }
  time_called++;
  return 0;
}

#ifdef TESTRUN
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout) {
  (void)nfds;
  (void)readfds;
  (void)writefds;
  (void)exceptfds;
  (void)timeout;
  errno = EINTR;
  return -1;
}
#endif

static void test_parse_rtattr(void) {
  nlarpcache_mod_init(&(netlink_arp_cache_mod_init_args_t){
      .log = mock_log, .get_time = mock_time});

  unsigned char buf[256];

  struct rtattr *rta1 = (struct rtattr *)buf;
  rta1->rta_type = NDA_DST;
  rta1->rta_len = RTA_LENGTH(4);
  memset(RTA_DATA(rta1), 0xaa, 4);

  struct rtattr *rta2 = (struct rtattr *)(buf + RTA_SPACE(4));
  rta2->rta_type = NDA_LLADDR;
  rta2->rta_len = RTA_LENGTH(6);
  memset(RTA_DATA(rta2), 0xbb, 6);

  unsigned len = RTA_SPACE(4) + RTA_SPACE(6);
  struct rtattr *tb[NDA_MAX + 1] = {0};
  parse_rtattr(tb, NDA_MAX, rta1, len);
  assert(time_called == 1);

  assert(tb[NDA_DST] == rta1);
  assert(tb[NDA_LLADDR] == rta2);

  void *rb = NULL;
  struct {
    struct nlmsghdr n;
    struct ndmsg ndm;
    char buf[0];
  } req = {
      .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg)),
      .n.nlmsg_type = RTM_GETNEIGH,
      .n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT,
  };

  send_recv(&req, req.n.nlmsg_len, &rb);
  free(rb);

  assert(log_called == 1);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {{"parse_rtattr", test_parse_rtattr}};
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All netlink_arp_cache tests passed! ======\n" KNRM);
  return rc;
}
