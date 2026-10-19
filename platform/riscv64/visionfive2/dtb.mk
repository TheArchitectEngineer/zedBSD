# zedBSD VisionFive 2: the mainline device trees and the U-Boot boot files
# on the EFI system partition (BUG-339).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The vendor U-Boot's control device tree lacks the mainline compatibles the
# visionfive2 platform looks for, so the card carries the mainline Linux
# device trees of the board's v1.2a and v1.3b revisions, compiled here from
# the sources of one pinned Linux revision (GPL-2.0 OR MIT; dtb.LICENSE goes
# beside them).  The sources are fetched file by file and checked against
# dts.sources; they are not kept in the tree.  cpp and dtc are the host's.
#
# vf2_uEnv.txt and uEnv.txt are the hooks of StarFive's U-Boot 2021.10
# (include/configs/starfive-visionfive2.h of starfive-tech/u-boot branch
# JH7110_VisionFive2_devel, c4c67bb66ae6): its bootcmd imports
# vf2_uEnv.txt from partition 3 and runs boot2, and its distro step imports
# uEnv.txt and then runs distro_bootenv_nvme, so both name zedBSD's boot.
# boot.scr serves a U-Boot that scans for boot scripts.
override VF2_DTS_REVISION := adc218676eef25575469234709c2d87185ca223a
override VF2_DTS_BASE_URL := https://raw.githubusercontent.com/torvalds/linux/$(VF2_DTS_REVISION)
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

# The files the native image adds to its EFI system partition.
override VF2_ESP_FILES := \
	$(foreach name,$(VF2_DTB_NAMES),dtb/$(name)=$(VF2_BOOT_DIR)/$(name) \
	dtb/starfive/$(name)=$(VF2_BOOT_DIR)/$(name)) \
	dtb/LICENSE=platform/riscv64/visionfive2/dtb.LICENSE \
	vf2_uEnv.txt=$(VF2_UENV) uEnv.txt=$(VF2_UENV) boot.scr=$(VF2_BOOT_SCRIPT)
override VF2_ESP_INPUTS := $(VF2_DTBS) $(VF2_BOOT_SCRIPT) $(VF2_UENV) \
	platform/riscv64/visionfive2/dtb.LICENSE

.PHONY: visionfive2-dts visionfive2-boot-files
visionfive2-dts: $(VF2_DTS_DIR)/.verified
visionfive2-boot-files: $(VF2_ESP_INPUTS)

$(VF2_DTS_DIR)/.verified: $(VF2_DTS_MANIFEST) platform/riscv64/tools/fetch-pinned-files.py
	$(PYTHON) platform/riscv64/tools/fetch-pinned-files.py --base-url $(VF2_DTS_BASE_URL) \
 --manifest $(VF2_DTS_MANIFEST) $(VF2_DTS_DIR)
	@touch $@

# The host's preprocessor and compiler, the way Linux's scripts/Makefile.lib
# runs them; the thermal header is zedBSD's (visionfive2/include).
$(VF2_BOOT_DIR)/%.dtb: $(VF2_DTS_DIR)/.verified
	@command -v cpp >/dev/null || { echo 'visionfive2: the host has no cpp' >&2; exit 1; }
	@command -v dtc >/dev/null || { echo 'visionfive2: the host has no dtc (device-tree-compiler)' >&2; exit 1; }
	@mkdir -p $(dir $@)
	cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
 -I platform/riscv64/visionfive2/include -I $(VF2_DTS_DIR)/include \
 -I $(VF2_DTS_DIR)/$(VF2_DTS_SUBDIR) $(VF2_DTS_DIR)/$(VF2_DTS_SUBDIR)/$*.dts -o $@.pre
	dtc -q -I dts -O dtb -o $@ $@.pre

$(VF2_BOOT_SCRIPT): platform/riscv64/visionfive2/boot.cmd platform/riscv64/tools/make-uboot-script.py
	@mkdir -p $(dir $@)
	$(PYTHON) platform/riscv64/tools/make-uboot-script.py $< $@
