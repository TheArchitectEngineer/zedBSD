include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The desktop's single way into the operating system, other than Vulkan and
# Wayland, and the wrapper of zdesktop's own Wayland extensions (the System
# Menu, WS070).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBZDESKTOP_SOURCES := userland/desktop/libkeiland/version.c userland/desktop/libkeiland/menu.c \
	userland/desktop/libkeiland/recent.c userland/desktop/libkeiland/titlebar.c \
	userland/desktop/libkeiland/glass.c userland/desktop/libkeiland/network.c \
	userland/base/net/protocol.c

LIBZDESKTOP_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libkeiland.so=$(BUILD)/dynamic/libkeiland.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libkeiland,Desktop system library,amd64,y,library,$(LIBZDESKTOP_SOURCES),y,base,desktop/libkeiland,desktop/libwayland,,shared-library,$(LIBZDESKTOP_IMAGE_DATA),,lib))
