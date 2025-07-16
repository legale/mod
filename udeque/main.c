#include "udeque.h"
#include <stdlib.h>
#include <stdio.h>

int main(void) {
  deq_t *dq = deque_create();
  if (!dq) {
    printf("failed to create deque\n");
    return 1;
  }
  int a = 1, b = 2;
  deq_push_head(dq, &a);
  deq_push_tail(dq, &b);
  deq_entry_t *e = deq_pop_head(dq);
  printf("pop head: %d\n", *(int *)e->data);
  free(e);
  e = deq_pop_tail(dq);
  printf("pop tail: %d\n", *(int *)e->data);
  free(e);
  deq_free(dq);
  return 0;
}
