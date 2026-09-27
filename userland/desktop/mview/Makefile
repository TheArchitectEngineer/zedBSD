MVIEW_PACKAGE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# Wayland/Vulkan model viewer and the converted model it shows by default.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MVIEW_SOURCES := userland/desktop/mview/main.c userland/desktop/mview/window.c \
	userland/desktop/mview/renderer.c userland/desktop/mview/model.c \
	userland/desktop/mview/camera.c userland/desktop/mview/input.c
MVIEW_TEXTURES := $(sort $(notdir $(wildcard $(MVIEW_PACKAGE_DIR)models/qs40/tex/*.pam)))
MVIEW_DATA := /usr/share/mview/qs40/model.txt=userland/desktop/mview/models/qs40/model.txt \
	$(foreach texture,$(MVIEW_TEXTURES),\
 /usr/share/mview/qs40/tex/$(texture)=userland/desktop/mview/models/qs40/tex/$(texture))
$(eval $(call ZEDBSD_USERLAND_PACKAGE,mview,Wayland model viewer,amd64,y,basic,$(MVIEW_SOURCES),y,base,desktop/mview,desktop/libvulkan desktop/libwayland,0755,program,$(MVIEW_DATA),,bin))
