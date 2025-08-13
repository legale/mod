BUILD ?= debug
include build/common.mk

LIB = build/$(BUILD)/libminheap.a

all: release

lib: $(LIB)

build/$(BUILD):
	mkdir -p $@

build/$(BUILD)/minheap.o: src/minheap.c | build/$(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): build/$(BUILD)/minheap.o
	ar rcs $@ $^

debug: BUILD=debug
debug: lib

release: BUILD=release
release: lib

asan: BUILD=asan
asan: lib

ubsan: BUILD=ubsan
ubsan: lib

tsan: BUILD=tsan
tsan: lib

DEMO = build/$(BUILD)/demo
demo: debug $(DEMO)
$(DEMO): demo/main.c lib | build/$(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) demo/main.c $(LIB) -o $@ $(LDFLAGS)

UNIT = build/$(BUILD)/test_unit
$(UNIT): tests/unit_heap.c tests/test_runner.c lib | build/$(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ tests/unit_heap.c tests/test_runner.c $(LIB) $(LDFLAGS)

test: debug $(UNIT)
	scripts/test.sh $(UNIT)

THREAD_BIN = build/$(BUILD)/test_thread
$(THREAD_BIN): tests/thread_basic.c tests/test_runner.c lib | build/$(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -DTEST_THREAD -o $@ tests/thread_basic.c tests/test_runner.c $(LIB) $(LDFLAGS)

test-thread:
	$(MAKE) BUILD=tsan lib
	$(MAKE) BUILD=tsan build/tsan/test_thread
	scripts/test.sh build/tsan/test_thread

helgrind:
	$(MAKE) BUILD=debug lib
	$(MAKE) BUILD=debug build/debug/test_thread
	valgrind --tool=helgrind build/debug/test_thread

STRESS_BIN = build/$(BUILD)/test_stress
$(STRESS_BIN): tests/stress_random.c tests/test_runner.c lib | build/$(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -DTEST_STRESS -o $@ tests/stress_random.c tests/test_runner.c $(LIB) $(LDFLAGS)

test-stress: debug $(STRESS_BIN)
	scripts/test.sh $(STRESS_BIN)

clean:
	find build -mindepth 1 -maxdepth 1 ! -name common.mk -exec rm -rf {} +

.PHONY: all debug release asan ubsan tsan lib test test-thread test-stress helgrind demo clean
