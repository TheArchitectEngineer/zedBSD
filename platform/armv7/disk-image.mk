# zedBSD armv7: the ZYBO's SD card image.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

ifeq ($(ZEDBSD_BOARD),zybo)
# An MBR disk of a power of two in size (the emulator's SD card model
# takes no other): partition 1 a bootable FAT16 of ZYBO_SD_FAT_MIB, which
# the kernel mounts as /boot/boot0, partition 2 the UFS root
# ($(BUILD)/root.ufs), which the boot parameter rootpart=/dev/mmcblk0p2
# names.  ZYBO_SD_BOOT_FILES lists DEST=SOURCE files for partition 1 (the
# files the board's boot ROM and loader read); without them the image is
# for a kernel loaded another way, over JTAG or by the emulator.
ZYBO_SD_IMAGE := $(BUILD)/zybo-sd.img
ZYBO_SD_IMAGE_MIB ?= 512
ZYBO_SD_FAT_MIB ?= 64
ZYBO_SD_BOOT_FILES ?=
ZYBO_SD_IMAGE_TOOL := platform/armv7/tools/make-zybo-sd-image.py
ZYBO_SD_CHECK_TOOL := platform/armv7/tools/check-zybo-sd-image.py
ZYBO_SD_IMAGE_ARGS = --fat-mib $(ZYBO_SD_FAT_MIB) \
	$(addprefix --boot-file ,$(ZYBO_SD_BOOT_FILES))

$(ZYBO_SD_IMAGE): $(BUILD)/root.ufs $(ZYBO_SD_IMAGE_TOOL) $(ZYBO_SD_CHECK_TOOL) \
	tools/build/check-ufs-image.py \
	$(foreach entry,$(ZYBO_SD_BOOT_FILES),$(word 2,$(subst =, ,$(entry))))
	$(PYTHON) $(ZYBO_SD_IMAGE_TOOL) --root-ufs $(BUILD)/root.ufs \
 --image-mib $(ZYBO_SD_IMAGE_MIB) $(ZYBO_SD_IMAGE_ARGS) $@
	$(PYTHON) $(ZYBO_SD_CHECK_TOOL) --root-ufs $(BUILD)/root.ufs \
 $(ZYBO_SD_IMAGE_ARGS) $@

.PHONY: sd-image zybo-sd-image-host-test
sd-image: $(ZYBO_SD_IMAGE)

# The image tool and its checker on an image whose boot partition carries a
# file in a directory and one at the top, as a boot image's will.
ZYBO_SD_TEST_IMAGE := $(BUILD)/tests/zybo-sd-image-test.img
ZYBO_SD_TEST_FILES := fstab=userland/base/etc/fstab \
	etc/rc.conf=userland/base/etc/rc.conf
zybo-sd-image-host-test: $(BUILD)/root.ufs $(ZYBO_SD_IMAGE_TOOL) $(ZYBO_SD_CHECK_TOOL)
	@mkdir -p $(dir $(ZYBO_SD_TEST_IMAGE))
	$(PYTHON) $(ZYBO_SD_IMAGE_TOOL) --root-ufs $(BUILD)/root.ufs \
 $(addprefix --boot-file ,$(ZYBO_SD_TEST_FILES)) $(ZYBO_SD_TEST_IMAGE)
	$(PYTHON) $(ZYBO_SD_CHECK_TOOL) --root-ufs $(BUILD)/root.ufs \
 $(addprefix --boot-file ,$(ZYBO_SD_TEST_FILES)) $(ZYBO_SD_TEST_IMAGE)
endif
