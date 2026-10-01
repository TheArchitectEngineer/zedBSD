/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Imports a client's GPU image into window mode's Vulkan device once per
 * wl_buffer: the OS module creates the image and its bound memory; this
 * file adopts them and makes the view and descriptor sets.  A commit
 * costs no allocation and no ioctl.
 *
 * The image's move to the general layout is not submitted and waited for
 * at the import (a client's swapchain of three images waited three times
 * for the frame in flight, ws094-p009): it is recorded at the start of the
 * next frame, which is the first that can sample the image (ws099-p016).
 */

#include "compose.h"

#include <stdlib.h>
#include <string.h>

static VkResult import_image(struct zwl_compose *compose, VkFormat format, struct zwl_import *import);
static VkResult import_layout(struct zwl_compose *compose, struct zwl_import *import);
static void import_release(struct zwl_compose *compose, struct zwl_import *import);

/*
 * Adopts an image and its bound memory for a GPU buffer.
 *
 * The OS module creates them; this function makes the view the shader samples, the move to the
 * general layout and the descriptor sets.  The image and the memory are
 * taken: on failure they are destroyed with whatever else was made.
 */
VkResult
zwl_import_adopt(
	struct zwl_object *buffer,
	VkImage image,
	VkDeviceMemory memory,
	uint32_t width,
	uint32_t height,
	VkFormat format)
{
	struct zwl_compose *compose;
	struct zwl_import *import;
	VkResult result;

	/* The compositor's Vulkan device the image belongs to. */
	compose = buffer->client->server->compose;

	/* The import record, owned by the buffer; without it the image and its memory go. */
	import = calloc(1, sizeof(*import));
	if (import == NULL) {
		vkDestroyImage(compose->device, image, NULL);
		vkFreeMemory(compose->device, memory, NULL);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	/* The image, its memory and its size, drawn opaque until set_alpha says otherwise. */
	import->image = image;
	import->memory = memory;
	import->width = width;
	import->height = height;
	import->draw = ZWL_DRAW_OPAQUE;

	/* The view and descriptor sets that sample it, and its move to the general layout. */
	result = import_image(compose, format, import);
	if (result != VK_SUCCESS) {
		import_release(compose, import);
		free(import);
		return result;
	}

	/* Succeeded: the buffer can be drawn in window mode. */
	buffer->import = import;
	return VK_SUCCESS;
}

/*
 * Sets how window mode draws a GPU buffer's image: covering what is under
 * it (alpha 0) or blended by its premultiplied alpha (alpha 1).  A buffer
 * without an image has nothing to change.
 */
void
zwl_import_set_alpha(
	struct zwl_object *buffer,
	uint32_t alpha)
{
	/* No image, nothing to draw differently. */
	if (buffer->import == NULL)
		return;

	/* The drawing's blending. */
	buffer->import->draw = ZWL_DRAW_OPAQUE;
	if (alpha == 1U)
		buffer->import->draw = ZWL_DRAW_ALPHA;
}

/*
 * Releases a buffer's Vulkan image; the caller guarantees that no frame in
 * flight still samples it (the frame holds the buffer until it completes).
 */
void
zwl_import_destroy(
	struct zwl_object *buffer)
{
	struct zwl_compose *compose;

	/* A buffer without an import has nothing to release. */
	if (buffer->import == NULL)
		return;

	/* The Vulkan objects, then the record. */
	compose = buffer->client->server->compose;
	if (compose != NULL)
		import_release(compose, buffer->import);
	free(buffer->import);
	buffer->import = NULL;
}

/* Makes the view and descriptor sets that sample an imported image. */
static VkResult
import_image(
	struct zwl_compose *compose,
	VkFormat format,
	struct zwl_import *import)
{
	VkImageViewCreateInfo view;
	VkDescriptorImageInfo image_info;
	VkWriteDescriptorSet write;
	VkResult result;

	/* The view the shader samples. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = import->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	result = vkCreateImageView(compose->device, &view, NULL, &import->view);
	if (result != VK_SUCCESS)
		return result;

	/*
	 * The image goes to the layout it is sampled in, once: in the next
	 * frame's commands, or now when too many images wait.
	 */
	if (compose->layout_count < ZWL_LAYOUTS_MAX) {
		compose->layouts[compose->layout_count] = import;
		compose->layout_count++;
		import->layout_pending = 1U;
	} else {
		result = import_layout(compose, import);
		if (result != VK_SUCCESS)
			return result;
	}

	/* Its descriptor set (a spare one when there is one). */
	result = zwl_compose_set_get(compose, &import->set);
	if (result != VK_SUCCESS)
		return result;
	memset(&image_info, 0, sizeof(image_info));
	image_info.sampler = compose->sampler;
	image_info.imageView = import->view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	/* Bind the sampled image to the buffer's descriptor set. */
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = import->set;
	write.dstBinding = 0U;
	write.descriptorCount = 1U;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image_info;
	vkUpdateDescriptorSets(compose->device, 1U, &write, 0U, NULL);

	/* And the same image sampled linearly. */
	result = zwl_compose_linear_set(compose, import);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded: both sampling modes have descriptor sets. */
	return VK_SUCCESS;
}

/*
 * Moves a new image to the general layout, where it is sampled while the
 * client keeps writing it through its own image of the same memory.
 */
static VkResult
import_layout(
	struct zwl_compose *compose,
	struct zwl_import *import)
{
	VkCommandBufferAllocateInfo allocate;
	VkCommandBufferBeginInfo begin;
	VkImageMemoryBarrier barrier;
	VkSubmitInfo submit;
	VkCommandBuffer command;
	VkResult result;

	/* A one-time command buffer. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = compose->pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	result = vkAllocateCommandBuffers(compose->device, &allocate, &command);
	if (result != VK_SUCCESS)
		return result;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command, &begin);

	/* The barrier to the general layout. */
	if (result == VK_SUCCESS) {
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = 0U;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = import->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1U;
		barrier.subresourceRange.layerCount = 1U;
		vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);
		result = vkEndCommandBuffer(command);
	}

	/* Submitted and finished before the image is first drawn (once per buffer). */
	if (result == VK_SUCCESS) {
		memset(&submit, 0, sizeof(submit));
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1U;
		submit.pCommandBuffers = &command;
		result = vkQueueSubmit(compose->queue, 1U, &submit, VK_NULL_HANDLE);
	}

	/* The barrier is done before the first frame samples the image. */
	if (result == VK_SUCCESS)
		result = vkQueueWaitIdle(compose->queue);

	/* The command buffer is not kept. */
	vkFreeCommandBuffers(compose->device, compose->pool, 1U, &command);
	return result;
}

/* Destroys what an import made, whatever part of it was made. */
static void
import_release(
	struct zwl_compose *compose,
	struct zwl_import *import)
{
	unsigned index;

	/* An image still waiting for its layout is no longer waited for. */
	if (import->layout_pending) {
		for (index = 0; index < compose->layout_count; index++) {
			if (compose->layouts[index] != import)
				continue;
			compose->layout_count--;
			compose->layouts[index] = compose->layouts[compose->layout_count];
			break;
		}

		/* It waits no more. */
		import->layout_pending = 0U;
	}

	/* Each object, in the reverse order of its making. */
	if (import->set != VK_NULL_HANDLE)
		zwl_compose_set_put(compose, import->set);
	if (import->linear_set != VK_NULL_HANDLE)
		zwl_compose_set_put(compose, import->linear_set);
	if (import->view != VK_NULL_HANDLE)
		vkDestroyImageView(compose->device, import->view, NULL);
	if (import->image != VK_NULL_HANDLE)
		vkDestroyImage(compose->device, import->image, NULL);
	if (import->memory != VK_NULL_HANDLE)
		vkFreeMemory(compose->device, import->memory, NULL);
	memset(import, 0, sizeof(*import));
}

/*
 * Records the move of the images imported since the last frame to the
 * general layout, where they are sampled while their clients keep writing
 * them through their own images of the same memory.  It is recorded
 * before the frame's pass, which is the first to sample them.
 */
void
zwl_import_layouts_record(
	struct zwl_compose *compose,
	VkCommandBuffer command)
{
	VkImageMemoryBarrier barriers[ZWL_LAYOUTS_MAX];
	unsigned index;

	/* No image waits. */
	if (compose->layout_count == 0U)
		return;

	/* One barrier for each waiting image. */
	memset(barriers, 0, sizeof(barriers));
	for (index = 0; index < compose->layout_count; index++) {
		barriers[index].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barriers[index].srcAccessMask = 0U;
		barriers[index].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barriers[index].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[index].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[index].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[index].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[index].image = compose->layouts[index]->image;
		barriers[index].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barriers[index].subresourceRange.levelCount = 1U;
		barriers[index].subresourceRange.layerCount = 1U;
	}

	/* All of them before the fragment shaders sample anything. */
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, NULL, 0U, NULL,
	    compose->layout_count, barriers);
}

/*
 * Forgets the images whose move was recorded, once the frame that records
 * it is submitted (a frame not submitted records them again next time).
 */
void
zwl_import_layouts_done(
	struct zwl_compose *compose)
{
	unsigned index;

	/* Each image is in the general layout from now on. */
	for (index = 0; index < compose->layout_count; index++)
		compose->layouts[index]->layout_pending = 0U;
	compose->layout_count = 0U;
}
