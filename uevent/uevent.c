#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <unistd.h>

#include "../list.h"
#include "../minheap/minheap.h"
#include "../syslog.h"
#include "../timeutil/timeutil.h"

#include "uevent.h"
#include "uevent_internal.h"
#include "uevent_worker.h"

#define EPOLL_MAX_TIMEOUT_MS 60000U
#define UEVENT_DEFAULT_WORKERS_NUM 6

#ifndef container_of
#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

struct uevent_base_t {
  int epoll_fd;                   // epoll fd
  uevent_t wakeup_event;          // служебное событие для пробуждения epoll_wait
  _Atomic bool wakeup_fd_written; // true, если в wakeup_fd уже записано значение
  _Atomic bool running;           // true, если event loop запущен
  _Atomic bool stopped;           // true, если event loop завершился
  pthread_mutex_t base_mut;       // мьютекс для защиты event_list и timer_heap
  pthread_cond_t base_cond;       // условие к мьютексу
  pthread_mutex_t slots_mut;      // мьютекс для защиты слотов ev_arr
  // pthread_mutex_t zombie_mut;        // мьютекс для защиты слотов списка зомби событий
  struct epoll_event *events; // массив epoll событий
  unsigned int max_events;    // размер массива events
  // struct list_head event_list;       // список всех событий в базе
  // struct list_head zombie_list;      // список всех событий в базе
  minheap_t *timer_heap;             // куча таймеров (minheap)
  _Atomic int num_active_fd;         // число активных fd-событий
  _Atomic int num_active_timers;     // число активных таймеров
  uevent_worker_pool_t *worker_pool; // пул воркеров для асинхронных колбэков
  uev_t *uev_arr;                    // массив с обертками событий
  unsigned int uev_arr_sz;           // размер массива с обертками событий
  unsigned int free_uev_arr_cnt;     // кол-во свободных слотов
  unsigned int *free_uev_arr;        // массив индексов свободных слотов ev_arr
};

// forward declaration
static void uevent_init_ev(uevent_t *event, uevent_base_t *base, int fd, short events, uevent_cb_t cb, void *arg, const char *name);
static int uev_return(uevent_base_t *base, uev_t *uev);

static void wakeup_fd_read_cb(uevent_t *ev, int fd, short events, void *arg) {
  TINIT;
  TMARK(10, "start");
  (void)events;
  (void)arg;

  uint64_t value;
  ssize_t res;
  do {
    res = read(fd, &value, sizeof(value));
  } while (res > 0);

  if ((res < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
    syslog2(LOG_ERR, "[TIMER_ERR] failed to read from wakeup_fd=%d: %s", fd, strerror(errno));
  } else {
    syslog2(LOG_DEBUG, "[TIMER_WAK] read wakeup_fd=%d OK", fd);
    atomic_store_explicit(&ev->base->wakeup_fd_written, false, memory_order_release);
  }
  TMARK(2, "wakeup_fd_read_cb END");
}

static void wakeup_fd_reset(uevent_base_t *base) {
  if (atomic_load_explicit(&base->wakeup_fd_written, memory_order_acquire)) {
    wakeup_fd_read_cb(&base->wakeup_event, base->wakeup_event.fd, 0, NULL);
  }
}

static void uevent_base_wakeup(uevent_base_t *base) {
  if (base == NULL) {
    return;
  }
  if (atomic_load_explicit(&base->wakeup_fd_written, memory_order_acquire)) {
    return;
  }
  uint64_t val = 1;
  int fd = base->wakeup_event.fd;
  ssize_t res = write(fd, &val, sizeof(val));
  if ((res < 0) && (errno != EAGAIN)) {
    syslog2(LOG_WARNING, "failed to write to wakeup_fd=%d error='%s'", fd, strerror(errno));
  } else {
    atomic_store_explicit(&base->wakeup_fd_written, true, memory_order_release);
    syslog2(LOG_DEBUG, "[TIMER_WAK] write wakeup_fd=%d OK", fd);
  }
}

static inline bool uevent_try_lock(uevent_t *ev) {
  bool expected = false;
  return atomic_compare_exchange_strong_explicit(
      &ev->modification_lock,
      &expected,
      true,
      memory_order_acquire,
      memory_order_relaxed);
}

static void uevent_unlock(uevent_t *ev) {
  atomic_store_explicit(&ev->modification_lock, false, memory_order_release);
}

// сбросить все поля статического события
static void uevent_reset_static_ev(uevent_t *ev) {
  ATOM_STORE_REL(ev->base, NULL);
  ATOM_STORE_REL(ev->modification_lock, 0);
  ATOM_STORE_REL(ev->active_fd, 0);
  ATOM_STORE_REL(ev->active_timer, 0);
  ev->uev = NULL; // Обнуляем ev->uev для статических событий
  ev->fd = -1;
  ev->events = 0;
  ev->cb = NULL;
  ev->cb_wrapper = NULL;
  ev->arg = NULL;
}

// внутренняя функция освобождения памяти или сброса для статического события
static void uevent_destroy_uev_internal_unsafe(uev_t *uev) {
  // 1. Атомарно получаем и обнуляем указатель
  uevent_t *ev = atomic_exchange_explicit(&uev->ev, NULL, memory_order_acq_rel);
  if (!ev) return;
  uevent_base_t *base = ATOM_LOAD_ACQ(ev->base);

  // возвращаем слот назад
  uev_return(base, uev);

  // Освобождаем (сбрасываем) событие
  if (ev->is_static) {
    uevent_reset_static_ev(ev);
  } else {
    ev->uev = NULL; // Обнуляем ev->uev для динамических событий
    free(ev);
  }
}

// логируем задержку таймера, если это таймер
static void log_timer_delay_if_needed(uev_t *uev, short triggered_events, uint64_t cron_time) {
  if ((triggered_events & UEV_TIMEOUT) == 0) return;

  uint64_t now_ms = tu_clock_gettime_monotonic_fast_ms();
  int64_t diff_ms = (int64_t)now_ms - (int64_t)cron_time;

  if (cached_mask & LOG_MASK(LOG_DEBUG)) {
    syslog2(LOG_DEBUG, "[TIMER_DBG] name='%s' cron_time=%" PRIu64 " exec_time=%" PRIu64 " diff_ms=%" PRId64, uev->ev->name, cron_time, now_ms, diff_ms);
  } else if (diff_ms > 1000) {
    syslog2(LOG_WARNING, "[TIMER_LAG] name='%s' cron_time=%" PRIu64 " exec_time=%" PRIu64 " diff_ms=%" PRId64, uev->ev->name, cron_time, now_ms, diff_ms);
  }
}

static void dispatch_event_callback(uev_t *uev, short triggered_events, uint64_t cron_time) {
  FUNC_START_DEBUG;
  if (!uevent_try_ref(uev)) return;

  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  uevent_base_t *base = atomic_load_explicit(&ev->base, memory_order_acquire);

  if ((base != NULL) && (base->worker_pool != NULL)) {
    if (!atomic_load_explicit(&base->running, memory_order_acquire)) {
      uevent_put(uev);
      return;
    }
    uevent_worker_pool_insert(base->worker_pool, uev, triggered_events, cron_time);
  } else {
    ev->cb_wrapper(ev, ev->fd, triggered_events, cron_time, ev->cb, ev->arg);
  }

  uevent_put(uev);
}

// обработать callback события
static void uevent_handle_ev_cb(uevent_t *ev, short triggered_events, uint64_t cron_time) {
  FUNC_START_DEBUG;
  // базовые проверки
  if ((ev == NULL) || (triggered_events == 0)) {
    return;
  }
  if (atomic_load_explicit(&ev->pending_free, memory_order_acquire)) {
    return;
  }
  uev_t *uev = ATOM_LOAD_ACQ(ev->uev);
  if (!uev) {
    syslog2(LOG_DEBUG, "[EV_CB] Skipping event with NULL uev, name='%s'", ev->name);
    return;
  }

  log_timer_delay_if_needed(uev, triggered_events, cron_time);

  // вызываем колбэк, если он есть
  if (ev->cb != NULL) {
    dispatch_event_callback(uev, triggered_events, cron_time);
  }
}

static inline bool uevent_base_has_events(const uevent_base_t *base) {
  int nev = atomic_load_explicit(&base->num_active_fd, memory_order_acquire);
  int ntm = atomic_load_explicit(&base->num_active_timers, memory_order_acquire);
  return (nev > 0) || (ntm > 0);
}

// Инициализация массива свободных слотов
static int uev_slots_init(uevent_base_t *base, int max_events) {
  if (base == NULL) return -1;

  pthread_mutex_init(&base->slots_mut, NULL);

  base->uev_arr = calloc(max_events, sizeof(uev_t));
  if (base->uev_arr == NULL) {
    return -1;
  }

  base->free_uev_arr = malloc(max_events * sizeof(unsigned int));
  if (base->free_uev_arr == NULL) {
    free(base->uev_arr);
    return -1;
  }

  for (unsigned int i = 0; i < (unsigned)max_events; i++) {
    base->free_uev_arr[i] = i;
  }

  base->uev_arr_sz = (unsigned)max_events;
  base->free_uev_arr_cnt = (unsigned)max_events;

  return 0;
}

// Деинициализация массива свободных слотов
static void uev_slots_deinit(uevent_base_t *base) {
  if (base == NULL) return;
  pthread_mutex_destroy(&base->slots_mut);
  if (base->free_uev_arr) free(base->free_uev_arr);
  base->free_uev_arr = NULL;
  if (base->uev_arr) free(base->uev_arr);
  base->uev_arr = NULL;
  base->free_uev_arr_cnt = 0;
  base->uev_arr_sz = 0;
}

// Получение свободного слота
static uev_t *uev_get_unused(uevent_base_t *base) {
  if (base == NULL) return NULL;

  pthread_mutex_lock(&base->slots_mut);
  if (base->free_uev_arr_cnt == 0) {
    pthread_mutex_unlock(&base->slots_mut);
    syslog2(LOG_ERR, "error: no ev_arr free slots left");
    return NULL;
  }
  unsigned idx = base->free_uev_arr[--base->free_uev_arr_cnt];
  pthread_mutex_unlock(&base->slots_mut);
  return &base->uev_arr[idx];
}

// Возврат слота в список свободных
static int uev_return(uevent_base_t *base, uev_t *uev) {
  if (base == NULL) {
    syslog2(LOG_ERR, "error: EINVAL base=NULL");
    return -1;
  }

  ptrdiff_t idx = uev - base->uev_arr;
  if (idx < 0 || idx >= base->uev_arr_sz) {
    syslog2(LOG_ERR, "error: invalid arr idx");
    return -1;
  }

  pthread_mutex_lock(&base->slots_mut);
  base->free_uev_arr[base->free_uev_arr_cnt++] = (unsigned)idx;
  pthread_mutex_unlock(&base->slots_mut);
  return 0;
}

uevent_base_t *uevent_base_new(int max_events) {
  return uevent_base_new_with_workers(max_events, UEVENT_DEFAULT_WORKERS_NUM);
}

// Создание новой базы событий с рабочими потоками
uevent_base_t *uevent_base_new_with_workers(int max_events, int num_workers) {
  uevent_base_t *base = NULL;
  int wakeup_event_fd = -1;

  if ((max_events <= 0) || (num_workers < 0)) {
    goto fail;
  }

  base = calloc(1, sizeof(uevent_base_t));
  if (base == NULL) {
    goto fail;
  }

  // создаем список для зомби событий (которые нужно освободить)
  // uevent_zombie_list_init(base);

  const uevent_t static_wakeup_tpl = {.is_static = true};
  memcpy((void *)&base->wakeup_event, &static_wakeup_tpl, sizeof(static_wakeup_tpl));

  base->max_events = max_events;
  base->epoll_fd = -1;
  base->worker_pool = NULL;

  base->epoll_fd = epoll_create1(0);
  if (base->epoll_fd == -1) {
    goto fail_calloc;
  }

  wakeup_event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeup_event_fd == -1) {
    syslog2(LOG_ERR, "error: eventfd: ret=-1 error='%s'", strerror(errno));
    goto fail_epoll_fd;
  }

  base->events = calloc(max_events, sizeof(struct epoll_event));
  if (base->events == NULL) {
    goto fail_wakeup_fd;
  }

  base->timer_heap = mh_create(max_events);
  if (base->timer_heap == NULL) {
    goto fail_events;
  }

  // Инициализация массива свободных слотов
  if (uev_slots_init(base, max_events) != 0) {
    goto fail_timer_heap;
  }

  // INIT_LIST_HEAD(&base->event_list);

  atomic_store_explicit(&base->running, false, memory_order_release);
  atomic_store_explicit(&base->num_active_fd, 0, memory_order_release);
  atomic_store_explicit(&base->num_active_timers, 0, memory_order_release);
  atomic_store_explicit(&base->stopped, true, memory_order_release);

  if (pthread_mutex_init(&base->base_mut, NULL) != 0) {
    goto fail_free_slots;
  }
  if (pthread_cond_init(&base->base_cond, NULL) != 0) {
    goto fail_base_mut;
  }

  if (num_workers > 0) {
    base->worker_pool = uevent_worker_pool_create(num_workers);
    if (base->worker_pool == NULL) {
      syslog2(LOG_ERR, "Failed to create uevent worker pool");
      goto fail_base_cond;
    }
  }

  uev_t *uev = uevent_create_or_assign_event(&base->wakeup_event, base, wakeup_event_fd, UEV_READ | UEV_PERSIST, wakeup_fd_read_cb, NULL, "wakeup_event");
  if (uev == NULL) {
    goto fail_worker_pool;
  }
  uevent_add(uev, 0);

  return base;

fail_worker_pool:
  if (num_workers > 0) {
    uevent_worker_pool_destroy(base->worker_pool);
  }
fail_base_cond:
  pthread_cond_destroy(&base->base_cond);
fail_base_mut:
  pthread_mutex_destroy(&base->base_mut);
fail_free_slots:
  uev_slots_deinit(base);
fail_timer_heap:
  mh_free(base->timer_heap);
fail_events:
  free(base->events);
fail_wakeup_fd:
  close(wakeup_event_fd);
fail_epoll_fd:
  close(base->epoll_fd);
fail_calloc:
  free(base);
fail:
  return NULL;
}

void uevent_set_timeout(uev_t *uev, int timeout_ms) {
  uev = uevent_try_ref(uev);
  if (!uev) return;
  syslog2(LOG_DEBUG, "timeout_ms=%d", timeout_ms);
  atomic_store_explicit(&uev->ev->timeout_ms, timeout_ms, memory_order_release);
  uevent_put(uev);
}

bool uevent_is_alive(uev_t *uev) {
  uev = uevent_try_ref(uev);
  if (uev == NULL) {
    return false;
  }
  uevent_put(uev);
  return true;
}

bool uevent_initialized(uev_t *uev) {
  if (!uev) return false;
  if (!uevent_try_ref(uev)) return false;
  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  uevent_base_t *base = ATOM_LOAD_ACQ(ev->base);
  uevent_put(uev);
  return base != NULL;
}

bool uevent_pending(uev_t *uev, int mask) {
  bool res = false;
  uev = uevent_try_ref(uev);
  if (uev == NULL) return res;
  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);

  if (((mask & UEV_TIMEOUT) != 0) && (atomic_load_explicit(&ev->active_timer, memory_order_acquire) != 0)) {
    res = true;
  }
  if ((!res) && ((mask & UEV_FD_EVENTS) != 0) && (atomic_load_explicit(&ev->active_fd, memory_order_acquire) != 0)) {
    res = true;
  }
  uevent_put(uev);
  return res;
}

void uevent_add_with_current_timeout(uev_t *uev) {
  (void)uevent_add(uev, UEV_TIMEOUT_FROM_EVENT);
}

void uevent_active(uev_t *uev) {
  (void)uevent_add(uev, UEV_TIMEOUT_FIRE_NOW);
}

static uevent_t *uevent_alloc_ev(uevent_t *ev) {
  if (ev != NULL) return ev;

  uevent_t *event = calloc(1, sizeof(uevent_t));
  return event;
}

uev_t *uevent_create_or_assign_event(uevent_t *ev, uevent_base_t *base, int fd, short events, uevent_cb_t cb, void *arg, const char *name) {
  uev_t *uev = NULL;

  if (base == NULL) {
    syslog2(LOG_ERR, "error: EINVAL base=NULL");
    goto fail;
  }

  if (ev != NULL) {
    if (!ev->is_static) {
      syslog2(LOG_ERR, "error: unable to assign dynamically allocated event");
      goto fail;
    }
    if (ATOM_LOAD_ACQ(ev->base) != NULL) {
      syslog2(LOG_ERR, "error: unable to assign initialized static event, reset event first");
      goto fail;
    }
  }
  ev = uevent_alloc_ev(ev);
  if (ev == NULL) {
    goto fail;
  }

  uevent_init_ev(ev, base, fd, events, cb, arg, name);

  if (pthread_mutex_lock(&base->base_mut) != 0) {
    goto fail_event;
  }

  // берм слот
  uev = uev_get_unused(base);

  if (uev == NULL) {
    syslog2(LOG_ERR, "error: no free slots in ev_arr");
    goto fail_unlock;
  }

  atomic_init(&uev->refcount, 1);

  ATOM_STORE_REL(uev->ev, ev);
  ev->uev = uev;

  // добавляем указатель на событие
  // list_add_tail(&ev->list_node, &base->event_list);

  pthread_mutex_unlock(&base->base_mut);
  return uev;

fail_unlock:
  pthread_mutex_unlock(&base->base_mut);
fail_event:
  if (!ev->is_static) {
    free(ev);
  }
fail:
  return NULL;
}

static uint32_t convert_to_epoll_events(short event) {
  uint32_t epoll_events = 0U;
  if ((event & UEV_READ) != 0) {
    epoll_events |= EPOLLIN;
  }
  if ((event & UEV_WRITE) != 0) {
    epoll_events |= EPOLLOUT;
  }
  if ((event & UEV_ERROR) != 0) {
    epoll_events |= EPOLLERR;
  }
  if ((event & UEV_HUP) != 0) {
    epoll_events |= EPOLLHUP;
  }
  return epoll_events;
}

static int insert_fd_to_epoll(uev_t *uev) {
  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);
  uevent_base_t *base = atomic_load_explicit(&ev->base, memory_order_acquire);
  uint32_t epoll_events = convert_to_epoll_events(ev->events);
  struct epoll_event ep_ev = {0};
  ep_ev.events = epoll_events | EPOLLET;
  ep_ev.data.ptr = uev;

  int was_active = atomic_load_explicit(&ev->active_fd, memory_order_acquire);
  int op = was_active ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  int epoll_ret = epoll_ctl(base->epoll_fd, op, ev->fd, &ep_ev);

  if (epoll_ret == 0) {
    if (!was_active) {
      atomic_store_explicit(&ev->active_fd, true, memory_order_release);
      if (ev != &base->wakeup_event) {
        atomic_fetch_add_explicit(&base->num_active_fd, 1, memory_order_acq_rel);
        uevent_ref(uev);
      }
    }
    return UEV_ERR_OK;
  }
  return UEV_ERR_EPOLL;
}

static void uevent_user_cb_wrapper(uevent_t *ev, int fd, short events, uint64_t cron_time, uevent_cb_t cb, void *arg) {
  if (!uevent_try_lock(ev)) return;

  uevent_base_t *base = ATOM_LOAD_ACQ(ev->base);
  // скипаем wakeup_event
  if (&base->wakeup_event == ev) {
    if (cb) {
      cb(ev, fd, events, arg);
    }
    uevent_unlock(ev);
    return;
  }

  uint64_t start = tu_clock_gettime_monotonic_fast_ms();
  if (cb) {
    cb(ev, fd, events, arg);
  }
  uint64_t finish = tu_clock_gettime_monotonic_fast_ms();

  // Метрики:
  int64_t diff_cron_to_exec = (int64_t)start - (int64_t)cron_time;
  int64_t duration = (int64_t)finish - (int64_t)start;

  // Логирование метрик
  uev_t *uev = ATOM_LOAD_ACQ(ev->uev);
  syslog2(LOG_DEBUG,
          "[TIMER_PROFILE] refcount=%d name='%s' diff_cron_to_exec=%" PRId64 " duration=%" PRId64 " cron_time=%" PRIu64,
          ATOM_LOAD_ACQ(uev->refcount),
          ev->name,
          diff_cron_to_exec,
          duration,
          cron_time);

  uevent_unlock(ev);
}

static void uevent_init_ev(uevent_t *ev, uevent_base_t *base, int fd, short events, uevent_cb_t cb, void *arg, const char *name) {
  FUNC_START_DEBUG;
  ev->fd = fd;
  ev->events = events;
  ev->cb = cb;
  ev->cb_wrapper = uevent_user_cb_wrapper;
  ev->arg = arg;
  atomic_store_explicit(&ev->base, base, memory_order_release);
  atomic_store_explicit(&ev->modification_lock, false, memory_order_release);
  atomic_store_explicit(&ev->active_fd, 0, memory_order_release);
  atomic_store_explicit(&ev->active_timer, 0, memory_order_release);
  atomic_store_explicit(&ev->pending_free, false, memory_order_relaxed);
  ev->timer_node.key = 0;
  if (name != NULL) {
    ev->name = name;
  }
}

static void insert_timer_to_heap(uev_t *uev, uint64_t cur_time_ms, int timeout_ms, bool no_lock) {
  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  uevent_base_t *base = ATOM_LOAD_ACQ(ev->base);
  if (!base) return;

  if (!no_lock && pthread_mutex_lock(&base->base_mut) != 0) {
    syslog2(LOG_ERR, "error locking base mutex");
    return;
  }

  timeout_ms = timeout_ms >= 0 ? timeout_ms : 0;
  uint64_t new_key = cur_time_ms + (unsigned)timeout_ms;
  ev->timer_node.key = new_key;

  mh_insert(base->timer_heap, &ev->timer_node);

  if (!ATOM_LOAD_ACQ(ev->active_timer)) {
    atomic_fetch_add_explicit(&base->num_active_timers, 1, memory_order_acq_rel);
    atomic_store_explicit(&ev->active_timer, true, memory_order_release);
    uevent_ref(uev);
  }

  bool wakeup = !ATOM_LOAD_ACQ(base->wakeup_fd_written) &&
                mh_get_min(base->timer_heap) == &ev->timer_node;

  if (!no_lock) {
    pthread_mutex_unlock(&base->base_mut);
  }

  if (wakeup) {
    uevent_base_wakeup(base);
  }
}

static int uevent_add_internal_unsafe(uevent_t *ev, uint64_t cur_time_ms, int timeout_ms, bool no_lock) {
  TINIT;
  TMARK(10, "uevent_add_internal");

  if (atomic_load_explicit(&ev->pending_free, memory_order_acquire)) {
    TMARK(10, "uevent_add_internal");
    return UEV_ERR_PENDING_FREE;
  }

  int ret = UEV_ERR_OK;
  if (internal_is_fd_event(ev)) {
    ret = insert_fd_to_epoll(ev->uev);
    if (ret != UEV_ERR_OK) {
      TMARK(10, "uevent_add_internal");
      return ret;
    }
  }
  if (((ev->events & UEV_TIMEOUT) != 0) || (timeout_ms < 0)) {
    insert_timer_to_heap(ev->uev, cur_time_ms, timeout_ms, no_lock);
  }
  TMARK(10, "uevent_add_internal");
  return ret;
}

int uevent_add(uev_t *uev, int timeout_ms) {
  if (uev == NULL) {
    return UEV_ERR_INVAL;
  }
  FUNC_START_DEBUG;
  if (uevent_try_ref(uev) == NULL) {
    return UEV_ERR_PENDING_FREE;
  }
  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);
  if (!uevent_try_lock(ev)) {
    uevent_put(uev);
    return UEV_ERR_BUSY;
  }
  if (timeout_ms == -2) {
    timeout_ms = atomic_load_explicit(&uev->ev->timeout_ms, memory_order_acquire);
  }
  int ret = uevent_add_internal_unsafe(ev, tu_clock_gettime_monotonic_fast_ms(), timeout_ms, false);
  uevent_unlock(ev);
  uevent_put(uev);
  return ret;
}

static void remove_event_from_epoll(uev_t *uev) {
  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);
  uevent_base_t *base = ATOM_LOAD_ACQ(ev->base);
  if (base == NULL) return;
  if (!atomic_deactivate_fd(ev)) return;

  if (internal_is_fd_event(ev)) {
    (void)epoll_ctl(base->epoll_fd, EPOLL_CTL_DEL, ev->fd, NULL);
  }

  if (ev != &base->wakeup_event) {
    atomic_fetch_sub_explicit(&base->num_active_fd, 1, memory_order_acq_rel);
  }
  uevent_put(uev);
}

static void remove_event_from_heap(uev_t *uev, bool no_lock) {
  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);
  uevent_base_t *base = atomic_load_explicit(&ev->base, memory_order_acquire);
  if (base == NULL) return;

  if (!atomic_deactivate_timer(ev)) {
    return;
  }
  if (!no_lock) {
    if (pthread_mutex_lock(&base->base_mut) != 0) {
      atomic_store_explicit(&ev->active_timer, true, memory_order_release);
      return;
    }
  }
  mh_delete_node(base->timer_heap, &ev->timer_node);
  atomic_fetch_sub_explicit(&base->num_active_timers, 1, memory_order_acq_rel);
  if (!no_lock) {
    (void)pthread_mutex_unlock(&base->base_mut);
  }
  uevent_put(uev);
}

int uevent_del(uev_t *uev) {
  FUNC_START_DEBUG;
  TINIT;
  TMARK(10, "uevent_del");
  if (uev == NULL) {
    TMARK(10, "uevent_del");
    return UEV_ERR_INVAL;
  }

  if (uevent_try_ref(uev) == NULL) {
    TMARK(10, "uevent_del");
    return UEV_ERR_PENDING_FREE;
  }

  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);

  syslog2(LOG_DEBUG, "[UEVENT_DEL] deleting event name='%s'", ev->name);
  remove_event_from_epoll(uev);
  remove_event_from_heap(uev, false);
  uevent_put(uev);

  TMARK(10, "uevent_del");
  return UEV_ERR_OK;
}

typedef struct {
  uevent_t *ev;
  uint64_t cron_time;
} expired_timer_info_t;

typedef struct {
  expired_timer_info_t *batch;
  int *count;
  int capacity;
} collection_context_t;

// если это PERSIST-таймер и не помечен на удаление — перепланировать
static void cron_persist_event_if_needed_internal_unsafe(uevent_base_t *base, uev_t *uev, uint64_t cron_key) {
  if (!uev) return;

  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  if (!ev || ATOM_LOAD_ACQ(ev->pending_free) || !base || !ATOM_LOAD_ACQ(base->running)) return;

  if (!internal_is_persist_timer(ev)) return;

  int timeout = ATOM_LOAD_ACQ(ev->timeout_ms);
  timeout = get_min_timeout_internal(timeout);
  insert_timer_to_heap(uev, cron_key, timeout, true);
}

// вызвать колбэк таймера, если он есть
static void call_cb_if_exists(uevent_t *ev, uint64_t cron_key) {
  FUNC_START_DEBUG;
  if (ev->cb != NULL) uevent_handle_ev_cb(ev, UEV_TIMEOUT, cron_key);
}

// обработать все истекшие таймеры
static void uevent_handle_timers(uevent_base_t *base) {
  if (!atomic_load_explicit(&base->running, memory_order_acquire)) return;

  FUNC_START_DEBUG;
  TINIT;
  TMARK(0, "START");

  if (base == NULL) return;

  if (!atomic_load_explicit(&base->running, memory_order_acquire)) {
    syslog2(LOG_DEBUG, "[TIMER_HANDLE] event loop is not running. processing timers aborted");
    return;
  }

  TMARK(10, "mutex_lock base");
  (void)pthread_mutex_lock(&base->base_mut);
  TMARK(10, "mutex_lock base OK");

  uint64_t now = tu_clock_gettime_monotonic_fast_ms();
  uint16_t max = 500;

  minheap_node_t *prev = NULL;
  while (max-- > 0) {
    minheap_node_t *min = mh_get_min(base->timer_heap);
    if (min == NULL) break;

    if (min->key > now) break;

    if (prev == min) break;

    TMARK(10, "extract_min");
    minheap_node_t *expired = mh_extract_min(base->timer_heap);
    TMARK(10, "extract_min ok");
    if (expired == NULL) break;

    uint64_t cron = expired->key;
    uevent_t *ev = container_of(expired, uevent_t, timer_node);
    uev_t *uev = ATOM_LOAD_ACQ(ev->uev);
    if (!uev) {
      syslog2(LOG_NOTICE, "[TIMER] Skipping timer with NULL uev, name='%s'", ev->name);
      continue;
    }

    atomic_store_explicit(&ev->active_timer, false, memory_order_release);
    atomic_fetch_sub_explicit(&base->num_active_timers, 1, memory_order_acq_rel);

    if (uevent_put(uev)) continue; // после извлечения из кучи нужно удалить ссылку

    TMARK(10, "cron_persist_timer_if_needed_internal");
    cron_persist_event_if_needed_internal_unsafe(base, uev, cron);
    TMARK(10, "cron_persist_timer_if_needed_internal ok");

    (void)pthread_mutex_unlock(&base->base_mut);

    TMARK(10, "call_cb_if_exists");
    call_cb_if_exists(ev, cron);
    TMARK(10, "call_cb_if_exists OK");

    TMARK(10, "mutex lock base");
    (void)pthread_mutex_lock(&base->base_mut);
    TMARK(10, "mutex lock base ok");
  }

  (void)pthread_mutex_unlock(&base->base_mut);
  TMARK(10, "FINISH");
}

static int calculate_epoll_timeout(uevent_base_t *base) {
  int epoll_timeout = EPOLL_MAX_TIMEOUT_MS;
  wakeup_fd_reset(base);
  if (atomic_load_explicit(&base->num_active_timers, memory_order_acquire) == 0) {
    return epoll_timeout;
  }

  if (pthread_mutex_lock(&base->base_mut) != 0) {
    syslog2(LOG_ERR, "failed to lock base mutex");
    return 100;
  }

  minheap_node_t *min_node = mh_get_min(base->timer_heap);
  if (min_node != NULL) {
    uint64_t current_time = tu_clock_gettime_monotonic_fast_ms();
    uevent_t *ev = container_of(min_node, uevent_t, timer_node);
    syslog2(LOG_DEBUG, "name='%s' min_node->key=%" PRIu64 " cur_time=%" PRIu64, ev->name, min_node->key, current_time);
    if (min_node->key <= current_time) {
      epoll_timeout = 0;
    } else {
      uint64_t diff = min_node->key - current_time;
      if (diff < (uint64_t)epoll_timeout) {
        epoll_timeout = (int)diff;
      }
    }
  }

  (void)pthread_mutex_unlock(&base->base_mut);
  return epoll_timeout;
}

static short convert_from_epoll_events(uint32_t epoll_events) {
  FUNC_START_DEBUG;

  short triggered_events = 0;
  if ((epoll_events & EPOLLIN) != 0) {
    triggered_events |= UEV_READ;
  }
  if ((epoll_events & EPOLLOUT) != 0) {
    triggered_events |= UEV_WRITE;
  }
  if ((epoll_events & EPOLLERR) != 0) {
    triggered_events |= UEV_ERROR;
  }
  if ((epoll_events & EPOLLHUP) != 0) {
    triggered_events |= UEV_HUP;
  }
  return triggered_events;
}

static void uevent_handle_epoll(uevent_base_t *base, int nfds) {
  if (!atomic_load_explicit(&base->running, memory_order_acquire)) return;

  for (int i = 0; i < nfds; i++) {
    uev_t *uev = (uev_t *)base->events[i].data.ptr;
    short triggered_events = convert_from_epoll_events(base->events[i].events);
    uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
    syslog2(LOG_DEBUG, "[EPOLL DBG] name='%s'", ev->name);
    if (!atomic_load_explicit(&ev->active_fd, memory_order_acquire)) {
      continue;
    }
    uevent_handle_ev_cb(ev, triggered_events, 0);
    if (internal_should_auto_del_fd(ev, triggered_events)) {
      uevent_del(uev);
    }
  }
}

static void dump_timer_heap(uevent_base_t *base) {
  return;
  if (!(cached_mask & LOG_MASK(LOG_NOTICE))) return;
  if (!base || !base->timer_heap) return;
  if (pthread_mutex_lock(&base->base_mut) != 0) return;

  int size = mh_get_size(base->timer_heap);
  syslog2(LOG_NOTICE, "=== timer heap_size=%d ===", size);
  for (int i = 0; i < size; ++i) {
    minheap_node_t *node = mh_get_node(base->timer_heap, i);
    if (!node) continue;
    uevent_t *ev = container_of(node, uevent_t, timer_node);
    syslog2(LOG_NOTICE, "  [%03d] key=%" PRIu64 " name='%s'", i, node->key, ev->name ? ev->name : "(null)");
  }
  pthread_mutex_unlock(&base->base_mut);
}

int uevent_base_dispatch(uevent_base_t *base) {
  FUNC_START_DEBUG;
  if (base == NULL) {
    return UEV_ERR_INVAL;
  }
  TINIT;
  TMARK(0, "START");
  atomic_store_explicit(&base->running, true, memory_order_release);
  atomic_store_explicit(&base->stopped, false, memory_order_release);
  while (atomic_load_explicit(&base->running, memory_order_acquire)) {
    dump_timer_heap(base);
    // uevent_zombie_list_purge(base);

    if (!uevent_base_has_events(base)) {
      syslog2(LOG_DEBUG, "no active events left, breaking event loop.");
      atomic_store_explicit(&base->running, false, memory_order_release);
      break;
    }
    TMARK(10, "uevent_base_has_events");
    int epoll_timeout = calculate_epoll_timeout(base);
    TMARK(10, "calculate_epoll_timeout");
    uint64_t __t_mark = tu_clock_gettime_monotonic_fast_ms();
    int nfds = epoll_wait(base->epoll_fd, base->events, base->max_events, epoll_timeout);
    uint64_t __t_now = tu_clock_gettime_monotonic_fast_ms();
    int64_t slept_ms = (int64_t)__t_now - (int64_t)__t_mark;
    __t_mark = __t_now;
    syslog2(LOG_DEBUG, "[EPOLL_DBG] epoll_timeout=%d slept_ms=%" PRId64 "", epoll_timeout, slept_ms);

    TMARK(epoll_timeout + 10, "epoll_wait");
    if (nfds == -1) {
      if (errno == EINTR) {
        continue;
      }
      syslog2(LOG_ERR, "epoll_wait failed: %s", strerror(errno));
      return UEV_ERR_EPOLL;
    }
    uevent_handle_timers(base);
    TMARK(10, "uevent_handle_timers");
    if (nfds > 0) {
      uevent_handle_epoll(base, nfds);
      TMARK(10, "uevent_handle_epoll");
    }
  }

  uevent_worker_pool_wait_for_idle(base->worker_pool);
  TMARK(10, "FINISH");

  // берем мьютекс, чтобы после сигнала другой поток не успел раньше
  pthread_mutex_lock(&base->base_mut);
  atomic_store_explicit(&base->stopped, true, memory_order_release);
  pthread_cond_signal(&base->base_cond);
  pthread_mutex_unlock(&base->base_mut);

  return UEV_ERR_OK;
}

void uevent_base_loopbreak(uevent_base_t *base) {
  FUNC_START_DEBUG;
  if (base == NULL) {
    return;
  }
  atomic_store_explicit(&base->running, false, memory_order_release);

  // Очищаем кучу таймеров
  pthread_mutex_lock(&base->base_mut);
  while (mh_get_min(base->timer_heap) != NULL) {
    minheap_node_t *expired = mh_extract_min(base->timer_heap);
    if (expired == NULL) break;
    uevent_t *ev = container_of(expired, uevent_t, timer_node);
    uev_t *uev = ATOM_LOAD_ACQ(ev->uev);
    atomic_store_explicit(&ev->active_timer, false, memory_order_release);
    atomic_fetch_sub_explicit(&base->num_active_timers, 1, memory_order_acq_rel);
    if (uev) uevent_put(uev);
    syslog2(LOG_DEBUG, "[LOOPBREAK] Removed timer: name='%s'", ev->name);
  }
  pthread_mutex_unlock(&base->base_mut);

  uevent_base_wakeup(base);
}

void uevent_free(uev_t *uev) {
  FUNC_START_DEBUG;
  if (uev == NULL) return;
  if (ATOM_LOAD_RELAX(uev->refcount) < 1) return;

  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  if (ev == NULL || atomic_load_explicit(&ev->pending_free, memory_order_acquire)) {
    return;
  }

  uevent_del(uev);

  if (atomic_exchange_explicit(&ev->pending_free, true, memory_order_acq_rel)) {
    return;
  }

  uevent_put(uev);
}

static void uevent_base_wait_stopped(uevent_base_t *base) {
  TINIT;
  TMARK(0, "START");
  if (base == NULL) {
    return;
  }

  pthread_mutex_lock(&base->base_mut);
  while (!atomic_load_explicit(&base->stopped, memory_order_acquire)) {
    pthread_cond_wait(&base->base_cond, &base->base_mut);
  }
  pthread_mutex_unlock(&base->base_mut);
  TMARK(0, "FINISH");
}

void uevent_deinit(uevent_base_t *base) {
  if (base == NULL) {
    return;
  }
  FUNC_START_DEBUG;

  uevent_base_loopbreak(base);

  uevent_base_wait_stopped(base);

  if (base->worker_pool != NULL) {
    uevent_worker_pool_stop(base->worker_pool);
    uevent_worker_pool_wait_for_idle(base->worker_pool);
    uevent_worker_pool_destroy(base->worker_pool);
    base->worker_pool = NULL;
  }

  pthread_mutex_lock(&base->base_mut);
  for (unsigned int i = 0; i < base->uev_arr_sz; i++) {
    uev_t *uev = &base->uev_arr[i];
    int old = atomic_load_explicit(&uev->refcount, memory_order_relaxed);
    if (old == 0) continue; // skip zombie event
    uevent_free(uev);
  }
  pthread_mutex_unlock(&base->base_mut);

  // uevent_zombie_list_purge(base);

  if (base->wakeup_event.fd != -1) {
    close(base->wakeup_event.fd);
    base->wakeup_event.fd = -1;
  }

  mh_free(base->timer_heap);
  free(base->events);
  uev_slots_deinit(base);
  close(base->epoll_fd);

  pthread_mutex_destroy(&base->base_mut);
  pthread_cond_destroy(&base->base_cond);
  // uevent_zombie_list_deinit(base);

  free(base);
}

void uevent_ref(uev_t *uev) {
  int old = atomic_fetch_add_explicit(&uev->refcount, 1, memory_order_relaxed);
  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  syslog2(LOG_DEBUG, "name='%s' inc refcount=%d", ev->name, old + 1);
}

static inline bool refcount_inc_not_zero(atomic_int *r) {
  int old = atomic_load_explicit(r, memory_order_relaxed);
  while (true) {
    if (old == 0)
      return false;
    if (atomic_compare_exchange_weak_explicit(
            r,
            &old,
            old + 1,
            memory_order_acq_rel,
            memory_order_relaxed)) {
      return true;
    }
  }
}

uev_t *uevent_try_ref(uev_t *uev) {
  if (!uev) {
    syslog2(LOG_ERR, "error: EINVAL uev=NULL");
    return NULL;
  }
  if (!refcount_inc_not_zero(&uev->refcount)) {
    syslog2(LOG_DEBUG, "error: refcount is zero uev=%p", uev);
    return NULL;
  }

  uevent_t *ev = ATOM_LOAD_RELAX(uev->ev);
  if (!ev) {
    syslog2(LOG_ERR, "error: NULL ev pointer for uev=%p", uev);
    atomic_fetch_sub_explicit(&uev->refcount, 1, memory_order_relaxed);
    return NULL;
  }

  if (atomic_load_explicit(&ev->pending_free, memory_order_acquire)) {
    syslog2(LOG_DEBUG, "error: event pending free for name='%s'", ev->name);
    atomic_fetch_sub_explicit(&uev->refcount, 1, memory_order_relaxed);
    return NULL;
  }

  syslog2(LOG_DEBUG, "name='%s' inc refcount=%d", ev->name, atomic_load_explicit(&uev->refcount, memory_order_relaxed));
  return uev;
}

int uevent_unref(uev_t *uev) {
  int old = atomic_fetch_sub_explicit(&uev->refcount, 1, memory_order_release);
  assert(old > 0 && "refcount <= 0");

  if (old == 1) {
    syslog2(LOG_DEBUG, "name='---' dec refcount=%d", old - 1);
  } else {
    uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
    syslog2(LOG_DEBUG, "name='%s' dec refcount=%d", ev->name, old - 1);
  }
  return old - 1;
}

static bool uevent_refcount_dec_and_test(uev_t *uev) {
  int old = atomic_fetch_sub_explicit(&uev->refcount, 1, memory_order_acq_rel);
  assert(old > 0 && "refcount <= 0");
  return old == 1;
}

bool uevent_put(uev_t *uev) {
  if (!uev) return false;

  int old = ATOM_LOAD_RELAX(uev->refcount);
  // Если уже 0 — ничего не делаем, объект уже был уничтожен
  if (old == 0) return false;

  if (uevent_refcount_dec_and_test(uev)) {
    uevent_destroy_uev_internal_unsafe(uev);
    return true;
  }
  return false;
}
