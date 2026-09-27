include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The desktop's single way into the operating system, other than Vulkan and
# Wayland, and the wrapper of zdesktop's own Wayland extensions (the System
# Menu, WS070).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBZDESKTOP_SOURCES := userland/base/libzdesktop/version.c userland/base/libzdesktop/menu.c \
	userland/base/libzdesktop/recent.c userland/base/libzdesktop/titlebar.c \
	userland/base/libzdesktop/glass.c userland/base/libzdesktop/network.c \
	userland/base/net/protocol.c

LIBZDESKTOP_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libzdesktop.so=$(BUILD)/dynamic/libzdesktop.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libzdesktop,Desktop system library,amd64,y,library,$(LIBZDESKTOP_SOURCES),y,base,base/libzdesktop,base/libwayland,,shared-library,$(LIBZDESKTOP_IMAGE_DATA),,lib))
