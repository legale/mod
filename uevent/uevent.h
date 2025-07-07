#ifndef LIBUEVENT_UEVENT_H
#define LIBUEVENT_UEVENT_H

#include "../list/list.h"
#include "../minheap/minheap.h"
#include "../syslog2/syslog2.h"
#include "../timeutil/timeutil.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <time.h>

// макросы для атомарных операций
#define ATOM_LOAD_RELAX(ptr) atomic_load_explicit((&(ptr)), memory_order_relaxed)
#define ATOM_LOAD_ACQ(ptr) atomic_load_explicit((&(ptr)), memory_order_acquire)
#define ATOM_STORE_REL(ptr, val) atomic_store_explicit((&(ptr)), (val), memory_order_release)
#define ATOM_STORE_RELAX(ptr, val) atomic_store_explicit((&(ptr)), (val), memory_order_relaxed)

/* Флаги для событий */
#define UEV_READ (1 << 0)
#define UEV_WRITE (1 << 1)
#define UEV_ERROR (1 << 2)
#define UEV_HUP (1 << 3)
#define UEV_TIMEOUT (1 << 4)
#define UEV_PERSIST (1 << 5)

// флаг включающий все fd события
#define UEV_FD_EVENTS (UEV_READ | UEV_WRITE | UEV_ERROR | UEV_HUP)

// коды ошибок
typedef enum {
  UEV_ERR_OK = 0,
  UEV_ERR_ALLOC = -1,
  UEV_ERR_EPOLL = -2,
  UEV_ERR_MUTEX = -3,
  UEV_ERR_INVAL = -4,
  UEV_ERR_PENDING_FREE = -5,
  UEV_ERR_BUSY = -6,
} uev_status_t;

/* колбек для событий */
typedef struct uevent_t uevent_t; // форвард декларация для типа uevent
typedef void (*uevent_cb_t)(uevent_t *ev, int fd, short events, void *arg);
typedef void (*uevent_cb_wrapper_t)(uevent_t *ev, int fd, short events, uint64_t cron_time_ms, uevent_cb_t cb, void *arg);

/* Структура для базы событий */
typedef struct uevent_base_t uevent_base_t;

/**
 * @brief Специальное значение таймаута для uevent_add().
 * Указывает, что событие должно сработать немедленно (таймаут 0).
 * Используется функцией uevent_active().
 */
#define UEV_TIMEOUT_FIRE_NOW -1

/**
 * @brief Специальное значение таймаута для uevent_add().
 * Указывает, что нужно использовать таймаут, ранее установленный
 * для этого события через uevent_set_timeout().
 * Используется функцией uevent_add_with_current_timeout().
 */
#define UEV_TIMEOUT_FROM_EVENT -2

#define TINIT                                         \
  uint64_t __t_start = 0;                             \
  uint64_t __t_mark = 0;                              \
  do {                                                \
    if (!(cached_mask & LOG_MASK(LOG_DEBUG))) break;  \
    __t_start = tu_clock_gettime_monotonic_fast_ms(); \
    __t_mark = __t_start;                             \
  } while (0)

#define TMARK(thr_ms, comment)                               \
  do {                                                       \
    if (!(cached_mask & LOG_MASK(LOG_DEBUG))) break;         \
    uint64_t __t_now = tu_clock_gettime_monotonic_fast_ms(); \
    uint64_t __t_diff = __t_now - __t_mark;                  \
    if (!(thr_ms) || (__t_diff > (uint64_t)(thr_ms))) {      \
      syslog2(LOG_DEBUG, "[TMARK] %s delay_ms=%" PRIu64 "",  \
              (comment), __t_diff);                          \
    }                                                        \
    __t_mark = __t_now;                                      \
  } while (0)

/* Структура для события */
typedef struct uevent_item_t uev_t; // форвард декларация

typedef struct uevent_t {
  _Atomic bool modification_lock; /* этот флаг используется для синхронизации доступа для изменений event из разных потоков */
  _Atomic bool active_fd;         /* флаг активности события fd (если добавлен в epoll) */
  _Atomic bool active_timer;      /* флаг активности события таймера (если добавлен в кучу) */
  _Atomic bool pending_free;      /* флаг, что событие нужно освободить */
  _Atomic bool is_in_worker_pool; /* этот флаг используется чтобы исключить возможность добавления этого события в пул вокеров дважды */
  const bool is_static;           /* является ли событие статическим */
  short events;                   /* типы событий (UEV_READ, UEV_WRITE и т.д.) */

  _Atomic int timeout_ms;         /* таймаут срабатывания для таймера, используется только, если установлен флаг UEVENT_PERSIST */
  int fd;                         /* дескриптор файла */
  uevent_cb_t cb;                 /* колбек для обработки события */
  uevent_cb_wrapper_t cb_wrapper; /* обертка для колбека, она вызывает внутри себя сам колбек юзера  */
  void *arg;                      /* аргумент для callback */
  _Atomic(uevent_base_t *) base;  /* атомарный указатель на базу событий */

  // struct list_head list_node; /* узел для списка, который хранится в uevent base */
  const char *name;
  uev_t *uev; // указатель на обертку для события

  minheap_node_t timer_node; /* узел таймера для minheap */

} uevent_t;

// обертка для указателя на событие и счетчика ссылок
typedef struct uevent_item_t {
  _Atomic(uevent_t *) ev;
  _Atomic int refcount; /* счетчик ссылок на событие, используется для синхронизации потоков и отложенного освобождения для борьбы с use-after-free */
} uev_t;

// THREAD-SAFE ФУНКЦИИ БИБЛИОТЕКИ

/* Добавляет событие в базу с опциональным таймаутом (в миллисекундах). Возвращает 0 при успехе, -1 при ошибке. */
int uevent_add(uev_t *uev, int timeout_ms);

/* Удаляет событие из базы. Возвращает 0 при успехе, -1 при ошибке. */
int uevent_del(uev_t *uev);

/* Освобождает память события (отмечает событие для освобождения). Для статических событий очищает содержимое. Для динамических — освобождает память. */
void uevent_free(uev_t *uev);

/* деинициализирует базу событий, освобождает все ресурсы. */
void uevent_deinit(uevent_base_t *base);

/* Устанавливает таймаут для EV_PERSIST таймера */
void uevent_set_timeout(uev_t *uev, int timeout_ms);

/* висит ли событие в цикле событий */
bool uevent_pending(uev_t *uev, int mask);

/* проверяет, является ли указатель на событие валидным и живым */
bool uevent_is_alive(uev_t *uev);

// было ли событие инициализировано (динамические создаются, а статические привязываются assign)
bool uevent_initialized(uev_t *uev);

/* немедленная активация события без соблюдения условий срабатывания */
void uevent_active(uev_t *uev);

/* тоже самое, что uevent_add, но не нужно передавать таймаут, используется текущий таймаут из события */
void uevent_add_with_current_timeout(uev_t *uev);

/* Запускает цикл обработки событий. Возвращает 0 при нормальном завершении, -1 при ошибке. */
int uevent_base_dispatch(uevent_base_t *base);

/* Прерывает цикл обработки событий. */
void uevent_base_loopbreak(uevent_base_t *base);

/* Создаёт новое событие или назначает существующее. Если ev == NULL, создаётся динамическое событие. Возвращает указатель на событие или NULL при ошибке. */
uev_t *uevent_create_or_assign_event(uevent_t *ev, uevent_base_t *base, int fd, short events, uevent_cb_t cb, void *arg, const char *name);

// ОСТАЛЬНЫЕ ФУНКЦИИ БИБЛИОТЕКИ

/* Создаёт новую базу событий. Возвращает указатель на uevent_base или NULL при ошибке. */
uevent_base_t *uevent_base_new(int max_events);

/* Создаёт новую базу событий вместе с пулом воркеров для обработки колбеков */
uevent_base_t *uevent_base_new_with_workers(int max_events, int num_workers);

/* получает текущее монотонное время в мс */
uint64_t get_current_time_ms(void);

uev_t *uevent_try_ref(uev_t *uev); // atomic
bool uevent_put(uev_t *uev);       // atomic

typedef struct {
  void *(*malloc_fn)(size_t);
  void (*free_fn)(void *);
  void (*log_fn)(const char *, ...);
} uevent_mod_init_args_t;

void uevent_mod_init(const uevent_mod_init_args_t *args);

#endif /* LIBUEVENT_UEVENT_H */
