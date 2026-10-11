/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The cursor plane (ws141-p014): when the output's display has a small
 * VK_KHR_display plane above the output's, with per-pixel premultiplied
 * alpha, the cursor is presented on it instead of being composed into the
 * frames.  A move then presents the plane's image again at a new place
 * (VkDisplayPresentInfoKHR's dstRect, its hotspot subtracted); a new cursor
 * image is copied into the plane's swapchain image first.  Only standard
 * Vulkan is used: an output without such a plane composes the cursor as
 * before.  A drag, a client's cursor without a host-written image or larger
 * than the plane, and a pointer on another display are composed too.
 */

#include "compose.h"
#include "extras.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int plane_state(struct kwl_server *server, const struct kwl_import **image, int32_t *x, int32_t *y);
static VkResult plane_find(struct kwl_compose *compose, VkDisplayModeKHR *mode, uint32_t *index, uint32_t *stack, VkExtent2D *extent);
static VkResult plane_mode(struct kwl_compose *compose, VkDisplayModeKHR *mode);
static VkResult plane_objects(struct kwl_compose *compose, struct kwl_cursor_plane *plane);
static void plane_present(struct kwl_server *server, struct kwl_cursor_plane *plane, const struct kwl_import *source, int32_t x, int32_t y);
static void plane_record(struct kwl_cursor_plane *plane, VkCommandBuffer command, VkImage target, const struct kwl_import *source);

/*
 * Makes the cursor plane's surface and swapchain after the output's, when the display has such a plane.
 *
 * Any failure leaves the plane closed, and the cursor composed.
 */
void
kwl_cursor_plane_open(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	struct kwl_cursor_plane *plane;
	VkDisplaySurfaceCreateInfoKHR create;
	VkDisplayModeKHR mode;
	VkExtent2D extent;
	uint32_t index;
	uint32_t stack;
	VkResult result;

	/* Nothing when it is open already. */
	compose = server->compose;
	plane = &compose->cursor_plane;
	if (plane->open)
		return;
	memset(plane, 0, sizeof(*plane));

	/* A small premultiplied plane of the output's display and mode, or none. */
	result = plane_find(compose, &mode, &index, &stack, &extent);
	if (result != VK_SUCCESS)
		return;

	/* Its surface, with per-pixel premultiplied alpha. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
	create.displayMode = mode;
	create.planeIndex = index;
	create.planeStackIndex = stack;
	create.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	create.globalAlpha = 1.0f;
	create.alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
	create.imageExtent = extent;
	plane->output.width = extent.width;
	plane->output.height = extent.height;
	result = vkCreateDisplayPlaneSurfaceKHR(compose->instance, &create, NULL, &plane->output.surface);
	if (result != VK_SUCCESS) {
		printf("KWL CURSOR_PLANE unavailable step=surface result=%d\n", (int)result);
		memset(plane, 0, sizeof(*plane));
		return;
	}

	/* Its swapchain, its images filled by copies. */
	result = vkdemo_display_create_swapchain_usage(compose->physical, compose->device, compose->family, &plane->output, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	if (result == VK_SUCCESS && plane->output.image_count > KWL_CURSOR_PLANE_IMAGES)
		result = VK_ERROR_INITIALIZATION_FAILED;
	if (result == VK_SUCCESS)
		result = plane_objects(compose, plane);
	if (result != VK_SUCCESS) {
		printf("KWL CURSOR_PLANE unavailable step=swapchain result=%d\n", (int)result);
		plane->open = 1;
		kwl_cursor_plane_close(server);
		return;
	}

	/* Succeeded: the next pass shows the cursor on it. */
	plane->open = 1;
	printf("KWL CURSOR_PLANE open plane=%u width=%u height=%u images=%u format=%d\n", index, extent.width, extent.height, plane->output.image_count, (int)plane->output.format);
}

/*
 * Destroys the cursor plane's swapchain and surface once the device is idle (before the output's).
 */
void
kwl_cursor_plane_close(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	struct kwl_cursor_plane *plane;
	unsigned index;

	/* A closed plane has nothing. */
	compose = server->compose;
	plane = &compose->cursor_plane;
	if (!plane->open)
		return;
	(void)vkDeviceWaitIdle(compose->device);

	/* The commands, fences and semaphores, then the swapchain and surface. */
	for (index = 0; index < KWL_CURSOR_PLANE_IMAGES; index++) {
		if (plane->commands[index] != VK_NULL_HANDLE)
			vkFreeCommandBuffers(compose->device, compose->pool, 1U, &plane->commands[index]);
		if (plane->fences[index] != VK_NULL_HANDLE)
			vkDestroyFence(compose->device, plane->fences[index], NULL);
		if (plane->ready[index] != VK_NULL_HANDLE)
			vkDestroySemaphore(compose->device, plane->ready[index], NULL);
	}
	for (index = 0; index < KWL_CURSOR_PLANE_IMAGES + 1U; index++) {
		if (plane->acquired[index] != VK_NULL_HANDLE)
			vkDestroySemaphore(compose->device, plane->acquired[index], NULL);
	}
	vkdemo_display_close(compose->instance, compose->device, &plane->output);

	/* A frame shows the cursor again. */
	if (plane->shown)
		server->dirty = 1;
	memset(plane, 0, sizeof(*plane));
	printf("KWL CURSOR_PLANE closed\n");
}

/*
 * Tells whether the cursor plane shows the cursor as it is now, so that a pointer move damages no frame.
 */
int
kwl_cursor_plane_takes(
	struct kwl_server *server)
{
	const struct kwl_import *image;
	int32_t x;
	int32_t y;
	int state;

	/* Only an open plane that can show the cursor as it is. */
	if (server->compose == NULL || !server->compose->cursor_plane.open)
		return 0;
	state = plane_state(server, &image, &x, &y);
	return state >= 0;
}

/*
 * Brings the cursor plane up to date at a pass of the event loop: its image and its place.
 *
 * A change between the plane and the composed cursor redraws the frame,
 * which draws the cursor or leaves it out.  When no swapchain image is
 * free the update waits for a later pass.
 */
void
kwl_cursor_plane_update(
	struct kwl_server *server)
{
	struct kwl_cursor_plane *plane;
	const struct kwl_import *image;
	int32_t x;
	int32_t y;
	int state;

	/* An output without the plane composes the cursor. */
	if (server->compose == NULL || !server->compose->cursor_plane.open || !server->compose->output_open)
		return;
	plane = &server->compose->cursor_plane;
	state = plane_state(server, &image, &x, &y);

	/* Moving between the plane and composing redraws the frame with or without the cursor. */
	if ((state >= 0) != (plane->shown != 0U)) {
		plane->shown = state >= 0 ? 1U : 0U;
		server->dirty = 1;
	}

	/* A composed cursor leaves the plane empty; an empty plane stays where it was. */
	if (state <= 0) {
		image = NULL;
		x = plane->x;
		y = plane->y;
	}

	/* Nothing changed since the last present. */
	if (plane->presented &&
	    plane->source == image &&
	    (image == NULL || (plane->version == image->version && plane->x == x && plane->y == y)))
		return;

	/* The image at its place. */
	plane_present(server, plane, image, x, y);
}

/*
 * Finds what the plane shows: 1 with an image and its top-left place, 0 for nothing, -1 when the cursor must be composed.
 */
static int
plane_state(
	struct kwl_server *server,
	const struct kwl_import **image,
	int32_t *x,
	int32_t *y)
{
	struct kwl_cursor_plane *plane;
	int32_t hotspot_x;
	int32_t hotspot_y;
	int state;

	/* The cursor as the frames would draw it. */
	plane = &server->compose->cursor_plane;
	state = kwl_compose_cursor_source(server, image, &hotspot_x, &hotspot_y);
	*x = server->pointer_x - hotspot_x;
	*y = server->pointer_y - hotspot_y;
	if (state <= 0)
		return state;

	/* The plane is the anchor's; another display composes its cursor. */
	if (server->pointer_output != KWL_PLANE_ANCHOR)
		return -1;

	/* Only a host-written image (the arrow, the shapes, a wl_shm cursor) that fits the plane. */
	if ((*image)->staging == VK_NULL_HANDLE ||
	    (*image)->width > plane->output.width ||
	    (*image)->height > plane->output.height)
		return -1;

	/* Succeeded: the plane shows this image there. */
	return 1;
}

/* Finds a plane of the output's display and mode with per-pixel premultiplied alpha and a small image, above the output's. */
static VkResult
plane_find(
	struct kwl_compose *compose,
	VkDisplayModeKHR *mode,
	uint32_t *index,
	uint32_t *stack,
	VkExtent2D *extent)
{
	VkDisplayPlanePropertiesKHR properties[8];
	VkDisplayPlaneCapabilitiesKHR capabilities;
	VkDisplayKHR displays[8];
	uint32_t count;
	uint32_t display_count;
	uint32_t plane;
	uint32_t at;
	VkResult result;
	int matches;

	/* The mode the output shows. */
	result = plane_mode(compose, mode);
	if (result != VK_SUCCESS)
		return result;

	/* Every plane; a display with more than this many is looked at only so far. */
	count = 8U;
	result = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(compose->physical, &count, properties);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
		return result;

	/* The first plane that fits. */
	for (plane = 0; plane < count; plane++) {
		/* One that drives the output's display. */
		display_count = 8U;
		result = vkGetDisplayPlaneSupportedDisplaysKHR(compose->physical, plane, &display_count, displays);
		if (result != VK_SUCCESS && result != VK_INCOMPLETE)
			continue;
		matches = 0;
		for (at = 0; at < display_count; at++) {
			if (displays[at] == compose->display)
				matches = 1;
		}
		if (!matches)
			continue;

		/* With per-pixel premultiplied alpha, a small image and places partly off the output. */
		result = vkGetDisplayPlaneCapabilitiesKHR(compose->physical, *mode, plane, &capabilities);
		if (result != VK_SUCCESS)
			continue;
		if ((capabilities.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR) == 0U)
			continue;
		if (capabilities.maxSrcExtent.width < 32U || capabilities.maxSrcExtent.width > 256U ||
		    capabilities.maxSrcExtent.height < 32U || capabilities.maxSrcExtent.height > 256U)
			continue;
		if (capabilities.minDstPosition.x >= 0 || capabilities.minDstPosition.y >= 0)
			continue;

		/* At most 64 pixels square, which every cursor image fits. */
		*index = plane;
		*stack = properties[plane].currentStackIndex;
		extent->width = capabilities.maxSrcExtent.width < 64U ? capabilities.maxSrcExtent.width : 64U;
		extent->height = capabilities.maxSrcExtent.height < 64U ? capabilities.maxSrcExtent.height : 64U;
		return VK_SUCCESS;
	}

	/* No plane fits: the cursor is composed. */
	return VK_ERROR_INITIALIZATION_FAILED;
}

/* Finds the mode of the output's display with the output's size. */
static VkResult
plane_mode(
	struct kwl_compose *compose,
	VkDisplayModeKHR *mode)
{
	VkDisplayModePropertiesKHR modes[16];
	uint32_t count;
	uint32_t index;
	VkResult result;

	/* The display's modes, as many as fit. */
	count = 16U;
	result = vkGetDisplayModePropertiesKHR(compose->physical, compose->display, &count, modes);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
		return result;

	/* The first of the output's size. */
	for (index = 0; index < count; index++) {
		if (modes[index].parameters.visibleRegion.width == compose->output.width &&
		    modes[index].parameters.visibleRegion.height == compose->output.height) {
			*mode = modes[index].displayMode;
			return VK_SUCCESS;
		}
	}

	/* None: no plane is looked for. */
	return VK_ERROR_INITIALIZATION_FAILED;
}

/* Makes a command buffer, fence and semaphore for each swapchain image, and the acquisitions' semaphores. */
static VkResult
plane_objects(
	struct kwl_compose *compose,
	struct kwl_cursor_plane *plane)
{
	VkCommandBufferAllocateInfo allocate;
	VkFenceCreateInfo fence;
	VkSemaphoreCreateInfo semaphore;
	unsigned index;
	VkResult result;

	/* One of each per image; the fences start signaled, as no copy is in flight. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = compose->pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	memset(&semaphore, 0, sizeof(semaphore));
	semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (index = 0; index < plane->output.image_count; index++) {
		result = vkAllocateCommandBuffers(compose->device, &allocate, &plane->commands[index]);
		if (result == VK_SUCCESS)
			result = vkCreateFence(compose->device, &fence, NULL, &plane->fences[index]);
		if (result == VK_SUCCESS)
			result = vkCreateSemaphore(compose->device, &semaphore, NULL, &plane->ready[index]);
		if (result != VK_SUCCESS)
			return result;
	}

	/* One more acquisition semaphore than images, so a free one is always there. */
	for (index = 0; index < plane->output.image_count + 1U; index++) {
		result = vkCreateSemaphore(compose->device, &semaphore, NULL, &plane->acquired[index]);
		if (result != VK_SUCCESS)
			return result;
	}

	/* Succeeded: the plane can be presented. */
	return VK_SUCCESS;
}

/*
 * Presents the plane's image at a place: copies the cursor image into a
 * free swapchain image first unless that image holds it already.  A
 * failure closes the plane, and the cursor is composed again.
 */
static void
plane_present(
	struct kwl_server *server,
	struct kwl_cursor_plane *plane,
	const struct kwl_import *source,
	int32_t x,
	int32_t y)
{
	struct kwl_compose *compose;
	VkDisplayPresentInfoKHR display;
	VkPresentInfoKHR present;
	VkSubmitInfo submit;
	VkPipelineStageFlags stage;
	VkSemaphore acquired;
	VkSemaphore wait;
	uint32_t image;
	uint32_t version;
	VkResult result;

	/*
	 * A copy still running holds this pass back before an image is acquired:
	 * an acquired image must be presented, and waiting here would stop the
	 * main loop behind the GPU.  The next pass tries again.
	 */
	compose = server->compose;
	if (plane->copied) {
		result = vkGetFenceStatus(compose->device, plane->fences[plane->last_copy]);
		if (result == VK_NOT_READY)
			return;
		if (result != VK_SUCCESS) {
			printf("KWL CURSOR_PLANE failed step=fence result=%d\n", (int)result);
			kwl_cursor_plane_close(server);
			return;
		}
	}

	/* A free image now, or at a later pass. */
	acquired = plane->acquired[plane->next];
	result = vkAcquireNextImageKHR(compose->device, plane->output.swapchain, 0U, acquired, VK_NULL_HANDLE, &image);
	if (result == VK_NOT_READY || result == VK_TIMEOUT)
		return;
	if ((result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) || image >= plane->output.image_count) {
		printf("KWL CURSOR_PLANE failed step=acquire result=%d\n", (int)result);
		kwl_cursor_plane_close(server);
		return;
	}
	plane->next = (plane->next + 1U) % (plane->output.image_count + 1U);
	version = source != NULL ? source->version : 0U;

	/* The cursor image goes into the swapchain image once; later moves present it as it is. */
	wait = acquired;
	if (!plane->held_valid[image] || plane->held[image] != source || plane->held_version[image] != version) {
		/* The image's last copy finished before it was presented, which it was to be acquired again. */
		result = vkWaitForFences(compose->device, 1U, &plane->fences[image], VK_TRUE, 100000000ULL);
		if (result == VK_SUCCESS)
			result = vkResetFences(compose->device, 1U, &plane->fences[image]);
		if (result == VK_SUCCESS)
			result = vkResetCommandBuffer(plane->commands[image], 0U);
		if (result == VK_SUCCESS) {
			plane_record(plane, plane->commands[image], plane->output.images[image], source);
			result = vkEndCommandBuffer(plane->commands[image]);
		}
		if (result == VK_SUCCESS) {
			stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			memset(&submit, 0, sizeof(submit));
			submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit.waitSemaphoreCount = 1U;
			submit.pWaitSemaphores = &acquired;
			submit.pWaitDstStageMask = &stage;
			submit.commandBufferCount = 1U;
			submit.pCommandBuffers = &plane->commands[image];
			submit.signalSemaphoreCount = 1U;
			submit.pSignalSemaphores = &plane->ready[image];
			result = vkQueueSubmit(compose->queue, 1U, &submit, plane->fences[image]);
		}
		if (result != VK_SUCCESS) {
			printf("KWL CURSOR_PLANE failed step=copy result=%d\n", (int)result);
			kwl_cursor_plane_close(server);
			return;
		}
		plane->held_valid[image] = 1U;
		plane->held[image] = source;
		plane->held_version[image] = version;
		plane->copies++;
		plane->copied = 1U;
		plane->last_copy = image;
		wait = plane->ready[image];
	}

	/* The whole image with its top-left corner at the place. */
	memset(&display, 0, sizeof(display));
	display.sType = VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR;
	display.srcRect.extent.width = plane->output.width;
	display.srcRect.extent.height = plane->output.height;
	display.dstRect.offset.x = x;
	display.dstRect.offset.y = y;
	display.dstRect.extent = display.srcRect.extent;
	memset(&present, 0, sizeof(present));
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.pNext = &display;
	present.waitSemaphoreCount = 1U;
	present.pWaitSemaphores = &wait;
	present.swapchainCount = 1U;
	present.pSwapchains = &plane->output.swapchain;
	present.pImageIndices = &image;
	result = vkQueuePresentKHR(compose->queue, &present);
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		printf("KWL CURSOR_PLANE failed step=present result=%d\n", (int)result);
		kwl_cursor_plane_close(server);
		return;
	}

	/* What the plane shows now; the log ties it to the input it shows. */
	plane->presented = 1U;
	plane->source = source;
	plane->version = version;
	plane->x = x;
	plane->y = y;
	plane->presents++;
	if (server->log_frames)
		printf("KWL CURSOR_PLANE present x=%d y=%d copy=%u input_us=%llu at_us=%llu\n", x, y, wait != acquired ? 1U : 0U, (unsigned long long)server->pointer_accepted_us, (unsigned long long)kwl_microseconds());
}

/* Records the swapchain image's clearing and the cursor image's copy into its top-left corner. */
static void
plane_record(
	struct kwl_cursor_plane *plane,
	VkCommandBuffer command,
	VkImage target,
	const struct kwl_import *source)
{
	VkCommandBufferBeginInfo begin;
	VkImageMemoryBarrier barrier;
	VkClearColorValue clear;
	VkImageCopy copy;
	VkImageBlit blit;

	/* One use of the command buffer. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	(void)vkBeginCommandBuffer(command, &begin);

	/* The swapchain image, its old contents dropped, takes the copy. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = 0U;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = target;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);

	/* Transparent where the cursor image does not reach. */
	memset(&clear, 0, sizeof(clear));
	vkCmdClearColorImage(command, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1U, &barrier.subresourceRange);

	/* The cursor image's host-written staging (B, G, R, A, premultiplied) into the corner. */
	if (source != NULL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);
		if (plane->output.format == VK_FORMAT_B8G8R8A8_UNORM) {
			memset(&copy, 0, sizeof(copy));
			copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.srcSubresource.layerCount = 1U;
			copy.dstSubresource = copy.srcSubresource;
			copy.extent.width = source->width;
			copy.extent.height = source->height;
			copy.extent.depth = 1U;
			vkCmdCopyImage(command, source->staging, VK_IMAGE_LAYOUT_GENERAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
		} else {
			/* Another channel order is converted by a one-to-one blit. */
			memset(&blit, 0, sizeof(blit));
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.layerCount = 1U;
			blit.dstSubresource = blit.srcSubresource;
			blit.srcOffsets[1].x = (int32_t)source->width;
			blit.srcOffsets[1].y = (int32_t)source->height;
			blit.srcOffsets[1].z = 1;
			blit.dstOffsets[1] = blit.srcOffsets[1];
			vkCmdBlitImage(command, source->staging, VK_IMAGE_LAYOUT_GENERAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &blit, VK_FILTER_NEAREST);
		}
	}

	/* Then to the layout it is presented in. */
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);
}
