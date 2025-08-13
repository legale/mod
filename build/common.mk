# toolchain
CC ?= cc
AR ?= ar

# base flags
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g
CFLAGS_REL := -O2 -g
CFLAGS_DBG := -O0 -g3 -fno-omit-frame-pointer
CFLAGS_ASAN := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS_UBSAN := -O1 -g -fsanitize=undefined -fno-omit-frame-pointer
CFLAGS_TSAN := -O1 -g -fsanitize=thread -fno-omit-frame-pointer

# dirs
OUTDIR ?= out
$(shell mkdir -p $(OUTDIR))

# musl build helper
musl:
	docker run --rm -v $$PWD:/w -w /w alpine:latest sh -c "apk add build-base musl-dev && make clean && make debug && make test"
