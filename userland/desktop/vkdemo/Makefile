include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# zedBSD userland package
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
$(eval $(call ZEDBSD_USERLAND_PACKAGE,vkdemo,vkdemo,amd64,y,basic,userland/desktop/vkdemo/main.c userland/desktop/vkdemo/renderer.c userland/desktop/vkdemo/display.c userland/base/common/sha256.c,y,desktop,desktop/vkdemo,desktop/libvulkan,0755,program,,,bin))
