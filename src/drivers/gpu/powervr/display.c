/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display node of the PowerVR driver: the boot framebuffer.
 *
 * The VisionFive 2 has no display controller driver of its own yet; the
 * loader leaves the screen on and hands over its framebuffer, which the
 * kernel's text console draws on.  This node gives that screen to a
 * session through the display operations (query, mode, claim, release,
 * present, wait): one output, one plane, one mode -- the loader's extent,
 * reported at 60 Hz -- and one lease at a time.
 *
 * While a lease is held the text console does not draw; the screen is
 * cleared for the lease's frames, and given back to the console (which
 * draws its retained text again) when the lease is released, also when
 * its holder closes or exits without releasing it.
 *
 * A presentation copies the frame with the CPU from a resource this node
 * allocated (ordinary cacheable RAM the session writes or maps) onto the
 * framebuffer, row by row, clipped to the screen, swapping red and blue
 * when the screen's order is not the frame's.  The framebuffer is written
 * through the direct map and then cleaned out of the caches, as the text
 * console draws it (WS256 p011, drawing (β)); a framebuffer outside the
 * direct map is written through an uncached device view instead.  The
 * presentation has completed on the screen when it returns.
 *
 * The node is registered before the render node, so it is gpu0 and the
 * render node gpu1.  It does not depend on the GPU core: it stays when the
 * core fails to come up.
 *
 * XXX: importing a render buffer for scanout (a private lookup of the
 * render node's object) waits for the render node's sharing; the import
 * operation exists and refuses with ENOTSUP until then.
 *
 * Procedure referenced:
 * src/drivers/gpu/bcm2711/display-device.c and
 * src/drivers/gpu/i915/display/bootfb.c (the display operations' contract
 * on a boot framebuffer).
 */

#include "drivers/gpu/powervr/powervr.h"
#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-display.h>
#include <drivers/gpu/gpu-scanout.h>
#include <kern/boot.h>
#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/platform.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/text-display.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-display.h>
#include <uapi/gpu-scanout.h>

/* The one output's identifier and generation; the screen never changes. */
#define POWERVR_DISPLAY_ID		1U
#define POWERVR_DISPLAY_GENERATION	1U

/* The refresh reported for the loader's screen, which does not tell its own. */
#define POWERVR_DISPLAY_REFRESH		60000U

/* The resources one session may hold, and the largest one (64 MiB). */
#define POWERVR_DISPLAY_RESOURCES	16U
#define POWERVR_DISPLAY_RESOURCE_BYTES	(64ULL << 20)

/* The page resources are allocated in, and the framebuffer's view is made in. */
#define POWERVR_DISPLAY_PAGE_BYTES	4096U

/* The bytes of one pixel of the screen and of a frame. */
#define POWERVR_DISPLAY_PIXEL_BYTES	4U

/* The name a query reports, and the driver name of the node. */
#define POWERVR_DISPLAY_NAME		"powervr bootfb"
#define POWERVR_DISPLAY_DRIVER		"powervr-display"

/* The capabilities of the node: its own storage, copies, CPU views and the display. */
#define POWERVR_DISPLAY_CAPABILITIES \
	(GPU_CAP_RESOURCE | GPU_CAP_TRANSFER | GPU_CAP_MAPPING | GPU_CAP_DISPLAY)

/*
 * One open of the node.
 *
 * Made by open and freed by close, after the core has destroyed every
 * resource of the open; the count is kept under the node's mutex.
 */
struct powervr_display_session {
	uint32_t resources;
};

/*
 * One resource of a session: a run of ordinary cacheable RAM.
 *
 * Made by resource_create and freed by resource_destroy; the run and its
 * address do not change in between, so a CPU view of it stays valid.
 */
struct powervr_display_resource {
	struct powervr_display_session *owner;
	struct kern_pmem run;
	uint8_t *address;
	uint64_t bytes;
};

/*
 * The node: the screen, how the CPU writes it, and the lease.
 *
 * Filled once by drv_powervr_display_attach() on the boot CPU; afterwards
 * the lease fields change only under the mutex, and the screen fields do
 * not change.
 */
struct powervr_display {
	/* Nonzero once the screen is known and the node is registered. */
	int ready;

	/* The loader's screen as the kernel reads it. */
	struct kern_boot_framebuffer screen;

	/* Nonzero when the screen's bytes are R, G, B, X; zero for B, G, R, X. */
	int rgbx;

	/* The CPU's view of the screen, and whether it is the direct map (cleaned after writes). */
	uint8_t *pixels;
	int direct;

	/* The device view this file made (NULL with the direct map), and its extent. */
	void *view;
	size_t view_bytes;

	/* Protects the lease fields below. */
	struct mutex mutex;

	/* The session holding the lease (NULL when none), its lease, and the next lease's number. */
	struct powervr_display_session *owner;
	uint64_t lease;
	uint64_t next_lease;

	/* Frames shown under the lease, and the tick the newest was shown at. */
	uint64_t sequence;
	uint64_t present_tick;

	/* The core's handle of the registered node. */
	struct drv_gpu_device *gpu;
};

/* Declared before the operation tables that name them. */
static int display_open(void *device, void **result);
static void display_close(void *device, void *session);
static int display_info(void *device, void *session, struct gpu_info *info);
static int resource_create(void *device, void *session, const struct gpu_resource_create *request, void **result);
static void resource_destroy(void *device, void *session, void *resource);
static int resource_read(void *device, void *session, void *resource, uint64_t offset, void *data, uint32_t bytes);
static int resource_write(void *device, void *session, void *resource, uint64_t offset, const void *data, uint32_t bytes);
static int resource_map(void *device, void *session, void *resource, struct drv_gpu_mapping *mapping);
static int output_query(void *device, void *session, struct gpu_display_info *request);
static int output_mode(void *device, void *session, struct gpu_display_mode *request);
static int output_claim(void *device, void *session, struct gpu_display_claim *request);
static int output_release(void *device, void *session, const struct gpu_display_release *request);
static int output_present(void *device, void *session, void *resource, struct gpu_display_present *request);
static int output_wait(void *device, void *session, struct gpu_display_wait *request);
static int output_device(void *device, void *session, struct gpu_device_info *info);
static int output_constraints(void *device, void *session, struct gpu_scanout_constraints *request);
static int import_image(void *device, void *session, const struct gpu_image_descriptor *image, const struct drv_gpu_scanout_backing *backing, void **result);

/* The display operations of the node. */
static const struct drv_gpu_display_ops display_operations = {
	.query = output_query,
	.mode = output_mode,
	.claim = output_claim,
	.release = output_release,
	.present = output_present,
	.wait = output_wait,
};

/* The scanout operations: the node's role, its constraints, and the import that waits for the render node. */
static const struct drv_gpu_scanout_ops scanout_operations = {
	.query_device = output_device,
	.constraints = output_constraints,
	.import_image = import_image,
};

/* The operations of the node. */
static const struct drv_gpu_ops node_operations = {
	.version = DRV_GPU_INTERFACE_VERSION,
	.size = sizeof(struct drv_gpu_ops),
	.capabilities = POWERVR_DISPLAY_CAPABILITIES,
	.open = display_open,
	.close = display_close,
	.get_info = display_info,
	.resource_create = resource_create,
	.resource_destroy = resource_destroy,
	.resource_read = resource_read,
	.resource_write = resource_write,
	.resource_map = resource_map,
	.display = &display_operations,
	.scanout = &scanout_operations,
};

/*
 * The board's one display node, set up by drv_powervr_display_attach().
 */
static struct powervr_display board_display;

static int screen_valid(const struct kern_boot_framebuffer *screen);
static int map_screen(struct powervr_display *display);
static void unmap_screen(struct powervr_display *display);
static void clear_screen(struct powervr_display *display);
static void flush_screen(struct powervr_display *display, uint32_t rows);
static void show_frame(struct powervr_display *display, const struct powervr_display_resource *resource, const struct gpu_display_present *request);
static void end_lease(struct powervr_display *display);

/*
 * Registers the display node on the loader's screen.
 */
int
drv_powervr_display_attach(
	void)
{
	struct powervr_display *display;
	int valid;
	int error;

	/* Refuses a second node. */
	display = &board_display;
	if (display->ready)
		return EALREADY;

	/* Reads the loader's screen; a boot without one has no display node. */
	error = kern_boot_framebuffer("riscv64.framebuffer", &display->screen);
	if (error != 0) {
		kern_logf("powervr: no display: no boot framebuffer (%d)\n", error);
		return error;
	}

	/* Refuses a screen the copies cannot write. */
	valid = screen_valid(&display->screen);
	if (!valid) {
		kern_logf("powervr: no display: framebuffer %ux%u pitch %u format %u not usable\n",
			  display->screen.width,
			  display->screen.height,
			  display->screen.pitch,
			  display->screen.format);
		return ENODEV;
	}

	display->rgbx = 0;
	if (display->screen.format == KERN_BOOT_FRAMEBUFFER_RGBX8888)
		display->rgbx = 1;

	/* Makes the CPU's view of the screen. */
	error = map_screen(display);
	if (error != 0) {
		kern_logf("powervr: no display: framebuffer map failed (%d)\n", error);
		return error;
	}

	/* The lease's lock; the lease starts free and lease numbers start at one. */
	error = mutex_init(&display->mutex, LOCK_RANK_DEVICE, "powervr-display");
	if (error != 0) {
		unmap_screen(display);
		kern_logf("powervr: no display: lock failed (%d)\n", error);
		return error;
	}

	display->owner = NULL;
	display->lease = 0U;
	display->next_lease = 1U;

	/* Publishes the node; before the render node, so it is the first GPU device. */
	error = drv_gpu_register(&node_operations, display, &display->gpu);
	if (error != 0) {
		unmap_screen(display);
		kern_logf("powervr: no display: register failed (%d)\n", error);
		return error;
	}

	display->ready = 1;
	kern_logf("powervr: display %ux%u on the boot framebuffer\n",
		  display->screen.width,
		  display->screen.height);

	/* Succeeded: the screen is a display node. */
	return 0;
}

/* Creates a session without a resource or the lease. */
static int
display_open(
	void *device,
	void **result)
{
	struct powervr_display_session *session;

	(void)device;

	/* Each open starts with nothing. */
	*result = NULL;
	session = kern_calloc(1, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	*result = session;

	/* Succeeded: the core owns the session. */
	return 0;
}

/* Ends a session; its lease, if it still holds one, gives the screen back to the console. */
static void
display_close(
	void *device,
	void *private_session)
{
	struct powervr_display *display;
	struct powervr_display_session *session;

	display = device;
	session = private_session;

	/* A holder that did not release its lease loses it here. */
	mutex_lock(&display->mutex);

	if (display->owner == session)
		end_lease(display);

	mutex_unlock(&display->mutex);

	/* The core destroyed every resource of the session before closing it. */
	kern_free(session);
}

/* Reports the node's capabilities and storage limits. */
static int
display_info(
	void *device,
	void *session,
	struct gpu_info *info)
{
	(void)device;
	(void)session;

	/* What the node does and how much storage a session may hold. */
	info->capabilities = POWERVR_DISPLAY_CAPABILITIES;
	info->max_resources = POWERVR_DISPLAY_RESOURCES;
	info->max_resource_bytes = POWERVR_DISPLAY_RESOURCE_BYTES;
	kern_snprintf(info->driver_name, sizeof(info->driver_name), "%s", POWERVR_DISPLAY_DRIVER);

	/* Succeeded: the limits describe the node's own storage. */
	return 0;
}

/* Allocates one storage resource of a session in ordinary cacheable RAM. */
static int
resource_create(
	void *device,
	void *private_session,
	const struct gpu_resource_create *request,
	void **result)
{
	struct powervr_display *display;
	struct powervr_display_session *session;
	struct powervr_display_resource *resource;
	uint64_t bytes;
	int error;

	display = device;
	session = private_session;
	*result = NULL;

	/* Only plain storage of a bounded size. */
	if (request->bytes == 0U || request->bytes > POWERVR_DISPLAY_RESOURCE_BYTES)
		return EINVAL;
	if (request->usage != GPU_RESOURCE_USAGE_STORAGE || request->flags != 0U)
		return ENOTSUP;

	/* Whole pages, so a CPU view of the run maps nothing else. */
	bytes = (request->bytes + POWERVR_DISPLAY_PAGE_BYTES - 1U) & ~(uint64_t)(POWERVR_DISPLAY_PAGE_BYTES - 1U);

	/* The record first, then its memory. */
	resource = kern_calloc(1, sizeof(*resource));
	if (resource == NULL)
		return ENOMEM;

	error = kern_pmem_alloc((size_t)bytes, POWERVR_DISPLAY_PAGE_BYTES, &resource->run);
	if (error != 0) {
		kern_free(resource);
		return error;
	}

	resource->address = kern_pmem_to_kernel(resource->run.paddr);
	if (resource->address == NULL) {
		(void)kern_pmem_free(&resource->run);
		kern_free(resource);
		return ENOMEM;
	}

	/* New storage holds zeroes, not an earlier owner's bytes. */
	kern_memset(resource->address, 0, (size_t)bytes);
	resource->owner = session;
	resource->bytes = request->bytes;

	/* Counts the resource against the session's limit. */
	mutex_lock(&display->mutex);

	if (session->resources >= POWERVR_DISPLAY_RESOURCES) {
		mutex_unlock(&display->mutex);
		(void)kern_pmem_free(&resource->run);
		kern_free(resource);
		return ENOSPC;
	}

	session->resources++;

	mutex_unlock(&display->mutex);

	*result = resource;

	/* Succeeded: the core owns the resource until resource_destroy. */
	return 0;
}

/* Frees one resource after the core has dropped every view and pin of it. */
static void
resource_destroy(
	void *device,
	void *private_session,
	void *private_resource)
{
	struct powervr_display *display;
	struct powervr_display_session *session;
	struct powervr_display_resource *resource;

	display = device;
	session = private_session;
	resource = private_resource;

	/* Gives the resource's place in the session's limit back. */
	mutex_lock(&display->mutex);

	if (session->resources != 0U)
		session->resources--;

	mutex_unlock(&display->mutex);

	/* A presentation copies and returns, so nothing on the screen still reads the run. */
	(void)kern_pmem_free(&resource->run);
	kern_free(resource);
}

/* Copies bytes out of a resource. */
static int
resource_read(
	void *device,
	void *session,
	void *private_resource,
	uint64_t offset,
	void *data,
	uint32_t bytes)
{
	struct powervr_display_resource *resource;

	(void)device;
	resource = private_resource;

	/* Only the owner's resource, and only inside it. */
	if (resource->owner != session)
		return EINVAL;
	if (offset > resource->bytes || bytes > resource->bytes - offset)
		return EINVAL;

	/* The run is ordinary RAM the CPU reads through the direct map. */
	kern_memcpy(data, resource->address + offset, bytes);

	/* Succeeded: the bytes are copied. */
	return 0;
}

/* Copies bytes into a resource. */
static int
resource_write(
	void *device,
	void *session,
	void *private_resource,
	uint64_t offset,
	const void *data,
	uint32_t bytes)
{
	struct powervr_display_resource *resource;

	(void)device;
	resource = private_resource;

	/* Only the owner's resource, and only inside it. */
	if (resource->owner != session)
		return EINVAL;
	if (offset > resource->bytes || bytes > resource->bytes - offset)
		return EINVAL;

	/* The run is ordinary RAM the CPU writes through the direct map. */
	kern_memcpy(resource->address + offset, data, bytes);

	/* Succeeded: the bytes are copied. */
	return 0;
}

/* Describes a resource's run for a cacheable CPU view in the session's address space. */
static int
resource_map(
	void *device,
	void *session,
	void *private_resource,
	struct drv_gpu_mapping *mapping)
{
	struct powervr_display_resource *resource;

	(void)device;
	resource = private_resource;

	/* Only the owner maps its resource. */
	if (resource->owner != session)
		return EINVAL;

	/* Ordinary cacheable RAM, as the CPU copies of a presentation read it. */
	mapping->physical = resource->run.paddr;
	mapping->address = resource->address;
	mapping->bytes = resource->run.size;
	mapping->attributes = 0U;

	/* Succeeded: the view stays valid while the resource lives. */
	return 0;
}

/* Describes the one output: the loader's screen. */
static int
output_query(
	void *device,
	void *session,
	struct gpu_display_info *request)
{
	struct powervr_display *display;

	(void)session;
	display = device;

	/* One output. */
	request->count = 1U;
	if (request->index == GPU_DISPLAY_COUNT_ONLY)
		return 0;
	if (request->index != 0U)
		return EINVAL;

	/* Connected, FIFO, active while a lease has shown a frame. */
	request->display_id = POWERVR_DISPLAY_ID;
	request->generation = POWERVR_DISPLAY_GENERATION;
	request->flags = GPU_DISPLAY_CONNECTED | GPU_DISPLAY_FIFO;
	mutex_lock(&display->mutex);

	if (display->owner != NULL && display->sequence != 0U)
		request->flags |= GPU_DISPLAY_ACTIVE;

	mutex_unlock(&display->mutex);

	/* One plane, the two packed formats, and the largest frame taken. */
	request->plane_count = 1U;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->max_frame_bytes = POWERVR_DISPLAY_RESOURCE_BYTES;

	/* The loader's extent is the current, preferred and largest one. */
	request->current_width = display->screen.width;
	request->current_height = display->screen.height;
	request->preferred_width = display->screen.width;
	request->preferred_height = display->screen.height;
	request->max_width = display->screen.width;
	request->max_height = display->screen.height;
	request->refresh_millihz = POWERVR_DISPLAY_REFRESH;

	/* The loader does not tell the screen's physical size. */
	request->physical_width_mm = 0U;
	request->physical_height_mm = 0U;
	kern_snprintf(request->name, sizeof(request->name), "%s", POWERVR_DISPLAY_NAME);

	/* Succeeded: the output is described. */
	return 0;
}

/* Enumerates the one mode, or checks a mode: the loader's extent or a smaller one, at 60 Hz. */
static int
output_mode(
	void *device,
	void *session,
	struct gpu_display_mode *request)
{
	struct powervr_display *display;

	(void)session;
	display = device;

	/* Only the one output of this generation. */
	if (request->display_id != POWERVR_DISPLAY_ID)
		return ENOENT;
	if (request->generation != POWERVR_DISPLAY_GENERATION)
		return ESTALE;

	/* Enumeration: the loader's mode. */
	if (request->operation == GPU_DISPLAY_MODE_ENUMERATE) {
		request->count = 1U;
		if (request->index == GPU_DISPLAY_COUNT_ONLY)
			return 0;
		if (request->index != 0U)
			return EINVAL;

		request->width = display->screen.width;
		request->height = display->screen.height;
		request->refresh_millihz = POWERVR_DISPLAY_REFRESH;
		return 0;
	}

	/* Validation: an extent that fits, at the one refresh (zero asks for it). */
	if (request->operation != GPU_DISPLAY_MODE_VALIDATE)
		return EINVAL;
	if (request->width == 0U || request->height == 0U)
		return EINVAL;
	if (request->width > display->screen.width || request->height > display->screen.height)
		return EINVAL;
	if (request->refresh_millihz != 0U && request->refresh_millihz != POWERVR_DISPLAY_REFRESH)
		return EINVAL;

	request->refresh_millihz = POWERVR_DISPLAY_REFRESH;

	/* Succeeded: the mode is shown unscaled at the top left. */
	return 0;
}

/*
 * Gives the lease to a session: the text console stops drawing and the
 * screen is cleared for the lease's frames.  EBUSY while another session
 * holds it.
 */
static int
output_claim(
	void *device,
	void *private_session,
	struct gpu_display_claim *request)
{
	struct powervr_display *display;
	struct powervr_display_session *session;

	display = device;
	session = private_session;

	/* Only plane 0 of the one output of this generation. */
	if (request->display_id != POWERVR_DISPLAY_ID)
		return ENOENT;
	if (request->generation != POWERVR_DISPLAY_GENERATION)
		return ESTALE;
	if (request->plane_index != 0U)
		return EINVAL;

	/* Takes the lease when it is free. */
	mutex_lock(&display->mutex);

	if (display->owner != NULL) {
		mutex_unlock(&display->mutex);
		return EBUSY;
	}

	display->owner = session;
	display->lease = display->next_lease;
	display->next_lease++;
	display->sequence = 0U;
	request->lease = display->lease;

	/* The console stops drawing before the screen is cleared for the frames. */
	kern_text_suspend();
	clear_screen(display);

	mutex_unlock(&display->mutex);

	kern_logf("powervr: display lease %llu claimed; the text console is suspended\n",
		  (unsigned long long)request->lease);

	/* Succeeded: the session holds the screen. */
	return 0;
}

/* Ends the lease of its holder and gives the screen back to the text console. */
static int
output_release(
	void *device,
	void *session,
	const struct gpu_display_release *request)
{
	struct powervr_display *display;

	display = device;

	/* Only the holder releases its own lease. */
	mutex_lock(&display->mutex);

	if (display->owner != session || display->lease != request->lease) {
		mutex_unlock(&display->mutex);
		return EINVAL;
	}

	end_lease(display);

	mutex_unlock(&display->mutex);

	/* Succeeded: the console draws the screen again. */
	return 0;
}

/*
 * Shows a frame of the lease holder: the CPU copies it from the session's
 * resource onto the screen, and it is on the screen when this returns.
 * A shared (BLOB) frame needs the scanout import, which is not there yet.
 */
static int
output_present(
	void *device,
	void *session,
	void *private_resource,
	struct gpu_display_present *request)
{
	struct powervr_display *display;
	struct powervr_display_resource *resource;

	display = device;
	resource = private_resource;

	/* A shared frame waits for the render node's sharing. */
	if ((request->flags & GPU_DISPLAY_PRESENT_BLOB) != 0U)
		return ENOTSUP;

	/* Only the session's own resource; the core checked the frame lies inside it. */
	if (resource->owner != session)
		return EINVAL;

	/* Checks the lease and shows the frame under the mutex. */
	mutex_lock(&display->mutex);

	if (display->owner != session || display->lease != request->lease) {
		mutex_unlock(&display->mutex);
		return EBUSY;
	}

	if (request->generation != POWERVR_DISPLAY_GENERATION) {
		mutex_unlock(&display->mutex);
		return ESTALE;
	}

	show_frame(display, resource, request);
	display->sequence++;
	display->present_tick = sched_ticks();
	request->sequence = display->sequence;

	mutex_unlock(&display->mutex);

	/* Succeeded: the frame is on the screen. */
	return 0;
}

/*
 * Reports the newest frame shown under the holder's lease.  A frame is on
 * the screen when its presentation returns, so this never waits.
 */
static int
output_wait(
	void *device,
	void *session,
	struct gpu_display_wait *request)
{
	struct powervr_display *display;

	display = device;

	/* Reads the lease's count under the mutex. */
	mutex_lock(&display->mutex);

	if (display->owner != session || display->lease != request->lease) {
		mutex_unlock(&display->mutex);
		return EBUSY;
	}

	if (request->sequence > display->sequence) {
		mutex_unlock(&display->mutex);
		return EINVAL;
	}

	if (display->sequence == 0U) {
		mutex_unlock(&display->mutex);
		return EAGAIN;
	}

	request->completed_sequence = display->sequence;
	request->present_time_ns = display->present_tick * (KERN_NSEC_PER_SEC / KERN_CLOCK_HZ);
	request->generation = POWERVR_DISPLAY_GENERATION;

	mutex_unlock(&display->mutex);

	/* Succeeded: every frame up to the sequence is on the screen. */
	return 0;
}

/* Describes the node as a display-only device; the render node is not named before it registers. */
static int
output_device(
	void *device,
	void *session,
	struct gpu_device_info *info)
{
	(void)device;
	(void)session;

	/* The core fills the device's identifier. */
	info->roles = GPU_DEVICE_DISPLAY;
	info->companion_id = 0U;
	info->flags = 0U;

	/* Succeeded: the node is a display. */
	return 0;
}

/* Reports the copied route only: shared scanout waits for the render node's sharing. */
static int
output_constraints(
	void *device,
	void *session,
	struct gpu_scanout_constraints *request)
{
	(void)device;
	(void)session;

	/* Only the one output of this generation. */
	if (request->display_id != POWERVR_DISPLAY_ID)
		return ENXIO;
	if (request->generation != POWERVR_DISPLAY_GENERATION)
		return ESTALE;

	/* Copies from ordinary storage, in the two packed formats, rows of whole pixels. */
	request->flags = GPU_SCANOUT_COPY;
	request->formats = GPU_DISPLAY_FORMAT_BGRA8888 | GPU_DISPLAY_FORMAT_RGBA8888;
	request->stride_alignment = POWERVR_DISPLAY_PIXEL_BYTES;
	request->offset_alignment = POWERVR_DISPLAY_PIXEL_BYTES;
	request->placement = 0U;
	request->max_dma_address = 0U;

	/* Succeeded: the constraints of the copied route. */
	return 0;
}

/*
 * Refuses a scanout import: reading a render buffer needs the render
 * node's private lookup of its object, which comes with its sharing.
 */
static int
import_image(
	void *device,
	void *session,
	const struct gpu_image_descriptor *image,
	const struct drv_gpu_scanout_backing *backing,
	void **result)
{
	(void)device;
	(void)session;
	(void)image;
	(void)backing;

	/* Owns nothing on failure. */
	*result = NULL;

	/* Not yet: the borrowed pages alone give no view the node may read. */
	return ENOTSUP;
}

/* Reports whether the copies can write a screen: a known format and room for every row. */
static int
screen_valid(
	const struct kern_boot_framebuffer *screen)
{
	uint64_t needed;

	/* The two four-byte formats. */
	if (screen->format != KERN_BOOT_FRAMEBUFFER_RGBX8888 &&
	    screen->format != KERN_BOOT_FRAMEBUFFER_BGRX8888)
		return 0;

	/* A visible extent, and rows of whole pixels at least as long as the width. */
	if (screen->width == 0U || screen->height == 0U)
		return 0;
	if ((screen->pitch % POWERVR_DISPLAY_PIXEL_BYTES) != 0U)
		return 0;
	if ((uint64_t)screen->pitch < (uint64_t)screen->width * POWERVR_DISPLAY_PIXEL_BYTES)
		return 0;

	/* Memory for every row. */
	needed = (uint64_t)screen->pitch * screen->height;
	if (screen->size < needed)
		return 0;

	/* Succeeded: the screen can be written. */
	return 1;
}

/*
 * Makes the CPU's view of the screen: the direct map when the screen is
 * RAM it holds, as the text console draws it, else an uncached device
 * view of its pages.
 */
static int
map_screen(
	struct powervr_display *display)
{
	uint64_t offset;
	void *view;
	int error;

	/* RAM the direct map holds is written through it and cleaned afterwards. */
	display->view = NULL;
	display->view_bytes = 0U;
	display->pixels = kern_pmem_to_kernel(display->screen.physical_base);
	if (display->pixels != NULL) {
		display->direct = 1;
		return 0;
	}

	/* Anything else through a device view of whole pages. */
	offset = display->screen.physical_base & (POWERVR_DISPLAY_PAGE_BYTES - 1U);
	display->view_bytes = (size_t)((offset + display->screen.size + POWERVR_DISPLAY_PAGE_BYTES - 1U) & ~(uint64_t)(POWERVR_DISPLAY_PAGE_BYTES - 1U));
	error = kern_device_map(display->screen.physical_base - offset, display->view_bytes, KERN_DEVICE_UNCACHED, &view);
	if (error != 0) {
		display->view_bytes = 0U;
		return error;
	}

	display->view = view;
	display->pixels = (uint8_t *)view + offset;
	display->direct = 0;

	/* Succeeded: the CPU can write the screen. */
	return 0;
}

/* Ends the device view of the screen, when there is one. */
static void
unmap_screen(
	struct powervr_display *display)
{
	/* The direct map is not this file's to end. */
	if (display->view != NULL)
		(void)kern_device_unmap(display->view, display->view_bytes);

	display->view = NULL;
	display->view_bytes = 0U;
	display->pixels = NULL;
}

/* Blanks every visible row of the screen.  The caller holds the mutex. */
static void
clear_screen(
	struct powervr_display *display)
{
	uint32_t row;

	/* Each row's visible pixels, top to bottom. */
	for (row = 0U; row < display->screen.height; row++)
		kern_memset(display->pixels + (size_t)row * display->screen.pitch, 0, (size_t)display->screen.width * POWERVR_DISPLAY_PIXEL_BYTES);

	/* The display controller reads memory, not the caches. */
	flush_screen(display, display->screen.height);
}

/* Pushes the top rows of the screen out of the caches when they were written through the direct map. */
static void
flush_screen(
	struct powervr_display *display,
	uint32_t rows)
{
	/* An uncached view wrote memory already. */
	if (!display->direct || rows == 0U)
		return;

	/* The rows written, as one range. */
	kern_dcache_clean_range(display->pixels, (size_t)rows * display->screen.pitch);
}

/*
 * Copies a frame onto the screen, clipped to its top left, swapping red
 * and blue when the screen's order is not the frame's.  The caller holds
 * the mutex; the core checked the frame lies inside the resource.
 */
static void
show_frame(
	struct powervr_display *display,
	const struct powervr_display_resource *resource,
	const struct gpu_display_present *request)
{
	const uint32_t *source;
	uint32_t *destination;
	uint32_t width;
	uint32_t height;
	uint32_t pixel;
	uint32_t row;
	uint32_t column;
	int swap;

	/* The part of the frame the screen has room for. */
	width = request->width;
	if (width > display->screen.width)
		width = display->screen.width;

	height = request->height;
	if (height > display->screen.height)
		height = display->screen.height;

	/* An RGBA frame swaps onto a BGRX screen, a BGRA frame onto an RGBX one. */
	swap = 0;
	if (request->format == GPU_PIXEL_RGBA8888 && !display->rgbx)
		swap = 1;
	if (request->format == GPU_PIXEL_BGRA8888 && display->rgbx)
		swap = 1;

	/* Every row, top to bottom. */
	for (row = 0U; row < height; row++) {
		source = (const uint32_t *)(const void *)(resource->address + request->offset + (uint64_t)row * request->stride);
		destination = (uint32_t *)(void *)(display->pixels + (size_t)row * display->screen.pitch);
		if (!swap) {
			kern_memcpy(destination, source, (size_t)width * POWERVR_DISPLAY_PIXEL_BYTES);
			continue;
		}

		/* Each pixel with its first and third bytes exchanged. */
		for (column = 0U; column < width; column++) {
			pixel = source[column];
			destination[column] = (pixel & 0xff00ff00U) | ((pixel & 0x00ff0000U) >> 16) | ((pixel & 0x000000ffU) << 16);
		}
	}

	/* The display controller reads memory, not the caches. */
	flush_screen(display, height);
}

/*
 * Ends the lease and lets the text console draw its retained text again.
 * The caller holds the mutex.
 */
static void
end_lease(
	struct powervr_display *display)
{
	kern_logf("powervr: display lease %llu released after %llu frame(s); the text console resumes\n",
		  (unsigned long long)display->lease,
		  (unsigned long long)display->sequence);

	/* The screen is free. */
	display->owner = NULL;
	display->lease = 0U;
	display->sequence = 0U;

	/* The console repaints the whole screen from its grid. */
	kern_text_resume();
}
