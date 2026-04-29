CC       ?= cc
AR       ?= ar
CFLAGS   := -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O0
INCS     := -Iinclude -I../saturn-base/include -I../test/include
BUILD    := build

CORE_SRCS := core/saturn_app_core.c
CORE_OBJS := $(CORE_SRCS:core/%.c=$(BUILD)/core_%.o)
LIB := $(BUILD)/libsaturn_app.a

TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(TEST_SRCS:tests/%.c=$(BUILD)/tests_%.o)
TEST_BIN  := $(BUILD)/run_tests

SATURN_TEST_DIR := ../test
SATURN_TEST_LIB := $(SATURN_TEST_DIR)/build/libsaturn_test.a

.PHONY: all test test-tap clean
all: $(LIB)
$(LIB): $(CORE_OBJS); $(AR) rcs $@ $^
$(BUILD)/core_%.o: core/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(BUILD)/tests_%.o: tests/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(SATURN_TEST_LIB):; $(MAKE) -C $(SATURN_TEST_DIR) all
$(BUILD)/test_main.o: $(SATURN_TEST_DIR)/src/test_main.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(TEST_BIN): $(TEST_OBJS) $(BUILD)/test_main.o $(LIB) $(SATURN_TEST_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(BUILD)/test_main.o $(LIB) $(SATURN_TEST_LIB)
$(BUILD):; mkdir -p $(BUILD)
test: $(TEST_BIN); $(TEST_BIN)
test-tap: $(TEST_BIN); $(TEST_BIN) --tap
clean:; rm -rf $(BUILD)
