#ifndef LIBMINHEAP_MINHEAP_H
#define LIBMINHEAP_MINHEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// --- Нужные функции и переменные для тестов ---
typedef void *(*malloc_func_t)(size_t);

void *test_malloc_fail(size_t size);

// Установить кастомный malloc (например, fail_malloc)
#define SET_MALLOC(fn)                                \
  do {                                                \
    malloc_hook = fn == NULL ? test_malloc_fail : fn; \
  } while (0)

// Вернуть оригинальный malloc
#define UNSET_MALLOC           \
  do {                         \
    malloc_hook = malloc_orig; \
  } while (0)

#ifndef container_of
#define container_of(ptr, type, member)                \
  ({                                                   \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })
#endif

typedef struct minheap_node {
  uint64_t key; /* Ключ для сортировки внутри кучи (например, время таймера) */
  uint16_t idx;
} minheap_node_t;

// Непрозрачный тип для сокрытия реализации
typedef struct minheap_t minheap_t;

minheap_t *mh_create(unsigned int capacity);
void mh_free(minheap_t *minheap);

/**
 * Вставляет node в кучу. Если node уже есть — обновляет key и перестраивает
 * кучу.
 */
int mh_insert(minheap_t *minheap, minheap_node_t *node);

/**
 * Удаляет node из кучи (если он там есть).
 */
void mh_delete_node(minheap_t *minheap, minheap_node_t *node);

/**
 * Извлекает и возвращает минимальный элемент (или NULL, если куча пуста).
 */
minheap_node_t *mh_extract_min(minheap_t *minheap);

/**
 * Возвращает минимальный элемент (или NULL, если куча пуста), не удаляя его.
 */
minheap_node_t *mh_get_min(minheap_t *minheap);

// возвращает указатель на узел по индексу в куче
minheap_node_t *mh_get_node(minheap_t *minheap, int idx);

/**
 * true, если куча пуста.
 */
bool mh_is_empty(minheap_t *minheap);

/**
 * Текущее количество элементов в куче.
 */
unsigned int mh_get_size(minheap_t *minheap);

typedef struct {
  void (*log)(int, const char *, ...);
  int (*get_time)(struct timespec *);
} minheap_mod_init_args_t;

int minheap_mod_init(const minheap_mod_init_args_t *args);

#endif /* LIBMINHEAP_MINHEAP_H */
