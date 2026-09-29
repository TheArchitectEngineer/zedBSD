include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The Web browser of the desktop (WS074): its own HTML, CSS, layout, JavaScript and Wasm engine.
# The sources live in one directory per module (plan/ws074/design.md §2).  Since ws074-p057 the engine
# is libbrowser.so (userland/desktop/libbrowser/Makefile lists its sources, which stay here); this
# package is /bin/browser, the shell over it: main.c (the command line and the headless modes) and
# shell/ (the zdesktop window, its titlebar and the input), which use the engine through <browser.h>.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KEILAND_BROWSER_DIR := userland/desktop/browser

KEILAND_BROWSER_SOURCES := $(KEILAND_BROWSER_DIR)/main.c \
	$(KEILAND_BROWSER_DIR)/shell/shell.c \
	$(KEILAND_BROWSER_DIR)/shell/window.c \
	$(KEILAND_BROWSER_DIR)/shell/present.c \
	$(KEILAND_BROWSER_DIR)/shell/titlebar.c \
	$(KEILAND_BROWSER_DIR)/shell/keys.c \
	$(KEILAND_BROWSER_DIR)/shell/touch.c

# The pages the browser brings: the start page it opens without an argument (App Home starts it with
# the same page) and the pages it links to.
KEILAND_BROWSER_DATA := /usr/share/browser/start.html=$(KEILAND_BROWSER_DIR)/data/start.html \
	/usr/share/browser/about.html=$(KEILAND_BROWSER_DIR)/data/about.html \
	/usr/share/browser/text.html=$(KEILAND_BROWSER_DIR)/data/text.html

$(eval $(call ZEDBSD_USERLAND_PACKAGE,browser,Web browser,amd64,n,basic,$(KEILAND_BROWSER_SOURCES),y,desktop,desktop/browser,desktop/libbrowser desktop/libvulkan desktop/libwayland desktop/libkeiland,0755,program,$(KEILAND_BROWSER_DATA),,bin))
