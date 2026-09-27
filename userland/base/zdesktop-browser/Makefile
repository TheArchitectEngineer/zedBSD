include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The Web browser of the desktop (WS074): its own HTML, CSS, layout, JavaScript and Wasm engine.
# The sources live in one directory per module (plan/ws074/design.md §2); this is the only Makefile.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_BROWSER_DIR := userland/base/zdesktop-browser

# The tables made from published lists (the HTML named character references, the Unicode case
# mappings) are committed: tools/regenerate.sh downloads the lists at their pinned SHA-256 and runs the
# generators, and the licences' notices are installed with the program (plan/ws074/design.md §17 D4).
ZDESKTOP_BROWSER_SOURCES := $(ZDESKTOP_BROWSER_DIR)/main.c \
	$(ZDESKTOP_BROWSER_DIR)/base/arena.c \
	$(ZDESKTOP_BROWSER_DIR)/base/buffer.c \
	$(ZDESKTOP_BROWSER_DIR)/base/vector.c \
	$(ZDESKTOP_BROWSER_DIR)/base/utf.c \
	$(ZDESKTOP_BROWSER_DIR)/base/hash.c \
	$(ZDESKTOP_BROWSER_DIR)/base/file.c \
	$(ZDESKTOP_BROWSER_DIR)/base/unicode.c \
	$(ZDESKTOP_BROWSER_DIR)/base/unicode-case-table.c \
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
	$(ZDESKTOP_BROWSER_DIR)/html/entities-table.c \
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
	$(ZDESKTOP_BROWSER_DIR)/bind/handler.c \
	$(ZDESKTOP_BROWSER_DIR)/bind/timer.c \
	$(ZDESKTOP_BROWSER_DIR)/net/url.c \
	$(ZDESKTOP_BROWSER_DIR)/net/host.c \
	$(ZDESKTOP_BROWSER_DIR)/net/data.c \
	$(ZDESKTOP_BROWSER_DIR)/net/http.c \
	$(ZDESKTOP_BROWSER_DIR)/net/tls.c \
	$(ZDESKTOP_BROWSER_DIR)/net/cookie.c \
	$(ZDESKTOP_BROWSER_DIR)/page/page.c \
	$(ZDESKTOP_BROWSER_DIR)/page/link.c \
	$(ZDESKTOP_BROWSER_DIR)/page/script.c \
	$(ZDESKTOP_BROWSER_DIR)/text/font.c \
	$(ZDESKTOP_BROWSER_DIR)/text/linebreak.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/box.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/block.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/inline.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/float.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/position.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/dump.c \
	$(ZDESKTOP_BROWSER_DIR)/layout/hit.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/list.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/software.c \
	$(ZDESKTOP_BROWSER_DIR)/paint/vulkan.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/shell.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/window.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/present.c \
	$(ZDESKTOP_BROWSER_DIR)/shell/titlebar.c

# The pages the browser brings: the start page it opens without an argument (App Home starts it with
# the same page) and the pages it links to; and the notices of the licences of its committed tables.
ZDESKTOP_BROWSER_DATA := /usr/share/zdesktop-browser/start.html=$(ZDESKTOP_BROWSER_DIR)/data/start.html \
	/usr/share/zdesktop-browser/about.html=$(ZDESKTOP_BROWSER_DIR)/data/about.html \
	/usr/share/zdesktop-browser/text.html=$(ZDESKTOP_BROWSER_DIR)/data/text.html \
	/usr/share/licenses/zdesktop-browser/WHATWG-entities=userland/base/licenses/zdesktop-browser/WHATWG-entities \
	/usr/share/licenses/zdesktop-browser/Unicode-License-V3=userland/base/licenses/zdesktop-browser/Unicode-License-V3

$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-browser,Web browser,amd64,n,basic,$(ZDESKTOP_BROWSER_SOURCES),y,base,base/zdesktop-browser,base/libtruetype base/libvulkan base/libwayland base/libzdesktop,0755,program,$(ZDESKTOP_BROWSER_DATA),,bin))
