include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Independent Wayland client library; protocol objects are maintained source.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBWAYLAND_SOURCES := userland/desktop/libwayland/client.c \
	userland/desktop/libwayland/proxy.c userland/desktop/libwayland/wire.c \
	userland/desktop/libwayland/event.c userland/desktop/libwayland/protocol.c \
	userland/desktop/libwayland/utility.c \
	userland/desktop/libwayland/menu-protocol.c \
	userland/desktop/libwayland/titlebar-protocol.c \
	userland/desktop/libwayland/glass-protocol.c \
	userland/desktop/libwayland/subsurface-protocol.c \
	userland/desktop/libwayland/data-device-protocol.c \
	userland/desktop/libwayland/primary-selection-protocol.c \
	userland/desktop/libwayland/tablet-protocol.c \
	userland/desktop/libwayland/touch-protocol.c \
	userland/desktop/libwayland/text-input-protocol.c \
	userland/desktop/libwayland/input-method-protocol.c \
	userland/desktop/libwayland/virtual-keyboard-protocol.c \
	userland/desktop/libwayland/ime-status-protocol.c \
	userland/desktop/libwayland/keyboard-inset-protocol.c \
	userland/desktop/libwayland/edit-protocol.c

# The public SONAME follows the standard client library spelling.
LIBWAYLAND_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libwayland-client.so=$(BUILD)/dynamic/libwayland-client.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libwayland-client,Wayland client,amd64,y,library,$(LIBWAYLAND_SOURCES),y,desktop,desktop/libwayland,,,shared-library,$(LIBWAYLAND_IMAGE_DATA),,lib))
