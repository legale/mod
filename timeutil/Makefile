NAME = timeutil
CC ?= gcc
CFLAGS_BASE = -Wall -Wextra -Werror -Wno-unused-parameter -O2 -g -fvisibility=hidden -fno-common -ffunction-sections -fdata-sections 
CFLAGS = $(CFLAGS_BASE) -fPIC -flto
LDFLAGS = -Wl,--gc-sections

# зависимости
MODULES = 

EXTRA_LIBS = 

SRC = $(filter-out main.c test.c, $(wildcard *.c))
HDR = $(wildcard *.h)

OBJ_STATIC = $(SRC:.c=.o)
OBJ_SHARED = $(SRC:.c=_shared.o)

TARGET_LIB = lib$(NAME).a
TARGET_SO  = lib$(NAME).so
TARGET_TEST = test
TARGET_MAIN = main

.PHONY: run all clean perf leak coverage $(MODULES)

all: $(MODULES) $(TARGET_LIB) $(TARGET_SO) $(TARGET_MAIN)

# Собираем зависимости
$(MODULES):
	$(MAKE) -C $@

# static library
$(TARGET_LIB): $(OBJ_STATIC)
	ar rcs $@ $^

# shared library (жёсткая зависимость на зависимости)
$(TARGET_SO): $(OBJ_SHARED) $(MODULES)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJ_SHARED) \
	$(addprefix -L,$(MODULES)) $(addprefix -l,$(notdir $(MODULES))) $(LDFLAGS) \
	$(addprefix -l,$(EXTRA_LIBS)) $(LDFLAGS)

# test binary (static)
$(TARGET_TEST): test.o $(TARGET_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) \
	$(addprefix -l,$(EXTRA_LIBS)) $(LDFLAGS)
	./test

space := $(empty) $(empty)

# main binary (linked with .so)
$(TARGET_MAIN): main.o $(TARGET_SO) $(MODULES)
	$(CC) $(CFLAGS) -o $@ main.o $(TARGET_SO) -L. -l$(NAME) $(addprefix -L,$(MODULES)) $(addprefix -l,$(notdir $(MODULES))) $(addprefix -l,$(EXTRA_LIBS)) $(LDFLAGS)

run:
	LD_LIBRARY_PATH=.:$(subst $(space),:,$(strip $(MODULES))) ./$(TARGET_MAIN)

# static .o
%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

# shared .o
%_shared.o: %.c $(HDR)
	$(CC) $(CFLAGS) -DIS_DYNAMIC_LIB -c $< -o $@

clean:
	rm -f callgrind.out *.gcov *.gcno $(OBJ_STATIC) $(OBJ_SHARED) *.o *.a *.so $(TARGET_TEST) $(TARGET_MAIN)
	$(foreach mod,$(MODULES),$(MAKE) -C $(mod) clean;)

# ------------------ профилирование и утечки ------------------

perf: $(TARGET_TEST)
	@echo "Running valgrind callgrind profiler..."
	valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./$(TARGET_TEST)
	callgrind_annotate --inclusive=yes --auto=yes callgrind.out | head -n60

leak: $(TARGET_TEST)
	@echo "Running valgrind memcheck for memory leaks..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET_TEST)

# ------------------ покрытие ------------------
COV_CFLAGS = --coverage -O0 -g
COV_LDFLAGS = --coverage
COV_SRCS = $(SRC)

coverage: clean
	$(MAKE) CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done
	@echo "=== Coverage report (gcov): ==="
	@for f in $(COV_SRCS); do \
		gcov "$$f" | grep -A10 "File '$$f'"; \
	done
	@echo "=== Coverage report (summary): ==="
	@for f in $(COV_SRCS); do \
		gcov -b "$$f" | grep -E 'Lines executed|Branches executed'; \
	done

