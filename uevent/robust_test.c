#include "uevent.h"
#include "uevent_internal.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Настройки теста
#define MAX_EVENTS 1000000
#define TEST_DURATION_S 10
#define STATIC_EVENT_PERCENTAGE 40
#define CHAOS_ITERATIONS 40000

// Глобальные ресурсы
static uev_t *g_events[MAX_EVENTS] = {0};
static uevent_t g_stat_events[MAX_EVENTS] = {[0 ... MAX_EVENTS - 1] = {.is_static = true}};
static _Atomic bool g_stat_evs_init[MAX_EVENTS] = {0};
static uevent_base_t *g_base = NULL;
static atomic_int g_callback_invoked = 0;
static atomic_int g_dynamic_created = 0;
static atomic_int g_dynamic_freed = 0;
static atomic_int g_stat_created = 0;
static atomic_int g_static_freed = 0;

// Колбэк для событий
void chaos_callback(uevent_t *ev, int fd, short event, void *arg) {
  atomic_fetch_add(&g_callback_invoked, 1);
}

static void delete_event(int idx) {
  uev_t *uev = atomic_load(&g_events[idx]);
  if (!uev) {
    return;
  }

  if (!uevent_try_ref(uev)) {
    return;
  }

  uev_t *uev_to_free = atomic_exchange(&g_events[idx], NULL);

  if (uev_to_free->ev->is_static) {
    uevent_t *ev = ATOM_LOAD_ACQ(uev->ev);
    ptrdiff_t static_idx = ev - g_stat_events;
    atomic_store(&g_stat_evs_init[static_idx], false);
    atomic_fetch_add(&g_static_freed, 1);
  } else {
    atomic_fetch_add(&g_dynamic_freed, 1);
  }
  uevent_put(uev); // Отпускаем ссылку, взятую в try_ref
  uevent_put(uev); // Отпускаем ссылку, взятую при добавлении в g_events
  uevent_free(uev_to_free);
}

// Создание нового события и добавление в g_events[idx]
static void create_event(int idx) {
  uev_t *new_uev = NULL;
  if (rand() % 100 < STATIC_EVENT_PERCENTAGE) {
    for (int stat_idx = 0; stat_idx < MAX_EVENTS; stat_idx++) {
      bool expected = false;
      if (atomic_load(&g_stat_evs_init[stat_idx])) continue;
      if (atomic_compare_exchange_strong(&g_stat_evs_init[stat_idx], &expected, true)) {
        new_uev = uevent_create_or_assign_event(&g_stat_events[stat_idx], g_base, -1, UEV_TIMEOUT | UEV_PERSIST, chaos_callback, NULL, "chaos_static_ev");
        if (new_uev) {
          atomic_fetch_add(&g_stat_created, 1);
        } else {
          atomic_store(&g_stat_evs_init[stat_idx], false);
        }
        break;
      }
    }
  } else {
    new_uev = uevent_create_or_assign_event(NULL, g_base, -1, UEV_TIMEOUT | UEV_PERSIST, chaos_callback, NULL, "chaos_dynamic_ev");
    if (new_uev) {
      atomic_fetch_add(&g_dynamic_created, 1);
    }
  }
  if (new_uev) {
    uevent_ref(new_uev);
    atomic_exchange(&g_events[idx], new_uev);
    uevent_set_timeout(new_uev, 10);
    uevent_add_with_current_timeout(new_uev);
  }
}

// Поток для создания и удаления событий
void *create_destroy_thread(void *arg) {
  for (int i = 0; i < CHAOS_ITERATIONS; i++) {
    int idx = rand() % MAX_EVENTS;
    delete_event(idx);
    create_event(idx);
    usleep(rand() % 50);
  }
  return NULL;
}

// Поток для хаотичного вызова API
void *stress_thread(void *arg) {
  for (int i = 0; i < CHAOS_ITERATIONS; i++) {
    int idx = rand() % MAX_EVENTS;
    uev_t *uev = atomic_load(&g_events[idx]);
    if (!uev) {
      continue;
    }
    if (!uevent_try_ref(uev)) {
      continue;
    }
    switch (rand() % 6) {
    case 0: {
      uevent_add(uev, rand() % 50);
      break;
    }
    case 1: {
      uevent_del(uev);
      break;
    }
    case 2: {
      uevent_set_timeout(uev, rand() % 100);
      break;
    }
    case 3: {
      uevent_pending(uev, UEV_READ | UEV_WRITE | UEV_TIMEOUT);
      break;
    }
    case 4: {
      uevent_active(uev);
      break;
    }
    case 5: {
      uevent_add_with_current_timeout(uev);
      break;
    }
    }
    uevent_put(uev);
    usleep(rand() % 20);
  }
  return NULL;
}

// Поток для прерывания цикла событий
void *final_loopbreak_thread(void *arg) {
  int delay_s = *(int *)arg;
  sleep(delay_s);
  printf("[Loopbreak Thread] Test time is up. Breaking dispatch loop.\n");
  uevent_base_loopbreak(g_base);
  return NULL;
}

// Основная функция теста
void run_chaos_test() {
  printf("--- Running Chaos Test ---\n");
  printf("  - Threads: 1 creator, 1 stressor\n");
  printf("  - Static events ratio: %d%%\n", STATIC_EVENT_PERCENTAGE);
  printf("  - Test duration: %d seconds\n", TEST_DURATION_S);

  g_base = uevent_base_new_with_workers(MAX_EVENTS, 4);
  assert(g_base != NULL);

  pthread_t creator, stressor, loopbreaker;
  int duration = TEST_DURATION_S;
  pthread_create(&creator, NULL, create_destroy_thread, NULL);
  pthread_create(&stressor, NULL, stress_thread, NULL);
  pthread_create(&loopbreaker, NULL, final_loopbreak_thread, &duration);

  uevent_base_dispatch(g_base);

  printf("  - Waiting for threads to finish...\n");
  pthread_join(creator, NULL);
  pthread_join(stressor, NULL);
  pthread_join(loopbreaker, NULL);

  printf("  - Cleaning up events...\n");
  for (int i = 0; i < MAX_EVENTS; i++) {
    uev_t *uev = atomic_exchange(&g_events[i], NULL);
    if (uev) {
      if (uev->ev->is_static) {
        int static_idx = uev->ev - g_stat_events;
        atomic_store(&g_stat_evs_init[static_idx], false);
        atomic_fetch_add(&g_static_freed, 1);
      } else {
        atomic_fetch_add(&g_dynamic_freed, 1);
      }
      uevent_free(uev);
    }
  }

  printf("  - Final cleanup...\n");
  uevent_deinit(g_base);

  printf("--- Chaos Test Stats ---\n");
  printf("  - Dynamic events created: %d\n", atomic_load(&g_dynamic_created));
  printf("  - Dynamic events freed: %d\n", atomic_load(&g_dynamic_freed));
  printf("  - Static events created: %d\n", atomic_load(&g_stat_created));
  printf("  - Static events freed: %d\n", atomic_load(&g_static_freed));
  printf("  - Callbacks run: %d\n", atomic_load(&g_callback_invoked));
  printf("--- Chaos Test PASSED (if no crash/hang/sanitizer error) ---\n");
}

int main() {
  printf("=================================================\n");
  printf("     Starting uevent Chaos Test\n");
  printf("=================================================\n");

#ifdef DEBUG
  setup_syslog2("uevent_test", LOG_DEBUG, false);
#else
  setup_syslog2("uevent_test", LOG_WARNING, false);
#endif
  srand(time(NULL));

  run_chaos_test();

  printf("All tests completed.\n");
  return 0;
}
