/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Cursor-only frames restore selected cached pixels without sampling unchanged client images. */
#include "scene.h"

#include <string.h>

/*
 * Allocates an optional cursor-free scene cache for one four-byte output.
 *
 * The cache is queue-owned, never host-mapped, and uses the device's standard
 * buffer/image transfers. Failure leaves no partially allocated cache.
 */
VkResult
kwl_scene_open(
	struct kwl_scene *scene,
	VkPhysicalDevice physical,
	VkDevice device,
	uint32_t width,
	uint32_t height)
{
	VkBufferCreateInfo create;
	VkMemoryRequirements requirements;
	VkPhysicalDeviceMemoryProperties properties;
	VkMemoryAllocateInfo allocate;
	uint32_t index;
	VkResult result;

	/* A whole output fits in one tightly packed transfer buffer. */
	memset(scene, 0, sizeof(*scene));
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = (VkDeviceSize)width * height * 4U;
	create.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(device, &create, NULL, &scene->buffer);
	if (result != VK_SUCCESS)
		return result;

	/* Any permitted memory type suffices for queue transfers without host access. */
	vkGetBufferMemoryRequirements(device, scene->buffer, &requirements);
	vkGetPhysicalDeviceMemoryProperties(physical, &properties);
	for (index = 0U; index < properties.memoryTypeCount; index++) {
		/* Stops at an actual permitted type, without assuming a vendor's type ordinal. */
		if ((requirements.memoryTypeBits & (1U << index)) != 0U)
			break;
	}

	/* Unsupported memory leaves the ordinary compositor path available. */
	if (index == properties.memoryTypeCount) {
		kwl_scene_close(scene, device);
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	/* Retains the independent buffer allocation until the output is idle. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = index;
	result = vkAllocateMemory(device, &allocate, NULL, &scene->memory);
	if (result == VK_SUCCESS)
		result = vkBindBufferMemory(device, scene->buffer, scene->memory, 0U);

	/* Every partial allocation failure releases the buffer before its backing. */
	if (result != VK_SUCCESS) {
		kwl_scene_close(scene, device);
		return result;
	}

	/* Geometry stays immutable throughout this output's cache lifetime. */
	scene->width = width;
	scene->height = height;

	/* Succeeded: later frame completion may publish a fully initialized cache. */
	return VK_SUCCESS;
}

/*
 * Releases an idle output's cached scene and invalidates its contents.
 */
void
kwl_scene_close(
	struct kwl_scene *scene,
	VkDevice device)
{
	/* The caller has completed every frame that could use either object. */
	vkDestroyBuffer(device, scene->buffer, NULL);
	vkFreeMemory(device, scene->memory, NULL);
	memset(scene, 0, sizeof(*scene));

	/* Succeeded: no new output can inherit the retired scene's validity. */
	return;
}

/*
 * Records a selected region's cursor-free capture or restoration.
 *
 * Regions use output coordinates and a full-output buffer stride. The caller
 * ends its scene pass before capture and starts its cursor LOAD pass afterwards.
 * Restoration happens outside any pass, after acquiring the swapchain image.
 */
void
kwl_scene_copy(
	struct kwl_scene *scene,
	struct kwl_list *command,
	VkImage image,
	const VkRect2D *region,
	unsigned restore)
{
	VkImageMemoryBarrier image_barrier;
	VkBufferMemoryBarrier buffer_barrier;
	VkBufferImageCopy copy;
	VkPipelineStageFlags source_stages;
	VkImageLayout transfer_layout;

	/* Preserves all buffer bytes outside the selected output rectangle. */
	memset(&copy, 0, sizeof(copy));
	copy.bufferRowLength = scene->width;
	copy.bufferImageHeight = scene->height;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = scene->width;
	copy.imageExtent.height = scene->height;
	copy.imageExtent.depth = 1U;
	if (region != NULL) {
		copy.imageOffset.x = region->offset.x;
		copy.imageOffset.y = region->offset.y;
		copy.imageExtent.width = region->extent.width;
		copy.imageExtent.height = region->extent.height;
		copy.bufferOffset = ((VkDeviceSize)region->offset.y * scene->width + region->offset.x) * 4U;
	}

	/* Orders earlier captures/restores and colour writes before this queue transfer. */
	memset(&image_barrier, 0, sizeof(image_barrier));
	image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = image;
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.levelCount = 1U;
	image_barrier.subresourceRange.layerCount = 1U;
	memset(&buffer_barrier, 0, sizeof(buffer_barrier));
	buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	buffer_barrier.buffer = scene->buffer;
	buffer_barrier.size = VK_WHOLE_SIZE;
	source_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	transfer_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	/* Restoring consumes the cache and writes the acquired colour image. */
	if (restore) {
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		transfer_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}

	/* Completes both the image transition and the cache's transfer dependency. */
	kwl_list_pipeline_barrier(
		command,
		source_stages,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0U,
		0U,
		NULL,
		1U,
		&buffer_barrier,
		1U,
		&image_barrier);

	/* Raw copies retain the output's actual channel order and alpha bits. */
	if (restore) {
		kwl_list_copy_buffer_to_image(command, scene->buffer, image, transfer_layout, 1U, &copy);
	} else {
		kwl_list_copy_image_to_buffer(command, image, transfer_layout, scene->buffer, 1U, &copy);
	}

	/* Makes the transferred pixels available to the next cursor's LOAD pass. */
	image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	if (restore)
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	image_barrier.oldLayout = transfer_layout;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	kwl_list_pipeline_barrier(
		command,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0U,
		0U,
		NULL,
		0U,
		NULL,
		1U,
		&image_barrier);

	/* Succeeded: the recorded transfer is ordered, but cache validity still awaits the fence. */
	return;
}
