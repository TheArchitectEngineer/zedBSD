/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the cursor plane's one present a refresh (ws213-p001 C0,
 * userland/desktop/wayland/cursor-plane.c): the first move presents at
 * once, the moves within the same refresh are held and the latest of them
 * goes when the refresh ends, a new image is never held, the event loop is
 * told when to wake, and a held move overdue by a refresh stops waking it.
 * Vulkan and the clock are fakes.
 */

#include "userland/desktop/wayland/compose.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fake clock, in microseconds. */
static uint64_t clock_now_us;

/* The presents the fake queue took, and the place of the last one. */
static unsigned presents_taken;
static int32_t present_x;
static int32_t present_y;

/* The image the cursor shows (a host-written one, as the arrow is). */
static struct kwl_import cursor_image;

/* The swapchain image the fake acquire gives next. */
static uint32_t acquire_next;

/* The fake swapchain's images. */
static VkImage plane_images[3];

/* Gives the fake clock. */
uint64_t
kwl_microseconds(
	void)
{
	/* The time the test set. */
	return clock_now_us;
}

/* Gives the fake cycle counter, one cycle a microsecond of the fake clock. */
uint64_t
kwl_cycles(
	void)
{
	/* The same fake time. */
	return clock_now_us;
}

/* Gives the cursor image, with its hotspot at its corner. */
int
kwl_compose_cursor_source(
	struct kwl_server *server,
	const struct kwl_import **image,
	int32_t *hotspot_x,
	int32_t *hotspot_y)
{
	(void)server;

	/* Always the one image. */
	*image = &cursor_image;
	*hotspot_x = 0;
	*hotspot_y = 0;
	return 1;
}

/* Gives the next swapchain image in turn. */
VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImageKHR(
	VkDevice device,
	VkSwapchainKHR swapchain,
	uint64_t timeout,
	VkSemaphore semaphore,
	VkFence fence,
	uint32_t *index)
{
	(void)device;
	(void)swapchain;
	(void)timeout;
	(void)semaphore;
	(void)fence;

	/* Round robin over the three images. */
	*index = acquire_next;
	acquire_next = (acquire_next + 1U) % 3U;
	return VK_SUCCESS;
}

/* Takes a present and its place. */
VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(
	VkQueue queue,
	const VkPresentInfoKHR *info)
{
	const VkDisplayPresentInfoKHR *display;

	(void)queue;

	/* The place rides in the display present info. */
	display = (const VkDisplayPresentInfoKHR *)info->pNext;
	present_x = display->dstRect.offset.x;
	present_y = display->dstRect.offset.y;
	presents_taken++;
	return VK_SUCCESS;
}

/* The copy's commands and synchronization, all succeeding at once. */
VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(
	VkQueue queue,
	uint32_t count,
	const VkSubmitInfo *submits,
	VkFence fence)
{
	(void)queue;
	(void)count;
	(void)submits;
	(void)fence;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceStatus(
	VkDevice device,
	VkFence fence)
{
	(void)device;
	(void)fence;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(
	VkDevice device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout)
{
	(void)device;
	(void)count;
	(void)fences;
	(void)all;
	(void)timeout;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(
	VkDevice device,
	uint32_t count,
	const VkFence *fences)
{
	(void)device;
	(void)count;
	(void)fences;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(
	VkCommandBuffer command,
	VkCommandBufferResetFlags flags)
{
	(void)command;
	(void)flags;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(
	VkCommandBuffer command,
	const VkCommandBufferBeginInfo *info)
{
	(void)command;
	(void)info;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(
	VkCommandBuffer command)
{
	(void)command;

	/* Succeeded at once. */
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(
	VkCommandBuffer command,
	VkPipelineStageFlags source,
	VkPipelineStageFlags target,
	VkDependencyFlags flags,
	uint32_t memory_count,
	const VkMemoryBarrier *memory,
	uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers,
	uint32_t image_count,
	const VkImageMemoryBarrier *images)
{
	(void)command;
	(void)source;
	(void)target;
	(void)flags;
	(void)memory_count;
	(void)memory;
	(void)buffer_count;
	(void)buffers;
	(void)image_count;
	(void)images;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(
	VkCommandBuffer command,
	VkImage image,
	VkImageLayout layout,
	const VkClearColorValue *color,
	uint32_t count,
	const VkImageSubresourceRange *ranges)
{
	(void)command;
	(void)image;
	(void)layout;
	(void)color;
	(void)count;
	(void)ranges;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage target,
	VkImageLayout target_layout,
	uint32_t count,
	const VkImageCopy *regions)
{
	(void)command;
	(void)source;
	(void)source_layout;
	(void)target;
	(void)target_layout;
	(void)count;
	(void)regions;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(
	VkCommandBuffer command,
	VkImage source,
	VkImageLayout source_layout,
	VkImage target,
	VkImageLayout target_layout,
	uint32_t count,
	const VkImageBlit *regions,
	VkFilter filter)
{
	(void)command;
	(void)source;
	(void)source_layout;
	(void)target;
	(void)target_layout;
	(void)count;
	(void)regions;
	(void)filter;
}

/* The plane's opening and closing, which the test does not reach. */
VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateCommandBuffers(
	VkDevice device,
	const VkCommandBufferAllocateInfo *info,
	VkCommandBuffer *commands)
{
	(void)device;
	(void)info;
	(void)commands;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(
	VkDevice device,
	const VkFenceCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkFence *fence)
{
	(void)device;
	(void)info;
	(void)allocator;
	(void)fence;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(
	VkDevice device,
	const VkSemaphoreCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkSemaphore *semaphore)
{
	(void)device;
	(void)info;
	(void)allocator;
	(void)semaphore;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(
	VkDevice device,
	VkFence fence,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)fence;
	(void)allocator;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(
	VkDevice device,
	VkSemaphore semaphore,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)semaphore;
	(void)allocator;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkDeviceWaitIdle(
	VkDevice device)
{
	(void)device;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR void VKAPI_CALL
vkFreeCommandBuffers(
	VkDevice device,
	VkCommandPool pool,
	uint32_t count,
	const VkCommandBuffer *commands)
{
	(void)device;
	(void)pool;
	(void)count;
	(void)commands;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDisplayPlaneSurfaceKHR(
	VkInstance instance,
	const VkDisplaySurfaceCreateInfoKHR *info,
	const VkAllocationCallbacks *allocator,
	VkSurfaceKHR *surface)
{
	(void)instance;
	(void)info;
	(void)allocator;
	(void)surface;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetDisplayModePropertiesKHR(
	VkPhysicalDevice physical,
	VkDisplayKHR display,
	uint32_t *count,
	VkDisplayModePropertiesKHR *properties)
{
	(void)physical;
	(void)display;
	(void)count;
	(void)properties;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetDisplayPlaneCapabilitiesKHR(
	VkPhysicalDevice physical,
	VkDisplayModeKHR mode,
	uint32_t plane,
	VkDisplayPlaneCapabilitiesKHR *capabilities)
{
	(void)physical;
	(void)mode;
	(void)plane;
	(void)capabilities;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetDisplayPlaneSupportedDisplaysKHR(
	VkPhysicalDevice physical,
	uint32_t plane,
	uint32_t *count,
	VkDisplayKHR *displays)
{
	(void)physical;
	(void)plane;
	(void)count;
	(void)displays;

	/* The scenes never reach it. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
	VkPhysicalDevice physical,
	uint32_t *count,
	VkDisplayPlanePropertiesKHR *properties)
{
	(void)physical;
	(void)count;
	(void)properties;

	/* The scenes never reach it. */
	abort();
}

VkResult
vkdemo_display_create_swapchain_usage(
	VkPhysicalDevice physical,
	VkDevice device,
	uint32_t family,
	struct vkdemo_display *display,
	VkImageUsageFlags extra)
{
	(void)physical;
	(void)device;
	(void)family;
	(void)display;
	(void)extra;

	/* The scenes never reach it. */
	abort();
}

void
vkdemo_display_close(
	VkInstance instance,
	VkDevice device,
	struct vkdemo_display *display)
{
	(void)instance;
	(void)device;
	(void)display;

	/* The scenes never reach it. */
	abort();
}

/* Moves the pointer at a time and runs one pass's update of the plane. */
static void
move(
	struct kwl_server *server,
	uint64_t at_ms,
	int32_t x,
	int32_t y)
{
	/* The pass at that time, with the pointer there. */
	clock_now_us = at_ms * 1000U;
	server->pointer_x = x;
	server->pointer_y = y;
	kwl_cursor_plane_update(server);
}

/* Runs the scenes. */
int
main(void)
{
	struct kwl_server *server;
	struct kwl_compose *compose;
	struct kwl_cursor_plane *plane;
	int due;

	/* A server whose output has an open plane of three images at 60 Hz. */
	server = calloc(1, sizeof(*server));
	compose = calloc(1, sizeof(*compose));
	assert(server != NULL);
	assert(compose != NULL);
	server->compose = compose;
	server->refresh = 60000U;
	server->pointer_output = KWL_PLANE_ANCHOR;
	compose->output_open = 1U;
	plane = &compose->cursor_plane;
	plane->open = 1U;
	plane->output.image_count = 3U;
	plane->output.images = plane_images;
	plane->output.width = 64U;
	plane->output.height = 64U;
	plane->output.format = VK_FORMAT_B8G8R8A8_UNORM;
	cursor_image.staging = (VkImage)(uintptr_t)1U;
	cursor_image.width = 16U;
	cursor_image.height = 16U;
	cursor_image.version = 1U;

	/* (1) The first move presents at once, with the image copied in. */
	move(server, 1000U, 10, 10);
	assert(presents_taken == 1U);
	assert(present_x == 10);
	assert(plane->copies == 1U);
	due = kwl_cursor_plane_due_ms(server);
	assert(due == -1);

	/*
	 * (2) A move 5 ms later is held; the loop wakes a little before the
	 * refresh's end (a refresh less the 2.5 ms slack of ws211-p006,
	 * 14.167 - 5 ms, rounded up).
	 */
	move(server, 1005U, 20, 20);
	assert(presents_taken == 1U);
	due = kwl_cursor_plane_due_ms(server);
	assert(due == 10);

	/* (3) Another within the refresh is held too. */
	move(server, 1010U, 30, 31);
	assert(presents_taken == 1U);
	due = kwl_cursor_plane_due_ms(server);
	assert(due == 5);

	/* (4) At the refresh's end the latest place goes (into the next swapchain image, copied there once). */
	move(server, 1017U, 30, 31);
	assert(presents_taken == 2U);
	assert(present_x == 30);
	assert(present_y == 31);
	due = kwl_cursor_plane_due_ms(server);
	assert(due == -1);

	/* (5) A still pointer presents nothing and holds nothing. */
	move(server, 1020U, 30, 31);
	assert(presents_taken == 2U);
	due = kwl_cursor_plane_due_ms(server);
	assert(due == -1);

	/* (6) A move after a still refresh goes at once; every swapchain image now holds the cursor. */
	move(server, 1040U, 40, 40);
	assert(presents_taken == 3U);
	assert(present_x == 40);
	assert(plane->copies == 3U);

	/* (7) A new image within the refresh is not held, and is copied. */
	cursor_image.version = 2U;
	move(server, 1045U, 41, 40);
	assert(presents_taken == 4U);
	assert(plane->copies == 4U);

	/* (8) A held move that no pass takes stops waking the loop after a further refresh. */
	move(server, 1050U, 50, 50);
	assert(presents_taken == 4U);
	clock_now_us = 1063U * 1000U;
	due = kwl_cursor_plane_due_ms(server);
	assert(due == 1);
	clock_now_us = 1080U * 1000U;
	due = kwl_cursor_plane_due_ms(server);
	assert(due == -1);
	move(server, 1080U, 50, 50);
	assert(presents_taken == 5U);
	assert(present_x == 50);

	/* (9) A display that told no refresh is taken as 60 Hz. */
	server->refresh = 0U;
	move(server, 1085U, 60, 60);
	assert(presents_taken == 5U);
	due = kwl_cursor_plane_due_ms(server);
	assert(due == 10);

	/* (10) The counters the KWL PERF main line reports. */
	assert(server->perf.cursor_presents == 5U);
	assert(server->perf.cursor_deferred == 4U);

	/* Succeeded: every scene held. */
	printf("cursor-due-host-test: PASS\n");
	return 0;
}
