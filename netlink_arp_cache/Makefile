# Name of output file
NAME = arp
# Build dir
BD = ./build

# Linker flags
LDLIBS +=
LDDIRS += -L$(BD)
LDFLAGS += -L../syslog2 -L../timeutil
LIBS += -lsyslog2 -ltimeutil

# Compiler flags
CFLAGS += -Wall -O2 -fPIC
I += -I/usr/include

# Compiler
CC = gcc
AR = ar

LIBNAME = nlarpcache
SRC_LIB = libnlarpcache.c
SRC_BIN = main.c
OBJ_LIB = $(patsubst %.c, $(BD)/%.o, $(SRC_LIB))
OBJ_BIN = $(patsubst %.c, $(BD)/%.o, $(SRC_BIN))

all: $(NAME)

$(NAME): $(BD)/lib$(LIBNAME).a $(BD)/$(NAME)_shared $(BD)/$(NAME)_static

$(BD)/lib$(LIBNAME).a: $(OBJ_LIB)
	$(AR) rcs $@ $^

$(BD)/lib$(LIBNAME).so: $(OBJ_LIB)
        $(CC) $(CFLAGS) $(I) $(LDDIRS) $(LDFLAGS) $(LDLIBS) $^ -shared -o $@ $(LIBS)

$(BD)/$(NAME)_shared: $(OBJ_BIN) $(BD)/lib$(LIBNAME).so
        $(CC) $(CFLAGS) $(I) $(LDDIRS) $(LDFLAGS) $(LDLIBS) $^ -o $@ $(LIBS)

$(BD)/$(NAME)_static: $(OBJ_BIN) $(BD)/lib$(LIBNAME).a
        $(CC) $(CFLAGS) $(I) $(LDDIRS) $(LDFLAGS) $(LDLIBS) -Wl,-Bstatic -l$(LIBNAME) -Wl,-Bdynamic $^ -o $@ $(LIBS)

$(BD)/%.o: %.c
	@mkdir -p $(BD)
	$(CC) $(CFLAGS) $(I) -c $< -o $@

clean:
	rm -rf $(BD)/*

.PHONY: all clean
