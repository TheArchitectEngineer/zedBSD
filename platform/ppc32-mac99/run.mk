# zedBSD ppc32: running the kernel in the emulator's mac99 machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The firmware (OpenBIOS) enters the flat image at its load address; the
# display is the one the firmware sets up, at 32 bits a pixel.
QEMU ?= qemu-system-ppc
QEMU_FLAGS ?= -machine mac99 -m 512M -g 800x600x32 -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix.bin
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix.bin)
