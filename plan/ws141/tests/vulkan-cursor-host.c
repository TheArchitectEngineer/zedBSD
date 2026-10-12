/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws141-p016 (B) B-u: the cursor plane's presentation in libvulkan on the
 * host.  The real WSI swapchain, display and content sources are compiled
 * into this file; the core Vulkan calls they make, the native platform of
 * the WSI part and the display's ioctl are fakes that record what they are
 * asked.  The scenario names (u1...u15) are those of phase.md "(B) 第 6 版".
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

/* The images of every test swapchain. */
#define FX_IMAGES 3U

/* One fake fence: a submission makes it pending, and a held one blocks its waiter. */
struct fx_fence {
	int pending;
	int held;
};

/* One fake lease of the fake platform. */
struct fx_lease {
	struct vulkan_surface *surface;
};

/* The fixture's record of what the fakes were asked, and what they answer. */
struct fx_state {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	pthread_t main_thread;
	struct VkDevice_T *device;
	struct VkQueue_T *queue;
	/* Submissions, the WSI's own copies, fence waits. */
	unsigned submits;
	unsigned wsi_copies;
	unsigned fence_waits;
	/* The next WSI submission's fence is held until released. */
	int hold_next;
	struct fx_fence *held;
	/* Cursor presents: calls, on the caller's thread, shown, dropped. */
	unsigned cursor_calls;
	unsigned cursor_on_caller;
	unsigned cursor_shown;
	unsigned cursor_shown_on_worker;
	unsigned cursor_dropped;
	uint64_t cursor_applied;
	uint64_t cursor_last_ticket;
	VkBool32 cursor_last_position;
	int32_t cursor_x;
	int32_t cursor_y;
	/* Busy answers still to give, and the locks each cursor present must find free. */
	unsigned busy_left;
	int check_queue_lock;
	unsigned lock_violations;
	/* Acquisitions that asked whether an image is still held. */
	unsigned available_calls;
	/* Primary presents. */
	unsigned primary_presents;
	/* The queue lock's holder: 1 once it holds the lock, 2 when told to let go. */
	int queue_hold;
};

/* The display ioctl's record and answers (the display op tests). */
struct fx_display {
	unsigned sets;
	unsigned moves;
	int set_error;
	int move_error;
	int32_t x;
	int32_t y;
	unsigned mutex_free;
	unsigned mutex_busy;
};

/* The one fixture state; the fakes and the tests share it. */
static struct fx_state fx;

/* The display ioctl's state. */
static struct fx_display fx_ioctl_state;

/* The fake memory types of the fake physical device. */
static struct VkPhysicalDevice_T fx_physical;

/* The fake renderer context: sharing, no exported fences. */
static struct vulkan_context fx_context;

static VkResult fx_capabilities(struct vulkan_surface *surface, struct VkPhysicalDevice_T *physical, VkSurfaceCapabilitiesKHR *capabilities);
static VkResult fx_formats(struct vulkan_surface *surface, struct VkPhysicalDevice_T *physical, uint32_t *count, VkSurfaceFormatKHR *formats);
static VkResult fx_modes(struct vulkan_surface *surface, struct VkPhysicalDevice_T *physical, uint32_t *count, VkPresentModeKHR *modes);
static VkResult fx_claim(struct vulkan_surface *surface, struct VkDevice_T *device, void **result);
static VkResult fx_release(void *lease);
static VkResult fx_import(void *lease, int fd, const struct gpu_image_descriptor *descriptor, void **result);
static VkResult fx_present_image(void *lease, void *image, VkPresentModeKHR mode, uint64_t *sequence);
static VkBool32 fx_available(void *image);
static void fx_destroy_image(void *image);
static VkResult fx_present_cursor(void *lease, void *image, VkBool32 has_position, int32_t x, int32_t y, uint64_t ticket, uint64_t *frame, uint64_t *sequence, enum vulkan_wsi_cursor_outcome *outcome, struct vulkan_wsi_cursor_times *times);

/* The fake native platform behind the WSI tests. */
static const struct vulkan_wsi_platform_ops fx_platform = {
	fx_capabilities, fx_formats, fx_modes,
	fx_claim, fx_release, NULL, NULL, NULL,
	fx_import, fx_present_image, NULL, fx_available, fx_destroy_image, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, fx_present_cursor
};

static void fx_setup(void);
static struct vulkan_surface *fx_surface(VkBool32 cursor);
static VkSwapchainKHR fx_swapchain(struct vulkan_surface *surface, VkImageUsageFlags usage, VkSwapchainKHR old);
static VkSemaphore fx_semaphore(void);
static void fx_app_write(VkSwapchainKHR swapchain, uint32_t image, VkSemaphore wait, VkSemaphore signal);
static VkResult fx_present(VkSwapchainKHR swapchain, uint32_t image, VkSemaphore wait, VkBool32 position, int32_t x, int32_t y);
static uint32_t fx_acquire(VkSwapchainKHR swapchain, VkSemaphore semaphore);
static void fx_drain(void);
static void fx_release_held(void);
static void fx_wait_shown(unsigned shown);
static void *fx_hold_queue(void *argument);
static void test_content(void);
static void test_display_op(void);
static void *test_display_holder(void *argument);
static void test_wsi(void);

/*
 * Runs the content, display op and WSI scenarios.
 */
int
main(
	void)
{
	/* The fakes' shared state. */
	fx_setup();

	/* u7: the swapchain-image write table and its counts. */
	test_content();

	/* u5, u8, u15 at the display: the cursor present's ticket, errors and lock. */
	test_display_op();

	/* u1-u6, u8-u13 at the swapchain. */
	test_wsi();

	/* Succeeded: every scenario held. */
	printf("vulkan-cursor-host: PASS\n");
	return 0;
}

/* ---- the scenarios ---- */

/* u7: the table names swapchain images once, merges secondaries, overflows to the epoch, and Begin empties it. */
static void
test_content(
	void)
{
	struct VkDevice_T device;
	struct VkCommandBuffer_T primary;
	struct VkCommandBuffer_T secondary;
	struct vulkan_image images[VULKAN_CONTENT_IMAGES + 1U];
	struct vulkan_image plain;
	VkCommandBuffer handle;
	VkSubmitInfo submit;
	uint32_t index;

	/* Swapchain images and one ordinary image. */
	memset(&device, 0, sizeof(device));
	memset(images, 0, sizeof(images));
	memset(&plain, 0, sizeof(plain));
	for (index = 0U; index <= VULKAN_CONTENT_IMAGES; index++)
		images[index].swapchain_owned = VK_TRUE;
	memset(&primary, 0, sizeof(primary));
	memset(&secondary, 0, sizeof(secondary));

	/* A repeated and an ordinary image add one entry. */
	vulkan_content_note_image(&primary, (VkImage)(uintptr_t)&images[0]);
	vulkan_content_note_image(&primary, (VkImage)(uintptr_t)&images[0]);
	vulkan_content_note_image(&primary, (VkImage)(uintptr_t)&plain);
	vulkan_content_note_image(&primary, VK_NULL_HANDLE);
	assert(primary.content_count == 1U);
	assert(primary.content_unknown == VK_FALSE);

	/* A secondary's image joins the primary. */
	vulkan_content_note_image(&secondary, (VkImage)(uintptr_t)&images[1]);
	handle = (VkCommandBuffer)&secondary;
	vulkan_content_note_secondaries(&primary, 1U, &handle);
	assert(primary.content_count == 2U);

	/* An accepted submission counts each named image once and leaves the epoch. */
	memset(&submit, 0, sizeof(submit));
	submit.commandBufferCount = 1U;
	handle = (VkCommandBuffer)&primary;
	submit.pCommandBuffers = &handle;
	vulkan_content_commit(&device, 1U, &submit);
	assert(images[0].content_serial == 1U);
	assert(images[1].content_serial == 1U);
	assert(images[2].content_serial == 0U);
	assert(device.content_epoch == 0U);

	/* More images than the table holds make it unknown, which moves the epoch. */
	for (index = 0U; index <= VULKAN_CONTENT_IMAGES; index++)
		vulkan_content_note_image(&primary, (VkImage)(uintptr_t)&images[index]);
	assert(primary.content_unknown != VK_FALSE);
	vulkan_content_commit(&device, 1U, &submit);
	assert(device.content_epoch == 1U);

	/* An unknown secondary makes its primary unknown. */
	vulkan_content_clear(&primary);
	secondary.content_unknown = VK_TRUE;
	handle = (VkCommandBuffer)&secondary;
	vulkan_content_note_secondaries(&primary, 1U, &handle);
	assert(primary.content_unknown != VK_FALSE);

	/* Begin and reset empty the table. */
	vulkan_content_clear(&primary);
	assert(primary.content_count == 0U);
	assert(primary.content_unknown == VK_FALSE);
	printf("u7 content table: ok\n");
}

/* u5, u8, u15: the real display op with a lease made by hand and the ioctl faked. */
static void
test_display_op(
	void)
{
	struct wsi_display_connection connection;
	struct wsi_display_plane plane;
	struct wsi_display_lease lease;
	struct wsi_display_image image;
	enum vulkan_wsi_cursor_outcome outcome;
	struct vulkan_wsi_cursor_times times;
	pthread_t holder;
	uint64_t frame;
	uint64_t sequence;
	VkResult error;
	int status;

	/* A cursor lease on a plane whose primary presents have shown seven frames. */
	memset(&connection, 0, sizeof(connection));
	connection.fd = 99;
	memset(&plane, 0, sizeof(plane));
	plane.connection = &connection;
	plane.frame = 7U;
	memset(&lease, 0, sizeof(lease));
	lease.plane = &plane;
	lease.cursor = VK_TRUE;
	lease.identifier = 5U;
	lease.generation = 1U;
	memset(&image, 0, sizeof(image));
	image.connection = &connection;
	image.imported.image.width = 64U;
	image.imported.image.height = 64U;
	image.imported.image.stride = 256U;
	frame = 0U;
	sequence = 0U;

	/* Ticket 2 is shown where it says, with the times taken. */
	memset(&times, 0, sizeof(times));
	error = display_present_cursor(&lease, &image, VK_TRUE, 10, 20, 2U, &frame, &sequence, &outcome, &times);
	assert(error == VK_SUCCESS);
	assert(outcome == VULKAN_WSI_CURSOR_SHOWN);
	assert(frame == 1U && sequence == 7U);
	assert(fx_ioctl_state.x == 10 && fx_ioctl_state.y == 20);
	assert(times.locked != 0U && times.set >= times.locked && times.done >= times.set);

	/* u4: an older ticket, and the same one, are dropped without an ioctl or a frame. */
	error = display_present_cursor(&lease, &image, VK_TRUE, 30, 40, 1U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_SUCCESS && outcome == VULKAN_WSI_CURSOR_DROPPED);
	error = display_present_cursor(&lease, &image, VK_TRUE, 30, 40, 2U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_SUCCESS && outcome == VULKAN_WSI_CURSOR_DROPPED);
	assert(fx_ioctl_state.sets == 1U && frame == 1U);

	/* u13: a present without a position keeps where the cursor was. */
	error = display_present_cursor(&lease, &image, VK_FALSE, 0, 0, 3U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_SUCCESS && outcome == VULKAN_WSI_CURSOR_SHOWN);
	assert(fx_ioctl_state.x == 10 && fx_ioctl_state.y == 20);

	/* u5: EBUSY changes nothing and is not an error. */
	fx_ioctl_state.set_error = EBUSY;
	error = display_present_cursor(&lease, &image, VK_TRUE, 50, 60, 4U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_SUCCESS && outcome == VULKAN_WSI_CURSOR_BUSY);
	assert(lease.applied_ticket == 3U && frame == 2U);

	/* u5: another SET failure is an error and changes nothing. */
	fx_ioctl_state.set_error = ENXIO;
	error = display_present_cursor(&lease, &image, VK_TRUE, 50, 60, 4U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_ERROR_SURFACE_LOST_KHR);
	assert(lease.applied_ticket == 3U && lease.x == 10 && frame == 2U);

	/* u5: a failed MOVE after a SET keeps the new image's ticket but not its position. */
	fx_ioctl_state.set_error = 0;
	fx_ioctl_state.move_error = ENXIO;
	error = display_present_cursor(&lease, &image, VK_TRUE, 50, 60, 4U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_ERROR_SURFACE_LOST_KHR);
	assert(lease.applied_ticket == 4U && lease.x == 10 && frame == 2U);
	fx_ioctl_state.move_error = 0;

	/* u8: no present of the cursor holds display_mutex during its ioctls. */
	assert(fx_ioctl_state.mutex_busy == 0U && fx_ioctl_state.mutex_free != 0U);

	/* u15: a primary present holding display_mutex does not stop the cursor. */
	pthread_mutex_lock(&display_mutex);
	status = pthread_create(&holder, NULL, test_display_holder, &lease);
	assert(status == 0);
	status = pthread_join(holder, NULL);
	assert(status == 0);
	pthread_mutex_unlock(&display_mutex);
	assert(lease.applied_ticket == 5U && lease.x == 70);
	printf("u4 u5 u8 u13 u15 display op: ok\n");
}

/* Presents ticket 5 from another thread while the test holds display_mutex. */
static void *
test_display_holder(
	void *argument)
{
	struct wsi_display_lease *lease;
	struct wsi_display_image image;
	enum vulkan_wsi_cursor_outcome outcome;
	uint64_t frame;
	uint64_t sequence;
	VkResult error;

	/* The lease's own image. */
	lease = argument;
	memset(&image, 0, sizeof(image));
	image.connection = lease->plane->connection;
	frame = 0U;

	/* The present completes although display_mutex is held. */
	error = display_present_cursor(lease, &image, VK_TRUE, 70, 80, 5U, &frame, &sequence, &outcome, NULL);
	assert(error == VK_SUCCESS && outcome == VULKAN_WSI_CURSOR_SHOWN);
	return NULL;
}

/* u1-u6, u8-u13: the swapchain's lanes, routes, tickets and log with a fake platform and core. */
static void
test_wsi(
	void)
{
	struct vulkan_surface *cursor_surface;
	struct vulkan_surface *primary_surface;
	VkSwapchainKHR cursor;
	VkSwapchainKHR primary;
	VkSwapchainKHR replacement;
	VkSemaphore acquired;
	VkSemaphore ready;
	VkSemaphore primary_acquired;
	pthread_t holder;
	struct vulkan_swapchain *chain;
	uint32_t image;
	uint32_t changed;
	uint32_t index;
	unsigned submits;
	unsigned copies;
	unsigned waits;
	unsigned shown;
	VkResult error;
	int status;

	/* A cursor surface and swapchain (transfer destination only), and the semaphores of a compositor. */
	cursor_surface = fx_surface(VK_TRUE);
	primary_surface = fx_surface(VK_FALSE);
	cursor = fx_swapchain(cursor_surface, VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_NULL_HANDLE);
	primary = fx_swapchain(primary_surface, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_NULL_HANDLE);
	acquired = fx_semaphore();
	ready = fx_semaphore();
	primary_acquired = fx_semaphore();

	/* Every image is copied once; each present is shown by the cursor lane's worker. */
	for (index = 0U; index < FX_IMAGES; index++) {
		image = fx_acquire(cursor, acquired);
		error = fx_present(cursor, image, acquired, VK_TRUE, (int32_t)index, 0);
		assert(error == VK_SUCCESS);
		fx_drain();
	}
	assert(fx.submits == FX_IMAGES && fx.wsi_copies == FX_IMAGES);
	assert(fx.cursor_shown == FX_IMAGES && fx.cursor_on_caller == 0U);

	/* u11: the cursor chain's acquisitions never asked the display whether an image is held. */
	assert(fx.available_calls == 0U);

	/* u1: an unchanged image with a complete wait is shown at once, with no submission, copy or wait. */
	submits = fx.submits;
	copies = fx.wsi_copies;
	waits = fx.fence_waits;
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 100, 200);
	assert(error == VK_SUCCESS);
	assert(fx.submits == submits && fx.wsi_copies == copies && fx.fence_waits == waits);
	assert(fx.cursor_on_caller == 1U && fx.cursor_x == 100 && fx.cursor_y == 200);
	fx_drain();

	/* u13: a present without a position keeps the last one. */
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_FALSE, 0, 0);
	assert(error == VK_SUCCESS && fx.cursor_last_position == VK_FALSE);
	assert(fx.cursor_on_caller == 2U);

	/* u10: an unchanged image waiting for GPU work is submitted without a copy. */
	submits = fx.submits;
	copies = fx.wsi_copies;
	image = fx_acquire(cursor, acquired);
	fx_app_write(VK_NULL_HANDLE, image, acquired, ready);
	error = fx_present(cursor, image, ready, VK_TRUE, 1, 1);
	assert(error == VK_SUCCESS);
	fx_drain();
	assert(fx.wsi_copies == copies && fx.submits == submits + 2U);

	/*
	 * u3, u4 (F1 and the ticket): the shape changes, and its copy's fence
	 * is held; the next unchanged image is shown at once with a later
	 * ticket, and the copy's own present is dropped when its fence
	 * completes.  The changed image's copy is then shown without copying.
	 */
	changed = fx_acquire(cursor, acquired);
	fx_app_write(cursor, changed, acquired, ready);
	fx.hold_next = 1;
	error = fx_present(cursor, changed, ready, VK_TRUE, 2, 2);
	assert(error == VK_SUCCESS);
	shown = fx.cursor_shown;
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 3, 3);
	assert(error == VK_SUCCESS && fx.cursor_shown == shown + 1U && fx.cursor_x == 3);
	fx_release_held();
	fx_drain();
	assert(fx.cursor_dropped == 1U && fx.cursor_x == 3);
	copies = fx.wsi_copies;
	for (index = 0U; index < FX_IMAGES; index++) {
		image = fx_acquire(cursor, acquired);
		error = fx_present(cursor, image, acquired, VK_TRUE, 4, 4);
		assert(error == VK_SUCCESS);
		fx_drain();
	}
	assert(fx.wsi_copies == copies);

	/* u2 (F1): a primary present held at its fence does not hold back a cursor copy. */
	image = fx_acquire(primary, primary_acquired);
	fx.hold_next = 1;
	error = fx_present(primary, image, primary_acquired, VK_FALSE, 0, 0);
	assert(error == VK_SUCCESS);
	changed = fx_acquire(cursor, acquired);
	fx_app_write(cursor, changed, acquired, ready);
	shown = fx.cursor_shown;
	error = fx_present(cursor, changed, ready, VK_TRUE, 5, 5);
	assert(error == VK_SUCCESS);
	fx_wait_shown(shown + 1U);
	assert(fx.primary_presents == 0U);
	fx_release_held();
	fx_drain();
	assert(fx.primary_presents == 1U);

	/* Brings every image back to copied, so the presents below are shown at once. */
	for (index = 0U; index < FX_IMAGES; index++) {
		image = fx_acquire(cursor, acquired);
		error = fx_present(cursor, image, acquired, VK_TRUE, 6, 6);
		assert(error == VK_SUCCESS);
		fx_drain();
	}

	/* u5: a busy output hands the present to the worker, which retries without a fence wait. */
	waits = fx.fence_waits;
	shown = fx.cursor_shown_on_worker;
	fx.busy_left = 2U;
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 7, 7);
	assert(error == VK_SUCCESS);
	fx_drain();
	assert(fx.cursor_shown_on_worker == shown + 1U && fx.fence_waits == waits && fx.cursor_x == 7);

	/* u5: a busy present gives way to a later one, and a lasting busy output drops it without an error. */
	fx.busy_left = 1000000U;
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 8, 8);
	assert(error == VK_SUCCESS);
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 9, 9);
	assert(error == VK_SUCCESS);
	fx_drain();
	fx.busy_left = 0U;
	chain = swapchain_get(cursor);
	assert(chain->error == VK_SUCCESS);
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 10, 10);
	assert(error == VK_SUCCESS && fx.cursor_x == 10);
	fx_drain();

	/* u9: a queue lock held elsewhere (a render thread's submission) does not stop the cursor. */
	fx.check_queue_lock = 0;
	status = pthread_create(&holder, NULL, fx_hold_queue, NULL);
	assert(status == 0);
	pthread_mutex_lock(&fx.mutex);
	while (fx.queue_hold != 1)
		pthread_cond_wait(&fx.condition, &fx.mutex);
	pthread_mutex_unlock(&fx.mutex);
	image = fx_acquire(cursor, acquired);
	error = fx_present(cursor, image, acquired, VK_TRUE, 11, 11);
	assert(error == VK_SUCCESS && fx.cursor_x == 11);
	pthread_mutex_lock(&fx.mutex);
	fx.queue_hold = 2;
	pthread_cond_broadcast(&fx.condition);
	pthread_mutex_unlock(&fx.mutex);
	status = pthread_join(holder, NULL);
	assert(status == 0);
	fx.check_queue_lock = 1;

	/* u6: a replacement swapchain shares the surface's tickets, which keep growing. */
	replacement = fx_swapchain(cursor_surface, VK_IMAGE_USAGE_TRANSFER_DST_BIT, cursor);
	image = fx_acquire(replacement, acquired);
	index = (uint32_t)fx.cursor_last_ticket;
	error = fx_present(replacement, image, acquired, VK_TRUE, 12, 12);
	assert(error == VK_SUCCESS);
	fx_drain();
	assert(fx.cursor_last_ticket > index && fx.cursor_x == 12);

	/* u8: no cursor present found a lock it must not hold taken. */
	assert(fx.lock_violations == 0U);

	/* u11: the primary chain's acquisitions did ask. */
	assert(fx.available_calls != 0U);

	/* The swapchains go, after their jobs. */
	vkDestroySwapchainKHR((VkDevice)fx.device, cursor, NULL);
	vkDestroySwapchainKHR((VkDevice)fx.device, replacement, NULL);
	vkDestroySwapchainKHR((VkDevice)fx.device, primary, NULL);
	vulkan_wsi_device_finish(fx.device);
	printf("u1 u2 u3 u4 u5 u6 u8 u9 u10 u11 u13 swapchain: ok\n");
}

/* ---- fixture helpers ---- */

/* Makes the fake device, queue and context. */
static void
fx_setup(
	void)
{
	struct VkDevice_T *device;
	struct VkQueue_T *queue;

	/* The fixture's lock and the thread that runs the tests. */
	pthread_mutex_init(&fx.mutex, NULL);
	pthread_cond_init(&fx.condition, NULL);
	fx.main_thread = pthread_self();
	fx.check_queue_lock = 1;

	/* A renderer that shares images and exports no fences. */
	fx_context.capabilities = GPU_CAP_SHARE;
	fx_physical.memory.memoryTypeCount = 1U;
	fx_physical.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
	    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	fx_physical.queue_family_count = 1U;

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

/* Makes a surface of the fake platform. */
static struct vulkan_surface *
fx_surface(
	VkBool32 cursor)
{
	struct vulkan_surface *surface;

	/* A 64x64 surface, cursor or not. */
	surface = calloc(1, sizeof(*surface));
	assert(surface != NULL);
	surface->object.kind = VULKAN_OBJECT_SURFACE;
	surface->platform = &fx_platform;
	surface->cursor = cursor;
	surface->display_info.imageExtent.width = 64U;
	surface->display_info.imageExtent.height = 64U;
	return surface;
}

/* Makes a swapchain of three images on a surface. */
static VkSwapchainKHR
fx_swapchain(
	struct vulkan_surface *surface,
	VkImageUsageFlags usage,
	VkSwapchainKHR old)
{
	VkSwapchainCreateInfoKHR create;
	VkSwapchainKHR swapchain;
	VkResult error;

	/* The compositor's request. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create.surface = (VkSurfaceKHR)(uintptr_t)surface;
	create.minImageCount = FX_IMAGES;
	create.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	create.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	create.imageExtent.width = 64U;
	create.imageExtent.height = 64U;
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

	/* A sync object whose payload is empty. */
	sync = calloc(1, sizeof(*sync));
	assert(sync != NULL);
	sync->object.kind = VULKAN_OBJECT_SEMAPHORE;
	return (VkSemaphore)(uintptr_t)sync;
}

/* Acquires the next image at once. */
static uint32_t
fx_acquire(
	VkSwapchainKHR swapchain,
	VkSemaphore semaphore)
{
	uint32_t image;
	VkResult error;

	/* An image is free: the tests drain before they run out. */
	error = vkAcquireNextImageKHR((VkDevice)fx.device, swapchain, 0U, semaphore, VK_NULL_HANDLE, &image);
	assert(error == VK_SUCCESS);
	return image;
}

/* Submits the compositor's work on an image: a copy into it when a swapchain is named, nothing otherwise. */
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

	/* The recording, with the copy noted as the generated wrapper notes it. */
	memset(&command, 0, sizeof(command));
	if (swapchain != VK_NULL_HANDLE) {
		chain = swapchain_get(swapchain);
		vulkan_content_note_image(&command, chain->group->images[image].image);
	}

	/* An accepted submission consumes the acquisition, counts the write, and leaves its signal on the GPU. */
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
	fx.submits++;
}

/* Presents one image, with or without a position. */
static VkResult
fx_present(
	VkSwapchainKHR swapchain,
	uint32_t image,
	VkSemaphore wait,
	VkBool32 position,
	int32_t x,
	int32_t y)
{
	VkDisplayPresentInfoKHR display;
	VkPresentInfoKHR present;
	VkResult error;

	/* The whole image with its corner at the point. */
	memset(&display, 0, sizeof(display));
	display.sType = VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR;
	display.srcRect.extent.width = 64U;
	display.srcRect.extent.height = 64U;
	display.dstRect.offset.x = x;
	display.dstRect.offset.y = y;
	display.dstRect.extent = display.srcRect.extent;
	memset(&present, 0, sizeof(present));
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	if (position != VK_FALSE)
		present.pNext = &display;
	present.waitSemaphoreCount = 1U;
	present.pWaitSemaphores = &wait;
	present.swapchainCount = 1U;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &image;
	error = vkQueuePresentKHR((VkQueue)fx.queue, &present);
	return error;
}

/* Waits until every accepted present of the queue has retired. */
static void
fx_drain(
	void)
{
	VkResult error;

	/* The WSI's own drain. */
	error = vulkan_wsi_queue_idle(fx.queue);
	assert(error == VK_SUCCESS);
}

/* Completes the held fence. */
static void
fx_release_held(
	void)
{
	/* Its waiter wakes. */
	pthread_mutex_lock(&fx.mutex);
	assert(fx.held != NULL);
	fx.held->held = 0;
	fx.held = NULL;
	pthread_cond_broadcast(&fx.condition);
	pthread_mutex_unlock(&fx.mutex);
}

/* Waits up to two seconds for a number of shown cursor presents. */
static void
fx_wait_shown(
	unsigned shown)
{
	struct timespec deadline;
	int status;

	/* The worker signals each shown present. */
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 2;
	pthread_mutex_lock(&fx.mutex);
	status = 0;
	while (fx.cursor_shown < shown && status == 0)
		status = pthread_cond_timedwait(&fx.condition, &fx.mutex, &deadline);
	pthread_mutex_unlock(&fx.mutex);
	assert(fx.cursor_shown >= shown);
}

/* Holds the queue lock, as a render thread's submission does, until the test lets go. */
static void *
fx_hold_queue(
	void *argument)
{
	(void)argument;

	/* The lock is taken before the test presents, and kept until it says. */
	pthread_mutex_lock(&fx.queue->mutex);
	pthread_mutex_lock(&fx.mutex);
	fx.queue_hold = 1;
	pthread_cond_broadcast(&fx.condition);
	while (fx.queue_hold != 2)
		pthread_cond_wait(&fx.condition, &fx.mutex);
	fx.queue_hold = 0;
	pthread_mutex_unlock(&fx.mutex);
	pthread_mutex_unlock(&fx.queue->mutex);
	return NULL;
}

/* ---- the fake platform ---- */

/* A fixed 64x64 surface with transfer and color use. */
static VkResult
fx_capabilities(
	struct vulkan_surface *surface,
	struct VkPhysicalDevice_T *physical,
	VkSurfaceCapabilitiesKHR *capabilities)
{
	(void)physical;
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->minImageCount = 2U;
	capabilities->currentExtent = surface->display_info.imageExtent;
	capabilities->minImageExtent = surface->display_info.imageExtent;
	capabilities->maxImageExtent = surface->display_info.imageExtent;
	capabilities->maxImageArrayLayers = 1U;
	capabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	capabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	capabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	capabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	return VK_SUCCESS;
}

/* One BGRA format. */
static VkResult
fx_formats(
	struct vulkan_surface *surface,
	struct VkPhysicalDevice_T *physical,
	uint32_t *count,
	VkSurfaceFormatKHR *formats)
{
	(void)surface;
	(void)physical;
	*count = 1U;
	formats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
	formats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	return VK_SUCCESS;
}

/* FIFO only. */
static VkResult
fx_modes(
	struct vulkan_surface *surface,
	struct VkPhysicalDevice_T *physical,
	uint32_t *count,
	VkPresentModeKHR *modes)
{
	(void)surface;
	(void)physical;
	*count = 1U;
	modes[0] = VK_PRESENT_MODE_FIFO_KHR;
	return VK_SUCCESS;
}

/* A lease per claim. */
static VkResult
fx_claim(
	struct vulkan_surface *surface,
	struct VkDevice_T *device,
	void **result)
{
	struct fx_lease *lease;

	(void)device;
	lease = calloc(1, sizeof(*lease));
	assert(lease != NULL);
	lease->surface = surface;
	*result = lease;
	return VK_SUCCESS;
}

/* Frees the lease. */
static VkResult
fx_release(
	void *lease)
{
	free(lease);
	return VK_SUCCESS;
}

/* A native image per import; the fd is the WSI's to close. */
static VkResult
fx_import(
	void *lease,
	int fd,
	const struct gpu_image_descriptor *descriptor,
	void **result)
{
	(void)lease;
	(void)fd;
	(void)descriptor;
	*result = malloc(1);
	assert(*result != NULL);
	return VK_SUCCESS;
}

/* A primary present, counted. */
static VkResult
fx_present_image(
	void *lease,
	void *image,
	VkPresentModeKHR mode,
	uint64_t *sequence)
{
	(void)lease;
	(void)image;
	(void)mode;
	*sequence = 0U;
	pthread_mutex_lock(&fx.mutex);
	fx.primary_presents++;
	pthread_mutex_unlock(&fx.mutex);
	return VK_SUCCESS;
}

/* Counts the question and answers that the image is free. */
static VkBool32
fx_available(
	void *image)
{
	(void)image;
	__atomic_add_fetch(&fx.available_calls, 1U, __ATOMIC_RELAXED);
	return VK_TRUE;
}

/* Frees a native image. */
static void
fx_destroy_image(
	void *image)
{
	free(image);
}

/*
 * The cursor present: checks that no WSI lock is held, applies the ticket
 * like the display, and gives the busy answers the test asked for.
 */
static VkResult
fx_present_cursor(
	void *lease,
	void *image,
	VkBool32 has_position,
	int32_t x,
	int32_t y,
	uint64_t ticket,
	uint64_t *frame,
	uint64_t *sequence,
	enum vulkan_wsi_cursor_outcome *outcome,
	struct vulkan_wsi_cursor_times *times)
{
	struct fx_lease *fake;
	int status;

	(void)image;
	fake = lease;
	assert(fake->surface->cursor != VK_FALSE);

	/* u8: the swapchain, device and queue locks are all free. */
	status = pthread_mutex_trylock(&swapchain_mutex);
	if (status == 0)
		pthread_mutex_unlock(&swapchain_mutex);
	else
		fx.lock_violations++;
	status = pthread_mutex_trylock(&fx.device->mutex);
	if (status == 0)
		pthread_mutex_unlock(&fx.device->mutex);
	else
		fx.lock_violations++;
	if (fx.check_queue_lock) {
		status = pthread_mutex_trylock(&fx.queue->mutex);
		if (status == 0)
			pthread_mutex_unlock(&fx.queue->mutex);
		else
			fx.lock_violations++;
	}

	/* The times, when the log is on. */
	if (times != NULL) {
		times->locked = vulkan_wsi_cursor_clock();
		times->set = times->locked;
		times->done = times->locked;
	}

	/* The ticket decides as at the display; a busy answer changes nothing. */
	pthread_mutex_lock(&fx.mutex);
	fx.cursor_calls++;
	if (pthread_equal(pthread_self(), fx.main_thread))
		fx.cursor_on_caller++;
	fx.cursor_last_ticket = ticket;
	fx.cursor_last_position = has_position;
	if (ticket <= fx.cursor_applied) {
		*outcome = VULKAN_WSI_CURSOR_DROPPED;
		fx.cursor_dropped++;
	} else if (fx.busy_left != 0U) {
		fx.busy_left--;
		*outcome = VULKAN_WSI_CURSOR_BUSY;
	} else {
		fx.cursor_applied = ticket;
		if (has_position != VK_FALSE) {
			fx.cursor_x = x;
			fx.cursor_y = y;
		}
		(*frame)++;
		*sequence = 0U;
		*outcome = VULKAN_WSI_CURSOR_SHOWN;
		fx.cursor_shown++;
		if (!pthread_equal(pthread_self(), fx.main_thread))
			fx.cursor_shown_on_worker++;
		pthread_cond_broadcast(&fx.condition);
	}
	pthread_mutex_unlock(&fx.mutex);
	return VK_SUCCESS;
}

/* ---- the display's ioctl ---- */

/* Answers the cursor SET and MOVE, and notes whether display_mutex is free during them. */
int
fixture_ioctl(
	int fd,
	unsigned long request,
	...)
{
	struct gpu_display_cursor_move *move;
	va_list arguments;
	void *argument;
	int status;

	(void)fd;
	va_start(arguments, request);
	argument = va_arg(arguments, void *);
	va_end(arguments);

	/* display_mutex is free unless the test holds it from its own thread. */
	status = pthread_mutex_trylock(&display_mutex);
	if (status == 0) {
		pthread_mutex_unlock(&display_mutex);
		fx_ioctl_state.mutex_free++;
	}

	/* SET, then MOVE, each with the error the test chose. */
	if (request == GPU_DISPLAY_CURSOR_SET) {
		fx_ioctl_state.sets++;
		if (fx_ioctl_state.set_error != 0) {
			errno = fx_ioctl_state.set_error;
			return -1;
		}
		return 0;
	}
	if (request == GPU_DISPLAY_CURSOR_MOVE) {
		fx_ioctl_state.moves++;
		if (fx_ioctl_state.move_error != 0) {
			errno = fx_ioctl_state.move_error;
			return -1;
		}
		move = argument;
		fx_ioctl_state.x = move->x;
		fx_ioctl_state.y = move->y;
		return 0;
	}
	errno = ENOTTY;
	return -1;
}

/* ---- the core Vulkan calls the WSI makes ---- */

void *
vulkan_allocate(
	const struct vulkan_allocator *allocator,
	size_t bytes,
	size_t alignment,
	VkSystemAllocationScope scope)
{
	void *memory;

	(void)allocator;
	(void)alignment;
	(void)scope;
	memory = calloc(1, bytes == 0U ? 1U : bytes);
	return memory;
}

void
vulkan_free(
	const struct vulkan_allocator *allocator,
	void *allocation)
{
	(void)allocator;
	free(allocation);
}

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

void
vulkan_object_free(
	struct vulkan_object *object)
{
	free(object);
}

VkResult
vulkan_object_publish(
	struct vulkan_object *object)
{
	object->published = VK_TRUE;
	return VK_SUCCESS;
}

void
vulkan_object_unpublish(
	struct vulkan_object *object)
{
	object->published = VK_FALSE;
}

struct vulkan_object *
vulkan_nondispatchable_object(
	uint64_t handle)
{
	return (struct vulkan_object *)(uintptr_t)handle;
}

uint64_t
vulkan_nondispatchable_handle(
	struct vulkan_object *object)
{
	return (uint64_t)(uintptr_t)object;
}

struct VkDevice_T *
vulkan_device(
	VkDevice device)
{
	return (struct VkDevice_T *)device;
}

struct VkQueue_T *
vulkan_queue(
	VkQueue queue)
{
	return (struct VkQueue_T *)queue;
}

struct vulkan_image *
vulkan_image(
	VkImage image)
{
	return (struct vulkan_image *)(uintptr_t)image;
}

struct vulkan_sync *
vulkan_sync_object(
	uint64_t handle)
{
	return (struct vulkan_sync *)(uintptr_t)handle;
}

struct vulkan_surface *
vulkan_wsi_surface(
	VkSurfaceKHR surface)
{
	return (struct vulkan_surface *)(uintptr_t)surface;
}

VkResult
vulkan_wsi_surface_retain(
	struct vulkan_surface *surface)
{
	surface->swapchain_references++;
	return VK_SUCCESS;
}

void
vulkan_wsi_surface_release(
	struct vulkan_surface *surface)
{
	surface->swapchain_references--;
}

void
vulkan_device_error(
	struct VkDevice_T *device,
	VkResult error)
{
	__atomic_store_n(&device->error, error, __ATOMIC_RELEASE);
}

void
vulkan_context_error(
	struct vulkan_context *context,
	VkResult error)
{
	__atomic_store_n(&context->error, error, __ATOMIC_RELEASE);
}

void
vulkan_context_lock(
	struct vulkan_context *context)
{
	(void)context;
}

void
vulkan_context_unlock(
	struct vulkan_context *context)
{
	(void)context;
}

VkResult
vulkan_sync_device_status_locked(
	struct VkDevice_T *device)
{
	VkResult status;

	status = __atomic_load_n(&device->error, __ATOMIC_ACQUIRE);
	return status;
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

/* A WSI submission: consumes complete waits, counts writes, and makes its fence pending (or held). */
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
	fx.fence_waits++;
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
	(void)extent;
	(void)allocator;
	(void)placement;
	created = calloc(1, sizeof(*created));
	assert(created != NULL);
	*image = (VkImage)(uintptr_t)created;
	*memory = (VkDeviceMemory)(uintptr_t)malloc(1);
	*fd = open("/dev/null", O_RDONLY);
	memset(descriptor, 0, sizeof(*descriptor));
	return VK_SUCCESS;
}

VkResult vulkan_wake_create(struct vulkan_wake *wake, struct VkDevice_T *device) { (void)wake; (void)device; return VK_ERROR_INITIALIZATION_FAILED; }
void vulkan_wake_destroy(struct vulkan_wake *wake) { (void)wake; }
void vulkan_wake_drain(struct vulkan_wake *wake) { (void)wake; }
void vulkan_wake_notify(struct VkDevice_T *device, struct vulkan_context *context) { (void)device; (void)context; }
VkResult vulkan_wsi_display_node_query(struct VkPhysicalDevice_T *physical, uint32_t index, uint32_t *count, struct gpu_display_info *request, uint64_t *device_id, char *path) { (void)physical; (void)index; (void)count; (void)request; (void)device_id; (void)path; return VK_ERROR_INITIALIZATION_FAILED; }
int vulkan_wsi_display_node_ioctl(struct VkPhysicalDevice_T *physical, const struct vulkan_wsi_output *output, unsigned long command, void *argument) { (void)physical; (void)output; (void)command; (void)argument; return -1; }
VkResult vulkan_wsi_display_refresh(struct vulkan_display *display) { (void)display; return VK_ERROR_INITIALIZATION_FAILED; }
VkResult vulkan_wsi_display_snapshot(struct vulkan_display *display, struct vulkan_wsi_output *output) { (void)display; (void)output; return VK_ERROR_INITIALIZATION_FAILED; }

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
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physical, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *properties) { (void)physical; (void)format; (void)type; (void)tiling; (void)usage; (void)flags; (void)properties; return VK_ERROR_FORMAT_NOT_SUPPORTED; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *info, const VkAllocationCallbacks *allocator, VkCommandPool *pool) { (void)device; (void)info; (void)allocator; *pool = (VkCommandPool)(uintptr_t)malloc(1); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks *allocator) { (void)device; (void)allocator; free((void *)(uintptr_t)pool); }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *info, VkCommandBuffer *buffers) { (void)device; (void)info; buffers[0] = (VkCommandBuffer)calloc(1, sizeof(struct VkCommandBuffer_T)); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer buffer, const VkCommandBufferBeginInfo *info) { (void)info; vulkan_content_clear((struct VkCommandBuffer_T *)buffer); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer buffer) { (void)buffer; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer buffer, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count, const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images) { (void)buffer; (void)source; (void)destination; (void)flags; (void)memory_count; (void)memory; (void)buffer_count; (void)buffers; (void)image_count; (void)images; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer buffer, VkImage source, VkImageLayout source_layout, VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageCopy *regions) { (void)source; (void)source_layout; (void)destination_layout; (void)count; (void)regions; vulkan_content_note_image((struct VkCommandBuffer_T *)buffer, destination); fx.wsi_copies++; }
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer buffer, VkImage source, VkImageLayout source_layout, VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageBlit *regions, VkFilter filter) { (void)buffer; (void)source; (void)source_layout; (void)destination; (void)destination_layout; (void)count; (void)regions; (void)filter; assert(0); }
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer buffer, VkImage image, VkImageLayout layout, const VkClearColorValue *color, uint32_t count, const VkImageSubresourceRange *ranges) { (void)buffer; (void)image; (void)layout; (void)color; (void)count; (void)ranges; assert(0); }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer buffer, VkImage image, VkImageLayout layout, VkBuffer destination, uint32_t count, const VkBufferImageCopy *regions) { (void)buffer; (void)image; (void)layout; (void)destination; (void)count; (void)regions; assert(0); }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo *info, const VkAllocationCallbacks *allocator, VkFence *fence) { (void)device; (void)info; (void)allocator; *fence = (VkFence)(uintptr_t)calloc(1, sizeof(struct fx_fence)); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t count, const VkFence *fences) { (void)device; (void)count; assert(((struct fx_fence *)(uintptr_t)fences[0])->pending == 0); return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *allocator) { (void)device; (void)allocator; free((void *)(uintptr_t)fence); }
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR *info, int *fd) { (void)device; (void)info; (void)fd; return VK_ERROR_FEATURE_NOT_PRESENT; }
