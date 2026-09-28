include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Portable standard Wayland/Vulkan presentation test.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
WLTEST_SOURCES := userland/desktop/wltest/main.c userland/desktop/wltest/window.c \
	userland/desktop/wltest/renderer.c
$(eval $(call ZEDBSD_USERLAND_PACKAGE,wltest,Wayland Vulkan test,amd64,y,basic,$(WLTEST_SOURCES),y,desktop,desktop/wltest,desktop/libvulkan desktop/libwayland,0755,program,,,bin))
