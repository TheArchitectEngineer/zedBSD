include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# The file manager of the desktop: a Wayland window drawn on the CPU and shown with Vulkan; text from libtruetype.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
ZDESKTOP_FILES_SOURCES := userland/base/zdesktop-files/main.c \
	userland/base/zdesktop-files/window.c userland/base/zdesktop-files/present.c \
	userland/base/zdesktop-files/canvas.c userland/base/zdesktop-files/text.c \
	userland/base/zdesktop-files/icons.c userland/base/zdesktop-files/ui.c \
	userland/base/zdesktop-files/ui-tabs.c \
	userland/base/zdesktop-files/ui-grid.c userland/base/zdesktop-files/dir.c \
	userland/base/zdesktop-files/mime.c userland/base/zdesktop-files/places.c \
	userland/base/zdesktop-files/ui-input.c userland/base/zdesktop-files/ui-list.c \
	userland/base/zdesktop-files/ui-field.c userland/base/zdesktop-files/select.c \
	userland/base/zdesktop-files/task.c userland/base/zdesktop-files/trash.c \
	userland/base/zdesktop-files/undo.c userland/base/zdesktop-files/clip.c \
	userland/base/zdesktop-files/actions.c userland/base/zdesktop-files/ui-overlay.c \
	userland/base/zdesktop-files/tags.c userland/base/zdesktop-files/search.c \
	userland/base/zdesktop-files/ui-search.c userland/base/zdesktop-files/ui-home.c \
	userland/base/zdesktop-files/thumb.c userland/base/zdesktop-files/peek.c \
	userland/base/zdesktop-files/ui-preview.c userland/base/zdesktop-files/apps.c \
	userland/base/zdesktop-files/info.c userland/base/zdesktop-files/ui-info.c \
	userland/base/zdesktop-files/ui-menu.c userland/base/zdesktop-files/ui-help.c \
	userland/base/zdesktop-files/menu.c userland/base/zdesktop-files/titlebar.c \
	userland/base/zdesktop-files/ui-titlebar.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,zdesktop-files,File manager,amd64,y,basic,$(ZDESKTOP_FILES_SOURCES),y,base,base/zdesktop-files,base/libvulkan base/libwayland base/libzdesktop base/libtruetype,0755,program,,,bin))
