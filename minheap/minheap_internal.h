#ifndef MINHEAP_INTERNAL_H
#define MINHEAP_INTERNAL_H

#include "minheap.h"

struct minheap_t {
  minheap_node_t **arr;
  unsigned int size;
  unsigned int capacity;
};

// Внутренние функции
// Устанавливает индекс узла (idx + 1 для отличия от 0)
static inline void mh_map_set(minheap_t *minheap, minheap_node_t *node, int idx) {
  if (!node) return;
  (void)minheap; // не используется
  node->idx = (uint16_t)idx + 1;
}

// Получает индекс узла или -1, если узел не в куче
static inline int mh_map_get(minheap_t *minheap, minheap_node_t *node) {
  if (!node) return -1;
  (void)minheap; // не используется
  return node->idx - 1;
}

// Удаляет узел из маппинга, сбрасывая его индекс
static inline void mh_map_del(minheap_t *minheap, minheap_node_t *node) {
  if (!node) return;
  (void)minheap; // не используется
  node->idx = 0;
}

#endif // MINHEAP_INTERNAL_H
