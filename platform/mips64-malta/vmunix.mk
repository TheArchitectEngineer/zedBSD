# zedBSD mips64 malta kernel.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

MIPS64_PLATFORM := platform/mips64-malta
AWK ?= awk

# The project's LLVM has no zedBSD target for MIPS, so the kernel is built
# for the generic ELF target with the zedBSD target's define (the kernel
# needs no C library and no user program is built for this port yet).
MIPS64_CC ?= $(ZEDBSD_TARGET_LLVM_BIN)/clang --target=mips64el-unknown-elf -D__ZEDBSD__=1
MIPS64_LD ?= $(ZEDBSD_TARGET_LLVM_BIN)/ld.lld
MIPS64_NM ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-nm
MIPS64_READELF ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-readelf
MIPS64_OBJDUMP ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-objdump

# The kernel and the HAL read the compiler's freestanding headers and the
# tree's include/ and src/, never the C library (as on the other ports).
# This first stage is the HAL alone up to its console
# (MIPS_HAL_CONSOLE_ONLY): the trap handling, the tables and the tick of
# the 64-bit processor come in the next stage.
MIPS64_CPPFLAGS := -nostdlibinc -Iinclude -Isrc -I. -Isrc/hal/mips \
	-DHAL_ARCH_MIPS64 -DHAL_BOARD_MALTA -DMIPS_HAL_CONSOLE_ONLY
MIPS64_CPPFLAGS += $(ZEDBSD_CONFIG_CPPFLAGS)

# The kernel's C uses no floating point.  The code is absolute, not
# position-independent, and uses no small-data section, since nothing
# sets up the global pointer those sections are reached through.  Its
# symbols are in CKSEG0, so the compiler's sym32 feature reaches them with
# the 32-bit lui and addiu (clang has no -msym32 option; the feature is
# given to the code generator directly).
MIPS64_CFLAGS := -march=mips64r2 -mabi=64 -msoft-float -mno-abicalls -fno-pic -fno-pie \
	-Xclang -target-feature -Xclang +sym32 \
	-G 0 -mno-gpopt -ffreestanding -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common \
	-ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror

# Link-time optimization of vmunix: ZEDBSD_KERNEL_LTO_CFLAGS (from the top
# Makefile) goes to every C object; the assembly stays native.
MIPS64_KERNEL_LTO_CFLAGS := $(ZEDBSD_KERNEL_LTO_CFLAGS)

# The code generation of link-time optimization runs in the linker, which
# does not see the compiler's -mno-abicalls, -G 0 and sym32: they are
# given to it again, so that the optimized object is absolute code without
# small data that reaches its symbols in 32 bits.
MIPS64_LTO_LDFLAGS := -mllvm -mattr=+noabicalls,+sym32 -mllvm -mips-ssection-threshold=0

MIPS64_BSP := src/hal/mips/bsp-malta
MIPS64_HAL_C := src/hal/mips/cmain.c src/hal/mips/lib.c \
	$(MIPS64_BSP)/board.c $(MIPS64_BSP)/pci.c $(MIPS64_BSP)/framebuffer.c \
	$(MIPS64_BSP)/cons.c $(MIPS64_BSP)/font.c \
	$(MIPS64_PLATFORM)/standalone.c
MIPS64_HAL_S := src/hal/mips/locore.S src/hal/mips/vectors.S
MIPS64_HAL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(MIPS64_HAL_C)) \
	$(patsubst %.S,$(BUILD)/%.o,$(MIPS64_HAL_S))

MIPS64_VMUNIX_OBJS := $(MIPS64_HAL_OBJS)
$(MIPS64_VMUNIX_OBJS): $(ZEDBSD_PLATFORM_CONFIG_STAMP)
$(MIPS64_VMUNIX_OBJS): $(ZEDBSD_KERNEL_LTO_STAMP)

vmunix: $(BUILD)/vmunix

# The kernel is an ELF64 the emulator's loader places by its CKSEG0
# addresses; it is taken as it is.
$(BUILD)/vmunix: $(MIPS64_VMUNIX_OBJS) $(MIPS64_PLATFORM)/vmunix.ld
	$(MIPS64_LD) -m elf64ltsmip $(MIPS64_LTO_LDFLAGS) --gc-sections -z max-page-size=4096 \
 -T $(MIPS64_PLATFORM)/vmunix.ld -nostdlib $(MIPS64_VMUNIX_OBJS) -o $@
	@# A weak undefined symbol is an optional hook, not a link error.
	@test -z "$$($(MIPS64_NM) -u $@ | $(AWK) '$$1 == "U"')" || \
		{ $(MIPS64_NM) -u $@; exit 1; }
	@# No small-data section and no instruction through gp: nothing sets
	@# gp up.
	@test -z "$$($(MIPS64_READELF) -S -W $@ | $(AWK) '/ \.(sdata|sbss|lit4|lit8)( |$$)/')" || \
		{ $(MIPS64_READELF) -S -W $@; echo "vmunix has a small-data section"; exit 1; }
	@test -z "$$($(MIPS64_OBJDUMP) -d $@ | $(AWK) '/[$$]gp/ && !/[sl]d[ \t]+[$$]gp, 0x[0-9a-f]+\([$$]sp\)/')" || \
		{ echo "vmunix has an instruction that uses gp"; exit 1; }

$(BUILD)/src/hal/mips/%.o: src/hal/mips/%.c
	@mkdir -p $(dir $@)
	$(MIPS64_CC) $(MIPS64_CPPFLAGS) $(MIPS64_CFLAGS) $(MIPS64_KERNEL_LTO_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/src/hal/mips/%.o: src/hal/mips/%.S
	@mkdir -p $(dir $@)
	$(MIPS64_CC) $(MIPS64_CPPFLAGS) $(MIPS64_CFLAGS) -D_ASM_SRC_ -c $< -o $@

$(BUILD)/$(MIPS64_PLATFORM)/%.o: $(MIPS64_PLATFORM)/%.c
	@mkdir -p $(dir $@)
	$(MIPS64_CC) $(MIPS64_CPPFLAGS) $(MIPS64_CFLAGS) $(MIPS64_KERNEL_LTO_CFLAGS) -MMD -MP -c $< -o $@

.PHONY: toolchain
toolchain:
	@command -v $(firstword $(MIPS64_CC)) >/dev/null
	@command -v $(MIPS64_LD) >/dev/null
	@echo "MIPS64 toolchain: $(MIPS64_CC), $(MIPS64_LD)"

-include $(MIPS64_HAL_OBJS:.o=.d)
