include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# libpdf: the PDF library of the base programs (plan/ws079/design-pdf.md).  ws079-p004: the
# writer (pages of filled vector paths, fill opacity, images, one attached file), the smoothed
# pressure-width outline of a pen stroke, and the reader of the writer's own documents (pages,
# boxes, content hashes, the attached file).  ws079-p006: the page content interpreted into a
# display list (paths, strokes, colours, opacity, Multiply, images through libjpeg-compat, Flate
# through libz-compat) and the CPU rasterizer of the list.  ws079-p007: text (the text operators, embedded
# TrueType fonts through libtruetype, substitutes for the others), axial and radial shadings, cross-reference
# streams, object streams and the repair of broken cross-references.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBPDF_SOURCES := userland/base/libpdf/writer.c userland/base/libpdf/outline.c \
	userland/base/libpdf/object.c userland/base/libpdf/reader.c \
	userland/base/libpdf/filter.c userland/base/libpdf/image.c userland/base/libpdf/display.c \
	userland/base/libpdf/content.c userland/base/libpdf/stroke.c userland/base/libpdf/raster.c \
	userland/base/libpdf/font.c userland/base/libpdf/encoding.c \
	userland/base/libpdf/shading.c

LIBPDF_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libpdf.so=$(BUILD)/dynamic/libpdf.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libpdf,PDF library for base programs,*,y,library,$(LIBPDF_SOURCES),y,base,base/libpdf,base/libz-compat base/libjpeg-compat desktop/libtruetype,,shared-library,$(LIBPDF_IMAGE_DATA),,lib))
