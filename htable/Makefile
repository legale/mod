CC = gcc
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb
AR = ar
ARFLAGS = rcs
LIBNAME = libhtable.a
OBJ = htable.o
TEST_OBJ = test.o
MAIN_OBJ = main.o
LDFLAGS = -L.
LIBS = -lhtable

COV_CFLAGS = $(CFLAGS) -O0 -g -fprofile-arcs -ftest-coverage
COV_LDFLAGS = $(LDFLAGS) -fprofile-arcs -ftest-coverage

all: $(LIBNAME) main test

$(LIBNAME): $(OBJ)
	$(AR) $(ARFLAGS) $(LIBNAME) $(OBJ)

htable.o: htable.c htable.h
	$(CC) $(CFLAGS) -c htable.c

main: $(MAIN_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(MAIN_OBJ) -o main $(LDFLAGS) $(LIBS)

test: $(TEST_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(TEST_OBJ) -o test $(LDFLAGS) $(LIBS)
	./test

main.o: main.c htable.h
	$(CC) $(CFLAGS) -c main.c

test.o: test.c htable.h
	$(CC) $(CFLAGS) -c test.c

perf: test
	@echo "Running valgrind callgrind profiler..."
	valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./test
	callgrind_annotate --inclusive=yes --auto=yes callgrind.out

leak: test
	@echo "Running valgrind memcheck for memory leaks..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test

clean:
	rm -f *.o *.gcov *.gcno *.gcda $(LIBNAME) main test callgrind.out

coverage: clean
	$(MAKE) CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" LIBS="$(LIBS)" test
	@echo "=== Coverage report (gcov): ==="
	@gcov htable.c | grep -A10 "File 'htable.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b htable.c | grep -E 'Lines executed|Branches executed'

.PHONY: all clean perf coverage
