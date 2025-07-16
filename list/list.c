#include "list.h"

/* The list helpers are simple wrappers over the Linux kernel macros. */

int list_is_empty(const struct list_head *head) {
  return list_empty(head);
}

int list_count(const struct list_head *head) {
  return list_size(head);
}
