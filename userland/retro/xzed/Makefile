# zedBSD X11 server package
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include $(dir $(lastword $(MAKEFILE_LIST)))../../base/package.mk

XZED_SOURCES := userland/retro/xzed/main.c userland/retro/xzed/input.c \
	userland/retro/xzed/input-posix.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,Xzed,Xzed,*,n,basic,$(XZED_SOURCES),y,x11-servers,,))
