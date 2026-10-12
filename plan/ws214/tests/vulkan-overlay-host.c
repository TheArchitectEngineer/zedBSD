/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws214-p004: libvulkan's overlay planes on the host.  The real WSI
 * swapchain, the real direct-display platform and the content table are
 * compiled into this file; the core Vulkan calls, the output inventory and
 * the display's ioctl are fakes.  The fake kernel follows the
 * GPU_DISPLAY_COMMIT contract: per-plane serials (a serial at or below the
 * plane's last is DROPPED), one record number a commit, the adopted number,
 * and GPU_DISPLAY_WAIT.  The overlay chains use the compositor's usage
 * (transfer destination only).  The scenario names (i)...(ix) are those of
 * phase004/phase.md.
 */

#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int fixture_ioctl(int fd, unsigned long request, ...);
#define ioctl fixture_ioctl
#include "userland/desktop/libvulkan/wsi-swapchain.c"
#include "userland/desktop/libvulkan/wsi-display.c"
#include "userland/desktop/libvulkan/content.c"
#undef ioctl

/* The output's mode: 64x48 at 10 Hz, so a held change expires after 200 ms. */
#define FX_WIDTH	64U
#define FX_HEIGHT	48U
#define FX_MILLIHERTZ	10000U

/* One fake fence: a submission makes it pending, and a held one blocks its waiter. */
struct fx_fence {
	int pending;
	int held;
};

/* One fake command buffer: the copies its current recording holds. */
struct fx_command {
	struct VkCommandBuffer_T base;
	unsigned copies;
};

/* The fixture's record of the core fakes. */
struct fx_state {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	struct VkDevice_T *device;
	struct VkQueue_T *queue;
	unsigned submits;
	unsigned wsi_copies;
	int hold_next;
	struct fx_fence *held;
	struct gpu_image_descriptor last_descriptor;
};

/* The fake kernel's output: three planes (primary, two overlays). */
struct fx_kernel {
	uint64_t state_serial;
	uint64_t adopted;
	/* Adoption comes only with a WAIT, not with the record. */
	int lag;
	uint64_t plane_serials[3];
	int shown[3];
	uint64_t handles[3];
	uint32_t dst_x[3];
	uint32_t dst_y[3];
	uint64_t primary_serial;
	unsigned presents;
	unsigned commits;
	unsigned commits_with_primary;
	unsigned last_count;
	int last_primary;
	unsigned waits;
	unsigned waits_unlocked;
	unsigned overlay_commits_unlocked;
	unsigned overlay_commits_locked;
	int fail_next;
	int drop_next;
	uint64_t next_handle;
	unsigned releases;
};

/* The one fixture state; the fakes and the tests share it. */
static struct fx_state fx;

/* The fake kernel. */
static struct fx_kernel fk;

/* The fake physical device, renderer context, display and mode. */
static struct VkPhysicalDevice_T fx_physical;
static struct vulkan_context fx_context;
static struct vulkan_display fx_display;
static struct vulkan_display_mode fx_mode;

static void fx_setup(void);
static struct vulkan_surface *fx_surface(uint32_t plane, uint32_t width, uint32_t height);
static VkSwapchainKHR fx_swapchain(struct vulkan_surface *surface, VkImageUsageFlags usage, uint32_t images, VkSwapchainKHR old);
static VkSemaphore fx_semaphore(void);
static void fx_app_write(VkSwapchainKHR swapchain, uint32_t image, VkSemaphore wait, VkSemaphore signal);
static VkResult fx_present(VkSwapchainKHR swapchain, uint32_t image, VkSemaphore wait, int has_rect, int32_t x, int32_t y, uint32_t width, uint32_t height);
static VkResult fx_acquire(VkSwapchainKHR swapchain, VkSemaphore semaphore, uint64_t timeout, uint32_t *image);
static void fx_settle(void);
static void fx_release_held(void);
static void fx_primary(VkSwapchainKHR primary, VkSemaphore semaphore);
static struct wsi_display_hold *fx_hold(struct vulkan_surface *surface);
static struct wsi_display_image *fx_native(VkSwapchainKHR swapchain, uint32_t image);
static int fx_wait_for(volatile unsigned *counter, unsigned value);
static int fx_wait_ready(struct vulkan_surface *surface);
static int fx_held_here(pthread_mutex_t *mutex);

/*
 * Runs the overlay scenarios in order; each builds on the planes the one
 * before left.
 */
int
main(
	void)
{
	struct vulkan_surface *primary_surface;
	struct vulkan_surface *window_surface;
	struct vulkan_surface *bar_surface;
	VkSwapchainKHR primary;
	VkSwapchainKHR window;
	VkSwapchainKHR replacement;
	VkSwapchainKHR bar;
	VkSemaphore acquired;
	VkSemaphore ready;
	VkSemaphore primary_acquired;
	uint32_t image;
	uint32_t other;
	unsigned commits;
	unsigned presents;
	unsigned submits;
	VkResult error;

	fx_setup();

	/*
	 * The planes: a 64x48 primary of colour use, a 32x32 window and a 16x8
	 * bar of transfer use only.  The window has three images, so a held
	 * change can be replaced while another image is shown.
	 */
	primary_surface = fx_surface(0U, FX_WIDTH, FX_HEIGHT);
	window_surface = fx_surface(1U, 32U, 32U);
	bar_surface = fx_surface(2U, 16U, 8U);
	primary = fx_swapchain(primary_surface, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 3U, VK_NULL_HANDLE);
	window = fx_swapchain(window_surface, VK_IMAGE_USAGE_TRANSFER_DST_BIT, 3U, VK_NULL_HANDLE);
	bar = fx_swapchain(bar_surface, VK_IMAGE_USAGE_TRANSFER_DST_BIT, 2U, VK_NULL_HANDLE);
	acquired = fx_semaphore();
	ready = fx_semaphore();
	primary_acquired = fx_semaphore();

	/* The overlay surfaces borrow the primary's claim: one kernel claim, no overlay shown. */
	fx_primary(primary, primary_acquired);
	fx_settle();
	assert(fk.presents == 1U && fk.commits == 0U);
	assert(fk.shown[1] == 0 && fk.shown[2] == 0);

	/*
	 * (i) N1: the previous frame's primary waits on the GPU in the lane
	 * when the window and the bar are shown; it does not take them, the
	 * next primary does, in one commit with both.
	 */
	fx.hold_next = 1;
	fx_primary(primary, primary_acquired);
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(window, image, acquired, ready);
	error = fx_present(window, image, ready, 1, 10, 10, 32U, 32U);
	assert(error == VK_SUCCESS);
	error = fx_acquire(bar, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(bar, image, acquired, ready);
	error = fx_present(bar, image, ready, 1, 0, 0, 16U, 8U);
	assert(error == VK_SUCCESS);
	assert(fx_wait_ready(window_surface) && fx_wait_ready(bar_surface));
	fx_primary(primary, primary_acquired);
	assert(fk.commits == 0U && fk.presents == 1U);
	fx_release_held();
	fx_settle();
	assert(fk.presents == 2U);
	assert(fk.commits == 1U && fk.commits_with_primary == 1U && fk.last_count == 3U);
	assert(fk.shown[1] == 1 && fk.shown[2] == 1 && fk.dst_x[1] == 10U);
	assert(fk.primary_serial == 3U);
	printf("(i) N1 the lane's earlier primary does not take the held changes: ok\n");

	/* (vii) a new picture goes through the overlay lane, alone, without the primary. */
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(window, image, acquired, ready);
	error = fx_present(window, image, ready, 1, 20, 10, 32U, 32U);
	assert(error == VK_SUCCESS);
	fx_settle();
	assert(fk.commits == 2U && fk.last_primary == 0 && fk.dst_x[1] == 20U);
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(window, image, acquired, ready);
	error = fx_present(window, image, ready, 1, 21, 10, 32U, 32U);
	assert(error == VK_SUCCESS);
	fx_settle();
	assert(fk.commits == 3U && fk.dst_x[1] == 21U);

	/* (vii) an unchanged image is moved at once, on the caller's thread, without GPU work or display_mutex. */
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	submits = fx.submits;
	error = fx_present(window, image, acquired, 1, 30, 12, 32U, 32U);
	assert(error == VK_SUCCESS);
	assert(fx.submits == submits && fk.commits == 4U && fk.dst_x[1] == 30U && fk.dst_y[1] == 12U);
	assert(fk.overlay_commits_locked == 0U && fk.overlay_commits_unlocked >= 3U);
	fx_settle();
	printf("(vii) fast path and overlay lane: ok\n");

	/*
	 * (ii) hiding is held; later presents of the plane replace the held
	 * change and never reach the output alone; a replaced image is free at
	 * once; the next primary shows the last one.
	 */
	commits = fk.commits;
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(window, image, acquired, 1, 0, 0, 1U, 1U);
	assert(error == VK_SUCCESS);
	assert(fx_hold(window_surface)->held != VK_FALSE && fx_hold(window_surface)->shown == VK_FALSE);
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(window, image, acquired, 1, 24, 10, 32U, 32U);
	assert(error == VK_SUCCESS);
	assert(fx_hold(window_surface)->shown != VK_FALSE && fx_hold(window_surface)->image != NULL);
	error = fx_acquire(window, acquired, 0U, &other);
	assert(error == VK_SUCCESS && other != image);
	error = fx_present(window, other, acquired, 1, 25, 10, 32U, 32U);
	assert(error == VK_SUCCESS);
	assert(fx_native(window, image)->holds == 0U);
	error = fx_acquire(window, acquired, 0U, &other);
	assert(error == VK_SUCCESS && other == image);
	error = fx_present(window, other, acquired, 1, 26, 10, 32U, 32U);
	assert(error == VK_SUCCESS);
	assert(fk.commits == commits);
	fx_primary(primary, primary_acquired);
	fx_settle();
	assert(fk.commits == commits + 1U && fk.last_primary != 0 && fk.last_count == 2U);
	assert(fk.shown[1] == 1 && fk.dst_x[1] == 26U);
	printf("(ii) held changes are replaced, their images freed at once, and shown by the next primary: ok\n");

	/* A held hide for the next scenario. */
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(window, image, acquired, 1, 0, 0, 1U, 1U);
	assert(error == VK_SUCCESS);
	assert(fx_hold(window_surface)->held != VK_FALSE && fx_hold(window_surface)->image == NULL);

	/* (iii) with no primary present, the held hide goes alone after two refreshes (why=expired). */
	commits = fk.commits;
	assert(fx_wait_for(&fk.commits, commits + 1U));
	assert(fk.last_primary == 0 && fk.shown[1] == 0);
	pthread_mutex_lock(&display_overlay_mutex);
	assert(fx_hold(window_surface)->held == VK_FALSE);
	pthread_mutex_unlock(&display_overlay_mutex);
	printf("(iii) an expired held change is committed alone: ok\n");

	/* (iv) an idle wait commits a held change alone and returns after it. */
	commits = fk.commits;
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(window, image, acquired, 1, 5, 6, 32U, 32U);
	assert(error == VK_SUCCESS);
	assert(fx_hold(window_surface)->held != VK_FALSE);
	error = vulkan_wsi_queue_idle(fx.queue);
	assert(error == VK_SUCCESS);
	assert(fk.commits == commits + 1U && fk.last_primary == 0 && fk.shown[1] == 1 && fk.dst_x[1] == 5U);
	printf("(iv) idle waits flush held changes: ok\n");

	/*
	 * (v) a retired chain's held change is dropped and never shown; its
	 * destruction hides the plane it showed.
	 */
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(window, image, acquired, 1, 0, 0, 1U, 1U);
	assert(error == VK_SUCCESS);
	assert(fx_hold(window_surface)->held != VK_FALSE);
	replacement = fx_swapchain(window_surface, VK_IMAGE_USAGE_TRANSFER_DST_BIT, 3U, window);
	assert(fx_hold(window_surface)->held == VK_FALSE);
	commits = fk.commits;
	presents = fk.presents;
	fx_primary(primary, primary_acquired);
	fx_settle();
	assert(fk.presents == presents + 1U && fk.commits == commits && fk.shown[1] == 1);
	vkDestroySwapchainKHR((VkDevice)fx.device, window, NULL);
	assert(fk.commits == commits + 1U && fk.shown[1] == 0 && fk.last_primary == 0);
	window = replacement;
	printf("(v) a retired chain's held change is dropped; destruction hides: ok\n");

	/*
	 * (vi) a held change the output cannot show (ENOSPC) drops with the
	 * primary that took it: nothing changes on the output, the primary's
	 * chain goes on and the overlay chain is out of date.
	 */
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(window, image, acquired, ready);
	error = fx_present(window, image, ready, 1, 8, 8, 32U, 32U);
	assert(error == VK_SUCCESS);
	fx_settle();
	assert(fx_hold(window_surface)->held != VK_FALSE && fx_hold(window_surface)->ready != VK_FALSE);
	commits = fk.commits;
	presents = fk.presents;
	fk.fail_next = ENOSPC;
	fx_primary(primary, primary_acquired);
	fx_settle();
	assert(fk.commits == commits && fk.presents == presents && fk.shown[1] == 0);
	assert(fx_hold(window_surface)->held == VK_FALSE);
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_ERROR_OUT_OF_DATE_KHR);
	error = fx_acquire(primary, primary_acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(primary, image, primary_acquired, 0, 0, 0, 0U, 0U);
	assert(error == VK_SUCCESS);
	fx_settle();
	assert(fk.presents == presents + 1U);
	replacement = fx_swapchain(window_surface, VK_IMAGE_USAGE_TRANSFER_DST_BIT, 3U, window);
	vkDestroySwapchainKHR((VkDevice)fx.device, window, NULL);
	window = replacement;
	printf("(vi) ENOSPC drops the held change with its primary: ok\n");

	/* (viii) a commit the output drops gives its image back at once. */
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(window, image, acquired, ready);
	error = fx_present(window, image, ready, 1, 2, 2, 32U, 32U);
	assert(error == VK_SUCCESS);
	fx_settle();
	fx_primary(primary, primary_acquired);
	fx_settle();
	assert(fk.shown[1] == 1 && fk.dst_x[1] == 2U);
	error = fx_acquire(window, acquired, 0U, &image);
	assert(error == VK_SUCCESS);
	fx_app_write(window, image, acquired, ready);
	fk.drop_next = 1;
	error = fx_present(window, image, ready, 1, 3, 3, 32U, 32U);
	assert(error == VK_SUCCESS);
	fx_settle();
	assert(fk.dst_x[1] == 2U && fx_native(window, image)->holds == 0U);
	printf("(viii) a dropped commit frees its image: ok\n");

	/*
	 * (ix) with adoption late, a replaced image is still read; once no image
	 * is free, acquiring waits for the adoption by GPU_DISPLAY_WAIT, holding
	 * no display lock.
	 */
	fk.lag = 1;
	for (;;) {
		/* Presents free images until none is left: the front one and the retiring ones are read. */
		error = fx_acquire(window, acquired, 0U, &image);
		if (error == VK_NOT_READY)
			break;
		assert(error == VK_SUCCESS);
		fx_app_write(window, image, acquired, ready);
		error = fx_present(window, image, ready, 1, 6, 6, 32U, 32U);
		assert(error == VK_SUCCESS);
		fx_settle();
	}
	presents = fk.waits;
	error = fx_acquire(window, acquired, 1000000000ULL, &other);
	assert(error == VK_SUCCESS);
	assert(fk.waits > presents && fk.waits_unlocked == fk.waits);
	error = fx_present(window, other, acquired, 1, 7, 7, 32U, 32U);
	assert(error == VK_SUCCESS);
	fx_settle();
	fk.lag = 0;
	printf("(ix) acquisition waits for the adoption without display locks: ok\n");

	/* A plane change the output cannot show on the fast path makes only its chain out of date. */
	error = fx_acquire(window, acquired, 1000000000ULL, &image);
	assert(error == VK_SUCCESS);
	fk.fail_next = ENOSPC;
	error = fx_present(window, image, acquired, 1, 9, 9, 32U, 32U);
	assert(error == VK_ERROR_OUT_OF_DATE_KHR);
	fx_primary(primary, primary_acquired);
	fx_settle();
	printf("ENOSPC on the fast path: ok\n");

	/* The chains go: the planes they show are hidden, and the claim is released last. */
	vkDestroySwapchainKHR((VkDevice)fx.device, window, NULL);
	vkDestroySwapchainKHR((VkDevice)fx.device, bar, NULL);
	assert(fk.shown[1] == 0 && fk.shown[2] == 0 && fk.releases == 0U);
	vkDestroySwapchainKHR((VkDevice)fx.device, primary, NULL);
	assert(fk.releases == 1U);
	vulkan_wsi_device_finish(fx.device);
	printf("vulkan-overlay-host: PASS\n");
	return 0;
}

/* ---- fixture helpers ---- */

/* Makes the fake device, queue, context, display and mode. */
static void
fx_setup(
	void)
{
	struct VkDevice_T *device;
	struct VkQueue_T *queue;

	pthread_mutex_init(&fx.mutex, NULL);
	pthread_cond_init(&fx.condition, NULL);

	/* A renderer that shares images and exports no fences. */
	fx_context.capabilities = GPU_CAP_SHARE;
	fx_context.fd = open("/dev/null", O_RDONLY);
	assert(fx_context.fd >= 0);
	fx_physical.memory.memoryTypeCount = 1U;
	fx_physical.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
	    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	fx_physical.queue_family_count = 1U;

	/* One display and its one mode. */
	fx_display.physical = &fx_physical;
	fx_mode.display = &fx_display;
	fx_mode.generation = 1U;
	fx_mode.parameters.visibleRegion.width = FX_WIDTH;
	fx_mode.parameters.visibleRegion.height = FX_HEIGHT;
	fx_mode.parameters.refreshRate = FX_MILLIHERTZ;

	/* One device and its queue. */
	device = calloc(1, sizeof(*device));
	assert(device != NULL);
	device->object.kind = VULKAN_OBJECT_DEVICE;
	device->object.context = &fx_context;
	device->physical = &fx_physical;
	pthread_mutex_init(&device->mutex, NULL);
	queue = calloc(1, sizeof(*queue));
	assert(queue != NULL);
	queue->object.kind = VULKAN_OBJECT_QUEUE;
	queue->object.context = &fx_context;
	queue->device = device;
	pthread_mutex_init(&queue->mutex, NULL);
	fx.device = device;
	fx.queue = queue;
}

/* Makes a display surface of the real platform on a plane (0 primary, 1 and 2 overlays). */
static struct vulkan_surface *
fx_surface(
	uint32_t plane,
	uint32_t width,
	uint32_t height)
{
	struct vulkan_surface *surface;

	surface = calloc(1, sizeof(*surface));
	assert(surface != NULL);
	surface->object.kind = VULKAN_OBJECT_SURFACE;
	surface->platform = &vulkan_wsi_display_platform;
	surface->display_mode = &fx_mode;
	surface->display_info.imageExtent.width = width;
	surface->display_info.imageExtent.height = height;
	surface->native_plane = plane;
	if (plane != 0U)
		surface->overlay = VK_TRUE;
	return surface;
}

/* Makes a swapchain on a surface. */
static VkSwapchainKHR
fx_swapchain(
	struct vulkan_surface *surface,
	VkImageUsageFlags usage,
	uint32_t images,
	VkSwapchainKHR old)
{
	VkSwapchainCreateInfoKHR create;
	VkSwapchainKHR swapchain;
	VkResult error;

	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create.surface = (VkSurfaceKHR)(uintptr_t)surface;
	create.minImageCount = images;
	create.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	create.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	create.imageExtent = surface->display_info.imageExtent;
	create.imageArrayLayers = 1U;
	create.imageUsage = usage;
	create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	create.oldSwapchain = old;
	error = vkCreateSwapchainKHR((VkDevice)fx.device, &create, NULL, &swapchain);
	assert(error == VK_SUCCESS);
	return swapchain;
}

/* Makes a binary semaphore. */
static VkSemaphore
fx_semaphore(
	void)
{
	struct vulkan_sync *sync;

	sync = calloc(1, sizeof(*sync));
	assert(sync != NULL);
	sync->object.kind = VULKAN_OBJECT_SEMAPHORE;
	return (VkSemaphore)(uintptr_t)sync;
}

/* Submits the compositor's copy into an image: it consumes the acquisition and leaves its signal on the GPU. */
static void
fx_app_write(
	VkSwapchainKHR swapchain,
	uint32_t image,
	VkSemaphore wait,
	VkSemaphore signal)
{
	struct VkCommandBuffer_T command;
	struct vulkan_swapchain *chain;
	struct vulkan_sync *sync;
	VkCommandBuffer handle;
	VkSubmitInfo submit;

	memset(&command, 0, sizeof(command));
	chain = swapchain_get(swapchain);
	vulkan_content_note_image(&command, chain->group->images[image].image);
	memset(&submit, 0, sizeof(submit));
	handle = (VkCommandBuffer)&command;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &handle;
	pthread_mutex_lock(&fx.device->mutex);
	sync = (struct vulkan_sync *)(uintptr_t)wait;
	sync->software_signaled = VK_FALSE;
	sync = (struct vulkan_sync *)(uintptr_t)signal;
	sync->software_signaled = VK_FALSE;
	vulkan_content_commit(fx.device, 1U, &submit);
	pthread_mutex_unlock(&fx.device->mutex);
}

/* Presents one image, with the same source and destination size at a point, or without rectangles. */
static VkResult
fx_present(
	VkSwapchainKHR swapchain,
	uint32_t image,
	VkSemaphore wait,
	int has_rect,
	int32_t x,
	int32_t y,
	uint32_t width,
	uint32_t height)
{
	VkDisplayPresentInfoKHR display;
	VkPresentInfoKHR present;
	VkResult error;

	memset(&display, 0, sizeof(display));
	display.sType = VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR;
	display.srcRect.extent.width = width;
	display.srcRect.extent.height = height;
	display.dstRect.offset.x = x;
	display.dstRect.offset.y = y;
	display.dstRect.extent = display.srcRect.extent;
	memset(&present, 0, sizeof(present));
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	if (has_rect)
		present.pNext = &display;
	present.waitSemaphoreCount = 1U;
	present.pWaitSemaphores = &wait;
	present.swapchainCount = 1U;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &image;
	error = vkQueuePresentKHR((VkQueue)fx.queue, &present);
	return error;
}

/* Acquires an image, waiting at most timeout. */
static VkResult
fx_acquire(
	VkSwapchainKHR swapchain,
	VkSemaphore semaphore,
	uint64_t timeout,
	uint32_t *image)
{
	VkResult error;

	error = vkAcquireNextImageKHR((VkDevice)fx.device, swapchain, timeout, semaphore, VK_NULL_HANDLE, image);
	return error;
}

/* Acquires and presents a primary image (its copy is the job's GPU work). */
static void
fx_primary(
	VkSwapchainKHR primary,
	VkSemaphore semaphore)
{
	uint32_t image;
	VkResult error;

	error = fx_acquire(primary, semaphore, 0U, &image);
	assert(error == VK_SUCCESS);
	error = fx_present(primary, image, semaphore, 0, 0, 0, 0U, 0U);
	assert(error == VK_SUCCESS);
}

/*
 * Waits until every worker is idle, without the idle wait's flush of held
 * changes (vulkan_wsi_queue_idle would commit them).
 */
static void
fx_settle(
	void)
{
	struct wsi_queue_worker *worker;
	int pending;

	pthread_mutex_lock(&swapchain_mutex);
	for (;;) {
		pending = 0;
		for (worker = workers; worker != NULL; worker = worker->next) {
			if (worker->head != NULL || worker->current != NULL)
				pending = 1;
		}
		if (!pending)
			break;
		pthread_cond_wait(&swapchain_condition, &swapchain_mutex);
	}
	pthread_mutex_unlock(&swapchain_mutex);
}

/* Completes the held fence. */
static void
fx_release_held(
	void)
{
	pthread_mutex_lock(&fx.mutex);
	assert(fx.held != NULL);
	fx.held->held = 0;
	fx.held = NULL;
	pthread_cond_broadcast(&fx.condition);
	pthread_mutex_unlock(&fx.mutex);
}

/* The held change of an overlay surface's plane (read without the lock: the tests look when it is still). */
static struct wsi_display_hold *
fx_hold(
	struct vulkan_surface *surface)
{
	struct wsi_display_lease *lease;

	lease = surface->platform_private;
	assert(lease != NULL && lease->overlay != 0U);
	return &lease->plane->overlays[lease->overlay - 1U].hold;
}

/* The native image behind a swapchain image (read without the lock: the tests look when it is still). */
static struct wsi_display_image *
fx_native(
	VkSwapchainKHR swapchain,
	uint32_t image)
{
	struct vulkan_swapchain *chain;

	chain = swapchain_get(swapchain);
	return chain->presentation[image].native_image;
}

/* Waits up to two seconds for a counter of the fake kernel to reach a value. */
static int
fx_wait_for(
	volatile unsigned *counter,
	unsigned value)
{
	struct timespec pause;
	unsigned tries;

	pause.tv_sec = 0;
	pause.tv_nsec = 1000000L;
	for (tries = 0U; tries < 2000U; tries++) {
		if (__atomic_load_n(counter, __ATOMIC_ACQUIRE) >= value)
			return 1;
		nanosleep(&pause, NULL);
	}
	return 0;
}

/* Waits up to two seconds for an overlay plane's held change to be ready. */
static int
fx_wait_ready(
	struct vulkan_surface *surface)
{
	struct wsi_display_hold *hold;
	struct timespec pause;
	unsigned tries;
	VkBool32 ready;

	hold = fx_hold(surface);
	pause.tv_sec = 0;
	pause.tv_nsec = 1000000L;
	for (tries = 0U; tries < 2000U; tries++) {
		pthread_mutex_lock(&display_overlay_mutex);
		ready = VK_FALSE;
		if (hold->held != VK_FALSE && hold->ready != VK_FALSE)
			ready = VK_TRUE;
		pthread_mutex_unlock(&display_overlay_mutex);
		if (ready != VK_FALSE)
			return 1;
		nanosleep(&pause, NULL);
	}
	return 0;
}

/* Tells whether the calling thread holds a (glibc, default-kind) mutex. */
static int
fx_held_here(
	pthread_mutex_t *mutex)
{
	pid_t owner;

	owner = __atomic_load_n(&mutex->__data.__owner, __ATOMIC_RELAXED);
	return owner == gettid();
}

/* ---- the fake kernel ---- */

/* Answers the display requests of the claim, its images and its planes. */
int
fixture_ioctl(
	int fd,
	unsigned long request,
	...)
{
	struct gpu_device_info *identity;
	struct gpu_info *info;
	struct gpu_display_claim *claim;
	struct gpu_resource_import *import;
	struct gpu_display_present *present;
	struct gpu_display_commit *commit;
	struct gpu_display_plane_state *state;
	struct gpu_display_wait *wait;
	va_list arguments;
	void *argument;
	uint32_t index;
	int primary;
	int recorded;

	(void)fd;
	va_start(arguments, request);
	argument = va_arg(arguments, void *);
	va_end(arguments);

	if (request == GPU_DEVICE_QUERY) {
		identity = argument;
		identity->device_id = 7U;
		return 0;
	}
	if (request == GPU_GET_INFO) {
		info = argument;
		info->capabilities = GPU_CAP_DISPLAY;
		return 0;
	}
	if (request == GPU_DISPLAY_CLAIM) {
		claim = argument;
		assert(claim->plane_index == 0U);
		claim->lease = 11U;
		return 0;
	}
	if (request == GPU_DISPLAY_RELEASE) {
		fk.releases++;
		fk.shown[1] = 0;
		fk.shown[2] = 0;
		return 0;
	}
	if (request == GPU_RESOURCE_IMPORT) {
		import = argument;
		fk.next_handle++;
		import->handle = fk.next_handle;
		import->image = fx.last_descriptor;
		return 0;
	}
	if (request == GPU_RESOURCE_DESTROY)
		return 0;
	if (request == GPU_DISPLAY_PRESENT) {
		/* A synchronized primary present: recorded and adopted before it returns. */
		present = argument;
		assert(present->lease == 11U);
		fk.state_serial++;
		fk.adopted = fk.state_serial;
		present->sequence = fk.state_serial;
		__atomic_add_fetch(&fk.presents, 1U, __ATOMIC_RELEASE);
		return 0;
	}
	if (request == GPU_DISPLAY_COMMIT) {
		commit = argument;
		assert(commit->lease == 11U && commit->generation == 1U);
		assert(commit->count >= 1U && commit->count <= 1U + GPU_DISPLAY_OVERLAY_MAX);
		assert(commit->signal_fd == -1 && (commit->flags & GPU_DISPLAY_COMMIT_WAIT) == 0U);
		for (index = commit->count; index < 1U + GPU_DISPLAY_OVERLAY_MAX; index++)
			assert(commit->planes[index].wait_fd == -1 && commit->planes[index].flags == 0U);

		/* An overlay commit never holds display_mutex, nor does any commit hold display_cursor_mutex. */
		primary = 0;
		for (index = 0U; index < commit->count; index++) {
			if (commit->planes[index].plane == 0U)
				primary = 1;
		}
		assert(!fx_held_here(&display_cursor_mutex));
		assert(fx_held_here(&display_overlay_mutex));
		if (!primary && !fx_held_here(&display_mutex))
			fk.overlay_commits_unlocked++;
		else if (!primary)
			fk.overlay_commits_locked++;

		/* A refusal the test asked for changes nothing. */
		if (fk.fail_next != 0) {
			errno = fk.fail_next;
			fk.fail_next = 0;
			return -1;
		}

		/* Each plane is recorded unless a later serial was, or the test drops it. */
		recorded = 0;
		for (index = 0U; index < commit->count; index++) {
			state = &commit->planes[index];
			assert(state->plane <= 2U && state->serial != 0U && state->result == 0U);
			if (state->serial <= fk.plane_serials[state->plane] || fk.drop_next) {
				state->result = GPU_DISPLAY_PLANE_DROPPED;
				continue;
			}
			state->result = GPU_DISPLAY_PLANE_APPLIED;
			fk.plane_serials[state->plane] = state->serial;
			if (state->plane == 0U)
				fk.primary_serial = state->serial;
			fk.shown[state->plane] = (state->flags & GPU_DISPLAY_PLANE_SHOWN) != 0U;
			if (fk.shown[state->plane]) {
				assert((state->flags & GPU_DISPLAY_PLANE_IMAGE) != 0U && state->handle != 0U);
				fk.handles[state->plane] = state->handle;
				fk.dst_x[state->plane] = state->dst_x;
				fk.dst_y[state->plane] = state->dst_y;
			} else {
				assert(state->flags == 0U && state->handle == 0U);
			}
			recorded = 1;
		}
		fk.drop_next = 0;

		/* One record number a commit; adopted at once unless adoption lags. */
		if (recorded)
			fk.state_serial++;
		if (!fk.lag)
			fk.adopted = fk.state_serial;
		commit->sequence = fk.state_serial;
		commit->adopted = fk.adopted;
		fk.last_count = commit->count;
		fk.last_primary = primary;
		if (primary)
			fk.commits_with_primary++;
		__atomic_add_fetch(&fk.commits, 1U, __ATOMIC_RELEASE);
		return 0;
	}
	if (request == GPU_DISPLAY_WAIT) {
		/* The waiter holds no display lock; a zero timeout answers at once. */
		wait = argument;
		assert(wait->lease == 11U);
		fk.waits++;
		if (!fx_held_here(&display_overlay_mutex) && !fx_held_here(&display_mutex))
			fk.waits_unlocked++;
		if (wait->sequence > fk.adopted && wait->timeout_ns == 0U) {
			errno = EAGAIN;
			return -1;
		}
		if (wait->sequence > fk.adopted)
			fk.adopted = wait->sequence;
		wait->completed_sequence = fk.adopted;
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

/* ---- the output inventory ---- */

VkResult
vulkan_wsi_display_refresh(
	struct vulkan_display *display)
{
	(void)display;
	return VK_SUCCESS;
}

VkResult
vulkan_wsi_display_snapshot(
	struct vulkan_display *display,
	struct vulkan_wsi_output *output)
{
	(void)display;
	memset(output, 0, sizeof(*output));
	output->identifier = 1U;
	output->device_identifier = 7U;
	strcpy(output->device_path, "/dev/null");
	output->generation = 1U;
	output->flags = VULKAN_WSI_OUTPUT_CONNECTED | VULKAN_WSI_OUTPUT_FIFO | VULKAN_WSI_OUTPUT_BLOB | VULKAN_WSI_OUTPUT_OVERLAYS;
	output->formats = VULKAN_WSI_FORMAT_BGRA;
	output->plane_count = 3U;
	output->overlay_count = 2U;
	output->refresh_millihz = FX_MILLIHERTZ;
	return VK_SUCCESS;
}

int
vulkan_wsi_display_node_ioctl(
	struct VkPhysicalDevice_T *physical,
	const struct vulkan_wsi_output *output,
	unsigned long command,
	void *argument)
{
	struct gpu_scanout_constraints *constraints;

	(void)physical;
	(void)output;
	if (command != GPU_DISPLAY_CONSTRAINTS) {
		errno = ENOTTY;
		return -1;
	}
	constraints = argument;
	constraints->flags = GPU_SCANOUT_SHARED;
	constraints->formats = GPU_DISPLAY_FORMAT_BGRA8888;
	constraints->stride_alignment = 1U;
	constraints->offset_alignment = 1U;
	return 0;
}

VkResult vulkan_wsi_display_node_query(struct VkPhysicalDevice_T *physical, uint32_t index, uint32_t *count, struct gpu_display_info *request, uint64_t *device_id, char *path) { (void)physical; (void)index; (void)count; (void)request; (void)device_id; (void)path; return VK_ERROR_INITIALIZATION_FAILED; }

/* ---- the core Vulkan calls the WSI makes ---- */

void *
vulkan_allocate(
	const struct vulkan_allocator *allocator,
	size_t bytes,
	size_t alignment,
	VkSystemAllocationScope scope)
{
	(void)allocator;
	(void)alignment;
	(void)scope;
	return calloc(1, bytes == 0U ? 1U : bytes);
}

void vulkan_free(const struct vulkan_allocator *allocator, void *allocation) { (void)allocator; free(allocation); }

VkResult
vulkan_object_alloc(
	size_t bytes,
	size_t alignment,
	enum vulkan_object_kind kind,
	struct vulkan_object *parent,
	struct vulkan_context *context,
	const VkAllocationCallbacks *allocator,
	VkSystemAllocationScope scope,
	struct vulkan_object **result)
{
	struct vulkan_object *object;

	(void)alignment;
	(void)allocator;
	object = calloc(1, bytes);
	assert(object != NULL);
	object->kind = kind;
	object->parent = parent;
	object->context = context;
	object->scope = scope;
	*result = object;
	return VK_SUCCESS;
}

void vulkan_object_free(struct vulkan_object *object) { free(object); }
VkResult vulkan_object_publish(struct vulkan_object *object) { object->published = VK_TRUE; return VK_SUCCESS; }
void vulkan_object_unpublish(struct vulkan_object *object) { object->published = VK_FALSE; }
struct vulkan_object *vulkan_nondispatchable_object(uint64_t handle) { return (struct vulkan_object *)(uintptr_t)handle; }
uint64_t vulkan_nondispatchable_handle(struct vulkan_object *object) { return (uint64_t)(uintptr_t)object; }
struct VkDevice_T *vulkan_device(VkDevice device) { return (struct VkDevice_T *)device; }
struct VkQueue_T *vulkan_queue(VkQueue queue) { return (struct VkQueue_T *)queue; }
struct vulkan_image *vulkan_image(VkImage image) { return (struct vulkan_image *)(uintptr_t)image; }
struct vulkan_sync *vulkan_sync_object(uint64_t handle) { return (struct vulkan_sync *)(uintptr_t)handle; }
struct vulkan_surface *vulkan_wsi_surface(VkSurfaceKHR surface) { return (struct vulkan_surface *)(uintptr_t)surface; }
VkResult vulkan_wsi_surface_retain(struct vulkan_surface *surface) { surface->swapchain_references++; return VK_SUCCESS; }
void vulkan_wsi_surface_release(struct vulkan_surface *surface) { surface->swapchain_references--; }
void vulkan_device_error(struct VkDevice_T *device, VkResult error) { __atomic_store_n(&device->error, error, __ATOMIC_RELEASE); }
void vulkan_context_error(struct vulkan_context *context, VkResult error) { __atomic_store_n(&context->error, error, __ATOMIC_RELEASE); }
void vulkan_context_lock(struct vulkan_context *context) { (void)context; }
void vulkan_context_unlock(struct vulkan_context *context) { (void)context; }

VkResult
vulkan_sync_device_status_locked(
	struct VkDevice_T *device)
{
	return __atomic_load_n(&device->error, __ATOMIC_ACQUIRE);
}

VkResult
vulkan_wsi_acquire_signal(
	struct VkDevice_T *device,
	VkSemaphore semaphore,
	VkFence fence)
{
	struct vulkan_sync *sync;

	(void)fence;
	pthread_mutex_lock(&device->mutex);
	sync = vulkan_sync_object((uint64_t)(uintptr_t)semaphore);
	if (sync != NULL)
		sync->software_signaled = VK_TRUE;
	pthread_mutex_unlock(&device->mutex);
	return VK_SUCCESS;
}

/* A WSI submission: consumes its waits, counts writes, and makes its fence pending (or held). */
VkResult
vulkan_queue_submit_locked(
	struct VkQueue_T *queue,
	uint32_t count,
	const VkSubmitInfo *submits,
	VkFence fence)
{
	struct fx_fence *pending;
	struct vulkan_sync *sync;
	uint32_t submit;
	uint32_t index;

	pthread_mutex_lock(&queue->device->mutex);
	for (submit = 0U; submit < count; submit++) {
		for (index = 0U; index < submits[submit].waitSemaphoreCount; index++) {
			sync = vulkan_sync_object((uint64_t)(uintptr_t)submits[submit].pWaitSemaphores[index]);
			sync->software_signaled = VK_FALSE;
		}
	}
	vulkan_content_commit(queue->device, count, submits);
	pthread_mutex_unlock(&queue->device->mutex);

	pthread_mutex_lock(&fx.mutex);
	fx.submits++;
	for (submit = 0U; submit < count; submit++) {
		for (index = 0U; index < submits[submit].commandBufferCount; index++)
			fx.wsi_copies += ((struct fx_command *)submits[submit].pCommandBuffers[index])->copies;
	}
	pending = (struct fx_fence *)(uintptr_t)fence;
	pending->pending = 1;
	if (fx.hold_next == 1) {
		pending->held = 1;
		fx.held = pending;
		fx.hold_next = 0;
	}
	pthread_mutex_unlock(&fx.mutex);
	return VK_SUCCESS;
}

VkResult
vulkan_fences_wait(
	struct VkDevice_T *device,
	uint32_t count,
	const VkFence *fences,
	VkBool32 all,
	uint64_t timeout_ns)
{
	struct fx_fence *fence;

	(void)device;
	(void)count;
	(void)all;
	(void)timeout_ns;
	fence = (struct fx_fence *)(uintptr_t)fences[0];
	pthread_mutex_lock(&fx.mutex);
	while (fence->held)
		pthread_cond_wait(&fx.condition, &fx.mutex);
	fence->pending = 0;
	pthread_mutex_unlock(&fx.mutex);
	return VK_SUCCESS;
}

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
	struct vulkan_image *created;

	(void)device;
	(void)format;
	(void)allocator;
	(void)placement;
	created = calloc(1, sizeof(*created));
	assert(created != NULL);
	*image = (VkImage)(uintptr_t)created;
	*memory = (VkDeviceMemory)(uintptr_t)malloc(1);
	*fd = open("/dev/null", O_RDONLY);
	memset(descriptor, 0, sizeof(*descriptor));
	descriptor->width = extent.width;
	descriptor->height = extent.height;
	descriptor->stride = extent.width * 4U;
	descriptor->format = GPU_PIXEL_BGRA8888;
	fx.last_descriptor = *descriptor;
	return VK_SUCCESS;
}

VkResult vulkan_wake_create(struct vulkan_wake *wake, struct VkDevice_T *device) { (void)wake; (void)device; return VK_ERROR_INITIALIZATION_FAILED; }
void vulkan_wake_destroy(struct vulkan_wake *wake) { (void)wake; }
void vulkan_wake_drain(struct vulkan_wake *wake) { (void)wake; }
void vulkan_wake_notify(struct VkDevice_T *device, struct vulkan_context *context) { (void)device; (void)context; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo *info, const VkAllocationCallbacks *allocator, VkImage *image)
{
	struct vulkan_image *created;

	(void)device;
	(void)allocator;
	created = calloc(1, sizeof(*created));
	assert(created != NULL);
	created->usage = info->usage;
	*image = (VkImage)(uintptr_t)created;
	return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) { (void)device; (void)allocator; free((void *)(uintptr_t)image); }
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *requirements) { (void)device; (void)image; requirements->size = 4096U; requirements->alignment = 4096U; requirements->memoryTypeBits = 1U; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *allocator, VkDeviceMemory *memory) { (void)device; (void)info; (void)allocator; *memory = (VkDeviceMemory)(uintptr_t)malloc(1); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *allocator) { (void)device; (void)allocator; free((void *)(uintptr_t)memory); }
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) { (void)device; (void)image; (void)memory; (void)offset; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) { (void)device; (void)buffer; (void)memory; (void)offset; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *info, const VkAllocationCallbacks *allocator, VkBuffer *buffer) { (void)device; (void)info; (void)allocator; (void)buffer; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *allocator) { (void)device; (void)buffer; (void)allocator; }
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *requirements) { (void)device; (void)buffer; (void)requirements; }
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void **data) { (void)device; (void)memory; (void)offset; (void)size; (void)flags; (void)data; return VK_ERROR_MEMORY_MAP_FAILED; }
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory) { (void)device; (void)memory; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physical, VkFormat format, VkFormatProperties *properties) { (void)physical; (void)format; memset(properties, 0xff, sizeof(*properties)); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physical, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *properties) { (void)physical; (void)format; (void)type; (void)tiling; (void)usage; (void)flags; memset(properties, 0, sizeof(*properties)); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info, const VkAllocationCallbacks *allocator, VkCommandPool *pool) { (void)device; (void)info; (void)allocator; *pool = (VkCommandPool)(uintptr_t)malloc(1); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *allocator) { (void)device; (void)allocator; free((void *)(uintptr_t)pool); }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *buffers) { (void)device; (void)info; buffers[0] = (VkCommandBuffer)calloc(1, sizeof(struct fx_command)); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer buffer, const VkCommandBufferBeginInfo *info) { (void)info; vulkan_content_clear((struct VkCommandBuffer_T *)buffer); ((struct fx_command *)buffer)->copies = 0U; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer buffer, VkCommandBufferResetFlags flags) { (void)flags; vulkan_content_clear((struct VkCommandBuffer_T *)buffer); ((struct fx_command *)buffer)->copies = 0U; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer buffer) { (void)buffer; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer buffer, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count, const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images) { (void)buffer; (void)source; (void)destination; (void)flags; (void)memory_count; (void)memory; (void)buffer_count; (void)buffers; (void)image_count; (void)images; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer buffer, VkImage source, VkImageLayout source_layout, VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageCopy *regions) { (void)source; (void)source_layout; (void)destination_layout; (void)count; (void)regions; vulkan_content_note_image((struct VkCommandBuffer_T *)buffer, destination); ((struct fx_command *)buffer)->copies++; }
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer buffer, VkImage source, VkImageLayout source_layout, VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageBlit *regions, VkFilter filter) { (void)buffer; (void)source; (void)source_layout; (void)destination; (void)destination_layout; (void)count; (void)regions; (void)filter; assert(0); }
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer buffer, VkImage image, VkImageLayout layout, const VkClearColorValue *color, uint32_t count, const VkImageSubresourceRange *ranges) { (void)buffer; (void)image; (void)layout; (void)color; (void)count; (void)ranges; assert(0); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer buffer, VkImage image, VkImageLayout layout, VkBuffer destination, uint32_t count, const VkBufferImageCopy *regions) { (void)buffer; (void)image; (void)layout; (void)destination; (void)count; (void)regions; assert(0); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info, const VkAllocationCallbacks *allocator, VkFence *fence) { (void)device; (void)info; (void)allocator; *fence = (VkFence)(uintptr_t)calloc(1, sizeof(struct fx_fence)); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t count, const VkFence *fences) { (void)device; (void)count; assert(((struct fx_fence *)(uintptr_t)fences[0])->pending == 0); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) { (void)device; (void)allocator; free((void *)(uintptr_t)fence); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR *info, int *fd) { (void)device; (void)info; (void)fd; return VK_ERROR_FEATURE_NOT_PRESENT; }
