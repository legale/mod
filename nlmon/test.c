#include "../test_util.h"
#include "nlmon.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

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

// helper: get lo index
static int get_lo_idx(void) {
  int idx = if_nametoindex("lo");
  assert(idx != 0 && "loopback interface not found");
  return idx;
}

// --- 1. Позитивный тест: добавление фильтра ---

static void test_add_filter_success(void) {
  PRINT_TEST_START(__func__);
  reset_alloc_counters();
  cb_ctx_t ctx = {0};
  nlmon_filter_t filter = {
      .idx = {get_lo_idx(), 0},
      .events = NLMON_EVENT_LINK_UP,
      .cb = mock_cb,
      .arg = &ctx};
  int rc = nlmon_add_filter(&filter);
  assert(rc == 0 && "add_filter should succeed for valid filter");
  nlmon_remove_filter(&filter);
  PRINT_TEST_PASSED();
}

// --- 2. Негативный тест: добавление NULL-фильтра ---

static void test_add_filter_null(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  int rc = nlmon_add_filter(NULL);
  assert(rc == EINVAL && "add_filter(NULL) should fail");
  PRINT_TEST_PASSED();
}

// --- 3. Негативный тест: добавление фильтра без колбека ---

static void test_add_filter_no_cb(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  nlmon_filter_t filter = {
      .idx = {0},
      .events = NLMON_EVENT_LINK_UP,
      .cb = NULL,
      .arg = NULL};
  int rc = nlmon_add_filter(&filter);
  assert(rc == EINVAL && "add_filter with NULL cb should fail");
  PRINT_TEST_PASSED();
}

// --- 4. Граничный тест: превышение лимита фильтров ---

static void test_add_filter_overflow(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  nlmon_filter_t filters[NLMON_MAX_FILTERS + 5];
  cb_ctx_t ctxs[NLMON_MAX_FILTERS + 5];
  int added = 0;

  // создаём уникальные фильтры
  for (int i = 0; i < NLMON_MAX_FILTERS + 5; ++i) {
    ctxs[i].called = 0;
    filters[i].idx[0] = i + 1; // уникальный индекс интерфейса
    filters[i].idx[1] = 0;     // null-terminated
    filters[i].events = NLMON_EVENT_LINK_UP;
    filters[i].cb = mock_cb;
    filters[i].arg = &ctxs[i];
  }

  // пытаемся добавить больше чем лимит
  for (int i = 0; i < NLMON_MAX_FILTERS + 5; ++i) {
    int rc = nlmon_add_filter(&filters[i]);
    if (rc == 0) {
      added++;
      PRINT_TEST_INFO("add[%d] success", i);
    } else if (rc == EINVAL) {
      PRINT_TEST_INFO("add[%d] fail: EINVAL (limit reached)", i);
      break;
    } else {
      PRINT_TEST_INFO("add[%d] fail: rc=%d", i, rc);
      assert(0 && "unexpected error");
    }
  }

  PRINT_TEST_INFO("added=%d", added);
  assert(added == NLMON_MAX_FILTERS && "should not add more than MAX_FILTERS");

  // очистка
  for (int i = 0; i < added; ++i)
    nlmon_remove_filter(&filters[i]);

  PRINT_TEST_PASSED();
}

// --- 5. Позитивный тест: удаление фильтра ---

static void test_remove_filter_success(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  cb_ctx_t ctx = {0};
  nlmon_filter_t filter = {
      .idx = {0},
      .events = NLMON_EVENT_LINK_UP,
      .cb = mock_cb,
      .arg = &ctx};
  nlmon_add_filter(&filter);
  int rc = nlmon_remove_filter(&filter);
  assert(rc == 0 && "remove_filter should succeed for existing filter");
  PRINT_TEST_PASSED();
}

// --- 6. Негативный тест: удаление несуществующего фильтра ---

static void test_remove_filter_not_found(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  cb_ctx_t ctx = {0};
  nlmon_filter_t filter = {
      .idx = {0},
      .events = NLMON_EVENT_LINK_UP,
      .cb = mock_cb,
      .arg = &ctx};
  int ret = nlmon_remove_filter(&filter);
  PRINT_TEST_INFO("nlmon_remove_filter ret=%d", ret);
  assert(ret == EINVAL && "remove_filter for non-existent filter should fail");
  PRINT_TEST_PASSED();
}

// --- 7. Негативный тест: удаление NULL-фильтра ---

static void test_remove_filter_null(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  int rc = nlmon_remove_filter(NULL);
  assert(rc == EINVAL && "remove_filter(NULL) should fail");
  PRINT_TEST_PASSED();
}

// --- 8. Позитивный тест: list_filters возвращает корректное количество ---

static void test_list_filters_count(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  cb_ctx_t ctx = {0};
  nlmon_filter_t filter = {
      .idx = {0},
      .events = NLMON_EVENT_LINK_UP,
      .cb = mock_cb,
      .arg = &ctx};
  nlmon_add_filter(&filter);
  int cnt = nlmon_list_filters();
  assert(cnt == 1 && "list_filters should return 1 after add");
  nlmon_remove_filter(&filter);
  PRINT_TEST_PASSED();
}

// --- 9. Негативный тест: list_filters с пустым списком ---

static void test_list_filters_empty(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  int cnt = nlmon_list_filters();
  assert(cnt == 0 && "list_filters should return 0 when empty");
  PRINT_TEST_PASSED();
}

// --- 10. Интеграционный стресс-тест: хаотичное добавление/удаление фильтров ---

static void test_chaos_add_remove_filters(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  nlmon_filter_t filters[NLMON_MAX_FILTERS];
  cb_ctx_t ctxs[NLMON_MAX_FILTERS];

  for (int i = 0; i < NLMON_MAX_FILTERS; ++i) {
    ctxs[i].called = 0;
    filters[i].idx[0] = i + 1; // уникальный индекс интерфейса
    filters[i].idx[1] = 0;     // null-terminated
    filters[i].events = (i % 2) ? NLMON_EVENT_LINK_UP : NLMON_EVENT_LINK_DOWN;
    filters[i].cb = mock_cb;
    filters[i].arg = &ctxs[i];
  }

  srand((unsigned)time(NULL));

  for (int i = 0; i < 1000; ++i) {
    int idx = rand() % NLMON_MAX_FILTERS;
    int action = rand() % 2;

    if (action == 0) { // add
      int rc = nlmon_add_filter(&filters[idx]);
      if (rc == 0) {
        PRINT_TEST_INFO("add[%d] success", idx);
      } else if (rc == EEXIST) {
        PRINT_TEST_INFO("add[%d] skipped: already exists", idx);
      } else if (rc == EINVAL) {
        PRINT_TEST_INFO("add[%d] fail: limit reached", idx);
      } else {
        PRINT_TEST_INFO("add[%d] fail: rc=%d", idx, rc);
        assert(0 && "unexpected error in add");
      }
    } else { // remove
      int rc = nlmon_remove_filter(&filters[idx]);
      if (rc == 0) {
        PRINT_TEST_INFO("remove[%d] success", idx);
      } else if (rc == EINVAL) {
        PRINT_TEST_INFO("remove[%d] skipped: not found", idx);
      } else {
        PRINT_TEST_INFO("remove[%d] fail: rc=%d", idx, rc);
        assert(0 && "unexpected error in remove");
      }
    }

    int cnt = nlmon_list_filters();
    assert(cnt >= 0 && cnt <= NLMON_MAX_FILTERS && "filter count out of range");
  }

  // финальная очистка
  nlmon_clear_filters();

  int cnt = nlmon_list_filters();
  PRINT_TEST_INFO("final cnt=%d", cnt);
  assert(cnt == 0 && "filters not fully cleared");
  PRINT_TEST_PASSED();
}

// --- 11. Граничный тест: фильтр с пустым idx ---

static void test_filter_null_idx(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  cb_ctx_t ctx = {0};
  nlmon_filter_t filter = {
      .idx = {0},
      .events = NLMON_EVENT_LINK_UP,
      .cb = mock_cb,
      .arg = &ctx};
  assert(nlmon_add_filter(&filter) == 0);
  filter.cb("eth999", NLMON_EVENT_LINK_UP, filter.arg);
  assert(ctx.called == 1);
  nlmon_remove_filter(&filter);
  PRINT_TEST_PASSED();
}

// --- 13. Негативный тест: nlmon_run с пустым списком фильтров ---

static void test_run_no_filters(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  int rc = nlmon_run();
  assert(rc == 0 && "nlmon_run should succeed even if no filters");
  nlmon_stop();
  PRINT_TEST_PASSED();
}

// --- 14. Негативный тест: nlmon_run с некорректным таймаутом (устарело) ---

static void test_run_invalid_timeout(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  PRINT_TEST_PASSED(); // таймаут больше не используется
}

// --- 16. Негативный тест: двойной nlmon_run ---

static void test_double_run(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  assert(nlmon_run() == 0 && "first nlmon_run should succeed");
  int rc = nlmon_run();
  assert(rc == -1 && "second nlmon_run should fail (already running)");
  nlmon_stop();
  PRINT_TEST_PASSED();
}

// --- 17. Негативный тест: двойной nlmon_stop ---

static void test_double_stop(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  assert(nlmon_run() == 0 && "nlmon_run should succeed");
  nlmon_stop();
  nlmon_stop(); // второй stop не должен падать
  PRINT_TEST_PASSED();
}

// --- 18. Негативный тест: nlmon_stop без nlmon_run ---

static void test_stop_without_run(void) {
  reset_alloc_counters();
  nlmon_stop(); // должен быть no-op
  PRINT_TEST_PASSED();
}

// --- 19. Позитивный тест: nlmon_run после stop ---

static void test_run_after_stop(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  assert(nlmon_run() == 0 && "first nlmon_run should succeed");
  nlmon_stop();
  assert(nlmon_run() == 0 && "nlmon_run after stop should succeed again");
  nlmon_stop();
  PRINT_TEST_PASSED();
}

// --- 20. Интеграционный стресс-тест: конкурентные add/remove при работе воркера ---
static void test_concurrent_add_remove(void) {
  PRINT_TEST_START(__func__);

  reset_alloc_counters();
  assert(nlmon_run() == 0 && "nlmon_run should succeed");

  nlmon_filter_t filters[NLMON_MAX_FILTERS];
  cb_ctx_t ctxs[NLMON_MAX_FILTERS];
  for (int i = 0; i < NLMON_MAX_FILTERS; ++i) {
    ctxs[i].called = 0;

    filters[i].events = NLMON_EVENT_LINK_UP;
    filters[i].cb = mock_cb;
    filters[i].arg = &ctxs[i];
  }

  for (int i = 0; i < NLMON_MAX_FILTERS; ++i)
    assert(nlmon_add_filter(&filters[i]) == 0);
  assert(nlmon_list_filters() == NLMON_MAX_FILTERS);

  for (int i = 0; i < NLMON_MAX_FILTERS; ++i)
    assert(nlmon_remove_filter(&filters[i]) == 0);
  assert(nlmon_list_filters() == 0);

  nlmon_stop();
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
      {"filter_null_idx", test_filter_null_idx},
      {"run_no_filters", test_run_no_filters},
      {"run_invalid_timeout", test_run_invalid_timeout},
      {"double_run", test_double_run},
      {"double_stop", test_double_stop},
      {"stop_without_run", test_stop_without_run},
      {"run_after_stop", test_run_after_stop},
      {"concurrent_add_remove", test_concurrent_add_remove},
  };
  int rc = run_named_test(argc > 1 ? argv[1] : NULL, tests, ARRAY_SIZE(tests));
  if (!rc && argc == 1)
    PRINT_TEST_INFO_GREEN("====== All nlmon tests passed! ======");
  return rc;
}
