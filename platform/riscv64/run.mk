# zedBSD riscv64: running the kernel in the emulator's virt machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The firmware (OpenSBI, the emulator's default) enters the kernel in
# supervisor mode; the display is a RAM framebuffer the kernel sets up.
QEMU ?= qemu-system-riscv64
QEMU_FLAGS ?= -machine virt -smp 4 -m 2G -device ramfb -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix)

# The UEFI path: U-Boot (the emulator's supervisor-mode build, run by the
# default firmware) boots EFI/BOOT/BOOTRISCV64.EFI from the disk image.
UBOOT ?= /usr/lib/u-boot/qemu-riscv64_smode/u-boot.bin

.PHONY: run-uboot
run-uboot: $(BUILD)/disk.img
	$(QEMU) $(QEMU_FLAGS) -kernel $(UBOOT) \
 -drive if=none,id=boot,file=$(abspath $(BUILD)/disk.img),format=raw,snapshot=on \
 -device virtio-blk-device,drive=boot
