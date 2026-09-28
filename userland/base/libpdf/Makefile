include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# libpdf: the PDF library of the base programs (plan/ws079/design-pdf.md).  ws079-p004: the
# writer (pages of filled vector paths, fill opacity, images, one attached file), the smoothed
# pressure-width outline of a pen stroke, and the reader of the writer's own documents (pages,
# boxes, content hashes, the attached file).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBPDF_SOURCES := userland/base/libpdf/writer.c userland/base/libpdf/outline.c \
	userland/base/libpdf/object.c userland/base/libpdf/reader.c

LIBPDF_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libpdf.so=$(BUILD)/dynamic/libpdf.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libpdf,PDF library for base programs,*,y,library,$(LIBPDF_SOURCES),y,base,base/libpdf,,,shared-library,$(LIBPDF_IMAGE_DATA),,lib))
