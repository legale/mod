#include "../syslog2/syslog2.h"
#include "../test_util.h"
#include "../timeutil/timeutil.h"

#include "heap-inl.h"
#include "minheap.h"
#include "minheap_internal.h" // для доступа к map в тестах

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h> // gettimeofday
#include <time.h>

#define ASSERT_EQ_UINT64(actual, expected, msg)                                                                          \
  do {                                                                                                                   \
    if ((actual) != (expected)) {                                                                                        \
      PRINT_TEST_INFO("FAIL: %s: got=%" PRIu64 ", expected=%" PRIu64 "", msg, (uint64_t)(actual), (uint64_t)(expected)); \
      abort();                                                                                                           \
    }                                                                                                                    \
  } while (0)

// --- Вспомогательная структура для тестов ---
typedef struct {
  minheap_node_t heap_node;
  int value;
} heap_value_t;

static uint64_t tu_get_current_time_ms() {
  struct timespec ts;
  int ret = clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  if (ret != 0) {
    syslog2(LOG_INFO, "clock_gettime_fast failed: ret=%d", ret);
    return 0U;
  }
  return (uint64_t)ts.tv_sec * MS_PER_SEC + (uint64_t)(ts.tv_nsec / NS_PER_MS);
}

// --- Юнит-тесты ---

/**
 * Принцип 1, 4: Изолированный тест на создание и освобождение кучи.
 * Покрывает позитивный и негативный сценарии.
 */
void test_create_and_free() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Create with valid capacity and free");
  minheap_t *heap = mh_create(10);
  assert(heap != NULL && "minheap_create return not NULL expected");
  assert(heap->capacity == 10 && "Capacity should be set correctly");
  mh_free(heap);
  PRINT_TEST_PASSED();

  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Create with zero capacity (negative case)");
  heap = mh_create(0);
  assert(heap == NULL && "minheap_create should fail for zero capacity");
  PRINT_TEST_PASSED();

  reset_alloc_counters();
  fail_malloc_at = 0;
}

/**
 * Принцип 1, 4: Изолированные негативные тесты для minheap.
 * Покрывают все проверки на NULL, переполнение, ошибки аллокации и удаление несуществующего элемента.
 */

void test_insert_null_heap() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Insert into NULL heap (negative case)");
  minheap_node_t node = {.key = 42};
  mh_insert(NULL, &node);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_create_malloc_fail() {
  reset_alloc_counters();
  fail_malloc_at = 1;
  PRINT_TEST_START("Create heap with malloc failure (negative case)");
  minheap_t *heap = mh_create(10);
  assert(heap == NULL && "mh_create should fail if malloc fails on first call");
  reset_alloc_counters();
  fail_malloc_at = 0;

  PRINT_TEST_INFO("test if malloc fail on second call");
  reset_alloc_counters();
  fail_malloc_at = 2;
  heap = mh_create(10);
  assert(heap == NULL && "mh_create should fail if malloc fails on second call");
  reset_alloc_counters();
  fail_malloc_at = 0;

  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_insert_null_node() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Insert NULL node (negative case)");
  minheap_t *heap = mh_create(4);
  mh_insert(heap, NULL);
  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_insert_overflow() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Insert overflow (negative case)");
  minheap_t *heap = mh_create(1);
  minheap_node_t node1 = {.key = 1};
  minheap_node_t node2 = {.key = 2};
  mh_insert(heap, &node1);
  mh_insert(heap, &node2); // должен игнорироваться
  assert(heap->size == 1 && "Heap should not overflow");
  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_free_null() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Free NULL heap (negative case)");
  mh_free(NULL);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_extract_min_null() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Extract min from NULL heap (negative case)");
  minheap_node_t *min = mh_extract_min(NULL);
  assert(min == NULL && "Should return NULL for NULL heap");
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_get_min_null() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Get min from NULL heap (negative case)");
  minheap_node_t *min = mh_get_min(NULL);
  assert(min == NULL && "Should return NULL for NULL heap");
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_get_min_empty() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Get min from empty heap (negative case)");
  minheap_t *heap = mh_create(4);
  minheap_node_t *min = mh_get_min(heap);
  assert(min == NULL && "Should return NULL for empty heap");
  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_extract_min_empty() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Extract min from empty heap (negative case)");
  minheap_t *heap = mh_create(4);
  minheap_node_t *min = mh_extract_min(heap);
  assert(min == NULL && "Should return NULL for empty heap");
  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_is_empty_null() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Is empty on NULL heap (negative case)");
  assert(mh_is_empty(NULL) == true && "NULL heap is empty");
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_get_size_null() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Get size on NULL heap (negative case)");
  assert(mh_get_size(NULL) == 0 && "NULL heap size is 0");
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_delete_value_null_heap() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Delete value from NULL heap (negative case)");
  minheap_node_t node = {.key = 42};
  mh_delete_node(NULL, &node);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_delete_value_null_node() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Delete NULL node (negative case)");
  minheap_t *heap = mh_create(4);
  mh_delete_node(heap, NULL);
  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_delete_value_not_in_heap() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Delete node not in heap (negative case)");
  minheap_t *heap = mh_create(4);
  minheap_node_t node = {.key = 42};
  mh_delete_node(heap, &node); // node не в куче
  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

/**
 * Принцип 1, 3, 7: Простой тест на вставку и получение минимального элемента.
 */
void test_insert_and_get_min() {
  reset_alloc_counters();
  fail_malloc_at = 0;
  PRINT_TEST_START("Insert items and get minimum");
  minheap_t *heap = mh_create(5);
  heap_value_t v1 = {.heap_node = {.key = 50}, .value = 50};
  heap_value_t v2 = {.heap_node = {.key = 20}, .value = 20};
  PRINT_TEST_INFO("insert");
  mh_insert(heap, &v1.heap_node);

  PRINT_TEST_INFO("minheap_get_size");
  assert(mh_get_size(heap) == 1 && "Size should be 1 after first insert");
  PRINT_TEST_INFO("minheap_get_min");
  assert(mh_get_min(heap)->key == 50 && "Min key should be 50");

  PRINT_TEST_INFO("insert");
  mh_insert(heap, &v2.heap_node);
  PRINT_TEST_INFO("minheap_get_size");
  assert(mh_get_size(heap) == 2 && "Size should be 2 after second insert");
  PRINT_TEST_INFO("minheap_get_min");
  assert(mh_get_min(heap)->key == 20 && "Min key should be 20 after update");

  mh_free(heap);
  PRINT_TEST_PASSED();
  reset_alloc_counters();
  fail_malloc_at = 0;
}

void test_minheap_get_node() {
  PRINT_TEST_START("minheap_get_node coverage");

  // 1. heap == NULL
  assert(mh_get_node(NULL, 0) == NULL);

  // 2. idx < 0
  minheap_t *heap = mh_create(4);
  assert(heap);
  assert(mh_get_node(heap, -1) == NULL);

  // 3. idx >= heap->size
  assert(mh_get_node(heap, 0) == NULL); // size == 0

  // 4. корректный случай
  minheap_node_t node = {.key = 123};
  heap->arr[0] = &node;
  heap->size = 1;
  assert(mh_get_node(heap, 0) == &node);

  // 5. idx >= size (size == 1, idx == 1)
  assert(mh_get_node(heap, 1) == NULL);

  mh_free(heap);

  PRINT_TEST_PASSED();
}

void test_mh_map_functions() {
  PRINT_TEST_START("mh_map_set/get/del basic and edge cases");
  minheap_t dummy;
  minheap_node_t node = {.key = 0, .idx = 0};

  /* normal usage */
  mh_map_set(&dummy, &node, 3);
  assert(node.idx == 4 && "mh_map_set should store idx+1");
  assert(mh_map_get(&dummy, &node) == 3 && "mh_map_get should return index");
  mh_map_del(&dummy, &node);
  assert(node.idx == 0 && "mh_map_del should reset index");
  assert(mh_map_get(&dummy, &node) == -1 && "mh_map_get should return -1 after del");

  /* NULL node */
  PRINT_TEST_INFO("NULL node handling");
  syslog2(LOG_DEBUG, "TEST SYSLOG");

  mh_map_set(&dummy, NULL, 1);
  mh_map_del(&dummy, NULL);
  assert(mh_map_get(&dummy, NULL) == -1 && "mh_map_get NULL node should return -1");

  /* NULL heap pointer */
  PRINT_TEST_INFO("NULL heap pointer handling");
  mh_map_set(NULL, &node, 5);
  assert(node.idx == 6 && "mh_map_set should work with NULL heap");
  assert(mh_map_get(NULL, &node) == 5 && "mh_map_get should work with NULL heap");
  mh_map_del(NULL, &node);
  assert(node.idx == 0 && "mh_map_del should reset index with NULL heap");

  PRINT_TEST_PASSED();
}

/**
 * Принцип 1, 5: Тест на извлечение элементов в правильном порядке.
 */
void test_extract_min_order() {
  PRINT_TEST_START("Extract items in ascending order");
  uint64_t cur = tu_get_current_time_ms();
  heap_value_t values[] = {
      {.heap_node = {.key = cur + 3500}, .value = 1},
      {.heap_node = {.key = cur + 7500}, .value = 2},
      {.heap_node = {.key = cur + 15000}, .value = 3},
      {.heap_node = {.key = cur + 1}, .value = 4},
      {.heap_node = {.key = cur + 12}, .value = 5},
      {.heap_node = {.key = cur + 10}, .value = 6},
      {.heap_node = {.key = cur + 10550}, .value = 7},
      {.heap_node = {.key = cur + 31000}, .value = 8},
      {.heap_node = {.key = cur + 5000}, .value = 9},
      {.heap_node = {.key = cur + 1000}, .value = 10},
      {.heap_node = {.key = cur + 61000}, .value = 11},
      {.heap_node = {.key = cur + 601000}, .value = 12},
  };
  int values_size = (int)(ARRAY_SIZE(values));
  minheap_t *heap = mh_create(values_size);

  for (int i = 0; i < values_size; ++i) {
    uint64_t key = values[i].heap_node.key;
    PRINT_TEST_INFO("insert key=%" PRIu64 "", key);
    mh_insert(heap, &values[i].heap_node);
  }

  uint64_t exp_order[] = {
      cur + 1,
      cur + 10,
      cur + 12,
      cur + 1000,
      cur + 3500,
      cur + 5000,
      cur + 7500,
      cur + 10550,
      cur + 15000,
      cur + 31000,
      cur + 61000,
      cur + 601000

  };
  int exp_order_size = ARRAY_SIZE(exp_order);
  for (int i = 0; i < exp_order_size; ++i) {
    uint64_t exp = exp_order[i];
    minheap_node_t *node = mh_extract_min(heap);
    PRINT_TEST_INFO("key=%" PRIu64 " expected=%" PRIu64 "", node->key, exp);
    assert(node != NULL && "Node should not be NULL");
    assert(node->key == exp && "Extracted in wrong order");
  }
  assert(mh_is_empty(heap) && "Heap should be empty after all extractions");

  mh_free(heap);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 4, 9: Тест на граничные случаи и обработку ошибок.
 */
void test_boundary_and_error_cases() {
  PRINT_TEST_START("Boundary cases and error handling");
  minheap_t *heap = mh_create(2);
  heap_value_t v1 = {.heap_node = {.key = 1}, .value = 1};
  heap_value_t v2 = {.heap_node = {.key = 2}, .value = 2};
  heap_value_t v3 = {.heap_node = {.key = 3}, .value = 3};

  // Вставка в полную кучу
  mh_insert(heap, &v1.heap_node);
  mh_insert(heap, &v2.heap_node);
  mh_insert(heap, &v3.heap_node); // Эта вставка должна быть проигнорирована
  assert(mh_get_size(heap) == 2 && "Insert should fail on full heap");

  // Удаление несуществующего элемента
  mh_delete_node(heap, &v3.heap_node);
  assert(mh_get_size(heap) == 2 && "Delete non-existent should not change size");

  // Извлечение из пустой кучи
  mh_extract_min(heap);
  mh_extract_min(heap);
  assert(mh_extract_min(heap) == NULL && "Extract from empty heap should be NULL");
  assert(mh_get_min(heap) == NULL && "Get-min from empty heap should be NULL");

  mh_free(heap);
  PRINT_TEST_PASSED();
}

// --- Реализация упорядоченного связанного списка для сравнения ---
typedef struct sorted_list_node {
  uint64_t key;
  struct sorted_list_node *next;
} sorted_list_node_t;

// Вставка в упорядоченный список (медленная операция O(N))
static void sorted_list_insert(sorted_list_node_t **head, uint64_t key) {
  sorted_list_node_t *new_node = malloc(sizeof(sorted_list_node_t));
  new_node->key = key;

  if (*head == NULL || (*head)->key >= key) {
    new_node->next = *head;
    *head = new_node;
    return;
  }

  sorted_list_node_t *current = *head;
  while (current->next != NULL && current->next->key < key) {
    current = current->next;
  }
  new_node->next = current->next;
  current->next = new_node;
}

// Извлечение минимума из упорядоченного списка (быстрая операция O(1))
static void sorted_list_extract_min(sorted_list_node_t **head) {
  if (*head == NULL)
    return;
  sorted_list_node_t *temp = *head;
  *head = (*head)->next;
  free(temp);
}

void test_heap_vs_sorted_list_consistency() {
  PRINT_TEST_START("Heap invariant check with sorted list reference");
  const int NUM_NODES = 200;
  const int OPERATIONS = 500;
  minheap_t *heap = mh_create(NUM_NODES);
  sorted_list_node_t *list_head = NULL;
  heap_value_t *nodes = calloc(NUM_NODES, sizeof(heap_value_t));
  assert(nodes);

  srand(123);

  for (int i = 0; i < OPERATIONS; ++i) {
    int op = rand() % 3;
    int idx = rand() % NUM_NODES;
    minheap_node_t *node = &nodes[idx].heap_node;

    if (op == 0) { // insert
      if (mh_map_get(heap, node) < 0) {
        node->key = rand();
        mh_insert(heap, node);
        sorted_list_insert(&list_head, node->key);
      }
    } else if (op == 1) { // delete
      if (mh_map_get(heap, node) >= 0) {
        mh_delete_node(heap, node);
        // delete from sorted list
        sorted_list_node_t **curr = &list_head;
        while (*curr) {
          if ((*curr)->key == node->key) {
            sorted_list_node_t *tmp = *curr;
            *curr = (*curr)->next;
            free(tmp);
            break;
          }
          curr = &(*curr)->next;
        }
      }
    } else if (op == 2) { // extract_min
      if (!mh_is_empty(heap)) {
        minheap_node_t *min = mh_extract_min(heap);
        sorted_list_extract_min(&list_head);
        (void)min;
      }
    }

    // validate min values match
    minheap_node_t *heap_min = mh_get_min(heap);
    uint64_t heap_min_key = heap_min ? heap_min->key : UINT64_MAX;
    uint64_t list_min_key = list_head ? list_head->key : UINT64_MAX;
    assert(heap_min_key == list_min_key && "heap min does not match sorted list min");
  }

  // cleanup
  mh_free(heap);
  free(nodes);
  while (list_head)
    sorted_list_extract_min(&list_head);

  PRINT_TEST_PASSED();
}

/**
 * Принцип 8: Тест на обновление ключа существующего узла.
 */
void test_update_node_key() {
  PRINT_TEST_START("Update an existing node's key");
  minheap_t *heap = mh_create(5);
  heap_value_t values[] = {
      {.heap_node = {.key = 20}, .value = 20},
      {.heap_node = {.key = 30}, .value = 30},
      {.heap_node = {.key = 40}, .value = 40},
  };
  for (int i = 0; i < 3; ++i) {
    mh_insert(heap, &values[i].heap_node);
  }

  // Уменьшаем ключ -> узел должен всплыть (sift_up)
  PRINT_TEST_INFO("Decreasing key (sift-up)...");
  values[2].heap_node.key = 10; // меняем 40 -> 10
  mh_insert(heap, &values[2].heap_node);
  assert(mh_get_min(heap)->key == 10 && "Node with decreased key should be min");

  // Увеличиваем ключ -> узел должен утонуть (sift_down)
  PRINT_TEST_INFO("Increasing key (sift-down)...");
  values[2].heap_node.key = 50; // меняем 10 -> 50
  mh_insert(heap, &values[2].heap_node);
  assert(mh_get_min(heap)->key == 20 && "Node with increased key should not be min");

  mh_free(heap);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 10: Интеграционный хаос-стресс тест.
 */
void test_stress_random_operations() {
  PRINT_TEST_START("Stress test with deterministic operations");
  const int NUM_NODES = 100;
  const int ITERATIONS = 200;
  minheap_t *heap = mh_create(NUM_NODES);

  srand(0); // fixed seed for reproducibility

  // Создаем пул узлов
  heap_value_t *nodes = calloc(NUM_NODES, sizeof(heap_value_t));
  assert(nodes != NULL);
  for (int i = 0; i < NUM_NODES; ++i) {
    nodes[i].value = i;
    nodes[i].heap_node.key = rand();
  }

  // Теневая структура для проверки - простой массив флагов (в куче или нет)
  bool *in_heap_shadow = calloc(NUM_NODES, sizeof(bool));
  assert(in_heap_shadow != NULL);

  for (int i = 0; i < ITERATIONS; ++i) {
    int node_idx = rand() % NUM_NODES;
    minheap_node_t *node = &nodes[node_idx].heap_node;

    if (in_heap_shadow[node_idx]) {
      // Узел в куче, можем удалить или извлечь минимальный
      if (rand() % 2 == 0) {
        mh_delete_node(heap, node);
        in_heap_shadow[node_idx] = false;
      } else {
        minheap_node_t *min_node = mh_extract_min(heap);
        if (min_node) {
          int min_val = container_of(min_node, heap_value_t, heap_node)->value;
          in_heap_shadow[min_val] = false;
        }
      }
    } else {
      // Узел не в куче, можем вставить
      node->key = rand(); // Присваиваем новый случайный ключ
      mh_insert(heap, node);
      in_heap_shadow[node_idx] = true;
    }
  }

  // Финальная проверка консистентности
  PRINT_TEST_INFO("Final consistency check...");
  unsigned int heap_size = mh_get_size(heap);
  unsigned int shadow_size = 0;
  for (int i = 0; i < NUM_NODES; ++i) {
    if (in_heap_shadow[i]) {
      shadow_size++;
    }
  }
  assert(heap_size == shadow_size && "Heap size must match shadow count");

  free(nodes);
  free(in_heap_shadow);
  mh_free(heap);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 10: (расширенный): Сравнительный тест производительности.
 * Сравнивает minheap с упорядоченным связанным списком.
 */

// --- Вспомогательная функция для измерения времени ---
uint64_t get_time_usec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// --- Вспомогательная функция для расчета разницы в % ---
// Показывает, на сколько % minheap быстрее (или медленнее) списка.

/**
 * Принцип 10 (расширенный): Сравнительный тест производительности.
 * Сравнивает minheap с упорядоченным связанным списком.
 */

struct heap uv_heap;
typedef struct {
  struct heap_node node;
  int key;
} uv_heap_value_t;

static int uv_heap_less_than(const struct heap_node *a, const struct heap_node *b) {
  const uv_heap_value_t *va = (const uv_heap_value_t *)a;
  const uv_heap_value_t *vb = (const uv_heap_value_t *)b;
  return va->key < vb->key;
}

static double calc_diff_perc(long long val, long long best_val) {
  if (best_val == 0) return 0.0;
  return 100.0 * ((double)val - (double)best_val) / (double)best_val;
}

void test_performance_vs_list() {
  PRINT_TEST_START("Performance comparison: minheap vs sorted linked list");
  const int NUM_ITEMS = 20000;
  uint64_t start_time, h_ins_t, h_ext_t, h_tot_t, l_ins_t, l_ext_t, l_tot_t, uv_ins_t, uv_ext_t, uv_tot_t, best_ins_t, best_ext_t, best_tot_t;

  // --- Этап 1: Тестирование minheap ---
  minheap_t *heap = mh_create(NUM_ITEMS);
  heap_value_t *heap_nodes = calloc(NUM_ITEMS, sizeof(heap_value_t));
  assert(heap && heap_nodes);
  start_time = get_time_usec();
  for (int i = 0; i < NUM_ITEMS; ++i) {
    heap_nodes[i].heap_node.key = rand();
    mh_insert(heap, &heap_nodes[i].heap_node);
  }
  h_ins_t = get_time_usec() - start_time;
  start_time = get_time_usec();
  for (int i = 0; i < NUM_ITEMS; ++i) {
    mh_extract_min(heap);
  }
  h_ext_t = get_time_usec() - start_time;
  free(heap_nodes);
  mh_free(heap);

  best_ins_t = h_ins_t;
  best_ext_t = h_ext_t;
  h_tot_t = h_ins_t + h_ext_t;
  best_tot_t = h_tot_t;

  // --- Этап 1b: Тестирование кучи на указателях (uv_heap) ---

  uv_heap_value_t *uv_nodes = calloc(NUM_ITEMS, sizeof(uv_heap_value_t));
  assert(uv_nodes);
  heap_init(&uv_heap);
  start_time = get_time_usec();
  for (int i = 0; i < NUM_ITEMS; ++i) {
    uv_nodes[i].key = rand();
    heap_insert(&uv_heap, &uv_nodes[i].node, uv_heap_less_than);
  }
  uv_ins_t = get_time_usec() - start_time;
  start_time = get_time_usec();
  for (int i = 0; i < NUM_ITEMS; ++i) {
    heap_dequeue(&uv_heap, uv_heap_less_than);
  }
  uv_ext_t = get_time_usec() - start_time;
  free(uv_nodes);

  best_ins_t = MIN(uv_ins_t, best_ins_t);
  best_ext_t = MIN(uv_ext_t, best_ext_t);
  uv_tot_t = uv_ins_t + uv_ext_t;
  best_tot_t = MIN(uv_tot_t, best_tot_t);

  // --- Этап 2: Тестирование упорядоченного связанного списка ---
  sorted_list_node_t *list_head = NULL;
  start_time = get_time_usec();
  for (int i = 0; i < NUM_ITEMS; ++i) {
    sorted_list_insert(&list_head, rand());
  }
  l_ins_t = get_time_usec() - start_time;
  start_time = get_time_usec();
  for (int i = 0; i < NUM_ITEMS; ++i) {
    sorted_list_extract_min(&list_head);
  }
  l_ext_t = get_time_usec() - start_time;

  best_ins_t = MIN(l_ins_t, best_ins_t);
  best_ext_t = MIN(l_ext_t, best_ext_t);
  l_tot_t = l_ins_t + l_ext_t;
  best_tot_t = MIN(l_tot_t, best_tot_t);

  // --- Этап 3: Сравнение в виде таблицы ---

// --- Макросы для форматирования таблицы ---
#define CW1 "14"
#define CW2 "20"
#define CW3 "23"
#define CW4 "22"
#define CW5 "15"

#define HC1 "%-" CW1 "s"
#define HC2 "%-" CW2 "s"
#define HC3 "%-" CW3 "s"
#define HC4 "%-" CW4 "s"
#define HC5 "%-" CW5 "s"

#define DC1 "%-" CW1 "s"
#define DC2 "%'" CW2 PRIu64
#define DC3 "%'" CW3 PRIu64
#define DC4 "%'" CW4 PRIu64
#define DC5 "%+" CW5 ".2f%%"

#define P "%%"

  // clang-format off

// --- Макросы для целых строк ---
#define HDR_FMT "structure name| insert (us)        | extract (us)          | total (us)           |Diff with best "P" \n"
#define SEPARAT "--------------|--------------------|-----------------------|----------------------|-------------------\n"
#define DTR_FMT DC1           "|"              DC2 "|" DC3                 "|"                DC4 "|"             DC5 "\n"

  printf("--- Comparison Summary (items: %d) ---\n", NUM_ITEMS);
  printf(HDR_FMT);
  printf(SEPARAT);
  printf(DTR_FMT, "minheap", h_ins_t, h_ext_t, h_tot_t, calc_diff_perc(h_tot_t, best_tot_t));
  printf(SEPARAT);
  printf(DTR_FMT, "uv minheap", uv_ins_t, uv_ext_t, uv_tot_t, calc_diff_perc(uv_tot_t, best_tot_t));
  printf(SEPARAT);
  printf(DTR_FMT, "linked list", l_ins_t, l_ext_t, l_tot_t, calc_diff_perc(l_tot_t, best_tot_t));
  printf(SEPARAT);
  // clang-format on

  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
#ifdef DEBUG
  setup_syslog2("uevent_test", LOG_DEBUG, false);
#else
  setup_syslog2("uevent_test", LOG_NOTICE, false);
#endif

  struct test_entry tests[] = {
      {"create_and_free", test_create_and_free},
      {"minheap_get_node", test_minheap_get_node},
      {"mh_map_functions", test_mh_map_functions},
      {"create_malloc_fail", test_create_malloc_fail},
      {"insert_null_heap", test_insert_null_heap},
      {"insert_null_node", test_insert_null_node},
      {"insert_overflow", test_insert_overflow},
      {"free_null", test_free_null},
      {"extract_min_null", test_extract_min_null},
      {"extract_min_empty", test_extract_min_empty},
      {"get_min_null", test_get_min_null},
      {"get_min_empty", test_get_min_empty},
      {"is_empty_null", test_is_empty_null},
      {"get_size_null", test_get_size_null},
      {"delete_value_null_heap", test_delete_value_null_heap},
      {"delete_value_null_node", test_delete_value_null_node},
      {"delete_value_not_in_heap", test_delete_value_not_in_heap},
      {"insert_and_get_min", test_insert_and_get_min},
      {"extract_min_order", test_extract_min_order},
      {"boundary_and_error_cases", test_boundary_and_error_cases},
      {"update_node_key", test_update_node_key},
      {"stress_random_operations", test_stress_random_operations},
      {"performance_vs_list", test_performance_vs_list}};

  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (argc > 1 || rc)
    return rc;

  test_create_and_free();
  test_minheap_get_node();
  test_mh_map_functions();
  test_create_malloc_fail();
  test_insert_null_heap();
  test_insert_null_node();
  test_insert_overflow();
  test_free_null();
  test_extract_min_null();
  test_extract_min_empty();
  test_get_min_null();
  test_get_min_empty();
  test_is_empty_null();
  test_get_size_null();
  test_delete_value_null_heap();
  test_delete_value_null_node();
  test_delete_value_not_in_heap();
  test_insert_and_get_min();
  test_extract_min_order();
  test_boundary_and_error_cases();
  test_update_node_key();
  test_stress_random_operations();
  test_heap_vs_sorted_list_consistency();
  test_performance_vs_list();

  printf(KGRN "====== All minheap tests passed! ======\n" KNRM);
  return rc;
}
