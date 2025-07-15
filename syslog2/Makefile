NAME        = syslog2
CC         ?= gcc
CFLAGS_BASE = -Wall -Wextra -Werror -Wno-unused-parameter \
              -O2 -g -fvisibility=hidden -fno-common -ffunction-sections -fdata-sections
CFLAGS      = $(CFLAGS_BASE) -fPIC
LDFLAGS     = -Wl,--gc-sections -lpthread

# Зависимости (модули)
MODULES     = ../timeutil

# Исходники и заголовки
SRC         = $(filter-out main.c test.c, $(wildcard *.c))
HDR         = $(wildcard *.h)

# Объекты
OBJ_STATIC  = $(SRC:.c=.o)
OBJ_SHARED  = $(SRC:.c=_shared.o)

# Цели
TARGET_LIB  = lib$(NAME).a
TARGET_SO   = lib$(NAME).so
TARGET_TEST = test
TARGET_MAIN = main

.PHONY: all clean $(MODULES)

all: $(MODULES) $(TARGET_LIB) $(TARGET_SO) $(TARGET_TEST) $(TARGET_MAIN)

# Собираем зависимости
$(MODULES):
	$(MAKE) -C $@

# Static library
$(TARGET_LIB): $(OBJ_STATIC)
	ar rcs $@ $^

# Shared library (жёсткая зависимость на зависимости)
$(TARGET_SO): $(OBJ_SHARED) $(addsuffix /lib$(notdir $(MODULES)).so,$(MODULES))
	$(CC) $(CFLAGS) -shared -o $@ $(OBJ_SHARED) \
	$(addprefix -L,$(MODULES)) $(addprefix -l,$(notdir $(MODULES))) $(LDFLAGS)

# Тестовый бинарник (статический линк)
$(TARGET_TEST): test.o $(TARGET_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Main бинарник (линк с .so)
$(TARGET_MAIN): main.o $(TARGET_SO)
	$(CC) $(CFLAGS) -o $@ $^ -L. -l$(NAME) \
	$(addprefix -L,$(MODULES)) $(addprefix -l,$(notdir $(MODULES))) $(LDFLAGS)

# Static .o
%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

# Shared .o
%_shared.o: %.c $(HDR)
	$(CC) $(CFLAGS) -DIS_DYNAMIC_LIB -c $< -o $@

clean:
	rm -f $(OBJ_STATIC) $(OBJ_SHARED) *.o *.a *.so $(TARGET_TEST) $(TARGET_MAIN)
	$(foreach mod,$(MODULES),$(MAKE) -C $(mod) clean;)
