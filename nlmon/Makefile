CC = gcc
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb -I. -I../timeutil -I../syslog2
AR = ar
ARFLAGS = rcs
LIBNAME = libnlmon.a
OBJ = nlmon.o
SRCS = nlmon.c
TEST_OBJ = nlmon_test.o
TEST_SRCS = nlmon_test.c
MAIN_SRCS = $(wildcard nlmon_main.c)
MAIN_OBJ = $(MAIN_SRCS:.c=.o)
LDFLAGS = -L. -L../timeutil -L../syslog2
LIBS = -lnlmon -ltimeutil -lsyslog2

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

all: dependencies $(LIBNAME) test $(if $(MAIN_OBJ), main)

dependencies:
	$(MAKE) -C ../timeutil
	$(MAKE) -C ../syslog2 all

$(LIBNAME): $(OBJ)
	$(AR) $(ARFLAGS) $(LIBNAME) $(OBJ)

%.o: %.c nlmon.h
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
	$(MAKE) CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" LIBS="$(LIBS)" test
	@echo "=== Coverage report (gcov): ==="
	@gcov nlmon.c | grep -A10 "File 'nlmon.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b nlmon.c | grep -E 'Lines executed|Branches executed'

.PHONY: all clean perf leak coverage test main dependencies
