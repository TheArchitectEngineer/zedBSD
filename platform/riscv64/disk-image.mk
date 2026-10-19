# zedBSD riscv64: the UEFI disk image.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

# A GPT disk whose EFI system partition holds the loader at the firmware's
# default path, zedbsd.cfg, the kernel and the root image, which the loader
# hands to the kernel as its initial ramdisk.
RISCV64_DISK_IMAGE_TOOL := platform/riscv64/tools/make-riscv64-disk-image.py

.PHONY: disk-image
disk-image: $(BUILD)/disk.img

$(BUILD)/disk.img: $(BUILD)/uefi/BOOTRISCV64.EFI $(BUILD)/vmunix \
	$(BUILD)/root.img platform/riscv64/zedbsd.cfg $(RISCV64_DISK_IMAGE_TOOL)
	$(PYTHON) $(RISCV64_DISK_IMAGE_TOOL) --force --efi $(BUILD)/uefi/BOOTRISCV64.EFI \
 --kernel $(BUILD)/vmunix --config platform/riscv64/zedbsd.cfg \
 --root-image $(BUILD)/root.img $@

# The native disk image: the root is a UFS partition of the disk itself,
# named by rootpart= in zedbsd-native.cfg, and no ramdisk is read.  It is
# the VisionFive 2's SD card image; on the virt board the same
# image is an NVMe disk.  An unused 64 MiB partition at the end is for raw
# read and write tests of the medium.  RISCV64_SPL and RISCV64_UBOOT_ITB,
# when both are set (the VisionFive 2's u-boot-spl.bin.normal.out and
# u-boot.itb, as Debian's u-boot-starfive installs them), put the firmware
# in partitions 1 and 2, so the board boots the card with its boot switch
# set to SD.
RISCV64_NATIVE_CONFIG_SOURCE := platform/riscv64/zedbsd-native.cfg
RISCV64_NATIVE_CONFIG := $(BUILD)/zedbsd-native.cfg
RISCV64_NATIVE_SCRATCH_MIB ?= 64
RISCV64_SPL ?=
RISCV64_UBOOT_ITB ?=

# The native image's boot configuration (ws270-p003): the source, and
# login=graphical when ZEDBSD_GRAPHICAL_LOGIN is y, as the other ports put
# it on the boot line.  Rechecked on every invocation; the mtime is kept
# when the text is unchanged, so only a changed choice rebuilds the image.
.PHONY: riscv64-native-config
riscv64-native-config:

$(RISCV64_NATIVE_CONFIG): $(RISCV64_NATIVE_CONFIG_SOURCE) riscv64-native-config
	@mkdir -p $(dir $@)
	@cp $(RISCV64_NATIVE_CONFIG_SOURCE) $@.tmp
	$(if $(filter y,$(ZEDBSD_GRAPHICAL_LOGIN)),@printf '%s\n' login=graphical >> $@.tmp)
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; fi

.PHONY: native-image
native-image: $(BUILD)/native.img

$(BUILD)/native.img: $(BUILD)/uefi/BOOTRISCV64.EFI $(BUILD)/vmunix \
	$(BUILD)/root.ufs $(RISCV64_NATIVE_CONFIG) $(RISCV64_DISK_IMAGE_TOOL) \
	$(RISCV64_SPL) $(RISCV64_UBOOT_ITB)
	$(PYTHON) $(RISCV64_DISK_IMAGE_TOOL) --force --efi $(BUILD)/uefi/BOOTRISCV64.EFI \
 --kernel $(BUILD)/vmunix --config $(RISCV64_NATIVE_CONFIG) \
 --root-ufs $(BUILD)/root.ufs --scratch-mib $(RISCV64_NATIVE_SCRATCH_MIB) \
 $(if $(RISCV64_SPL),--spl $(RISCV64_SPL)) \
 $(if $(RISCV64_UBOOT_ITB),--uboot-itb $(RISCV64_UBOOT_ITB)) $@
