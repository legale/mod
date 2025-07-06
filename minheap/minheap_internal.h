#ifndef MINHEAP_INTERNAL_H
#define MINHEAP_INTERNAL_H

#include "minheap.h"

struct minheap_t {
  minheap_node_t **arr;
  unsigned int size;
  unsigned int capacity;
};

// Декларации внутренних функций, которые теперь не static
// и могут быть использованы в тестах.
void mh_map_set(minheap_t *minheap, minheap_node_t *node, int idx);
int mh_map_get(minheap_t *minheap, minheap_node_t *node);
void mh_map_del(minheap_t *minheap, minheap_node_t *node);

#endif // MINHEAP_INTERNAL_H
