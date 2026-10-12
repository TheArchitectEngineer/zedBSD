/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the cursor thread (ws213-p004, the design's 9 (i), and the
 * one present a refresh of ws213-p001 C0; userland/desktop/wayland/
 * cursor-thread.c, with the main thread's side in cursor-plane.c and the
 * box in cursor-box.c).  Vulkan and the clock are fakes.
 *
 * The first part runs the cursor thread's steps one at a time: the first
 * move presents at once, the moves within the same refresh are held and
 * the latest of them goes when the refresh ends, a new image is not held;
 * a copy into one swapchain image does not hold back a present from
 * another (only the acquired image's own copy is waited for, ws141-p016
 * review-B4 F1 (b)); a copy of a client's cursor stays pinned until its
 * fence is seen done, and the main thread writes only a copy that is
 * neither shown nor pinned; a failure stops the thread's Vulkan and is
 * told in the box; the counts are read from the box.
 *
 * The second part runs the real thread while a writer moves the box and
 * the main thread changes the client's cursor, writes the copies, and
 * parks and unparks the thread; built with -fsanitize=thread it is the
 * design's 9 (m) for the cursor thread.  The fake GPU checks that a copy's
 * source is not written between its submission and its fence, and every
 * fake Vulkan call checks that the box's lock is not held by its caller.
 */

#include "userland/desktop/wayland/compose.h"
#include "userland/desktop/wayland/cursor-thread.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* The fake swapchain's images, the fences' count, and the client cursor's size. */
#define FAKE_IMAGES		3U
#define FAKE_FENCES		8U
#define CLIENT_SIZE		16U

/* The fake clock in microseconds, and whether the first part uses it. */
static atomic_ullong fake_us;
static int fake_clock;

/* The checks run and failed. */
static unsigned checks;
static unsigned failures;

/* The server the main thread's side uses, and its box. */
static struct kwl_server *server;

/* The client's cursor image the source gives, and its pixels. */
static struct kwl_import client_image;
static uint32_t client_pixels[CLIENT_SIZE * CLIENT_SIZE];

/* What the fake cursor source gives: 1 the client's image, 2 the arrow. */
static int source_kind;

/* The fake swapchain's images, and the image the fake acquire gives next (-1 none free). */
static VkImage swapchain_images[FAKE_IMAGES];
static atomic_int acquire_next;

/*
 * The fake GPU: whether each fence is signalled, the copy each fence's
 * submission read and that copy's stamp then, and when it was submitted.
 */
static atomic_int fence_signalled[FAKE_FENCES];
static const struct kwl_import *fence_source[FAKE_FENCES];
static uint32_t fence_stamp[FAKE_FENCES];
static uint64_t fence_submitted_us[FAKE_FENCES];
static pthread_mutex_t gpu_lock = PTHREAD_MUTEX_INITIALIZER;

/* The source the last recorded copy read. */
static const struct kwl_import *recorded_source;

/* What the fake calls saw: presents, their place, copies, fence waits by fence, Vulkan calls, faults. */
static atomic_uint presents_taken;
static atomic_int present_x;
static atomic_int present_y;
static atomic_uint copies_submitted;
static atomic_uint waits_on[FAKE_FENCES];
static atomic_uint vulkan_calls;
static atomic_uint lock_held_calls;
static atomic_uint torn_copies;
static atomic_int present_fails;

static void check(int condition, const char *what);
static void vulkan_call(void);
static void gpu_complete(unsigned fence);
static uint32_t copy_stamp(const struct kwl_import *source);
static void stepwise(void);
static void threaded(void);
static void *mover_main(void *argument);
static void set_client(uint32_t stamp);
static struct kwl_import *new_copy(void);
static struct kwl_cursor_thread *setup(void);
static void move_to(int32_t x, int32_t y);

/* Gives the clock: the fake one in the first part. */
uint64_t
kwl_microseconds(
	void)
{
	struct timespec now;

	/* The fake time. */
	if (fake_clock)
		return atomic_load(&fake_us);

	/* The monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Gives the cycle counter: a cycle a microsecond. */
uint64_t
kwl_cycles(
	void)
{
	/* The same clock. */
	return kwl_microseconds();
}

/* Gives the cursor the test chose, with its hotspot at (2, 3). */
int
kwl_compose_cursor_source(
	struct kwl_server *source_server,
	const struct kwl_import **image,
	int32_t *hotspot_x,
	int32_t *hotspot_y)
{
	/* The arrow, or the client's image. */
	*hotspot_x = 2;
	*hotspot_y = 3;
	*image = &client_image;
	if (source_kind == 2)
		*image = source_server->arrow;
	return 1;
}

/* Makes a host image for the plane's copies: its pixels in memory, its staging a token of its own. */
VkResult
kwl_host_image_create(
	struct kwl_compose *compose,
	uint32_t width,
	uint32_t height,
	VkSampler sampler,
	struct kwl_import *import)
{
	static uint64_t generation = 100U;

	(void)compose;
	(void)sampler;

	/* The pixels the main thread writes. */
	import->map = calloc((size_t)width * height, 4U);
	if (import->map == NULL)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	import->row_pitch = (VkDeviceSize)width * 4U;
	import->width = width;
	import->height = height;
	generation++;
	import->generation = generation;
	import->staging = (VkImage)(uintptr_t)(0x5000U + generation);
	return VK_SUCCESS;
}

/* Releases a host image of the fake. */
void
kwl_host_image_release(
	struct kwl_compose *compose,
	struct kwl_import *import)
{
	(void)compose;

	/* Its pixels. */
	free(import->map);
	import->map = NULL;
}

/* Counts a fake Vulkan call, and a call made with the box's lock held by its caller (design 4.6). */
static void
vulkan_call(void)
{
	/* glibc's mutex keeps its owner's thread number. */
	atomic_fetch_add(&vulkan_calls, 1U);
	if (server != NULL && server->cursor_box.lock.__data.__owner == (int)syscall(SYS_gettid))
		atomic_fetch_add(&lock_held_calls, 1U);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImageKHR(
	VkDevice device,
	VkSwapchainKHR swapchain,
	uint64_t timeout,
	VkSemaphore semaphore,
	VkFence fence,
	uint32_t *index)
{
	int next;

	(void)device;
	(void)swapchain;
	(void)timeout;
	(void)semaphore;
	(void)fence;
	vulkan_call();

	/* The image the test gives next, or none free. */
	next = atomic_load(&acquire_next);
	if (next < 0)
		return VK_NOT_READY;
	*index = (uint32_t)next;
	atomic_store(&acquire_next, (int)(((unsigned)next + 1U) % FAKE_IMAGES));
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(
	VkQueue queue,
	const VkPresentInfoKHR *info)
{
	const VkDisplayPresentInfoKHR *display;

	(void)queue;
	vulkan_call();

	/* A failing device, or the place. */
	if (atomic_load(&present_fails))
		return VK_ERROR_DEVICE_LOST;
	display = (const VkDisplayPresentInfoKHR *)info->pNext;
	atomic_store(&present_x, display->dstRect.offset.x);
	atomic_store(&present_y, display->dstRect.offset.y);
	atomic_fetch_add(&presents_taken, 1U);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(
	VkQueue queue,
	uint32_t count,
	const VkSubmitInfo *submits,
	VkFence fence)
{
	unsigned index;

	(void)queue;
	(void)count;
	(void)submits;
	vulkan_call();

	/* The copy runs until the fence completes; its source's stamp now. */
	index = (unsigned)(uintptr_t)fence - 1U;
	pthread_mutex_lock(&gpu_lock);
	atomic_store(&fence_signalled[index], 0);
	fence_source[index] = recorded_source;
	fence_stamp[index] = copy_stamp(recorded_source);
	fence_submitted_us[index] = kwl_microseconds();
	pthread_mutex_unlock(&gpu_lock);
	atomic_fetch_add(&copies_submitted, 1U);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceStatus(
	VkDevice device,
	VkFence fence)
{
	uint64_t submitted;
	unsigned index;

	(void)device;
	vulkan_call();

	/* The threaded part's GPU finishes a copy half a millisecond after its submission. */
	index = (unsigned)(uintptr_t)fence - 1U;
	pthread_mutex_lock(&gpu_lock);
	submitted = fence_submitted_us[index];
	pthread_mutex_unlock(&gpu_lock);
	if (!fake_clock && kwl_microseconds() - submitted > 500U)
		gpu_complete(index);
	if (atomic_load(&fence_signalled[index]))
		return VK_SUCCESS;
	return VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(
	VkDevice device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout)
{
	unsigned index;

	(void)device;
	(void)count;
	(void)all;
	(void)timeout;
	vulkan_call();

	/* A wait completes the copy (the fake GPU is never stuck). */
	index = (unsigned)(uintptr_t)fences[0] - 1U;
	atomic_fetch_add(&waits_on[index], 1U);
	gpu_complete(index);
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
	vulkan_call();
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(
	VkCommandBuffer command,
	VkCommandBufferResetFlags flags)
{
	(void)command;
	(void)flags;
	vulkan_call();
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(
	VkCommandBuffer command,
	const VkCommandBufferBeginInfo *info)
{
	(void)command;
	(void)info;
	vulkan_call();
	recorded_source = NULL;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(
	VkCommandBuffer command)
{
	(void)command;
	vulkan_call();
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(
	VkCommandBuffer command,
	VkPipelineStageFlags source_stage,
	VkPipelineStageFlags destination_stage,
	VkDependencyFlags dependency,
	uint32_t memory_count,
	const VkMemoryBarrier *memory,
	uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers,
	uint32_t image_count,
	const VkImageMemoryBarrier *images)
{
	(void)command;
	(void)source_stage;
	(void)destination_stage;
	(void)dependency;
	(void)memory_count;
	(void)memory;
	(void)buffer_count;
	(void)buffers;
	(void)image_count;
	(void)images;
	vulkan_call();
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
	vulkan_call();
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
	unsigned index;

	(void)command;
	(void)source_layout;
	(void)target;
	(void)target_layout;
	(void)count;
	(void)regions;
	vulkan_call();

	/* Which image the copy reads: the arrow or one of the plane's copies, by its staging. */
	recorded_source = NULL;
	if (source == server->arrow->staging)
		recorded_source = server->arrow;
	for (index = 0; index < KWL_CURSOR_COPIES; index++) {
		if (server->compose->cursor_plane.copies[index] != NULL && source == server->compose->cursor_plane.copies[index]->staging)
			recorded_source = server->compose->cursor_plane.copies[index];
	}
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

	/* The fake swapchain is B, G, R, A: no blit. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkDeviceWaitIdle(
	VkDevice device)
{
	(void)device;
	return VK_SUCCESS;
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
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(
	VkDevice device,
	VkCommandPool pool,
	const VkAllocationCallbacks *allocator)
{
	(void)device;
	(void)pool;
	(void)allocator;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateCommandPool(
	VkDevice device,
	const VkCommandPoolCreateInfo *info,
	const VkAllocationCallbacks *allocator,
	VkCommandPool *pool)
{
	(void)device;
	(void)info;
	(void)allocator;
	(void)pool;

	/* The scenes make the objects themselves. */
	abort();
}

VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateCommandBuffers(
	VkDevice device,
	const VkCommandBufferAllocateInfo *info,
	VkCommandBuffer *commands)
{
	(void)device;
	(void)info;
	(void)commands;
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
	abort();
}

VkResult
vkdemo_display_create_swapchain_exact(
	VkPhysicalDevice physical,
	VkDevice device,
	uint32_t family,
	struct vkdemo_display *display,
	VkImageUsageFlags usage)
{
	(void)physical;
	(void)device;
	(void)family;
	(void)display;
	(void)usage;
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
}

int
main(
	void)
{
	/* The steps one at a time, then the threads. */
	stepwise();
	threaded();

	/* The verdict. */
	if (failures != 0U) {
		printf("cursor-due-host-test: FAIL (%u of %u checks)\n", failures, checks);
		return 1;
	}
	printf("cursor-due-host-test: PASS (%u checks)\n", checks);
	return 0;
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check. */
	checks++;
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Completes a copy on the fake GPU, checking its source was not written meanwhile. */
static void
gpu_complete(
	unsigned fence)
{
	/* Once. */
	pthread_mutex_lock(&gpu_lock);
	if (!atomic_load(&fence_signalled[fence])) {
		if (fence_source[fence] != NULL && copy_stamp(fence_source[fence]) != fence_stamp[fence])
			atomic_fetch_add(&torn_copies, 1U);
		atomic_store(&fence_signalled[fence], 1);
	}
	pthread_mutex_unlock(&gpu_lock);
}

/* Reads a copy's stamp: its pixels over the client image's size, folded. */
static uint32_t
copy_stamp(
	const struct kwl_import *source)
{
	const uint32_t *row;
	uint32_t sum;
	uint32_t x;
	uint32_t y;

	/* Nothing to read. */
	if (source == NULL || source->map == NULL)
		return 0U;

	/* Every pixel the client image covers. */
	sum = 0U;
	for (y = 0; y < CLIENT_SIZE && y < source->height; y++) {
		row = (const uint32_t *)((const unsigned char *)source->map + (size_t)y * source->row_pitch);
		for (x = 0; x < CLIENT_SIZE && x < source->width; x++)
			sum = sum * 31U + row[x];
	}
	return sum;
}

/* Gives the client's cursor a new picture: every pixel the stamp, a new version. */
static void
set_client(
	uint32_t stamp)
{
	unsigned index;

	/* The pixels, then the version the plane compares. */
	for (index = 0; index < CLIENT_SIZE * CLIENT_SIZE; index++)
		client_pixels[index] = stamp;
	client_image.version++;
}

/* Makes one of the plane's copies as the main thread's open does. */
static struct kwl_import *
new_copy(void)
{
	struct kwl_import *copy;

	/* A 64 by 64 host image. */
	copy = calloc(1, sizeof(*copy));
	if (copy == NULL || kwl_host_image_create(NULL, 64U, 64U, VK_NULL_HANDLE, copy) != VK_SUCCESS)
		exit(2);
	return copy;
}

/* Sets up the server, the plane and the cursor thread's objects as the plane's opening would. */
static struct kwl_cursor_thread *
setup(void)
{
	struct kwl_cursor_plane *plane;
	struct kwl_cursor_thread *cursor;
	unsigned index;

	/* The server with an open output and an open plane of three images at 60 Hz. */
	server = calloc(1, sizeof(*server));
	if (server == NULL)
		exit(2);
	server->compose = calloc(1, sizeof(*server->compose));
	if (server->compose == NULL)
		exit(2);
	server->refresh = 60000U;
	server->compose->output_open = 1U;
	server->threads_prepared = 1U;
	server->wake[0] = -1;
	server->wake[1] = -1;
	if (kwl_cursor_box_init(&server->cursor_box, 100, 100, 1920, 1080) != 0)
		exit(2);
	server->arrow = calloc(1, sizeof(*server->arrow));
	server->arrow->width = 12U;
	server->arrow->height = 19U;
	server->arrow->staging = (VkImage)(uintptr_t)0x4000U;
	server->arrow->map = calloc(12U * 19U, 4U);
	server->arrow->row_pitch = 48U;
	server->arrow->generation = 7U;
	plane = &server->compose->cursor_plane;
	plane->open = 1U;
	plane->width = 64U;
	plane->height = 64U;

	/* The client's cursor, a wl_shm image the main thread wrote. */
	client_image.width = CLIENT_SIZE;
	client_image.height = CLIENT_SIZE;
	client_image.staging = (VkImage)(uintptr_t)0x6000U;
	client_image.map = client_pixels;
	client_image.row_pitch = CLIENT_SIZE * 4U;
	client_image.generation = 9U;
	set_client(0x11111111U);

	/* The cursor thread's record and objects, the fences numbered from 1 and signalled. */
	cursor = calloc(1, sizeof(*cursor));
	if (cursor == NULL)
		exit(2);
	cursor->box = &server->cursor_box;
	cursor->wake_main = -1;
	cursor->hold_us = 16667U - 2500U;
	cursor->output.image_count = FAKE_IMAGES;
	cursor->output.images = swapchain_images;
	cursor->output.width = 64U;
	cursor->output.height = 64U;
	cursor->output.format = VK_FORMAT_B8G8R8A8_UNORM;
	cursor->pool = (VkCommandPool)(uintptr_t)0x7000U;
	for (index = 0; index < FAKE_IMAGES; index++) {
		cursor->commands[index] = (VkCommandBuffer)(uintptr_t)(0x8000U + index);
		cursor->fences[index] = (VkFence)(uintptr_t)(index + 1U);
		atomic_store(&fence_signalled[index], 1);
	}
	for (index = 0; index < KWL_CURSOR_THREAD_IMAGES; index++)
		cursor->pinned[index] = -1;
	plane->thread = cursor;
	for (index = 0; index < KWL_CURSOR_COPIES; index++) {
		plane->copies[index] = new_copy();
		cursor->copies[index] = plane->copies[index];
		server->cursor_box.copies[index] = plane->copies[index];
	}
	atomic_store(&acquire_next, 0);
	return cursor;
}

/* Moves the box as the input thread does. */
static void
move_to(
	int32_t x,
	int32_t y)
{
	struct kwl_cursor_motion motion;
	int32_t now_x;
	int32_t now_y;
	unsigned output;

	/* The difference from the box's place. */
	kwl_cursor_box_read(&server->cursor_box, &now_x, &now_y, &output);
	kwl_cursor_box_move(&server->cursor_box, x - now_x, y - now_y, kwl_microseconds(), &motion);
}

/* Runs the cursor thread's steps one at a time. */
static void
stepwise(void)
{
	struct kwl_cursor_thread *cursor;
	struct kwl_cursor_stats stats;
	enum kwl_cursor_step step;
	uint64_t deadline;
	unsigned waits;
	unsigned calls;

	/* The arrow on the plane: the main thread publishes it. */
	fake_clock = 1;
	atomic_store(&fake_us, 1000000U);
	cursor = setup();
	source_kind = 2;
	kwl_cursor_plane_update(server);
	check(server->cursor_box.image == server->arrow && server->cursor_box.show, "the arrow is published to the box as it is");

	/* (1) The first move presents at once, with the image copied in. */
	move_to(10, 10);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_PRESENTED && atomic_load(&presents_taken) == 1U, "the first move presents at once");
	check(atomic_load(&present_x) == 8 && atomic_load(&present_y) == 7, "at the place less the hotspot");
	check(atomic_load(&copies_submitted) == 1U, "the image is copied in once");

	/* (2) A move 5 ms later is held until the refresh's end (a refresh less 2.5 ms after the present). */
	atomic_fetch_add(&fake_us, 5000U);
	move_to(20, 20);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_IDLE && atomic_load(&presents_taken) == 1U, "a move within the refresh is held");
	check(deadline == 1000000U + 16667U - 2500U, "until the refresh's end");

	/* (3) At the refresh's end the latest place goes, into the next swapchain image. */
	move_to(30, 31);
	atomic_store(&fake_us, deadline);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_PRESENTED && atomic_load(&present_x) == 28 && atomic_load(&present_y) == 28, "the latest place at the refresh's end");

	/* (4) A still pointer presents nothing and waits for a change only. */
	atomic_fetch_add(&fake_us, 20000U);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_IDLE && deadline == 0U && atomic_load(&presents_taken) == 2U, "a still pointer waits for a change");

	/*
	 * (5) The copied gate is gone (review-B4 F1 (b)): a new image is copied
	 * into swapchain image 2, its copy left running; the next present
	 * acquires image 0, copies there and waits only for image 0's fence.
	 */
	source_kind = 1;
	kwl_cursor_plane_update(server);
	check(server->cursor_box.image == server->compose->cursor_plane.copies[0], "the client's cursor is published through a copy");
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_PRESENTED && atomic_load(&presents_taken) == 3U, "a new image is not held");
	check(!atomic_load(&fence_signalled[2]), "its copy into image 2 still runs");
	check(server->cursor_box.pins[0] == 1U, "the copy it reads is pinned while the copy runs");
	waits = atomic_load(&waits_on[2]);
	atomic_fetch_add(&fake_us, 20000U);
	move_to(40, 40);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_PRESENTED && atomic_load(&presents_taken) == 4U, "the next move presents from another image");
	check(atomic_load(&waits_on[2]) == waits, "without waiting for image 2's copy");

	/* (6) The main thread writes only a copy neither shown nor pinned: a new picture goes into copy 1. */
	set_client(0x22222222U);
	kwl_cursor_plane_update(server);
	check(server->cursor_box.image == server->compose->cursor_plane.copies[1], "the new picture goes into the other copy");
	check(copy_stamp(server->compose->cursor_plane.copies[0]) == fence_stamp[2], "the pinned copy was not written");

	/* (7) Another picture while copy 0 is still pinned and copy 1 is shown: the main thread waits. */
	set_client(0x33333333U);
	kwl_cursor_plane_update(server);
	check(server->cursor_box.image == server->compose->cursor_plane.copies[1], "no copy is free: the box keeps showing copy 1");

	/* (8) The copies from copy 0 (into images 2 and 0) are seen done: its pins go, and the next pass writes copy 0. */
	gpu_complete(2U);
	gpu_complete(0U);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(server->cursor_box.pins[0] == 0U, "a copy seen done lets go of its pin");
	kwl_cursor_plane_update(server);
	check(server->cursor_box.image == server->compose->cursor_plane.copies[0], "then the free copy is written");
	check(atomic_load(&torn_copies) == 0U, "no copy's source changed under it");

	/* (9) The counts the KWL PERF lines report. */
	kwl_cursor_box_take_stats(&server->cursor_box, &stats);
	check(stats.presents == atomic_load(&presents_taken) && stats.held >= 1U && stats.copies >= 3U, "the counts are read from the box");

	/* (10) A failure stops the thread's Vulkan and is told in the box. */
	atomic_store(&present_fails, 1);
	atomic_fetch_add(&fake_us, 20000U);
	move_to(50, 50);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_FAILED && server->cursor_box.failed, "a failed present is told in the box");
	calls = atomic_load(&vulkan_calls);
	move_to(60, 60);
	step = kwl_cursor_thread_step(cursor, &deadline);
	check(step == KWL_CURSOR_STEP_FAILED && atomic_load(&vulkan_calls) == calls, "no Vulkan after the failure");
	check(atomic_load(&lock_held_calls) == 0U, "no Vulkan call with the box's lock held");
	atomic_store(&present_fails, 0);
	fake_clock = 0;
}

/* The threaded part's writer: moves the box as the input thread would, 1000 times a second. */
static void *
mover_main(
	void *argument)
{
	atomic_int *stop;
	int32_t step;

	/* Back and forth until stopped. */
	stop = argument;
	step = 0;
	while (!atomic_load(stop)) {
		move_to(100 + (step % 200), 100 + (step % 150));
		step++;
		usleep(1000);
	}
	return NULL;
}

/* Runs the real cursor thread against a writer and the main thread's side. */
static void
threaded(void)
{
	struct kwl_cursor_thread *cursor;
	pthread_t mover;
	atomic_int stop;
	unsigned round;
	uint32_t stamp;
	unsigned presents_before;

	/* A fresh server and plane, the real clock, the thread started. */
	atomic_store(&presents_taken, 0U);
	cursor = setup();
	source_kind = 1;
	kwl_cursor_plane_update(server);
	check(kwl_cursor_thread_start(cursor) == 0, "the cursor thread starts");
	atomic_store(&stop, 0);
	if (pthread_create(&mover, NULL, mover_main, &stop) != 0)
		exit(2);

	/* The main thread: new pictures, passes, a park now and then, for about two seconds. */
	stamp = 1U;
	for (round = 0; round < 400U; round++) {
		if ((round % 3U) == 0U) {
			stamp++;
			set_client(stamp * 0x01010101U);
		}
		kwl_cursor_plane_update(server);
		if ((round % 97U) == 50U) {
			kwl_cursor_plane_hold(server);
			presents_before = atomic_load(&presents_taken);
			usleep(30000);
			check(atomic_load(&presents_taken) == presents_before, "a parked thread does not present");
		}
		usleep(5000);
	}

	/* The threads end. */
	atomic_store(&stop, 1);
	(void)pthread_join(mover, NULL);
	check(kwl_cursor_thread_stop(cursor) == 0, "the cursor thread stops after its copies");
	check(atomic_load(&presents_taken) > 50U, "the thread presented while the box moved");
	check(atomic_load(&torn_copies) == 0U, "no copy's source was written under the GPU (threads)");
	check(atomic_load(&lock_held_calls) == 0U, "no Vulkan call with the box's lock held (threads)");
	check(!server->cursor_box.failed, "the thread did not fail");
}
