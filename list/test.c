#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../test_util.h"

struct mynode {
    int val;
    struct list_head node;
};

static void test_basic(void) {
    PRINT_TEST_START("list basic");
    struct list_head head;
    INIT_LIST_HEAD(&head);
    assert(list_is_empty(&head));
    assert(list_count(&head) == 0);

    struct mynode a = { .val = 1 }, b = { .val = 2 }, c = { .val = 3 };
    INIT_LIST_HEAD(&a.node);
    INIT_LIST_HEAD(&b.node);
    INIT_LIST_HEAD(&c.node);

    list_add(&a.node, &head);
    list_add_tail(&b.node, &head);
    list_add(&c.node, &head);

    assert(!list_is_empty(&head));
    assert(list_count(&head) == 3);

    int sum = 0;
    struct mynode *p;
    list_for_each_entry(p, &head, node) {
        sum += p->val;
    }
    assert(sum == 6);

    list_del(&a.node);
    list_del(&b.node);
    list_del(&c.node);
    assert(list_is_empty(&head));
    PRINT_TEST_PASSED();
}

int main(void) {
    test_basic();
    return 0;
}
