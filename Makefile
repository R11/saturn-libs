#
# saturn-online/Makefile - build the library as a single object
#
# This Makefile compiles src/net.c against the SH-2 toolchain and SGL
# headers supplied by saturn-tools' SDK.  Consumers usually don't need
# to invoke this directly -- they compile src/net.c as part of their
# own app build (the way examples/hello/Makefile does it).  This file
# exists so you can sanity-check the library compiles standalone.
#

SATURN_SDK_ROOT ?= /opt/saturn-sdk
CC   = $(SATURN_SDK_ROOT)/bin/sh-elf-gcc
SGLDIR = $(SATURN_SDK_ROOT)/sgl

BUILDDIR = build

CCFLAGS = -m2 -O2 -fomit-frame-pointer \
	-Wall -Wextra -Wno-unused-parameter \
	-fno-common -fno-builtin \
	-nostdlib -nodefaultlibs \
	-I include \
	-I$(SGLDIR)/INC \
	-D__SATURN__ -DSATURN

.PHONY: all example clean

OBJS = $(BUILDDIR)/net.o $(BUILDDIR)/connect_async.o $(BUILDDIR)/matchmaking.o

all: $(OBJS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/net.o: src/net.c | $(BUILDDIR)
	$(CC) $(CCFLAGS) -c $< -o $@

$(BUILDDIR)/connect_async.o: src/connect_async.c | $(BUILDDIR)
	$(CC) $(CCFLAGS) -c $< -o $@

$(BUILDDIR)/matchmaking.o: src/matchmaking.c | $(BUILDDIR)
	$(CC) $(CCFLAGS) -c $< -o $@

example:
	$(MAKE) -C examples/hello

clean:
	rm -rf $(BUILDDIR)
	$(MAKE) -C examples/hello clean
