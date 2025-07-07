CC = gcc
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb
AR = ar
ARFLAGS = rcs
LIBNAME = libleak_detector.a
OBJ = leak_detector.o
TEST_OBJ = test.o
MAIN_OBJ = main.o

LDFLAGS = -L.
LIBS = -lleak_detector

COV_CFLAGS = $(CFLAGS) -O0 -g -fprofile-arcs -ftest-coverage
COV_LDFLAGS = $(LDFLAGS) -fprofile-arcs -ftest-coverage

ifeq ($(LEAKCHECK),1)
  CFLAGS += -DLEAKCHECK
  COV_CFLAGS += -DLEAKCHECK
endif

all: $(LIBNAME) main test

$(LIBNAME): $(OBJ)
	$(AR) $(ARFLAGS) $(LIBNAME) $(OBJ)

leak_detector.o: leak_detector.c leak_detector.h
	$(CC) $(CFLAGS) -c leak_detector.c

main: $(MAIN_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(MAIN_OBJ) -o main $(LDFLAGS) $(LIBS)

test: $(TEST_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(TEST_OBJ) -o test $(LDFLAGS) $(LIBS)
	./test

main.o: main.c leak_detector.h
	$(CC) $(CFLAGS) -c main.c

test.o: test.c leak_detector.h
	$(CC) $(CFLAGS) -c test.c

clean:
	rm -f *.o *.gc* $(LIBNAME) main test

coverage: clean
	$(MAKE) CFLAGS="$(COV_CFLAGS)" LDFLAGS="$(COV_LDFLAGS)" LIBS="$(LIBS)" test
	@echo "=== Coverage report (gcov): ==="
	@gcov leak_detector.c | grep -A10 "File 'leak_detector.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b leak_detector.c | grep -E 'Lines executed|Branches executed'
	
.PHONY: all clean coverage
