CC = gcc
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb -I../timeutil
AR = ar
ARFLAGS = rcs
LIBNAME = libsyslog2.a
OBJ = syslog2.o
SRCS = $(OBJ:.o=.c)
TEST_OBJ = test.o
TEST_SRCS = $(TEST_OBJ:.o=.c)
MAIN_SRCS = $(wildcard main.c)
MAIN_OBJ = $(MAIN_SRCS:.c=.o)
LDFLAGS = -L. -L../timeutil
LIBS = -lsyslog2 -ltimeutil

COV_CFLAGS = $(CFLAGS) -O0 -g -fprofile-arcs -ftest-coverage
COV_LDFLAGS = $(LDFLAGS) -fprofile-arcs -ftest-coverage

ifeq ($(DEBUG),1)
  CFLAGS += -O0 -DDEBUG=1 -g
  COV_CFLAGS += -O0 -DDEBUG=1 -g
endif

ifeq ($(TESTRUN),1)
  CFLAGS += -DTESTRUN=1
  COV_CFLAGS += -DTESTRUN=1
endif

ifeq ($(MAKECMDGOALS),coverage)
  CFLAGS += -DTESTRUN=1
  COV_CFLAGS += -DTESTRUN=1
endif

all: $(LIBNAME) test $(if $(MAIN_OBJ), main)

$(LIBNAME): $(OBJ)
	$(AR) $(ARFLAGS) $(LIBNAME) $(OBJ)

%.o: %.c syslog2.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(TEST_OBJ) -o test $(LDFLAGS) $(LIBS)
	./test

main: $(MAIN_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(MAIN_OBJ) -o main $(LDFLAGS) $(LIBS)

.PRECIOUS: test main

perf: test
	@echo "Running valgrind callgrind profiler..."
	valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./test
	callgrind_annotate --inclusive=yes --auto=yes callgrind.out | head -n60

leak: test
	@echo "Running valgrind memcheck for memory leaks..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test

clean:
	rm -f *.o *.gcov *.gcno *.gcda $(LIBNAME) test main callgrind.out

coverage: clean
	$(MAKE) DEBUG=1 CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" LIBS="$(LIBS)" test
	@echo "=== Coverage report (gcov): ==="
	@gcov syslog2.c | grep -A10 "File 'syslog2.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b syslog2.c | grep -E 'Lines executed|Branches executed'

.PHONY: all clean perf leak coverage test main
