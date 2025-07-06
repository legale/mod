# Имя исходных файлов и заголовков
SRC = uevent.c uevent_worker.c ../minheap/minheap.c ../timeutil/timeutil.c ../syslog2/syslog2.c
HDR = uevent.h uevent_worker.h uevent_internal.h ../minheap/minheap.h ../minheap/minheap_internal.h ../timeutil/timeutil.h ../syslog2/syslog2.h

# Исходные файлы для тестов
TEST_SRC = test.c
ROBUST_TEST_SRC = robust_test.c

# Флаги компиляции
ifeq ($(DEBUG),1)
  CFLAGS += -O0 -DDEBUG=1 -g
endif
CFLAGS += -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -g
CFLAGS_SO = $(CFLAGS) -fPIC

# <<< НОВЫЕ ФЛАГИ: Специальные флаги для тестов с санитайзерами
# -O1 рекомендуется для санитайзеров, чтобы не оптимизировать слишком много
# -g обязательно для получения читаемых стектрейсов
CFLAGS_TEST = -fsanitize=thread,undefined -g
LDFLAGS_TEST = -ltsan -lubsan

# Флаги линковки
LDFLAGS = -lpthread
PERF_LDFLAGS = $(LDFLAGS) -levent -luv

# Имена целей
MAIN = main_bin
TEST_BIN = test_bin
ROBUST_TEST_BIN = robust_test_bin 
LIB = libuevent.so
PERF = perftest

# Объектные файлы для библиотеки
OBJS = $(patsubst %.c,%.o,$(SRC))

# Объектные файлы для main, test и perf
MAIN_OBJS = main.o $(OBJS)
TEST_OBJS = $(patsubst %.c,%.o,$(TEST_SRC)) $(OBJS)
ROBUST_TEST_OBJS = $(patsubst %.c,%.o,$(ROBUST_TEST_SRC)) $(OBJS) 
PERF_OBJS = perftest.o $(OBJS)


ifeq ($(MAKECMDGOALS),robust_test)
	CFLAGS := $(CFLAGS) $(CFLAGS_TEST)
	LDFLAGS := $(LDFLAGS) $(LDFLAGS_TEST)
else ifeq ($(MAKECMDGOALS),simple)
	CFLAGS := $(CFLAGS)
	LDFLAGS := $(LDFLAGS)
else ifeq ($(MAKECMDGOALS),perf)
	CFLAGS := $(CFLAGS) $(CFLAGS_TEST)
	LDFLAGS := $(LDFLAGS) $(LDFLAGS_TEST)
else
	CFLAGS := $(CFLAGS) $(CFLAGS_TEST)
	LDFLAGS := $(LDFLAGS) $(LDFLAGS_TEST)
endif

# Цели по умолчанию
all: $(MAIN) $(LIB)

simple: $(MAIN) $(LIB)

# Компиляция основного приложения
$(MAIN): $(MAIN_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Компиляция ОБЫЧНОГО тестового бинарного файла
$(TEST_BIN): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Компиляция ТЕСТА НА НАДЕЖНОСТЬ со специальными флагами
$(ROBUST_TEST_BIN): $(ROBUST_TEST_OBJS)
	$(CC) $(CFLAGS) $(CFLAGS_TEST) -o $@ $^ $(LDFLAGS)

# --- Правила для запуска тестов ---

# Запуск обычных тестов
test: $(TEST_BIN)
	./$(TEST_BIN)

# Запуск тестов на надежность
robust_test: $(ROBUST_TEST_BIN)
	./$(ROBUST_TEST_BIN)

# Запуск ВСЕХ тестов
check: test robust_test

# --- Остальные цели ---

$(PERF): $(PERF_OBJS)
	$(CC) $(CFLAGS) -o $@ $(PERF_OBJS) $(PERF_LDFLAGS)

perf: $(PERF)
	./$(PERF)

profile: $(TEST_BIN)
	@echo "Running valgrind callgrind profiler..."
	valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./$(TEST_BIN)
	callgrind_annotate --auto=yes callgrind.out

# Компиляция динамической библиотеки
$(LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

# Правило для компиляции .c файлов
%.o: %.c $(HDR)
	$(CC) $(CFLAGS_SO) $(CFLAGS) -c $< -o $@


# Очистка скомпилированных файлов
clean:
	rm -f $(MAIN) $(TEST_BIN) $(ROBUST_TEST_BIN) $(PERF) $(LIB) callgrind.out *.o ../syslog2/*.o ../timeutil/*.o ../minheap/*.o ../*.o

.PHONY: all test robust_test check clean
