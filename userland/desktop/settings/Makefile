include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Settings (WS089): the Kei desktop's settings, a Wayland window drawn on the CPU and shown with Vulkan, the list of
# pages and the page floating as glass cards; text from libtruetype.  The file manager's canvas, text and icons
# (userland/desktop/files) and the Kei mark (userland/desktop/artwork) are compiled in unchanged (plan/ws089/design.md).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
KEILAND_SETTINGS_SOURCES := userland/desktop/settings/main.c \
	userland/desktop/settings/window.c userland/desktop/settings/present.c \
	userland/desktop/settings/glass.c userland/desktop/settings/titlebar.c \
	userland/desktop/settings/menu.c userland/desktop/settings/ui.c \
	userland/desktop/settings/widgets.c userland/desktop/settings/glyphs.c \
	userland/desktop/settings/pages.c userland/desktop/settings/about.c \
	userland/desktop/settings/page-home.c userland/desktop/settings/page-about.c \
	userland/desktop/settings/page-soon.c userland/desktop/settings/page-network.c \
	userland/desktop/settings/network.c userland/desktop/settings/search.c \
	userland/desktop/settings/look.c userland/desktop/settings/page-look.c userland/desktop/settings/page-input.c userland/desktop/settings/sound.c \
	userland/desktop/files/canvas.c userland/desktop/files/text.c \
	userland/desktop/files/icons.c userland/desktop/artwork/mark.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,settings,Settings,amd64,y,basic,$(KEILAND_SETTINGS_SOURCES),y,desktop,desktop/settings,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype,0755,program,,,bin))
