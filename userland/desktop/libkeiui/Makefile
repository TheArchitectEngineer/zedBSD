include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The desktop's shared widgets and controls (WS090, plan/ws090/design.md): so far the drawing layer,
# a CPU canvas, TrueType text, icons and the Kei theme (KUI_VERSION 1).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBKEIUI_SOURCES := userland/desktop/libkeiui/version.c userland/desktop/libkeiui/canvas.c \
	userland/desktop/libkeiui/text.c userland/desktop/libkeiui/icons.c \
	userland/desktop/libkeiui/icons-line.c userland/desktop/libkeiui/theme.c

LIBKEIUI_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libkeiui.so=$(BUILD)/dynamic/libkeiui.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libkeiui,Desktop widgets,amd64,y,library,$(LIBKEIUI_SOURCES),y,desktop,desktop/libkeiui,desktop/libtruetype,,shared-library,$(LIBKEIUI_IMAGE_DATA),,lib))
