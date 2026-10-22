# zedBSD VisionFive 2: the mainline device trees and the U-Boot boot files
# on the EFI system partition (BUG-339).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The vendor U-Boot's control device tree lacks the mainline compatibles the
# visionfive2 platform looks for, so the card carries the mainline Linux
# device trees of the board's v1.2a and v1.3b revisions (GPL-2.0 OR MIT;
# dtb.LICENSE goes beside them).  They are compiled once from the sources of
# one pinned Linux revision (dts.sources) and kept, with those sources, in the
# zedBSD firmware repository's visionfive2-dtb/ (userland/firmware/
# zedbsd-firmware.mk); a build fetches the two files from there into a
# verified cache, checked against dtb.files.  Only when the repository cannot
# be reached (or its revision is empty) does the build fall back to fetching
# the sources file by file from Linux and compiling them with the host's cpp
# and dtc, and the result must still match dtb.files.  "make
# visionfive2-dtb-rebuild" compiles them the same way into
# $(BUILD)/visionfive2-dtb-rebuild and checks them, to reproduce the files.
#
# vf2_uEnv.txt and uEnv.txt are the hooks of StarFive's U-Boot 2021.10
# (include/configs/starfive-visionfive2.h of starfive-tech/u-boot branch
# JH7110_VisionFive2_devel, c4c67bb66ae6): its bootcmd imports
# vf2_uEnv.txt from partition 3 and runs boot2, and its distro step imports
# uEnv.txt and then runs distro_bootenv_nvme, so both name zedBSD's boot.
# boot.scr serves a U-Boot that scans for boot scripts.
include $(if $(ZEDBSD_TOPLEVEL_BUILD),userland/firmware,$(ZEDBSD_REPO_ROOT)/userland/firmware)/zedbsd-firmware.mk

override VF2_DTB_MANIFEST := platform/riscv64/visionfive2/dtb.files
override VF2_DTB_REPOSITORY_URL := $(if $(ZEDBSD_FIRMWARE_REPOSITORY_URL),$(ZEDBSD_FIRMWARE_REPOSITORY_URL)/visionfive2-dtb)
VF2_DTB_CACHE_ROOT ?= build/sources/firmware/visionfive2-dtb
# The cache is named by the manifest, so new device trees get a new cache.
override VF2_DTB_DIR := $(VF2_DTB_CACHE_ROOT)/$(firstword $(shell sha256sum $(VF2_DTB_MANIFEST)))

override VF2_DTS_REVISION := adc218676eef25575469234709c2d87185ca223a
override VF2_DTS_BASE_URL := https://raw.githubusercontent.com/torvalds/linux/$(VF2_DTS_REVISION)
override VF2_DTS_MIRROR := https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/{path}?id=$(VF2_DTS_REVISION)
override VF2_DTS_MANIFEST := platform/riscv64/visionfive2/dts.sources
VF2_DTS_CACHE_ROOT ?= build/sources/linux-dts
override VF2_DTS_DIR := $(VF2_DTS_CACHE_ROOT)/$(VF2_DTS_REVISION)
override VF2_DTS_SUBDIR := arch/riscv/boot/dts/starfive
override VF2_DTB_NAMES := jh7110-starfive-visionfive-2-v1.2a.dtb \
	jh7110-starfive-visionfive-2-v1.3b.dtb
override VF2_BOOT_DIR := $(BUILD)/visionfive2-boot
override VF2_DTBS := $(addprefix $(VF2_BOOT_DIR)/,$(VF2_DTB_NAMES))
override VF2_BOOT_SCRIPT := $(VF2_BOOT_DIR)/boot.scr
override VF2_UENV := platform/riscv64/visionfive2/uEnv.txt
override VF2_FETCH_PINNED := $(PYTHON) platform/riscv64/tools/fetch-pinned-files.py

# The files the native image adds to its EFI system partition.
override VF2_ESP_FILES := \
	$(foreach name,$(VF2_DTB_NAMES),dtb/$(name)=$(VF2_BOOT_DIR)/$(name) \
	dtb/starfive/$(name)=$(VF2_BOOT_DIR)/$(name)) \
	dtb/LICENSE=platform/riscv64/visionfive2/dtb.LICENSE \
	vf2_uEnv.txt=$(VF2_UENV) uEnv.txt=$(VF2_UENV) boot.scr=$(VF2_BOOT_SCRIPT)
override VF2_ESP_INPUTS := $(VF2_DTBS) $(VF2_BOOT_SCRIPT) $(VF2_UENV) \
	platform/riscv64/visionfive2/dtb.LICENSE

# $(call VF2_DTB_COMPILE,DIRECTORY): fetch the pinned Linux sources, compile
# the device trees into DIRECTORY the way Linux's scripts/Makefile.lib runs
# cpp and dtc (the thermal header is zedBSD's, visionfive2/include), and
# check them against dtb.files.
override VF2_DTB_COMPILE = \
	command -v cpp >/dev/null || { echo 'visionfive2: the host has no cpp' >&2; exit 1; }; \
	command -v dtc >/dev/null || { echo 'visionfive2: the host has no dtc (device-tree-compiler)' >&2; exit 1; }; \
	$(VF2_FETCH_PINNED) --base-url $(VF2_DTS_BASE_URL) --mirror '$(VF2_DTS_MIRROR)' \
		--manifest $(VF2_DTS_MANIFEST) $(VF2_DTS_DIR); \
	mkdir -p '$(1)' $(VF2_BOOT_DIR); \
	for name in $(VF2_DTB_NAMES); do \
		cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
			-I platform/riscv64/visionfive2/include -I $(VF2_DTS_DIR)/include \
			-I $(VF2_DTS_DIR)/$(VF2_DTS_SUBDIR) $(VF2_DTS_DIR)/$(VF2_DTS_SUBDIR)/$${name%.dtb}.dts \
			-o $(VF2_BOOT_DIR)/$$name.pre; \
		dtc -q -I dts -O dtb -o '$(1)'/$$name $(VF2_BOOT_DIR)/$$name.pre; \
	done; \
	$(VF2_FETCH_PINNED) --verify --manifest $(VF2_DTB_MANIFEST) '$(1)'

.PHONY: visionfive2-dtb visionfive2-dtb-rebuild visionfive2-dts visionfive2-boot-files
visionfive2-dtb: $(VF2_DTB_DIR)/.verified
visionfive2-boot-files: $(VF2_ESP_INPUTS)

# The pinned Linux sources alone (dts.sources), for the rebuild.
visionfive2-dts:
	$(VF2_FETCH_PINNED) --base-url $(VF2_DTS_BASE_URL) --mirror '$(VF2_DTS_MIRROR)' \
 --manifest $(VF2_DTS_MANIFEST) $(VF2_DTS_DIR)

# A cache once published is only checked; a missing one comes from the
# repository, or is compiled when the repository cannot give it.
$(VF2_DTB_DIR)/.verified: $(VF2_DTB_MANIFEST) platform/riscv64/tools/fetch-pinned-files.py
	@set -eu; \
	if test -e '$(VF2_DTB_DIR)' || test -L '$(VF2_DTB_DIR)'; then \
		$(VF2_FETCH_PINNED) --verify --manifest $(VF2_DTB_MANIFEST) '$(VF2_DTB_DIR)'; \
	elif test -n '$(VF2_DTB_REPOSITORY_URL)' && \
		$(VF2_FETCH_PINNED) --base-url '$(VF2_DTB_REPOSITORY_URL)' \
			--manifest $(VF2_DTB_MANIFEST) '$(VF2_DTB_DIR)'; then \
		echo 'visionfive2: fetched the device trees from the zedBSD firmware repository'; \
	else \
		echo 'visionfive2: could not fetch the device trees from the zedBSD firmware repository; compiling them from the pinned Linux sources' >&2; \
		$(call VF2_DTB_COMPILE,$(VF2_DTB_DIR).compile); \
		mv -- '$(VF2_DTB_DIR).compile' '$(VF2_DTB_DIR)'; \
	fi
	@touch $@

$(VF2_DTBS): $(VF2_BOOT_DIR)/%: $(VF2_DTB_DIR)/.verified
	@mkdir -p $(dir $@)
	cp $(VF2_DTB_DIR)/$* $@

# Reproduces the repository's device trees from the pinned sources.
visionfive2-dtb-rebuild:
	@set -eu; $(call VF2_DTB_COMPILE,$(BUILD)/visionfive2-dtb-rebuild)
	@echo 'visionfive2: $(BUILD)/visionfive2-dtb-rebuild matches $(VF2_DTB_MANIFEST)'

$(VF2_BOOT_SCRIPT): platform/riscv64/visionfive2/boot.cmd platform/riscv64/tools/make-uboot-script.py
	@mkdir -p $(dir $@)
	$(PYTHON) platform/riscv64/tools/make-uboot-script.py $< $@
