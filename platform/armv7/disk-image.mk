# zedBSD armv7: the ZYBO's SD card image.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

ifeq ($(ZEDBSD_BOARD),zybo)
# An MBR disk of a power of two in size (the emulator's SD card model
# takes no other): partition 1 a bootable FAT32 of ZYBO_SD_FAT_MIB (at
# least 64), which the kernel mounts as /boot/boot0, partition 2 the UFS root
# ($(BUILD)/root.ufs), which the boot parameter rootpart=/dev/mmcblk0p2
# names.
#
# Partition 1 carries the files of the SD boot: the kernel as the FIT
# vmunix.itb and its device tree system.dtb (platform/armv7/tools/
# make-zybo-boot-files.py), which U-Boot SPL reads in Falcon mode, and,
# when ZYBO_BOOT_BIN names it, SPL itself as BOOT.BIN, which the board's
# boot ROM reads.  ZYBO_BOOT_BIN is the boot.bin plan/tools/zybo/
# spl-build.sh builds outside the tree (U-Boot is GPL-2.0+ and is not
# shipped); without it the image is for a kernel loaded another way, over
# JTAG or by the emulator.  ZYBO_BOOTARGS adds boot parameters (words
# without quotes, such as mac0=02:5a:59:42:4f:31) after the root's in the
# device tree.  ZYBO_SD_EXTRA_FILES lists more DEST=SOURCE files for
# partition 1.
ZYBO_SD_IMAGE := $(BUILD)/zybo-sd.img
ZYBO_SD_IMAGE_MIB ?= 512
ZYBO_SD_FAT_MIB ?= 64
ZYBO_BOOT_BIN ?=
ZYBO_BOOTARGS ?=
ZYBO_SD_EXTRA_FILES ?=
ZYBO_SD_IMAGE_TOOL := platform/armv7/tools/make-zybo-sd-image.py
ZYBO_SD_CHECK_TOOL := platform/armv7/tools/check-zybo-sd-image.py
ZYBO_BOOT_FILES_TOOL := platform/armv7/tools/make-zybo-boot-files.py
ZYBO_SD_DTS := platform/armv7/zybo/zybo-sd.dts
ZYBO_SD_BOOT_FILES := vmunix.itb=$(BUILD)/vmunix.itb system.dtb=$(BUILD)/system.dtb \
	$(if $(ZYBO_BOOT_BIN),BOOT.BIN=$(ZYBO_BOOT_BIN)) $(ZYBO_SD_EXTRA_FILES)
ZYBO_SD_IMAGE_ARGS = --fat-mib $(ZYBO_SD_FAT_MIB) \
	$(addprefix --boot-file ,$(ZYBO_SD_BOOT_FILES))
ZYBO_SD_CHECK_ARGS = $(ZYBO_SD_IMAGE_ARGS) --kernel $(BUILD)/vmunix.bin \
	$(addprefix --bootarg ,$(ZYBO_BOOTARGS))

# The FIT checks the raw kernel against its ELF: loaded and entered at
# 0x00100000, and below the device tree at 0x01F00000, BSS included.
$(BUILD)/vmunix.itb: $(BUILD)/vmunix.bin $(BUILD)/vmunix $(ZYBO_BOOT_FILES_TOOL)
	$(PYTHON) $(ZYBO_BOOT_FILES_TOOL) fit --kernel $(BUILD)/vmunix.bin \
 --elf $(BUILD)/vmunix $@

# The device tree is made again when ZYBO_BOOTARGS changes: this
# content-stable stamp holds the value and is rewritten only when it
# differs.
ZYBO_BOOTARGS_STAMP := $(BUILD)/.zybo-bootargs

.PHONY: FORCE_ZYBO_BOOTARGS
FORCE_ZYBO_BOOTARGS:

$(ZYBO_BOOTARGS_STAMP): FORCE_ZYBO_BOOTARGS
	@mkdir -p $(dir $@)
	@value='$(ZYBO_BOOTARGS)'; \
 if ! test -f $@ || test "$$(cat $@)" != "$$value"; then \
 printf '%s\n' "$$value" > $@.tmp; \
 mv $@.tmp $@; \
 fi

$(BUILD)/system.dtb: $(ZYBO_SD_DTS) $(ZYBO_BOOTARGS_STAMP) $(ZYBO_BOOT_FILES_TOOL)
	$(PYTHON) $(ZYBO_BOOT_FILES_TOOL) dtb --dts $(ZYBO_SD_DTS) \
 --bootargs '$(ZYBO_BOOTARGS)' $@

$(ZYBO_SD_IMAGE): $(BUILD)/root.ufs $(ZYBO_SD_IMAGE_TOOL) $(ZYBO_SD_CHECK_TOOL) \
	$(ZYBO_BOOT_FILES_TOOL) tools/build/check-ufs-image.py \
	$(foreach entry,$(ZYBO_SD_BOOT_FILES),$(word 2,$(subst =, ,$(entry))))
	$(PYTHON) $(ZYBO_SD_IMAGE_TOOL) --root-ufs $(BUILD)/root.ufs \
 --image-mib $(ZYBO_SD_IMAGE_MIB) $(ZYBO_SD_IMAGE_ARGS) $@
	$(PYTHON) $(ZYBO_SD_CHECK_TOOL) --root-ufs $(BUILD)/root.ufs \
 $(ZYBO_SD_CHECK_ARGS) $@

.PHONY: sd-image zybo-sd-image-host-test
sd-image: $(ZYBO_SD_IMAGE)

# The image tool and its checker on an image whose boot partition carries,
# beside the files of the SD boot, a file in a directory and one at the
# top, and whose device tree has a parameter added.  The checker must
# refuse the same image when the kernel it is told of is not the FIT's, and
# when the device tree lacks a parameter it is told to look for.
ZYBO_SD_TEST_IMAGE := $(BUILD)/tests/zybo-sd-image-test.img
ZYBO_SD_TEST_DTB := $(BUILD)/tests/zybo-sd-image-test.dtb
ZYBO_SD_TEST_BOOTARG := mac0=02:5a:59:42:4f:31
ZYBO_SD_TEST_FILES := vmunix.itb=$(BUILD)/vmunix.itb \
	system.dtb=$(ZYBO_SD_TEST_DTB) \
	$(if $(ZYBO_BOOT_BIN),BOOT.BIN=$(ZYBO_BOOT_BIN)) \
	fstab=userland/base/etc/fstab etc/rc.conf=userland/base/etc/rc.conf
ZYBO_SD_TEST_CHECK = $(PYTHON) $(ZYBO_SD_CHECK_TOOL) --root-ufs $(BUILD)/root.ufs \
 $(addprefix --boot-file ,$(ZYBO_SD_TEST_FILES))
zybo-sd-image-host-test: $(BUILD)/root.ufs $(BUILD)/vmunix.itb $(ZYBO_SD_DTS) \
	$(ZYBO_SD_IMAGE_TOOL) $(ZYBO_SD_CHECK_TOOL) $(ZYBO_BOOT_FILES_TOOL)
	@mkdir -p $(dir $(ZYBO_SD_TEST_IMAGE))
	$(PYTHON) $(ZYBO_BOOT_FILES_TOOL) dtb --dts $(ZYBO_SD_DTS) \
 --bootargs '$(ZYBO_SD_TEST_BOOTARG)' $(ZYBO_SD_TEST_DTB)
	$(PYTHON) $(ZYBO_SD_IMAGE_TOOL) --root-ufs $(BUILD)/root.ufs \
 $(addprefix --boot-file ,$(ZYBO_SD_TEST_FILES)) $(ZYBO_SD_TEST_IMAGE)
	$(ZYBO_SD_TEST_CHECK) --kernel $(BUILD)/vmunix.bin \
 --bootarg $(ZYBO_SD_TEST_BOOTARG) $(ZYBO_SD_TEST_IMAGE)
	! $(ZYBO_SD_TEST_CHECK) --kernel $(ZYBO_SD_TEST_DTB) $(ZYBO_SD_TEST_IMAGE)
	! $(ZYBO_SD_TEST_CHECK) --kernel $(BUILD)/vmunix.bin \
 --bootarg rootpart=/dev/mmcblk0p3 $(ZYBO_SD_TEST_IMAGE)
	@echo 'zybo-sd-image-host-test: PASS'
endif
