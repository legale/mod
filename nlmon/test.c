#include "../test_util.h"
#include "nlmon.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <time.h>

// --- Моки и вспомогательные структуры ---

typedef struct {
    int called;
    char ifname[IF_NAMESIZE];
    uint32_t event;
    void *arg;
} cb_ctx_t;

static void mock_cb(const char *ifname, uint32_t event, void *arg) {
    cb_ctx_t *ctx = (cb_ctx_t *)arg;
    ctx->called++;
    strncpy(ctx->ifname, ifname, sizeof(ctx->ifname) - 1);
    ctx->ifname[sizeof(ctx->ifname) - 1] = 0;
    ctx->event = event;
}

// --- 1. Позитивный тест: добавление фильтра ---

static void test_add_filter_success(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    const char *ifaces[] = {"lo", NULL};
    nlmon_filter_t filter = {
        .ifnames = ifaces,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    int rc = nlmon_add_filter(&filter);
    assert(rc == 0 && "add_filter should succeed for valid filter");
    nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

// --- 2. Негативный тест: добавление NULL-фильтра ---

static void test_add_filter_null(void) {
    reset_alloc_counters();
    int rc = nlmon_add_filter(NULL);
    assert(rc == -1 && "add_filter(NULL) should fail");
    PRINT_TEST_PASSED();
}

// --- 3. Негативный тест: добавление фильтра без колбека ---

static void test_add_filter_no_cb(void) {
    reset_alloc_counters();
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = NULL,
        .arg = NULL
    };
    int rc = nlmon_add_filter(&filter);
    assert(rc == -1 && "add_filter with NULL cb should fail");
    PRINT_TEST_PASSED();
}

// --- 4. Граничный тест: превышение лимита фильтров ---

static void test_add_filter_overflow(void) {
    reset_alloc_counters();
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = NULL
    };
    int added = 0;
    for (int i = 0; i < 40; ++i) {
        int rc = nlmon_add_filter(&filter);
        if (rc == 0)
            added++;
        else
            break;
    }
    assert(added == 32 && "should not add more than MAX_FILTERS");
    // cleanup
    for (int i = 0; i < added; ++i)
        nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

// --- 5. Позитивный тест: удаление фильтра ---

static void test_remove_filter_success(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    nlmon_add_filter(&filter);
    int rc = nlmon_remove_filter(&filter);
    assert(rc == 0 && "remove_filter should succeed for existing filter");
    PRINT_TEST_PASSED();
}

// --- 6. Негативный тест: удаление несуществующего фильтра ---

static void test_remove_filter_not_found(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    int rc = nlmon_remove_filter(&filter);
    assert(rc == -1 && "remove_filter for non-existent filter should fail");
    PRINT_TEST_PASSED();
}

// --- 7. Негативный тест: удаление NULL-фильтра ---

static void test_remove_filter_null(void) {
    reset_alloc_counters();
    int rc = nlmon_remove_filter(NULL);
    assert(rc == -1 && "remove_filter(NULL) should fail");
    PRINT_TEST_PASSED();
}

// --- 8. Позитивный тест: list_filters возвращает корректное количество ---

static void test_list_filters_count(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    nlmon_add_filter(&filter);
    int cnt = nlmon_list_filters();
    assert(cnt == 1 && "list_filters should return 1 after add");
    nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

// --- 9. Негативный тест: list_filters с пустым списком ---

static void test_list_filters_empty(void) {
    reset_alloc_counters();
    int cnt = nlmon_list_filters();
    assert(cnt == 0 && "list_filters should return 0 when empty");
    PRINT_TEST_PASSED();
}

// --- 10. Интеграционный стресс-тест: хаотичное добавление/удаление фильтров ---

static void test_chaos_add_remove_filters(void) {
    reset_alloc_counters();
    nlmon_filter_t filters[32];
    cb_ctx_t ctxs[32];
    for (int i = 0; i < 32; ++i) {
        ctxs[i].called = 0;
        filters[i].ifnames = NULL;
        filters[i].events = (i % 2) ? NLMON_EVENT_LINK_UP : NLMON_EVENT_LINK_DOWN;
        filters[i].cb = mock_cb;
        filters[i].arg = &ctxs[i];
    }
    // Случайный порядок добавления и удаления
    int order[32];
    for (int i = 0; i < 32; ++i) order[i] = i;
    srand((unsigned)time(NULL));
    for (int i = 0; i < 32; ++i) {
        int j = rand() % 32;
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    for (int i = 0; i < 32; ++i)
        assert(nlmon_add_filter(&filters[order[i]]) == 0);
    assert(nlmon_list_filters() == 32);
    for (int i = 31; i >= 0; --i)
        assert(nlmon_remove_filter(&filters[order[i]]) == 0);
    assert(nlmon_list_filters() == 0);
    PRINT_TEST_PASSED();
}

// --- 11. Граничный тест: фильтр с пустым списком интерфейсов (NULL) ---

static void test_filter_null_ifnames(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    assert(nlmon_add_filter(&filter) == 0);
    // эмулируем вызов колбека вручную
    filter.cb("eth999", NLMON_EVENT_LINK_UP, filter.arg);
    assert(ctx.called == 1 && "callback should be called even if ifnames is NULL");
    nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

// --- 12. Граничный тест: фильтр с пустым массивом интерфейсов (только NULL) ---

static void test_filter_empty_ifnames_array(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    const char *ifaces[] = {NULL};
    nlmon_filter_t filter = {
        .ifnames = ifaces,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    assert(nlmon_add_filter(&filter) == 0);
    filter.cb("eth0", NLMON_EVENT_LINK_UP, filter.arg);
    assert(ctx.called == 1 && "callback should be called if ifnames is empty array");
    nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

// --- 13. Негативный тест: nlmon_run с пустым списком фильтров ---

static void test_run_no_filters(void) {
    reset_alloc_counters();
    int rc = nlmon_run(1);
    assert(rc == -1 && "nlmon_run with no filters should fail");
    PRINT_TEST_PASSED();
}

// --- 14. Негативный тест: nlmon_run с некорректным таймаутом ---

static void test_run_invalid_timeout(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    nlmon_filter_t filter = {
        .ifnames = NULL,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    nlmon_add_filter(&filter);
    int rc = nlmon_run(0);
    assert(rc == -1 && "nlmon_run with timeout=0 should fail");
    nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

// --- 15. Нестандартный кейс: фильтр с очень длинным именем интерфейса ---

static void test_long_ifname(void) {
    reset_alloc_counters();
    cb_ctx_t ctx = {0};
    char long_ifname[IFNAMSIZ * 2];
    memset(long_ifname, 'A', sizeof(long_ifname) - 1);
    long_ifname[sizeof(long_ifname) - 1] = 0;
    const char *ifaces[] = {long_ifname, NULL};
    nlmon_filter_t filter = {
        .ifnames = ifaces,
        .events = NLMON_EVENT_LINK_UP,
        .cb = mock_cb,
        .arg = &ctx
    };
    assert(nlmon_add_filter(&filter) == 0);
    filter.cb(long_ifname, NLMON_EVENT_LINK_UP, filter.arg);
    assert(ctx.called == 1 && "callback should handle long ifname");
    nlmon_remove_filter(&filter);
    PRINT_TEST_PASSED();
}

int main(int argc, char **argv) {
    struct test_entry tests[] = {
        {"add_filter_success", test_add_filter_success},
        {"add_filter_null", test_add_filter_null},
        {"add_filter_no_cb", test_add_filter_no_cb},
        {"add_filter_overflow", test_add_filter_overflow},
        {"remove_filter_success", test_remove_filter_success},
        {"remove_filter_not_found", test_remove_filter_not_found},
        {"remove_filter_null", test_remove_filter_null},
        {"list_filters_count", test_list_filters_count},
        {"list_filters_empty", test_list_filters_empty},
        {"chaos_add_remove_filters", test_chaos_add_remove_filters},
        {"filter_null_ifnames", test_filter_null_ifnames},
        {"filter_empty_ifnames_array", test_filter_empty_ifnames_array},
        {"run_no_filters", test_run_no_filters},
        {"run_invalid_timeout", test_run_invalid_timeout},
        {"long_ifname", test_long_ifname},
    };
    int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
    if (!rc && argc == 1)
        printf(KGRN "====== All nlmon tests passed! ======\n" KNRM);
    return rc;
}