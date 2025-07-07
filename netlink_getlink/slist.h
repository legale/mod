#ifndef _SLIST_H_
#define _SLIST_H_

/*
 * Singly-linked list
 */

/* Macros for branch prediction */
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef NULL
#define NULL 0
#endif

/* Offset of member MEMBER in a struct of type TYPE. */
#define offsetof_(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)

#define container_of_(ptr, type, member) ({            \
    const typeof(((type *)0)->member) *__mptr = (ptr);    \
    (type *)((char *)__mptr - offsetof_(type, member)); })

typedef struct slist_node {
  struct slist_node *next;
} slist_node_t;

typedef struct slist_head {
  slist_node_t *head; /* head element */
  slist_node_t *tail; /* tail element */
} slist_head_t;

/* Инициализация головы списка */
static inline void INIT_SLIST_HEAD(struct slist_head *list) {
  list->head = NULL;
  list->tail = NULL;
}

/* Проверка, пуст ли список */
static inline int slist_empty(const struct slist_head *list) {
  if (unlikely(list == NULL)) {
    return 1; /* Если список не существует, считаем его пустым */
  }
  return (list->head == NULL) ? 1 : 0;
}

/* Добавление в начало списка */
static inline void slist_add(struct slist_node *new, struct slist_head *list) {
  if (unlikely((new == NULL) || (list == NULL))) {
    return;
  }

  new->next = list->head;
  list->head = new;

  /* Если список был пустой, обновляем tail */
  if (unlikely(list->tail == NULL)) {
    list->tail = new;
  }
}

/* Добавление в конец списка */
static inline void slist_add_tail(struct slist_node *new, struct slist_head *list) {
  if (unlikely((new == NULL) || (list == NULL))) {
    return;
  }

  new->next = NULL;

  if (likely(slist_empty(list) == 0)) {
    if (likely(list->tail != NULL)) {
      list->tail->next = new; /* Присоединяем к последнему */
    }
  } else {
    list->head = new; /* Если список пустой */
  }

  list->tail = new; /* Обновляем указатель на последний элемент */
}

/* Удаление первого элемента списка */
static inline void slist_del_head(struct slist_head *list) {
  if (unlikely((list == NULL) || (slist_empty(list) != 0))) {
    return;
  }

  struct slist_node *first = list->head;
  if (likely(first != NULL)) {
    list->head = first->next;

    /* Если это был последний элемент, обнуляем tail */
    if (unlikely(list->head == NULL)) {
      list->tail = NULL;
    }
  }
}

/* Удаление указанного элемента, следующего за prev (или головы, если prev == NULL) */
static inline void slist_del_next(struct slist_node *prev, struct slist_head *list) {
  if (unlikely(list == NULL)) {
    return;
  }

  if (unlikely(prev == NULL)) {
    /* Удаляем первый элемент списка */
    slist_del_head(list);
    return;
  }

  /* Проверка на наличие следующего элемента */
  if (unlikely(prev->next == NULL)) {
    return;
  }

  struct slist_node *to_del = prev->next;
  prev->next = to_del->next;

  /* Если удаляемый был последним, обновляем tail */
  if (unlikely(to_del == list->tail)) {
    list->tail = prev;
  }
}

/* Удаление конкретного элемента из списка */
static inline int slist_del_node(struct slist_node *node, struct slist_head *list) {
  if (unlikely(list == NULL || node == NULL || list->head == NULL)) {
    return -1;
  }

  /* Если это головной элемент */
  if (list->head == node) {
    slist_del_head(list);
    return 0;
  }

  /* Ищем предыдущий элемент */
  struct slist_node *prev = list->head;
  while (prev && prev->next != node) {
    prev = prev->next;
  }

  if (prev) {
    /* Нашли предыдущий, удаляем текущий */
    slist_del_next(prev, list);
    return 0;
  }

  return -1; /* Элемент не найден */
}

/* Получаем контейнер узла списка */
#define slist_entry(ptr, type, member) \
  container_of_(ptr, type, member)

/* Получаем первый элемент списка */
#define slist_head_entry(list, type, member) \
  (likely((list)->head != NULL) ? slist_entry((list)->head, type, member) : NULL)

/* Получаем следующий элемент списка */
#define slist_next_entry(pos, member) \
  (likely((pos) != NULL && (pos)->member.next != NULL) ? slist_entry((pos)->member.next, typeof(*(pos)), member) : NULL)

/* Итерация по всем элементам списка */
#define slist_for_each(pos, list)                            \
  for (pos = (likely((list) != NULL) ? (list)->head : NULL); \
       likely(pos != NULL);                                  \
       pos = pos->next)

/* Итерация по всем элементам списка с получением структуры-владельца */
#define slist_for_each_entry(pos, list, member)                                                       \
  for (pos = (likely((list)->head != NULL) ? slist_entry((list)->head, typeof(*pos), member) : NULL); \
       likely(pos != NULL);                                                                           \
       pos = (likely((pos)->member.next != NULL) ? slist_entry((pos)->member.next, typeof(*pos), member) : NULL))

/* Безопасная итерация с отслеживанием следующего элемента (можно удалять текущий элемент) */
#define slist_for_each_safe(pos, next, list)                 \
  for (pos = (likely((list) != NULL) ? (list)->head : NULL), \
      next = (likely(pos != NULL) ? (pos)->next : NULL);     \
       likely(pos != NULL);                                  \
       pos = next,                                           \
      next = (likely(next != NULL) ? next->next : NULL))

/* Безопасная итерация для удаления элементов */
#define slist_for_each_entry_safe(pos, temp, list, member)                                                                       \
  for (pos = (likely((list)->head != NULL) ? slist_entry((list)->head, typeof(*pos), member) : NULL),                            \
      temp = (likely(pos != NULL && (pos)->member.next != NULL) ? slist_entry((pos)->member.next, typeof(*pos), member) : NULL); \
       likely(pos != NULL);                                                                                                      \
       pos = temp,                                                                                                               \
      temp = (likely(temp != NULL && (temp)->member.next != NULL) ? slist_entry((temp)->member.next, typeof(*temp), member) : NULL))

#endif /* _SLIST_H_ */
