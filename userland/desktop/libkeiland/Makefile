LIBKEILAND_MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(LIBKEILAND_MAKEFILE_DIR)../package.mk

# The wrapper of zdesktop's own Wayland extensions (the System Menu, WS070),
# and an application's way to the desktop's system and settings through
# Keiland's system extension (kl_system_*, kl_settings_*; WS131 p011: the
# library reaches no daemon and no system file itself).  The file chooser of
# ws092-p003 moved to libkeiui (ws090-p006, KEILAND_VERSION 16).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBKEILAND_SOURCES := userland/desktop/libkeiland/version.c userland/desktop/libkeiland/menu.c \
	userland/desktop/libkeiland/recent.c userland/desktop/libkeiland/titlebar.c \
	userland/desktop/libkeiland/glass.c \
	userland/desktop/libkeiland/motion.c userland/desktop/libkeiland/scroll.c \
	userland/desktop/libkeiland/gesture.c \
	userland/desktop/libkeiland/desktop.c \
	userland/desktop/libkeiland/keyboard-inset.c userland/desktop/libkeiland/edit.c \
	userland/desktop/libkeiland/settings.c userland/desktop/libkeiland/settings-cache.c \
	userland/desktop/libkeiland/settings-app.c userland/desktop/settings-keys/settings-keys.c \
	userland/desktop/libkeiland/system/system.c userland/desktop/libkeiland/system/system-view.c \
	userland/desktop/libkeiland/system/system-protocol.c userland/desktop/libkeiland/system/system-monitor.c \
	userland/desktop/libkeiland/system/system-monitor-rate.c

LIBZDESKTOP_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libkeiland.so=$(BUILD)/dynamic/libkeiland.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libkeiland,Desktop system library,amd64,y,library,$(LIBKEILAND_SOURCES),y,desktop,desktop/libkeiland,desktop/libwayland desktop/libtruetype,,shared-library,$(LIBZDESKTOP_IMAGE_DATA),,lib))
