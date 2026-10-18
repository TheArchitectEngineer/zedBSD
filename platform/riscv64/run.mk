# zedBSD riscv64: running the kernel in the emulator's virt machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The firmware (OpenSBI, the emulator's default) enters the kernel in
# supervisor mode; the display is a RAM framebuffer the kernel sets up.
QEMU ?= qemu-system-riscv64
QEMU_FLAGS ?= -machine virt -smp 4 -m 2G -device ramfb -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix)
