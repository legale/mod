// perftest.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Заголовки для libevent
#include <event2/event.h>
#include <event2/event_struct.h>

// libuv
#include <uv.h>

// Заголовок для вашей библиотеки
#include "uevent.h"

// --- Утилита для замера времени ---
long long get_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// --- Код для тестирования uevent ---

static int uevent_triggered_count;
void uevent_perf_cb(uevent_t *ev, int fd, short event, void *arg) {
  (void)ev;
  (void)fd;
  (void)event;
  int *counter = (int *)arg;
  atomic_fetch_add_explicit(counter, 1, memory_order_acq_rel);
}

void run_uevent_test(int num_events) {
#ifdef DEBUG
  setup_syslog2("uevent_test", LOG_DEBUG, false);
#else
  setup_syslog2("uevent_test", LOG_WARNING, false);
#endif

  printf("--- test uevent (events=%d) ---\n", num_events);
  _Atomic int uevent_triggered_count = 0;

  uevent_base_t *base = uevent_base_new_with_workers(num_events, 0);
  assert(base);

  // Замеряем только добавление и выполнение
  long long start_time = get_time_ms();

  for (int i = 0; i < num_events; i++) {
    uevent_t *ev = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, uevent_perf_cb, &uevent_triggered_count, __func__);
    assert(ev);
    // Добавляем с минимальным таймаутом, чтобы они сработали как можно скорее
    uevent_add(ev, 0);
  }

  uevent_base_dispatch(base);

  long long end_time = get_time_ms();

  printf("check: triggered=%d of events=%d\n", uevent_triggered_count, num_events);
  assert(uevent_triggered_count == num_events);
  printf("result uevent: %lld ms\n", end_time - start_time);

  uevent_deinit(base);
}

// --- Код для тестирования libevent ---

static int libevent_triggered_count;
void libevent_perf_cb(evutil_socket_t fd, short event, void *arg) {
  (void)fd;
  (void)event;
  int *counter = (int *)arg;
  (*counter)++;
}

void run_libevent_test(int num_events) {
  printf("--- test libevent (events=%d) ---\n", num_events);
  libevent_triggered_count = 0;

  struct event_base *base = event_base_new();
  assert(base);

  // libevent требует хранить указатели на события для их освобождения
  struct event **events = malloc(sizeof(struct event *) * num_events);
  assert(events);

  long long start_time = get_time_ms();

  struct timeval tv = {0, 0}; // 1 мс

  for (int i = 0; i < num_events; i++) {
    events[i] = event_new(base, -1, 0, libevent_perf_cb, &libevent_triggered_count);
    assert(events[i]);
    event_add(events[i], &tv);
  }

  event_base_dispatch(base);

  long long end_time = get_time_ms();

  printf("check: triggered=%d of events=%d\n", libevent_triggered_count, num_events);
  assert(libevent_triggered_count == num_events);
  printf("result libevent: %lld ms\n", end_time - start_time);

  // Освобождаем память
  for (int i = 0; i < num_events; i++) {
    event_free(events[i]);
  }
  free(events);
  event_base_free(base);
}

static int libuv_triggered_count;

void libuv_perf_cb(uv_timer_t *handle) {
  int *counter = (int *)handle->data;
  (*counter)++;
  uv_close((uv_handle_t *)handle, free);
}

void run_libuv_test(int num_events) {
  printf("--- test libuv (events=%d) ---\n", num_events);
  libuv_triggered_count = 0;

  uv_loop_t *loop = uv_loop_new();
  assert(loop);

  long long start_time = get_time_ms();

  for (int i = 0; i < num_events; i++) {
    uv_timer_t *timer = malloc(sizeof(uv_timer_t));
    assert(timer);
    uv_timer_init(loop, timer);
    timer->data = &libuv_triggered_count;
    uv_timer_start(timer, libuv_perf_cb, 0, 0);
  }

  uv_run(loop, UV_RUN_DEFAULT);

  long long end_time = get_time_ms();

  printf("check: triggered=%d of events=%d\n", libuv_triggered_count, num_events);
  assert(libuv_triggered_count == num_events);
  printf("result libuv: %lld ms\n", end_time - start_time);

  uv_loop_delete(loop);
}

int main() {
  const int event_counts[] = {100, 1000, 10000};
  int num_tests = sizeof(event_counts) / sizeof(int);

  printf("running perftest...\n");

  for (int i = 0; i < num_tests; i++) {
    int count = event_counts[i];
    run_uevent_test(count);
    run_libevent_test(count);
    run_libuv_test(count);

    printf("----------------------------------------\n");
  }

  return 0;
}
