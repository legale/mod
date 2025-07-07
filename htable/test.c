#include "htable.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../test_util.h"

// --- Юнит-тесты ---

/**
 * Принцип 1, 2, 4: Изолированный тест на базовое создание и освобождение.
 * Проверяет позитивный (создание с >0 capacity) и негативный (с 0 capacity) сценарии.
 */
void test_create_and_free() {
  PRINT_TEST_START("Create with valid capacity and free");
  htable_t *ht = htable_create(10);
  assert(ht != NULL && "htable_create should succeed for non-zero capacity");
  assert(ht->capacity == 10 && "Capacity should be set correctly");
  assert(ht->buckets != NULL && "Buckets should be allocated");
  htable_free(ht);
  PRINT_TEST_PASSED();

  PRINT_TEST_START("Create with zero capacity (negative case)");
  ht = htable_create(0);
  assert(ht == NULL && "htable_create should fail for zero capacity");
  PRINT_TEST_PASSED();
}

/**
 * Принцип 1, 3, 5, 7: Простой, изолированный тест на добавление и получение одного элемента.
 */
void test_set_and_get_single_item() {
  PRINT_TEST_START("Set and get a single item");
  htable_t *ht = htable_create(10);
  int my_value = 42;
  uintptr_t key = 101;

  htable_set(ht, key, &my_value);
  void *retrieved_value = htable_get(ht, key);

  assert(retrieved_value != NULL && "Value should be found for existing key");
  assert(*(int *)retrieved_value == my_value &&
         "Retrieved value should match the original");

  htable_free(ht);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 4: Тест на обновление существующего элемента.
 */
void test_update_existing_item() {
  PRINT_TEST_START("Update an existing item's value");
  htable_t *ht = htable_create(10);
  int value1 = 42;
  int value2 = 99;
  uintptr_t key = 101;

  htable_set(ht, key, &value1);
  htable_set(ht, key, &value2); // Обновляем значение по тому же ключу
  void *retrieved_value = htable_get(ht, key);

  assert(retrieved_value != NULL && "Value should still be found after update");
  assert(*(int *)retrieved_value == value2 &&
         "Retrieved value should be the updated one");

  htable_free(ht);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 4: Тест на удаление элемента.
 */
void test_delete_item() {
  PRINT_TEST_START("Delete an item and verify its removal");
  htable_t *ht = htable_create(10);
  int my_value = 42;
  uintptr_t key = 101;

  htable_set(ht, key, &my_value);
  htable_del(ht, key);
  void *retrieved_value = htable_get(ht, key);

  assert(retrieved_value == NULL &&
         "Value should not be found after deletion");

  // Повторное удаление не должно вызывать проблем
  htable_del(ht, key);

  htable_free(ht);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 4, 9: Тест на получение несуществующего элемента и граничные ключи.
 */
void test_get_non_existent_and_boundary_keys() {
  PRINT_TEST_START("Get non-existent and boundary keys");
  htable_t *ht = htable_create(10);
  int my_value = 123;

  // Проверяем несуществующий ключ
  assert(htable_get(ht, 999) == NULL && "Should return NULL for non-existent key");

  // Проверяем граничные случаи для ключей
  htable_set(ht, 0, &my_value); // Ключ 0
  assert(*(int *)htable_get(ht, 0) == my_value && "Should handle key 0 correctly");

  htable_set(ht, UINTPTR_MAX, &my_value); // Максимальный ключ
  assert(*(int *)htable_get(ht, UINTPTR_MAX) == my_value &&
         "Should handle UINTPTR_MAX key correctly");

  htable_free(ht);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 8, 9: Тест на обработку коллизий.
 * Создаем ключи, которые гарантированно попадут в один и тот же бакет.
 */
void test_collision_handling() {
  PRINT_TEST_START("Handle key collisions correctly");
  size_t capacity = 10;
  htable_t *ht = htable_create(capacity);

  uintptr_t key1 = 5;
  uintptr_t key2 = 15; // key2 % 10 == key1 % 10
  uintptr_t key3 = 25; // key3 % 10 == key1 % 10

  int val1 = 1, val2 = 2, val3 = 3;

  htable_set(ht, key1, &val1);
  htable_set(ht, key2, &val2);
  htable_set(ht, key3, &val3);

  assert(*(int *)htable_get(ht, key1) == val1 && "First item in chain is correct");
  assert(*(int *)htable_get(ht, key2) == val2 && "Second item in chain is correct");
  assert(*(int *)htable_get(ht, key3) == val3 && "Third item in chain is correct");

  // Удаляем средний элемент цепочки
  htable_del(ht, key2);
  assert(htable_get(ht, key2) == NULL && "Middle item should be deleted");
  assert(*(int *)htable_get(ht, key1) == val1 && "First item remains correct");
  assert(*(int *)htable_get(ht, key3) == val3 && "Third item remains correct");

  htable_free(ht);
  PRINT_TEST_PASSED();
}

/**
 * Принцип 10: Интеграционный хаос-стресс тест.
 * Выполняет большое количество случайных операций для проверки стабильности.
 */
void test_stress_and_random_operations() {
  PRINT_TEST_START("Stress test with deterministic operations");
  const int ITERATIONS = 200;
  const int KEY_SPACE = 100;
  htable_t *ht = htable_create(KEY_SPACE / 10); // Меньшая емкость для коллизий

  // "Теневая" структура для проверки корректности
  void **shadow_map = calloc(KEY_SPACE, sizeof(void *));
  assert(shadow_map != NULL);

  srand(0); // fixed seed for deterministic behavior

  for (int i = 0; i < ITERATIONS; ++i) {
    int op = rand() % 3; // 0: set, 1: get, 2: del
    uintptr_t key = rand() % KEY_SPACE;

    switch (op) {
    case 0: { // set
      // Используем указатель на статическую переменную, т.к. нам не важно само значение
      static int val = 1;
      htable_set(ht, key, &val);
      shadow_map[key] = &val;
      break;
    }
    case 1: { // get
      void *ht_val = htable_get(ht, key);
      void *shadow_val = shadow_map[key];
      assert(ht_val == shadow_val && "Get operation should match shadow map");
      break;
    }
    case 2: { // del
      htable_del(ht, key);
      shadow_map[key] = NULL;
      break;
    }
    }
  }

  // Финальная проверка консистентности
  PRINT_TEST_INFO("Final consistency check...");
  for (int i = 0; i < KEY_SPACE; ++i) {
    assert(htable_get(ht, i) == shadow_map[i] &&
           "Final state should be consistent with shadow map");
  }

  free(shadow_map);
  htable_free(ht);
  PRINT_TEST_PASSED();
}

// --- Главная функция для запуска всех тестов ---
int main(void) {
  // Принцип 6: Автоматический запуск всех тестов
  test_create_and_free();
  test_set_and_get_single_item();
  test_update_existing_item();
  test_delete_item();
  test_get_non_existent_and_boundary_keys();
  test_collision_handling();
  test_stress_and_random_operations();

  printf(KGRN "====== All htable tests passed! ======\n" KNRM);
  return 0;
}
