# zedBSD ppc64: running the kernel in the emulator's pseries machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The firmware (SLOF) enters the kernel the emulator loaded; the display is
# the VGA device the firmware sets up.
QEMU ?= qemu-system-ppc64
QEMU_FLAGS ?= -machine pseries -m 512M -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix)
