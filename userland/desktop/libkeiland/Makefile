include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The desktop's single way into the operating system, other than Vulkan and
# Wayland, and the wrapper of zdesktop's own Wayland extensions (the System
# Menu, WS070), and the parts the desktop's applications share (the file
# chooser, ws092-p003), drawn with libtruetype.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBZDESKTOP_SOURCES := userland/desktop/libkeiland/version.c userland/desktop/libkeiland/menu.c \
	userland/desktop/libkeiland/recent.c userland/desktop/libkeiland/titlebar.c \
	userland/desktop/libkeiland/glass.c userland/desktop/libkeiland/network.c \
	userland/desktop/libkeiland/motion.c userland/desktop/libkeiland/scroll.c \
	userland/desktop/libkeiland/gesture.c userland/desktop/libkeiland/network-link.c \
	userland/desktop/libkeiland/chooser.c userland/desktop/libkeiland/chooser-model.c \
	userland/desktop/libkeiland/chooser-draw.c userland/desktop/libkeiland/paint.c \
	userland/desktop/libkeiland/paint-text.c userland/desktop/libkeiland/preferences.c \
	userland/desktop/libkeiland/desktop.c \
	userland/base/net/protocol.c userland/base/net/wifi-conf.c userland/base/net/wifi-store.c

LIBZDESKTOP_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libkeiland.so=$(BUILD)/dynamic/libkeiland.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libkeiland,Desktop system library,amd64,y,library,$(LIBZDESKTOP_SOURCES),y,desktop,desktop/libkeiland,desktop/libwayland desktop/libtruetype,,shared-library,$(LIBZDESKTOP_IMAGE_DATA),,lib))
