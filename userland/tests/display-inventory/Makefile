include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# ws113-p002: prints the GPU display inventory and its topology events (test images only).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
$(eval $(call ZEDBSD_USERLAND_PACKAGE,display-inventory,GPU display inventory probe,amd64,n,basic,userland/tests/display-inventory/main.c,y,tests,tests/display-inventory,,0755,program,,,bin))
