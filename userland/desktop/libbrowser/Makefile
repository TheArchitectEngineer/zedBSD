include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# libbrowser (ws074-p057): the Web browser engine as a component, plan/ws074/design.md §19 -- HTML, CSS,
# the layout, JavaScript, the DOM, the network, the images and the CPU and Vulkan renderers -- behind
# <browser.h> (include/libc/browser.h).  /bin/browser is its shell; any program can show HTML with it.
# The sources stay in userland/desktop/browser, one directory per module (plan/ws074/design.md §2), where
# the tests find them; this Makefile only registers them as the library.  exports.map lets only the calls
# of <browser.h> out.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KEILAND_BROWSER_DIR := userland/desktop/browser

# The tables made from published lists (the HTML named character references, the Unicode case
# mappings) are committed: tools/regenerate.sh downloads the lists at their pinned SHA-256 and runs the
# generators, and the licences' notices are installed with the library (plan/ws074/design.md §17 D4).
LIBBROWSER_SOURCES := $(KEILAND_BROWSER_DIR)/base/arena.c \
	$(KEILAND_BROWSER_DIR)/base/buffer.c \
	$(KEILAND_BROWSER_DIR)/base/vector.c \
	$(KEILAND_BROWSER_DIR)/base/utf.c \
	$(KEILAND_BROWSER_DIR)/base/hash.c \
	$(KEILAND_BROWSER_DIR)/base/file.c \
	$(KEILAND_BROWSER_DIR)/base/unicode.c \
	$(KEILAND_BROWSER_DIR)/base/unicode-case-table.c \
	$(KEILAND_BROWSER_DIR)/vm/heap.c \
	$(KEILAND_BROWSER_DIR)/vm/string.c \
	$(KEILAND_BROWSER_DIR)/vm/atom.c \
	$(KEILAND_BROWSER_DIR)/vm/shape.c \
	$(KEILAND_BROWSER_DIR)/vm/object.c \
	$(KEILAND_BROWSER_DIR)/vm/function.c \
	$(KEILAND_BROWSER_DIR)/vm/realm.c \
	$(KEILAND_BROWSER_DIR)/vm/code.c \
	$(KEILAND_BROWSER_DIR)/vm/number.c \
	$(KEILAND_BROWSER_DIR)/vm/operation.c \
	$(KEILAND_BROWSER_DIR)/vm/access.c \
	$(KEILAND_BROWSER_DIR)/vm/interpreter.c \
	$(KEILAND_BROWSER_DIR)/js/lexer.c \
	$(KEILAND_BROWSER_DIR)/js/parser.c \
	$(KEILAND_BROWSER_DIR)/js/parse_expr.c \
	$(KEILAND_BROWSER_DIR)/js/parse_stmt.c \
	$(KEILAND_BROWSER_DIR)/js/dump.c \
	$(KEILAND_BROWSER_DIR)/js/scope.c \
	$(KEILAND_BROWSER_DIR)/js/emit.c \
	$(KEILAND_BROWSER_DIR)/js/compile.c \
	$(KEILAND_BROWSER_DIR)/js/compile_expr.c \
	$(KEILAND_BROWSER_DIR)/js/script.c \
	$(KEILAND_BROWSER_DIR)/js/builtin.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_error.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_object.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_function.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_array.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_string.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_json.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_number.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_math.c \
	$(KEILAND_BROWSER_DIR)/js/builtin_global.c \
	$(KEILAND_BROWSER_DIR)/js/tool.c \
	$(KEILAND_BROWSER_DIR)/html/input.c \
	$(KEILAND_BROWSER_DIR)/html/entities-table.c \
	$(KEILAND_BROWSER_DIR)/html/tokenizer.c \
	$(KEILAND_BROWSER_DIR)/html/parser.c \
	$(KEILAND_BROWSER_DIR)/html/modes.c \
	$(KEILAND_BROWSER_DIR)/html/body.c \
	$(KEILAND_BROWSER_DIR)/html/foreign.c \
	$(KEILAND_BROWSER_DIR)/dom/node.c \
	$(KEILAND_BROWSER_DIR)/dom/names.c \
	$(KEILAND_BROWSER_DIR)/dom/control.c \
	$(KEILAND_BROWSER_DIR)/css/tokenizer.c \
	$(KEILAND_BROWSER_DIR)/css/parser.c \
	$(KEILAND_BROWSER_DIR)/css/values.c \
	$(KEILAND_BROWSER_DIR)/css/cascade.c \
	$(KEILAND_BROWSER_DIR)/css/index.c \
	$(KEILAND_BROWSER_DIR)/css/ua.c \
	$(KEILAND_BROWSER_DIR)/bind/window.c \
	$(KEILAND_BROWSER_DIR)/bind/node.c \
	$(KEILAND_BROWSER_DIR)/bind/mixin.c \
	$(KEILAND_BROWSER_DIR)/bind/element.c \
	$(KEILAND_BROWSER_DIR)/bind/text.c \
	$(KEILAND_BROWSER_DIR)/bind/document.c \
	$(KEILAND_BROWSER_DIR)/bind/event.c \
	$(KEILAND_BROWSER_DIR)/bind/handler.c \
	$(KEILAND_BROWSER_DIR)/bind/timer.c \
	$(KEILAND_BROWSER_DIR)/bind/input.c \
	$(KEILAND_BROWSER_DIR)/net/url.c \
	$(KEILAND_BROWSER_DIR)/net/host.c \
	$(KEILAND_BROWSER_DIR)/net/data.c \
	$(KEILAND_BROWSER_DIR)/net/http.c \
	$(KEILAND_BROWSER_DIR)/net/tls.c \
	$(KEILAND_BROWSER_DIR)/net/cookie.c \
	$(KEILAND_BROWSER_DIR)/net/loader.c \
	$(KEILAND_BROWSER_DIR)/page/page.c \
	$(KEILAND_BROWSER_DIR)/page/link.c \
	$(KEILAND_BROWSER_DIR)/page/script.c \
	$(KEILAND_BROWSER_DIR)/page/images.c \
	$(KEILAND_BROWSER_DIR)/page/sheets.c \
	$(KEILAND_BROWSER_DIR)/page/network.c \
	$(KEILAND_BROWSER_DIR)/page/input.c \
	$(KEILAND_BROWSER_DIR)/page/form.c \
	$(KEILAND_BROWSER_DIR)/view/view.c \
	$(KEILAND_BROWSER_DIR)/image/decode.c \
	$(KEILAND_BROWSER_DIR)/image/jpeg.c \
	$(KEILAND_BROWSER_DIR)/image/png.c \
	$(KEILAND_BROWSER_DIR)/image/gif.c \
	$(KEILAND_BROWSER_DIR)/text/font.c \
	$(KEILAND_BROWSER_DIR)/text/linebreak.c \
	$(KEILAND_BROWSER_DIR)/layout/box.c \
	$(KEILAND_BROWSER_DIR)/layout/block.c \
	$(KEILAND_BROWSER_DIR)/layout/inline.c \
	$(KEILAND_BROWSER_DIR)/layout/float.c \
	$(KEILAND_BROWSER_DIR)/layout/position.c \
	$(KEILAND_BROWSER_DIR)/layout/replaced.c \
	$(KEILAND_BROWSER_DIR)/layout/control.c \
	$(KEILAND_BROWSER_DIR)/layout/dump.c \
	$(KEILAND_BROWSER_DIR)/layout/hit.c \
	$(KEILAND_BROWSER_DIR)/layout/bounds.c \
	$(KEILAND_BROWSER_DIR)/paint/list.c \
	$(KEILAND_BROWSER_DIR)/paint/software.c \
	$(KEILAND_BROWSER_DIR)/paint/vulkan.c

# The library, and the notices of the licences of its committed tables.
LIBBROWSER_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libbrowser.so=$(BUILD)/dynamic/libbrowser.so) \
	/usr/share/licenses/browser/WHATWG-entities=userland/base/licenses/browser/WHATWG-entities \
	/usr/share/licenses/browser/Unicode-License-V3=userland/base/licenses/browser/Unicode-License-V3

$(eval $(call ZEDBSD_USERLAND_PACKAGE,libbrowser,Web browser engine library,amd64,n,library,$(LIBBROWSER_SOURCES),y,desktop,desktop/libbrowser,desktop/libtruetype desktop/libvulkan base/libjpeg-compat base/libpng-compat base/libz-compat base/libgif-compat,,shared-library,$(LIBBROWSER_IMAGE_DATA),include/libc/browser.h,lib))
