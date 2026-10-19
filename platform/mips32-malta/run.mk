# zedBSD mips32 malta: running the kernel in the emulator's malta machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The emulator's own bootloader enters the kernel ELF directly; the
# display is the standard VGA on PCI, which the kernel sets up.
QEMU ?= qemu-system-mipsel
QEMU_FLAGS ?= -M malta -m 256M -vga std -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix)
