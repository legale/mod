#ifndef MINHEAP_MINHEAP_H
#define MINHEAP_MINHEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef container_of
#define container_of(ptr, type, member)                \
  ({                                                   \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })
#endif

typedef struct minheap_node {
  uint64_t key;
  uint16_t idx;
} minheap_node_t;

typedef struct minheap_t minheap_t;

minheap_t *mh_create(unsigned int capacity);
void mh_free(minheap_t *mh);
int mh_insert(minheap_t *mh, minheap_node_t *node);
void mh_delete_node(minheap_t *mh, minheap_node_t *node);
minheap_node_t *mh_extract_min(minheap_t *mh);
minheap_node_t *mh_get_min(minheap_t *mh);
minheap_node_t *mh_get_node(minheap_t *mh, int idx);
bool mh_is_empty(minheap_t *mh);
unsigned int mh_get_size(minheap_t *mh);

#ifdef __cplusplus
}
#endif

#endif // MINHEAP_MINHEAP_H
