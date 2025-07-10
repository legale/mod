#include "../test_util.h"
#include "libnl_getlink.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static int log_called = 0;

static void syslog2_mock(int pri, const char *func, const char *filename, int line, const char *fmt, bool add_nl, va_list ap) {
  log_called++;

  char msg[4096];
  vsnprintf(msg, sizeof(msg), fmt, ap);

  printf("%s:%d %s: %s%s", filename, line, func, msg, add_nl ? "\n" : "");
}

static void test_get_netdev_list(void) {
  netlink_getlink_mod_init(&(netlink_getlink_mod_init_args_t){
      .syslog2_func = syslog2_mock,
  });

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
