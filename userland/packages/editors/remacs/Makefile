# zedBSD userland package
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZEDBSD_REMACS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(ZEDBSD_REMACS_DIR)../../../../build-jobs.mk
include $(ZEDBSD_REMACS_DIR)../../../base/package.mk

REQUIRE = base/noct
# Remacs is written in Noct, so choosing it chooses the interpreter that
# runs it.
$(eval $(call ZEDBSD_USERLAND_PACKAGE,remacs,Remacs editor,*,n,package,,y,\
	packages/editors,editors/remacs,$(REQUIRE)))
REQUIRE =

REMACS_REPOSITORY ?= https://github.com/awemorris/remacs.git
REMACS_GIT_REF ?= main
REMACS_SOURCE_DIR := build/sources/remacs
REMACS_SOURCE_STAMP := $(REMACS_SOURCE_DIR)/.zedbsd-source
REMACS_PATCH := $(ZEDBSD_REMACS_DIR)patches/0001-use-noct2-bytecode-extension.patch
REMACS_PATCH_STAMP := $(REMACS_SOURCE_DIR)/.zedbsd-noct2-patch
REMACS_HOST_NOCT := $(NOCT_HOST_ARTIFACT)
REMACS_NAP := $(BUILD)/packages/editors/remacs/remacs.nap
REMACS_DICTIONARY := $(REMACS_SOURCE_DIR)/dict/SKK-JISYO.remacs

# These recursive additions are evaluated after config.mk dependencies have
# been expanded.  Merely opening menuconfig therefore performs no network I/O.
ZEDBSD_PACKAGE_INPUTS += $(if $(filter remacs,$(ZEDBSD_USER_PROGRAMS)),\
	$(REMACS_NAP) $(REMACS_DICTIONARY))
ZEDBSD_PACKAGE_FILES += $(if $(filter remacs,$(ZEDBSD_USER_PROGRAMS)),\
	--file $(ZEDBSD_PACKAGE_BINDIR)/remacs.nap=$(REMACS_NAP) \
	--file /usr/share/remacs/skkjisyo.dic=$(REMACS_DICTIONARY))

# The dictionary is one of the cloned files, so the clone produces it.  Saying
# so keeps a tree without build/sources from stopping with "no rule to make".
$(REMACS_DICTIONARY): $(REMACS_SOURCE_STAMP)
	@test -f $@

$(REMACS_SOURCE_STAMP):
	@mkdir -p $(dir $(REMACS_SOURCE_DIR))
	git clone --depth 1 --branch $(REMACS_GIT_REF) \
		$(REMACS_REPOSITORY) $(REMACS_SOURCE_DIR)
	@git -C $(REMACS_SOURCE_DIR) rev-parse HEAD > $@

$(REMACS_PATCH_STAMP): $(REMACS_SOURCE_STAMP) $(REMACS_PATCH)
	patch --batch --forward --fuzz=0 -d $(REMACS_SOURCE_DIR) -p1 < $(REMACS_PATCH)
	@touch $@

$(REMACS_NAP): $(REMACS_PATCH_STAMP) $(NOCT_HOST_BUILD_STAMP)
	$(MAKE) -C $(REMACS_SOURCE_DIR) NOCT=$(abspath $(REMACS_HOST_NOCT)) all
	@mkdir -p $(dir $@)
	cp -f $(REMACS_SOURCE_DIR)/build/remacs.nap $@

.PHONY: remacs
remacs: $(REMACS_NAP)
