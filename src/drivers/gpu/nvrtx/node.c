/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU device of the nvrtx driver: sessions and resources (stage P8c).
 *
 * With nvrtx.resident=1 and a head taken over (P8b), the worker leaves the
 * GSP up and registers the GPU with the GPU core.  The device has ordinary
 * storage (RAM the CPU reads and writes), blobs in the VRAM (buffer objects
 * of P4m's allocator, mapped in the CPU's window), transfers and CPU mappings
 * of both, and the display of present.c.  It has no command stream yet: the
 * 3D and compute work of the GPU device is a later stage (ws037-p007).
 *
 * The device is never unregistered: the GSP has no path back to the firmware
 * while it runs, so the GPU is kept until power-off (detach refuses).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-display.h>

#include "drivers/gpu/nvrtx/nvrtx-node.h"

/* The device's capabilities: storage, VRAM blobs, transfers, CPU mappings and the display. */
#define NODE_CAPABILITIES		(GPU_CAP_RESOURCE | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_MAPPING | GPU_CAP_DISPLAY)

/* The largest resource, and how many one session may hold. */
#define NODE_MAX_RESOURCE_BYTES		(64ULL << 20)
#define NODE_MAX_RESOURCES		1024U

/* A blob's VRAM is on this boundary (a window's surface address is a multiple of 256). */
#define NODE_BLOB_ALIGN			0x10000ULL

/* The page the storage is allocated in. */
#define NODE_PAGE_BYTES			4096U

static int node_open(void *private_data, void **session);
static void node_close(void *private_data, void *session);
static int node_get_info(void *private_data, void *session, struct gpu_info *info);
static int node_resource_create(void *private_data, void *session, const struct gpu_resource_create *request, void **object);
static void node_resource_destroy(void *private_data, void *session, void *object);
static int node_blob_create(void *private_data, void *session, const struct gpu_blob_create *request, void **object, uint32_t *resource_id);
static int node_resource_read(void *private_data, void *session, void *object, uint64_t offset, void *buffer, uint32_t bytes);
static int node_resource_write(void *private_data, void *session, void *object, uint64_t offset, const void *buffer, uint32_t bytes);
static int node_resource_map(void *private_data, void *session, void *object, struct drv_gpu_mapping *mapping);
static int node_range(const struct nvrtx_resource *resource, uint64_t offset, uint32_t bytes);
static void node_free(struct nvrtx_node *node);

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

	/* Initializes the node: no lease, numbers from one. */
	(void)mutex_init(&node->lock, LOCK_RANK_DEVICE, "nvrtx node");
	node->gpu = gpu;
	node->next_lease = 1U;
	node->next_resource_id = 1U;

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

	/* Takes the display's frames and shows the console surface. */
	error = nvrtx_present_start(node);
	if (error != 0) {
		nvrtx_stage_mark("P8c fail: the display's frames (%d)", error);
		node_free(node);
		return error;
	}

	/* Registers the device; the core publishes /dev/gpuN. */
	error = drv_gpu_register(&node->ops, node, &node->device);
	if (error != 0) {
		nvrtx_stage_mark("P8c fail: register (%d)", error);
		node_free(node);
		return error;
	}

	/* Succeeded: the GPU is a GPU device; the GSP stays up for it. */
	gpu->node = node;
	nvrtx_stage_mark("P8c ok: gpu device registered, display %ux%u at %u mHz",
	    (unsigned)node->width,
	    (unsigned)node->height,
	    (unsigned)node->refresh_millihz);
	return 0;
}

/* Opens a session: it only names its node. */
static int
node_open(
	void *private_data,
	void **session)
{
	struct nvrtx_session *opened;

	/* Allocates the session. */
	opened = kern_calloc(1U, sizeof(*opened));
	if (opened == NULL)
		return ENOMEM;

	/* Succeeded: the session is the caller's. */
	opened->node = private_data;
	*session = opened;
	return 0;
}

/* Closes a session: its lease ends (the console surface comes back) before it goes. */
static void
node_close(
	void *private_data,
	void *session)
{
	struct nvrtx_node *node;

	node = private_data;

	/* Ends the session's lease, if it holds it. */
	nvrtx_present_session_close(node, session);

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
	UNUSED_PARAMETER(private_data);
	UNUSED_PARAMETER(session);

	/* The capabilities and the limits of the resources. */
	info->capabilities = NODE_CAPABILITIES;
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

	/* A blob leaves the screen first. */
	nvrtx_present_resource_gone(node, resource);

	/* Its VRAM and its place in the window go back, unless the display may still read it. */
	if (resource->kept == 0)
		nvrtx_bo_free(node->gpu, &resource->bo);

	/* Frees the record. */
	kern_free(resource);
}

/* Creates a blob: a buffer object in the VRAM, mapped in the CPU's window. */
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
	volatile uint8_t *cpu;
	uint64_t at;
	int error;

	UNUSED_PARAMETER(session);

	node = private_data;

	/* The core bounded the size and refused the sharing flags. */
	if (request->bytes == 0U || request->bytes > NODE_MAX_RESOURCE_BYTES)
		return EINVAL;

	/* Allocates the record. */
	resource = kern_calloc(1U, sizeof(*resource));
	if (resource == NULL)
		return ENOMEM;

	/* Takes the VRAM and maps it for the CPU. */
	error = nvrtx_bo_alloc(node->gpu, request->bytes, NODE_BLOB_ALIGN, &resource->bo);
	if (error == 0)
		error = nvrtx_bo_map(node->gpu, &resource->bo);
	if (error != 0) {
		nvrtx_bo_free(node->gpu, &resource->bo);
		kern_free(resource);
		return error;
	}

	/* Clears it through the window, so no earlier contents of the VRAM show. */
	cpu = nvrtx_bo_cpu(node->gpu, &resource->bo);
	for (at = 0U; at < resource->bo.size; at += 4U)
		*(volatile uint32_t *)(volatile void *)(cpu + at) = 0U;

	/* Numbers it for the protocol (never zero). */
	resource->kind = NVRTX_RESOURCE_BLOB;
	resource->bytes = request->bytes;
	mutex_lock(&node->lock);

	*resource_id = node->next_resource_id;
	node->next_resource_id++;
	if (node->next_resource_id == 0U)
		node->next_resource_id = 1U;

	mutex_unlock(&node->lock);

	/* Succeeded: the blob is the session's. */
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
	source = nvrtx_bo_cpu(node->gpu, &resource->bo) + offset;
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
	target = nvrtx_bo_cpu(node->gpu, &resource->bo) + offset;
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
	mapping->physical = nvrtx_bo_bus(node->gpu, &resource->bo);
	mapping->address = (void *)(uintptr_t)nvrtx_bo_cpu(node->gpu, &resource->bo);
	mapping->bytes = resource->bo.size;
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
 * Frees a node that was never registered: its frames' VRAM and the node.
 * The caller then stops the GSP, which no longer shows the frames.
 */
static void
node_free(
	struct nvrtx_node *node)
{
	unsigned index;

	/* Gives each frame's VRAM back (a frame never taken is skipped). */
	for (index = 0U; index < 2U; index++)
		nvrtx_bo_free(node->gpu, &node->frame_objects[index]);

	/* Frees the node. */
	kern_free(node);
}
