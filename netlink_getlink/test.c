#include "libnl_getlink.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../test_util.h"

static int log_called = 0;

static void mock_log(int pri, const char *func, const char *file, int line,
                     const char *fmt, bool nl, ...) {
  (void)pri;
  (void)func;
  (void)file;
  (void)line;
  (void)fmt;
  (void)nl;
  log_called++;
}

static int mock_time(clockid_t clk, struct timespec *ts) {
  (void)clk;
  ts->tv_sec = 0;
  ts->tv_nsec = 0;
  return 0;
}

static void test_get_netdev_list(void) {
  netlink_getlink_mod_init(&(netlink_getlink_mod_init_args_t){
      .log = mock_log,
      .get_time = mock_time});
  struct slist_head list;
  INIT_SLIST_HEAD(&list);

  int ret = get_netdev(&list);
  assert(ret == 0 && "get_netdev should succeed");
  assert(slist_empty(&list) == 0 && "netdev list should not be empty");

  free_netdev_list(&list);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {{"get_netdev_list", test_get_netdev_list}};
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All netlink_getlink tests passed! ======\n" KNRM);
  return rc;
}
