// test.c - unit tests for the syslog2 module
#include "syslog2.h"

#include "../test_util.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int log_called = 0;
static int time_called = 0;

static void syslog2_mock(int pri, const char *func, const char *filename, int line, const char *fmt, bool add_nl, va_list ap) {
  log_called++;

  char msg[4096];
  vsnprintf(msg, sizeof(msg), fmt, ap);

  printf("%s:%d %s: %s%s", filename, line, func, msg, add_nl ? "\n" : "");
}

static int64_t get_tz_off_func_mock(void) {
  time_t t = time(NULL);
  struct tm local_tm = {0};
#if defined(__GLIBC__) && defined(__USE_MISC)
  localtime_r(&t, &local_tm);
  return local_tm.tm_gmtoff;
#else
  struct tm utc_tm = {0};
  gmtime_r(&t, &utc_tm);
  return (local_tm.tm_hour - utc_tm.tm_hour) * 3600 +
         (local_tm.tm_min - utc_tm.tm_min) * 60;
#endif
}

static int clock_gettime_realtime_fast_mock(struct timespec *ts) {
  time_called++;
  return clock_gettime(CLOCK_REALTIME, ts);
}

// helper to capture stdout using open_memstream
static FILE *capture_start(char **buf, size_t *size, FILE **old) {
  *buf = NULL;
  *size = 0;
  FILE *mem = open_memstream(buf, size);
  if (!mem) {
    perror("open_memstream");
    exit(EXIT_FAILURE);
  }
  *old = stdout;
  fflush(stdout);
  stdout = mem;
  return mem;
}

static void capture_end(FILE *mem, FILE *old) {
  fflush(stdout);
  stdout = old;
  fclose(mem);
}

static void test_setlogmask2(void) {
  PRINT_TEST_START(__func__);
  setlogmask2(LOG_UPTO(LOG_ERR));
  assert(cached_mask == LOG_UPTO(LOG_ERR));

  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2(LOG_DEBUG, "hidden message");
  capture_end(mem, old);
  if (sz > 0 && out) {
    assert(strstr(out, "hidden message") == NULL);
  }
  free(out);

  setlogmask2(LOG_UPTO(LOG_DEBUG));
  assert(cached_mask == LOG_UPTO(LOG_DEBUG));

  out = NULL;
  sz = 0;
  mem = capture_start(&out, &sz, &old);
  syslog2(LOG_DEBUG, "visible message");
  capture_end(mem, old);
  PRINT_TEST_INFO("out='%s'", out);
  assert(out && ((strstr(out, "visible message") != NULL)));
  free(out);
}

static void test_setlogmask2_edges(void) {
  PRINT_TEST_START(__func__);

  setlogmask2(0);
  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2(LOG_ERR, "blocked");
  capture_end(mem, old);
  if (sz > 0 && out) {
    assert(strstr(out, "blocked") == NULL);
  }
  free(out);

  setlogmask2(-1);
  out = NULL;
  sz = 0;
  mem = capture_start(&out, &sz, &old);
  syslog2(LOG_NOTICE, "allowed again");
  capture_end(mem, old);
  assert(out && strstr(out, "allowed again") != NULL);
  free(out);
}

static void test_inline_pthread_set_name(void) {
  PRINT_TEST_START(__func__);

  const char *longname = "ABCDEFGHIJKLMNOPQRSTUV";
  inline_pthread_set_name(longname, strlen(longname));

  char buf[16] = {0};
  int ret = pthread_getname_np(pthread_self(), buf, sizeof(buf));
  assert(ret == 0);
  assert(strcmp(buf, "ABCDEFGHIJKLMNO") == 0);
}

static void test_print_last_functions(void) {
  PRINT_TEST_START(__func__);

  SET_CURRENT_FUNCTION();
  syslog2_mod_init(NULL);

  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  print_last_functions();
  capture_end(mem, old);

  PRINT_TEST_INFO("out='%s'", out);
  assert(out && (strstr(out, "last called functions by threads") != NULL));
  assert(strstr(out, "test_print_last_functions") != NULL);
  free(out);
}

static void test_syslog2_printf(void) {
  PRINT_TEST_START(__func__);

  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2_printf(LOG_WARNING, "printf test %d", 7);
  capture_end(mem, old);
  assert(out && strstr(out, "printf test 7") != NULL);
  free(out);
}

static void test_syslog_branch(void) {
  PRINT_TEST_START(__func__);

  setup_syslog2("test_syslog_branch", LOG_DEBUG, true);
  syslog2(LOG_INFO, "branch to syslog");
  syslog2_printf(LOG_INFO, "syslog printf");
  setup_syslog2("test_syslog2", LOG_DEBUG, false);
  syslog2(LOG_INFO, "branch to syslog");
  syslog2_printf(LOG_INFO, "syslog printf");
}

static void test_syslog2_mod_init(void) {
  PRINT_TEST_START(__func__);

  syslog2_mod_init_args_t args = {
      .realtime_func = clock_gettime_realtime_fast_mock,
      .get_tz_off_func = get_tz_off_func_mock,
      .syslog2_func = syslog2_mock};
  syslog2_mod_init(&args);
  // with syslog scenario
  setup_syslog2("init_syslog", LOG_DEBUG, true);
  log_called = 0;
  syslog2(LOG_INFO, "syslog entry");

  PRINT_TEST_INFO("log_called=%d", log_called);
  assert(log_called == 1);

  // without syslog scenario
  setup_syslog2("init_syslog", LOG_DEBUG, false);
  log_called = 0;
  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2(LOG_INFO, "syslog entry2");
  capture_end(mem, old);
  free(out);
  assert(log_called == 1);

  syslog2_mod_init(NULL);
}

int main(int argc, char **argv) {
  setup_syslog2("test_syslog2", LOG_DEBUG, false);

  struct test_entry tests[] = {
      {"setlogmask2", test_setlogmask2},
      {"setlogmask2_edges", test_setlogmask2_edges},
      {"inline_pthread_set_name", test_inline_pthread_set_name},
      {"print_last_functions", test_print_last_functions},
      {"syslog2_printf", test_syslog2_printf},
      {"syslog_branch", test_syslog_branch},
      {"syslog2_mod_init", test_syslog2_mod_init},

  };

  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== all syslog2 tests passed ======\n" KNRM);
  return rc;
}
