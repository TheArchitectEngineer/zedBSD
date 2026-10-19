# zedBSD mips64 malta: running the kernel in the emulator's malta machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The emulator's own bootloader enters the kernel ELF directly; the
# display is the standard VGA on PCI, which the kernel sets up.  The
# processor is the MIPS64 release 2 one (the machine's default 20Kc is
# release 1, without EBase).
QEMU ?= qemu-system-mips64el
QEMU_FLAGS ?= -M malta -cpu MIPS64R2-generic -m 256M -vga std -serial mon:stdio

.PHONY: run
run: $(BUILD)/vmunix
	$(QEMU) $(QEMU_FLAGS) -kernel $(abspath $(BUILD)/vmunix)
