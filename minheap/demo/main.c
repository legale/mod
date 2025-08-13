#include "minheap.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
  minheap_t *h = mh_create(8);
  if (!h) return 1;
  minheap_node_t nodes[4];
  for (int i = 0; i < 4; i++) {
    nodes[i].key = (uint64_t)(4 - i);
    mh_insert(h, &nodes[i]);
  }
  while (!mh_is_empty(h)) {
    minheap_node_t *n = mh_extract_min(h);
    printf("%lu\n", (unsigned long)n->key);
  }
  mh_free(h);
  return 0;
}
