CC ?= gcc
CFLAGS =	-Wall -Wextra -Werror -Wno-unused-parameter \
			-O2 -g \
         	-fvisibility=hidden -fno-common -ffunction-sections -fdata-sections
LDFLAGS = -lpthread -Wl,--gc-sections

SRC = syslog2.c
HDR = syslog2.h
OBJ = $(SRC:.c=.o)

TEST_SRC = test.c
TEST_OBJ = $(TEST_SRC:.c=.o)

MAIN_SRC = main.c
MAIN_OBJ = $(MAIN_SRC:.c=.o)

# timeutil как отдельная библиотека
TIMEUTIL_PATH = ../timeutil
TIMEUTIL_SRC = $(TIMEUTIL_PATH)/timeutil.c
TIMEUTIL_OBJ = $(TIMEUTIL_SRC:.c=.o)
TIMEUTIL_INC = -I$(TIMEUTIL_PATH)
TIMEUTIL_LIB = $(TIMEUTIL_PATH)/libtimeutil.a

TARGET_LIB = libsyslog2.a
TARGET_TEST = test
TARGET_MAIN = main

.PHONY: all clean

all: $(TARGET_LIB) $(TIMEUTIL_LIB) $(TARGET_TEST) $(TARGET_MAIN)

$(TARGET_LIB): $(OBJ)
	ar rcs $@ $^

$(TIMEUTIL_LIB): $(TIMEUTIL_OBJ)
	ar rcs $@ $^

$(TARGET_TEST): $(TEST_OBJ) $(TARGET_LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJ) $(TARGET_LIB) $(LDFLAGS)

$(TARGET_MAIN): $(MAIN_OBJ) $(OBJ) $(TIMEUTIL_OBJ)
	$(CC) $(CFLAGS) $(TIMEUTIL_INC) -o $@ $(MAIN_OBJ) $(OBJ) $(TIMEUTIL_OBJ) $(LDFLAGS)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) $(TIMEUTIL_INC) -c $< -o $@

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(MAIN_OBJ) $(TIMEUTIL_OBJ) $(TARGET_LIB) $(TIMEUTIL_LIB) $(TARGET_TEST) $(TARGET_MAIN)