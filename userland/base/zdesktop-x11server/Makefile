include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# zdesktop's X11 server: rootless, each top-level X window a window of the desktop (WS069 p008).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_X11SERVER_SOURCES := userland/base/zdesktop-x11server/main.c \
	userland/base/zdesktop-x11server/server.c userland/base/zdesktop-x11server/protocol.c \
	userland/base/zdesktop-x11server/window.c userland/base/zdesktop-x11server/rootless.c \
	userland/base/zdesktop-x11server/wayland.c userland/base/zdesktop-x11server/keymap.c \
	userland/base/zdesktop-x11server/glyphs.c userland/base/zdesktop-x11server/glx.c \
	userland/base/zdesktop-x11server/vulkan.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-x11server,X11 server for zdesktop,amd64,y,basic,$(ZDESKTOP_X11SERVER_SOURCES),y,base,base/zdesktop-x11server,base/libvulkan base/libwayland base/libtruetype,0755,program,,,bin))
