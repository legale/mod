#ifndef LIBMINHEAP_MINHEAP_H
#define LIBMINHEAP_MINHEAP_H

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif //_POSIX_SOURCE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>


#ifndef EXPORT_API
#define EXPORT_API __attribute__((visibility("default")))
#endif


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

EXPORT_API minheap_t *mh_create(unsigned int capacity);
EXPORT_API void mh_free(minheap_t *minheap);

/**
 * Вставляет node в кучу. Если node уже есть — обновляет key и перестраивает
 * кучу.
 */
EXPORT_API int mh_insert(minheap_t *minheap, minheap_node_t *node);

/**
 * Удаляет node из кучи (если он там есть).
 */
EXPORT_API void mh_delete_node(minheap_t *minheap, minheap_node_t *node);

/**
 * Извлекает и возвращает минимальный элемент (или NULL, если куча пуста).
 */
EXPORT_API minheap_node_t *mh_extract_min(minheap_t *minheap);

/**
 * Возвращает минимальный элемент (или NULL, если куча пуста), не удаляя его.
 */
EXPORT_API minheap_node_t *mh_get_min(minheap_t *minheap);

// возвращает указатель на узел по индексу в куче
EXPORT_API minheap_node_t *mh_get_node(minheap_t *minheap, int idx);

/**
 * true, если куча пуста.
 */
EXPORT_API bool mh_is_empty(minheap_t *minheap);

/**
 * Текущее количество элементов в куче.
 */
EXPORT_API unsigned int mh_get_size(minheap_t *minheap);

typedef struct {
  void (*log)(int, const char *, ...);
  int (*get_time)(struct timespec *);
} minheap_mod_init_args_t;


#endif /* LIBMINHEAP_MINHEAP_H */
