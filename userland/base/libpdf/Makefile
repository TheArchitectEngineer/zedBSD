include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# libpdf: the PDF library of the base programs (plan/ws079/design-pdf.md).  This first part is the
# writer (ws079-p004): pages of filled vector paths, fill opacity, one attached file and
# the pressure-width outline of a pen stroke.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBPDF_SOURCES := userland/base/libpdf/writer.c userland/base/libpdf/outline.c

LIBPDF_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libpdf.so=$(BUILD)/dynamic/libpdf.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libpdf,PDF library for base programs,*,y,library,$(LIBPDF_SOURCES),y,base,base/libpdf,,,shared-library,$(LIBPDF_IMAGE_DATA),,lib))
