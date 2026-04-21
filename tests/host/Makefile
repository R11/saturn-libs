# Host-side unit tests for saturn-online.
#
# These tests compile src/net.c (and later matchmaking.c / connect_async.c)
# against the host toolchain without __SATURN__ defined, so we can
# exercise configuration validation and protocol logic without a Saturn
# toolchain in the CI loop.
#
# Usage:
#     make -C tests/host
#     make -C tests/host run

CC      ?= clang
CFLAGS  ?= -Wall -Wextra -Wno-unused-parameter \
           -Wno-int-to-pointer-cast \
           -Iinclude -Isrc -I.

LIB_ROOT := ../..
LIB_INC  := $(LIB_ROOT)/include
LIB_SRC  := $(LIB_ROOT)/src

CPPFLAGS += -I$(LIB_INC) -I$(LIB_SRC)

TESTS := test_init_placeholder test_transport_and_txbuf test_connect_async \
         test_matchmaking

.PHONY: all clean run

all: $(TESTS)

test_init_placeholder: test_init_placeholder.c $(LIB_SRC)/net.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

test_transport_and_txbuf: test_transport_and_txbuf.c $(LIB_SRC)/net.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

test_connect_async: test_connect_async.c $(LIB_SRC)/net.c $(LIB_SRC)/connect_async.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

test_matchmaking: test_matchmaking.c $(LIB_SRC)/net.c $(LIB_SRC)/matchmaking.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $@

clean:
	rm -f $(TESTS)

run: all
	@for t in $(TESTS); do \
	    echo "=== $$t ==="; \
	    ./$$t || exit 1; \
	done
