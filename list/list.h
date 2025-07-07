/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LIST_H_
#define _LINUX_LIST_H_ 1
/* List and hash list stuff from kernel */

#include <stdio.h>

#ifndef __LINUX_KERNEL_H
#define __LINUX_KERNEL_H

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	const typeof(((type *)0)->member) * __mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type, member)); })
#endif

#ifndef max
#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })
#endif

#ifndef min
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })
#endif

#ifndef roundup
#define roundup(x, y) (                \
    {                                  \
      const typeof(y) __y = y;         \
      (((x) + (__y - 1)) / __y) * __y; \
    })
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define __KERNEL_DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#endif //__LINUX_KERNEL_H

struct list_head {
  struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list) {
  list->next = list;
  list->prev = list;
}

static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next) {
  next->prev = new;
  new->next = next;
  new->prev = prev;
  prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head) {
  __list_add(new, head, head->next);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head) {
  __list_add(new, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next) {
  next->prev = prev;
  prev->next = next;
}

static inline void list_del(struct list_head *entry) {
  __list_del(entry->prev, entry->next);
}

#define list_entry(ptr, type, member) \
  container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
  list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
  list_entry((ptr)->prev, type, member)

#define list_next_entry(pos, member) \
  list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_prev_entry(pos, member) \
  list_entry((pos)->member.prev, typeof(*(pos)), member)

#define list_for_each_entry(pos, head, member)             \
  for (pos = list_first_entry(head, typeof(*pos), member); \
       &pos->member != (head);                             \
       pos = list_next_entry(pos, member))

#define list_for_each_entry_safe(pos, n, head, member)     \
  for (pos = list_first_entry(head, typeof(*pos), member), \
      n = list_next_entry(pos, member);                    \
       &pos->member != (head);                             \
       pos = n, n = list_next_entry(n, member))

#define list_for_each_entry_reverse(pos, head, member)    \
  for (pos = list_last_entry(head, typeof(*pos), member); \
       &pos->member != (head);                            \
       pos = list_prev_entry(pos, member))

struct hlist_head {
  struct hlist_node *first;
};

struct hlist_node {
  struct hlist_node *next, **pprev;
};

static inline void hlist_del(struct hlist_node *n) {
  struct hlist_node *next = n->next;
  struct hlist_node **pprev = n->pprev;
  *pprev = next;
  if (next)
    next->pprev = pprev;
}

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h) {
  struct hlist_node *first = h->first;
  n->next = first;
  if (first)
    first->pprev = &n->next;
  h->first = n;
  n->pprev = &h->first;
}

static inline int list_empty(const struct list_head *head) {
  return head->next == head;
}

static inline int list_size(const struct list_head *head) {
  int count = 0;
  const struct list_head *pos;
  for (pos = head->next; pos != head; pos = pos->next) {
    count++;
  }
  return count;
}

#define hlist_for_each(pos, head) \
  for (pos = (head)->first; pos; pos = pos->next)

#define hlist_for_each_safe(pos, n, head) \
  for (pos = (head)->first; pos && ({ n = pos->next; 1; }); \
       pos = n)

#define hlist_entry_safe(ptr, type, member)              \
  ({                                                     \
    typeof(ptr) ____ptr = (ptr);                         \
    ____ptr ? hlist_entry(____ptr, type, member) : NULL; \
  })

#define hlist_for_each_entry(pos, head, member)                       \
  for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); \
       pos;                                                           \
       pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))

#define hlist_entry(ptr, type, member) \
  container_of(ptr, type, member)

static inline void __list_splice(const struct list_head *list,
                                 struct list_head *prev,
                                 struct list_head *next) {
  struct list_head *first = list->next;
  struct list_head *last = list->prev;

  first->prev = prev;
  prev->next = first;

  last->next = next;
  next->prev = last;
}

/**
 * list_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice(const struct list_head *list,
                               struct list_head *head) {
  if (!list_empty(list))
    __list_splice(list, head, head->next);
}

/**
 * list_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static inline void list_splice_init(struct list_head *list,
                                    struct list_head *head) {
  if (!list_empty(list)) {
    __list_splice(list, head, head->next);
    INIT_LIST_HEAD(list);
  }
}

int list_is_empty(const struct list_head *head);
int list_count(const struct list_head *head);
#endif /* _LINUX_LIST_H_ */
