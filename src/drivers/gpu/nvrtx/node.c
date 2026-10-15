/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU device of the nvrtx driver: sessions and resources (stage P8c).
 *
 * By default (unless nvrtx.resident=0) with a head taken over (P8b), the
 * worker leaves the GSP up and registers the GPU with the GPU core.  The
 * device has ordinary storage (RAM the CPU reads and writes), blobs in the
 * VRAM (buffer objects of P4m's allocator, mapped in the CPU's window,
 * shared between opens by share.c), transfers and CPU mappings of both, and
 * the display of present.c and cursor.c.  With nvrtx.vk=1 it is also a
 * renderer (runtime.c: the Vulkan protocol's commands, ws037-p007 stage D2).
 *
 * The device is not unregistered while the machine runs: the GSP has no
 * path back to the firmware while it runs, so the GPU is kept until the
 * reboot or the shutdown (detach refuses).  Then the PCI shutdown hook
 * stops the device (nvrtx_node_stop: it goes offline, the lease ends, the
 * drain stops) and the GSP (P7), so the next boot finds the GPU as the
 * firmware left it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-display.h>

#include "drivers/gpu/nvrtx/nvrtx-node.h"
#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/*
 * The device's capabilities: storage, VRAM blobs, transfers, CPU mappings,
 * sharing of blobs, and the display with its events and its control.
 */
#define NODE_CAPABILITIES		(GPU_CAP_RESOURCE | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_MAPPING | GPU_CAP_SHARE | \
					 GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_EVENTS | GPU_CAP_DISPLAY_CONTROL)

/* The largest resource, and how many one session may hold. */
#define NODE_MAX_RESOURCE_BYTES		(64ULL << 20)
#define NODE_MAX_RESOURCES		1024U

/* A blob's VRAM is on this boundary (a window's surface address is a multiple of 256). */
#define NODE_BLOB_ALIGN			0x10000ULL

/* How long each turn of the message drain takes messages, and its pause after a failed turn. */
#define NODE_DRAIN_TURN_US		1000000U
#define NODE_DRAIN_PAUSE_US		1000000U

/* How often the drain writes the counts nvrtx.vk.perf=1 asks for. */
#define NODE_PERF_PERIOD_US		5000000U

/* Matches no function: the drain takes every message until its turn ends. */
#define NODE_DRAIN_NO_FUNCTION		0xffffffffU

/* The page the storage is allocated in. */
#define NODE_PAGE_BYTES			4096U

/* How long the stop waits for the message drain to end (a turn, its pause and a margin), and its step. */
#define NODE_DRAIN_STOP_TIMEOUT_US	3500000U
#define NODE_DRAIN_STOP_STEP_US		10000U

static int node_open(void *private_data, void **session);
static void node_close(void *private_data, void *session);
static int node_get_info(void *private_data, void *session, struct gpu_info *info);
static int node_resource_create(void *private_data, void *session, const struct gpu_resource_create *request, void **object);
static void node_resource_destroy(void *private_data, void *session, void *object);
static void node_blob_unlisted(struct nvrtx_node *node, struct nvrtx_resource *resource);
static int node_blob_create(void *private_data, void *session, const struct gpu_blob_create *request, void **object, uint32_t *resource_id);
static int node_resource_read(void *private_data, void *session, void *object, uint64_t offset, void *buffer, uint32_t bytes);
static int node_resource_write(void *private_data, void *session, void *object, uint64_t offset, const void *buffer, uint32_t bytes);
static int node_resource_map(void *private_data, void *session, void *object, struct drv_gpu_mapping *mapping);
static int node_range(const struct nvrtx_resource *resource, uint64_t offset, uint32_t bytes);
static void node_free(struct nvrtx_node *node);
static void node_drain(void *argument);
static void node_perf_report(struct nvrtx_node *node);
static uint64_t node_perf_average(uint64_t total, uint64_t count);

/*
 * Registers the GPU as a GPU device after the takeover (stage P8c).
 *
 * Needs the CPU's window (P4m) and the head taken over (P8b).  Returns 0
 * once the device is registered: the GSP must then stay up.  On a failure
 * nothing is registered and the caller stops the GSP as usual.
 */
int
nvrtx_node_start(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_node *node;
	struct thread *drain;
	bool vk_wanted;
	int error;

	/* The display's frames need the window and a head of the driver's own. */
	nvrtx_stage_mark("P8c begin");
	if (gpu->memory_state.bar1_ready == 0) {
		nvrtx_stage_mark("P8c fail: no bar1 window");
		return ENODEV;
	}

	if (gpu->display.window_made == 0) {
		nvrtx_stage_mark("P8c fail: no head taken over");
		return ENODEV;
	}

	/* Allocates the node; it lives until power-off. */
	node = kern_calloc(1U, sizeof(*node));
	if (node == NULL)
		return ENOMEM;

	/* Initializes the node: no lease, numbers from one, the refresh count from one. */
	(void)mutex_init(&node->present_lock, LOCK_RANK_DEVICE, "nvrtx present");
	(void)mutex_init(&node->lock, LOCK_RANK_DEVICE, "nvrtx node");
	spin_init(&node->refresh_lock, LOCK_RANK_DEVICE, "nvrtx refresh");
	node->gpu = gpu;
	node->next_lease = 1U;
	node->next_resource_id = 1U;
	node->refresh_count = 1U;

	/* The counts every 5 s, when the boot asked for them. */
	node->perf = 0;
	if (nvrtx_stage_vk_perf_wanted())
		node->perf = 1;
	node->perf_since_us = nvrtx_now_us();

	/* Describes the device: its interface, its capabilities and every operation. */
	node->ops.version = DRV_GPU_INTERFACE_VERSION;
	node->ops.size = sizeof(node->ops);
	node->ops.capabilities = NODE_CAPABILITIES;
	node->ops.open = node_open;
	node->ops.close = node_close;
	node->ops.get_info = node_get_info;
	node->ops.resource_create = node_resource_create;
	node->ops.resource_destroy = node_resource_destroy;
	node->ops.blob_create = node_blob_create;
	node->ops.resource_read = node_resource_read;
	node->ops.resource_write = node_resource_write;
	node->ops.resource_map = node_resource_map;
	node->ops.display = &nvrtx_display_ops;
	node->ops.share = &nvrtx_share_ops;
	node->ops.scanout = &nvrtx_scanout_ops;

	/* With nvrtx.vk=1 the device is also a renderer (ws037-p007 stage D2). */
	vk_wanted = nvrtx_stage_vk_wanted();
	if (vk_wanted)
		nvrtx_runtime_bind(node);

	/* Takes the display's frames and shows the console surface. */
	error = nvrtx_present_start(node);
	if (error != 0) {
		nvrtx_stage_mark("P8c fail: the display's frames (%d)", error);
		node_free(node);
		return error;
	}

	/*
	 * Makes the cursor plane while the worker is still the GSP's only
	 * caller (its channel is made through the resource manager); without
	 * it the display has no cursor plane.
	 */
	nvrtx_cursor_start(node);

	/* Registers the device; the core publishes /dev/gpuN. */
	error = drv_gpu_register(&node->ops, node, &node->device);
	if (error != 0) {
		nvrtx_stage_mark("P8c fail: register (%d)", error);
		node_free(node);
		return error;
	}

	/* The renderer's worker runs once the device is registered. */
	if (node->renderer != 0)
		nvrtx_runtime_start(node);

	/*
	 * Starts the thread that takes the messages the GSP sends meanwhile
	 * (channel errors, display events), as nouveau's message worker does,
	 * so its queue never fills.  Without it the device still works for a
	 * while, so a failure is only shown.
	 */
	error = kthread_create(node_drain, node, SCHED_PRIORITY_DEFAULT, &drain);
	if (error == 0) {
		node->drain_running = 1;
		drain->detached = 1;
		thread_start(drain);
	} else {
		nvrtx_stage_mark("P8c: no message drain (%d)", error);
	}

	/* Succeeded: the GPU is a GPU device; the GSP stays up for it. */
	gpu->node = node;
	nvrtx_stage_mark("P8c ok: gpu device registered, display %ux%u at %u mHz",
	    (unsigned)node->width,
	    (unsigned)node->height,
	    (unsigned)node->refresh_millihz);
	return 0;
}

/*
 * Stops the GPU device and the GSP for the reboot or the shutdown of the
 * machine.
 *
 * In this order: the renderer takes no more work; the device goes offline
 * (the core refuses new opens and operations; an open that remains only
 * delays the registration's release, which the reset makes moot); the
 * message drain ends, so this thread is the GSP's only reader; the lease
 * ends under the node's present lock and its lock, which bring the
 * firmware's picture back on the head and are held while the GSP is
 * stopped (P7), so no presentation or frame runs meanwhile; the display is
 * broken after it.  Every step is bounded and a failure is only shown.
 */
void
nvrtx_node_stop(
	struct nvrtx_node *node)
{
	uint64_t start;
	uint64_t waited;
	int error;

	/* The renderer refuses new frames and markers from here on. */
	nvrtx_runtime_stop(node);

	/* The device goes offline; opens that remain keep the registration (offline too), which the reset ends. */
	error = drv_gpu_unregister(node->device);
	if (error == 0) {
		node->device = NULL;
	} else if (error == EBUSY) {
		nvrtx_stage_mark("P7 shutdown: the GPU device is offline; opens remain until the reset");
	} else {
		nvrtx_stage_mark("P7 shutdown: the GPU device was not withdrawn (%d)", error);
	}

	/* Ends the message drain at its next turn, so the GSP's replies reach the stop. */
	node->drain_stop = 1;
	start = nvrtx_now_us();
	for (;;) {
		if (node->drain_running == 0)
			break;

		waited = nvrtx_now_us() - start;
		if (waited > NODE_DRAIN_STOP_TIMEOUT_US) {
			nvrtx_stage_mark("P7 shutdown: the message drain did not end in %u ms", (unsigned)(waited / 1000U));
			break;
		}

		kern_usleep_range(NODE_DRAIN_STOP_STEP_US, NODE_DRAIN_STOP_STEP_US * 2U);
	}

	/*
	 * Brings the firmware's picture back and stops the GSP while nothing
	 * else may use the display or the channel: a presentation still
	 * waiting for its flip finishes first (the present lock).  The display
	 * is broken from then on, so a presentation or a cursor change that
	 * comes after touches nothing of the stopped GPU.
	 */
	mutex_lock(&node->present_lock);
	mutex_lock(&node->lock);

	nvrtx_present_stop(node);
	(void)nvrtx_gsp_shutdown(node->gpu);
	node->broken = 1;

	mutex_unlock(&node->lock);
	mutex_unlock(&node->present_lock);
}

/*
 * Drops one reference to a blob's VRAM: the last one gives the VRAM back,
 * unless the display may still read it (kept), and frees the record.
 */
void
nvrtx_node_backing_put(
	struct nvrtx_node *node,
	struct nvrtx_backing *backing)
{
	uint32_t refs;

	/* Counts the reference off under the node's lock. */
	mutex_lock(&node->lock);

	backing->refs--;
	refs = backing->refs;

	mutex_unlock(&node->lock);

	/* Others still name the VRAM. */
	if (refs != 0U)
		return;

	/* The VRAM and its place in the window go back, unless the display may still read it. */
	if (backing->kept == 0)
		nvrtx_bo_free(node->gpu, &backing->bo);

	/* Frees the record. */
	kern_free(backing);
}

/*
 * Numbers a new blob or import for the protocol: never zero.
 */
uint32_t
nvrtx_node_resource_id(
	struct nvrtx_node *node)
{
	uint32_t resource_id;

	/* Takes the next number, skipping zero when the count wraps. */
	mutex_lock(&node->lock);

	resource_id = node->next_resource_id;
	node->next_resource_id++;
	if (node->next_resource_id == 0U)
		node->next_resource_id = 1U;

	mutex_unlock(&node->lock);

	/* Succeeded: the number is the resource's. */
	return resource_id;
}

/*
 * Lists a blob in its session under its protocol number, so the executor
 * finds it.
 */
void
nvrtx_node_blob_listed(
	struct nvrtx_node *node,
	struct nvrtx_session *session,
	struct nvrtx_resource *resource,
	uint32_t resource_id)
{
	/* At the head of the session's list. */
	mutex_lock(&node->lock);

	resource->resource_id = resource_id;
	resource->session = session;
	resource->next_blob = session->blobs;
	session->blobs = resource;

	mutex_unlock(&node->lock);
}

/*
 * Gives the CPU view of a blob of the executor's owner (a node session) by
 * its protocol number.  The caller holds the node's lock.
 */
int
nvrtx_vk_blob(
	void *owner,
	uint32_t resource_id,
	uint8_t **cpu,
	uint64_t *bytes)
{
	struct nvrtx_session *session;
	struct nvrtx_resource *resource;

	session = owner;

	/* Finds the number among the session's blobs. */
	for (resource = session->blobs; resource != NULL; resource = resource->next_blob) {
		if (resource->resource_id == resource_id)
			break;
	}

	/* Refuses a number the session has no blob for. */
	if (resource == NULL)
		return ENOENT;

	/* Succeeded: the blob's bytes in the CPU's window and their count. */
	*cpu = (uint8_t *)(uintptr_t)nvrtx_bo_cpu(session->node->gpu, &resource->backing->bo);
	*bytes = resource->bytes;
	return 0;
}

/*
 * Gives the executor's owner's GPU's PCI product.
 */
uint32_t
nvrtx_vk_device_id(
	void *owner)
{
	struct nvrtx_session *session;

	session = owner;

	/* The product the attach read. */
	return session->node->gpu->identity.product;
}

/*
 * Gives the bytes of VRAM the executor's memory may take: what the CPU's
 * window holds after the console's part.
 */
uint64_t
nvrtx_vk_heap_bytes(
	void *owner)
{
	struct nvrtx_session *session;
	const struct nvrtx_memory_state *memory;

	session = owner;
	memory = &session->node->gpu->memory_state;

	/* The window's part the blobs are mapped in. */
	return memory->bar1_window - memory->bar1_cpu_from;
}

/* Opens a session: it names its node and has an executor session of its own. */
static int
node_open(
	void *private_data,
	void **session)
{
	struct nvrtx_session *opened;
	int error;

	/* Allocates the session. */
	opened = kern_calloc(1U, sizeof(*opened));
	if (opened == NULL)
		return ENOMEM;

	/* Opens its executor session. */
	error = nvrtx_vk_session_open(opened, &opened->vk);
	if (error != 0) {
		kern_free(opened);
		return error;
	}

	/* Succeeded: the session is the caller's. */
	opened->node = private_data;
	*session = opened;
	return 0;
}

/* Closes a session: its lease ends (the console surface comes back), its executor session closes, then it goes. */
static void
node_close(
	void *private_data,
	void *session)
{
	struct nvrtx_node *node;
	struct nvrtx_session *closing;
	int error;

	node = private_data;
	closing = session;

	/* Ends the session's lease, if it holds it. */
	nvrtx_present_session_close(node, session);

	/* Closes the executor session; a payload still held keeps it (and is shown). */
	mutex_lock(&node->lock);

	error = nvrtx_vk_session_close(&closing->vk);

	mutex_unlock(&node->lock);

	if (error != 0)
		nvrtx_stage_mark("P8c: an executor session closed with %d", error);

	/* Frees the session. */
	kern_free(session);
}

/* Describes the device to userspace. */
static int
node_get_info(
	void *private_data,
	void *session,
	struct gpu_info *info)
{
	struct nvrtx_node *node;

	UNUSED_PARAMETER(session);

	node = private_data;

	/* The capabilities (the renderer's too when it is bound) and the limits of the resources. */
	info->capabilities = node->ops.capabilities;
	info->max_resources = NODE_MAX_RESOURCES;
	info->max_resource_bytes = NODE_MAX_RESOURCE_BYTES;

	/* Names the driver, with the terminating NUL. */
	kern_memset(info->driver_name, 0, sizeof(info->driver_name));
	kern_memcpy(info->driver_name, "nvrtx", 6U);

	/* Succeeded: userspace can identify the device. */
	return 0;
}

/* Creates ordinary storage: cleared, contiguous RAM. */
static int
node_resource_create(
	void *private_data,
	void *session,
	const struct gpu_resource_create *request,
	void **object)
{
	struct nvrtx_resource *resource;
	uint64_t size;
	int error;

	UNUSED_PARAMETER(private_data);
	UNUSED_PARAMETER(session);

	/* The core bounded the size; the run is whole pages. */
	if (request->bytes == 0U || request->bytes > NODE_MAX_RESOURCE_BYTES)
		return EINVAL;
	size = (request->bytes + NODE_PAGE_BYTES - 1U) & ~((uint64_t)NODE_PAGE_BYTES - 1U);

	/* Allocates the record. */
	resource = kern_calloc(1U, sizeof(*resource));
	if (resource == NULL)
		return ENOMEM;

	/* Allocates the RAM. */
	error = kern_pmem_alloc((size_t)size, NODE_PAGE_BYTES, &resource->ram);
	if (error != 0) {
		kern_free(resource);
		return ENOMEM;
	}

	/* Clears it through the direct map. */
	resource->cpu = kern_pmem_to_kernel(resource->ram.paddr);
	if (resource->cpu == NULL) {
		(void)kern_pmem_free(&resource->ram);
		kern_free(resource);
		return EIO;
	}

	kern_memset(resource->cpu, 0, (size_t)size);
	resource->kind = NVRTX_RESOURCE_STORAGE;
	resource->bytes = request->bytes;

	/* Succeeded: the storage is the session's. */
	*object = resource;
	return 0;
}

/*
 * Destroys a resource of either kind: a blob on the screen is taken off it
 * first; one that could not be is kept (its VRAM is never reused).
 */
static void
node_resource_destroy(
	void *private_data,
	void *session,
	void *object)
{
	struct nvrtx_node *node;
	struct nvrtx_resource *resource;

	UNUSED_PARAMETER(session);

	node = private_data;
	resource = object;

	/* Storage: its RAM goes back. */
	if (resource->kind == NVRTX_RESOURCE_STORAGE) {
		(void)kern_pmem_free(&resource->ram);
		kern_free(resource);
		return;
	}

	/* A blob leaves its session's list and the screen first. */
	node_blob_unlisted(node, resource);
	nvrtx_present_resource_gone(node, resource);

	/* Drops its reference to the VRAM; the last one gives it back. */
	nvrtx_node_backing_put(node, resource->backing);

	/* Frees the record. */
	kern_free(resource);
}

/*
 * Creates a blob: a buffer object in the VRAM, mapped in the CPU's window.
 *
 * The sharing flags need nothing more: every blob is VRAM the other opens
 * of the device can import (share.c); another device cannot (no physical
 * view is lent).
 */
static int
node_blob_create(
	void *private_data,
	void *session,
	const struct gpu_blob_create *request,
	void **object,
	uint32_t *resource_id)
{
	struct nvrtx_node *node;
	struct nvrtx_resource *resource;
	struct nvrtx_backing *backing;
	volatile uint8_t *cpu;
	uint64_t at;
	int error;

	node = private_data;

	/* The core bounded the size. */
	if (request->bytes == 0U || request->bytes > NODE_MAX_RESOURCE_BYTES)
		return EINVAL;

	/* Allocates the record. */
	resource = kern_calloc(1U, sizeof(*resource));
	if (resource == NULL)
		return ENOMEM;

	/* Allocates the backing's record. */
	backing = kern_calloc(1U, sizeof(*backing));
	if (backing == NULL) {
		kern_free(resource);
		return ENOMEM;
	}

	/* Takes the VRAM and maps it for the CPU. */
	error = nvrtx_bo_alloc(node->gpu, request->bytes, NODE_BLOB_ALIGN, &backing->bo);
	if (error == 0)
		error = nvrtx_bo_map(node->gpu, &backing->bo);
	if (error != 0) {
		nvrtx_bo_free(node->gpu, &backing->bo);
		kern_free(backing);
		kern_free(resource);
		return error;
	}

	/* Clears it through the window, so no earlier contents of the VRAM show. */
	cpu = nvrtx_bo_cpu(node->gpu, &backing->bo);
	for (at = 0U; at < backing->bo.size; at += 4U)
		*(volatile uint32_t *)(volatile void *)(cpu + at) = 0U;

	/* The blob holds the backing's one reference. */
	backing->bytes = request->bytes;
	backing->refs = 1U;
	resource->kind = NVRTX_RESOURCE_BLOB;
	resource->bytes = request->bytes;
	resource->backing = backing;

	/*
	 * A blob of the renderer's made for a VkDeviceMemory (its blob_id is the
	 * memory's identity) is that memory's storage: the memory takes a
	 * reference to the backing and the work channel maps it.
	 */
	if (request->blob_id != 0U && node->renderer != 0) {
		error = nvrtx_vk_node_blob_attach(node, session, request->blob_id, backing, request->bytes);
		if (error != 0) {
			nvrtx_bo_free(node->gpu, &backing->bo);
			kern_free(backing);
			kern_free(resource);
			return error;
		}
	}

	/* Succeeded: the blob is the session's, numbered for the protocol and listed. */
	*resource_id = nvrtx_node_resource_id(node);
	nvrtx_node_blob_listed(node, session, resource, *resource_id);
	*object = resource;
	return 0;
}

/* Copies bytes of a resource out to a kernel buffer. */
static int
node_resource_read(
	void *private_data,
	void *session,
	void *object,
	uint64_t offset,
	void *buffer,
	uint32_t bytes)
{
	struct nvrtx_node *node;
	struct nvrtx_resource *resource;
	volatile uint8_t *source;
	uint8_t *target;
	uint32_t at;
	int error;

	UNUSED_PARAMETER(session);

	node = private_data;
	resource = object;
	target = buffer;

	/* The range lies inside the resource. */
	error = node_range(resource, offset, bytes);
	if (error != 0)
		return error;

	/* Storage is plain RAM. */
	if (resource->kind == NVRTX_RESOURCE_STORAGE) {
		kern_memcpy(target, resource->cpu + offset, bytes);
		return 0;
	}

	/* A blob is read byte by byte through the uncached window. */
	source = nvrtx_bo_cpu(node->gpu, &resource->backing->bo) + offset;
	for (at = 0U; at < bytes; at++)
		target[at] = source[at];

	/* Succeeded: the bytes are in the buffer. */
	return 0;
}

/* Copies bytes of a kernel buffer into a resource. */
static int
node_resource_write(
	void *private_data,
	void *session,
	void *object,
	uint64_t offset,
	const void *buffer,
	uint32_t bytes)
{
	struct nvrtx_node *node;
	struct nvrtx_resource *resource;
	volatile uint8_t *target;
	const uint8_t *source;
	uint32_t at;
	int error;

	UNUSED_PARAMETER(session);

	node = private_data;
	resource = object;
	source = buffer;

	/* The range lies inside the resource. */
	error = node_range(resource, offset, bytes);
	if (error != 0)
		return error;

	/* Storage is plain RAM. */
	if (resource->kind == NVRTX_RESOURCE_STORAGE) {
		kern_memcpy(resource->cpu + offset, source, bytes);
		return 0;
	}

	/* A blob is written byte by byte through the uncached window, then read back once so the writes have landed. */
	target = nvrtx_bo_cpu(node->gpu, &resource->backing->bo) + offset;
	for (at = 0U; at < bytes; at++)
		target[at] = source[at];
	if (bytes != 0U)
		(void)target[bytes - 1U];

	/* Succeeded: the bytes are in the resource. */
	return 0;
}

/* Describes the CPU view of a resource: cached RAM for storage, the window (device memory) for a blob. */
static int
node_resource_map(
	void *private_data,
	void *session,
	void *object,
	struct drv_gpu_mapping *mapping)
{
	struct nvrtx_node *node;
	struct nvrtx_resource *resource;

	UNUSED_PARAMETER(session);

	node = private_data;
	resource = object;

	/* Storage: the run of RAM. */
	if (resource->kind == NVRTX_RESOURCE_STORAGE) {
		mapping->physical = resource->ram.paddr;
		mapping->address = resource->cpu;
		mapping->bytes = resource->ram.size;
		mapping->attributes = 0U;
		return 0;
	}

	/* A blob: its place in the window. */
	mapping->physical = nvrtx_bo_bus(node->gpu, &resource->backing->bo);
	mapping->address = (void *)(uintptr_t)nvrtx_bo_cpu(node->gpu, &resource->backing->bo);
	mapping->bytes = resource->backing->bo.size;
	mapping->attributes = DRV_GPU_MAPPING_DEVICE;

	/* Succeeded: the view covers the whole object. */
	return 0;
}

/* Reports whether a range of bytes lies inside a resource: EINVAL otherwise. */
static int
node_range(
	const struct nvrtx_resource *resource,
	uint64_t offset,
	uint32_t bytes)
{
	/* The offset is inside, and the bytes after it too. */
	if (offset > resource->bytes)
		return EINVAL;
	if (bytes > resource->bytes - offset)
		return EINVAL;

	/* The range is inside. */
	return 0;
}

/*
 * Frees a node that was never registered: its frames' and cursor's VRAM
 * and the node.  The caller then stops the GSP, which no longer shows the
 * frames and frees the cursor channel.
 */
static void
node_free(
	struct nvrtx_node *node)
{
	unsigned index;

	/* Gives each frame's VRAM back (a frame never taken is skipped). */
	for (index = 0U; index < 2U; index++)
		nvrtx_bo_free(node->gpu, &node->frame_objects[index]);

	/* Gives the cursor's slots back (skipped when never taken). */
	nvrtx_bo_free(node->gpu, &node->cursor_object);

	/* Frees the node. */
	kern_free(node);
}

/*
 * Takes the messages the GSP sends while the device is up, a turn at a
 * time, until the stop for the reboot asks it to end.  The worker that
 * made the device has ended, and the device's operations make no calls to
 * the GSP, so this thread is the channel's only reader.  The wait shows
 * the channel errors it meets.
 *
 * After each turn (about a second) it also reads the head's frame counter,
 * so the refresh count keeps up even while nobody waits for a boundary
 * (the 16-bit counter wraps after about 18 minutes at 60 Hz).
 */
static void
node_drain(
	void *argument)
{
	struct nvrtx_node *node;
	uint64_t count;
	uint64_t time_ns;
	size_t length;
	int error;

	node = argument;

	/* Takes messages turn after turn; a turn ends when its time is spent, and the stop ends the turns. */
	for (;;) {
		error = nvrtx_rpc_wait(node->gpu, NODE_DRAIN_NO_FUNCTION, 0U, NODE_DRAIN_TURN_US, &length);
		if (error != 0 && error != ETIMEDOUT)
			kern_usleep_range(NODE_DRAIN_PAUSE_US, NODE_DRAIN_PAUSE_US);

		/* The stop for the reboot reads the GSP's replies itself from here on. */
		if (node->drain_stop != 0)
			break;

		/* Brings the refresh count up to date. */
		nvrtx_present_refresh_sample(node, &count, &time_ns);

		/* Writes the counts when they are asked for and their time has come. */
		node_perf_report(node);
	}

	/* drain_running cleared tells the stop that nothing else reads the channel. */
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	node->drain_running = 0;
}

/*
 * Writes the counts of the last period into the kernel log and starts the
 * next (nvrtx.vk.perf=1): the push buffers the work channel ran and their
 * average time, the CPU's copies, the renderer's frames with their average
 * wait for the node's lock and their average hold of it, and the flips
 * with their average wait for the head.
 */
static void
node_perf_report(
	struct nvrtx_node *node)
{
	struct nvrtx_node_perf taken;
	uint64_t now;
	uint64_t period;

	/* Nothing is written without nvrtx.vk.perf=1, nor before the period is over. */
	if (node->perf == 0)
		return;

	now = nvrtx_now_us();
	period = now - node->perf_since_us;
	if (period < NODE_PERF_PERIOD_US)
		return;

	/* Takes each count and clears it; adds that come meanwhile go to the next period. */
	taken.runs = __atomic_exchange_n(&node->counts.runs, 0U, __ATOMIC_RELAXED);
	taken.run_us = __atomic_exchange_n(&node->counts.run_us, 0U, __ATOMIC_RELAXED);
	taken.copies = __atomic_exchange_n(&node->counts.copies, 0U, __ATOMIC_RELAXED);
	taken.copy_bytes = __atomic_exchange_n(&node->counts.copy_bytes, 0U, __ATOMIC_RELAXED);
	taken.frames = __atomic_exchange_n(&node->counts.frames, 0U, __ATOMIC_RELAXED);
	taken.wait_us = __atomic_exchange_n(&node->counts.wait_us, 0U, __ATOMIC_RELAXED);
	taken.held_us = __atomic_exchange_n(&node->counts.held_us, 0U, __ATOMIC_RELAXED);
	taken.flips = __atomic_exchange_n(&node->counts.flips, 0U, __ATOMIC_RELAXED);
	taken.flip_us = __atomic_exchange_n(&node->counts.flip_us, 0U, __ATOMIC_RELAXED);
	node->perf_since_us = now;

	/* Two lines within the log's 79 columns. */
	nvrtx_stage_mark("perf %llums: runs %llu avg %lluus, cpu copies %llu %lluKiB",
	    (unsigned long long)(period / 1000U),
	    (unsigned long long)taken.runs,
	    (unsigned long long)node_perf_average(taken.run_us, taken.runs),
	    (unsigned long long)taken.copies,
	    (unsigned long long)(taken.copy_bytes / 1024U));
	nvrtx_stage_mark("perf: frames %llu wait %lluus held %lluus, flips %llu avg %lluus",
	    (unsigned long long)taken.frames,
	    (unsigned long long)node_perf_average(taken.wait_us, taken.frames),
	    (unsigned long long)node_perf_average(taken.held_us, taken.frames),
	    (unsigned long long)taken.flips,
	    (unsigned long long)node_perf_average(taken.flip_us, taken.flips));
}

/* Reports a total's average over its count, zero for no count. */
static uint64_t
node_perf_average(
	uint64_t total,
	uint64_t count)
{
	/* Nothing was counted. */
	if (count == 0U)
		return 0U;

	/* Succeeded: the average. */
	return total / count;
}

/* Takes a blob out of its session's list (a blob never listed is left alone). */
static void
node_blob_unlisted(
	struct nvrtx_node *node,
	struct nvrtx_resource *resource)
{
	struct nvrtx_resource **link;

	/* Unlinks it from the session's list. */
	mutex_lock(&node->lock);

	if (resource->session != NULL) {
		for (link = &resource->session->blobs; *link != NULL; link = &(*link)->next_blob) {
			if (*link == resource) {
				*link = resource->next_blob;
				break;
			}
		}
	}

	resource->session = NULL;
	resource->next_blob = NULL;

	mutex_unlock(&node->lock);
}
