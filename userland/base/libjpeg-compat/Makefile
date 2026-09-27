include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# libjpeg-compat: the IJG libjpeg decompression interface for the base programs (plan/ws074/design.md §10,
# after plan/ws035/ws.md D1 to D6); no libjpeg code.  This first part decodes sequential Huffman JPEG
# (ws074-p019); progressive, CMYK/YCCK and saved markers come with ws074-p020.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBJPEG_COMPAT_SOURCES := userland/base/libjpeg-compat/error.c \
	userland/base/libjpeg-compat/memory.c \
	userland/base/libjpeg-compat/source.c \
	userland/base/libjpeg-compat/marker.c \
	userland/base/libjpeg-compat/huffman.c \
	userland/base/libjpeg-compat/idct.c \
	userland/base/libjpeg-compat/decompress.c

LIBJPEG_COMPAT_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libjpeg-compat.so=$(BUILD)/dynamic/libjpeg-compat.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libjpeg-compat,JPEG interface for base programs,amd64,y,library,$(LIBJPEG_COMPAT_SOURCES),y,base,base/libjpeg-compat,,,shared-library,$(LIBJPEG_COMPAT_IMAGE_DATA),,lib))
