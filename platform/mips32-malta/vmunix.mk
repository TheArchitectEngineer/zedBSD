# zedBSD mips32 malta kernel.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

MIPS32_PLATFORM := platform/mips32-malta
AWK ?= awk

# The project's LLVM has no zedBSD target for MIPS, so the kernel is built
# for the generic ELF target with the zedBSD target's define (the kernel
# needs no C library and no user program is built for this port yet).
MIPS32_CC ?= $(ZEDBSD_TARGET_LLVM_BIN)/clang --target=mipsel-unknown-elf -D__ZEDBSD__=1
MIPS32_LD ?= $(ZEDBSD_TARGET_LLVM_BIN)/ld.lld
MIPS32_NM ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-nm
MIPS32_READELF ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-readelf
MIPS32_OBJDUMP ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-objdump

# The kernel and the HAL read the compiler's freestanding headers and the
# tree's include/ and src/, never the C library (as on the other ports).
MIPS32_CPPFLAGS := -nostdlibinc -Iinclude -Isrc -I. -Isrc/hal/mips \
	-DHAL_ARCH_MIPS32 -DHAL_BOARD_MALTA -DKERN_USER_ABI_MIPS32
MIPS32_CPPFLAGS += $(ZEDBSD_CONFIG_CPPFLAGS)

# The kernel's C uses no floating point.  The code is absolute, not
# position-independent, and uses no small-data section, since nothing
# sets up the global pointer those sections are reached through.
MIPS32_CFLAGS := -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls -fno-pic -fno-pie \
	-G 0 -mno-gpopt -ffreestanding -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common \
	-ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror

# Link-time optimization of vmunix: ZEDBSD_KERNEL_LTO_CFLAGS (from the top
# Makefile) goes to every C object of the HAL; the assembly stays native.
MIPS32_KERNEL_LTO_CFLAGS := $(ZEDBSD_KERNEL_LTO_CFLAGS)

# The code generation of link-time optimization runs in the linker, which
# does not see the compiler's -mno-abicalls and -G 0: they are given to it
# again, so that the optimized object is absolute code without small data.
MIPS32_LTO_LDFLAGS := -mllvm -mattr=+noabicalls -mllvm -mips-ssection-threshold=0

MIPS32_BSP := src/hal/mips/bsp-malta
MIPS32_HAL_C := src/hal/mips/cmain.c src/hal/mips/lib.c \
	src/hal/mips/int.c src/hal/mips/clock.c src/hal/mips/task.c \
	$(MIPS32_BSP)/board.c $(MIPS32_BSP)/irq.c \
	$(MIPS32_BSP)/pci.c $(MIPS32_BSP)/framebuffer.c \
	$(MIPS32_BSP)/cons.c $(MIPS32_BSP)/font.c
MIPS32_HAL_S := src/hal/mips/locore.S src/hal/mips/vectors.S src/hal/mips/trap.S \
	src/hal/mips/dispatch.S
MIPS32_HAL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(MIPS32_HAL_C)) \
	$(patsubst %.S,$(BUILD)/%.o,$(MIPS32_HAL_S))

# The image is the HAL alone until the page tables are in place: it links
# no kernel object, and the platform supplies the one kernel symbol the
# HAL reads.
MIPS32_STANDALONE_SOURCES := $(MIPS32_PLATFORM)/standalone.c
MIPS32_STANDALONE_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(MIPS32_STANDALONE_SOURCES))

# A 32-bit processor divides and shifts 64-bit integers through the
# compiler's helper calls.  Their object stays native: link-time
# optimization would drop the helpers before the code that calls them is
# generated.
MIPS32_HELPER_SOURCES := src/libc/int64.c
MIPS32_HELPER_OBJS := $(patsubst %.c,$(BUILD)/kernel-helper/%.o,$(MIPS32_HELPER_SOURCES))

MIPS32_VMUNIX_OBJS := $(MIPS32_HAL_OBJS) $(MIPS32_STANDALONE_OBJS) $(MIPS32_HELPER_OBJS)
$(MIPS32_VMUNIX_OBJS): $(ZEDBSD_PLATFORM_CONFIG_STAMP)
$(MIPS32_VMUNIX_OBJS): $(ZEDBSD_KERNEL_LTO_STAMP)

vmunix: $(BUILD)/vmunix

# The kernel is an ELF the emulator's loader places by its KSEG0
# addresses; it is taken as it is.
$(BUILD)/vmunix: $(MIPS32_VMUNIX_OBJS) $(MIPS32_PLATFORM)/vmunix.ld
	$(MIPS32_LD) -m elf32ltsmip $(MIPS32_LTO_LDFLAGS) --gc-sections -z max-page-size=4096 \
 -T $(MIPS32_PLATFORM)/vmunix.ld -nostdlib $(MIPS32_VMUNIX_OBJS) -o $@
	@# A weak undefined symbol is an optional hook, not a link error.
	@test -z "$$($(MIPS32_NM) -u $@ | $(AWK) '$$1 == "U"')" || \
		{ $(MIPS32_NM) -u $@; exit 1; }
	@# No small-data section and no instruction through gp: nothing sets
	@# gp up.  (The linker keeps the two reserved words of its .got, and the
	@# trap frame saves and restores gp on the stack like any register.)
	@test -z "$$($(MIPS32_READELF) -S -W $@ | $(AWK) '/ \.(sdata|sbss|lit4|lit8)( |$$)/')" || \
		{ $(MIPS32_READELF) -S -W $@; echo "vmunix has a small-data section"; exit 1; }
	@test -z "$$($(MIPS32_OBJDUMP) -d $@ | $(AWK) '/[$$]gp/ && !/[sl]w[ \t]+[$$]gp, 0x[0-9a-f]+\([$$]sp\)/')" || \
		{ echo "vmunix has an instruction that uses gp"; exit 1; }

$(BUILD)/src/hal/mips/%.o: src/hal/mips/%.c
	@mkdir -p $(dir $@)
	$(MIPS32_CC) $(MIPS32_CPPFLAGS) $(MIPS32_CFLAGS) $(MIPS32_KERNEL_LTO_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/src/hal/mips/%.o: src/hal/mips/%.S
	@mkdir -p $(dir $@)
	$(MIPS32_CC) $(MIPS32_CPPFLAGS) $(MIPS32_CFLAGS) -D_ASM_SRC_ -c $< -o $@

$(BUILD)/$(MIPS32_PLATFORM)/%.o: $(MIPS32_PLATFORM)/%.c
	@mkdir -p $(dir $@)
	$(MIPS32_CC) $(MIPS32_CPPFLAGS) $(MIPS32_CFLAGS) $(MIPS32_KERNEL_LTO_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel-helper/%.o: %.c
	@mkdir -p $(dir $@)
	$(MIPS32_CC) $(MIPS32_CPPFLAGS) $(MIPS32_CFLAGS) -fno-builtin -MMD -MP -c $< -o $@

.PHONY: toolchain
toolchain:
	@command -v $(firstword $(MIPS32_CC)) >/dev/null
	@command -v $(MIPS32_LD) >/dev/null
	@echo "MIPS32 toolchain: $(MIPS32_CC), $(MIPS32_LD)"

-include $(MIPS32_HAL_OBJS:.o=.d) $(MIPS32_STANDALONE_OBJS:.o=.d) $(MIPS32_HELPER_OBJS:.o=.d)
