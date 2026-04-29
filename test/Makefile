# libs/test — host-only test harness for the saturn lobby project.
#
# Builds a static library (libsaturn_test.a) plus a default `main` object
# consumers can link to get a no-config test runner. Self-tests under tests/
# exercise every framework feature.
#
# Targets:
#   make            -- build the static lib only.
#   make test       -- build + run self-tests with human-readable output.
#   make test-tap   -- build + run self-tests with TAP 14 output.
#   make clean      -- remove build artifacts.

CC       ?= cc
AR       ?= ar
CFLAGS   := -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O0
INCS     := -Iinclude
BUILD    := build

LIB_SRCS  := src/test.c
LIB_OBJS  := $(LIB_SRCS:src/%.c=$(BUILD)/%.o)
LIB       := $(BUILD)/libsaturn_test.a

MAIN_SRC  := src/test_main.c
MAIN_OBJ  := $(BUILD)/test_main.o

TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(TEST_SRCS:tests/%.c=$(BUILD)/tests_%.o)
TEST_BIN  := $(BUILD)/run_self_tests

.PHONY: all test test-tap clean
all: $(LIB)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

$(BUILD)/tests_%.o: tests/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

$(TEST_BIN): $(TEST_OBJS) $(MAIN_OBJ) $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(MAIN_OBJ) $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

test: $(TEST_BIN)
	$(TEST_BIN)

test-tap: $(TEST_BIN)
	$(TEST_BIN) --tap

clean:
	rm -rf $(BUILD)
