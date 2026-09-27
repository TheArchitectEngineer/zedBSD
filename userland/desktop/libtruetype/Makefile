include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Independent TrueType reader and glyph rasterizer; no FreeType code.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBTRUETYPE_SOURCES := userland/desktop/libtruetype/face.c \
	userland/desktop/libtruetype/cmap.c userland/desktop/libtruetype/outline.c \
	userland/desktop/libtruetype/render.c userland/desktop/libtruetype/glyph.c \
	userland/desktop/libtruetype/design.c

LIBTRUETYPE_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libtruetype.so=$(BUILD)/dynamic/libtruetype.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libtruetype,TrueType glyphs,amd64,y,library,$(LIBTRUETYPE_SOURCES),y,desktop,desktop/libtruetype,,,shared-library,$(LIBTRUETYPE_IMAGE_DATA),,lib))
