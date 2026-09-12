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
