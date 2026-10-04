# zedBSD userland package: ime-dict-ja, the Japanese dictionaries of the input method (WS095).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# Two SKK dictionaries go into the image under /usr/share/kei/ime/ja/, both in
# this directory:
#
#   SKK-JISYO.X    REmacs's dictionary, copied into the tree from
#                  userland/base/emacs/dict/SKK-JISYO.X on 2026-10-04 (user: "IME の
#                  辞書はコピーして取り込んでください。"); until then it was taken
#                  from a pinned REmacs archive (1a724393053e), with the same bytes
#                  (SHA-256 73819384159330a0c822915d0fd211c3e21dea77f2cfd2083273ac1ffd121ab9).
#   SKK-JISYO.kei  the supplement written for Kei, looked in before SKK-JISYO.X
#                  for the everyday words it lacks.
#
# License of SKK-JISYO.X: Awe Morris, who holds the copyright of REmacs and of the
# dictionary, relicensed it under zlib for Kei on 2026-09-29 ("私が著作権者なので、
# zlibライセンスで改めてライセンスします。…プロジェクト全体のライセンスファイルの影響下
# ということで問題ないです。", WS095 decision D1, plan/ws095/design.md section 9.1),
# and REmacs itself is in the tree under zlib since 2026-10-04.  It is covered by the
# project's own license file, and no license file of its own is installed; the file
# is installed as published, header included.
ZEDBSD_IME_DICT_MAKEFILE := $(lastword $(MAKEFILE_LIST))
ZEDBSD_IME_DICT_DIR := $(patsubst %/,%,$(dir $(ZEDBSD_IME_DICT_MAKEFILE)))
include $(ZEDBSD_IME_DICT_DIR)/../../package.mk

ZEDBSD_IME_DICT_X := $(ZEDBSD_IME_DICT_DIR)/SKK-JISYO.X
ZEDBSD_IME_DICT_KEI := $(ZEDBSD_IME_DICT_DIR)/SKK-JISYO.kei

.PHONY: ime-dict-ja
ime-dict-ja: $(ZEDBSD_IME_DICT_X) $(ZEDBSD_IME_DICT_KEI)
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
