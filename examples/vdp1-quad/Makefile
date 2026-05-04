# examples/vdp1-quad/Makefile
#
# Builds a tiny Saturn ROM that shows a single filled quad on VDP1.
# Toolchain paths match the saturn-io hello example so the same
# saturn-tools Docker SDK wrapper works for both.

SATURN_SDK_ROOT ?= /opt/saturn-sdk
CC      = $(SATURN_SDK_ROOT)/bin/sh-elf-gcc
SGLDIR  = $(SATURN_SDK_ROOT)/sgl

LIBS_ROOT = ../..

BUILDDIR = build

CCFLAGS = -m2 -O2 -fomit-frame-pointer \
	-Wall -Wextra -Wno-unused-parameter \
	-fno-common -fno-builtin \
	-nostdlib -nodefaultlibs \
	-I$(LIBS_ROOT)/saturn-base/include \
	-I$(LIBS_ROOT)/saturn-vdp1/include \
	-I$(SGLDIR)/INC \
	-D__SATURN__ -DSATURN

.PHONY: all clean
all: $(BUILDDIR)/main.o

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/main.o: main.c | $(BUILDDIR)
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR)
