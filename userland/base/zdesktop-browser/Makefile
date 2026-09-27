include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The Web browser of the desktop (WS074): its own HTML, CSS, layout, JavaScript and Wasm engine.
# The sources live in one directory per module (plan/ws074/design.md §2); this is the only Makefile.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_BROWSER_DIR := userland/base/zdesktop-browser

# Tables made from published lists at build time; the lists are downloaded, checked against their
# SHA-256 and kept in distfiles/ (not in git), like noct's archive.  The HTML named character
# references are the WHATWG HTML Standard's (CC BY 4.0; the notice is in the generated file).
ZDESKTOP_BROWSER_DISTDIR := $(ZDESKTOP_BROWSER_DIR)/distfiles
ZDESKTOP_BROWSER_ENTITIES_JSON := $(ZDESKTOP_BROWSER_DISTDIR)/entities.json
ZDESKTOP_BROWSER_ENTITIES_URL := https://html.spec.whatwg.org/entities.json
ZDESKTOP_BROWSER_ENTITIES_SHA256 := d741d877ac77c4194c4ad526b5b4a19aef8dfe411ab840a466891cdbb9f362e6
ZDESKTOP_BROWSER_GENDIR := $(BUILD)/zdesktop-browser-gen
ZDESKTOP_BROWSER_ENTITIES_C := $(ZDESKTOP_BROWSER_GENDIR)/html-entities.c

ZDESKTOP_BROWSER_SOURCES := $(ZDESKTOP_BROWSER_DIR)/main.c \
	$(ZDESKTOP_BROWSER_DIR)/base/arena.c \
	$(ZDESKTOP_BROWSER_DIR)/base/buffer.c \
	$(ZDESKTOP_BROWSER_DIR)/base/vector.c \
	$(ZDESKTOP_BROWSER_DIR)/base/utf.c \
	$(ZDESKTOP_BROWSER_DIR)/base/hash.c \
	$(ZDESKTOP_BROWSER_DIR)/base/file.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/heap.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/string.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/atom.c \
	$(ZDESKTOP_BROWSER_DIR)/html/input.c \
	$(ZDESKTOP_BROWSER_DIR)/html/tokenizer.c \
	$(ZDESKTOP_BROWSER_DIR)/html/parser.c \
	$(ZDESKTOP_BROWSER_DIR)/html/modes.c \
	$(ZDESKTOP_BROWSER_DIR)/html/body.c \
	$(ZDESKTOP_BROWSER_DIR)/html/foreign.c \
	$(ZDESKTOP_BROWSER_DIR)/dom/node.c \
	$(ZDESKTOP_BROWSER_DIR)/dom/names.c \
	$(ZDESKTOP_BROWSER_DIR)/css/tokenizer.c \
	$(ZDESKTOP_BROWSER_DIR)/css/parser.c \
	$(ZDESKTOP_BROWSER_DIR)/css/values.c \
	$(ZDESKTOP_BROWSER_DIR)/css/cascade.c \
	$(ZDESKTOP_BROWSER_DIR)/css/ua.c \
	$(ZDESKTOP_BROWSER_DIR)/page/page.c \
	$(ZDESKTOP_BROWSER_DIR)/text/font.c \
	$(ZDESKTOP_BROWSER_DIR)/text/linebreak.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/box.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/block.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/inline.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/dump.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/list.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/software.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/vulkan.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/shell.c

ifdef ZEDBSD_TOPLEVEL_BUILD
ZDESKTOP_BROWSER_SOURCES += $(ZDESKTOP_BROWSER_ENTITIES_C)

$(ZDESKTOP_BROWSER_ENTITIES_JSON):
	@mkdir -p $(dir $@)
	curl --fail --location --silent --show-error --output '$@.part' '$(ZDESKTOP_BROWSER_ENTITIES_URL)'
	@if test "$$(sha256sum '$@.part' | cut -d' ' -f1)" != '$(ZDESKTOP_BROWSER_ENTITIES_SHA256)'; then \
		echo 'zdesktop-browser: $@ does not have the pinned SHA-256' >&2; rm -f '$@.part'; exit 1; fi
	@mv '$@.part' '$@'

$(ZDESKTOP_BROWSER_ENTITIES_C): $(ZDESKTOP_BROWSER_ENTITIES_JSON) $(ZDESKTOP_BROWSER_DIR)/tools/gen-entities.py
	@mkdir -p $(dir $@)
	$(PYTHON) $(ZDESKTOP_BROWSER_DIR)/tools/gen-entities.py $< $@

ZEDBSD_USERLAND_DOWNLOAD_TARGETS += $(ZDESKTOP_BROWSER_ENTITIES_JSON)
endif

$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-browser,Web browser,amd64,n,basic,$(ZDESKTOP_BROWSER_SOURCES),y,base,base/zdesktop-browser,base/libtruetype base/libvulkan,0755,program,,,bin))
