include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The file manager of the desktop: a Wayland window drawn on the CPU and shown with Vulkan; text from libtruetype.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_FILES_SOURCES := userland/base/zdesktop-files/main.c \
	userland/base/zdesktop-files/window.c userland/base/zdesktop-files/present.c \
	userland/base/zdesktop-files/canvas.c userland/base/zdesktop-files/text.c \
	userland/base/zdesktop-files/icons.c userland/base/zdesktop-files/ui.c \
	userland/base/zdesktop-files/ui-grid.c userland/base/zdesktop-files/dir.c \
	userland/base/zdesktop-files/mime.c userland/base/zdesktop-files/places.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-files,File manager,amd64,y,basic,$(ZDESKTOP_FILES_SOURCES),y,base,base/zdesktop-files,base/libvulkan base/libwayland base/libzdesktop base/libtruetype,0755,program,,,bin))
