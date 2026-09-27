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

# The Unicode Character Database's case mappings (Unicode License V3; the notice is in the generated file).
ZDESKTOP_BROWSER_UNICODE_DATA := $(ZDESKTOP_BROWSER_DISTDIR)/UnicodeData-16.0.0.txt
ZDESKTOP_BROWSER_UNICODE_DATA_URL := https://www.unicode.org/Public/16.0.0/ucd/UnicodeData.txt
ZDESKTOP_BROWSER_UNICODE_DATA_SHA256 := ff58e5823bd095166564a006e47d111130813dcf8bf234ef79fa51a870edb48f
ZDESKTOP_BROWSER_SPECIAL_CASING := $(ZDESKTOP_BROWSER_DISTDIR)/SpecialCasing-16.0.0.txt
ZDESKTOP_BROWSER_SPECIAL_CASING_URL := https://www.unicode.org/Public/16.0.0/ucd/SpecialCasing.txt
ZDESKTOP_BROWSER_SPECIAL_CASING_SHA256 := 8d5de354eef79f2395a54c9c7dcebbaf3d30fc962d0f85611ea97aa973a0c451
ZDESKTOP_BROWSER_UNICODE_CASE_C := $(ZDESKTOP_BROWSER_GENDIR)/unicode-case.c

ZDESKTOP_BROWSER_SOURCES := $(ZDESKTOP_BROWSER_DIR)/main.c \
	$(ZDESKTOP_BROWSER_DIR)/base/arena.c \
	$(ZDESKTOP_BROWSER_DIR)/base/buffer.c \
	$(ZDESKTOP_BROWSER_DIR)/base/vector.c \
	$(ZDESKTOP_BROWSER_DIR)/base/utf.c \
	$(ZDESKTOP_BROWSER_DIR)/base/hash.c \
	$(ZDESKTOP_BROWSER_DIR)/base/file.c \
	$(ZDESKTOP_BROWSER_DIR)/base/unicode.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/heap.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/string.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/atom.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/shape.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/object.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/function.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/realm.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/code.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/number.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/operation.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/access.c \
	$(ZDESKTOP_BROWSER_DIR)/vm/interpreter.c \
	$(ZDESKTOP_BROWSER_DIR)/js/lexer.c \
	$(ZDESKTOP_BROWSER_DIR)/js/parser.c \
	$(ZDESKTOP_BROWSER_DIR)/js/parse_expr.c \
	$(ZDESKTOP_BROWSER_DIR)/js/parse_stmt.c \
	$(ZDESKTOP_BROWSER_DIR)/js/dump.c \
	$(ZDESKTOP_BROWSER_DIR)/js/scope.c \
	$(ZDESKTOP_BROWSER_DIR)/js/emit.c \
	$(ZDESKTOP_BROWSER_DIR)/js/compile.c \
	$(ZDESKTOP_BROWSER_DIR)/js/compile_expr.c \
	$(ZDESKTOP_BROWSER_DIR)/js/script.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_error.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_object.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_function.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_array.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_string.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_json.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_number.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_math.c \
	$(ZDESKTOP_BROWSER_DIR)/js/builtin_global.c \
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
	$(ZDESKTOP_BROWSER_DIR)/bind/window.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/node.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/mixin.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/element.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/text.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/document.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/event.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/timer.c \
	$(ZDESKTOP_BROWSER_DIR)/page/page.c \
	$(ZDESKTOP_BROWSER_DIR)/page/link.c \
	$(ZDESKTOP_BROWSER_DIR)/page/script.c \
	$(ZDESKTOP_BROWSER_DIR)/text/font.c \
	$(ZDESKTOP_BROWSER_DIR)/text/linebreak.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/box.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/block.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/inline.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/dump.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/hit.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/list.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/software.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/vulkan.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/shell.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/window.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/present.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/titlebar.c

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

ZDESKTOP_BROWSER_SOURCES += $(ZDESKTOP_BROWSER_UNICODE_CASE_C)

$(ZDESKTOP_BROWSER_UNICODE_DATA):
	@mkdir -p $(dir $@)
	curl --fail --location --silent --show-error --output '$@.part' '$(ZDESKTOP_BROWSER_UNICODE_DATA_URL)'
	@if test "$$(sha256sum '$@.part' | cut -d' ' -f1)" != '$(ZDESKTOP_BROWSER_UNICODE_DATA_SHA256)'; then \
		echo 'zdesktop-browser: $@ does not have the pinned SHA-256' >&2; rm -f '$@.part'; exit 1; fi
	@mv '$@.part' '$@'

$(ZDESKTOP_BROWSER_SPECIAL_CASING):
	@mkdir -p $(dir $@)
	curl --fail --location --silent --show-error --output '$@.part' '$(ZDESKTOP_BROWSER_SPECIAL_CASING_URL)'
	@if test "$$(sha256sum '$@.part' | cut -d' ' -f1)" != '$(ZDESKTOP_BROWSER_SPECIAL_CASING_SHA256)'; then \
		echo 'zdesktop-browser: $@ does not have the pinned SHA-256' >&2; rm -f '$@.part'; exit 1; fi
	@mv '$@.part' '$@'

$(ZDESKTOP_BROWSER_UNICODE_CASE_C): $(ZDESKTOP_BROWSER_UNICODE_DATA) $(ZDESKTOP_BROWSER_SPECIAL_CASING) $(ZDESKTOP_BROWSER_DIR)/tools/gen-unicode-case.py
	@mkdir -p $(dir $@)
	$(PYTHON) $(ZDESKTOP_BROWSER_DIR)/tools/gen-unicode-case.py $(ZDESKTOP_BROWSER_UNICODE_DATA) $(ZDESKTOP_BROWSER_SPECIAL_CASING) $@

ZEDBSD_USERLAND_DOWNLOAD_TARGETS += $(ZDESKTOP_BROWSER_UNICODE_DATA) $(ZDESKTOP_BROWSER_SPECIAL_CASING)
endif

# The pages the browser brings: the start page it opens without an argument (App Home starts it with
# the same page) and the pages it links to.
ZDESKTOP_BROWSER_DATA := /usr/share/zdesktop-browser/start.html=$(ZDESKTOP_BROWSER_DIR)/data/start.html \
	/usr/share/zdesktop-browser/about.html=$(ZDESKTOP_BROWSER_DIR)/data/about.html \
	/usr/share/zdesktop-browser/text.html=$(ZDESKTOP_BROWSER_DIR)/data/text.html

$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-browser,Web browser,amd64,n,basic,$(ZDESKTOP_BROWSER_SOURCES),y,base,base/zdesktop-browser,base/libtruetype base/libvulkan base/libwayland base/libzdesktop,0755,program,$(ZDESKTOP_BROWSER_DATA),,bin))
