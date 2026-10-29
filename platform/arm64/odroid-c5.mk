# ODROID-C5 boot files
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Hardkernel's U-Boot, on the card's bootloader area, runs the FAT
# partition's boot.scr, which loads vmunix and odroid-c5.dtb.  The device
# tree is built from Hardkernel's sources for the board, fetched file by
# file at pinned revisions into a verified cache, with zedBSD's additions
# (platform/arm64/odroid-c5/odroid-c5.dts).  The bootloader itself is not
# built or carried: ODROID_C5_BOOTLOADER_FROM names a card image (such as
# Hardkernel's own) whose bootloader area is copied into the SD image.

ODROID_C5_DIR := $(ARM64_PLATFORM)/odroid-c5
ODROID_C5_DTS_MANIFEST := $(ODROID_C5_DIR)/dts-sources.txt
override ODROID_C5_DTS_REVISION := 34daba993d84e4aa09afbe3e2678dd19978eaace
ODROID_C5_DTS_CACHE_ROOT ?= build/sources/odroid-c5-dts
override ODROID_C5_DTS_CACHE := $(ODROID_C5_DTS_CACHE_ROOT)/$(ODROID_C5_DTS_REVISION)
override ODROID_C5_DTS_STAMP := $(BUILD)/odroid-c5/dts-cache.stamp
ODROID_C5_DTB := $(BUILD)/odroid-c5.dtb
ODROID_C5_BOOT_SCRIPT := $(BUILD)/boot.scr
ODROID_C5_BOOTLOADER_FROM ?=

# The login the kernel's command line asks for, as cmdline.txt does on the Pi.
ODROID_C5_LOGIN := $(if $(filter y,$(ZEDBSD_GRAPHICAL_LOGIN)),graphical,console)

.PHONY: odroid-c5-dts-cache odroid-c5-boot-files
odroid-c5-dts-cache: $(ODROID_C5_DTS_STAMP)
download: odroid-c5-dts-cache

# Fetches (once) and checks the cache; the stamp follows the manifest.
$(ODROID_C5_DTS_STAMP): $(ODROID_C5_DTS_MANIFEST) platform/arm64/tools/fetch-odroid-c5-dts.py
	@mkdir -p $(dir $@)
	$(PYTHON) platform/arm64/tools/fetch-odroid-c5-dts.py --manifest $(ODROID_C5_DTS_MANIFEST) $(ODROID_C5_DTS_CACHE)
	@touch $@

# The device tree: the board's sources through the C preprocessor, then dtc.
$(ODROID_C5_DTB): $(ODROID_C5_DTS_STAMP) $(ODROID_C5_DIR)/odroid-c5.dts
	@mkdir -p $(BUILD)/odroid-c5
	cpp -nostdinc -undef -x assembler-with-cpp -D__DTS__ \
 -I $(ODROID_C5_DTS_CACHE)/common/arch/arm64/boot/dts/amlogic \
 -I $(ODROID_C5_DTS_CACHE)/common/include \
 -I $(ODROID_C5_DTS_CACHE)/linux/include \
 $(ODROID_C5_DIR)/odroid-c5.dts -o $(BUILD)/odroid-c5/odroid-c5.pre.dts
	dtc -q -I dts -O dtb -o $@ $(BUILD)/odroid-c5/odroid-c5.pre.dts

# The boot script with the build's login, in the image header U-Boot's source reads.
$(ODROID_C5_BOOT_SCRIPT): $(ODROID_C5_DIR)/boot.cmd platform/arm64/tools/make-uboot-script.py $(ZEDBSD_PLATFORM_CONFIG_STAMP)
	@mkdir -p $(BUILD)/odroid-c5
	sed 's/login=console/login=$(ODROID_C5_LOGIN)/' $< > $(BUILD)/odroid-c5/boot.cmd
	$(PYTHON) platform/arm64/tools/make-uboot-script.py $(BUILD)/odroid-c5/boot.cmd $@

odroid-c5-boot-files: $(ODROID_C5_DTB) $(ODROID_C5_BOOT_SCRIPT)

# The HAL's device tree parser on the board's tree, on the host.
$(BUILD)/tests/odroid-c5-fdt-host-test: tests/odroid-c5-fdt-host-test.c \
	src/hal/arm64/bsp-s905x5m/fdt.c src/hal/arm64/bsp-rpi-common/fdt.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -Isrc \
 src/hal/arm64/bsp-s905x5m/fdt.c $< -o $@

.PHONY: odroid-c5-fdt-host-test
odroid-c5-fdt-host-test: $(BUILD)/tests/odroid-c5-fdt-host-test $(ODROID_C5_DTB)
	$< $(ODROID_C5_DTB)

CHECK_RUN_TARGETS += odroid-c5-fdt-host-test

# The SD card: the bootloader area, the FAT boot partition and the UFS root.
$(BUILD)/hdd-image.img: $(BUILD)/vmunix $(BUILD)/ufs-root.img $(DATA_IMAGE) $(SWAP_IMAGE) \
	$(ODROID_C5_DTB) $(ODROID_C5_BOOT_SCRIPT) \
	platform/arm64/tools/make-odroid-c5-hdd-image.py tools/build/check-ufs-image.py
	$(PYTHON) platform/arm64/tools/make-odroid-c5-hdd-image.py --force \
 --kernel $(BUILD)/vmunix --dtb $(ODROID_C5_DTB) --script $(ODROID_C5_BOOT_SCRIPT) \
 --data-image $(DATA_IMAGE) --swapfile $(SWAP_IMAGE) --ufs-root $(BUILD)/ufs-root.img \
 $(if $(ODROID_C5_BOOTLOADER_FROM),--bootloader-from $(ODROID_C5_BOOTLOADER_FROM)) $@
