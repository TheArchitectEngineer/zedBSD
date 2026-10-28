/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Memory, buffers and images of the Vulkan executor, and the executor
 * heap of each device.
 *
 * A VkDeviceMemory is host memory below the GPU's limit (or the memory of
 * a resource the node session imported), held by the object and mapped
 * once into the device's GHCI session; its CPU view is the blob libvulkan
 * makes of it.  A buffer or an image is a range of one memory object once
 * it is bound.  Every image is laid out in linear rows (section 8.5.2 of
 * the specification): attachments and storage images must be, and the
 * others may be.
 *
 * The executor heap is what the driver itself writes for the GPU in a
 * device's session (events now; descriptors, pipelines and the temporary
 * memory of a submission later): blocks of host memory mapped once and
 * handed out in pieces, a block twice the last when none has room.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "vk.h"

#include "drivers/gpu/i915/vk/vk-codec.inc"

/* The one memory type. */
#define MEMORY_TYPES			1U

/* A buffer's size in memory is a whole number of words. */
#define BUFFER_GRANULE			4U

static int allocate_memory(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static uint32_t memory_back(struct ghci_vk_session *session, struct ghci_vk_object *object, uint32_t imported, uint64_t bytes);
static int free_memory(struct ghci_vk_session *session, enum ghci_vk_kind kind, struct i915_wire_reader *reader);
static int commitment(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int create_buffer(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int create_image(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static uint32_t image_admit(struct ghci_vk_session *session, const VkImageCreateInfo *info, struct ghci_vk_image *image);
static int requirements(struct ghci_vk_session *session, enum ghci_vk_kind kind, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int bind(struct ghci_vk_session *session, enum ghci_vk_kind kind, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int subresource_layout(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int sparse_requirements(uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static struct ghci_vk_device *device_of(struct ghci_vk_session *session, uint64_t handle);
static uint64_t round_up(uint64_t value, uint64_t granule);
static int heap_grow(struct ghci_vk_session *session, struct ghci_vk_device *device, uint32_t least);

/*
 * Runs a memory, buffer or image command; *handled stays clear for
 * another group's number.
 */
int
ghci_vk_memory_route(
	struct ghci_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another group. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_ALLOCATE_MEMORY:
		error = allocate_memory(session, requested, reader, reply);
		break;
	case GPU_OP_FREE_MEMORY:
		error = free_memory(session, GHCI_VK_MEMORY, reader);
		break;
	case GPU_OP_GET_DEVICE_MEMORY_COMMITMENT:
		error = commitment(session, requested, reader, reply);
		break;
	case GPU_OP_CREATE_BUFFER:
		error = create_buffer(session, requested, reader, reply);
		break;
	case GPU_OP_DESTROY_BUFFER:
		error = free_memory(session, GHCI_VK_BUFFER, reader);
		break;
	case GPU_OP_CREATE_IMAGE:
		error = create_image(session, requested, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE:
		error = free_memory(session, GHCI_VK_IMAGE, reader);
		break;
	case GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS:
		error = requirements(session, GHCI_VK_BUFFER, requested, reader, reply);
		break;
	case GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS:
		error = requirements(session, GHCI_VK_IMAGE, requested, reader, reply);
		break;
	case GPU_OP_BIND_BUFFER_MEMORY:
		error = bind(session, GHCI_VK_BUFFER, requested, reader, reply);
		break;
	case GPU_OP_BIND_IMAGE_MEMORY:
		error = bind(session, GHCI_VK_IMAGE, requested, reader, reply);
		break;
	case GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT:
		error = subresource_layout(session, requested, reader, reply);
		break;
	case GPU_OP_GET_IMAGE_SPARSE_MEMORY_REQUIREMENTS:
		error = sparse_requirements(requested, reader, reply);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded or run. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/*
 * Reports the allocation of one of a node session's VkDeviceMemory
 * objects for its blob; NULL when the session has no such object.  The
 * caller holds the device's lock.
 */
struct ghci_allocation *
ghci_vk_memory_blob(
	struct ghci_device *device,
	struct ghci_session *owner,
	uint64_t blob_id)
{
	struct ghci_vk_object *object;

	(void)device;

	/* The memory object of that identity. */
	if (owner->vk == NULL)
		return NULL;
	object = ghci_vk_object_find(owner->vk, GHCI_VK_MEMORY, blob_id);
	if (object == NULL)
		return NULL;

	/* Succeeded: its allocation, which the blob's resource will hold too. */
	return object->as.memory.allocation;
}

/*
 * Takes bytes of a device's executor heap, aligned to alignment (a power
 * of two), growing the heap when no block has room.  Returns 0 with the
 * block and the offset in it, or the error of the growth (ENOMEM, EIO).
 */
int
ghci_vk_heap_take(
	struct ghci_vk_session *session,
	struct ghci_vk_device *device,
	uint32_t bytes,
	uint32_t alignment,
	struct ghci_vk_heap_block **block,
	uint32_t *offset)
{
	struct ghci_vk_heap_block *each;
	int error;

	/* The blocks in turn, then a new one. */
	for (;;) {
		for (each = device->heap; each != NULL; each = each->next) {
			error = ghci_range_alloc(&each->map, bytes, alignment, offset);
			if (error == 0) {
				*block = each;
				return 0;
			}
		}

		/* No block has room: one more, large enough. */
		error = heap_grow(session, device, bytes + alignment);
		if (error != 0)
			return error;
	}
}

/* Gives back bytes of the executor heap. */
void
ghci_vk_heap_give(
	struct ghci_vk_heap_block *block,
	uint32_t offset)
{
	/* The run goes back to its block; nothing else holds it. */
	if (block != NULL)
		(void)ghci_range_free(&block->map, offset);
}

/*
 * Lets a device's executor heap go: each block is unmapped and its
 * memory goes back once the unmap passed.  The device's objects are gone.
 */
void
ghci_vk_heap_fini(
	struct ghci_vk_session *session,
	struct ghci_vk_device *device)
{
	struct ghci_vk_heap_block *block;

	/* Each block in turn. */
	while (device->heap != NULL) {
		block = device->heap;
		device->heap = block->next;

		/* The GPU address goes with the unmap's fence, and the block lets its memory go. */
		ghci_vm_unmap(session->device, block->mapping);
		mutex_lock(&session->device->lock);

		ghci_memory_unhold(session->device, block->allocation);

		mutex_unlock(&session->device->lock);
		kern_free(block);
	}
	device->heap_bytes = 0U;
}

/*
 * Lets a VkDeviceMemory go: its mapping is unmapped (the mapping holds
 * the memory until the unmap passed) and the object's own hold goes.
 */
void
ghci_vk_memory_release(
	struct ghci_vk_session *session,
	struct ghci_vk_object *object)
{
	struct ghci_vk_memory *memory;

	/* The mapping, when the memory got one. */
	memory = &object->as.memory;
	if (memory->mapping != NULL)
		ghci_vm_unmap(session->device, memory->mapping);
	memory->mapping = NULL;

	/* The object's hold. */
	if (memory->allocation != NULL) {
		mutex_lock(&session->device->lock);

		ghci_memory_unhold(session->device, memory->allocation);

		mutex_unlock(&session->device->lock);
	}
	memory->allocation = NULL;
}

/* Fills the linear layout of an image (the caller checked that it fits the heap's size). */
void
ghci_vk_image_layout(
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levels,
	uint32_t layers,
	uint32_t samples,
	uint32_t texel_bytes,
	uint32_t block,
	struct ghci_layout_image *layout)
{
	/* The extent, the levels, the layers, the samples and the texel. */
	layout->texel_bytes = texel_bytes;
	layout->block = block;
	layout->width = width;
	layout->height = height;
	layout->depth = depth;
	layout->levels = levels;
	layout->layers = layers;
	layout->samples = samples;
}

/*
 * vkAllocateMemory: [device][u64 1][u32 MEMORY_ALLOCATE_INFO][u64 chain]
 * (a chained [u32 sType][u64 0][u32 value]: an import of a resource of
 * the session, or an export) [u64 size][u32 type][u64 0][u64 1][memory] ->
 * [result][u64 1][memory].
 */
static int
allocate_memory(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_device *device;
	struct ghci_vk_object *object;
	uint64_t handle;
	uint64_t present;
	uint64_t chain;
	uint64_t bytes;
	uint64_t memory;
	uint32_t record;
	uint32_t value;
	uint32_t type;
	uint32_t imported;
	uint32_t result;
	int error;

	/* The device, the allocate info's head and its one chained record. */
	handle = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	chain = drv_i915_wire_read_u64(reader);
	record = 0U;
	value = 0U;
	if (chain != 0U) {
		record = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		value = drv_i915_wire_read_u32(reader);
	}

	/* The size, the type and the tail. */
	bytes = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	memory = ghci_vk_create_tail(reader);
	if (reader->error != 0 || present != 1U || chain > 1U)
		return EINVAL;

	/* A device of the session, and the import's resource id. */
	device = device_of(session, handle);
	if (device == NULL)
		return EINVAL;
	imported = 0U;
	if (chain != 0U && record == GPU_OP_RECORD_IMPORT_RESOURCE)
		imported = value;
	else if (chain != 0U && record != VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO)
		return EINVAL;

	/* The size and the type the device has. */
	result = (uint32_t)VK_SUCCESS;
	if (type >= MEMORY_TYPES || bytes == 0U)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	else if (bytes > GHCI_VK_HEAP_SIZE)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* The object, then its memory and its mapping; a failure takes the object back. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(session, GHCI_VK_MEMORY, memory, device, &object);
		if (error != 0)
			return error;
		object->as.memory.type = type;
		result = memory_back(session, object, imported, bytes);
		if (result != (uint32_t)VK_SUCCESS)
			ghci_vk_object_destroy(session, object);
	}

	/* The result, and the identity when it succeeded. */
	ghci_vk_reply_created(reply, requested, result, memory);

	/* Succeeded: the memory exists, or the reply says why not. */
	return 0;
}

/*
 * Gives a memory object its memory (a new allocation, or the imported
 * resource's) and maps it into the device's GHCI session.  Returns the
 * VkResult; on a failure the object holds what it got, which its destroy
 * lets go.
 */
static uint32_t
memory_back(
	struct ghci_vk_session *session,
	struct ghci_vk_object *object,
	uint32_t imported,
	uint64_t bytes)
{
	struct ghci_allocation *allocation;
	struct ghci_resource *resource;
	struct ghci_device *device;
	int error;

	/* The memory: the resource of the import, or a new allocation below the GPU's limit. */
	device = session->device;
	allocation = NULL;
	if (imported != 0U) {
		mutex_lock(&device->lock);

		for (resource = session->owner->resources; resource != NULL; resource = resource->next) {
			if (resource->resource_id == imported)
				break;
		}
		if (resource != NULL && resource->allocation->bytes >= bytes) {
			allocation = resource->allocation;
			ghci_memory_hold(allocation);
		}

		mutex_unlock(&device->lock);
		if (allocation == NULL)
			return (uint32_t)VK_ERROR_INVALID_EXTERNAL_HANDLE;
		object->as.memory.imported = 1;
	} else {
		error = ghci_memory_alloc(device, bytes, 0U, &allocation);
		if (error != 0)
			return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
		mutex_lock(&device->lock);

		ghci_memory_hold(allocation);

		mutex_unlock(&device->lock);
	}
	object->as.memory.allocation = allocation;
	object->as.memory.bytes = bytes;

	/* The mapping into the device's session: no GPU address left, or a lost session. */
	error = ghci_vm_map(device, object->device->vm, allocation, 0U, &object->as.memory.mapping);
	if (error == ENOMEM)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (error != 0)
		return (uint32_t)VK_ERROR_DEVICE_LOST;

	/* Succeeded: the memory is the GPU's at its address in the session. */
	return (uint32_t)VK_SUCCESS;
}

/* vkFreeMemory, vkDestroyBuffer and vkDestroyImage: the destroy form, no reply body. */
static int
free_memory(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t identity;
	uint64_t device;

	/* The destroy form. */
	identity = ghci_vk_destroy_form(reader, &device);
	if (reader->error != 0)
		return EINVAL;

	/* The object, when the session has it (the null handle destroys nothing). */
	object = ghci_vk_object_find(session, kind, identity);
	if (object != NULL)
		ghci_vk_object_destroy(session, object);

	/* Succeeded: the object is gone. */
	return 0;
}

/* vkGetDeviceMemoryCommitment: [device][memory][u64 1] -> [u64 1][u64 bytes]: all of it. */
static int
commitment(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_object *object;
	uint64_t memory;

	/* The device, the memory and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	memory = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A memory object of the session. */
	object = ghci_vk_object_find(session, GHCI_VK_MEMORY, memory);
	if (object == NULL)
		return EINVAL;

	/* Every byte is committed. */
	if (requested != 0U) {
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u64(reply, object->as.memory.bytes);
	}

	/* Succeeded: the commitment is reported. */
	return 0;
}

/* vkCreateBuffer: the create form with VkBufferCreateInfo. */
static int
create_buffer(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkBufferCreateInfo info;
	struct ghci_vk_device *device;
	struct ghci_vk_object *object;
	uint64_t handle;
	uint64_t present;
	uint64_t buffer;
	uint32_t result;
	int error;

	/* The device, the create info and the tail. */
	kern_memset(&info, 0, sizeof(info));
	handle = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present == 1U)
		i915_vkc_dec_VkBufferCreateInfo(reader, &session->arena, &info);
	buffer = ghci_vk_create_tail(reader);
	if (reader->error != 0 || present != 1U)
		return EINVAL;
	device = device_of(session, handle);
	if (device == NULL)
		return EINVAL;

	/* No sparse buffer, and one no larger than the heap. */
	result = (uint32_t)VK_SUCCESS;
	if (info.size == 0U || info.size > GHCI_VK_HEAP_SIZE)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	else if ((info.flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT | VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT | VK_BUFFER_CREATE_SPARSE_ALIASED_BIT)) != 0U)
		result = (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT;

	/* The object. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(session, GHCI_VK_BUFFER, buffer, device, &object);
		if (error != 0)
			return error;
		object->as.buffer.size = info.size;
		object->as.buffer.usage = info.usage;
		object->as.buffer.flags = info.flags;
	}

	/* The result, and the identity when it succeeded. */
	ghci_vk_reply_created(reply, requested, result, buffer);

	/* Succeeded: the buffer exists, or the reply says why not. */
	return 0;
}

/* vkCreateImage: the create form with VkImageCreateInfo. */
static int
create_image(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageCreateInfo info;
	struct ghci_vk_image image;
	struct ghci_vk_device *device;
	struct ghci_vk_object *object;
	uint64_t handle;
	uint64_t present;
	uint64_t identity;
	uint32_t result;
	int error;

	/* The device, the create info and the tail. */
	kern_memset(&info, 0, sizeof(info));
	handle = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present == 1U)
		i915_vkc_dec_VkImageCreateInfo(reader, &session->arena, &info);
	identity = ghci_vk_create_tail(reader);
	if (reader->error != 0 || present != 1U)
		return EINVAL;
	device = device_of(session, handle);
	if (device == NULL)
		return EINVAL;

	/* What the image may be, and its layout. */
	kern_memset(&image, 0, sizeof(image));
	result = image_admit(session, &info, &image);

	/* The object. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(session, GHCI_VK_IMAGE, identity, device, &object);
		if (error != 0)
			return error;
		object->as.image = image;
	}

	/* The result, and the identity when it succeeded. */
	ghci_vk_reply_created(reply, requested, result, identity);

	/* Succeeded: the image exists, or the reply says why not. */
	return 0;
}

/*
 * Checks an image against what its format, type, tiling, usage and flags
 * allow (the same answer vkGetPhysicalDeviceImageFormatProperties gives)
 * and fills its record and linear layout.  Returns the VkResult.
 */
static uint32_t
image_admit(
	struct ghci_vk_session *session,
	const VkImageCreateInfo *info,
	struct ghci_vk_image *image)
{
	VkImageFormatProperties limits;
	struct ghci_vk *vk;
	uint64_t bound;
	uint32_t result;
	uint32_t bytes;
	uint32_t block;
	int error;

	/* What the format, type, tiling, usage and flags allow. */
	vk = session->device->vk;
	result = ghci_vk_caps_image_format(&session->device->caps, &vk->formats, info->format, info->imageType, info->tiling, info->usage, info->flags, &limits);
	if (result != (uint32_t)VK_SUCCESS)
		return result;

	/* The extent, the levels, the layers and the samples within them. */
	if (info->extent.width == 0U || info->extent.height == 0U || info->extent.depth == 0U)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	if (info->extent.width > limits.maxExtent.width || info->extent.height > limits.maxExtent.height)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	if (info->extent.depth > limits.maxExtent.depth)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	if (info->mipLevels == 0U || info->mipLevels > limits.maxMipLevels)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	if (info->arrayLayers == 0U || info->arrayLayers > limits.maxArrayLayers)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	if ((info->samples & limits.sampleCounts) == 0U || (info->samples & (info->samples - 1U)) != 0U)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;

	/* A bound on the bytes before the layout is computed in 32 bits: every level twice the first is more. */
	error = ghci_vk_format_size(info->format, &bytes, &block);
	if (error != 0)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	bound = ((uint64_t)info->extent.width * info->samples * bytes + GHCI_LAYOUT_ALIGN) * 2U;
	bound *= (uint64_t)info->extent.height * info->extent.depth * info->arrayLayers;
	if (bound > GHCI_VK_HEAP_SIZE * 2U)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* The record and the linear layout. */
	image->format = info->format;
	image->type = info->imageType;
	image->tiling = info->tiling;
	image->usage = info->usage;
	image->flags = info->flags;
	ghci_vk_image_layout(info->extent.width,
			     info->extent.height,
			     info->extent.depth,
			     info->mipLevels,
			     info->arrayLayers,
			     info->samples,
			     bytes,
			     block,
			     &image->layout);
	image->bytes = ghci_layout_size(&image->layout);
	if (image->bytes > GHCI_VK_HEAP_SIZE)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* Succeeded: the image may be made. */
	return (uint32_t)VK_SUCCESS;
}

/*
 * vkGetBufferMemoryRequirements and vkGetImageMemoryRequirements:
 * [device][object][u64 1] -> [u64 1][VkMemoryRequirements]: the size,
 * 256-byte alignment, and the one memory type.
 */
static int
requirements(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkMemoryRequirements record;
	struct ghci_vk_object *object;
	uint64_t identity;

	/* The device, the object and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An object of the session. */
	object = ghci_vk_object_find(session, kind, identity);
	if (object == NULL)
		return EINVAL;

	/* Its bytes, rounded, at the alignment of every buffer and image. */
	kern_memset(&record, 0, sizeof(record));
	if (kind == GHCI_VK_BUFFER)
		record.size = round_up(object->as.buffer.size, BUFFER_GRANULE);
	else
		record.size = round_up(object->as.image.bytes, GHCI_VK_ALIGN);
	record.alignment = GHCI_VK_ALIGN;
	record.memoryTypeBits = (1U << MEMORY_TYPES) - 1U;

	/* The presence word and the record. */
	if (requested != 0U) {
		drv_i915_wire_reply_u64(reply, 1U);
		i915_vkc_enc_VkMemoryRequirements(reply, &record);
	}

	/* Succeeded: the requirements are reported. */
	return 0;
}

/*
 * vkBindBufferMemory and vkBindImageMemory: [device][object][memory]
 * [u64 offset] -> [result].  The object lies wholly in the memory at an
 * aligned offset, once.
 */
static int
bind(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_object *object;
	struct ghci_vk_object *memory;
	uint64_t *bound;
	uint64_t identity;
	uint64_t handle;
	uint64_t offset;
	uint64_t bytes;
	uint64_t *at;
	uint32_t result;

	/* The device, the object, the memory and the offset. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	handle = drv_i915_wire_read_u64(reader);
	offset = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The object and the memory, of the session and of one device. */
	object = ghci_vk_object_find(session, kind, identity);
	memory = ghci_vk_object_find(session, GHCI_VK_MEMORY, handle);
	if (object == NULL || memory == NULL || object->device != memory->device)
		return EINVAL;

	/* Where the binding is kept, and the bytes it needs. */
	if (kind == GHCI_VK_BUFFER) {
		bound = &object->as.buffer.memory;
		at = &object->as.buffer.offset;
		bytes = object->as.buffer.size;
	} else {
		bound = &object->as.image.memory;
		at = &object->as.image.offset;
		bytes = object->as.image.bytes;
	}

	/* Once, aligned, and inside the memory. */
	result = (uint32_t)VK_SUCCESS;
	if (*bound != 0U || (offset % GHCI_VK_ALIGN) != 0U)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	else if (offset > memory->as.memory.bytes || bytes > memory->as.memory.bytes - offset)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (result == (uint32_t)VK_SUCCESS) {
		*bound = handle;
		*at = offset;
	}

	/* The result. */
	ghci_vk_reply_result(reply, requested, result);

	/* Succeeded: the object is bound, or the reply says why not. */
	return 0;
}

/*
 * vkGetImageSubresourceLayout: [device][image][u64 1][VkImageSubresource]
 * [u64 1] -> [u64 1][VkSubresourceLayout] of the linear layout.
 */
static int
subresource_layout(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageSubresource subresource;
	VkSubresourceLayout record;
	struct ghci_vk_object *object;
	struct ghci_layout_image *layout;
	uint64_t identity;
	uint64_t present;

	/* The device, the image, the subresource and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	kern_memset(&subresource, 0, sizeof(subresource));
	if (present == 1U)
		i915_vkc_dec_VkImageSubresource(reader, &session->arena, &subresource);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || present != 1U)
		return EINVAL;

	/* An image of the session, and a level and layer it has. */
	object = ghci_vk_object_find(session, GHCI_VK_IMAGE, identity);
	if (object == NULL)
		return EINVAL;
	layout = &object->as.image.layout;
	if (subresource.mipLevel >= layout->levels || subresource.arrayLayer >= layout->layers)
		return EINVAL;

	/* The level's place in the layer's, and its pitches. */
	kern_memset(&record, 0, sizeof(record));
	record.offset = (uint64_t)subresource.arrayLayer * ghci_layout_layer_stride(layout) +
	    ghci_layout_level_offset(layout, subresource.mipLevel);
	record.size = ghci_layout_level_bytes(layout, subresource.mipLevel);
	record.rowPitch = ghci_layout_row(layout, subresource.mipLevel);
	record.arrayPitch = ghci_layout_layer_stride(layout);
	record.depthPitch = ghci_layout_plane(layout, subresource.mipLevel);

	/* The presence word and the record. */
	if (requested != 0U) {
		drv_i915_wire_reply_u64(reply, 1U);
		i915_vkc_enc_VkSubresourceLayout(reply, &record);
	}

	/* Succeeded: the layout is reported. */
	return 0;
}

/*
 * vkGetImageSparseMemoryRequirements: [device][image][u64 1][u32 capacity]
 * [u64 capacity] -> [u64 1][u32 0][u64 0]: no sparse image.
 */
static int
sparse_requirements(
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	/* The device, the image, the presence word and the capacities. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* None. */
	if (requested != 0U) {
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u32(reply, 0U);
		drv_i915_wire_reply_u64(reply, 0U);
	}

	/* Succeeded: no requirement. */
	return 0;
}

/* Finds a device of the session by its identity; NULL when it has none. */
static struct ghci_vk_device *
device_of(
	struct ghci_vk_session *session,
	uint64_t handle)
{
	struct ghci_vk_object *object;

	/* The device's object, and its record. */
	object = ghci_vk_object_find(session, GHCI_VK_DEVICE, handle);
	if (object == NULL)
		return NULL;

	/* Succeeded: the record. */
	return object->device;
}

/* Reports a value rounded up to a granule (a power of two). */
static uint64_t
round_up(
	uint64_t value,
	uint64_t granule)
{
	/* The next multiple. */
	return (value + granule - 1U) & ~(granule - 1U);
}

/*
 * Adds a block to a device's executor heap: twice the last block (the
 * first is GHCI_VK_HEAP_FIRST_BYTES), at least least bytes, at most
 * GHCI_VK_HEAP_BLOCK_MAX, mapped into the device's session.  Returns 0,
 * ENOMEM, or EIO for a session or a device the GPU lost.
 */
static int
heap_grow(
	struct ghci_vk_session *session,
	struct ghci_vk_device *device,
	uint32_t least)
{
	struct ghci_vk_heap_block *block;
	struct ghci_allocation *allocation;
	uint64_t bytes;
	int error;

	/* The block's size. */
	bytes = GHCI_VK_HEAP_FIRST_BYTES;
	if (device->heap_bytes != 0U)
		bytes = device->heap_bytes;
	while (bytes < least)
		bytes <<= 1;
	if (bytes > GHCI_VK_HEAP_BLOCK_MAX)
		return ENOMEM;

	/* The record. */
	block = kern_calloc(1U, sizeof(*block));
	if (block == NULL)
		return ENOMEM;

	/* The memory, held by the block. */
	error = ghci_memory_alloc(session->device, bytes, 0U, &allocation);
	if (error != 0) {
		kern_free(block);
		return error;
	}
	mutex_lock(&session->device->lock);

	ghci_memory_hold(allocation);

	mutex_unlock(&session->device->lock);

	/* Its mapping in the device's session; a failure lets the memory go again. */
	error = ghci_vm_map(session->device, device->vm, allocation, 0U, &block->mapping);
	if (error != 0) {
		mutex_lock(&session->device->lock);

		ghci_memory_unhold(session->device, allocation);

		mutex_unlock(&session->device->lock);
		kern_free(block);
		return error;
	}

	/* The block's allocator, and the block first in the heap. */
	block->allocation = allocation;
	ghci_range_init(&block->map, 0U, bytes, block->extents, GHCI_VK_HEAP_EXTENTS);
	block->next = device->heap;
	device->heap = block;
	device->heap_bytes += bytes;

	/* Succeeded: the heap has room. */
	return 0;
}
