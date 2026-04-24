#
# examples/hello/Makefile - build the saturn-io hello example
#
# This builds a Saturn ROM image using the SH-2 toolchain + SGL inside
# the saturn-tools Docker SDK.  Toolchain paths match the canvas app
# at retro/saturn/tools/saturn-tools/saturn/apps/canvas/Makefile so the
# existing `scripts/docker-saturn-build.sh` works against this tree.
#

SATURN_SDK_ROOT ?= /opt/saturn-sdk

CC   = $(SATURN_SDK_ROOT)/bin/sh-elf-gcc
CONV = $(SATURN_SDK_ROOT)/bin/sh-elf-objcopy

SGLDIR = $(SATURN_SDK_ROOT)/sgl

# Path to this library's include + src, relative to examples/hello/
LIB_ROOT = ../..
LIB_INC  = $(LIB_ROOT)/include
LIB_SRC  = $(LIB_ROOT)/src

BUILDDIR = build

CCFLAGS = -m2 -O2 -fomit-frame-pointer \
	-Wall -Wextra -Wno-unused-parameter \
	-fno-common -fno-builtin \
	-nostdlib -nodefaultlibs \
	-I. \
	-I$(LIB_INC) \
	-I$(LIB_SRC) \
	-I$(SGLDIR)/INC \
	-D__SATURN__ -DSATURN

LDFLAGS = -m2 -nostartfiles \
	-L$(SGLDIR)/LIB_ELF \
	-Xlinker -T$(SATURN_SDK_ROOT)/sgl.linker \
	-Xlinker -Map -Xlinker $(BUILDDIR)/hello.map \
	-Xlinker -e -Xlinker ___Start

LIBS = $(SGLDIR)/LIB_ELF/SEGA_SYS.A \
       $(SGLDIR)/LIB_ELF/LIBSGL.A

# The library splits into three source files:
#   net.c           -- core (required)
#   connect_async.c -- optional, adds saturn_io_connect_{start,tick}
#   matchmaking.c   -- optional, adds saturn_io_matchmake
#
# hello uses only the blocking connect, so pulling net.c is enough. A
# game that wants the non-blocking connect and/or the matchmake helper
# adds the other two sources to its SRCS list.
SRCS = main.c $(LIB_SRC)/net.c
OBJS = $(BUILDDIR)/main.o $(BUILDDIR)/net.o

TARGET     = $(BUILDDIR)/hello.elf
TARGET_RAW = $(BUILDDIR)/hello.raw
TARGET_ISO = $(BUILDDIR)/hello.iso
TARGET_CUE = $(BUILDDIR)/hello.cue

MKISOFS = mkisofs
RM      = rm -rf

.PHONY: all clean

all: $(TARGET_CUE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/cd:
	mkdir -p $(BUILDDIR)/cd

$(BUILDDIR)/main.o: main.c | $(BUILDDIR)
	$(CC) $(CCFLAGS) -c $< -o $@

$(BUILDDIR)/net.o: $(LIB_SRC)/net.c | $(BUILDDIR)
	$(CC) $(CCFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -lgcc -o $@

$(TARGET_RAW): $(TARGET) $(BUILDDIR)/cd
	$(CONV) -O binary $(TARGET) $(TARGET_RAW)
	$(CONV) -O binary $(TARGET) $(BUILDDIR)/cd/0.bin

$(TARGET_ISO): $(TARGET_RAW)
	$(MKISOFS) -quiet -sysid "SEGA SATURN" -volid "SatOnline" \
		-volset "SatOnline" -sectype 2352 \
		-publisher "saturn-io example" \
		-preparer "saturn-io example" \
		-appid "SatOnlineHello" \
		-generic-boot $(SATURN_SDK_ROOT)/IP.BIN \
		-full-iso9660-filenames -o $(TARGET_ISO) $(BUILDDIR)/cd

$(TARGET_CUE): $(TARGET_ISO)
	@echo 'FILE "hello.iso" BINARY' > $(TARGET_CUE)
	@echo '  TRACK 01 MODE1/2048' >> $(TARGET_CUE)
	@echo '    INDEX 01 00:00:00' >> $(TARGET_CUE)
	@echo '    POSTGAP 00:02:00' >> $(TARGET_CUE)
	@echo "Generated $(TARGET_CUE)"

clean:
	$(RM) $(BUILDDIR)
