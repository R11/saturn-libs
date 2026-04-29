# libs/saturn-smpc — peripheral & RTC library.
#
# Targets:
#   make            -- build host static library (libsaturn_smpc.a) for tests.
#   make test       -- build + run host tests against the mock PAL.
#   make test-tap   -- same with TAP 14 output.
#   make saturn     -- (placeholder) cross-compile for Saturn (sh-elf-gcc).
#                      Requires SH_GCC, SGL_PATH; user-supplied.
#   make clean      -- remove build artifacts.

CC       ?= cc
AR       ?= ar
CFLAGS   := -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O0
INCS     := -Iinclude -I../saturn-base/include -I../test/include
BUILD    := build

# Portable core/. Same source list compiles for host, web, and Saturn.
CORE_SRCS := core/saturn_smpc_core.c
CORE_OBJS := $(CORE_SRCS:core/%.c=$(BUILD)/core_%.o)

# Saturn shell. Compiled only when targeting sh-elf.
SATURN_SRCS := saturn/saturn_smpc_saturn.c

# Host library = portable core only. Saturn shell does not link into the
# host library (it includes <smpc.h> from SGL which the host doesn't have).
LIB := $(BUILD)/libsaturn_smpc.a

# Tests
TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(TEST_SRCS:tests/%.c=$(BUILD)/tests_%.o)
TEST_BIN  := $(BUILD)/run_tests

# libs/test artefacts; built lazily via recursive make so this Makefile
# can be invoked standalone.
SATURN_TEST_DIR := ../test
SATURN_TEST_LIB := $(SATURN_TEST_DIR)/build/libsaturn_test.a
SATURN_TEST_MAIN_OBJ := $(SATURN_TEST_DIR)/build/test_main.o

.PHONY: all test test-tap saturn clean
all: $(LIB)

$(LIB): $(CORE_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/core_%.o: core/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

$(BUILD)/tests_%.o: tests/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

$(SATURN_TEST_LIB) $(SATURN_TEST_MAIN_OBJ):
	$(MAKE) -C $(SATURN_TEST_DIR) all
	$(MAKE) -C $(SATURN_TEST_DIR) build/test_main.o

$(BUILD)/test_main.o: $(SATURN_TEST_DIR)/src/test_main.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $<

$(TEST_BIN): $(TEST_OBJS) $(BUILD)/test_main.o $(LIB) $(SATURN_TEST_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(BUILD)/test_main.o $(LIB) $(SATURN_TEST_LIB)

$(BUILD):
	mkdir -p $(BUILD)

test: $(TEST_BIN)
	$(TEST_BIN)

test-tap: $(TEST_BIN)
	$(TEST_BIN) --tap

saturn:
	@echo "saturn target needs SH_GCC and SGL_PATH; user provides toolchain."
	@echo "Sources: $(CORE_SRCS) $(SATURN_SRCS)"

clean:
	rm -rf $(BUILD)
