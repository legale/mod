#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../list/list.h"
#include "../syslog2/syslog2.h"
#include "uevent.h" // uevent_t и uevent_cb_t

#include "uevent_internal.h"
#include "uevent_worker.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

#define UEV_EXTRA_WORKER_TASKS 100
#define UEV_MAX_WORKER_MULTIPLIER 8

static _Atomic bool enable_extra_workers = true;

void uevent_worker_pool_enable_extra_workers(bool enable) {
  atomic_store(&enable_extra_workers, enable);
}

// Внутренняя структура для задачи в очереди
typedef struct {
  uev_t *uev;
  uint64_t cron_time;
  short triggered_events;
  struct list_head node;
} uevent_task_t;

// основная структура пула воркеров
typedef struct uevent_worker_pool_t {
  _Atomic bool running;
  _Atomic int active_tasks;
  int queue_size;

  _Atomic int total_workers; // текущее общее число воркеров (основные + экстра)
  unsigned int num_workers;
  pthread_t *workers;
  struct list_head task_queue;
  pthread_mutex_t task_mutex;
  pthread_cond_t task_cond;

  pthread_mutex_t idle_mutex;
  pthread_cond_t idle_cond;
} uevent_worker_pool_t;

// forward declaration
static void try_spawn_extra_worker(uevent_worker_pool_t *pool);

// ожидание и извлечение задачи из очереди
static uevent_task_t *wait_and_pop_task(uevent_worker_pool_t *pool) {
  pthread_mutex_lock(&pool->task_mutex);

  // ждем, пока появится задача или пул не остановят
  while (!pool->queue_size && atomic_load_explicit(&pool->running, memory_order_acquire)) {
    pthread_cond_wait(&pool->task_cond, &pool->task_mutex);
  }

  // если очередь пуста — возвращаем NULL
  if (pool->queue_size < 1) {
    pthread_mutex_unlock(&pool->task_mutex);
    return NULL;
  }

  if (syslog2_get_pri() & LOG_MASK(LOG_DEBUG)) {
    syslog2(LOG_DEBUG, "[WORKER_QUEUE] task popped, queue_size=%d", pool->queue_size);
  } else if (pool->queue_size > ATOM_LOAD_ACQ(pool->total_workers)) {
    syslog2(LOG_WARNING, "[WORKER_QUEUE] task popped, queue_size=%d", pool->queue_size);
    try_spawn_extra_worker(pool);
  }

  // извлекаем задачу из очереди
  uevent_task_t *task = list_first_entry(&pool->task_queue, uevent_task_t, node);
  list_del(&task->node);
  pool->queue_size--; // уменьшаем размер очереди.

  atomic_fetch_add_explicit(&pool->active_tasks, 1, memory_order_acq_rel);

  pthread_mutex_unlock(&pool->task_mutex);
  return task;
}

// обработка одной задачи воркером
static void process_worker_task(uevent_worker_pool_t *pool, uevent_task_t *task) {
  if (task == NULL || task->uev == NULL || !uevent_try_ref(task->uev)) {
    return;
  }

  uev_t *uev = task->uev;
  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
  if (ev == NULL || ev->cb == NULL || ATOM_LOAD_ACQ(ev->pending_free)) {
    uevent_put(uev);
    return;
  }

  uint64_t cb_start_time = tu_clock_gettime_monotonic_ms();
  int64_t queue_delay = (int64_t)cb_start_time - (int64_t)task->cron_time;

  // логируем задержку в очереди
  if (task->cron_time) {
    int refcount = ATOM_LOAD_ACQ(uev->refcount);
    if (syslog2_get_pri() & LOG_MASK(LOG_DEBUG)) {
      syslog2(LOG_DEBUG, "[WORKER_DBG] name='%s' task_start_delay=%" PRId64 " refcount=%d", ev->name, queue_delay, refcount);
    } else if (queue_delay > 10000) {
      syslog2(LOG_WARNING, "[WORKER_LAG] name='%s' task_start_delay=%" PRId64 " refcount=%d", ev->name, queue_delay, refcount);
      try_spawn_extra_worker(pool);
    }
  }

  syslog2(LOG_ALERT, "[WORKER_START] name=%s fd=%d", ev->name, ev->fd);
  ev->cb_wrapper(ev, ev->fd, task->triggered_events, task->cron_time, ev->cb, ev->arg);
  syslog2(LOG_ALERT, "[WORKER_END] name=%s fd=%d", ev->name, ev->fd);
  uevent_put(uev);
}

// освобождение задачи и обновление счетчиков
static void finalize_worker_task(uevent_worker_pool_t *pool, uevent_task_t *task) {
  if (task) {
    if (task->uev->ev) {
      uevent_t *ev = task->uev->ev;
      atomic_store_explicit(&ev->is_in_worker_pool, false, memory_order_release);
      uevent_put(task->uev);
    }
    free(task);
  }

  int prev_active = atomic_fetch_sub_explicit(&pool->active_tasks, 1, memory_order_acq_rel);
  if (prev_active == 1) {
    pthread_mutex_lock(&pool->idle_mutex);
    pthread_cond_broadcast(&pool->idle_cond); // будим всех!
    pthread_mutex_unlock(&pool->idle_mutex);
  }
}

// основная функция воркера
static void *uevent_worker_thread(void *arg) {
  FUNC_START_DEBUG;
  PTHREAD_SET_NAME(__func__);
  uevent_worker_pool_t *pool = (uevent_worker_pool_t *)arg;
  while (atomic_load_explicit(&pool->running, memory_order_acquire)) {

    uevent_task_t *task = wait_and_pop_task(pool); // ждем и извлекаем задачу
    if (task == NULL) continue;                    // если задач нет — продолжаем цикл

    process_worker_task(pool, task);  // обработать задачу
    finalize_worker_task(pool, task); // освободить задачу и обновить счетчики
  }
  // при завершении уменьшаем счетчик воркеров
  atomic_fetch_sub_explicit(&pool->total_workers, 1, memory_order_acq_rel);
  return NULL;
}

static void *uevent_extra_worker_thread(void *arg) {
  uevent_worker_pool_t *pool = (uevent_worker_pool_t *)arg;
  int tasks_left = UEV_EXTRA_WORKER_TASKS;
  while (atomic_load_explicit(&pool->running, memory_order_acquire) && tasks_left-- > 0) {
    uevent_task_t *task = wait_and_pop_task(pool);
    if (!task) break; // если задачи кончились, просто завершаемся
    process_worker_task(pool, task);
    finalize_worker_task(pool, task);
  }
  // при завершении уменьшаем счетчик воркеров
  atomic_fetch_sub_explicit(&pool->total_workers, 1, memory_order_acq_rel);
  return NULL;
}

static void try_spawn_extra_worker(uevent_worker_pool_t *pool) {
  FUNC_START_DEBUG;
  if (!atomic_load(&enable_extra_workers)) {
    return;
  }
  int max_workers = pool->num_workers * UEV_MAX_WORKER_MULTIPLIER;
  int old_total;

  // Compare-And-Swap цикл: пытаемся увеличить total_workers только если не превышен лимит
  do {
    // читаем текущее значение total_workers
    old_total = ATOM_LOAD_ACQ(pool->total_workers);

    // если лимит достигнут — выходим
    if (old_total >= max_workers) {
      int qsize = pool->queue_size;
      syslog2(LOG_WARNING, "max total workers reached! %d/%d qsize=%d", old_total, max_workers, qsize);
      return;
    }

    // пытаемся атомарно увеличить total_workers на 1
  } while (!atomic_compare_exchange_weak_explicit(
      &pool->total_workers, // указатель на атомарный счетчик воркеров
      &old_total,           // указатель на ожидаемое значение, сюда же запишется текущее при неудаче
      old_total + 1,        // новое значение, которое хотим установить (old_total + 1)
      memory_order_acq_rel, // порядок памяти при успешном обмене (acquire+release)
      memory_order_acquire  // порядок памяти при неудаче (только acquire)
      ));

  // если мы здесь, то лимит не превышен, счетчик увеличен, можно создавать поток
  pthread_t tid;
  if (pthread_create(&tid, NULL, uevent_extra_worker_thread, pool) == 0) {
    pthread_detach(tid);
    syslog2(LOG_WARNING, "created extra worker total_workers=%d", old_total + 1);
  } else {
    // если поток не удалось создать, откатываем счетчик обратно
    atomic_fetch_sub_explicit(&pool->total_workers, 1, memory_order_acq_rel);
  }
}

/**
 * @brief Создает и запускает пул рабочих потоков.
 */
uevent_worker_pool_t *uevent_worker_pool_create(int num_workers) {
  uevent_worker_pool_t *pool = NULL;
  unsigned int i;

  if (num_workers <= 0) return NULL;

  pool = calloc(1, sizeof(uevent_worker_pool_t));
  if (pool == NULL) return NULL;

  pool->num_workers = num_workers;
  atomic_store_explicit(&pool->running, true, memory_order_release);
  atomic_store_explicit(&pool->active_tasks, 0, memory_order_release);
  INIT_LIST_HEAD(&pool->task_queue);

  if (pthread_mutex_init(&pool->task_mutex, NULL) != 0) goto fail;
  if (pthread_cond_init(&pool->task_cond, NULL) != 0) goto fail_task_mutex;
  if (pthread_mutex_init(&pool->idle_mutex, NULL) != 0) goto fail_task_cond;
  if (pthread_cond_init(&pool->idle_cond, NULL) != 0) goto fail_idle_mutex;

  pool->workers = calloc(num_workers, sizeof(pthread_t));
  if (pool->workers == NULL) goto fail_idle_cond;

  for (i = 0; i < pool->num_workers; i++) {
    if (pthread_create(&pool->workers[i], NULL, uevent_worker_thread, pool) == 0) {
      atomic_fetch_add_explicit(&pool->total_workers, 1, memory_order_acq_rel);
    } else {
      pool->num_workers = i;
      uevent_worker_pool_destroy(pool);
      return NULL;
    }
  }

  return pool;

fail_idle_cond:
  pthread_cond_destroy(&pool->idle_cond);
fail_idle_mutex:
  pthread_mutex_destroy(&pool->idle_mutex);
fail_task_cond:
  pthread_cond_destroy(&pool->task_cond);
fail_task_mutex:
  pthread_mutex_destroy(&pool->task_mutex);
fail:
  free(pool);
  return NULL;
}

/**
 * @brief Добавляет задачу (вызов колбека) в очередь пула воркеров
 */
void uevent_worker_pool_insert(uevent_worker_pool_t *pool, uev_t *uev, short triggered_events, uint64_t cron_time) {
  FUNC_START_DEBUG;
  if (pool == NULL || uev == NULL) {
    return;
  }
  uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);

  if (!ev || atomic_load_explicit(&uev->ev->is_in_worker_pool, memory_order_acquire)) return;

  TINIT;
  TMARK(10, "START");
  uevent_task_t *task = malloc(sizeof(uevent_task_t));
  if (task == NULL) {
    syslog2(LOG_ERR, "Failed to allocate memory for uevent task");
    return;
  }

  // увеличить счетчик ссылок перед добавлением в очередь
  uevent_ref(uev);
  // поднять флаг, что event в пуле
  ATOM_STORE_REL(ev->is_in_worker_pool, true);
  task->uev = uev;
  task->cron_time = cron_time;
  task->triggered_events = triggered_events;

  pthread_mutex_lock(&pool->task_mutex);
  list_add_tail(&task->node, &pool->task_queue);

  pool->queue_size++; // увеличиваем размер очереди

  pthread_cond_signal(&pool->task_cond); // Разбудить один ожидающий поток
  pthread_mutex_unlock(&pool->task_mutex);

  TMARK(10, "END");
}

static void trigger_workers_internal(uevent_worker_pool_t *pool) {
  pthread_mutex_lock(&pool->task_mutex);
  pthread_cond_broadcast(&pool->task_cond);
  pthread_mutex_unlock(&pool->task_mutex);
  pthread_mutex_lock(&pool->idle_mutex);
  pthread_cond_broadcast(&pool->idle_cond);
  pthread_mutex_unlock(&pool->idle_mutex);
}

/**
 * @brief Останавливает и уничтожает пул рабочих потоков.
 */
void uevent_worker_pool_destroy(uevent_worker_pool_t *pool) {
  if (pool == NULL) {
    return;
  }

  TINIT;
  TMARK(0, "START");

  // сигнал к завершению
  atomic_store_explicit(&pool->running, false, memory_order_release);

  // Будим все потоки, чтобы они проверили флаг 'running'

  // Дать шанс воркерам заснуть и еще раз пытаемся их разбудить
  // это для того, что гарантированно зацепить воркеры, которые могли еще не спать на первом вызове
  int tries = 3;
  while (tries-- > 0) {
    msleep(1); // ждем 1 мс
    trigger_workers_internal(pool);
  }
  TMARK(10, "trigger_workers_internal ok");

  // Ждем завершения каждого потока
  TMARK(10, "worker threads finished");
  for (unsigned int i = 0; i < pool->num_workers; i++) {
    pthread_join(pool->workers[i], NULL);
  }
  TMARK(0, "worker threads finished");

  int cnt = 50;
  while (cnt-- > 0 && atomic_load_explicit(&pool->total_workers, memory_order_acquire) > 0) {
    msleep(100); // Неактивное ожидание (можно заменить на condvar, если нужно)
  }

  // Освобождаем оставшиеся ресурсы
  free(pool->workers);

  pthread_mutex_destroy(&pool->task_mutex);
  pthread_cond_destroy(&pool->task_cond);

  pthread_mutex_destroy(&pool->idle_mutex);
  pthread_cond_destroy(&pool->idle_cond);

  // Очищаем задачи, которые могли остаться в очереди
  uevent_task_t *task, *tmp;
  list_for_each_entry_safe(task, tmp, &pool->task_queue, node) {

    list_del(&task->node);

    // уменьшаем счетчик ссылок
    if (task->uev->ev) {
      uevent_t *ev = task->uev->ev;
      atomic_store_explicit(&ev->is_in_worker_pool, false, memory_order_release);
      uevent_put(task->uev);
    }
    free(task);
  }

  // Освобождаем пул
  free(pool);
  TMARK(0, "END");
}

bool uevent_worker_pool_is_idle(uevent_worker_pool_t *pool) {
  if (!pool) return true;

  TINIT;
  TMARK(0, "START");

  bool queue_empty;
  pthread_mutex_lock(&pool->task_mutex);
  queue_empty = pool->queue_size < 1;
  pthread_mutex_unlock(&pool->task_mutex);

  int active = atomic_load_explicit(&pool->active_tasks, memory_order_acquire);
  syslog2(LOG_DEBUG, "worker pool active_tasks=%d", active);
  TMARK(0, "FINISH");
  return queue_empty && (active == 0);
}

void uevent_worker_pool_wait_for_idle(uevent_worker_pool_t *pool) {
  if (pool == NULL) {
    return;
  }
  TINIT;
  TMARK(0, "START");

  pthread_mutex_lock(&pool->idle_mutex);
  // цикл while здесь необходим для защиты от ложных пробуждений, когда очередь задач пула воркеров на самом деле не пуста
  while (!uevent_worker_pool_is_idle(pool)) {
    // отпускаем мьютекс и засыпаем пока нас не разбудит кто-то через флаг idle_cond
    pthread_cond_wait(&pool->idle_cond, &pool->idle_mutex);
  }
  // pthread_cond_wait отпускает мьютекс пока спит, но снова блокирует перед выходом, поэтому надо его раблокировать
  pthread_mutex_unlock(&pool->idle_mutex);
  TMARK(0, "FINISH");
}

void uevent_worker_pool_stop(uevent_worker_pool_t *pool) {
  if (pool == NULL) {
    return;
  }

  // 1. Устанавливаем флаг остановки
  atomic_store_explicit(&pool->running, false, memory_order_release);

  // 2. Будим все потоки немедленно
  trigger_workers_internal(pool);

  // 3. Очищаем очередь задач (опционально, если нужно сбросить pending tasks)
  pthread_mutex_lock(&pool->task_mutex);

  uevent_task_t *task, *tmp;
  list_for_each_entry_safe(task, tmp, &pool->task_queue, node) {
    list_del(&task->node);
    pool->queue_size--;
    uevent_t *ev = ATOM_LOAD_ACQ(task->uev->ev);
    if (ev) {
      atomic_store_explicit(&ev->is_in_worker_pool, false, memory_order_release);
      uevent_put(task->uev);
    }
    free(task);
  }

  pthread_mutex_unlock(&pool->task_mutex);

  // 4. Будим ожидающие потоки (например, в wait_for_idle)
  pthread_mutex_lock(&pool->idle_mutex);
  pthread_cond_broadcast(&pool->idle_cond);
  pthread_mutex_unlock(&pool->idle_mutex);
}
