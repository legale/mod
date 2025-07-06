// test.c - unit tests for the syslog2 module
#include "syslog2.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Helper to capture stdout using open_memstream
static FILE *capture_start(char **buf, size_t *size, FILE **old) {
  *buf = NULL;
  *size = 0;
  FILE *mem = open_memstream(buf, size);
  *old = stdout;
  stdout = mem;
  return mem;
}

static void capture_end(FILE *mem, FILE *old) {
  fflush(stdout);
  stdout = old;
  fclose(mem);
}

static void test_setlogmask2(void) {
  // disallow debug messages
  setlogmask2(LOG_UPTO(LOG_ERR));
  assert(cached_mask == LOG_UPTO(LOG_ERR));

  // capture output and ensure debug messages are filtered out
  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2(LOG_DEBUG, "hidden message");
  capture_end(mem, old);
  if (sz > 0) {
    assert(strstr(out, "hidden message") == NULL);
  }
  free(out);

  // allow debug messages again
  setlogmask2(LOG_UPTO(LOG_DEBUG));
  assert(cached_mask == LOG_UPTO(LOG_DEBUG));

  out = NULL;
  sz = 0;
  mem = capture_start(&out, &sz, &old);
  syslog2(LOG_DEBUG, "visible message");
  capture_end(mem, old);
  assert(strstr(out, "visible message") != NULL);
  free(out);
}

static void test_setlogmask2_edges(void) {
  // mask out everything
  setlogmask2(0);
  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2(LOG_ERR, "blocked");
  capture_end(mem, old);
  if (sz > 0) {
    assert(strstr(out, "blocked") == NULL);
  }
  free(out);

  // enable all bits explicitly
  setlogmask2(-1);
  out = NULL;
  sz = 0;
  mem = capture_start(&out, &sz, &old);
  syslog2(LOG_NOTICE, "allowed again");
  capture_end(mem, old);
  assert(strstr(out, "allowed again") != NULL);
  free(out);
}

static void test_debug_call(void) {
  // exercise the debug() wrapper even when DEBUG is not defined
  debug("unused message %d", 1);
}

static void test_inline_pthread_set_name(void) {
  const char *longname = "ABCDEFGHIJKLMNOPQRSTUV"; // >15 chars
  inline_pthread_set_name(longname, strlen(longname));

  char buf[16];
  int ret = pthread_getname_np(pthread_self(), buf, sizeof(buf));
  assert(ret == 0);
  assert(strcmp(buf, "ABCDEFGHIJKLMNO") == 0); // truncated to 15 chars
}

static void test_print_last_functions(void) {
  // record current function and print list
  SET_CURRENT_FUNCTION();

  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  print_last_functions();
  capture_end(mem, old);

  assert(strstr(out, "last called functions by threads") != NULL);
  assert(strstr(out, "test_print_last_functions") != NULL);
  free(out);
}

static void test_syslog2_printf(void) {
  char *out = NULL;
  size_t sz = 0;
  FILE *old, *mem = capture_start(&out, &sz, &old);
  syslog2_printf(LOG_WARNING, "printf test %d", 7);
  capture_end(mem, old);
  assert(strstr(out, "printf test 7") != NULL);
  free(out);
}

static void test_syslog_branch(void) {
  // enable syslog output to execute syslog-specific paths
  setup_syslog2("test_syslog_branch", LOG_DEBUG, true);
  syslog2(LOG_INFO, "branch to syslog");
  syslog2_printf(LOG_INFO, "syslog printf");
  // switch back to stdout logging for the rest of the tests
  setup_syslog2("test_syslog2", LOG_DEBUG, false);
}

int main(void) {
  setup_syslog2("test_syslog2", LOG_DEBUG, false);

  syslog2(LOG_INFO, "Starting test");

  test_setlogmask2();
  test_setlogmask2_edges();
  test_inline_pthread_set_name();
  test_print_last_functions();
  test_debug_call();
  test_syslog2_printf();
  test_syslog_branch();

  printf("All syslog2 tests passed!\n");
  return 0;
}
