#include "minheap.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

struct minheap_t {
  unsigned int cap;
  unsigned int sz;
  minheap_node_t **arr;
  pthread_mutex_t mtx;
};

static void sift_up(minheap_t *h, unsigned int i)
{
  while (i > 0) {
    unsigned int p = (i - 1) / 2;
    if (h->arr[p]->key <= h->arr[i]->key) break;
    minheap_node_t *tmp = h->arr[i];
    h->arr[i] = h->arr[p];
    h->arr[i]->idx = i;
    h->arr[p] = tmp;
    tmp->idx = p;
    i = p;
  }
}

static void sift_down(minheap_t *h, unsigned int i)
{
  while (1) {
    unsigned int l = 2 * i + 1;
    unsigned int r = l + 1;
    if (l >= h->sz) break;
    unsigned int m = l;
    if (r < h->sz && h->arr[r]->key < h->arr[l]->key) m = r;
    if (h->arr[i]->key <= h->arr[m]->key) break;
    minheap_node_t *tmp = h->arr[i];
    h->arr[i] = h->arr[m];
    h->arr[i]->idx = i;
    h->arr[m] = tmp;
    tmp->idx = m;
    i = m;
  }
}

minheap_t *mh_create(unsigned int cap)
{
  if (cap == 0) return NULL;
  minheap_t *h = calloc(1, sizeof(*h));
  if (!h) return NULL;
  h->arr = calloc(cap, sizeof(h->arr[0]));
  if (!h->arr) {
    free(h);
    return NULL;
  }
  h->cap = cap;
  h->sz = 0;
  pthread_mutex_init(&h->mtx, NULL);
  return h;
}

void mh_free(minheap_t *h)
{
  if (!h) return;
  pthread_mutex_destroy(&h->mtx);
  free(h->arr);
  free(h);
}

int mh_insert(minheap_t *h, minheap_node_t *node)
{
  if (!h || !node) return -1;
  pthread_mutex_lock(&h->mtx);
  if (h->sz >= h->cap) {
    pthread_mutex_unlock(&h->mtx);
    return -1;
  }
  if (node->idx < h->sz && h->arr[node->idx] == node) {
    sift_up(h, node->idx);
    sift_down(h, node->idx);
    pthread_mutex_unlock(&h->mtx);
    return 0;
  }
  unsigned int i = h->sz++;
  h->arr[i] = node;
  node->idx = i;
  sift_up(h, i);
  pthread_mutex_unlock(&h->mtx);
  return 0;
}

void mh_delete_node(minheap_t *h, minheap_node_t *node)
{
  if (!h || !node) return;
  pthread_mutex_lock(&h->mtx);
  if (node->idx >= h->sz || h->arr[node->idx] != node) {
    pthread_mutex_unlock(&h->mtx);
    return;
  }
  unsigned int i = node->idx;
  h->sz--;
  if (i != h->sz) {
    h->arr[i] = h->arr[h->sz];
    h->arr[i]->idx = i;
    sift_up(h, i);
    sift_down(h, i);
  }
  h->arr[h->sz] = NULL;
  pthread_mutex_unlock(&h->mtx);
}

minheap_node_t *mh_extract_min(minheap_t *h)
{
  if (!h) return NULL;
  pthread_mutex_lock(&h->mtx);
  if (h->sz == 0) {
    pthread_mutex_unlock(&h->mtx);
    return NULL;
  }
  minheap_node_t *min = h->arr[0];
  h->sz--;
  if (h->sz > 0) {
    h->arr[0] = h->arr[h->sz];
    h->arr[0]->idx = 0;
    h->arr[h->sz] = NULL;
    sift_down(h, 0);
  } else {
    h->arr[0] = NULL;
  }
  pthread_mutex_unlock(&h->mtx);
  return min;
}

minheap_node_t *mh_get_min(minheap_t *h)
{
  if (!h) return NULL;
  pthread_mutex_lock(&h->mtx);
  minheap_node_t *n = h->sz ? h->arr[0] : NULL;
  pthread_mutex_unlock(&h->mtx);
  return n;
}

minheap_node_t *mh_get_node(minheap_t *h, int idx)
{
  if (!h || idx < 0) return NULL;
  pthread_mutex_lock(&h->mtx);
  minheap_node_t *n = (unsigned int)idx < h->sz ? h->arr[idx] : NULL;
  pthread_mutex_unlock(&h->mtx);
  return n;
}

bool mh_is_empty(minheap_t *h)
{
  if (!h) return true;
  pthread_mutex_lock(&h->mtx);
  bool e = h->sz == 0;
  pthread_mutex_unlock(&h->mtx);
  return e;
}

unsigned int mh_get_size(minheap_t *h)
{
  if (!h) return 0;
  pthread_mutex_lock(&h->mtx);
  unsigned int s = h->sz;
  pthread_mutex_unlock(&h->mtx);
  return s;
}
