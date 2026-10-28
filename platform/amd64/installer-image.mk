# The installer's image (make installer-image): the units an installed system is made of, their index, and the
# image the installer runs from.  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# make installer-image makes $(BUILD)/installer-image.img with ZEDBSD_IMAGE_PROFILE=installer (Makefile), which
# selects every program of the base, the desktop, the firmware and the packages.  In that order:
#
#   $(BUILD)/installer/full           the whole root, staged as an ordinary root is but of that selection
#   $(BUILD)/installer/full.owners    who put each file of it there (below)
#   $(BUILD)/installer/full.manifest  each file's unit (tools/build/installer-manifest.py)
#   $(BUILD)/installer/payload        what the image has at /opt/installer: the units, the boot files and the
#                                     index (tools/build/installer-units.py)
#   $(BUILD)/installer/index-sha256.h the index's SHA-256, which the helper is built with
#   $(BUILD)/installer/live           the image's root: the base, the desktop and the firmware of the whole
#                                     root, the installer and its helper, and the payload
#                                     (tools/build/installer-live.py)
#   $(BUILD)/installer/root.ufs       the live root as UFS, sized from it (tools/build/installer-root-size.py)
#   $(BUILD)/installer-image.img      the native layout: the ESP, the ZEDBSD FAT32 with the kernel, the logo
#                                     and a zedbsd.cfg that names the root and swap by this build's GUIDs, the
#                                     root and swap
#
# The installer and its helper are not in the whole root (the helper is built with the index's SHA-256, which is
# made from the whole root), so they are in no unit and installed nowhere but the image.
#
# The units are cut from the whole root by who put each file there.  The root's description is one list of
# --file, --subtree, --mode and link entries that each package appends to under a condition on the selection
# ($(if $(filter NAME,$(ZEDBSD_USER_PROGRAMS)),...)), so the list is expanded again with the selection narrowed to
# one program at a time: what a program's expansion has and the empty selection's has not is that program's.
# What every expansion has (the accounts, os-release, the platform's own files) is the base's.  A program's own
# command and data files are known from its registration.  tools/build/installer-manifest.py reads what is
# written here with the staged root and gives each file of the root its unit.

ifeq ($(ZEDBSD_IMAGE_PROFILE),installer)

INSTALLER_DIR := $(BUILD)/installer
INSTALLER_FULL := $(INSTALLER_DIR)/full
INSTALLER_OWNERS := $(INSTALLER_DIR)/full.owners
INSTALLER_MANIFEST := $(INSTALLER_DIR)/full.manifest
INSTALLER_PAYLOAD := $(INSTALLER_DIR)/payload
INSTALLER_INDEX := $(INSTALLER_PAYLOAD)/index
INSTALLER_INDEX_HASH := $(INSTALLER_DIR)/index-sha256.h
INSTALLER_LIVE := $(INSTALLER_DIR)/live
INSTALLER_ROOT_IMAGE := $(INSTALLER_DIR)/root.ufs
INSTALLER_PARTUUIDS := $(INSTALLER_DIR)/partuuids
INSTALLER_ZEDBSD_CONFIG := $(INSTALLER_DIR)/zedbsd.cfg
INSTALLER_AUTOLOGIN := $(INSTALLER_DIR)/autologin
INSTALLER_IMAGE := $(BUILD)/installer-image.img
INSTALLER_ITEMS := $(AMD64_PLATFORM)/installer-items

#
# The files an ordinary image may be given from the command line are not units: the installer's image takes
# a test's additions (ZEDBSD_INSTALLER_TEST_*, below) into the live root only.
#
ifneq ($(strip $(ZEDBSD_EXTRA_FILES) $(ZEDBSD_TEST_EXTRA_FILES) $(ZEDBSD_TEST_RC_CONF)),)
$(error make installer-image takes no ZEDBSD_EXTRA_FILES, ZEDBSD_TEST_EXTRA_FILES or ZEDBSD_TEST_RC_CONF; \
	a test adds to the live root with ZEDBSD_INSTALLER_TEST_FILES and ZEDBSD_INSTALLER_TEST_UNITS)
endif

#
# A test's additions to the live root and to nothing else: ZEDBSD_INSTALLER_TEST_UNITS names units of the
# whole root (network-openssh), ZEDBSD_INSTALLER_TEST_PROGRAMS programs the installer's selection does not have
# (a test's input and screen tools), ZEDBSD_INSTALLER_TEST_FILES --file DEST=SOURCE and --mode DEST=MODE
# entries (an SSH key).  Only a test's configuration sets them; the units and the index are the same with them.
#
ZEDBSD_INSTALLER_TEST_UNITS ?=
ZEDBSD_INSTALLER_TEST_PROGRAMS ?=
ZEDBSD_INSTALLER_TEST_FILES ?=

# The programs only the live root has, built as their class is: the installer's own, and a test's.
INSTALLER_LIVE_PROGRAMS := $(ZEDBSD_INSTALLER_PROGRAMS) $(ZEDBSD_INSTALLER_TEST_PROGRAMS)
$(foreach command,$(filter $(USERLAND_BASIC_PROGRAMS),$(INSTALLER_LIVE_PROGRAMS)),\
	$(eval $(call AMD64_USER_BASIC_COMMAND,$(command))))
$(foreach command,$(filter $(USERLAND_NETWORK_PROGRAMS),$(INSTALLER_LIVE_PROGRAMS)),\
	$(eval $(call AMD64_USER_NET_COMMAND,$(command))))
INSTALLER_LIVE_INPUTS := $(addprefix $(BUILD)/bin/,$(INSTALLER_LIVE_PROGRAMS)) \
	$(wildcard $(foreach entry,$(ZEDBSD_INSTALLER_TEST_FILES),$(lastword $(subst =, ,$(entry)))))
INSTALLER_LIVE_FILES := $(foreach program,$(INSTALLER_LIVE_PROGRAMS),\
	--file $(call zedbsd_userland_destination,$(program))=$(BUILD)/bin/$(program) \
	$(if $(USERLAND_$(program)_MODE),--mode $(call zedbsd_userland_destination,$(program))=$(USERLAND_$(program)_MODE))) \
	--file /etc/keiland/autologin=$(INSTALLER_AUTOLOGIN) $(ZEDBSD_INSTALLER_TEST_FILES)

# The whole root, staged as an ordinary root is (the platform's own files without a test's replacements).
$(eval $(call ZEDBSD_ROOTFS_TREE_RULE,amd64,$(ZEDBSD_ROOTFS_INPUTS),$(ZEDBSD_ROOTFS_FILES),$(INSTALLER_FULL)))

#
# The selection the root was staged from, kept apart from ZEDBSD_USER_PROGRAMS, which the expansion below
# narrows for a moment and gives back.
#
INSTALLER_SELECTION := $(ZEDBSD_USER_PROGRAMS)

#
# The words of the root's package entries (--file DEST=SOURCE, --subtree, --mode DEST=MODE, and --link
# DEST=TARGET for a link) under a selection of the programs in $(1), a word a line; the reader pairs them.  The
# selection is given back afterwards.
#
installer_words = $(eval override ZEDBSD_USER_PROGRAMS := $(1))$(foreach word,$(ZEDBSD_PACKAGE_FILES) \
	$(foreach link,$(ZEDBSD_PACKAGE_LINKS),--link $(link)),W	$(word)$(INSTALLER_NEWLINE))$(eval \
	override ZEDBSD_USER_PROGRAMS := $(INSTALLER_SELECTION))

define INSTALLER_NEWLINE


endef

#
# One block a program: P, its name, menu, registered path, class, mode, the paths it requires, its label and its
# version (an external package's release, LLVM's, or zedBSD's); then C for its command's destination (the commands the
# root installs, platform/common/userland-rootfs.mk), D for each data file, and W for each word its selection
# alone gives the entries.  The block of the empty selection (B) comes first, then the root's own list (R: the
# commands, the accounts, the base's data and the runtime), which gives the base what no program claims.
#
# The release of a package that is built from a source other than its own archive: the compiler and its C++
# runtime are LLVM's.
INSTALLER_VERSION_clang := $(ZEDBSD_LLVM_VERSION)
INSTALLER_VERSION_libcxx := $(ZEDBSD_LLVM_VERSION)

define installer_program_block
P	$(1)	$(USERLAND_$(1)_MENU)	$(USERLAND_$(1)_PACKAGE)	$(USERLAND_$(1)_CLASS)	$(USERLAND_$(1)_MODE)	$(strip $(USERLAND_$(1)_REQUIRE))	$(USERLAND_$(1)_LABEL)	$(or $(INSTALLER_VERSION_$(1)),$(ZEDBSD_EXT_$(1)_VERSION),$(ZEDBSD_VERSION))
$(if $(filter $(ZEDBSD_ROOTFS_COMMANDS),$(1)),C	$(if $(filter umount,$(1)),/sbin/umount,$(call zedbsd_userland_destination,$(1)))
)$(foreach entry,$(USERLAND_$(1)_DATA),D	$(word 1,$(subst =, ,$(entry)))
)$(call installer_words,$(1))
endef

.PHONY: installer-owners installer-image
installer-owners: $(INSTALLER_OWNERS)
installer-image: $(INSTALLER_IMAGE)

$(INSTALLER_OWNERS): $(ZEDBSD_ROOTFS_CONFIG_STAMP) $(ZEDBSD_PACKAGE_INPUTS) platform/amd64/installer-image.mk
	$(shell mkdir -p $(INSTALLER_DIR))$(file >$@.tmp,B$(INSTALLER_NEWLINE)$(call installer_words,))
	$(file >>$@.tmp,R$(INSTALLER_NEWLINE)$(foreach word,$(ZEDBSD_ROOTFS_FILES),W	$(word)$(INSTALLER_NEWLINE)))
	$(foreach program,$(INSTALLER_SELECTION),$(file >>$@.tmp,$(call installer_program_block,$(program))))
	@mv -f $@.tmp $@

$(INSTALLER_MANIFEST): $(INSTALLER_FULL)/.stamp $(INSTALLER_OWNERS) tools/build/installer-manifest.py
	$(PYTHON) tools/build/installer-manifest.py --owners $(INSTALLER_OWNERS) --tree $(INSTALLER_FULL) \
 --desktop-include userland/desktop/include --output $@

#
# The units and the boot files.  The tarballs' entries have the time of the commit the tree is at, so that the
# same tree makes the same index (and the helper is not built again for nothing).
#
INSTALLER_MTIME = $(shell git log -1 --format=%ct 2>/dev/null || echo 0)

$(INSTALLER_INDEX): $(INSTALLER_MANIFEST) $(INSTALLER_ITEMS) $(BUILD)/vmunix $(BUILD)/uefi/BOOTX64.EFI \
	$(AMD64_BOOT_LOGO) tools/build/installer-units.py
	rm -rf $(INSTALLER_PAYLOAD).tmp
	$(PYTHON) tools/build/installer-units.py --manifest $(INSTALLER_MANIFEST) --owners $(INSTALLER_OWNERS) \
 --tree $(INSTALLER_FULL) --items $(INSTALLER_ITEMS) --version '$(ZEDBSD_VERSION)' --mtime $(INSTALLER_MTIME) \
 --boot BOOTX64.EFI=esp:/EFI/BOOT/BOOTX64.EFI=$(BUILD)/uefi/BOOTX64.EFI \
 --boot vmunix=/boot/vmunix=$(BUILD)/vmunix --boot logo.ppm=/boot/logo.ppm=$(AMD64_BOOT_LOGO) \
 --output $(INSTALLER_PAYLOAD).tmp
	rm -rf $(INSTALLER_PAYLOAD)
	mv $(INSTALLER_PAYLOAD).tmp $(INSTALLER_PAYLOAD)

# The index's SHA-256 for the helper, written again only when it changes.
$(INSTALLER_INDEX_HASH): $(INSTALLER_INDEX)
	@value="#define INSTALL_ADMIN_INDEX_SHA256 \"$$(sha256sum $< | cut -d' ' -f1)\""; \
 if ! test -f $@ || ! grep -Fqx -- "$$value" $@; then printf '%s\n' "$$value" > $@.tmp; mv -f $@.tmp $@; fi

# The helper's main.c includes that header (index-sha256.h), so its object is built again when the index changes;
# the include path is its own (private), not the index's prerequisites'.
INSTALLER_ADMIN_MAIN_OBJ := $(AMD64_APP_OBJ)/userland/desktop/installer/admin/main.o
$(INSTALLER_ADMIN_MAIN_OBJ): private DYNAMIC_CPPFLAGS += -I$(INSTALLER_DIR)
$(INSTALLER_ADMIN_MAIN_OBJ): $(INSTALLER_INDEX_HASH)

$(INSTALLER_AUTOLOGIN):
	@mkdir -p $(dir $@)
	printf '%s\n' user > $@.tmp
	mv -f $@.tmp $@

$(INSTALLER_LIVE)/.stamp: $(INSTALLER_MANIFEST) $(INSTALLER_INDEX) $(INSTALLER_AUTOLOGIN) $(INSTALLER_LIVE_INPUTS) \
	tools/build/installer-live.py
	rm -rf $(INSTALLER_LIVE)
	$(PYTHON) tools/build/installer-live.py --manifest $(INSTALLER_MANIFEST) --tree $(INSTALLER_FULL) \
 $(addprefix --unit ,base desktop firmware $(ZEDBSD_INSTALLER_TEST_UNITS)) --payload $(INSTALLER_PAYLOAD) \
 $(INSTALLER_LIVE_FILES) --output $(INSTALLER_LIVE)
	touch $@

$(INSTALLER_ROOT_IMAGE): $(INSTALLER_LIVE)/.stamp $(ARCH_UFS_IMAGE_TOOLS) tools/build/installer-root-size.py
	size=$$($(PYTHON) tools/build/installer-root-size.py --tree $(INSTALLER_LIVE)) && \
 $(NOCT) --path=$(BUILD_TOOLS_DIR) $(BUILD_TOOLS_DIR)/make-arch-overlay-ufs.noct \
 --backend $(abspath $(ZEDBSD_IMAGE_HOST)) --force --profile amd64 --output $@ --tree $(INSTALLER_LIVE) \
 --size-mib $$size --min-inodes 65536

#
# The root's and swap's GUIDs, new for each image, and the configuration that names them: the image of one
# build does not take another's disk (or an ordinary image's) for its own.  The installer's image always boots
# graphically and logs in to the desktop, which opens the installer.
#
$(INSTALLER_PARTUUIDS): $(INSTALLER_ROOT_IMAGE)
	$(PYTHON) -c 'import uuid; print(uuid.uuid4(), uuid.uuid4())' > $@.tmp
	mv -f $@.tmp $@

$(INSTALLER_ZEDBSD_CONFIG): $(INSTALLER_PARTUUIDS)
	set -- $$(cat $<) && printf '%s\n' kernel=vmunix "rootpart=PARTUUID=$$1" "swap0=PARTUUID=$$2" \
 $(AMD64_GRAPHICAL_BOOT_LINES) login=graphical > $@.tmp
	$(if $(filter n,$(ZEDBSD_BOOT_KERNEL_MESSAGES)),printf '%s\n' kmsg=quiet >> $@.tmp)
	$(if $(strip $(ZEDBSD_BOOT_EXTRA_LINES)),printf '%s\n' $(ZEDBSD_BOOT_EXTRA_LINES) >> $@.tmp)
	mv -f $@.tmp $@

$(INSTALLER_IMAGE): $(BUILD)/vmunix $(BUILD)/uefi/BOOTX64.EFI $(INSTALLER_ZEDBSD_CONFIG) $(INSTALLER_ROOT_IMAGE) \
	$(AMD64_NATIVE_SWAP_IMAGE) $(ZEDBSD_IMAGE_HOST) $(AMD64_BOOT_LOGO) platform/amd64/tools/check-amd64-native-image.py
	set -- $$(cat $(INSTALLER_PARTUUIDS)) && $(ZEDBSD_IMAGE_HOST) disk --machine pcat --layout native \
 --kernel $(BUILD)/vmunix --bootx64 $(BUILD)/uefi/BOOTX64.EFI \
 --zedbsd-config $(INSTALLER_ZEDBSD_CONFIG) --logo $(AMD64_BOOT_LOGO) \
 --ufs-root $(INSTALLER_ROOT_IMAGE) --swapfile $(AMD64_NATIVE_SWAP_IMAGE) \
 --root-partuuid $$1 --swap-partuuid $$2 --fat-size-mib $(AMD64_NATIVE_BOOT_MIB) $@.unchecked
	set -- $$(cat $(INSTALLER_PARTUUIDS)) && $(PYTHON) platform/amd64/tools/check-amd64-native-image.py \
 --kernel $(BUILD)/vmunix --bootx64 $(BUILD)/uefi/BOOTX64.EFI \
 --zedbsd-config $(INSTALLER_ZEDBSD_CONFIG) --logo $(AMD64_BOOT_LOGO) \
 --ufs-root $(INSTALLER_ROOT_IMAGE) --swap $(AMD64_NATIVE_SWAP_IMAGE) \
 --root-partuuid $$1 --swap-partuuid $$2 $@.unchecked
	mv -f $@.unchecked $@

endif
