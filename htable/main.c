#include "htable.h"
#include <stdint.h> // Для uintptr_t
#include <stdio.h>

// Пример пользовательской структуры данных
typedef struct {
  int id;
  const char *name;
} my_data_t;

int main(void) {
  // 1. Создаем хэш-таблицу
  htable_t *ht = htable_create(10); // Емкость 10 бакетов
  if (!ht) {
    printf("Failed to create htable\n");
    return 1;
  }
  printf("Htable created.\n\n");

  // 2. Создаем данные для хранения
  my_data_t items[] = {{101, "alpha"}, {202, "beta"}, {303, "gamma"}};
  int num_items = sizeof(items) / sizeof(items[0]);

  // 3. Добавляем элементы в хэш-таблицу
  printf("Adding items...\n");
  for (int i = 0; i < num_items; ++i) {
    // Используем 'id' как ключ, а указатель на структуру как значение
    htable_set(ht, items[i].id, &items[i]);
    printf("  Set: key=%d, value={id:%d, name:'%s'}\n", items[i].id,
           items[i].id, items[i].name);
  }
  printf("\n");

  // 4. Получаем и печатаем один из элементов
  uintptr_t key_to_find = 202;
  my_data_t *found_item = (my_data_t *)htable_get(ht, key_to_find);
  printf("Getting item with key %lu...\n", (unsigned long)key_to_find);
  if (found_item) {
    printf("  Found: id=%d, name='%s'\n", found_item->id,
           found_item->name);
  } else {
    printf("  Item with key %lu not found.\n", (unsigned long)key_to_find);
  }
  printf("\n");

  // 5. Обновляем элемент, просто передав новое значение по тому же ключу
  my_data_t item_updated = {202, "beta_updated"};
  printf("Updating item with key %d...\n", item_updated.id);
  htable_set(ht, item_updated.id, &item_updated);
  found_item = (my_data_t *)htable_get(ht, item_updated.id);
  if (found_item) {
    printf("  After update: id=%d, name='%s'\n", found_item->id,
           found_item->name);
  }
  printf("\n");

  // 6. Удаляем элемент
  uintptr_t key_to_delete = 101;
  printf("Deleting item with key %lu...\n", (unsigned long)key_to_delete);
  htable_del(ht, key_to_delete);

  // Проверяем, что он удален
  found_item = (my_data_t *)htable_get(ht, key_to_delete);
  if (!found_item) {
    printf("  Successfully verified item with key %lu is deleted.\n",
           (unsigned long)key_to_delete);
  } else {
    printf("  ERROR: Item with key %lu was not deleted.\n",
           (unsigned long)key_to_delete);
  }
  printf("\n");

  // 7. Освобождаем ресурсы
  htable_free(ht);
  printf("Htable freed.\n");

  return 0;
}
