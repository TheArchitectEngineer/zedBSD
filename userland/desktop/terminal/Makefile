include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# VT100 terminal in a Wayland window, drawn with Vulkan; glyphs from libtruetype.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KEILAND_TERMINAL_SOURCES := userland/desktop/terminal/main.c \
	userland/desktop/terminal/screen.c userland/desktop/terminal/keys.c \
	userland/desktop/terminal/font.c userland/desktop/terminal/render.c \
	userland/desktop/terminal/window.c userland/desktop/terminal/menu.c \
	userland/desktop/terminal/clipboard.c userland/desktop/terminal/tabs.c \
	userland/desktop/terminal/primary.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,terminal,Wayland terminal,amd64,y,basic,$(KEILAND_TERMINAL_SOURCES),y,base,desktop/terminal,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype,0755,program,,,bin))
