NAME = netlink_getlink
CC ?= gcc
CFLAGS_BASE = -Wall -Wextra -Werror -Wno-unused-parameter \
              -O2 -g -fvisibility=hidden -fno-common -ffunction-sections -fdata-sections
CFLAGS = $(CFLAGS_BASE) -fPIC
LDFLAGS = -Wl,--gc-sections

# зависимости (timeutil syslog2)
MODULES = ../timeutil ../syslog2

SRC = $(filter-out main.c test.c, $(wildcard *.c))
HDR = $(wildcard *.h)

OBJ_STATIC = $(SRC:.c=.o)
OBJ_SHARED = $(SRC:.c=_shared.o)

TARGET_LIB = lib$(NAME).a
TARGET_SO  = lib$(NAME).so
TARGET_TEST = test
TARGET_MAIN = main

.PHONY: all clean $(MODULES)

all: $(MODULES) $(TARGET_LIB) $(TARGET_SO) $(TARGET_TEST) $(TARGET_MAIN)

# Собираем зависимости
$(MODULES):
	$(MAKE) -C $@

# static library
$(TARGET_LIB): $(OBJ_STATIC)
	ar rcs $@ $^

# shared library (жёсткая зависимость на зависимости)
$(TARGET_SO): $(OBJ_SHARED)
	$(CC) $(CFLAGS) -shared -o $@ $(OBJ_SHARED) \
	$(addprefix -L,$(MODULES)) $(addprefix -l,$(notdir $(MODULES))) $(LDFLAGS)

# test binary (static)
$(TARGET_TEST): test.o $(TARGET_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# main binary (linked with .so)
$(TARGET_MAIN): main.o $(TARGET_SO)
	$(CC) $(CFLAGS) -o $@ $^ -L. -l$(NAME) \
	$(addprefix -L,$(MODULES)) $(addprefix -l,$(notdir $(MODULES))) $(LDFLAGS)

# static .o
%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

# shared .o
%_shared.o: %.c $(HDR)
	$(CC) $(CFLAGS) -DIS_DYNAMIC_LIB -c $< -o $@

clean:
	rm -f $(OBJ_STATIC) $(OBJ_SHARED) *.o *.a *.so $(TARGET_TEST) $(TARGET_MAIN)
	$(foreach mod,$(MODULES),$(MAKE) -C $(mod) clean;)
