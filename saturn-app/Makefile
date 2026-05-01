CC       ?= cc
AR       ?= ar
CFLAGS   := -std=c99 -Wall -Wextra -Werror -Wpedantic -g -O0
INCS     := -Iinclude -I../saturn-base/include -I../saturn-bup/include -I../test/include
BUILD    := build

CORE_SRCS    := core/saturn_app_core.c core/identity.c
CORE_OBJS    := $(CORE_SRCS:core/%.c=$(BUILD)/core_%.o)

WIDGET_SRCS  := widgets/keyboard.c
WIDGET_OBJS  := $(WIDGET_SRCS:widgets/%.c=$(BUILD)/widgets_%.o)

STATE_SRCS   := state/state_name_entry.c \
                state/state_local_lobby.c \
                state/state_name_pick.c \
                state/state_connecting.c \
                state/state_lobby_list.c \
                state/state_room_create.c \
                state/state_in_room.c \
                state/state_online_ctx.c \
                state/state_game_select_online.c \
                state/state_vote_timer.c \
                state/state_countdown.c \
                state/state_playing_online.c \
                state/state_results_online.c
STATE_OBJS   := $(STATE_SRCS:state/%.c=$(BUILD)/state_%.o)

NET_SRCS     := net/sapp_proto.c net/sapp_net.c net/sapp_net_host.c
NET_OBJS     := $(NET_SRCS:net/%.c=$(BUILD)/net_%.o)

LIB := $(BUILD)/libsaturn_app.a

TEST_SRCS := $(wildcard tests/*.c)
TEST_OBJS := $(TEST_SRCS:tests/%.c=$(BUILD)/tests_%.o)
TEST_BIN  := $(BUILD)/run_tests

SATURN_TEST_DIR := ../test
SATURN_TEST_LIB := $(SATURN_TEST_DIR)/build/libsaturn_test.a

SATURN_BUP_DIR := ../saturn-bup
SATURN_BUP_LIB := $(SATURN_BUP_DIR)/build/libsaturn_bup.a

.PHONY: all test test-tap clean
all: $(LIB)
$(LIB): $(CORE_OBJS) $(WIDGET_OBJS) $(STATE_OBJS) $(NET_OBJS); $(AR) rcs $@ $^
$(BUILD)/core_%.o: core/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(BUILD)/widgets_%.o: widgets/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(BUILD)/state_%.o: state/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(BUILD)/net_%.o: net/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(BUILD)/tests_%.o: tests/%.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(SATURN_TEST_LIB):; $(MAKE) -C $(SATURN_TEST_DIR) all
$(SATURN_BUP_LIB):; $(MAKE) -C $(SATURN_BUP_DIR) all
$(BUILD)/test_main.o: $(SATURN_TEST_DIR)/src/test_main.c | $(BUILD); $(CC) $(CFLAGS) $(INCS) -c -o $@ $<
$(TEST_BIN): $(TEST_OBJS) $(BUILD)/test_main.o $(LIB) $(SATURN_TEST_LIB) $(SATURN_BUP_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(BUILD)/test_main.o $(LIB) $(SATURN_BUP_LIB) $(SATURN_TEST_LIB)
$(BUILD):; mkdir -p $(BUILD)
test: $(TEST_BIN); $(TEST_BIN)
test-tap: $(TEST_BIN); $(TEST_BIN) --tap
clean:; rm -rf $(BUILD)
