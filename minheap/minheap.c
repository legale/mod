#include "minheap.h"
#include "minheap_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef TESTRUN
// Переопределение malloc для тестирования
malloc_func_t malloc_orig = malloc;
malloc_func_t malloc_hook = malloc;

#undef malloc
#define malloc malloc_hook

// Функция для имитации сбоя выделения памяти
void *test_malloc_fail(size_t size) {
  return NULL;
}
#endif

// Устанавливает индекс узла (idx + 1 для отличия от 0)
void mh_map_set(minheap_t *minheap, minheap_node_t *node, int idx) {
  if (!node) return;
  (void)minheap; // не используется
  node->idx = (uint16_t)idx + 1;
}

// Получает индекс узла или -1, если узел не в куче
int mh_map_get(minheap_t *minheap, minheap_node_t *node) {
  if (!node) return -1;
  (void)minheap; // не используется
  return node->idx - 1;
}

// Удаляет узел из маппинга, сбрасывая его индекс
void mh_map_del(minheap_t *minheap, minheap_node_t *node) {
  if (!node) return;
  (void)minheap; // не используется
  node->idx = 0;
}

// Создаёт кучу с заданной вместимостью
minheap_t *mh_create(unsigned int capacity) {
  if (capacity == 0) return NULL;

  minheap_t *minheap = (minheap_t *)malloc(sizeof(*minheap));
  if (!minheap) return NULL;

  minheap->capacity = capacity;
  minheap->size = 0;
  minheap->arr = (minheap_node_t **)malloc(capacity * sizeof(minheap->arr[0]));
  if (!minheap->arr) {
    free(minheap);
    return NULL;
  }

  // Инициализация массива NULL для отладки
  for (unsigned int i = 0; i < capacity; i++) {
    minheap->arr[i] = NULL;
  }
  return minheap;
}

// Освобождает память кучи (не освобождает узлы)
void mh_free(minheap_t *minheap) {
  if (!minheap) return;
  free(minheap->arr);
  free(minheap);
}

static int mh_parent(int i) { return (i - 1) / 4; }
static int mh_left_child(int i) { return 4 * i + 1; }
//следующие дети идут по порядку за первым

void mh_sift_up(minheap_t *minheap, int idx) {
  if (!minheap || idx < 0 || idx >= (int)minheap->size) return;
  minheap_node_t **heap = minheap->arr;
  minheap_node_t *new_node = heap[idx];
  int hole = idx;                     // Позиция дырки — переданный индекс
  uint64_t new_value = new_node->key; // Значение нового узла
  int parent;

  // Просеивание вверх
  while (hole > 0) {
    parent = mh_parent(hole); // Индекс родителя
    if (new_value < heap[parent]->key) {
      // Перемещаем родителя вниз в дырку
      heap[hole] = heap[parent];
      mh_map_set(minheap, heap[hole], hole); // Обновляем индекс родителя
      hole = parent;                         // Дырка перемещается вверх
    } else {
      break; // Место для нового узла найдено
    }
  }

  // Вставляем новый узел в финальную позицию дырки
  heap[hole] = new_node;
  mh_map_set(minheap, heap[hole], hole); // Обновляем индекс нового узла
}

void mh_sift_down(minheap_t *minheap, int idx) {
  if (!minheap || idx < 0 || idx >= (int)minheap->size) return;
  minheap_node_t **heap = minheap->arr;
  minheap_node_t *node = heap[idx]; // Сохраняем узел для просеивания
  int hole = idx;                   // Позиция дырки — переданный индекс
  uint64_t val = node->key;         // Значение узла
  int mc;                           // Минимальный ребёнок
  int sz = minheap->size;

  // Просеивание вниз
  while (mh_left_child(hole) < sz) {
    mc = mh_left_child(hole);

    if (mc + 1 < sz && heap[mc + 1]->key < heap[mc]->key) {
      mc += 1;
    }

    if (mc + 2 < sz && heap[mc + 2]->key < heap[mc]->key) {
      mc += 2;
    }

    if (mc + 3 < sz && heap[mc + 3]->key < heap[mc]->key) {
      mc += 3;
    }

    if (heap[mc]->key < val) {
      heap[hole] = heap[mc];
      mh_map_set(minheap, heap[hole], hole);
      hole = mc;
    } else {
      break;
    }
  }

  // Вставляем сохранённый узел в финальную позицию
  heap[hole] = node;
  mh_map_set(minheap, heap[hole], hole);
}

// Вставляет новый узел или обновляет существующий
int mh_insert(minheap_t *minheap, minheap_node_t *node) {
  if (!minheap || !node) {
    return -1; // Ошибка: NULL-указатель
  }
  minheap_node_t **heap = minheap->arr;

  // Проверяем, есть ли узел уже в куче
  int idx = mh_map_get(minheap, node);
  if (idx >= 0) {
    // Узел уже в куче, его key мог измениться
    mh_sift_up(minheap, idx);   // Просеиваем вверх, если key уменьшился
    mh_sift_down(minheap, idx); // Просеиваем вниз, если key увеличился
    return 0;                   // Успех: узел обновлён
  }

  // Проверяем, есть ли место в куче
  if (minheap->size >= minheap->capacity) {
    return -1; // Ошибка: куча заполнена
  }

  // Вставляем новый узел в конец кучи
  heap[minheap->size] = node;
  // Обновляем индекс узла
  mh_map_set(minheap, node, minheap->size);
  // Увеличиваем размер кучи
  minheap->size++;
  // Просеиваем новый узел вверх
  mh_sift_up(minheap, minheap->size - 1);
  return 0; // Успех: узел вставлен
}

minheap_node_t *mh_extract_min(minheap_t *minheap) {
  if (!minheap || minheap->size == 0) {
    return NULL;
  }
  minheap_node_t **heap = minheap->arr;

  // Сохраняем минимальный узел
  minheap_node_t *min = heap[0];
  // Удаляем узел из маппинга
  mh_map_del(minheap, min);

  // Уменьшаем размер кучи
  minheap->size--;

  // Если куча не опустела, перемещаем последний элемент в корень
  // и восстанавливаем свойство кучи.
  if (minheap->size > 0) {
    heap[0] = heap[minheap->size];
    heap[minheap->size] = NULL; // Очищаем старую позицию
    mh_map_set(minheap, heap[0], 0);
    mh_sift_down(minheap, 0);
  } else {
    // Если куча опустела, просто очищаем корень
    heap[0] = NULL;
  }

  return min;
}

minheap_node_t *mh_get_node(minheap_t *minheap, int idx) {
  if (!minheap || idx < 0 || idx >= (int)minheap->size) {
    return NULL;
  }
  return minheap->arr[idx];
}

minheap_node_t *mh_get_min(minheap_t *minheap) {
  if (!minheap || minheap->size == 0) {
    return NULL;
  }
  return minheap->arr[0];
}

bool mh_is_empty(minheap_t *minheap) {
  return !minheap || minheap->size == 0;
}

unsigned int mh_get_size(minheap_t *minheap) {
  return minheap ? minheap->size : 0;
}

void mh_delete_node(minheap_t *minheap, minheap_node_t *node) {
  if (!minheap || minheap->size == 0 || !node) {
    return;
  }
  minheap_node_t **heap = minheap->arr;
  int i = mh_map_get(minheap, node);
  if (i < 0) {
    return;
  }
  // Удаляем узел из маппинга
  mh_map_del(minheap, node);
  // Если узел последний, просто уменьшаем размер
  if (i == (int)minheap->size - 1) {
    minheap->size--;
    heap[minheap->size] = NULL;
    return;
  }
  // Заменяем узел последним элементом
  heap[i] = heap[minheap->size - 1];
  // Очищаем последний элемент
  heap[minheap->size - 1] = NULL;
  // Обновляем индекс нового узла
  mh_map_set(minheap, heap[i], i);
  // Уменьшаем размер кучи
  minheap->size--;
  // Восстанавливаем свойство кучи
  mh_sift_up(minheap, i);   // Просеиваем вверх, если key уменьшился
  mh_sift_down(minheap, i); // Просеиваем вниз, если key увеличился
}