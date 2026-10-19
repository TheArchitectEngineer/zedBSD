# zedBSD mips32 RG350 kernel: the malta port's kernel and common HAL with
# the JZ4770 board, and the images the boot loader reads.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

MIPS32_PLATFORM := platform/mips32-rg350
MIPS32_BOARD_CPPFLAGS := -DHAL_BOARD_RG350
MIPS32_BSP := src/hal/mips/bsp-jz4770
MIPS32_BSP_C := $(MIPS32_BSP)/board-rg350.c
MIPS32_BSP_S := $(MIPS32_BSP)/early.S
include platform/mips32-malta/vmunix.mk

MIPS32_OBJCOPY ?= $(ZEDBSD_TARGET_LLVM_BIN)/llvm-objcopy
PYTHON3 ?= python3

# The boot loader reads the kernel from the card's first partition
# either as a raw image, entered at its first byte, or as a uImage; the
# kernel is linked so that both start at _start.  The .BAK names are
# those the loader tries when its alternative key is held at power-on.
rg350-images: $(BUILD)/VMLINUZ.BAK $(BUILD)/UZIMAGE.BAK

$(BUILD)/VMLINUZ.BAK: $(BUILD)/vmunix
	$(MIPS32_OBJCOPY) -O binary $< $@

$(BUILD)/UZIMAGE.BAK: $(BUILD)/VMLINUZ.BAK $(MIPS32_PLATFORM)/mkuimage.py
	@entry=$$($(MIPS32_NM) $(BUILD)/vmunix | $(AWK) '$$3 == "_start" { print "0x" $$1 }'); \
	test -n "$$entry" || { echo "vmunix has no _start"; exit 1; }; \
	echo "$(PYTHON3) $(MIPS32_PLATFORM)/mkuimage.py --load $$entry --entry $$entry $< $@"; \
	$(PYTHON3) $(MIPS32_PLATFORM)/mkuimage.py --load $$entry --entry $$entry $< $@
	$(PYTHON3) $(MIPS32_PLATFORM)/mkuimage.py --check $@

.PHONY: rg350-images
