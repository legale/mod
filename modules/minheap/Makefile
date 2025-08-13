# include common flags
include ../../build/common.mk

MOD := minheap
SRCS := $(wildcard src/*.c)
HDRS := $(wildcard include/*.h)
TEST_SRCS := $(wildcard tests/unit_*.c) tests/test_runner.c
THR_SRCS := $(wildcard tests/thread_*.c) tests/test_runner.c
STR_SRCS := $(wildcard tests/stress_*.c) tests/test_runner.c

INC := -Iinclude -Itests
CFLAGS += $(INC)
LDFLAGS += -lpthread

ALL_OBJS := $(SRCS:.c=.o)

all: release

release: CFLAGS += $(CFLAGS_REL)
release: $(MOD).a

debug: CFLAGS += $(CFLAGS_DBG)
debug: $(MOD)_dbg.a

asan: CFLAGS += $(CFLAGS_ASAN)
asan: $(MOD)_asan.a

ubsan: CFLAGS += $(CFLAGS_UBSAN)
ubsan: $(MOD)_ubsan.a

tsan: CFLAGS += $(CFLAGS_TSAN)
tsan: $(MOD)_tsan.a

$(MOD).a: $(ALL_OBJS)
	$(AR) rcs $@ $^

$(MOD)_dbg.a: $(ALL_OBJS)
	$(AR) rcs $@ $^

$(MOD)_asan.a: $(ALL_OBJS)
	$(AR) rcs $@ $^

$(MOD)_ubsan.a: $(ALL_OBJS)
	$(AR) rcs $@ $^

$(MOD)_tsan.a: $(ALL_OBJS)
	$(AR) rcs $@ $^

test: debug
	$(CC) $(CFLAGS) $(CFLAGS_DBG) $(TEST_SRCS) -o out/test_unit $(LDFLAGS) $(MOD)_dbg.a
	./out/test_unit --tap --junit out/junit_unit.xml

test-thread: tsan
	$(CC) $(CFLAGS) $(CFLAGS_TSAN) $(THR_SRCS) -o out/test_thread $(LDFLAGS) $(MOD)_tsan.a
	./out/test_thread --tap --junit out/junit_thread.xml

test-stress: debug
	$(CC) $(CFLAGS) $(CFLAGS_DBG) $(STR_SRCS) -o out/test_stress $(LDFLAGS) $(MOD)_dbg.a
	./out/test_stress --tap --junit out/junit_stress.xml

helgrind: debug
	valgrind --tool=helgrind --error-exitcode=1 ./out/test_thread

demo: release
	$(CC) $(CFLAGS) demo/main.c -o out/demo $(LDFLAGS) $(MOD).a

clean:
	$(RM) -r out */*.o *.a

.PHONY: all release debug asan ubsan tsan test test-thread test-stress helgrind demo clean
