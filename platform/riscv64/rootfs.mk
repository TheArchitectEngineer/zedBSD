# zedBSD riscv64: the root disk image the kernel reads from its ramdisk.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# The image is an MBR disk laid out like the Raspberry Pi 4's SD card: a
# small FAT boot partition, then the UFS root with the /etc/zedbsd-root
# marker beside it, which the kernel's root search looks for.  The firmware
# (or the loader) puts it in memory as the initial ramdisk.
RISCV64_ROOT_IMAGE_TOOL := platform/riscv64/tools/make-riscv64-root-image.py

# A root with nothing but the marker: the kernel mounts it and finds no
# init to start.
$(BUILD)/root-bare.img: $(RISCV64_ROOT_IMAGE_TOOL) tools/build/ufs_format.py \
	tools/build/check-ufs-image.py
	@mkdir -p $(dir $@)
	$(PYTHON) $(RISCV64_ROOT_IMAGE_TOOL) --force --bare-root $@

# The root of the base system: the staged root tree (platform/riscv64/userland.mk).
$(BUILD)/root.img: $(BUILD)/rootfs/.stamp $(RISCV64_ROOT_IMAGE_TOOL) \
	tools/build/ufs_format.py tools/build/check-ufs-image.py
	@mkdir -p $(dir $@)
	$(PYTHON) $(RISCV64_ROOT_IMAGE_TOOL) --force --ufs-tree $(BUILD)/rootfs \
 --ufs-size-mib 64 $@
