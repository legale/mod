# Name of output file
NAME = getlink
# Build dir
BD = ./build

# Linker flags
LDLIBS +=
LDDIRS += -L$(BD)

# Compiler flags
CFLAGS += -Wall -Wextra -O2 -Wno-unused-parameter
ifdef LEAKCHECK
CFLAGS += -DLEAKCHECK
endif
I += -I./usr/include

# Compiler
CC = gcc
AR = ar

# SRC=$(wildcard *.c)
LIBNAME = nl_getlink
SRC_LIB = libnl_getlink.c syslog.c 
SRC_BIN = main.c
ifdef LEAKCHECK
SRC_BIN += leak_detector_c.c 
endif
OBJ_LIB = $(patsubst %.c, $(BD)/%.o, $(SRC_LIB))
OBJ_LIB_PIC = $(patsubst %.c, $(BD)/%.pic.o, $(SRC_LIB))
OBJ_BIN = $(patsubst %.c, $(BD)/%.o, $(SRC_BIN))

all: $(NAME)

$(NAME): $(BD)/lib$(LIBNAME).a $(BD)/$(NAME)_shared $(BD)/$(NAME)_static
# Combine additional compilation steps here if needed
# ...

$(BD)/lib$(LIBNAME).a: $(OBJ_LIB)
	$(AR) rcs $@ $^

$(BD)/$(NAME)_shared: $(OBJ_BIN) $(BD)/lib$(LIBNAME).so
	$(CC) $(CFLAGS) $(I) $(LDDIRS) $^ -o $@

$(BD)/$(NAME)_static: $(OBJ_BIN) $(BD)/lib$(LIBNAME).a
	$(CC) $(CFLAGS) $(I) $(LDDIRS) $(LDLIBS) -Wl,-Bstatic -l$(LIBNAME) -Wl,-Bdynamic $^ -o $@

$(BD)/%.o: %.c
	$(CC) $(CFLAGS) $(I) -c $< -o $@

# for shared library with -fPIC
$(BD)/%.pic.o: %.c
	$(CC) $(CFLAGS) $(I) -c $< -o $@ -fPIC

$(BD)/lib$(LIBNAME).so: $(OBJ_LIB_PIC)
	$(CC) $(CFLAGS) $(I) $(LDDIRS) $(LDLIBS) $^ -shared -fPIC -o $@ 

clean:
	rm -rf $(BD)/*
