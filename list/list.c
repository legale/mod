#include "list.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void *(*list_malloc_hook)(size_t) = malloc;
static void (*list_free_hook)(void *) = free;
static void (*list_log_hook)(const char *, ...) = NULL;

void list_mod_init(const list_mod_init_args_t *args) {
  if (args) {
    list_malloc_hook = args->malloc_fn ? args->malloc_fn : malloc;
    list_free_hook = args->free_fn ? args->free_fn : free;
    list_log_hook = args->log_fn;
  } else {
    list_malloc_hook = malloc;
    list_free_hook = free;
    list_log_hook = NULL;
  }
}

int list_is_empty(const struct list_head *head) {
  return list_empty(head);
}

int list_count(const struct list_head *head) {
  return list_size(head);
}
