# The installer's image (make installer-image): the units an installed system is made of, their index, and the
# image the installer runs from.  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# The units are cut from the whole root by who put each file there.  The root's description is one list of
# --file, --subtree, --mode and link entries that each package appends to under a condition on the selection
# ($(if $(filter NAME,$(ZEDBSD_USER_PROGRAMS)),...)), so the list is expanded again with the selection narrowed to
# one program at a time: what a program's expansion has and the empty selection's has not is that program's.
# What every expansion has (the accounts, os-release, the platform's own files) is the base's.  A program's own
# command and data files are known from its registration.  tools/build/installer-manifest.py reads what is
# written here with the staged root and gives each file of the root its unit.

INSTALLER_DIR := $(BUILD)/installer
INSTALLER_OWNERS := $(INSTALLER_DIR)/full.owners

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
# One block a program: P, its name, menu, registered path, class, mode and the paths it requires; then C for
# its command's destination (the commands the root installs, platform/common/userland-rootfs.mk), D for each
# data file, and W for each word its selection alone gives the entries.  The block of the empty selection (B)
# comes first, then the root's own list (R: the commands, the accounts, the base's data and the runtime), which
# gives the base what no program claims.
#
define installer_program_block
P	$(1)	$(USERLAND_$(1)_MENU)	$(USERLAND_$(1)_PACKAGE)	$(USERLAND_$(1)_CLASS)	$(USERLAND_$(1)_MODE)	$(strip $(USERLAND_$(1)_REQUIRE))
$(if $(filter $(ZEDBSD_ROOTFS_COMMANDS),$(1)),C	$(if $(filter umount,$(1)),/sbin/umount,$(call zedbsd_userland_destination,$(1)))
)$(foreach entry,$(USERLAND_$(1)_DATA),D	$(word 1,$(subst =, ,$(entry)))
)$(call installer_words,$(1))
endef

.PHONY: installer-owners
installer-owners: $(INSTALLER_OWNERS)

$(INSTALLER_OWNERS): $(ZEDBSD_ROOTFS_CONFIG_STAMP) $(ZEDBSD_PACKAGE_INPUTS) platform/amd64/installer-image.mk
	$(shell mkdir -p $(INSTALLER_DIR))$(file >$@.tmp,B$(INSTALLER_NEWLINE)$(call installer_words,))
	$(file >>$@.tmp,R$(INSTALLER_NEWLINE)$(foreach word,$(AMD64_ARCH_FILES),W	$(word)$(INSTALLER_NEWLINE)))
	$(foreach program,$(INSTALLER_SELECTION),$(file >>$@.tmp,$(call installer_program_block,$(program))))
	@mv -f $@.tmp $@
