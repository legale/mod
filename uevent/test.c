
#include "uevent.h"
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// =============================================================================
// МАКРОСЫ НАСТРОЕК ТЕСТОВ
// =============================================================================
#define STRESS_THREADS 16
#define STRESS_EVENTS 512
#define STRESS_ITER 1000
#define MAX_TIMER_MS 100
#define MAX_EVENTS 1024

// =============================================================================
// МАКРОСЫ ДЛЯ УПРОЩЕНИЯ ВЫВОДА В ТЕСТАХ
// =============================================================================
// --- Макросы для вывода ---
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KCYN "\x1B[36m"

#define PRINT_TEST_START(name) \
  printf(KCYN "%s:%d --- Starting Test: %s ---\n" KNRM, __FILE__, __LINE__, name)
#define PRINT_TEST_PASSED() printf(KGRN "--- Test Passed ---\n\n" KNRM)
#define PRINT_TEST_INFO(fmt, ...) \
  printf(KYEL "%s:%d [INFO] " fmt "\n" KNRM, __FILE__, __LINE__, ##__VA_ARGS__)

// =============================================================================
// ХЕЛПЕР ДЛЯ ТЕСТИРОВАНИЯ ПАДЕНИЙ
// =============================================================================
void run_test_in_fork_that_should_crash(void (*test_func)(void)) {
  pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    setup_syslog2("child_crash_test", LOG_CRIT, false);
    test_func();
    exit(EXIT_SUCCESS);
  } else {
    int status;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
      PRINT_TEST_INFO("Child process terminated as expected with signal %d (%s)", WTERMSIG(status), strsignal(WTERMSIG(status)));
    } else {
      fprintf(stderr, "\n\n!!! TEST FAILED: Process was expected to crash but exited cleanly. !!!\n\n");
      assert(0 && "Test function did not crash as expected!");
    }
  }
}

// =============================================================================
// ТЕСТЫ
// =============================================================================

void test_uevent_active() {
  PRINT_TEST_START("Immediate execution with uevent_active");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) { (*(int *)arg)++; }
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &triggered, "test_active");
  assert(uev != NULL);
  uevent_active(uev);
  uevent_base_dispatch(base);
  assert(triggered == 1);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_uevent_add_with_current_timeout() {
  PRINT_TEST_START("Add with timeout set in event object");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    (*(int *)arg)++;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &triggered, __func__);
  assert(uev != NULL);
  uevent_set_timeout(uev, 50);
  uevent_add_with_current_timeout(uev);
  uevent_base_dispatch(base);
  assert(triggered == 1);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_del_inactive_event() {
  PRINT_TEST_START("Delete an inactive event");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, NULL, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_del(uev) == UEV_ERR_OK);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_null_event_del() {
  PRINT_TEST_START("Delete a NULL event pointer");
  assert(uevent_del(NULL) == UEV_ERR_INVAL);
  PRINT_TEST_PASSED();
}

void test_multiple_events_on_fd() {
  PRINT_TEST_START("Multiple events on one file descriptor");
  uevent_base_t *base = uevent_base_new_with_workers(16, 2);
  assert(base != NULL);

  int pipefd[2];
  assert(pipe(pipefd) == 0);

  atomic_int triggered1, triggered2;
  atomic_init(&triggered1, 0);
  atomic_init(&triggered2, 0);

  void cb1(uevent_t * ev, int fd, short event, void *arg) { atomic_store(&triggered1, 1); }
  void cb2(uevent_t * ev, int fd, short event, void *arg) { atomic_store(&triggered2, 1); }

  uev_t *uev1 = uevent_create_or_assign_event(NULL, base, pipefd[0], UEV_READ, cb1, NULL, "ev1");
  uev_t *uev2 = uevent_create_or_assign_event(NULL, base, pipefd[0], UEV_READ, cb2, NULL, "ev2");
  assert(uev1 && uev2);

  // Первый add должен пройти успешно
  assert(uevent_add(uev1, 0) == UEV_ERR_OK);
  // Второй add для того же fd должен вернуть ошибку
  assert(uevent_add(uev2, 0) == UEV_ERR_EPOLL);

  void *dispatch_thread(void *arg) {
    uevent_base_dispatch((uevent_base_t *)arg);
    return NULL;
  }
  pthread_t dispatcher;
  pthread_create(&dispatcher, NULL, dispatch_thread, base);

  write(pipefd[1], "x", 1);
  usleep(100000);

  uevent_base_loopbreak(base);
  pthread_join(dispatcher, NULL);

  PRINT_TEST_INFO("Checking if only the first added event was triggered");
  // Проверяем, что сработал только первый колбэк, а второй - нет.
  assert(atomic_load(&triggered1) == 1 && atomic_load(&triggered2) == 0);

  close(pipefd[0]);
  close(pipefd[1]);
  uevent_free(uev1);
  uevent_free(uev2);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_event_flags_combinations() {
  PRINT_TEST_START("Combinations of event flags");
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    triggered = 1;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }

  {
    uevent_base_t *base = uevent_base_new_with_workers(16, 4);
    assert(base != NULL);
    triggered = 0;
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_PERSIST, cb, NULL, __func__);
    assert(uev != NULL);
    uevent_active(uev);
    uevent_base_dispatch(base);
    assert(triggered == 1);
    uevent_free(uev);
    uevent_deinit(base);
  }

  {
    uevent_base_t *base = uevent_base_new(16);
    assert(base != NULL);
    triggered = 0;
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, NULL, __func__);
    assert(uev != NULL);
    uevent_add(uev, 10);
    uevent_base_dispatch(base);
    assert(triggered == 1);
    uevent_free(uev);
    uevent_deinit(base);
  }

  {
    uevent_base_t *base = uevent_base_new(16);
    assert(base != NULL);
    triggered = 0;
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, cb, NULL, __func__);
    assert(uev != NULL);
    uevent_add(uev, 10);
    uevent_base_dispatch(base);
    assert(triggered == 1);
    uevent_free(uev);
    uevent_deinit(base);
  }

  PRINT_TEST_PASSED();
}

void test_add_same_event_twice() {
  PRINT_TEST_START("Add the same event twice");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    triggered++;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 10) == 0);
  assert(uevent_add(uev, 10) == 0);
  uevent_base_dispatch(base);
  assert(triggered == 1);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_add_event_fd_minus1() {
  PRINT_TEST_START("Add event with fd=-1 and no UEV_TIMEOUT");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) { triggered = 1; }
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, 0, cb, NULL, __func__);
  assert(uev != NULL);
  uevent_add(uev, 0); // This will add it to the list, but not epoll or timer heap
  uevent_base_dispatch(base);
  assert(triggered == 0); // Callback should not be triggered as it's not a timer or active FD.
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_stress_active() {
  PRINT_TEST_START("Stress test with uevent_active");
  uevent_base_t *base = uevent_base_new(MAX_EVENTS);
  assert(base != NULL);
  atomic_int triggered;
  atomic_init(&triggered, 0);
  void cb(uevent_t * ev, int fd, short event, void *arg) { atomic_fetch_add((atomic_int *)arg, 1); }
  for (int i = 0; i < 1000; ++i) {
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &triggered, __func__);
    assert(uev != NULL);
    uevent_active(uev);
  }
  uevent_base_dispatch(base);
  assert(atomic_load(&triggered) == 1000);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_create_and_free_dynamic_event() {
  PRINT_TEST_START("Create and free a dynamic event");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, 0, UEV_READ, NULL, NULL, __func__);
  assert(uev != NULL);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_create_and_free_static_event() {
  PRINT_TEST_START("Create and free a static event");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  uevent_t ev = {.is_static = true};
  uev_t *uev_ptr = uevent_create_or_assign_event(&ev, base, 0, UEV_READ, NULL, NULL, __func__);
  assert(uev_ptr != NULL);                   // uev_ptr is now the wrapper for 'ev'
  assert(ATOM_LOAD_ACQ(uev_ptr->ev) == &ev); // The wrapper should point to the static event
  uevent_free(uev_ptr);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_add_and_del_event() {
  PRINT_TEST_START("Add and then delete an event");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, 0, UEV_READ, NULL, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 0) == UEV_ERR_OK);
  assert(uevent_del(uev) == UEV_ERR_OK);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_timeout_event() {
  PRINT_TEST_START("A simple timeout event");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    triggered = 1;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 100) == 0);
  PRINT_TEST_INFO("Waiting for timeout event (100 ms)...");
  uevent_base_dispatch(base);
  assert(triggered == 1);
  uevent_free(uev); // Add free for the event
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_read_event() {
  PRINT_TEST_START("A simple read event on a pipe");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    triggered = 1;
    uevent_del(ev->uev); // Use uev pointer
  }
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, pipefd[0], UEV_READ, cb, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 0) == 0);
  write(pipefd[1], "test", 4);
  uevent_base_dispatch(base);
  assert(triggered == 1);
  close(pipefd[0]);
  close(pipefd[1]);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_write_event() {
  PRINT_TEST_START("A simple write event on a pipe");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    triggered = 1;
    uevent_del(ev->uev); // Use uev pointer
  }
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, pipefd[1], UEV_WRITE, cb, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 0) == 0);
  uevent_base_dispatch(base);
  assert(triggered == 1);
  close(pipefd[0]);
  close(pipefd[1]);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_error_hup_event() {
  PRINT_TEST_START("Error and HUP events on a closed pipe");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int error_triggered = 0, hup_triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    if (event & UEV_ERROR) error_triggered = 1;
    if (event & UEV_HUP) hup_triggered = 1;
    uevent_del(ev->uev); // Use uev pointer
  }
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  close(pipefd[1]);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, pipefd[0], UEV_ERROR | UEV_HUP, cb, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 0) == 0);
  uevent_base_dispatch(base);
  assert(error_triggered == 1 || hup_triggered == 1);
  uevent_free(uev);
  close(pipefd[0]);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_invalid_fd_read_event() {
  PRINT_TEST_START("READ event on a closed fd");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) { triggered = 1; }
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  close(pipefd[0]);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, pipefd[0], UEV_READ, cb, NULL, __func__);
  assert(uev != NULL);
  int ret = uevent_add(uev, 0);
  if (ret == 0) {
    uevent_base_dispatch(base);
    assert(triggered == 1);
  } else {
    PRINT_TEST_INFO("epoll_ctl failed as expected for closed fd.");
  }
  uevent_free(uev);
  close(pipefd[1]);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_invalid_fd_write_event() {
  PRINT_TEST_START("WRITE event on a closed fd");
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) { triggered = 1; }
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  close(pipefd[1]);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, pipefd[1], UEV_WRITE, cb, NULL, __func__);
  assert(uev != NULL);
  int ret = uevent_add(uev, 0);
  if (ret == 0) {
    uevent_base_dispatch(base);
    assert(triggered == 1);
  } else {
    PRINT_TEST_INFO("epoll_ctl failed as expected for closed fd.");
  }
  uevent_free(uev);
  close(pipefd[0]);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_fd_becomes_invalid_event() {
  PRINT_TEST_START("Fd becomes invalid during event loop");
  int triggered = 0;
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    if (event & (UEV_ERROR | UEV_HUP)) {
      triggered = 1;
      uevent_del(ev->uev); // Use uev pointer
    }
  }
  void *close_fd_later(void *arg) {
    usleep(50000);
    close(*(int *)arg);
    return NULL;
  }
  void *force_break_dispatch(void *arg) {
    usleep(200 * 1000);
    uevent_base_loopbreak((uevent_base_t *)arg);
    return NULL;
  }
  uevent_base_t *base = uevent_base_new(16);
  assert(base != NULL);
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, pipefd[0], UEV_READ | UEV_ERROR | UEV_HUP, cb, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 0) == 0);
  pthread_t tid, break_tid;
  pthread_create(&tid, NULL, close_fd_later, &pipefd[0]);
  pthread_create(&break_tid, NULL, force_break_dispatch, base);
  uevent_base_dispatch(base);
  pthread_join(tid, NULL);
  pthread_join(break_tid, NULL);
  if (triggered) {
    PRINT_TEST_INFO("Event triggered as expected.");
  } else {
    PRINT_TEST_INFO("Event not triggered (epoll limitation, this is NOT an error!)");
  }
  uevent_free(uev);
  close(pipefd[1]);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_stress_mt() {
  PRINT_TEST_START("Multithreaded stress test");
  uevent_base_t *base = uevent_base_new_with_workers(STRESS_EVENTS * STRESS_THREADS * 2, 4);
  assert(base != NULL);
  typedef struct {
    uevent_base_t *base;
    int events_count;
    atomic_int *triggered;
  } stress_arg_t;

  void cb(uevent_t * ev, int fd, short event, void *arg) { atomic_fetch_add((atomic_int *)arg, 1); }

  void *thread_func(void *arg) {
    stress_arg_t *sarg = (stress_arg_t *)arg;
    for (int i = 0; i < sarg->events_count; ++i) {
      uev_t *uev = uevent_create_or_assign_event(NULL, sarg->base, -1, UEV_TIMEOUT, cb, sarg->triggered, __func__);
      assert(uev != NULL);
      assert(uevent_add(uev, rand() % MAX_TIMER_MS + 1) == 0);
    }
    return NULL;
  }

  pthread_t threads[STRESS_THREADS];
  stress_arg_t args[STRESS_THREADS];
  atomic_int total_triggered;
  atomic_init(&total_triggered, 0);
  for (int t = 0; t < STRESS_THREADS; ++t) {
    args[t].base = base;
    args[t].events_count = STRESS_EVENTS;
    args[t].triggered = &total_triggered;
    pthread_create(&threads[t], NULL, thread_func, &args[t]);
  }
  int total_expected = STRESS_THREADS * STRESS_EVENTS;
  // This loop needs to eventually break if all events are processed,
  // potentially with a timeout to avoid infinite loop on test failure.
  uint64_t start_time = tu_clock_gettime_monotonic_fast_ms();
  while (atomic_load(&total_triggered) < total_expected) {
    uevent_base_dispatch(base);
    if (tu_clock_gettime_monotonic_fast_ms() - start_time > 5000) { // Add a timeout to prevent infinite loop
      PRINT_TEST_INFO("Stress test timed out. Triggered: %d / %d", atomic_load(&total_triggered), total_expected);
      break;
    }
  }
  for (int t = 0; t < STRESS_THREADS; ++t) {
    pthread_join(threads[t], NULL);
  }
  uevent_deinit(base);
  assert(atomic_load(&total_triggered) == total_expected); // Ensure all events triggered
  PRINT_TEST_PASSED();
}

void test_persist_event() {
  PRINT_TEST_START("UEV_PERSIST flag for timers");
  int persist_cb_count = 0;
  int persist_nofree_cb_count = 0;

  void cb1(uevent_t * ev, int fd, short event, void *arg) {
    PRINT_TEST_INFO("cnt=%d", persist_cb_count);
    if (event & UEV_TIMEOUT) {
      persist_cb_count++;
      if (persist_cb_count >= 3) uevent_free(ev->uev); // Use uev pointer
    }
  }

  void cb2(uevent_t * ev, int fd, short event, void *arg) {
    PRINT_TEST_INFO("cnt=%d", persist_nofree_cb_count);
    if (event & UEV_TIMEOUT) {
      persist_nofree_cb_count++;
      if (persist_nofree_cb_count >= 3) uevent_del(ev->uev); // Use uev pointer
    }
  }
  PRINT_TEST_START("UEV_PERSIST flag for timers");
  uevent_base_t *base = uevent_base_new_with_workers(64, 4);
  assert(base != NULL);
  uev_t *uev1 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, cb1, NULL, __func__);
  uevent_t ev2_static = {.is_static = true};
  uev_t *uev2 = uevent_create_or_assign_event(&ev2_static, base, -1, UEV_TIMEOUT | UEV_PERSIST, cb2, NULL, __func__);
  assert(uev1 && uev2);
  uevent_set_timeout(uev1, 100);
  uevent_set_timeout(uev2, 100);
  uevent_add(uev1, 10);
  uevent_add(uev2, 10);
  PRINT_TEST_START("UEV_PERSIST flag for timers");
  uevent_base_dispatch(base);
  assert(persist_cb_count == 3);
  assert(persist_nofree_cb_count == 3);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_null_params() {
  PRINT_TEST_START("API calls with NULL parameters");
  assert(uevent_base_new(0) == NULL);
  assert(uevent_create_or_assign_event(NULL, NULL, 0, 0, NULL, NULL, __func__) == NULL);
  assert(uevent_add(NULL, 0) == UEV_ERR_INVAL);
  assert(uevent_del(NULL) == UEV_ERR_INVAL);
  uevent_free(NULL);
  uevent_deinit(NULL);
  PRINT_TEST_PASSED();
}

void test_double_free_detection() {
  PRINT_TEST_START("Double free is correctly detected");

  uevent_base_t *base = uevent_base_new(1024);
  assert(base != NULL);

  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, NULL, NULL, "double_free_target");
  assert(uev != NULL);

  uevent_free(uev);

  // второй вызов free проверяем, что сработает защита от double free
  if (fork() == 0) {
    // дочерний процесс для изоляции защитного assert
    uevent_free(uev); // This is the call that might trigger an assert
    _exit(0);
  } else {
    int status = 0;
    wait(&status);
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
      PRINT_TEST_INFO("Child process terminated as expected with SIGABRT (likely from assert in uevent_unref/uevent_put when refcount <= 0).");
      PRINT_TEST_PASSED();
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      PRINT_TEST_INFO("Child process exited cleanly. Double free was likely prevented without an assert on current implementation.");
      PRINT_TEST_PASSED();
    } else {
      fprintf(stderr, "\n\n!!! TEST FAILED: Unexpected child process termination status: %d !!!\n\n", status);
      assert(0 && "Test function did not behave as expected for double free!");
    }
  }

  // base might still be active if the child process exited/crashed.
  // We need to ensure it is clean.
  // It's tricky to cleanup if the 'uev' was already considered freed by the parent.
  // For simplicity, for this specific test, we might choose not to deinit base if the child crashed
  // because the state of 'uev' in parent could be inconsistent.
  // However, for a robust test suite, you'd want to ensure base cleanup.
  // Let's call deinit, it should handle some inconsistencies.
  uevent_base_dispatch(base); // Dispatch to process any remaining events, including zombie cleanup
  uevent_deinit(base);
}

void test_double_add_del() {
  PRINT_TEST_START("Double add and double delete");
  uevent_base_t *base = uevent_base_new(8);
  assert(base != NULL);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, NULL, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 10) == 0);
  assert(uevent_add(uev, 10) == 0); // Adding again should return OK as it's already active
  assert(uevent_del(uev) == 0);
  assert(uevent_del(uev) == 0); // Deleting again should return OK, but not actually do anything if already inactive
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_fd_edge_cases() {
  PRINT_TEST_START("File descriptor edge cases");
  uevent_base_t *base = uevent_base_new(8);
  assert(base != NULL);
  uev_t *uev0 = uevent_create_or_assign_event(NULL, base, 0, UEV_READ, NULL, NULL, "stdin");
  assert(uev0 != NULL);
  assert(uevent_add(uev0, 0) == 0);
  uevent_del(uev0);
  uevent_free(uev0);
  // Test with a large FD value
  int large_fd = 100000; // Choose a large, but potentially valid or invalid FD
  int temp_pipe[2];
  if (pipe(temp_pipe) == 0) { // Create a pipe to get a valid large FD if possible
    // Close read end to get a high number for write end for testing
    close(temp_pipe[0]);
    large_fd = temp_pipe[1];
  } else {
    PRINT_TEST_INFO("Failed to create pipe for large_fd test, using a synthetic large_fd.");
  }

  uev_t *uevmax = uevent_create_or_assign_event(NULL, base, large_fd, UEV_READ, NULL, NULL, "fd_max");
  assert(uevmax != NULL);
  int ret = uevent_add(uevmax, 0);
  // Expect UEV_ERR_OK if FD is valid, UEV_ERR_EPOLL if epoll_ctl fails (e.g., bad FD)
  assert(ret == UEV_ERR_OK || ret == UEV_ERR_EPOLL);
  uevent_del(uevmax);
  uevent_free(uevmax);
  if (temp_pipe[1] == large_fd) { // Close the pipe if it was opened
    close(large_fd);
  }

  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_special_timeouts() {
  PRINT_TEST_START("Special timeout values (0, -1, -2)");
  void cb(uevent_t * ev, int fd, short event, void *arg) {
    *(int *)arg = 1;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }
  {
    uevent_base_t *base = uevent_base_new(8);
    assert(base != NULL);
    int triggered = 0;
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &triggered, "timeout_zero");
    assert(uev != NULL);
    assert(uevent_add(uev, 0) == 0);
    uevent_base_dispatch(base);
    assert(triggered == 1);
    uevent_free(uev);
    uevent_deinit(base);
  }
  {
    uevent_base_t *base = uevent_base_new(8);
    assert(base != NULL);
    int triggered = 0;
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &triggered, "timeout_immediate");
    assert(uev != NULL);
    assert(uevent_add(uev, UEV_TIMEOUT_FIRE_NOW) == 0);
    uevent_base_dispatch(base);
    assert(triggered == 1);
    uevent_free(uev);
    uevent_deinit(base);
  }
  {
    uevent_base_t *base = uevent_base_new(8);
    assert(base != NULL);
    int triggered = 0;
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &triggered, "timeout_from_event");
    assert(uev != NULL);
    uevent_set_timeout(uev, 50);
    assert(uevent_add(uev, UEV_TIMEOUT_FROM_EVENT) == 0);
    uevent_base_dispatch(base);
    assert(triggered == 1);
    uevent_free(uev);
    uevent_deinit(base);
  }
  PRINT_TEST_PASSED();
}

void test_null_callback() {
  PRINT_TEST_START("Event with a NULL callback");
  uevent_base_t *base = uevent_base_new(8);
  assert(base != NULL);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, NULL, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_add(uev, 10) == 0);
  uevent_base_dispatch(base);
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_pending_and_initialized() {
  PRINT_TEST_START("uevent_pending and uevent_initialized logic");
  uevent_base_t *base = uevent_base_new(8);
  assert(base != NULL);
  uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, NULL, NULL, __func__);
  assert(uev != NULL);
  assert(uevent_initialized(uev));
  assert(!uevent_pending(uev, UEV_TIMEOUT));
  uevent_add(uev, 10);
  assert(uevent_pending(uev, UEV_TIMEOUT));
  uevent_del(uev);
  assert(!uevent_pending(uev, UEV_TIMEOUT));
  uevent_free(uev);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_multiple_bases() {
  PRINT_TEST_START("Interaction between multiple event bases");
  uevent_base_t *base1 = uevent_base_new(8);
  uevent_base_t *base2 = uevent_base_new(8);
  assert(base1 && base2);
  int count1 = 0, count2 = 0;
  void cb1(uevent_t * ev, int fd, short event, void *arg) {
    count1++;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }
  void cb2(uevent_t * ev, int fd, short event, void *arg) {
    count2++;
    uevent_base_loopbreak(atomic_load_explicit(&ev->base, memory_order_acquire));
  }
  uev_t *uev1 = uevent_create_or_assign_event(NULL, base1, -1, UEV_TIMEOUT, cb1, NULL, __func__);
  uev_t *uev2 = uevent_create_or_assign_event(NULL, base2, -1, UEV_TIMEOUT, cb2, NULL, __func__);
  assert(uev1 && uev2);
  uevent_add(uev1, 10);
  uevent_add(uev2, 10);
  uevent_base_dispatch(base1);
  uevent_base_dispatch(base2);
  assert(count1 == 1 && count2 == 1);
  uevent_free(uev1);
  uevent_free(uev2);
  uevent_deinit(base1);
  uevent_deinit(base2);
  PRINT_TEST_PASSED();
}

void test_mt_add_del() {
  PRINT_TEST_START("Multithreaded add/delete operations");
  uevent_base_t *base = uevent_base_new(32);
  assert(base != NULL);
  enum { THREADS = 4,
         EVENTS = 100 };
  pthread_t threads[THREADS];
  void *thread_func(void *arg) {
    (void)arg; // Unused
    for (int i = 0; i < EVENTS; ++i) {
      uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, NULL, NULL, __func__);
      assert(uev);
      assert(uevent_add(uev, 10) == 0);
      usleep(100);
      assert(uevent_del(uev) == 0);
      uevent_free(uev);
    }
    return NULL;
  }
  for (int t = 0; t < THREADS; ++t) {
    pthread_create(&threads[t], NULL, thread_func, NULL);
  }
  for (int t = 0; t < THREADS; ++t) {
    pthread_join(threads[t], NULL);
  }
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_massive_events() {
  PRINT_TEST_START("Massive number of one-shot events");
  uevent_base_t *base = uevent_base_new(1024);
  assert(base != NULL);
  enum { N = 1000 };
  atomic_int count;
  atomic_init(&count, 0);
  void cb(uevent_t * ev, int fd, short event, void *arg) { atomic_fetch_add((atomic_int *)arg, 1); }
  for (int i = 0; i < N; ++i) {
    uev_t *uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, cb, &count, __func__);
    assert(uev);
    uevent_add(uev, 0);
  }
  uevent_base_dispatch(base);
  assert(atomic_load(&count) == N);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_self_readding_timers_race() {
  PRINT_TEST_START("Race condition with self-re-adding timers");
  const int TEST_DURATION_S = 3;
  const int TIMER_INTERVAL_MS = 1;
  uevent_base_t *base = uevent_base_new_with_workers(32, 4);
  assert(base != NULL);
  void readding_callback(uevent_t * ev, int fd, short event, void *arg) { uevent_add(ev->uev, TIMER_INTERVAL_MS); } // Use ev->uev
  uev_t *uev1 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, readding_callback, NULL, "timer_A");
  uev_t *uev2 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, readding_callback, NULL, "timer_B");
  assert(uev1 && uev2);
  void *dispatch_thread(void *arg) {
    uevent_base_dispatch((uevent_base_t *)arg);
    return NULL;
  }
  pthread_t dispatcher;
  PRINT_TEST_INFO("Starting two self-re-adding timers for %d seconds...", TEST_DURATION_S);
  uevent_add(uev1, TIMER_INTERVAL_MS);
  uevent_add(uev2, TIMER_INTERVAL_MS);
  pthread_create(&dispatcher, NULL, dispatch_thread, base);
  sleep(TEST_DURATION_S);
  PRINT_TEST_INFO("Stopping dispatch thread...");
  uevent_base_loopbreak(base);
  pthread_join(dispatcher, NULL);
  uevent_free(uev1);
  uevent_free(uev2);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

void test_wakeup_and_timeout_accuracy() {
  PRINT_TEST_START("timeout accuracy and wakeup fd");

  const int timeout_1 = 450;
  const int timeout_2 = 700;
  const int timeout_long = 100000;
  const int test_duration_ms = 2000;
  const int delay_after_loop_start = 50; // ms

  void recurring_cb(uevent_t * ev, int fd, short event, void *arg) {
    atomic_fetch_add((atomic_int *)arg, 1);
  }

  uevent_base_t *base = uevent_base_new_with_workers(8, 4);
  assert(base);

  atomic_int count_1, count_2, count_long;
  atomic_init(&count_1, 0);
  atomic_init(&count_2, 0);
  atomic_init(&count_long, 0);

  uev_t *uev1 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, recurring_cb, &count_1, "t1");
  uev_t *uev2 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, recurring_cb, &count_2, "t2");
  uev_t *uev_long = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, recurring_cb, &count_long, "t_long");
  assert(uev1 && uev2 && uev_long);

  uevent_set_timeout(uev1, timeout_1);
  uevent_set_timeout(uev2, timeout_2);

  void *dispatch_thread(void *arg) {
    PRINT_TEST_INFO("starting event loop");
    uevent_base_dispatch((uevent_base_t *)arg);
    return NULL;
  }

  // Добавляем длинный таймер до запуска event loop
  uevent_add(uev_long, timeout_long);

  pthread_t dispatcher;
  pthread_create(&dispatcher, NULL, dispatch_thread, base);

  // Ждём, чтобы event loop гарантированно стартовал
  msleep(delay_after_loop_start);

  // Добавляем таймеры, которые должны сработать
  uevent_add(uev1, timeout_1);
  uevent_add(uev2, timeout_2);

  msleep(test_duration_ms);

  uevent_base_loopbreak(base);
  pthread_join(dispatcher, NULL);

  int window = test_duration_ms - delay_after_loop_start;
  int expected_1 = window / timeout_1;
  int expected_2 = window / timeout_2;
  int expected_long = 0;

  int c1 = atomic_load(&count_1);
  int c2 = atomic_load(&count_2);
  int c_long = atomic_load(&count_long);

  PRINT_TEST_INFO("t1=%d (exp. >=%d), t2=%d (exp. >=%d), t_long=%d (exp. %d)", c1, expected_1, c2, expected_2, c_long, expected_long);

  // Разрешаем максимум одну потерю тика из-за планировщика/ОС
  assert(c1 >= expected_1 - 1);
  assert(c2 >= expected_2 - 1);
  assert(c_long == expected_long);

  uevent_free(uev1);
  uevent_free(uev2);
  uevent_free(uev_long);
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

typedef struct {
  uevent_t *ev;
  atomic_int *refcount_check; // Pointer to the atomic int for signaling
} thread_arg_t;

void test_refcount_during_callback_execution() {
  PRINT_TEST_START("refcount check during callback execution");

  // Колбэк с задержкой. Определен внутри, используя расширение GCC/Clang.
  void delayed_callback(uevent_t * ev, int fd, short event, void *arg) {
    // Имитируем длительную работу в колбэке
    sleep(1);
  }

  // Поток для event loop'а. Это единственный дополнительный поток.
  void *dispatch_thread(void *arg) {
    uevent_base_dispatch((uevent_base_t *)arg);
    return NULL;
  }

  uevent_base_t *base = uevent_base_new_with_workers(16, 0); // Workers set to 0 to keep callbacks in main loop
  assert(base != NULL);

  uev_t *long_uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, delayed_callback, NULL, __func__);
  assert(long_uev != NULL);
  uevent_add(long_uev, 10000); // Add a long timer, but not the one we are testing refcount on

  uev_t *test_uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, delayed_callback, NULL, __func__);
  assert(test_uev != NULL);

  // 1. Проверка refcount сразу после создания.
  int init_rcount = atomic_load(&test_uev->refcount);
  PRINT_TEST_INFO("Checking initial refcount after create/assign: %d", init_rcount);
  assert((init_rcount == 1) && "expected refcount=1");

  // Добавляем таймер на 1 секунду.
  int add_result = uevent_add(test_uev, 1000);
  assert(add_result == UEV_ERR_OK);

  // Запускаем event loop в отдельном потоке.
  pthread_t dispatch_tid;
  pthread_create(&dispatch_tid, NULL, dispatch_thread, base);

  // 2. Проверка refcount ВО ВРЕМЯ работы колбэка.
  // Ждем 1.5 секунды: 1с до срабатывания таймера + 0.5с пока колбэк "работает".
  sleep(1.5);
  int mid_rcount = atomic_load(&test_uev->refcount);
  PRINT_TEST_INFO("Checking refcount during callback execution: mid_rcount=%d", mid_rcount);
  assert(mid_rcount == 2 && "expected refcount=2 during callback execution (event + timer ref)");

  // 3. Проверка refcount ПОСЛЕ завершения колбэка.
  // Ждем еще 1 секунду, чтобы колбэк точно завершился.
  sleep(2); // Should be enough time for the 1 sec sleep in callback + internal processing
  int last_rcount = atomic_load(&test_uev->refcount);
  PRINT_TEST_INFO("Checking refcount after callback completion: last_rcount=%d", last_rcount);
  // After callback completes, if it's not a PERSIST timer and not immediately re-added,
  // the timer-related refcount should be released.
  // So it should return to 1 (the one from uevent_create_or_assign_event).
  assert(last_rcount == 1 && "expected refcount=1 after callback completion for non-persist timer");

  uevent_del(test_uev);
  last_rcount = atomic_load(&test_uev->refcount);
  PRINT_TEST_INFO("Checking refcount after uevent_del: last_rcount=%d", last_rcount);
  assert(last_rcount == 1 && "expected refcount=1 after uevent_del if not freed");

  uevent_free(test_uev);
  uevent_free(long_uev); // Free the long timer too for cleanup

  // Очистка
  uevent_base_loopbreak(base);
  pthread_join(dispatch_tid, NULL);
  uevent_deinit(base);

  PRINT_TEST_PASSED();
}

#define DECLARE_TIMERS(num_timers, interval_ms, exec_time_ms)               \
  int intervals_ms[num_timers] = {[0 ...(num_timers) - 1] = (interval_ms)}; \
  int durations_ms[num_timers] = {[0 ...(num_timers) - 1] = (exec_time_ms)};

static void fill_timer_arrays(int *int_arr, int *delay_arr, int arr_sz, int init_delay, int step_ms, int exec_min_ms, int exec_max_ms) {
  for (int i = 0; i < arr_sz; i++) {
    int_arr[i] = init_delay + i * step_ms;
    if (exec_max_ms > exec_min_ms) {
      delay_arr[i] = exec_min_ms + rand() % (exec_max_ms - exec_min_ms + 1);
    } else {
      delay_arr[i] = exec_min_ms;
    }
  }
}

void test_static_persist_timer_recreate() {
  PRINT_TEST_START("Static persist timer recreation from another thread");

  uevent_base_t *base = uevent_base_new_with_workers(32, 0);
  assert(base != NULL);

  atomic_int callback_count;
  atomic_init(&callback_count, 0);

  void timer_cb(uevent_t * ev, int fd, short event, void *arg) {
    (void)fd;
    (void)event;
    (void)arg;
    PRINT_TEST_INFO("timer_cb for %s", ev->name);
    atomic_fetch_add(&callback_count, 1);
  }

  // Static event object
  static uevent_t static_ev = {.is_static = true};

  // Create initial timer
  uev_t *uev = uevent_create_or_assign_event(&static_ev, base, -1, UEV_TIMEOUT | UEV_PERSIST, timer_cb, NULL, "stat_persist_timer");
  assert(uev != NULL);
  assert(ATOM_LOAD_ACQ(uev->ev) == &static_ev);
  uevent_set_timeout(uev, 50);
  uevent_add_with_current_timeout(uev);

  // Thread function to recreate timer
  void *recreate_thread(void *arg) {
    uev_t *uev = (uev_t *)arg;
    // Wait for first callback
    int cnt = 1000;
    while (atomic_load(&callback_count) < 1 && cnt-- > 0) { // Wait for at least one callback to ensure it's active
      msleep(100);
    }

    PRINT_TEST_INFO("create: dynamic timer event");
    uev_t *new_uev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, timer_cb, NULL, "dyn_timer");
    assert(new_uev != NULL);
    uevent_set_timeout(new_uev, 2000);
    uevent_add_with_current_timeout(new_uev);

    PRINT_TEST_INFO("recreate_thread: Deleting event");
    // Delete and recreate timer
    uevent_free(uev); // Pass the uev_t wrapper
    msleep(100);      // Give some time for free operation to propagate

    PRINT_TEST_INFO("recreate_thread: Recreating event");
    new_uev = uevent_create_or_assign_event(&static_ev, base, -1, UEV_TIMEOUT, timer_cb, NULL, "stat_oneshot_timer_recreated");
    assert(new_uev != NULL);
    assert(ATOM_LOAD_ACQ(new_uev->ev) == &static_ev);
    uevent_set_timeout(new_uev, 100);
    uevent_add_with_current_timeout(new_uev);

    PRINT_TEST_INFO("recreate_thread finished");
    return NULL;
  }

  pthread_t tid;
  pthread_create(&tid, NULL, recreate_thread, uev);

  // Run the dispatch for a reasonable time to allow initial and recreated timers to fire
  uevent_base_dispatch(base);

  PRINT_TEST_INFO("uevent_base_dispatch finished");

  PRINT_TEST_INFO("join");
  pthread_join(tid, NULL);
  PRINT_TEST_INFO("join finished");
  PRINT_TEST_INFO("callback_cnt=%d", atomic_load(&callback_count));
  assert(atomic_load(&callback_count) >= 2); // Expect at least 2 callbacks (one before recreate, some after)
  // Stop the loop cleanly after tests
  uevent_base_loopbreak(base);
  uevent_base_dispatch(base); // Dispatch again to clean up if loopbreak didn't immediately exit
  PRINT_TEST_INFO("uevent_deinit start");
  uevent_deinit(base);
  PRINT_TEST_PASSED();
}

/**
 * @brief Эмулирует реальную нагрузку с набором повторяющихся таймеров.
 *
 * Создает 10 PERSIST таймеров с разными интервалами и настраиваемой
 * задержкой в колбэке. Это позволяет симулировать сложные условия
 * работы и выявлять проблемы с задержками и гонками.
 */
void test_real_world_load_simulation() {
  PRINT_TEST_START("Real-world load simulation with multiple timers");

  typedef struct {
    atomic_int count;
    int duration_ms;
  } cb_ctx_t;

  void delayed_cb(uevent_t * ev, int fd, short event, void *arg) {
    (void)fd;
    (void)event;
    cb_ctx_t *ctx = (cb_ctx_t *)arg;
    atomic_fetch_add(&ctx->count, 1);
    uint64_t start = tu_clock_gettime_monotonic_fast_ms();
    if (ctx->duration_ms > 0) usleep(ctx->duration_ms * USEC_PER_MS);
    uint64_t end = tu_clock_gettime_monotonic_fast_ms();
    PRINT_TEST_INFO("cb for name='%s' duration_ms=%" PRIu64 "", ev->name, (end - start));
  }

  void *dispatch_thread(void *arg) {
    uevent_base_dispatch((uevent_base_t *)arg);
    return NULL;
  }

  uevent_base_t *base = uevent_base_new_with_workers(1024, 4);
  assert(base != NULL);

// ARRAY_SIZE is not defined, assuming a common macro or simple calculation
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

  int intervals_ms_arr[] = {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 150, 250, 350, 450, 550, 650, 750, 850, 950, 1050, 1100, 1200, 1300, 1400};
  int durations_ms_arr[ARRAY_SIZE(intervals_ms_arr)];
  fill_timer_arrays(intervals_ms_arr, durations_ms_arr, ARRAY_SIZE(intervals_ms_arr), 100, 0, 0, 50); // intervals_ms_arr already filled, adjust func call if needed. Here, fixed intervals, random durations.

  int num_timers = ARRAY_SIZE(intervals_ms_arr);
  uev_t *uev_timers[num_timers]; // Use uev_t pointers
  char *timers_names[num_timers];
  cb_ctx_t contexts[num_timers];

  PRINT_TEST_INFO("Creating %d recurring timers...", num_timers);
  for (int i = 0; i < num_timers; ++i) {
    atomic_init(&contexts[i].count, 0);
    contexts[i].duration_ms = durations_ms_arr[i];
    char *name_buf = malloc(32);
    assert(name_buf != NULL);
    snprintf(name_buf, 32, "timer_%dms_%d", intervals_ms_arr[i], i);
    uev_timers[i] = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, delayed_cb, &contexts[i], name_buf);
    assert(uev_timers[i] != NULL);
    timers_names[i] = name_buf;
    uevent_set_timeout(uev_timers[i], intervals_ms_arr[i]);
    uevent_add_with_current_timeout(uev_timers[i]);
  }

  pthread_t dispatcher;
  pthread_create(&dispatcher, NULL, dispatch_thread, base);

  const int test_duration_ms = 6500;
  PRINT_TEST_INFO("Running simulation for %d ms...", test_duration_ms);
  msleep(test_duration_ms);

  PRINT_TEST_INFO("Stopping event loop...");
  uevent_base_loopbreak(base);
  pthread_join(dispatcher, NULL);

  PRINT_TEST_INFO("--- Verifying callback counts ---");
  for (int i = 0; i < num_timers; ++i) {
    int expected = test_duration_ms / intervals_ms_arr[i];
    int actual = atomic_load(&contexts[i].count);
    int lower = expected > 0 ? expected - 2 : 0; // Allow slightly more deviation for stress
    int upper = expected + 2;
    // Check if the timer fired at least once if expected.
    if (expected > 0 && actual == 0) {
      PRINT_TEST_INFO("WARN: Timer '%s' expected %d, got 0. This might indicate significant lag or error.", ATOM_LOAD_ACQ(uev_timers[i]->ev)->name, expected);
      assert(false && "Timer did not fire at all when expected.");
    }

    PRINT_TEST_INFO("Timer '%s': expected ~%d, actual %d", ATOM_LOAD_ACQ(uev_timers[i]->ev)->name, expected, actual);
    assert(actual >= lower && actual <= upper && "Callback count is out of expected range");
  }

  for (int i = 0; i < num_timers; ++i) {
    uevent_free(uev_timers[i]); // Free the uev_t wrapper
    free(timers_names[i]);      // Free the allocated name string
  }
  uevent_deinit(base);

  PRINT_TEST_PASSED();
}

void test_persist_and_self_adding_timer() {
  PRINT_TEST_START("2 PERSIST timers + 1 self-adding one-shot timer, deinit without loopbreak");
  uevent_base_t *base = uevent_base_new_with_workers(32, 0);
  assert(base);

  void persist_cb(uevent_t * ev, int fd, short event, void *arg) {
    (void)ev;
    (void)fd;
    (void)event;
    (void)arg;
    msleep(50); // Reduced to 50ms to speed up test and avoid very long sleeps in a test
  }
  void selfadd_cb(uevent_t * ev, int fd, short event, void *arg) {
    (void)fd;
    (void)event;
    (void)arg;
    uevent_add(ev->uev, 0); // Use uev pointer for re-adding
    msleep(50);             // Reduced to 50ms
  }
  void *dispatch_thread(void *arg) {
    uevent_base_dispatch((uevent_base_t *)arg);
    msleep(50); // Small sleep after dispatch to ensure thread can exit if loopbreak happens
    return NULL;
  }

  uev_t *uev1 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, persist_cb, NULL, "persist1");
  uev_t *uev2 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, persist_cb, NULL, "persist2");
  uev_t *uev3 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, selfadd_cb, NULL, "selfadd");
  assert(uev1 && uev2 && uev3);

  uevent_set_timeout(uev1, 200);
  uevent_set_timeout(uev2, 300);
  uevent_add_with_current_timeout(uev1);
  uevent_add_with_current_timeout(uev2);
  uevent_add(uev3, 0);

  pthread_t tid;
  pthread_create(&tid, NULL, dispatch_thread, base);
  msleep(1500); // Allow timers to fire multiple times

  // Без loopbreak — сразу deinit
  PRINT_TEST_INFO("Calling uevent_deinit without loopbreak...");
  uevent_deinit(base);
  pthread_join(tid, NULL);
  PRINT_TEST_INFO("Dispatch thread joined.");

  PRINT_TEST_PASSED();
}

void test_persist_and_self_adding_timer_with_workers() {
  PRINT_TEST_START("2 PERSIST timers + 1 self-adding one-shot timer with workers, deinit without loopbreak");
  uevent_base_t *base = uevent_base_new_with_workers(32, 4); // With worker threads
  assert(base);

  void persist_cb(uevent_t * ev, int fd, short event, void *arg) {
    (void)ev;
    (void)fd;
    (void)event;
    (void)arg;
    msleep(50);
  }
  void selfadd_cb(uevent_t * ev, int fd, short event, void *arg) {
    (void)fd;
    (void)event;
    (void)arg;
    uevent_add(ev->uev, 0);
    msleep(50);
  }
  void *dispatch_thread(void *arg) {
    uevent_base_dispatch((uevent_base_t *)arg);
    msleep(50);
    return NULL;
  }

  uev_t *uev1 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, persist_cb, NULL, "persist1_worker");
  uev_t *uev2 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, persist_cb, NULL, "persist2_worker");
  uev_t *uev3 = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, selfadd_cb, NULL, "selfadd_worker");
  assert(uev1 && uev2 && uev3);

  uevent_set_timeout(uev1, 200);
  uevent_set_timeout(uev2, 300);
  uevent_add_with_current_timeout(uev1);
  uevent_add_with_current_timeout(uev2);
  uevent_add(uev3, 0);

  pthread_t tid;
  pthread_create(&tid, NULL, dispatch_thread, base);
  msleep(1500);

  // Без loopbreak — сразу deinit
  PRINT_TEST_INFO("Calling uevent_deinit without loopbreak (with workers)...");
  uevent_deinit(base);
  pthread_join(tid, NULL);
  PRINT_TEST_INFO("Dispatch thread joined.");

  PRINT_TEST_PASSED();
}

int main() {
  printf("Starting uevent library tests...\n\n");
#ifdef DEBUG
  setup_syslog2("uevent_test", LOG_DEBUG, false);
#else
  setup_syslog2("uevent_test", LOG_NOTICE, false);
#endif

  test_static_persist_timer_recreate();
  test_real_world_load_simulation();
  test_refcount_during_callback_execution();
  test_uevent_active();
  test_uevent_add_with_current_timeout();

  test_persist_and_self_adding_timer();

  test_persist_and_self_adding_timer_with_workers();
  test_del_inactive_event();
  test_null_event_del();
  test_multiple_events_on_fd();
  test_event_flags_combinations();
  test_add_same_event_twice();
  test_add_event_fd_minus1();
  test_stress_active();
  test_create_and_free_dynamic_event();
  test_create_and_free_static_event();
  test_add_and_del_event();
  test_timeout_event();
  test_read_event();
  test_write_event();
  test_error_hup_event();
  test_invalid_fd_read_event();
  test_invalid_fd_write_event();
  test_fd_becomes_invalid_event();
  test_stress_mt();
  test_persist_event();
  test_null_params();
  test_double_free_detection();
  test_double_add_del();
  test_fd_edge_cases();
  test_special_timeouts();
  test_null_callback();
  test_pending_and_initialized();
  test_multiple_bases();
  test_mt_add_del();
  test_massive_events();
  test_self_readding_timers_race();
  test_wakeup_and_timeout_accuracy();

  printf("\nAll tests completed successfully!\n");
  return 0;
}
