include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The Web browser of the desktop (WS074): its own HTML, CSS, layout, JavaScript and Wasm engine.
# The sources live in one directory per module (plan/ws074/design.md §2); this is the only Makefile.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_BROWSER_SOURCES := userland/base/zdesktop-browser/main.c \
	userland/base/zdesktop-browser/base/arena.c \
	userland/base/zdesktop-browser/base/buffer.c \
	userland/base/zdesktop-browser/base/vector.c \
	userland/base/zdesktop-browser/base/utf.c \
	userland/base/zdesktop-browser/base/hash.c \
	userland/base/zdesktop-browser/base/file.c \
	userland/base/zdesktop-browser/shell/shell.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-browser,Web browser,amd64,n,basic,$(ZDESKTOP_BROWSER_SOURCES),y,base,base/zdesktop-browser,,0755,program,,,bin))
