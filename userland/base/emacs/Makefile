# zedBSD userland package: REmacs (userland/base/emacs, ws129, 2026-10-04).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# REmacs (github.com/awemorris/remacs at 1a72439) is part of this tree, under
# the zlib license of its author.  It is built from here: the editor's Noct
# sources are compiled by the host's Noct into one bytecode file,
# /usr/bin/remacs.nap, which the target's noct runs; the bundled SKK dictionary
# goes to /usr/share/remacs/skkjisyo.dic.  REmacs's own Makefile is
# Makefile.remacs; it runs in a copy under BUILD, so the tree stays clean.
ZEDBSD_REMACS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(ZEDBSD_REMACS_DIR)../../../build-jobs.mk
include $(ZEDBSD_REMACS_DIR)../package.mk
include $(ZEDBSD_REMACS_DIR)../noct/version.mk

REQUIRE = base/noct
# Remacs is written in Noct, so choosing it chooses the interpreter that
# runs it.  The menu files it with the editors.
$(eval $(call ZEDBSD_USERLAND_PACKAGE,remacs,Remacs editor,*,n,package,,y,\
	packages/editors,base/emacs,$(REQUIRE)))
REQUIRE =

REMACS_SOURCE_DIR := userland/base/emacs
REMACS_SOURCES := $(wildcard $(REMACS_SOURCE_DIR)/editor/*.noct) \
	$(REMACS_SOURCE_DIR)/src/napi.def $(REMACS_SOURCE_DIR)/tools/gen-napi.py \
	$(REMACS_SOURCE_DIR)/tools/build-nap.sh $(REMACS_SOURCE_DIR)/Makefile.remacs
REMACS_WORK_DIR := $(BUILD)/packages/base/emacs/work
# The host's Noct is noct's Makefile's (userland/base/noct), which is read after
# this one (the packages are read in name order), so its stamp is named here from
# the same version and patch level; the recipe checks the two agree.
REMACS_HOST_NOCT_STAMP := build/host-noct-state/built-$(ZEDBSD_NOCT_VERSION)-$(ZEDBSD_NOCT_PATCH_LEVEL)-process
REMACS_NAP := $(BUILD)/packages/base/emacs/remacs.nap
REMACS_DICTIONARY := $(REMACS_SOURCE_DIR)/dict/SKK-JISYO.remacs

# These recursive additions are evaluated after config.mk dependencies have
# been expanded.
ZEDBSD_PACKAGE_INPUTS += $(if $(filter remacs,$(ZEDBSD_USER_PROGRAMS)),\
	$(REMACS_NAP) $(REMACS_DICTIONARY))
ZEDBSD_PACKAGE_FILES += $(if $(filter remacs,$(ZEDBSD_USER_PROGRAMS)),\
	--file $(ZEDBSD_PACKAGE_BINDIR)/remacs.nap=$(REMACS_NAP) \
	--file /usr/share/remacs/skkjisyo.dic=$(REMACS_DICTIONARY))

# The bytecode, built in a fresh copy of the sources with the host's Noct.
$(REMACS_NAP): $(REMACS_SOURCES) $(REMACS_HOST_NOCT_STAMP)
	@test '$(REMACS_HOST_NOCT_STAMP)' = '$(NOCT_HOST_BUILD_STAMP)' || \
		{ echo 'remacs: the host Noct stamp moved to $(NOCT_HOST_BUILD_STAMP)' >&2; exit 1; }
	@rm -rf $(REMACS_WORK_DIR)
	@mkdir -p $(REMACS_WORK_DIR)
	cp -R $(REMACS_SOURCE_DIR)/editor $(REMACS_SOURCE_DIR)/src $(REMACS_SOURCE_DIR)/tools \
		$(REMACS_SOURCE_DIR)/Makefile.remacs $(REMACS_WORK_DIR)/
	$(MAKE) -C $(REMACS_WORK_DIR) -f Makefile.remacs NOCT=$(abspath $(NOCT_HOST_ARTIFACT)) build/remacs.nap
	@mkdir -p $(dir $@)
	cp -f $(REMACS_WORK_DIR)/build/remacs.nap $@

.PHONY: remacs
remacs: $(REMACS_NAP)
