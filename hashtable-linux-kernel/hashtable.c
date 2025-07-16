
#include <stdint.h>
#include <stdlib.h>

#ifdef JEMALLOC
#include "jemalloc.h"
#endif
#include "hashtable.h"
#include "mock_mem_functions.h"

// redefine mem functions with custom version
#define malloc custom_malloc
#define calloc custom_calloc
#define free custom_free

#ifndef IS_DYNAMIC_LIB
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#define syslog2(pri, fmt, ...) syslog2_(pri, __func__, __FILE__, __LINE__, fmt, true, ##__VA_ARGS__)

__attribute__((weak)) void setup_syslog2(const char *ident, int level, bool use_syslog);
void setup_syslog2(const char *ident, int level, bool use_syslog) {}

__attribute__((weak)) void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...);
void syslog2_(int pri, const char *func, const char *file, int line, const char *fmt, bool nl, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, nl);
  int len = snprintf(buf, sizeof(buf), "[%d] %s:%d %s: ", pri, file, line, func);
  len += vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
  va_end(ap);
  if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
  if (nl && len < (int)sizeof(buf) - 1) buf[len++] = '\n';
  ssize_t written = write(STDOUT_FILENO, buf, len);
  (void)written;
}
#endif

hashtable_t *ht_create(uint32_t bits) {
  hashtable_t *ht = malloc(sizeof(hashtable_t));
  if (!ht) return NULL;

  ht->bits = bits;
  ht->table = malloc((1 << bits) * sizeof(struct hlist_head));
  if (!ht->table) {
    free(ht);
    return NULL;
  }

  hashtable_init(ht);
  return ht;
}
