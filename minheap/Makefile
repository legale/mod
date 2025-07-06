CC = gcc
# Добавляем путь к заголовкам
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb
AR = ar
ARFLAGS = rcs
LIBNAME = libminheap.a
OBJ = minheap.o
TEST_OBJ = test.o
MAIN_OBJ = main.o 

LDFLAGS = -L. -L../syslog2 -L../timeutil
LIBS = -lminheap -lsyslog2 -ltimeutil

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


$(LIBNAME): $(OBJ)
	$(AR) $(ARFLAGS) $(LIBNAME) $(OBJ)

minheap.o: minheap.c minheap.h minheap_internal.h
	$(CC) $(CFLAGS) -c minheap.c 


main: main.o $(LIBNAME)
	$(CC) $(CFLAGS) main.o -o main $(LDFLAGS) $(LIBS)


#чтобы не удалять бинарник теста
.PRECIOUS: test

test: $(TEST_OBJ) $(LIBNAME) ../syslog2/libsyslog2.a
	$(CC) $(CFLAGS) $(TEST_OBJ) -o test $(LDFLAGS) $(LIBS)
	./test

../syslog2/libsyslog2.a:
	$(MAKE) -C ../syslog2

main.o: main.c minheap.h
	$(CC) $(CFLAGS) -c main.c 

test.o: test.c minheap.h
	$(CC) $(CFLAGS) -c test.c


perf: test
	@echo "Running valgrind callgrind profiler..."
	valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./test
	callgrind_annotate --inclusive=yes --auto=yes callgrind.out | head -n60

leak: test
	@echo "Running valgrind memcheck for memory leaks..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test

clean:
	rm -f *.o *.gcov *.gcno *.gcda $(LIBNAME) main test callgrind.out $(TEST_OBJ)
	# clean module

# ------------------ coverage ------------------
coverage: clean
	$(MAKE) CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" LIBS="$(LIBS)" test
	@echo "=== Coverage report (gcov): ==="
	@gcov minheap.c | grep -A10 "File 'minheap.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b minheap.c | grep -E 'Lines executed|Branches executed'


.PHONY: all clean perf leak