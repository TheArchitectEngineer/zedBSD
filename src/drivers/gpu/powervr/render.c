/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR render node: sessions with a memory context of their own,
 * storage and blob allocations mapped into it, CPU views of them, and
 * sharing of allocations between opens and with the display node.
 *
 * Every allocation is one physically contiguous run below the core's DMA
 * limit.  The core does not snoop the processor's caches, so the host
 * reaches an allocation only through its uncached alias, in the kernel
 * and in user mappings alike; the core reaches it through the session's
 * page tables, 4 KiB pages only, never asking for a snoop.  Allocations
 * the processor reads also bypass the core's system level cache.
 *
 * The node is published once the firmware runs, after a check of the
 * firmware processor's writes and a short self-test of the queue.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-scanout.h>
#include <drivers/gpu/gpu-share.h>
#include <kern/clock.h>
#include <kern/dcache.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-scanout.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/retry.h"

/*
 * How often, and how far apart, a refused registration is tried again.
 */
#define REGISTER_ATTEMPTS	50U
#define REGISTER_PAUSE_MS	100U

/* How long the self-test waits for an empty job, and for a fence it expects to hold. */
#define SELF_TEST_WAIT_MS	1000U
#define SELF_TEST_HOLD_MS	200U

static int render_open(void *device, void **result);
static void render_close(void *device, void *session);
static int render_info(void *device, void *session, struct gpu_info *info);
static int render_resource_create(void *device, void *session, const struct gpu_resource_create *request, void **result);
static void render_resource_destroy(void *device, void *session, void *resource);
static int render_blob_create(void *device, void *session, const struct gpu_blob_create *request, void **result, uint32_t *identifier);
static int render_blob_create_placed(void *device, void *session, const struct gpu_blob_create_placed *request, void **result, uint32_t *identifier);
static int blob_make(struct powervr_render *render, struct powervr_session *session, const struct gpu_blob_create *request, const struct gpu_placement *placement, void **result, uint32_t *identifier);
static int render_read(void *device, void *session, void *resource, uint64_t offset, void *bytes, uint32_t length);
static int render_write(void *device, void *session, void *resource, uint64_t offset, const void *bytes, uint32_t length);
static int render_map(void *device, void *session, void *resource, struct drv_gpu_mapping *mapping);
static int render_export(void *device, void *session, void *resource, const struct gpu_image_descriptor *image, void **result);
static void render_release(void *device, void *shared);
static int render_import(void *device, void *session, void *shared, void **result, uint32_t *identifier);
static int render_backing(void *device, void *shared, struct drv_gpu_scanout_backing *backing);
static int render_query(void *device, void *session, struct gpu_device_info *info);
static int resource_make(struct powervr_session *session, struct powervr_buffer *buffer, int mappable, int shareable, struct powervr_resource **result);
static void resource_unmake(struct powervr_session *session, struct powervr_resource *resource);
static int check_image(const struct powervr_buffer *buffer, const struct gpu_image_descriptor *image);
static int make_pages(struct powervr_buffer *buffer);
static int same_image(const struct gpu_image_descriptor *first, const struct gpu_image_descriptor *second);
static void buffer_free(struct powervr_buffer *buffer);
static void copy_out(volatile const uint8_t *view, uint8_t *bytes, uint32_t length);
static void copy_in(volatile uint8_t *view, const uint8_t *bytes, uint32_t length);
static int register_attempt(void *context);
static void register_pause(void *context);
static int self_test(struct powervr_render *render, const char **step);
static int wait_timeline(struct powervr_session *session, uint32_t value, unsigned milliseconds);

/*
 * The board's one render node.  Filled by powervr_render_start() on the
 * later stage's thread before the node is published; its fields are then
 * guarded as struct powervr_render says.
 */
static struct powervr_render board_render;

/* The node's sharing operations. */
static const struct drv_gpu_share_ops share_operations = {
	.export_resource = render_export,
	.release = render_release,
	.import_resource = render_import,
	.get_scanout_backing = render_backing,
};

/* The node's role: rendering, with no display of its own. */
static const struct drv_gpu_scanout_ops scanout_operations = {
	.query_device = render_query,
};

/* The node's queue: native commands retired through each session's timeline. */
static const struct drv_gpu_command_ops command_operations = {
	.submit = powervr_render_submit,
	.drain = powervr_render_drain,
};

/* The node's recovery: one session stopped or isolated, or the device faulted and reset. */
static const struct drv_gpu_recovery_ops recovery_operations = {
	.stop_begin = powervr_render_stop_begin,
	.stop_poll = powervr_render_stop_poll,
	.fault = powervr_render_fault,
	.reset = powervr_render_reset,
	.isolate = powervr_render_isolate,
};

/*
 * Publishes the render node over the running firmware.
 */
int
powervr_render_start(
	struct powervr_core *core)
{
	struct powervr_render *render;
	struct powervr_retry retry;
	const char *step;
	unsigned tries;
	int error;

	/* The lock the firmware's buffers and heap are shared under from here. */
	render = &board_render;
	error = mutex_init(&core->lock, LOCK_RANK_DEVICE, "powervr-core");
	if (error != 0)
		return error;

	/* Checks what the processor sees of the firmware processor's writes; it only reports. */
	powervr_coherency_check(core);

	/* Sets up the node's state and its operation table. */
	kern_memset(render, 0, sizeof(*render));
	render->core = core;
	render->next_identity = 1;
	spin_init(&render->queue_lock, LOCK_RANK_DEVICE, "powervr-queue");
	spin_init(&render->buffer_guard, LOCK_RANK_DEVICE, "powervr-buffers");
	waitq_init(&render->service, "powervr-service");
	waitq_init(&render->retired, "powervr-retired");
	render->ops.version = DRV_GPU_INTERFACE_VERSION;
	render->ops.size = sizeof(render->ops);
	render->ops.capabilities = GPU_CAP_RESOURCE | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_MAPPING | GPU_CAP_SHARE | GPU_CAP_NOTIFICATION;
	render->ops.open = render_open;
	render->ops.close = render_close;
	render->ops.get_info = render_info;
	render->ops.resource_create = render_resource_create;
	render->ops.resource_destroy = render_resource_destroy;
	render->ops.blob_create = render_blob_create;
	render->ops.blob_create_placed = render_blob_create_placed;
	render->ops.resource_read = render_read;
	render->ops.resource_write = render_write;
	render->ops.resource_map = render_map;
	render->ops.share = &share_operations;
	render->ops.scanout = &scanout_operations;
	render->ops.commands = &command_operations;
	render->ops.recovery = &recovery_operations;
	render->running = 1;

	/* Lets the interrupt handler wake the service from here on. */
	core->render = render;

	/* Starts the service that retires commands and reads the firmware's notices. */
	error = powervr_service_start(render);
	if (error != 0) {
		core->render = NULL;
		return error;
	}

	/* Runs one empty job through a session of the driver's own, with its fence and memory. */
	step = "open";
	error = self_test(render, &step);
	if (error != 0) {
		kern_logf("powervr: render self-test %s failed (%d)\n", step, error);
		powervr_render_quarantine(render, error);
		powervr_service_stop(render);
		core->render = NULL;
		return error;
	}

	/* Registers the node, trying again a bounded number of times while registration is busy. */
	retry.attempt = register_attempt;
	retry.pause = register_pause;
	retry.context = render;
	error = powervr_retry_busy(&retry, REGISTER_ATTEMPTS, &tries);
	if (error != 0) {
		kern_logf("powervr: render node not registered after %u tries (%d)\n", tries, error);
		powervr_render_quarantine(render, error);
		powervr_service_stop(render);
		core->render = NULL;
		return error;
	}

	/* Succeeded: the node is published. */
	kern_logf("powervr: render node ready (self-test ok: empty job, fence, memory)\n");
	return 0;
}

/*
 * Finds the render buffer exported with this image whose backing is this
 * one, and retains it for the caller.
 */
int
powervr_render_lookup(
	const struct gpu_image_descriptor *image,
	const struct drv_gpu_scanout_backing *backing,
	struct powervr_buffer **result)
{
	struct powervr_render *render;
	struct powervr_shared *shared;
	unsigned long enabled;
	int matches;

	/* Only the render node's own page lists name its buffers. */
	render = &board_render;
	*result = NULL;
	if (backing == NULL || image == NULL)
		return ENOTSUP;
	if (backing->page_bytes != POWERVR_GPU_PAGE_BYTES || backing->flags != DRV_GPU_BACKING_CONTIGUOUS)
		return ENOTSUP;

	/* Walks the live exports for the one whose page list this is. */
	enabled = spin_lock_irqsave(&render->buffer_guard);

	for (shared = render->exports; shared != NULL; shared = shared->next) {
		if (!shared->has_image || shared->buffer->pages != backing->pages)
			continue;
		if (backing->page_count != shared->buffer->page_count || backing->bytes != shared->buffer->bytes)
			continue;

		/* The image must be the one exported, whatever the core added. */
		matches = same_image(image, &shared->image);
		if (!matches)
			continue;

		/* Takes a reference that outlives the export. */
		shared->buffer->references++;
		*result = shared->buffer;
		break;
	}

	spin_unlock_irqrestore(&render->buffer_guard, enabled);

	/* No live export of the render node matches. */
	if (*result == NULL)
		return ENOTSUP;

	/* Succeeded: the caller holds the buffer. */
	return 0;
}

/*
 * Reports a retained buffer's uncached kernel view.
 */
const volatile uint8_t *
powervr_buffer_view(
	const struct powervr_buffer *buffer)
{
	/* Succeeded: the view the buffer was made with. */
	return buffer->view;
}

/*
 * Reports a retained buffer's size in bytes, as allocated.
 */
uint64_t
powervr_buffer_bytes(
	const struct powervr_buffer *buffer)
{
	/* Succeeded: the size asked for at creation. */
	return buffer->bytes;
}

/*
 * Releases a reference taken by powervr_render_lookup().
 */
void
powervr_buffer_release(
	struct powervr_buffer *buffer)
{
	/* Drops it the way the node's own holders do. */
	powervr_buffer_put(&board_render, buffer);
}

/*
 * Makes a zeroed buffer of at least bytes bytes.
 */
int
powervr_buffer_create(
	struct powervr_render *render,
	uint64_t bytes,
	struct powervr_buffer **result)
{
	struct powervr_buffer *buffer;
	volatile uint64_t *words;
	uint64_t rounded;
	uint64_t index;
	void *view;
	int error;

	/* Refuses an empty buffer and one over the limit, and rounds the rest to whole pages. */
	*result = NULL;
	if (bytes == 0U || bytes > POWERVR_RENDER_MAX_BYTES)
		return EINVAL;
	rounded = (bytes + POWERVR_GPU_PAGE_BYTES - 1U) & ~(uint64_t)(POWERVR_GPU_PAGE_BYTES - 1U);

	/* The buffer's bookkeeping. */
	buffer = kern_calloc(1, sizeof(*buffer));
	if (buffer == NULL)
		return ENOMEM;

	/* One contiguous run the core can name. */
	error = kern_pmem_alloc_limited((size_t)rounded, POWERVR_GPU_PAGE_BYTES, render->core->description.dma_limit, 0, &buffer->run);
	if (error != 0) {
		kern_free(buffer);
		return error;
	}

	/* Its uncached alias; the run's cached lines are pushed out first. */
	error = kern_pmem_map_uncached(&buffer->run, &view);
	if (error != 0) {
		(void)kern_pmem_free(&buffer->run);
		kern_free(buffer);
		return error;
	}

	/* Clears it through the alias, so neither the core nor a reader sees old bytes. */
	buffer->view = view;
	words = (volatile uint64_t *)buffer->view;
	for (index = 0; index < rounded / 8U; index++)
		words[index] = 0;

	/* Succeeded: one reference, the caller's. */
	buffer->bytes = bytes;
	buffer->references = 1;
	*result = buffer;
	return 0;
}

/*
 * Takes another reference on a buffer.
 */
void
powervr_buffer_retain(
	struct powervr_render *render,
	struct powervr_buffer *buffer)
{
	unsigned long enabled;

	/* Counts the new holder. */
	enabled = spin_lock_irqsave(&render->buffer_guard);

	buffer->references++;

	spin_unlock_irqrestore(&render->buffer_guard, enabled);
}

/*
 * Drops a reference; the last one frees the buffer, or keeps it after a
 * fault, since the core may still reach it.
 */
void
powervr_buffer_put(
	struct powervr_render *render,
	struct powervr_buffer *buffer)
{
	unsigned long enabled;
	unsigned left;
	unsigned faulted;

	/* Counts the holder off, and reads whether the device is in quarantine. */
	enabled = spin_lock_irqsave(&render->buffer_guard);

	buffer->references--;
	left = buffer->references;

	spin_unlock_irqrestore(&render->buffer_guard, enabled);

	/* Other holders remain. */
	if (left != 0U)
		return;

	/* After a fault the core may still reach the run: keep it until the reset. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	faulted = render->faulted;
	if (faulted) {
		buffer->next = render->kept_buffers;
		render->kept_buffers = buffer;
	}

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* Kept for the reset. */
	if (faulted)
		return;

	/* Frees the run, its view and its page list. */
	buffer_free(buffer);
}

/*
 * Frees a list of buffers kept for a reset, linked through next.
 */
void
powervr_buffers_free(
	struct powervr_buffer *list)
{
	struct powervr_buffer *buffer;

	/* Each buffer of the list. */
	while (list != NULL) {
		buffer = list;
		list = buffer->next;
		buffer_free(buffer);
	}
}

/*
 * Frees what the render node kept for a reset once the core is stopped
 * for good.
 */
void
powervr_render_abandon(
	struct powervr_core *core)
{
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_buffer *list;
	unsigned long enabled;

	/* Only the board's node, and only when it was set up. */
	render = &board_render;
	if (render->core != core)
		return;

	/* The kept sessions: their records' heap is gone with the firmware. */
	while (render->kept_sessions != NULL) {
		session = render->kept_sessions;
		render->kept_sessions = session->next;
		powervr_session_firmware_discard(session);
		kern_free(session);
	}

	/* The kept buffers. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	list = render->kept_buffers;
	render->kept_buffers = NULL;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	powervr_buffers_free(list);
}

/* Opens a session with a memory context and a compute context of its own. */
static int
render_open(
	void *device,
	void **result)
{
	struct powervr_render *render;
	struct powervr_session *session;
	unsigned long enabled;
	unsigned faulted;
	int error;

	/* Refuses a new session while the device is in quarantine. */
	render = device;
	*result = NULL;
	enabled = spin_lock_irqsave(&render->queue_lock);

	faulted = render->faulted;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	if (faulted)
		return EIO;

	/* The session's bookkeeping. */
	session = kern_calloc(1, sizeof(*session));
	if (session == NULL)
		return ENOMEM;
	session->render = render;
	powervr_gpu_space_init(&session->space, POWERVR_GENERAL_HEAP_BASE, POWERVR_GENERAL_HEAP_BYTES);

	/* Makes its tables and firmware records, and lists it. */
	mutex_lock(&render->core->lock);

	session->identity = render->next_identity++;
	error = powervr_session_firmware_create(render, session);
	if (error == 0) {
		session->next = render->sessions;
		render->sessions = session;
	}

	mutex_unlock(&render->core->lock);

	/* The records could not be made; they were undone. */
	if (error != 0) {
		kern_free(session);
		return error;
	}

	/* Succeeded: the session is the core's. */
	*result = session;
	return 0;
}

/*
 * Closes a session.  The core has drained its callbacks and destroyed its
 * resources.  The firmware cleans the context up first; a session the
 * device may still reach is kept for the reset instead of freed.
 */
static void
render_close(
	void *device,
	void *opaque)
{
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_session **position;
	unsigned long enabled;
	unsigned keep;
	int error;

	/* Unlists the session. */
	render = device;
	session = opaque;
	mutex_lock(&render->core->lock);

	position = &render->sessions;
	while (*position != NULL && *position != session)
		position = &(*position)->next;
	if (*position != NULL)
		*position = session->next;

	mutex_unlock(&render->core->lock);

	/* Reads whether the session or the device may still be reached by the core. */
	enabled = spin_lock_irqsave(&render->queue_lock);

	keep = 0;
	if (render->faulted || session->isolated)
		keep = 1;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* A context that ran is cleaned up by the firmware before its records go. */
	if (!keep && session->kicked) {
		error = powervr_session_cleanup(render, session);
		if (error != 0)
			keep = 1;
	}

	/* Kept for the reset: nothing of it is touched again until then. */
	if (keep) {
		mutex_lock(&render->core->lock);

		session->retained = 1;
		session->next = render->kept_sessions;
		render->kept_sessions = session;

		mutex_unlock(&render->core->lock);
		return;
	}

	/* Returns its tables and records, then the bookkeeping. */
	powervr_session_firmware_destroy(render, session);
	kern_free(session);
}

/* Reports the node's capabilities and limits. */
static int
render_info(
	void *device,
	void *session,
	struct gpu_info *info)
{
	struct powervr_render *render;

	/* The node's table holds the capabilities. */
	render = device;
	(void)session;
	info->capabilities = render->ops.capabilities;
	info->max_resources = POWERVR_RENDER_RESOURCES;
	info->max_resource_bytes = POWERVR_RENDER_MAX_BYTES;
	kern_memcpy(info->driver_name, "powervr", sizeof("powervr"));

	/* Succeeded: the snapshot is filled. */
	return 0;
}

/* Makes one storage allocation of the session, mapped and host visible. */
static int
render_resource_create(
	void *device,
	void *opaque,
	const struct gpu_resource_create *request,
	void **result)
{
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_buffer *buffer;
	struct powervr_resource *resource;
	int error;

	/* Only storage, with no flags. */
	render = device;
	session = opaque;
	*result = NULL;
	if (request->usage != GPU_RESOURCE_USAGE_STORAGE || request->flags != 0U)
		return EINVAL;

	/* The allocation. */
	error = powervr_buffer_create(render, request->bytes, &buffer);
	if (error != 0)
		return error;

	/* The session's handle on it, mapped; the handle takes the creation reference. */
	error = resource_make(session, buffer, 1, 0, &resource);
	powervr_buffer_put(render, buffer);
	if (error != 0)
		return error;

	/* Succeeded: the resource is the core's. */
	*result = resource;
	return 0;
}

/* Destroys one resource of the session. */
static void
render_resource_destroy(
	void *device,
	void *opaque,
	void *object)
{
	struct powervr_session *session;
	struct powervr_resource *resource;

	/* Unmaps it, unless the core may still reach it, and drops its buffer. */
	(void)device;
	session = opaque;
	resource = object;
	resource_unmake(session, resource);
}

/* Makes one blob of the session; a mappable blob is host visible and uncached. */
static int
render_blob_create(
	void *device,
	void *opaque,
	const struct gpu_blob_create *request,
	void **result,
	uint32_t *identifier)
{
	/* No placement condition. */
	return blob_make(device, opaque, request, NULL, result, identifier);
}

/*
 * Makes one blob that meets a placement.  Every buffer is one contiguous
 * run below the core's DMA limit, so the run is only checked.
 */
static int
render_blob_create_placed(
	void *device,
	void *opaque,
	const struct gpu_blob_create_placed *request,
	void **result,
	uint32_t *identifier)
{
	/* The blob part carries the placed request's size; the rest of it is the plain request. */
	struct gpu_blob_create blob;

	blob = request->blob;
	blob.size = sizeof(blob);
	return blob_make(device, opaque, &blob, &request->placement, result, identifier);
}

/* Makes one blob, checking its run against a placement when one is given. */
static int
blob_make(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct gpu_blob_create *request,
	const struct gpu_placement *placement,
	void **result,
	uint32_t *identifier)
{
	struct powervr_buffer *buffer;
	struct powervr_resource *resource;
	uint64_t limit;
	int mappable;
	int shareable;
	int error;

	/* Native blobs only, with the defined flags; a cross-device blob must be shareable. */
	*result = NULL;
	*identifier = 0;
	if (request->blob_id != 0U)
		return EINVAL;
	if ((request->flags & ~(GPU_BLOB_MAPPABLE | GPU_BLOB_SHAREABLE | GPU_BLOB_CROSS_DEVICE)) != 0U)
		return EINVAL;
	if ((request->flags & GPU_BLOB_CROSS_DEVICE) != 0U && (request->flags & GPU_BLOB_SHAREABLE) == 0U)
		return EINVAL;

	/* A coherent placement is not offered: the buffers are only reached through uncached views. */
	limit = 0;
	if (placement != NULL) {
		if ((placement->flags & GPU_PLACEMENT_COHERENT) != 0U)
			return ENOTSUP;
		limit = placement->max_dma_address;
		if ((placement->flags & GPU_PLACEMENT_DMA32) != 0U && (limit == 0U || limit > 0xffffffffULL))
			limit = 0xffffffffULL;
	}

	/* The allocation. */
	error = powervr_buffer_create(render, request->bytes, &buffer);
	if (error != 0)
		return error;

	/* The run must meet the placement; the buffer goes if it does not. */
	if (placement != NULL) {
		error = powervr_gpu_run_fits(buffer->run.paddr, buffer->run.size, limit, placement->alignment);
		if (error != 0) {
			powervr_buffer_put(render, buffer);
			return error;
		}
	}

	/* The session's handle on it, mapped; the handle takes the creation reference. */
	mappable = 0;
	if ((request->flags & GPU_BLOB_MAPPABLE) != 0U)
		mappable = 1;
	shareable = 0;
	if ((request->flags & GPU_BLOB_SHAREABLE) != 0U)
		shareable = 1;
	error = resource_make(session, buffer, mappable, shareable, &resource);
	powervr_buffer_put(render, buffer);
	if (error != 0)
		return error;

	/* Succeeded: the blob, named for the renderer by the session's next number. */
	*result = resource;
	*identifier = ++session->next_resource;
	return 0;
}

/* Copies bytes out of a resource. */
static int
render_read(
	void *device,
	void *opaque,
	void *object,
	uint64_t offset,
	void *bytes,
	uint32_t length)
{
	struct powervr_resource *resource;
	uint64_t size;

	/* Refuses a range outside the allocation. */
	(void)device;
	(void)opaque;
	resource = object;
	size = resource->buffer->bytes;
	if (offset > size || length > size - offset)
		return EINVAL;

	/* Reads through the uncached view. */
	copy_out(resource->buffer->view + offset, bytes, length);

	/* Succeeded: the bytes are copied. */
	return 0;
}

/* Copies bytes into a resource. */
static int
render_write(
	void *device,
	void *opaque,
	void *object,
	uint64_t offset,
	const void *bytes,
	uint32_t length)
{
	struct powervr_resource *resource;
	uint64_t size;

	/* Refuses a range outside the allocation. */
	(void)device;
	(void)opaque;
	resource = object;
	size = resource->buffer->bytes;
	if (offset > size || length > size - offset)
		return EINVAL;

	/* Writes through the uncached view; the bytes are in memory when it returns. */
	copy_in(resource->buffer->view + offset, bytes, length);

	/* Succeeded: the bytes are copied. */
	return 0;
}

/* Describes the CPU view of a host-visible resource: its uncached alias. */
static int
render_map(
	void *device,
	void *opaque,
	void *object,
	struct drv_gpu_mapping *mapping)
{
	struct powervr_resource *resource;

	/* Only a host-visible resource of this session has a view. */
	(void)device;
	resource = object;
	if (resource->session != opaque || !resource->mappable)
		return ENOTSUP;

	/* The whole run, non-cacheable RAM: the user mapping keeps the kernel's policy. */
	mapping->physical = resource->buffer->run.paddr;
	mapping->address = (void *)resource->buffer->view;
	mapping->bytes = resource->buffer->run.size;
	mapping->attributes = DRV_GPU_MAPPING_UNCACHED_RAM;

	/* Succeeded: the core maps the run with the same attributes. */
	return 0;
}

/* Exports a resource's buffer, as an image or as an allocation only. */
static int
render_export(
	void *device,
	void *opaque,
	void *object,
	const struct gpu_image_descriptor *image,
	void **result)
{
	struct powervr_render *render;
	struct powervr_resource *resource;
	struct powervr_shared *shared;
	unsigned long enabled;
	int error;

	/* Only a resource of this session. */
	render = device;
	resource = object;
	*result = NULL;
	if (resource->session != opaque)
		return EINVAL;

	/* An image must fit the buffer, and needs the buffer's page list. */
	if (image != NULL) {
		error = check_image(resource->buffer, image);
		if (error != 0)
			return error;
		mutex_lock(&render->core->lock);

		error = make_pages(resource->buffer);

		mutex_unlock(&render->core->lock);
		if (error != 0)
			return error;
	}

	/* The export's bookkeeping. */
	shared = kern_calloc(1, sizeof(*shared));
	if (shared == NULL)
		return ENOMEM;
	shared->buffer = resource->buffer;
	if (image != NULL) {
		shared->image = *image;
		shared->has_image = 1;
	}

	/* Holds the buffer, and lists the export where a lookup finds it. */
	enabled = spin_lock_irqsave(&render->buffer_guard);

	resource->buffer->references++;
	shared->next = render->exports;
	render->exports = shared;

	spin_unlock_irqrestore(&render->buffer_guard, enabled);

	/* Succeeded: the export lives independently of the session. */
	*result = shared;
	return 0;
}

/* Ends an export; a lookup's own reference keeps the buffer if it has one. */
static void
render_release(
	void *device,
	void *object)
{
	struct powervr_render *render;
	struct powervr_shared *shared;
	struct powervr_shared **position;
	unsigned long enabled;

	/* Unlists the export. */
	render = device;
	shared = object;
	enabled = spin_lock_irqsave(&render->buffer_guard);

	position = &render->exports;
	while (*position != NULL && *position != shared)
		position = &(*position)->next;
	if (*position != NULL)
		*position = shared->next;

	spin_unlock_irqrestore(&render->buffer_guard, enabled);

	/* Drops the export's hold on the buffer. */
	powervr_buffer_put(render, shared->buffer);
	kern_free(shared);
}

/* Imports an export of this node into a session as a resource of its own. */
static int
render_import(
	void *device,
	void *opaque,
	void *object,
	void **result,
	uint32_t *identifier)
{
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_shared *shared;
	struct powervr_resource *resource;
	int error;

	/* The export the core kept alive for this call. */
	render = device;
	session = opaque;
	shared = object;
	*result = NULL;
	*identifier = 0;

	/* A new handle, mapped into this session, holding the buffer on its own. */
	powervr_buffer_retain(render, shared->buffer);
	error = resource_make(session, shared->buffer, 1, 1, &resource);
	powervr_buffer_put(render, shared->buffer);
	if (error != 0)
		return error;

	/* Succeeded: the resource is the session's, under the session's next number. */
	*result = resource;
	*identifier = ++session->next_resource;
	return 0;
}

/* Describes an image export's pages for a display; allocation-only exports have none. */
static int
render_backing(
	void *device,
	void *object,
	struct drv_gpu_scanout_backing *backing)
{
	struct powervr_shared *shared;

	/* Only an image export has a layout to show. */
	(void)device;
	shared = object;
	if (!shared->has_image)
		return ENOTSUP;

	/* The run's pages, contiguous, in RAM; the page list is the lookup's key. */
	backing->pages = shared->buffer->pages;
	backing->page_count = shared->buffer->page_count;
	backing->bytes = shared->buffer->bytes;
	backing->page_bytes = POWERVR_GPU_PAGE_BYTES;
	backing->flags = DRV_GPU_BACKING_CONTIGUOUS;

	/* Succeeded: the view stays valid while the export lives. */
	return 0;
}

/* Reports the node's role: rendering only. */
static int
render_query(
	void *device,
	void *session,
	struct gpu_device_info *info)
{
	/* The core fills the identity; the node has no companion to name. */
	(void)device;
	(void)session;
	info->roles = GPU_DEVICE_RENDER;
	info->companion_id = 0;

	/* Succeeded: the role is filled. */
	return 0;
}

/*
 * Makes a session's handle on a buffer and maps it at a device address of
 * the session's own; the handle takes a reference of its own.
 */
static int
resource_make(
	struct powervr_session *session,
	struct powervr_buffer *buffer,
	int mappable,
	int shareable,
	struct powervr_resource **result)
{
	struct powervr_render *render;
	struct powervr_resource *resource;
	unsigned flags;
	int error;

	/* Refuses one more resource than a session may hold. */
	render = session->render;
	*result = NULL;
	if (session->resources >= POWERVR_RENDER_RESOURCES)
		return ENOSPC;

	/* The handle. */
	resource = kern_calloc(1, sizeof(*resource));
	if (resource == NULL)
		return ENOMEM;
	resource->session = session;
	resource->buffer = buffer;
	resource->mappable = mappable;

	/*
	 * Host-visible and shareable memory goes around the core's system level
	 * cache, so the processor and the display read what the core wrote.
	 */
	flags = powervr_gpu_buffer_flags(mappable, shareable);

	/* Takes a device address and maps the run there, then has the core drop stale table entries. */
	mutex_lock(&render->core->lock);

	error = powervr_gpu_space_take(&session->space, buffer->run.size, &resource->address);
	if (error == 0) {
		error = powervr_gpu_mmu_map(&session->mmu, resource->address, buffer->run.paddr, buffer->run.size, flags);
		if (error != 0)
			(void)powervr_gpu_space_give(&session->space, resource->address);
	}
	if (error == 0)
		error = powervr_command_mmu_flush(render->core);

	mutex_unlock(&render->core->lock);

	/* Nothing is mapped; the handle goes. */
	if (error != 0) {
		kern_free(resource);
		return error;
	}

	/* Succeeded: the handle holds the buffer. */
	powervr_buffer_retain(render, buffer);
	session->resources++;
	*result = resource;
	return 0;
}

/*
 * Undoes a handle: unmaps it and drops its buffer, unless the core may
 * still reach the session, when the mapping and the buffer both stay.
 */
static void
resource_unmake(
	struct powervr_session *session,
	struct powervr_resource *resource)
{
	struct powervr_render *render;
	unsigned long enabled;
	unsigned keep;

	/* Reads whether the device or the session is in quarantine. */
	render = session->render;
	enabled = spin_lock_irqsave(&render->queue_lock);

	keep = 0;
	if (render->faulted || session->isolated)
		keep = 1;

	spin_unlock_irqrestore(&render->queue_lock, enabled);

	/* Unmaps it and has the core drop what it cached of the entries. */
	if (!keep) {
		mutex_lock(&render->core->lock);

		powervr_gpu_mmu_unmap(&session->mmu, resource->address, resource->buffer->run.size);
		(void)powervr_gpu_space_give(&session->space, resource->address);
		(void)powervr_command_mmu_flush(render->core);

		mutex_unlock(&render->core->lock);
	}

	/* Drops the handle's hold; after a fault the buffer is kept for the reset. */
	powervr_buffer_put(render, resource->buffer);
	session->resources--;
	kern_free(resource);
}

/* Checks a linear four-byte image fits the buffer and the core can reach it. */
static int
check_image(
	const struct powervr_buffer *buffer,
	const struct gpu_image_descriptor *image)
{
	uint64_t bytes;

	/* Only the two four-byte linear colour layouts, of a non-empty size. */
	if (image->width == 0U || image->height == 0U || image->width > UINT32_MAX / 4U)
		return EINVAL;
	if (image->format != GPU_PIXEL_BGRA8888 && image->format != GPU_PIXEL_RGBA8888)
		return ENOTSUP;
	if (image->tiling != GPU_IMAGE_LINEAR || image->allocation_bytes != buffer->bytes)
		return ENOTSUP;

	/* Rows of whole pixels, and every row inside the allocation. */
	if (image->stride < image->width * 4U || (image->stride & 3U) != 0U)
		return EINVAL;
	if ((image->offset & 3U) != 0U || image->offset > buffer->bytes)
		return EINVAL;
	bytes = (uint64_t)image->stride * image->height;
	if (bytes > buffer->bytes - image->offset)
		return EINVAL;

	/* Succeeded: the image lies inside the buffer. */
	return 0;
}

/* Makes the buffer's page list once; the core's lock is held. */
static int
make_pages(
	struct powervr_buffer *buffer)
{
	uint64_t *pages;
	uint64_t count;
	uint64_t index;

	/* A list made earlier is kept. */
	if (buffer->pages != NULL)
		return 0;

	/* One physical address per page of the run. */
	count = buffer->run.size / POWERVR_GPU_PAGE_BYTES;
	pages = kern_calloc((size_t)count, sizeof(*pages));
	if (pages == NULL)
		return ENOMEM;
	for (index = 0; index < count; index++)
		pages[index] = buffer->run.paddr + index * POWERVR_GPU_PAGE_BYTES;

	/* Succeeded: the list lives as long as the buffer. */
	buffer->pages = pages;
	buffer->page_count = count;
	return 0;
}

/* Reports whether two image descriptions agree on every field the export fixed. */
static int
same_image(
	const struct gpu_image_descriptor *first,
	const struct gpu_image_descriptor *second)
{
	/* The size and format. */
	if (first->width != second->width || first->height != second->height)
		return 0;
	if (first->format != second->format || first->stride != second->stride)
		return 0;

	/* The placement in the allocation. */
	if (first->offset != second->offset || first->allocation_bytes != second->allocation_bytes)
		return 0;

	/* The memory type, the use and the layout. */
	if (first->memory_type != second->memory_type || first->usage != second->usage)
		return 0;
	if (first->tiling != second->tiling || first->reserved != second->reserved)
		return 0;

	/* Succeeded: the same image. */
	return 1;
}

/* Frees a buffer's run, view, page list and bookkeeping. */
static void
buffer_free(
	struct powervr_buffer *buffer)
{
	/* Ends the alias, which drops the run's lines, then returns the run. */
	(void)kern_pmem_unmap_uncached((void *)buffer->view, buffer->run.size);
	(void)kern_pmem_free(&buffer->run);

	/* The page list and the bookkeeping. */
	if (buffer->pages != NULL)
		kern_free(buffer->pages);
	kern_free(buffer);
}

/*
 * Copies out of an uncached view: whole aligned words while both sides
 * allow it, bytes otherwise, so the view never takes an access wider
 * than its alignment.
 */
static void
copy_out(
	volatile const uint8_t *view,
	uint8_t *bytes,
	uint32_t length)
{
	uint32_t index;

	/* Words while both addresses are aligned to eight. */
	index = 0;
	if ((((uintptr_t)view | (uintptr_t)bytes) & 7U) == 0U) {
		for (; index + 8U <= length; index += 8U)
			*(uint64_t *)(bytes + index) = *(volatile const uint64_t *)(view + index);
	}

	/* Bytes for the rest. */
	for (; index < length; index++)
		bytes[index] = view[index];
}

/* Copies into an uncached view, the same way. */
static void
copy_in(
	volatile uint8_t *view,
	const uint8_t *bytes,
	uint32_t length)
{
	uint32_t index;

	/* Words while both addresses are aligned to eight. */
	index = 0;
	if ((((uintptr_t)view | (uintptr_t)bytes) & 7U) == 0U) {
		for (; index + 8U <= length; index += 8U)
			*(volatile uint64_t *)(view + index) = *(const uint64_t *)(bytes + index);
	}

	/* Bytes for the rest. */
	for (; index < length; index++)
		view[index] = bytes[index];
}

/* One registration attempt of the node. */
static int
register_attempt(
	void *context)
{
	struct powervr_render *render;
	int error;

	/* Registers the node with its table and state. */
	render = context;
	error = drv_gpu_register(&render->ops, render, &render->device);
	if (error != 0)
		return error;

	/* Succeeded: the node is published. */
	return 0;
}

/* The pause between registration attempts. */
static void
register_pause(
	void *context)
{
	uint64_t ticks;

	/* Sleeps the pause, rounded up to whole ticks. */
	(void)context;
	ticks = ((uint64_t)REGISTER_PAUSE_MS * KERN_CLOCK_HZ + 999U) / 1000U;
	sched_sleep(sched_ticks() + ticks);
}

/*
 * Runs the queue once through a session of the driver's own before the
 * node is published: a blob mapped into the session, written and read
 * back, an empty job whose fence retires, and an empty job held by a
 * fence on a word the processor wrote cached and did not clean.
 *
 * The last one tells whether the firmware's processor reads the
 * processor's cached writes; it is reported, not acted on.
 */
static int
self_test(
	struct powervr_render *render,
	const char **step)
{
	struct powervr_session *session;
	struct powervr_resource *resource;
	struct gpu_blob_create blob;
	uint8_t pattern[64];
	uint8_t back[64];
	struct powervr_object probe;
	volatile uint32_t *word;
	const char *reverse;
	uint32_t address;
	uint32_t index;
	void *opaque;
	void *object;
	uint32_t identifier;
	int error;
	int seen;

	/* Opens a session of the driver's own. */
	*step = "open";
	error = render_open(render, &opaque);
	if (error != 0)
		return error;
	session = opaque;

	/* A mappable blob of one page. */
	*step = "blob";
	kern_memset(&blob, 0, sizeof(blob));
	blob.bytes = POWERVR_GPU_PAGE_BYTES;
	blob.flags = GPU_BLOB_MAPPABLE;
	error = render_blob_create(render, session, &blob, &object, &identifier);
	if (error != 0)
		goto close;
	resource = object;

	/* Writes a pattern and reads it back through the transfer operations. */
	*step = "memory";
	for (index = 0; index < sizeof(pattern); index++)
		pattern[index] = (uint8_t)(index * 7U + 1U);
	error = render_write(render, session, resource, 64, pattern, sizeof(pattern));
	if (error == 0)
		error = render_read(render, session, resource, 64, back, sizeof(back));
	if (error == 0 && kern_memcmp(pattern, back, sizeof(pattern)) != 0)
		error = EIO;
	if (error != 0)
		goto destroy;

	/* An empty job, whose fence must retire. */
	*step = "empty job";
	mutex_lock(&render->core->lock);

	session->submitted++;
	error = powervr_session_kick(render, session, session->submitted, 0, 0);

	mutex_unlock(&render->core->lock);
	if (error == 0)
		error = wait_timeline(session, session->submitted, SELF_TEST_WAIT_MS);
	if (error != 0)
		goto destroy;

	/*
	 * An empty job held by a fence on a word of a block of its own, which
	 * the processor writes through its cached view without cleaning.  The
	 * block's line holds nothing else, so cleaning it later loses nothing.
	 */
	*step = "fence";
	mutex_lock(&render->core->lock);

	error = powervr_object_create(&render->core->firmware.heap, POWERVR_GPU_PAGE_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &probe);

	mutex_unlock(&render->core->lock);
	if (error != 0)
		goto destroy;
	address = powervr_object_address(&probe, 0);
	word = kern_pmem_to_kernel(powervr_object_physical(&probe, 0));
	*word = 0x600dU;
	mutex_lock(&render->core->lock);

	session->submitted++;
	error = powervr_session_kick(render, session, session->submitted, address, 0x600dU);

	mutex_unlock(&render->core->lock);

	/* Whether the firmware saw the uncleaned write; if not, cleaning the line must let the job go. */
	seen = 1;
	if (error == 0)
		error = wait_timeline(session, session->submitted, SELF_TEST_HOLD_MS);
	if (error == ETIMEDOUT) {
		seen = 0;
		kern_dcache_clean_range((const void *)word, sizeof(*word));
		mutex_lock(&render->core->lock);

		error = powervr_session_rekick(render, session);

		mutex_unlock(&render->core->lock);
		if (error == 0)
			error = wait_timeline(session, session->submitted, SELF_TEST_WAIT_MS);
	}

	/*
	 * Drops the probe's line.  A job that never ran may still wait on the
	 * probe, so on failure the page stays with the firmware, which is then
	 * stopped and powered down and never reaches it again.
	 */
	kern_dcache_invalidate_range((const void *)word, sizeof(*word));
	if (error != 0)
		goto destroy;
	mutex_lock(&render->core->lock);

	powervr_object_destroy(&render->core->firmware.heap, &probe);

	mutex_unlock(&render->core->lock);

	/* Reports the reverse direction; the sharing stays as it is. */
	if (seen) {
		reverse = "seen without a clean";
	} else {
		reverse = "seen after a clean only";
	}
	kern_logf("powervr: coherency (firmware reads of cached writes): %s\n", reverse);

destroy:
	/* Destroys the blob. */
	render_resource_destroy(render, session, resource);

close:
	/* Closes the session; a failure above leaves the error to report. */
	render_close(render, session);
	if (error != 0)
		return error;

	/* Succeeded: the queue, the fence and the memory work. */
	return 0;
}

/* Waits, bounded, for a session's timeline to reach a value. */
static int
wait_timeline(
	struct powervr_session *session,
	uint32_t value,
	unsigned milliseconds)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t current;

	/* Reads the timeline once a tick until it reaches the value or the bound is spent. */
	deadline = sched_ticks() + ((uint64_t)milliseconds * KERN_CLOCK_HZ + 999U) / 1000U;
	for (;;) {
		current = powervr_session_timeline(session);
		if ((int32_t)(current - value) >= 0)
			break;

		/* The bound is spent. */
		now = sched_ticks();
		if (now >= deadline)
			return ETIMEDOUT;

		/* Sleeps one tick. */
		sched_sleep(now + 1U);
	}

	/* Succeeded: the timeline reached the value. */
	return 0;
}
