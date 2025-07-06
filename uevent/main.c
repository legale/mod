#include "uevent.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TIMER_TIMEOUT_MS 500

#define HANDLE_DELAY_MS TIMER_TIMEOUT_MS * 3

void cb(uevent_t *ev, int fd, short events, void *arg) {
  printf("event triggered on fd %d with events %d\n", fd, events);
}

void static_cb(uevent_t *ev, int fd, short events, void *arg) {
  printf("static event triggered on fd %d with events %d\n", fd, events);
}

void stdin_cb(uevent_t *ev, int fd, short events, void *arg) {
  char buf[256];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    printf("stdin: %s", buf);
  }
}

void remove_stdin_event_cb(uevent_t *ev, int fd, short events, void *arg) {
  printf("timer: remove stdin event UEV_READ\n");
  uevent_del(ev->uev);
  printf("timer: remove stdin event UEV_READ DONE\n");
}

void *dispatch_thr(void *arg) {
  uevent_base_dispatch((uevent_base_t *)arg);
  return NULL;
}

struct handle_thr_arg {
  int sz;
  uev_t **arr;
  uevent_base_t *base;
};

void *handle_thr(void *arg) {
  msleep(HANDLE_DELAY_MS);
  struct handle_thr_arg *a = (struct handle_thr_arg *)arg;
  for (int j = 0; j < a->sz; j++) {
    uev_t *uev = a->arr[j];
    uevent_free(uev);
    uevent_free(uev);
  }

  int tout = TIMER_TIMEOUT_MS;
  int cnt = 0;
  while (cnt++ < 5) {
    for (int j = 0; j < a->sz; j++) {
      size_t sz = sizeof("new dynamic ev      ");
      char *buf = malloc(sz);
      snprintf(buf, sz, "ev idx=%d.%d", cnt, j);
      uev_t *uev = uevent_create_or_assign_event(NULL, a->base, -1, UEV_TIMEOUT, cb, NULL, buf);
      tout += 100;
      uevent_set_timeout(uev, tout);
      uevent_add_with_current_timeout(uev);
    }
  }

  return NULL;
}

int main() {

#ifdef DEBUG
  setup_syslog2("uevent_test", LOG_DEBUG, false);
#else
  setup_syslog2("uevent_test", LOG_NOTICE, false);
#endif

  uevent_base_t *base = uevent_base_new_with_workers(1024 * 1024, 8);
  if (!base) return -1;

  int uev_arr_sz = 10;
  uev_t *uev_arr[10] = {0};

  uevent_t ev_static = {.is_static = true};

  int i = 0;
  uev_arr[i] = uevent_create_or_assign_event(NULL, base, 0, UEV_READ | UEV_PERSIST, stdin_cb, NULL, "stdin dynamic ev");
  uevent_add(uev_arr[i++], 0);

  uev_arr[i] = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT, remove_stdin_event_cb, NULL, "remove stdin dynamic ev");
  uevent_add(uev_arr[i++], 0);

  uev_arr[i] = uevent_create_or_assign_event(&ev_static, base, -1, UEV_TIMEOUT, static_cb, NULL, "static ev2");
  uevent_set_timeout(uev_arr[i], TIMER_TIMEOUT_MS);
  uevent_add_with_current_timeout(uev_arr[i++]);

  uev_arr[i] = uevent_create_or_assign_event(NULL, base, -1, UEV_TIMEOUT | UEV_PERSIST, cb, NULL, "dynamic ev1");
  uevent_set_timeout(uev_arr[i], TIMER_TIMEOUT_MS);
  uevent_add(uev_arr[i++], TIMER_TIMEOUT_MS);

  pthread_t tid1, tid2;
  pthread_create(&tid1, NULL, dispatch_thr, base);

  struct handle_thr_arg a = {.sz = uev_arr_sz, .arr = uev_arr, .base = base};
  pthread_create(&tid2, NULL, handle_thr, &a);

  pthread_join(tid2, NULL);
  pthread_join(tid1, NULL);

  uevent_deinit(base);

  return 0;
}
