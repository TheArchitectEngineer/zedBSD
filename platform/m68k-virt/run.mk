# zedBSD m68k virt: running the kernel in the emulator's virt machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The emulator loads the kernel's ELF itself and enters it with its boot
# information after the image; the display is a virtio GPU.
QEMU ?= qemu-system-m68k
QEMU_FLAGS ?= -machine virt -cpu m68040 -m 512M -device virtio-gpu-device -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix)
