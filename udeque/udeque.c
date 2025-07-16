#include "udeque.h"
#include <stdlib.h>

deq_t *deque_create(void) {
  deq_t *deq = malloc(sizeof(deq_t));
  if (!deq) return NULL;
  INIT_LIST_HEAD(&deq->list);
  deq->size = 0;
  return deq;
}

static void _deq_push(deq_t *deq, bool push_tail, void *data) {
  deq_entry_t *item = malloc(sizeof(deq_entry_t));
  if (!item) return;
  item->data = data;
  if (push_tail)
    list_add_tail(&item->list, &deq->list);
  else
    list_add(&item->list, &deq->list);
  deq->size++;
}

void deq_push_tail(deq_t *deq, void *data) { _deq_push(deq, true, data); }
void deq_push_head(deq_t *deq, void *data) { _deq_push(deq, false, data); }

static deq_entry_t *_deq_pop(deq_t *deq, bool from_tail) {
  if (deq_isempty(deq)) return NULL;
  deq_entry_t *item;
  if (from_tail)
    item = list_last_entry(&deq->list, deq_entry_t, list);
  else
    item = list_first_entry(&deq->list, deq_entry_t, list);
  list_del(&item->list);
  deq->size--;
  return item;
}

deq_entry_t *deq_pop_tail(deq_t *deq) { return _deq_pop(deq, true); }
deq_entry_t *deq_pop_head(deq_t *deq) { return _deq_pop(deq, false); }

static deq_entry_t *_deq_get(deq_t *deq, bool from_tail) {
  if (deq_isempty(deq)) return NULL;
  if (from_tail)
    return list_last_entry(&deq->list, deq_entry_t, list);
  return list_first_entry(&deq->list, deq_entry_t, list);
}

deq_entry_t *deq_get_head(deq_t *deq) { return _deq_get(deq, false); }
deq_entry_t *deq_get_tail(deq_t *deq) { return _deq_get(deq, true); }

bool deq_isempty(deq_t *deq) { return deq->size == 0; }

void deq_free(deq_t *deq) {
  deq_entry_t *cur, *tmp;
  list_for_each_entry_safe(cur, tmp, &deq->list, list) { free(cur); }
  free(deq);
}
