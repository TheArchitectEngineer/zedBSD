# zedBSD armv7: running the kernel in the emulator's virt machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The raw image is booted as Linux is, so the device tree reaches it in
# r2; the display is a RAM framebuffer the kernel sets up.
QEMU ?= qemu-system-arm
QEMU_FLAGS ?= -machine virt,gic-version=2 -cpu cortex-a15 -smp 1 -m 512M -device ramfb -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix.bin
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix.bin)
