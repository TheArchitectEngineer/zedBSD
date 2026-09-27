include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# libz-compat: zedBSD's own zlib interface for the base programs (plan/ws035/ws.md D1 to D6); no zlib code.
# This first part decompresses (ws071-p010); compression comes with ws035-p040.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBZ_COMPAT_SOURCES := userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c

LIBZ_COMPAT_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libz-compat.so=$(BUILD)/dynamic/libz-compat.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libz-compat,zlib interface for base programs,amd64,y,library,$(LIBZ_COMPAT_SOURCES),y,base,base/libz-compat,,,shared-library,$(LIBZ_COMPAT_IMAGE_DATA),,lib))
