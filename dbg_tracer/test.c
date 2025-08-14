#define _GNU_SOURCE
#include "dbg_tracer.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../test_util.h"

/* tiny helpers */

static void set_name(const char *s) {
  if (s && *s) prctl(PR_SET_NAME, s);
}

static void expect_exit_or_sig(int status, int exp_sig) {
  /* segv handler path exits with 128+SIG, default path is signaled */
  if (WIFEXITED(status)) {
    int ec = WEXITSTATUS(status);
    assert(ec == 128 + exp_sig && "expected exit 128+sig");
    return;
  }
  assert(WIFSIGNALED(status) && "expected signaled");
  int sig = WTERMSIG(status);
  assert(sig == exp_sig && "unexpected terminating signal");
}

/* child runner */

typedef void (*child_fn_t)(void);

static void run_child_and_expect_sig(child_fn_t fn, int exp_sig) {
  pid_t pid = fork();
  assert(pid >= 0 && "fork failed");
  if (pid == 0) {
    fn();
    _exit(0);
  }
  int st = 0;
  for (;;) {
    pid_t r = waitpid(pid, &st, 0);
    if (r < 0 && errno == EINTR) continue;
    assert(r == pid && "waitpid failed");
    break;
  }
  expect_exit_or_sig(st, exp_sig);
}

static void run_child_and_expect_ok(child_fn_t fn) {
  pid_t pid = fork();
  assert(pid >= 0 && "fork failed");
  if (pid == 0) {
    fn();
    _exit(0);
  }
  int st = 0;
  for (;;) {
    pid_t r = waitpid(pid, &st, 0);
    if (r < 0 && errno == EINTR) continue;
    assert(r == pid && "waitpid failed");
    break;
  }
  assert(WIFEXITED(st) && WEXITSTATUS(st) == 0 && "child failed");
}

/* threads */

struct thr_arg {
  int marks;
  const char *name;
};

static void *thr_body(void *arg_) {
  struct thr_arg *a = arg_;
  trace_reg_attach_name(a->name);
  for (int i = 0; i < a->marks; i++) {
    TRACE();
    /* small jitter */
    struct timespec ts = {0, 1000000};
    nanosleep(&ts, NULL);
  }
  return NULL;
}

/* crash functions */

static void fn_crash_memwrite(void) {
  tracer_setup();
  tracer_install_segv();
  trace_reg_attach_name("crasher");
  TRACE();
  volatile int *p = NULL;
  *p = 123; /* segv */
}

static void fn_crash_snprintf_no_handler(void) {
  tracer_setup();
  trace_reg_attach_name("fmt_crasher");
  char b[8];
  /* overflow triggers raise(SIGSEGV) inside safe_snprintf, no handler installed */
  safe_snprintf(b, sizeof(b), "0123456789%u", 42u);
  (void)b;
}

static void fn_manual_dump_to_file_ok(void) {
  tracer_setup();
  trace_reg_attach_name("manual");

  char fname[] = "/tmp/dbg_tracer_test_XXXXXX";
  int fd = mkstemp(fname);
  assert(fd >= 0 && "mkstemp failed");

  TRACE();
  tracer_dump_now_fd(fd);

  lseek(fd, 0, SEEK_SET);
  char buf[65536];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  unlink(fname); // удалить файл после чтения
  printf("BUF: '%s'", buf);
  assert(n > 0 && "empty dump");
  assert(strstr(buf, "trace tid=") && "no trace header");
  assert(strstr(buf, "addr2line_") && "no addr2line hints");
}

static void fn_trace_basic_ok(void) {
  tracer_setup();
  trace_reg_attach();
  TRACE();
  TRACE();
  tracer_dump_now();
}

static void fn_threading_many_traces_ok(void) {
  tracer_setup();
  enum { N = 4 };
  pthread_t th[N];
  struct thr_arg args[N];
  for (int i = 0; i < N; i++) {
    static char names[4][16];
    snprintf(names[i], sizeof(names[i]), "t%d", i);
    args[i].marks = 16;
    args[i].name = names[i];
    int rc = pthread_create(&th[i], NULL, thr_body, &args[i]);
    assert(rc == 0 && "pthread_create failed");
  }
  for (int i = 0; i < N; i++)
    pthread_join(th[i], NULL);
  tracer_dump_now();
}

static void fn_set_thread_name_ok(void) {
  tracer_setup();
  trace_reg_attach_name("mainA");
  set_name("mainB");
  trace_set_thread_name("mainC");
  char got[16] = {0};
  prctl(PR_GET_NAME, got);
  assert(strcmp(got, "mainC") == 0 && "name not set");
}

/* tests */

static void test_segv_handler_dump(void) {
  PRINT_TEST_START("segv handler: dump + exit 128+SIGSEGV");
  run_child_and_expect_sig(fn_crash_memwrite, SIGSEGV);
  PRINT_TEST_PASSED();
}

static void test_snprintf_raises_sigsegv(void) {
  PRINT_TEST_START("safe_snprintf overflow: default SIGSEGV");
  run_child_and_expect_sig(fn_crash_snprintf_no_handler, SIGSEGV);
  PRINT_TEST_PASSED();
}

static void test_manual_dump_fd_capture(void) {
  PRINT_TEST_START("manual dump to file contains headers");
  run_child_and_expect_ok(fn_manual_dump_to_file_ok);
  PRINT_TEST_PASSED();
}

static void test_trace_macro_basic(void) {
  PRINT_TEST_START("TRACE macro basic smoke");
  run_child_and_expect_ok(fn_trace_basic_ok);
  PRINT_TEST_PASSED();
}

static void test_multithread_tracing(void) {
  PRINT_TEST_START("multithread tracing and dump");
  run_child_and_expect_ok(fn_threading_many_traces_ok);
  PRINT_TEST_PASSED();
}

static void test_set_thread_name(void) {
  PRINT_TEST_START("set thread name api");
  run_child_and_expect_ok(fn_set_thread_name_ok);
  PRINT_TEST_PASSED();
}

/* main */
int main(int argc, char **argv) {
  struct test_entry tests[] = {
      {"segv_handler_dump", test_segv_handler_dump},
      {"safe_snprintf_sigsegv", test_snprintf_raises_sigsegv},
      {"manual_dump_fd_capture", test_manual_dump_fd_capture},
      {"trace_macro_basic", test_trace_macro_basic},
      {"multithread_tracing", test_multithread_tracing},
      {"set_thread_name", test_set_thread_name},
  };
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All dbg_tracer tests passed! ======\n" KNRM);
  return rc;
}
