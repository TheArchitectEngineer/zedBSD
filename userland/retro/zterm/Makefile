# zedBSD Unicode VT100 terminal
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
include $(dir $(lastword $(MAKEFILE_LIST)))../../base/package.mk

$(eval $(call ZEDBSD_USERLAND_PACKAGE,zterm,zterm,*,n,basic,userland/retro/zterm/main.c userland/retro/libX11/xlib.c,y,x11-applications,,))
ZEDBSD_PACKAGE_INPUTS += userland/retro/zterm/app-icon.xpm
ZEDBSD_PACKAGE_FILES += $(if $(filter zterm,$(ZEDBSD_USER_PROGRAMS)),--file /usr/share/zterm/icons/app-icon.xpm=userland/retro/zterm/app-icon.xpm)
