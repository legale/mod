CC ?= gcc
CFLAGS_BASE := -std=c11 -Wall -Wextra -Wpedantic -Werror
DEBUG_FLAGS := -O0 -g3 -fno-omit-frame-pointer
RELEASE_FLAGS := -O2
ASAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS := -fsanitize=undefined -fno-omit-frame-pointer
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer
LDFLAGS_BASE := -lpthread

ifeq ($(BUILD),release)
  CFLAGS += $(RELEASE_FLAGS)
else ifeq ($(BUILD),asan)
  CFLAGS += $(DEBUG_FLAGS) $(ASAN_FLAGS)
  LDFLAGS += $(ASAN_FLAGS)
else ifeq ($(BUILD),ubsan)
  CFLAGS += $(DEBUG_FLAGS) $(UBSAN_FLAGS)
  LDFLAGS += $(UBSAN_FLAGS)
else ifeq ($(BUILD),tsan)
  CFLAGS += $(DEBUG_FLAGS) $(TSAN_FLAGS)
  LDFLAGS += $(TSAN_FLAGS)
else
  BUILD := debug
  CFLAGS += $(DEBUG_FLAGS)
endif

CFLAGS += $(CFLAGS_BASE)
LDFLAGS += $(LDFLAGS_BASE)

INCLUDES := -Iinclude
