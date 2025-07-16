#include "assoc_array.h"
#include "hashtable.h"
#include "../test_util.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct string_entry {
  struct hlist_node node;
  char *str;
};

static void free_string_entry(struct string_entry *se) {
  free(se->str);
  free(se);
}

// wrapper used by assoc_array to free both the entry and its data
static void free_assoc_entry(void *e) {
  assoc_array_entry_t *ae = e;
  if (ae->data) free_string_entry(ae->data);
  free(ae->key);
  free(ae);
}

static void test_hashtable_basic(void) {
  PRINT_TEST_START("hashtable basic");
  hashtable_t *ht = ht_create(6);
  assert(ht);
  struct string_entry *e = malloc(sizeof(*e));
  e->str = strdup("foo");
  int key = hash_time33(e->str, strlen(e->str));
  hashtable_add(ht, &e->node, key);

  int bkt = calc_bkt(key, 1 << ht->bits);
  struct string_entry *cur;
  int found = 0;
  hlist_for_each_entry(cur, &ht->table[bkt], node) {
    if (strcmp(cur->str, e->str) == 0)
      found = 1;
  }
  assert(found);
  hashtable_del(ht, &e->node);
  free_string_entry(e);
  HT_FREE(ht, struct string_entry, node, free_string_entry);
  PRINT_TEST_PASSED();
}


static void test_assoc_array_basic(void) {
  PRINT_TEST_START("assoc array basic");
  assoc_array_t *arr = array_create(4, free_assoc_entry, NULL);
  assert(arr);
  struct string_entry *e = malloc(sizeof(*e));
  e->str = strdup("bar");
  uint8_t ksz = strlen(e->str) + 1;
  assert(array_add(arr, e, e->str, ksz) == 0);
  assoc_array_entry_t *found = array_get_by_key(arr, e->str, ksz);
  assert(found && found->data == e);
  assert(array_del(arr, e->str, ksz) == 0);
  array_free(arr);
  PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
  struct test_entry tests[] = {
      {"hashtable_basic", test_hashtable_basic},
      {"assoc_array_basic", test_assoc_array_basic}};
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    printf(KGRN "====== All hashtable-linux-kernel tests passed! ======\n" KNRM);
  return rc;
}
