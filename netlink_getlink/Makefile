CC = gcc
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb
AR = ar
ARFLAGS = rcs
	LIBNAME = libnl_getlink.a
OBJ = libnl_getlink.o
TEST_OBJ = test.o
MAIN_OBJ = main.o

LDFLAGS = -L. -L../syslog2 -L../timeutil
LIBS = -lnl_getlink -lsyslog2 -ltimeutil

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

all: $(LIBNAME) main test

../syslog2/libsyslog2.a:
	$(MAKE) -C ../syslog2 all

$(LIBNAME): $(OBJ)
	$(AR) $(ARFLAGS) $(LIBNAME) $(OBJ)

libnl_getlink.o: libnl_getlink.c libnl_getlink.h slist.h ../syslog2/syslog2.h
	$(CC) $(CFLAGS) -c libnl_getlink.c


main: $(MAIN_OBJ) $(LIBNAME) ../syslog2/libsyslog2.a
	$(CC) $(CFLAGS) $(MAIN_OBJ) -o main $(LDFLAGS) $(LIBS)

test: $(TEST_OBJ) $(LIBNAME) ../syslog2/libsyslog2.a
	$(CC) $(CFLAGS) $(TEST_OBJ) -o test $(LDFLAGS) $(LIBS)
	./test

main.o: main.c libnl_getlink.h slist.h ../syslog2/syslog2.h
	$(CC) $(CFLAGS) -c main.c

test.o: test.c libnl_getlink.h slist.h ../syslog2/syslog2.h
	$(CC) $(CFLAGS) -c test.c

.PRECIOUS: test

perf: test
	@echo "Running valgrind callgrind profiler..."
	valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./test
	callgrind_annotate --inclusive=yes --auto=yes callgrind.out | head -n60

leak: test
	@echo "Running valgrind memcheck for memory leaks..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test

clean:
	rm -f *.o *.gcov *.gcno *.gcda $(LIBNAME) main test callgrind.out

coverage: clean
	$(MAKE) CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" LIBS="$(LIBS)" test
	@echo "=== Coverage report (gcov): ==="
	@gcov libnl_getlink.c | grep -A10 "File 'libnl_getlink.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b libnl_getlink.c | grep -E 'Lines executed|Branches executed'

.PHONY: all clean perf leak
