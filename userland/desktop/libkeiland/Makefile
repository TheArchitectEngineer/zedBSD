include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The desktop's single way into the operating system, other than Vulkan and
# Wayland, and the wrapper of zdesktop's own Wayland extensions (the System
# Menu, WS070).  The file chooser of ws092-p003 moved to libkeiui (ws090-p006,
# KEILAND_VERSION 16).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBKEILAND_SOURCES := userland/desktop/libkeiland/version.c userland/desktop/libkeiland/menu.c \
	userland/desktop/libkeiland/recent.c userland/desktop/libkeiland/titlebar.c \
	userland/desktop/libkeiland/glass.c \
	userland/desktop/libkeiland/motion.c userland/desktop/libkeiland/scroll.c \
	userland/desktop/libkeiland/gesture.c \
	userland/desktop/libkeiland/preferences.c \
	userland/desktop/libkeiland/desktop.c \
	userland/desktop/libkeiland/keyboard-inset.c userland/desktop/libkeiland/edit.c

# The zedBSD side of the library: networkd, the interfaces and the saved keys,
# audiod (WS104).  WS105 has the Linux side in Makefile.linux.
LIBKEILAND_ZEDBSD_SOURCES := userland/desktop/libkeiland/zedbsd/network-zedbsd.c \
	userland/desktop/libkeiland/zedbsd/network-link-zedbsd.c \
	userland/desktop/libkeiland/zedbsd/audio-zedbsd.c \
	userland/base/net/protocol.c userland/base/net/wifi-conf.c userland/base/net/wifi-store.c

LIBZDESKTOP_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libkeiland.so=$(BUILD)/dynamic/libkeiland.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libkeiland,Desktop system library,amd64,y,library,$(LIBKEILAND_SOURCES) $(LIBKEILAND_ZEDBSD_SOURCES),y,desktop,desktop/libkeiland,desktop/libwayland desktop/libtruetype,,shared-library,$(LIBZDESKTOP_IMAGE_DATA),,lib))
