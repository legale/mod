#ifndef UDEQUE_H
#define UDEQUE_H

#include <stdbool.h>
#include "../list/list.h"

typedef struct deq {
  struct list_head list;
  int size;
} deq_t;

typedef struct deq_entry {
  struct list_head list;
  void *data;
} deq_entry_t;

deq_t *deque_create(void);
bool deq_isempty(deq_t *deq);
void deq_free(deq_t *deq);

void deq_push_tail(deq_t *deq, void *data);
void deq_push_head(deq_t *deq, void *data);

deq_entry_t *deq_pop_tail(deq_t *deq);
deq_entry_t *deq_pop_head(deq_t *deq);
deq_entry_t *deq_get_head(deq_t *deq);
deq_entry_t *deq_get_tail(deq_t *deq);

#endif // UDEQUE_H
