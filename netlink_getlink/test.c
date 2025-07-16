#include "../test_util.h"
#include "libnl_getlink.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

static void test_get_netdev_list(void) {
    struct slist_head list;
    INIT_SLIST_HEAD(&list);

    int ret = get_netdev(&list);
    assert(ret == 0 && "get_netdev return 0 expected");
    assert(slist_empty(&list) == 0 && "netdev list should not be empty");

    free_netdev_list(&list);
    PRINT_TEST_PASSED();
}

static void test_default_logger_stdout(void) {
    fflush(stdout);
    int orig_fd = dup(STDOUT_FILENO);
    FILE *tmp = tmpfile();
    assert(tmp && "tmpfile failed");
    dup2(fileno(tmp), STDOUT_FILENO);

    struct slist_head list;
    INIT_SLIST_HEAD(&list);

    int ret = get_netdev(&list);
    assert(ret == 0 && "get_netdev should succeed");

    fflush(stdout);
    fseek(tmp, 0, SEEK_SET);
    int c = fgetc(tmp);
    assert(c != EOF && "default logger should output to stdout");

    dup2(orig_fd, STDOUT_FILENO);
    close(orig_fd);
    fclose(tmp);

    free_netdev_list(&list);
    PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
    struct test_entry tests[] = {
        {"get_netdev_list", test_get_netdev_list},
        {"default_logger_stdout", test_default_logger_stdout}
    };
    int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
    if (!rc && argc == 1)
        printf(KGRN "====== All netlink_getlink tests passed! ======\n" KNRM);
    return rc;
}
