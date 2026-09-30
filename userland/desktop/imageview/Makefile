include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Image Viewer (ws091): PNG, JPEG and GIF images (libpng-compat, libjpeg-compat, libgif-compat) in a
# Wayland window, drawn with Vulkan as a textured quad under a canvas of the viewer's own words and
# cards; fit, zoom, pan and the folder's other images, with the touch screen's inertia and two-finger
# zoom (libkeiland).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
IMAGEVIEW_SOURCES := userland/desktop/imageview/main.c userland/desktop/imageview/window.c \
	userland/desktop/imageview/present.c userland/desktop/imageview/menu.c \
	userland/desktop/imageview/titlebar.c userland/desktop/imageview/glass.c \
	userland/desktop/imageview/view.c userland/desktop/imageview/draw.c \
	userland/desktop/imageview/image.c userland/desktop/imageview/folder.c \
	userland/desktop/imageview/chooser.c userland/desktop/imageview/canvas.c \
	userland/desktop/imageview/text.c userland/desktop/imageview/touch.c \
	userland/desktop/artwork/mark.c userland/desktop/picture/picture.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,imageview,Image Viewer,amd64,y,basic,$(IMAGEVIEW_SOURCES),y,desktop,desktop/imageview,desktop/libvulkan desktop/libwayland desktop/libkeiland desktop/libtruetype base/libz-compat base/libpng-compat base/libjpeg-compat base/libgif-compat,0755,program,,,bin))
