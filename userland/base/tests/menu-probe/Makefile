include $(dir $(lastword $(MAKEFILE_LIST)))../../package.mk

# The System Menu's protocol errors and libzdesktop's own checks (WS070 p002).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MENU_PROBE_SOURCES := userland/base/tests/menu-probe/main.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,menu-probe,System Menu probe,amd64,y,basic,$(MENU_PROBE_SOURCES),y,base,base/tests/menu-probe,base/libwayland base/libzdesktop,0755,program,,,bin))
