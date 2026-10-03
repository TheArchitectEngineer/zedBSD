include $(dir $(lastword $(MAKEFILE_LIST)))../package.mk

# zedBSD standard Vulkan shared library.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIBVULKAN_SOURCES := userland/desktop/libvulkan/objects.c \
	userland/desktop/libvulkan/wire.c userland/desktop/libvulkan/context.c \
	userland/desktop/libvulkan/codec.c userland/desktop/libvulkan/dispatch.c \
	userland/desktop/libvulkan/instance.c userland/desktop/libvulkan/device.c \
	userland/desktop/libvulkan/external-properties.c userland/desktop/libvulkan/external-fence.c \
	userland/desktop/libvulkan/resources.c userland/desktop/libvulkan/pipeline.c \
	userland/desktop/libvulkan/descriptors.c userland/desktop/libvulkan/commands.c \
	userland/desktop/libvulkan/memory.c userland/desktop/libvulkan/dedicated.c \
	userland/desktop/libvulkan/sync.c \
	userland/desktop/libvulkan/queue.c userland/desktop/libvulkan/query.c \
	userland/desktop/libvulkan/wsi.c userland/desktop/libvulkan/wsi-display.c \
	userland/desktop/libvulkan/wsi-swapchain.c userland/desktop/libvulkan/wsi-wayland.c \
	userland/desktop/libvulkan/wsi-image.c userland/desktop/libvulkan/wsi-display-nodes.c

# The image metadata owns /lib placement; standalone installation honors PREFIX.
LIBVULKAN_IMAGE_DATA := $(if $(ZEDBSD_TOPLEVEL_BUILD),/lib/libvulkan.so=$(BUILD)/dynamic/libvulkan.so)
$(eval $(call ZEDBSD_USERLAND_PACKAGE,libvulkan,Vulkan 1.0,amd64,y,library,$(LIBVULKAN_SOURCES),y,desktop,desktop/libvulkan,desktop/libwayland,,shared-library,$(LIBVULKAN_IMAGE_DATA),,lib))
