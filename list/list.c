#include "list.h"
#include <time.h>

static void (*log_func)(int, const char *, ...) = NULL;
static int (*get_time_func)(struct timespec *) = NULL;

int list_mod_init(const list_mod_init_args_t *args) {
  if (!args) {
    log_func = NULL;
    get_time_func = NULL;
    return 0;
  }
  log_func = args->log;
  get_time_func = args->get_time;
  return 0;
}

int list_is_empty(const struct list_head *head) {
  return list_empty(head);
}

int list_count(const struct list_head *head) {
  return list_size(head);
}
