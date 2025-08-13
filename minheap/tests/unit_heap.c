#include "minheap.h"

#include <stdlib.h>

int test_create_free(void)
{
  minheap_t *h = mh_create(4);
  if (!h) return 1;
  mh_free(h);
  mh_free(NULL);
  return 0;
}

int test_insert_extract(void)
{
  minheap_t *h = mh_create(4);
  if (!h) return 1;
  minheap_node_t a = {.key = 5};
  minheap_node_t b = {.key = 3};
  if (mh_insert(h, &a) || mh_insert(h, &b)) {
    mh_free(h);
    return 1;
  }
  if (mh_get_size(h) != 2) {
    mh_free(h);
    return 1;
  }
  minheap_node_t *n = mh_extract_min(h);
  if (n != &b) {
    mh_free(h);
    return 1;
  }
  n = mh_extract_min(h);
  if (n != &a) {
    mh_free(h);
    return 1;
  }
  mh_free(h);
  return 0;
}

int test_insert_overflow(void)
{
  minheap_t *h = mh_create(1);
  if (!h) return 1;
  minheap_node_t a = {.key = 1};
  minheap_node_t b = {.key = 2};
  int r1 = mh_insert(h, &a);
  int r2 = mh_insert(h, &b);
  mh_free(h);
  return !(r1 == 0 && r2 == -1);
}

int test_delete_node(void)
{
  minheap_t *h = mh_create(4);
  if (!h) return 1;
  minheap_node_t a = {.key = 1};
  minheap_node_t b = {.key = 2};
  mh_insert(h, &a);
  mh_insert(h, &b);
  mh_delete_node(h, &a);
  minheap_node_t *n = mh_extract_min(h);
  int ok = (n == &b);
  mh_free(h);
  return ok ? 0 : 1;
}

int test_get_min(void)
{
  minheap_t *h = mh_create(4);
  if (!h) return 1;
  minheap_node_t a = {.key = 1};
  mh_insert(h, &a);
  int ok = mh_get_min(h) == &a;
  mh_free(h);
  return ok ? 0 : 1;
}

int test_get_node(void)
{
  minheap_t *h = mh_create(4);
  if (!h) return 1;
  minheap_node_t a = {.key = 1};
  mh_insert(h, &a);
  int ok = mh_get_node(h, 0) == &a && mh_get_node(h, 1) == NULL;
  mh_free(h);
  return ok ? 0 : 1;
}

int test_is_empty_size(void)
{
  minheap_t *h = mh_create(4);
  if (!h) return 1;
  int ok = mh_is_empty(h);
  minheap_node_t a = {.key = 1};
  mh_insert(h, &a);
  ok = ok && !mh_is_empty(h) && mh_get_size(h) == 1;
  mh_extract_min(h);
  ok = ok && mh_is_empty(h) && mh_get_size(h) == 0;
  mh_free(h);
  return ok ? 0 : 1;
}
