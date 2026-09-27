include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# zdesktop's X11 server: rootless, each top-level X window a window of the desktop (WS069 p008).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_X11SERVER_SOURCES := userland/desktop/xserver/main.c \
	userland/desktop/xserver/server.c userland/desktop/xserver/protocol.c \
	userland/desktop/xserver/window.c userland/desktop/xserver/rootless.c \
	userland/desktop/xserver/wayland.c userland/desktop/xserver/keymap.c \
	userland/desktop/xserver/glyphs.c userland/desktop/xserver/glx.c \
	userland/desktop/xserver/vulkan.c userland/desktop/xserver/selection.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,xserver,X11 server for zdesktop,amd64,y,basic,$(ZDESKTOP_X11SERVER_SOURCES),y,base,desktop/xserver,desktop/libvulkan desktop/libwayland desktop/libtruetype,0755,program,,,bin))
