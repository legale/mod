#include "../test_util.h"
#include "libnl_getlink.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

static void test_get_netdev_list(void) {
  struct slist_head list;
  INIT_SLIST_HEAD(&list);

  int ret = get_netdev(&list);
  assert(ret == 0 && "get_netdev return 0 expected");
  assert(slist_empty(&list) == 0 && "netdev list should not be empty");

  free_netdev_list(&list);
  PRINT_TEST_PASSED();
}

static void test_default_logger_stdout(void) {
  fflush(stdout);
  int orig_fd = dup(STDOUT_FILENO);
  FILE *tmp = tmpfile();
  assert(tmp && "tmpfile failed");
  dup2(fileno(tmp), STDOUT_FILENO);

  struct slist_head list;
  INIT_SLIST_HEAD(&list);

  int ret = get_netdev(&list);
  assert(ret == 0 && "get_netdev should succeed");

  fflush(stdout);
  fseek(tmp, 0, SEEK_SET);
  int c = fgetc(tmp);
  assert(c != EOF && "default logger should output to stdout");

  dup2(orig_fd, STDOUT_FILENO);
  close(orig_fd);
  fclose(tmp);

  free_netdev_list(&list);
  PRINT_TEST_PASSED();
}

static volatile int stress_stop;

static void *recv_worker(void *arg) {
  (void)arg;
  while (!stress_stop) {
    struct slist_head list;
    INIT_SLIST_HEAD(&list);
    get_netdev(&list);
    free_netdev_list(&list);
  }
  return NULL;
}

static void *if_worker(void *arg) {
  int id = (int)(intptr_t)arg;
  char br[32];
  char veth0[32];
  char veth1[32];
  char cmd[128];
  snprintf(br, sizeof(br), "brstress%d", id);
  snprintf(veth0, sizeof(veth0), "veths%da", id);
  snprintf(veth1, sizeof(veth1), "veths%db", id);
  int r;
  while (!stress_stop) {
    switch (rand() % 4) {
    case 0:
      snprintf(cmd, sizeof(cmd),
               "ip link add %s type bridge >/dev/null 2>&1", br);
      r = system(cmd);
      (void)r;
      break;
    case 1:
      snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", br);
      r = system(cmd);
      (void)r;
      break;
    case 2:
      snprintf(cmd, sizeof(cmd),
               "ip link add %s type veth peer name %s >/dev/null 2>&1",
               veth0, veth1);
      r = system(cmd);
      (void)r;
      break;
    default:
      snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", veth0);
      r = system(cmd);
      (void)r;
      snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", veth1);
      r = system(cmd);
      (void)r;
      break;
    }
  }
  snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", br);
  r = system(cmd);
  (void)r;
  snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", veth0);
  r = system(cmd);
  (void)r;
  snprintf(cmd, sizeof(cmd), "ip link del %s >/dev/null 2>&1", veth1);
  r = system(cmd);
  (void)r;
  return NULL;
}

static void test_stress_recv_msg_chunk(void) {
  int ret = system("ip link add __stress_dummy0 type dummy >/dev/null 2>&1");
  if (ret != 0) {
    PRINT_TEST_INFO("stress test skipped: requires NET_ADMIN");
    PRINT_TEST_PASSED();
    return;
  }
  ret = system("ip link del __stress_dummy0 >/dev/null 2>&1");
  (void)ret;

  stress_stop = 0;
  srand(time(NULL));
  const int recv_cnt = 80;
  const int if_cnt = 40;
  pthread_t recv_threads[recv_cnt];
  pthread_t mod_threads[if_cnt];
  for (int i = 0; i < recv_cnt; i++)
    pthread_create(&recv_threads[i], NULL, recv_worker, NULL);
  for (int i = 0; i < if_cnt; i++)
    pthread_create(&mod_threads[i], NULL, if_worker, (void *)(intptr_t)i);
  sleep(5);
  stress_stop = 1;
  for (int i = 0; i < recv_cnt; i++)
    pthread_join(recv_threads[i], NULL);
  for (int i = 0; i < if_cnt; i++)
    pthread_join(mod_threads[i], NULL);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {
      {"get_netdev_list", test_get_netdev_list},
      {"default_logger_stdout", test_default_logger_stdout},
      {"stress_recv_msg_chunk", test_stress_recv_msg_chunk}};
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All netlink_getlink tests passed! ======\n" KNRM);
  return rc;
}
