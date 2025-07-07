#include "list.h"

#include <stdio.h>
#include <string.h>

struct item {
    char name[16];
    int value;
    struct list_head node;
};

int main(void) {
    struct list_head head;
    INIT_LIST_HEAD(&head);

    struct item items[3] = {
        {"first", 1},
        {"second", 2},
        {"third", 3}
    };

    for (int i = 0; i < 3; ++i) {
        INIT_LIST_HEAD(&items[i].node);
        list_add_tail(&items[i].node, &head);
    }

    struct item *pos;
    list_for_each_entry(pos, &head, node) {
        printf("%s => %d\n", pos->name, pos->value);
    }

    return 0;
}
