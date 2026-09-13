/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Rejects entry into the separate shared-image backend from the direct-display fixture. */

#include <assert.h>
#include "wsi-internal.h"

/* Direct-display creation must retain its existing image and CPU readback path. */
VkResult
vulkan_wsi_shared_image_create(
	struct VkDevice_T *device,
	VkFormat format,
	VkExtent2D extent,
	const VkAllocationCallbacks *allocator,
	const struct gpu_placement *placement,
	VkImage *image,
	VkDeviceMemory *memory,
	int *fd,
	struct gpu_image_descriptor *descriptor)
{
	/* These shared-image parameters must never reach this direct-display oracle. */
	(void)device;
	(void)format;
	(void)extent;
	(void)allocator;
	(void)placement;
	(void)image;
	(void)memory;
	(void)fd;
	(void)descriptor;
	assert(0);
	return VK_ERROR_FEATURE_NOT_PRESENT;
}

/* Direct display uses its separately asserted image-to-buffer copy operation. */
VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageCopy *regions)
{
	/* A shared-image GPU copy would be a backend-selection regression here. */
	(void)command;
	(void)source;
	(void)source_layout;
	(void)destination;
	(void)destination_layout;
	(void)count;
	(void)regions;
	assert(0);
	return;
}

/* Rejects GPU-only composition in this deliberately copied native-adapter fixture. */
VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	const VkClearColorValue *color,
	uint32_t count,
	const VkImageSubresourceRange *ranges)
{
	/* The copied adapter's independent oracle must remain on image-to-buffer transfer. */
	(void)command;
	(void)image;
	(void)layout;
	(void)color;
	(void)count;
	(void)ranges;
	assert(0);

	/* Succeeded only if no GPU-only composition was requested. */
	return;
}

/* Rejects GPU-only scaling in this deliberately copied native-adapter fixture. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageBlit *regions,
	VkFilter filter)
{
	/* This fixture tests the retained fallback independently from the native BLOB adapter. */
	(void)command;
	(void)source;
	(void)source_layout;
	(void)destination;
	(void)destination_layout;
	(void)count;
	(void)regions;
	(void)filter;
	assert(0);

	/* Succeeded only if no GPU-only scaling was requested. */
	return;
}

/* A copied-only adapter never requests direct GPU blit capability negotiation. */
VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(
	VkPhysicalDevice physical,
	VkFormat format,
	VkFormatProperties *properties)
{
	(void)physical;
	(void)format;
	(void)properties;
	assert(0);
	return;
}
