#ifndef HTABLE_H
#define HTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../list/list.h" // Предполагаем, что list.h в родительском каталоге

// Узел хэш-таблицы
typedef struct htable_node {
  uintptr_t key;           // Ключ (например, адрес указателя)
  void *value;             // Значение (указатель на данные)
  struct hlist_node hnode; // Узел для цепочки при коллизиях
} htable_node_t;

// Структура хэш-таблицы
typedef struct htable {
  size_t capacity;            // Максимальное количество бакетов
  struct hlist_head *buckets; // Массив бакетов (голов списков)
} htable_t;

/**
 * @brief Создает новую хэш-таблицу.
 * @param capacity Начальное количество бакетов.
 * @return Указатель на созданную хэш-таблицу или NULL при ошибке.
 */
htable_t *htable_create(size_t capacity);

/**
 * @brief Освобождает всю память, занимаемую хэш-таблицей и ее элементами.
 * @param ht Указатель на хэш-таблицу.
 */
void htable_free(htable_t *ht);

/**
 * @brief Добавляет или обновляет элемент в хэш-таблице.
 * Если ключ уже существует, его значение будет обновлено.
 * @param ht Указатель на хэш-таблицу.
 * @param key Ключ элемента.
 * @param value Указатель на значение элемента.
 */
void htable_set(htable_t *ht, uintptr_t key, void *value);

/**
 * @brief Получает значение из хэш-таблицы по ключу.
 * @param ht Указатель на хэш-таблицу.
 * @param key Ключ для поиска.
 * @return Указатель на значение или NULL, если ключ не найден.
 */
void *htable_get(htable_t *ht, uintptr_t key);

/**
 * @brief Удаляет элемент из хэш-таблицы по ключу.
 * @param ht Указатель на хэш-таблицу.
 * @param key Ключ элемента для удаления.
 */
void htable_del(htable_t *ht, uintptr_t key);

#endif // HTABLE_H
