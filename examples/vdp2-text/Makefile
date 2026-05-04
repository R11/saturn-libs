# examples/vdp2-text/Makefile
#
# Builds a tiny Saturn ROM that prints "hello, saturn" via saturn-vdp2.
# Toolchain paths match the rest of the examples and saturn-io's
# hello demo.

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
	-I$(LIBS_ROOT)/saturn-vdp2/include \
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
