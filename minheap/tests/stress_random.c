#define _POSIX_C_SOURCE 200809L
#include "minheap.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct thr_args {
  minheap_t *h;
  unsigned int seed;
};

static void *worker(void *arg)
{
  struct thr_args *a = arg;
  minheap_node_t nodes[64];
  int used[64] = {0};
  for (int i = 0; i < 2000; i++) {
    int op = rand_r(&a->seed) % 3;
    int j = rand_r(&a->seed) % 64;
    if (op == 0) {
      nodes[j].key = rand_r(&a->seed);
      mh_insert(a->h, &nodes[j]);
      used[j] = 1;
    } else if (op == 1) {
      if (used[j]) {
        mh_delete_node(a->h, &nodes[j]);
        used[j] = 0;
      }
    } else {
      mh_extract_min(a->h);
    }
    struct timespec ts = {0, 50000};
    nanosleep(&ts, NULL);
  }
  return NULL;
}

int test_stress_random(void)
{
  minheap_t *h = mh_create(1024);
  if (!h) return 1;
  pthread_t th[4];
  struct thr_args args[4];
  for (int i = 0; i < 4; i++) {
    args[i].h = h;
    args[i].seed = (unsigned)time(NULL) + i;
    pthread_create(&th[i], NULL, worker, &args[i]);
  }
  for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
  int ok = 1;
  uint64_t last = 0;
  minheap_node_t *n = mh_extract_min(h);
  if (n) last = n->key;
  while (n) {
    n = mh_extract_min(h);
    if (!n) break;
    if (n->key < last) { ok = 0; break; }
    last = n->key;
  }
  mh_free(h);
  return ok ? 0 : 1;
}
