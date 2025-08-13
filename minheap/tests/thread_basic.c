#define _POSIX_C_SOURCE 200809L
#include "minheap.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

struct prod_args {
  minheap_t *h;
  minheap_node_t *nodes;
  int n;
};

struct cons_args {
  minheap_t *h;
  bool *seen;
  int n;
};

static void *producer(void *arg)
{
  struct prod_args *a = arg;
  for (int i = 0; i < a->n; i++) {
    a->nodes[i].key = (uint64_t)i;
    mh_insert(a->h, &a->nodes[i]);
    struct timespec ts = {0, 100000};
    nanosleep(&ts, NULL);
  }
  return NULL;
}

static void *consumer(void *arg)
{
  struct cons_args *a = arg;
  int c = 0;
  while (c < a->n) {
    minheap_node_t *n = mh_extract_min(a->h);
    if (!n) {
      sched_yield();
      continue;
    }
    if (n->key < (unsigned)a->n) a->seen[n->key] = true;
    c++;
  }
  return NULL;
}

int test_thread_push_pop(void)
{
  const int n = 1000;
  minheap_t *h = mh_create(n);
  if (!h) return 1;
  minheap_node_t *nodes = calloc(n, sizeof(*nodes));
  bool *seen = calloc(n, sizeof(bool));
  struct prod_args pa = {h, nodes, n};
  struct cons_args ca = {h, seen, n};
  pthread_t pt, ct;
  pthread_create(&pt, NULL, producer, &pa);
  pthread_create(&ct, NULL, consumer, &ca);
  pthread_join(pt, NULL);
  pthread_join(ct, NULL);
  int ok = 1;
  for (int i = 0; i < n; i++) {
    if (!seen[i]) { ok = 0; break; }
  }
  free(seen);
  free(nodes);
  mh_free(h);
  return ok ? 0 : 1;
}
