#include "minheap.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  minheap_node_t heap_node; // узел минимальной кучи - контейнер для хранения ключа кучи
  char name[16];
  int some_data;
} my_struct;

int main(void) {
  minheap_t *heap;
  my_struct items[5] = {
      {.heap_node = {.key = 5}, .name = "five", .some_data = 50},
      {.heap_node = {.key = 3}, .name = "three", .some_data = 30},
      {.heap_node = {.key = 9}, .name = "nine", .some_data = 90},
      {.heap_node = {.key = 2}, .name = "two", .some_data = 20},
      {.heap_node = {.key = 8}, .name = "eight", .some_data = 80}

  };

  heap = mh_create(10);
  if (!heap) {
    printf("failed to create min-heap\n");
    return 1;
  }

  /* Вставляем структуры в кучу */
  for (int i = 0; i < 5; ++i) {
    mh_insert(heap, &items[i].heap_node);
  }

  /* Печатаем минимальный элемент */
  minheap_node_t *min_node = mh_get_min(heap);
  if (min_node) {
    my_struct *min_item = (my_struct *)container_of(min_node, my_struct, heap_node);
    printf("minimum: key=%" PRIu64 ", name=%s, some_data=%d\n",
           min_node->key, min_item->name, min_item->some_data);
  } else {
    printf("heap is empty\n");
  }

  /* Извлекаем и печатаем все элементы по возрастанию ключа */
  printf("extracting in order:\n");
  while (!mh_is_empty(heap)) {
    min_node = mh_extract_min(heap);
    if (min_node) {
      my_struct *item = (my_struct *)container_of(min_node, my_struct, heap_node);
      printf("key=%" PRIu64 ", name=%s, some_data=%d\n",
             min_node->key, item->name, item->some_data);
    }
  }

  mh_free(heap);
  return 0;
}
