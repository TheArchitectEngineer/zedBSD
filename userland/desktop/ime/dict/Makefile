# zedBSD userland package: ime-dict-ja, the Japanese dictionaries of the input method (WS095).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Two SKK dictionaries go into the image under /usr/share/kei/ime/ja/:
#
#   SKK-JISYO.X    REmacs's dictionary, taken from a pinned REmacs revision.  It
#                  stays outside the source tree: only its identity is here.
#   SKK-JISYO.kei  the supplement written for Kei (this directory), looked in
#                  before SKK-JISYO.X for the everyday words it lacks.
#
# License of SKK-JISYO.X: its header says it is distributed under the same license
# as REmacs (GPL).  Awe Morris, who holds the copyright of REmacs and of the
# dictionary, relicensed it under zlib for Kei on 2026-09-29 ("私が著作権者なので、
# zlibライセンスで改めてライセンスします。…プロジェクト全体のライセンスファイルの影響下
# ということで問題ないです。", WS095 decision D1, plan/ws095/design.md section 9.1).
# It is therefore covered by the project's own license file, and no license file
# of its own is installed; the file is installed as published, header included.
#
# The archive is GitHub's tarball of the commit.  GitHub may regenerate such a
# tarball with other bytes, so besides the archive's size and SHA-256 the
# dictionary itself is checked by its own SHA-256 after extraction; a changed
# archive with the same dictionary is then only a changed declaration.
#
# To update: pick a REmacs commit, fetch
# https://github.com/awemorris/remacs/archive/<commit>.tar.gz, and record its size,
# its SHA-256 and the SHA-256 of dict/SKK-JISYO.X in it.
ZEDBSD_IME_DICT_MAKEFILE := $(lastword $(MAKEFILE_LIST))
ZEDBSD_IME_DICT_DIR := $(patsubst %/,%,$(dir $(ZEDBSD_IME_DICT_MAKEFILE)))
include $(ZEDBSD_IME_DICT_DIR)/../../package.mk
include $(ZEDBSD_IME_DICT_DIR)/../../../packages/external.mk

# REmacs as of 2026-09-29, the revision the input method was designed against.
ZEDBSD_EXT_ime-dict-ja_VERSION := 1a724393053e
ZEDBSD_EXT_ime-dict-ja_ROOT := REmacs-1a724393053e18c4e1f502ecc5ca8ce07d99287a
ZEDBSD_EXT_ime-dict-ja_ARCHIVE := remacs-1a724393053e.tar.gz
ZEDBSD_EXT_ime-dict-ja_URL := https://github.com/awemorris/remacs/archive/1a724393053e18c4e1f502ecc5ca8ce07d99287a.tar.gz
ZEDBSD_EXT_ime-dict-ja_SIZE := 597025
ZEDBSD_EXT_ime-dict-ja_SHA256 := 419d03a195e18875e4761f4d81992905d4130697b72a5fc87849f0228a2f1507
ZEDBSD_EXT_ime-dict-ja_PATCH_LEVEL := zedbsd0
ZEDBSD_EXT_ime-dict-ja_PATCHES :=

$(eval $(call ZEDBSD_EXTERNAL_SOURCE,ime-dict-ja))

# The dictionary's own digest: 16,412 headwords, 387,465 bytes, UTF-8.
ZEDBSD_IME_DICT_X_SHA256 := 73819384159330a0c822915d0fd211c3e21dea77f2cfd2083273ac1ffd121ab9
ZEDBSD_IME_DICT_X := $(ZEDBSD_EXT_ime-dict-ja_WORKDIR)/SKK-JISYO.X
ZEDBSD_IME_DICT_KEI := $(ZEDBSD_IME_DICT_DIR)/SKK-JISYO.kei

# The dictionary is copied out of the extracted tree only when its digest holds.
$(ZEDBSD_IME_DICT_X): $(ZEDBSD_EXT_ime-dict-ja_SRCSTAMP)
	@set -eu; \
	source='$(ZEDBSD_EXT_ime-dict-ja_SRCDIR)/dict/SKK-JISYO.X'; \
	got=$$(sha256sum "$$source" | awk '{print $$1}'); \
	if test "$$got" != '$(ZEDBSD_IME_DICT_X_SHA256)'; then \
		echo "ime-dict-ja: SKK-JISYO.X SHA-256 mismatch: expected $(ZEDBSD_IME_DICT_X_SHA256), got $$got" >&2; \
		exit 1; \
	fi; \
	cp "$$source" '$@.tmp'; \
	mv '$@.tmp' '$@'

.PHONY: ime-dict-ja
ime-dict-ja: $(ZEDBSD_IME_DICT_X)
	@:

# ---------------------------------------------------------------- packaging

# Not chosen by default until the input method itself is a package (ws095-p004),
# which then requires this one.
$(eval $(call ZEDBSD_USERLAND_PACKAGE,ime-dict-ja,Japanese dictionaries of the input method,*,n,package,,y,\
	desktop,desktop/ime/dict,))

ZEDBSD_PACKAGE_INPUTS += $(if $(filter ime-dict-ja,$(ZEDBSD_USER_PROGRAMS)),\
	$(ZEDBSD_IME_DICT_X) $(ZEDBSD_IME_DICT_KEI))
ZEDBSD_PACKAGE_FILES += $(if $(filter ime-dict-ja,$(ZEDBSD_USER_PROGRAMS)),\
	--file /usr/share/kei/ime/ja/SKK-JISYO.X=$(ZEDBSD_IME_DICT_X) \
	--file /usr/share/kei/ime/ja/SKK-JISYO.kei=$(ZEDBSD_IME_DICT_KEI) \
	--mode /usr/share/kei/ime/ja/SKK-JISYO.X=0644 \
	--mode /usr/share/kei/ime/ja/SKK-JISYO.kei=0644)
