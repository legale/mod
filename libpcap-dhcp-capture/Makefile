CC = gcc
CFLAGS = -Wall -Wextra -O3 -Wno-unused-parameter -ffunction-sections -fdata-sections -ggdb
AR = ar
ARFLAGS = rcs
LIBNAME = libpcap_dhcp.a
OBJ = pcap_dhcp.o
TEST_OBJ = test.o
MAIN_OBJ = main.o
LDFLAGS = -L. -L../syslog2
LIBS = -lpcap_dhcp -lpcap -lsyslog2

COV_CFLAGS = \
  $(CFLAGS) -O0 -g -fprofile-arcs -ftest-coverage
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

pcap_dhcp.o: pcap_dhcp.c pcap_dhcp.h ../syslog2/syslog2.h
        $(CC) $(CFLAGS) -c pcap_dhcp.c


main: $(MAIN_OBJ) $(LIBNAME)
	$(CC) $(CFLAGS) $(MAIN_OBJ) -o main $(LDFLAGS) $(LIBS)

test: $(TEST_OBJ) $(LIBNAME)
        $(CC) $(CFLAGS) $(TEST_OBJ) -o test $(LDFLAGS) $(LIBS)
        ./test $(TEST)

main.o: main.c pcap_dhcp.h ../syslog2/syslog2.h
        $(CC) $(CFLAGS) -c main.c

test.o: test.c pcap_dhcp.h
	$(CC) $(CFLAGS) -c test.c

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
	@gcov pcap_dhcp.c | grep -A10 "File 'pcap_dhcp.c'"
	@echo "=== Coverage report (summary): ==="
	@gcov -b pcap_dhcp.c | grep -E 'Lines executed|Branches executed'

.PHONY: all clean perf leak coverage
