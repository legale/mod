#include "htable.h"
#include "../list.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Хеш-функция по ключу
static inline unsigned int htable_hash(uintptr_t key, size_t capacity) {
  return key % capacity;
}

// Найти узел в хэш-таблице по ключу. Внутренняя функция.
static htable_node_t *htable_find_node(htable_t *ht, uintptr_t key) {
  if (!ht) {
    return NULL;
  }
  unsigned int bucket = htable_hash(key, ht->capacity);
  htable_node_t *entry;
  hlist_for_each_entry(entry, &ht->buckets[bucket], hnode) {
    if (entry->key == key) {
      return entry;
    }
  }
  return NULL;
}

// Создать новую хэш-таблицу
htable_t *htable_create(size_t capacity) {
  if (capacity == 0) {
    return NULL;
  }

  htable_t *ht = (htable_t *)malloc(sizeof(*ht));
  if (!ht) {
    return NULL;
  }

  ht->capacity = capacity;
  ht->buckets =
      (struct hlist_head *)malloc(ht->capacity * sizeof(struct hlist_head));
  if (!ht->buckets) {
    free(ht);
    return NULL;
  }

  for (size_t i = 0; i < ht->capacity; ++i) {
    ht->buckets[i].first = NULL;
  }

  return ht;
}

// Освободить память, занимаемую хэш-таблицей
void htable_free(htable_t *ht) {
  if (!ht) {
    return;
  }

  for (size_t i = 0; i < ht->capacity; ++i) {
    struct hlist_node *pos, *n;
    hlist_for_each_safe(pos, n, &ht->buckets[i]) {
      htable_node_t *entry = hlist_entry(pos, htable_node_t, hnode);
      free(entry);
    }
  }

  free(ht->buckets);
  free(ht);
}

// Добавить/обновить пару ключ-значение
void htable_set(htable_t *ht, uintptr_t key, void *value) {
  if (!ht) {
    return;
  }

  htable_node_t *entry = htable_find_node(ht, key);
  if (entry) {
    entry->value = value;
    return;
  }

  entry = (htable_node_t *)malloc(sizeof(*entry));
  if (!entry) {
    return;
  }
  entry->key = key;
  entry->value = value;

  unsigned int bucket = htable_hash(key, ht->capacity);
  hlist_add_head(&entry->hnode, &ht->buckets[bucket]);
}

// Получить значение по ключу
void *htable_get(htable_t *ht, uintptr_t key) {
  htable_node_t *entry = htable_find_node(ht, key);
  return entry ? entry->value : NULL;
}

// Удалить пару ключ-значение по ключу
void htable_del(htable_t *ht, uintptr_t key) {
  if (!ht) {
    return;
  }

  htable_node_t *entry = htable_find_node(ht, key);
  if (entry) {
    hlist_del(&entry->hnode);
    free(entry);
  }
}
