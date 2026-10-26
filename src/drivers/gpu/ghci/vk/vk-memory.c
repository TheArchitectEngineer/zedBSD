/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's memory, buffers and images, and the host's side of the
 * fences, events and queries.
 *
 * vkAllocateMemory is the driver's (R'): it takes the GPU memory and
 * tells the firmware the memory object's address with GHCI_MEMORY.  The
 * buffers and the images are the firmware's objects, relayed as they
 * came, but the driver keeps what their memory requirements and layouts
 * are computed from (layout.h, which the firmware computes the same way).
 * vkFreeMemory is relayed and the memory goes back after the session's
 * last fence.
 *
 * The fences are the driver's: a fence a submission signals waits for the
 * device's fence after that submission.  An event's word and a query
 * pool's slots are in the firmware's heap, at the address the firmware
 * reports when it makes them; the host reads and writes them there.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "../frame.h"
#include "vk.h"
#include "vk-engine.h"

#include "drivers/gpu/i915/vk/vk-codec.inc"

/* The flags of GHCI_MEMORY: the memory is host visible, and it is another session's. */
#define MEMORY_HOST_VISIBLE		0x1U
#define MEMORY_IMPORTED			0x2U

/* The memory types an object may use: both. */
#define MEMORY_TYPE_BITS		((1U << GHCI_VK_MEMORY_TYPES) - 1U)

/* The alignment of a buffer that a uniform block may be read from, and of any other. */
#define BUFFER_UNIFORM_ALIGN		256U
#define BUFFER_ALIGN			64U

/* The handle types an allocation may be exported by: an opaque descriptor and a dma-buf. */
#define EXTERNAL_HANDLES		0x201U

/* A query pool slot in the firmware's heap: {u64 result, u32 available, u32 0}. */
#define QUERY_SLOT_BYTES		16U

/* The VkResult of a status the GPU has not reached, and of an event's states. */
#define RESULT_NOT_READY		1U
#define RESULT_EVENT_SET		3U
#define RESULT_EVENT_RESET		4U

static int allocate_memory(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int import_memory(struct ghci_vk_build *build, uint32_t flags, uint32_t resource, uint64_t size, uint32_t type, uint64_t memory);
static int stage_memory(struct ghci_vk_build *build, uint32_t flags, uint64_t memory, const struct ghci_allocation *allocation, uint32_t memory_flags, uint32_t *index);
static int free_memory(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int memory_commitment(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int relay_with_result(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, size_t start);
static int bind_memory(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int create_buffer(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int create_image(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int destroy_kept(struct ghci_vk_build *build, uint32_t opcode, enum ghci_vk_kind kind, uint32_t flags, struct i915_wire_reader *reader);
static int create_reply(struct ghci_vk_build *build, uint32_t flags, uint32_t index, uint64_t identity);
static int buffer_requirements(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int image_requirements(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int image_sparse_requirements(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int subresource_layout(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int image_layout(const struct ghci_vk_image *image, struct ghci_layout_image *layout);
static int create_fence(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int destroy_fence(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int reset_fences(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int fence_status(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int create_heap_object(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int event_access(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int query_results(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static volatile uint32_t *heap_words(struct ghci_device *device, uint32_t address, uint32_t bytes);

/*
 * Runs a memory, buffer, image, fence, event or query command; *handled
 * stays clear for another route's number.
 */
int
ghci_vk_memory_route(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another route. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_ALLOCATE_MEMORY:
		error = allocate_memory(build, flags, reader);
		break;
	case GPU_OP_FREE_MEMORY:
		error = free_memory(build, flags, reader);
		break;
	case GPU_OP_GET_DEVICE_MEMORY_COMMITMENT:
		error = memory_commitment(build, flags, reader);
		break;
	case GPU_OP_BIND_BUFFER_MEMORY:
	case GPU_OP_BIND_IMAGE_MEMORY:
		error = bind_memory(build, opcode, flags, reader);
		break;
	case GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS:
		error = buffer_requirements(build, flags, reader);
		break;
	case GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS:
		error = image_requirements(build, flags, reader);
		break;
	case GPU_OP_GET_IMAGE_SPARSE_MEMORY_REQUIREMENTS:
		error = image_sparse_requirements(build, flags, reader);
		break;
	case GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT:
		error = subresource_layout(build, flags, reader);
		break;
	case GPU_OP_CREATE_BUFFER:
		error = create_buffer(build, flags, reader);
		break;
	case GPU_OP_DESTROY_BUFFER:
		error = destroy_kept(build, opcode, GHCI_VK_BUFFER, flags, reader);
		break;
	case GPU_OP_CREATE_IMAGE:
		error = create_image(build, flags, reader);
		break;
	case GPU_OP_DESTROY_IMAGE:
		error = destroy_kept(build, opcode, GHCI_VK_IMAGE, flags, reader);
		break;
	case GPU_OP_CREATE_FENCE:
		error = create_fence(build, flags, reader);
		break;
	case GPU_OP_DESTROY_FENCE:
		error = destroy_fence(build, reader);
		break;
	case GPU_OP_RESET_FENCES:
		error = reset_fences(build, flags, reader);
		break;
	case GPU_OP_GET_FENCE_STATUS:
		error = fence_status(build, flags, reader);
		break;
	case GPU_OP_CREATE_EVENT:
	case GPU_OP_CREATE_QUERY_POOL:
		error = create_heap_object(build, opcode, flags, reader);
		break;
	case GPU_OP_DESTROY_EVENT:
		error = destroy_kept(build, opcode, GHCI_VK_EVENT, flags, reader);
		break;
	case GPU_OP_DESTROY_QUERY_POOL:
		error = destroy_kept(build, opcode, GHCI_VK_QUERY_POOL, flags, reader);
		break;
	case GPU_OP_GET_EVENT_STATUS:
	case GPU_OP_SET_EVENT:
	case GPU_OP_RESET_EVENT:
		error = event_access(build, opcode, flags, reader);
		break;
	case GPU_OP_GET_QUERY_POOL_RESULTS:
		error = query_results(build, flags, reader);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/*
 * Finds the allocation of a session's VkDeviceMemory, for the blob that maps it.
 */
int
ghci_vk_memory_blob(
	struct ghci_device *device,
	struct ghci_session *owner,
	uint64_t identity,
	struct ghci_allocation **allocation)
{
	struct ghci_vk_object *object;

	(void)device;

	/* A session with an executor and a memory object of that identity. */
	if (owner->vk == NULL)
		return EINVAL;
	object = ghci_vk_object_find(owner->vk, GHCI_VK_MEMORY, identity);
	if (object == NULL)
		return EINVAL;

	/* Only memory the processor reaches can be mapped. */
	if (object->as.memory.allocation->view == NULL)
		return EINVAL;

	/* Succeeded: the memory's allocation. */
	*allocation = object->as.memory.allocation;
	return 0;
}

/*
 * Writes a query copy's results into a reply: for each query its result
 * (32 or 64 bits) and, when asked, its availability; VK_NOT_READY when a
 * query is not available and partial results were not asked for.
 */
void
ghci_vk_query_copy(
	struct ghci_device *device,
	struct ghci_vk_session *session,
	const struct ghci_vk_patch *patch,
	uint8_t *view)
{
	struct ghci_vk_object *object;
	volatile uint32_t *slots;
	uint8_t *data;
	uint32_t result;
	uint32_t query;
	uint32_t low;
	uint32_t high;
	uint32_t available;
	uint32_t lost;
	uint32_t at;
	int wide;

	/* The pool's slots in the heap. */
	object = ghci_vk_object_find(session, GHCI_VK_QUERY_POOL, patch->identity);
	slots = NULL;
	if (object != NULL)
		slots = heap_words(device, object->as.heap.address, object->as.heap.count * QUERY_SLOT_BYTES);

	/* A pool that went meanwhile reports the loss. */
	if (slots == NULL) {
		lost = (uint32_t)VK_ERROR_DEVICE_LOST;
		kern_memcpy(view + patch->offset, &lost, 4U);
		return;
	}

	/* Each query's result (64 bits when asked for) and availability at its stride. */
	result = (uint32_t)VK_SUCCESS;
	wide = 0;
	if ((patch->flags & VK_QUERY_RESULT_64_BIT) != 0U)
		wide = 1;
	data = view + patch->offset + 12U;
	for (query = 0U; query < patch->count; query++) {
		low = slots[(patch->first + query) * 4U];
		high = slots[(patch->first + query) * 4U + 1U];
		available = slots[(patch->first + query) * 4U + 2U];
		at = query * patch->stride;

		/* A query not yet available, without partial results, writes no result. */
		if (!available && (patch->flags & VK_QUERY_RESULT_PARTIAL_BIT) == 0U) {
			result = RESULT_NOT_READY;
		} else if (wide && at + 8U <= patch->bytes) {
			kern_memcpy(data + at, &low, 4U);
			kern_memcpy(data + at + 4U, &high, 4U);
		} else if (at + 4U <= patch->bytes) {
			kern_memcpy(data + at, &low, 4U);
		}

		/* The availability word after the result, when asked for. */
		if ((patch->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != 0U) {
			at += 4U;
			if (wide)
				at += 4U;
			if (at + 4U <= patch->bytes)
				kern_memcpy(data + at, &available, 4U);
		}
	}

	/* The result word. */
	kern_memcpy(view + patch->offset, &result, 4U);
}

/*
 * vkAllocateMemory: [device][u64 1][u32 sType][u64 chain] and, with a
 * chain, [u32 sType][u64 0][u32 value], then [u64 size][u32 type][u64 0]
 * [u64 1][memory] -> [result][u64 1][memory].  The driver takes the GPU
 * memory and stages GHCI_MEMORY [u64 memory][u64 address][u64 bytes]
 * [u32 flags] instead of the command; the reply's VkResult is the
 * firmware's answer to it.
 */
static int
allocate_memory(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	struct ghci_allocation *allocation;
	enum ghci_memory_use use;
	uint32_t memory_flags;
	uint64_t chained;
	uint64_t size;
	uint64_t memory;
	uint32_t extension;
	uint32_t value;
	uint32_t type;
	uint32_t index;
	uint32_t result;
	int error;

	/* The device, the info's presence word, its sType and its chain. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	chained = drv_i915_wire_read_u64(reader);
	extension = 0U;
	value = 0U;
	if (chained != 0U) {
		extension = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		value = drv_i915_wire_read_u32(reader);
	}

	/* The size, the type and the create tail. */
	size = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	memory = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The identities of session protection's records are the driver's. */
	if ((memory & GHCI_VK_PROT_IDENTITY) != 0U)
		return EINVAL;

	/* An import names a blob of the session; an export the handle types it is shared by. */
	if (chained != 0U && extension == GPU_OP_RECORD_IMPORT_RESOURCE) {
		error = import_memory(build, flags, value, size, type, memory);
		if (error != 0)
			return error;

		/* Succeeded: the memory is imported. */
		return 0;
	}

	/* Another chain than the export is not taken. */
	if (chained != 0U && extension != VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO)
		return ENOTSUP;
	if (chained != 0U &&
	    (value == 0U ||
	     (value & ~EXTERNAL_HANDLES) != 0U))
		return ENOTSUP;

	/* A type of the device. */
	result = (uint32_t)VK_SUCCESS;
	if (type >= GHCI_VK_MEMORY_TYPES)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* Host-visible memory is memory the processor reaches; the other may be the GPU's alone. */
	use = GHCI_USE_DEVICE;
	if (type == GHCI_VK_MEMORY_HOST)
		use = GHCI_USE_MAPPABLE;

	/* The memory, cleared and on the session's list. */
	allocation = NULL;
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_allocation_make(build->session->device, build->session->owner, use, size, 0, &allocation);
		if (error != 0)
			result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	/* The driver's record of the memory object. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(build->session, GHCI_VK_MEMORY, memory, &object);
		if (error != 0) {
			ghci_allocation_drop(build->session->device, build->session->owner, allocation);
			result = (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;
		}
	}

	/* An allocation that failed is answered here; nothing goes to the firmware. */
	if (result != (uint32_t)VK_SUCCESS) {
		if (flags != 0U)
			drv_i915_wire_reply_u32(&build->reply, result);
		return 0;
	}

	/* The record keeps the allocation, its size and its type. */
	object->as.memory.allocation = allocation;
	object->as.memory.bytes = size;
	object->as.memory.type = type;

	/* GHCI_MEMORY: the identity, the GPU address, the bytes and whether the host sees it. */
	memory_flags = 0U;
	if (type == GHCI_VK_MEMORY_HOST)
		memory_flags = MEMORY_HOST_VISIBLE;
	error = stage_memory(build, flags, memory, allocation, memory_flags, &index);
	if (error != 0)
		return error;

	/* The firmware's result, the presence word and the identity. */
	error = create_reply(build, flags, index, memory);
	if (error != 0)
		return error;

	/* Succeeded: the memory is the session's. */
	return 0;
}

/*
 * Imports another session's memory a blob of the session shows (the
 * import record's resource id): the memory object holds that memory, and
 * the firmware is told its address as GHCI_MEMORY with the import flag.
 */
static int
import_memory(
	struct ghci_vk_build *build,
	uint32_t flags,
	uint32_t resource,
	uint64_t size,
	uint32_t type,
	uint64_t memory)
{
	struct ghci_vk_object *object;
	struct ghci_allocation *blob;
	struct ghci_allocation *backing;
	uint32_t memory_flags;
	uint32_t index;
	int error;

	/* A blob of the session that holds the whole size. */
	blob = ghci_allocation_blob(build->session->owner, resource);
	if (blob == NULL)
		return EINVAL;
	if (size > blob->requested)
		return EINVAL;
	if (type >= GHCI_VK_MEMORY_TYPES)
		return EINVAL;

	/* The memory it shows: the import's, or the blob's own. */
	backing = blob;
	if (blob->backing != NULL)
		backing = blob->backing;

	/* The memory object holds it. */
	error = ghci_vk_object_add(build->session, GHCI_VK_MEMORY, memory, &object);
	if (error != 0)
		return error;
	ghci_memory_hold(backing);
	object->as.memory.allocation = backing;
	object->as.memory.bytes = size;
	object->as.memory.type = type;
	object->as.memory.imported = 1;

	/* GHCI_MEMORY with the import flag. */
	memory_flags = MEMORY_IMPORTED;
	if (type == GHCI_VK_MEMORY_HOST)
		memory_flags |= MEMORY_HOST_VISIBLE;
	error = stage_memory(build, flags, memory, backing, memory_flags, &index);
	if (error != 0)
		return error;

	/* The firmware's result, the presence word and the identity. */
	error = create_reply(build, flags, index, memory);
	if (error != 0)
		return error;

	/* Succeeded: the memory is imported. */
	return 0;
}

/* Stages GHCI_MEMORY [u64 memory][u64 GPU address][u64 bytes][u32 flags]. */
static int
stage_memory(
	struct ghci_vk_build *build,
	uint32_t flags,
	uint64_t memory,
	const struct ghci_allocation *allocation,
	uint32_t memory_flags,
	uint32_t *index)
{
	uint32_t arguments[7];
	int error;

	/* The identity, the GPU address, the bytes and the flags. */
	arguments[0] = (uint32_t)memory;
	arguments[1] = (uint32_t)(memory >> 32);
	arguments[2] = allocation->gpu_address;
	arguments[3] = 0U;
	arguments[4] = allocation->bytes;
	arguments[5] = 0U;
	arguments[6] = memory_flags;
	error = ghci_vk_stage(build, GPU_OP_GHCI_MEMORY, flags, arguments, sizeof(arguments), index);
	if (error != 0)
		return error;

	/* Succeeded: the record is staged. */
	return 0;
}

/*
 * vkFreeMemory: [device][memory][u64 0], no reply body.  The firmware
 * forgets the memory object; its memory goes back after the session's
 * last fence, which is the fence after this frame.
 */
static int
free_memory(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t memory;
	uint32_t index;
	size_t start;
	int error;

	/* The destroy form, relayed; not of an identity of session protection's records (the driver's). */
	start = reader->offset;
	memory = ghci_vk_destroy_form(reader);
	if ((memory & GHCI_VK_PROT_IDENTITY) != 0U)
		return EINVAL;
	error = ghci_vk_relay(build, GPU_OP_FREE_MEMORY, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The memory object, when the session has it. */
	object = ghci_vk_object_find(build->session, GHCI_VK_MEMORY, memory);
	if (object == NULL)
		return 0;

	/* An imported memory is let go with its record (which unholds it after the work so far). */
	if (object->as.memory.imported) {
		ghci_vk_object_remove(build->session, object);
		return 0;
	}

	/* Its memory goes back after the frame; the record goes now. */
	error = ghci_vk_release_later(build, object->as.memory.allocation);
	if (error != 0)
		return error;
	ghci_vk_object_remove(build->session, object);

	/* Succeeded: the memory is freed. */
	return 0;
}

/* vkGetDeviceMemoryCommitment: [device][memory][u64 1] -> [u64 1][u64 bytes]: memory is never lazy. */
static int
memory_commitment(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t memory;
	uint64_t bytes;

	/* The device, the memory and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	memory = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The whole of the memory is committed. */
	bytes = 0U;
	object = ghci_vk_object_find(build->session, GHCI_VK_MEMORY, memory);
	if (object != NULL)
		bytes = object->as.memory.bytes;

	/* The presence word and the bytes. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, bytes);
	}

	/* Succeeded: the commitment is reported. */
	return 0;
}

/* Relays a command whose reply is [result] alone, the firmware's. */
static int
relay_with_result(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	size_t start)
{
	uint32_t index;
	int error;

	/* The command as it came. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The firmware's VkResult in the reply. */
	if (flags != 0U) {
		error = ghci_vk_reply_result(build, index);
		if (error != 0)
			return error;
	}

	/* Succeeded: the command is relayed. */
	return 0;
}

/* vkBindBufferMemory and vkBindImageMemory: [device][object][memory][u64 offset] -> [result], relayed. */
static int
bind_memory(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	size_t start;
	int error;

	/* The device, the object, the memory and the offset. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);

	/* The firmware binds the object; the reply is its result. */
	error = relay_with_result(build, opcode, flags, reader, start);
	if (error != 0)
		return error;

	/* Succeeded: the bind is relayed. */
	return 0;
}

/* vkCreateBuffer: the create form with VkBufferCreateInfo, relayed; the driver keeps the size and usage. */
static int
create_buffer(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkBufferCreateInfo info;
	struct ghci_vk_object *object;
	uint64_t present;
	uint64_t buffer;
	uint32_t index;
	size_t start;
	int error;

	/* The create form. */
	start = reader->offset;
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	i915_vkc_dec_VkBufferCreateInfo(reader, &build->session->arena, &info);
	buffer = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The driver's record: what the memory requirements are computed from. */
	error = ghci_vk_object_add(build->session, GHCI_VK_BUFFER, buffer, &object);
	if (error != 0)
		return error;
	object->as.buffer.size = info.size;
	object->as.buffer.usage = info.usage;

	/* The firmware makes the buffer. */
	error = ghci_vk_relay(build, GPU_OP_CREATE_BUFFER, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* Its result, the presence word and the identity. */
	error = create_reply(build, flags, index, buffer);
	if (error != 0)
		return error;

	/* Succeeded: the buffer is relayed. */
	return 0;
}

/* vkCreateImage: the create form with VkImageCreateInfo, relayed; the driver keeps its shape. */
static int
create_image(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkImageCreateInfo info;
	struct ghci_vk_object *object;
	uint64_t present;
	uint64_t image;
	uint32_t index;
	size_t start;
	int error;

	/* The create form. */
	start = reader->offset;
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	i915_vkc_dec_VkImageCreateInfo(reader, &build->session->arena, &info);
	image = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The driver's record: what the layout and the memory requirements are computed from. */
	error = ghci_vk_object_add(build->session, GHCI_VK_IMAGE, image, &object);
	if (error != 0)
		return error;
	object->as.image.format = info.format;
	object->as.image.type = info.imageType;
	object->as.image.width = info.extent.width;
	object->as.image.height = info.extent.height;
	object->as.image.depth = info.extent.depth;
	object->as.image.levels = info.mipLevels;
	object->as.image.layers = info.arrayLayers;
	object->as.image.samples = info.samples;
	object->as.image.tiling = info.tiling;
	object->as.image.usage = info.usage;

	/* The firmware makes the image. */
	error = ghci_vk_relay(build, GPU_OP_CREATE_IMAGE, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* Its result, the presence word and the identity. */
	error = create_reply(build, flags, index, image);
	if (error != 0)
		return error;

	/* Succeeded: the image is relayed. */
	return 0;
}

/* Relays the destroy form of an object the driver keeps a record of, and forgets the record. */
static int
destroy_kept(
	struct ghci_vk_build *build,
	uint32_t opcode,
	enum ghci_vk_kind kind,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t identity;
	uint32_t index;
	size_t start;
	int error;

	/* The destroy form, relayed: the reply is the echo alone. */
	start = reader->offset;
	identity = ghci_vk_destroy_form(reader);
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The record, when the session has one. */
	object = ghci_vk_object_find(build->session, kind, identity);
	if (object != NULL)
		ghci_vk_object_remove(build->session, object);

	/* Succeeded: the object is destroyed. */
	return 0;
}

/* Writes the reply of the create form: [result][u64 1][identity], the result the firmware's. */
static int
create_reply(
	struct ghci_vk_build *build,
	uint32_t flags,
	uint32_t index,
	uint64_t identity)
{
	int error;

	/* A command that wants no reply has none. */
	if (flags == 0U)
		return 0;

	/* The firmware's result, then the identity libvulkan chose. */
	error = ghci_vk_reply_result(build, index);
	if (error != 0)
		return error;
	drv_i915_wire_reply_u64(&build->reply, 1U);
	drv_i915_wire_reply_u64(&build->reply, identity);

	/* Succeeded: the reply waits for the result. */
	return 0;
}

/*
 * vkGetBufferMemoryRequirements: [device][buffer][u64 1] -> [u64 1]
 * [requirements].  Whole lines, a uniform buffer at the uniform offset
 * alignment, in either memory type.
 */
static int
buffer_requirements(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkMemoryRequirements record;
	struct ghci_vk_object *object;
	uint64_t buffer;

	/* The device, the buffer and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	buffer = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A buffer the session has. */
	object = ghci_vk_object_find(build->session, GHCI_VK_BUFFER, buffer);
	if (object == NULL)
		return EINVAL;

	/* The requirements. */
	kern_memset(&record, 0, sizeof(record));
	record.alignment = BUFFER_ALIGN;
	if ((object->as.buffer.usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) != 0U)
		record.alignment = BUFFER_UNIFORM_ALIGN;
	record.size = (object->as.buffer.size + record.alignment - 1U) & ~(record.alignment - 1U);
	record.memoryTypeBits = MEMORY_TYPE_BITS;

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkMemoryRequirements(&build->reply, &record);
	}

	/* Succeeded: the requirements are reported. */
	return 0;
}

/* vkGetImageMemoryRequirements: [device][image][u64 1] -> [u64 1][requirements], from the layout. */
static int
image_requirements(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkMemoryRequirements record;
	struct ghci_layout_image layout;
	struct ghci_vk_object *object;
	uint64_t image;
	int error;

	/* The device, the image and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	image = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An image the session has, of a format the driver lays out. */
	object = ghci_vk_object_find(build->session, GHCI_VK_IMAGE, image);
	if (object == NULL)
		return EINVAL;
	error = image_layout(&object->as.image, &layout);
	if (error != 0)
		return error;

	/* The whole image, aligned as the layout aligns its planes. */
	kern_memset(&record, 0, sizeof(record));
	record.size = ghci_layout_size(&layout);
	record.alignment = GHCI_LAYOUT_ALIGN;
	record.memoryTypeBits = MEMORY_TYPE_BITS;

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkMemoryRequirements(&build->reply, &record);
	}

	/* Succeeded: the requirements are reported. */
	return 0;
}

/* vkGetImageSparseMemoryRequirements: [device][image][u64 1][u32 capacity][u64 capacity] -> none. */
static int
image_sparse_requirements(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	/* The device, the image, the presence word and the capacities. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* No sparse requirement: [u64 1][u32 0][u64 0]. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u64(&build->reply, 0U);
	}

	/* Succeeded: the image has no sparse binding. */
	return 0;
}

/*
 * vkGetImageSubresourceLayout: [device][image][u64 1][VkImageSubresource]
 * [u64 1] -> [u64 1][VkSubresourceLayout], from the layout: every image
 * is laid out in linear rows.
 */
static int
subresource_layout(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkImageSubresource subresource;
	VkSubresourceLayout record;
	struct ghci_layout_image layout;
	struct ghci_vk_object *object;
	uint64_t image;
	int error;

	/* The device, the image, the subresource and the output's presence word. */
	kern_memset(&subresource, 0, sizeof(subresource));
	(void)drv_i915_wire_read_u64(reader);
	image = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageSubresource(reader, &build->session->arena, &subresource);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An image the session has, of a format the driver lays out. */
	object = ghci_vk_object_find(build->session, GHCI_VK_IMAGE, image);
	if (object == NULL)
		return EINVAL;
	error = image_layout(&object->as.image, &layout);
	if (error != 0)
		return error;

	/* A level and a layer of the image. */
	if (subresource.mipLevel >= layout.levels)
		return EINVAL;
	if (subresource.arrayLayer >= layout.layers)
		return EINVAL;

	/* The level of the layer: its place, its bytes and its pitches. */
	kern_memset(&record, 0, sizeof(record));
	record.offset = (uint64_t)ghci_layout_layer_stride(&layout) * subresource.arrayLayer;
	record.offset += ghci_layout_level_offset(&layout, subresource.mipLevel);
	record.size = ghci_layout_level_bytes(&layout, subresource.mipLevel);
	record.rowPitch = ghci_layout_row(&layout, subresource.mipLevel);
	record.arrayPitch = ghci_layout_layer_stride(&layout);
	record.depthPitch = ghci_layout_plane(&layout, subresource.mipLevel);

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkSubresourceLayout(&build->reply, &record);
	}

	/* Succeeded: the layout is reported. */
	return 0;
}

/* Describes an image for layout.h: EINVAL for a format the driver does not lay out. */
static int
image_layout(
	const struct ghci_vk_image *image,
	struct ghci_layout_image *layout)
{
	uint32_t block;
	uint32_t attachment_usage;

	/* The texel's bytes. */
	kern_memset(layout, 0, sizeof(*layout));
	layout->texel_bytes = ghci_vk_format_bytes(image->format, &block);
	if (layout->texel_bytes == 0U)
		return EINVAL;

	/* The extent, the levels, the layers and the samples. */
	layout->block = block;
	layout->width = image->width;
	layout->height = image->height;
	layout->depth = image->depth;
	layout->levels = image->levels;
	layout->layers = image->layers;
	layout->samples = image->samples;

	/* An image that may be an attachment has even rows. */
	attachment_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	if ((image->usage & attachment_usage) != 0U)
		layout->attachment = 1U;

	/* An empty shape is no image. */
	if (layout->width == 0U || layout->height == 0U)
		return EINVAL;
	if (layout->depth == 0U || layout->levels == 0U)
		return EINVAL;
	if (layout->layers == 0U || layout->samples == 0U)
		return EINVAL;

	/* Succeeded: the image is described. */
	return 0;
}

/* vkCreateFence: the create form with the fence's info -> [VK_SUCCESS][u64 1][fence]; the driver's. */
static int
create_fence(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkFenceCreateInfo info;
	struct ghci_vk_object *object;
	uint64_t present;
	uint64_t fence;
	uint32_t result;
	int error;

	/* The create form. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	i915_vkc_dec_VkFenceCreateInfo(reader, &build->session->arena, &info);
	fence = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The fence, signaled when it was made so. */
	result = (uint32_t)VK_SUCCESS;
	error = ghci_vk_object_add(build->session, GHCI_VK_FENCE, fence, &object);
	if (error == ENOMEM)
		return error;
	if (error != 0)
		result = (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;
	if (error == 0 && (info.flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0U)
		object->as.fence.signaled = 1;

	/* The result, and the identity when it succeeded. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, result);
		if (result == (uint32_t)VK_SUCCESS) {
			drv_i915_wire_reply_u64(&build->reply, 1U);
			drv_i915_wire_reply_u64(&build->reply, fence);
		}
	}

	/* Succeeded: the fence exists, or the reply says why not. */
	return 0;
}

/* vkDestroyFence: the destroy form, no reply body. */
static int
destroy_fence(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t fence;

	/* The destroy form. */
	fence = ghci_vk_destroy_form(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The fence, when the session has it. */
	object = ghci_vk_object_find(build->session, GHCI_VK_FENCE, fence);
	if (object != NULL)
		ghci_vk_object_remove(build->session, object);

	/* Succeeded: the fence is destroyed. */
	return 0;
}

/* vkResetFences: [device][u32 count][u64 count][fences] -> [VK_SUCCESS]. */
static int
reset_fences(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t count;
	uint64_t index;
	uint64_t fence;

	/* The device and the counts. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Each fence goes back to unsignaled, with no submission. */
	for (index = 0U; index < count; index++) {
		fence = drv_i915_wire_read_u64(reader);
		if (reader->error != 0)
			return EINVAL;

		/* A fence the session does not have is skipped. */
		object = ghci_vk_object_find(build->session, GHCI_VK_FENCE, fence);
		if (object == NULL)
			continue;

		object->as.fence.signaled = 0;
		object->as.fence.pending = 0;
	}

	/* Success. */
	if (flags != 0U)
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);

	/* Succeeded: the fences are reset. */
	return 0;
}

/*
 * vkGetFenceStatus: [device][fence] -> [result]: VK_SUCCESS once the
 * submission that signals it has finished, VK_NOT_READY before, and
 * VK_ERROR_DEVICE_LOST when a reset ended that submission.
 */
static int
fence_status(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	struct ghci_device *device;
	uint64_t fence;
	uint32_t result;
	int done;

	/* The device and the fence. */
	(void)drv_i915_wire_read_u64(reader);
	fence = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The fence's state. */
	device = build->session->device;
	object = ghci_vk_object_find(build->session, GHCI_VK_FENCE, fence);
	result = RESULT_NOT_READY;
	if (object == NULL) {
		result = (uint32_t)VK_ERROR_DEVICE_LOST;
	} else if (object->as.fence.signaled) {
		result = (uint32_t)VK_SUCCESS;
	} else if (object->as.fence.pending) {
		/* A submission signals it: done when the fence after it passed, lost when a reset ended it. */
		done = ghci_cp_done(device, object->as.fence.ticket.fence, object->as.fence.ticket.generation);
		if (object->as.fence.ticket.generation != device->generation) {
			result = (uint32_t)VK_ERROR_DEVICE_LOST;
		} else if (done) {
			object->as.fence.signaled = 1;
			object->as.fence.pending = 0;
			result = (uint32_t)VK_SUCCESS;
		}
	}

	/* The result. */
	if (flags != 0U)
		drv_i915_wire_reply_u32(&build->reply, result);

	/* Succeeded: the status is reported. */
	return 0;
}

/*
 * vkCreateEvent and vkCreateQueryPool: the create form, relayed; the
 * firmware's result gives the address of the object's words in its heap,
 * which the driver keeps for the host's reads.
 */
static int
create_heap_object(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkEventCreateInfo event_info;
	VkQueryPoolCreateInfo pool_info;
	struct ghci_vk_object *object;
	enum ghci_vk_patch_kind patch_kind;
	enum ghci_vk_kind kind;
	uint64_t present;
	uint64_t identity;
	uint32_t index;
	size_t start;
	int error;

	/* The create form of the object. */
	start = reader->offset;
	kern_memset(&event_info, 0, sizeof(event_info));
	kern_memset(&pool_info, 0, sizeof(pool_info));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	kind = GHCI_VK_EVENT;
	patch_kind = GHCI_VK_PATCH_EVENT;
	if (opcode == GPU_OP_CREATE_QUERY_POOL) {
		i915_vkc_dec_VkQueryPoolCreateInfo(reader, &build->session->arena, &pool_info);
		kind = GHCI_VK_QUERY_POOL;
		patch_kind = GHCI_VK_PATCH_QUERY_POOL;
	} else {
		i915_vkc_dec_VkEventCreateInfo(reader, &build->session->arena, &event_info);
	}

	/* The tail and its identity. */
	identity = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The driver's record, whose address comes with the result. */
	error = ghci_vk_object_add(build->session, kind, identity, &object);
	if (error != 0)
		return error;
	object->as.heap.count = pool_info.queryCount;

	/* The firmware makes the object. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;
	error = ghci_vk_patch_address(build, patch_kind, index, identity);
	if (error != 0)
		return error;

	/* Its result, the presence word and the identity. */
	error = create_reply(build, flags, index, identity);
	if (error != 0)
		return error;

	/* Succeeded: the object is relayed. */
	return 0;
}

/*
 * vkGetEventStatus, vkSetEvent and vkResetEvent: [device][event] ->
 * [result].  The event's word in the heap is 1 when it is set.
 */
static int
event_access(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	volatile uint32_t *word;
	uint64_t event;
	uint32_t result;

	/* The device and the event. */
	(void)drv_i915_wire_read_u64(reader);
	event = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The event's word, once the firmware said where it is. */
	word = NULL;
	object = ghci_vk_object_find(build->session, GHCI_VK_EVENT, event);
	if (object != NULL && object->as.heap.known)
		word = heap_words(build->session->device, object->as.heap.address, 4U);

	/* Reads, sets or resets it. */
	result = (uint32_t)VK_ERROR_DEVICE_LOST;
	if (word != NULL && opcode == GPU_OP_GET_EVENT_STATUS) {
		result = RESULT_EVENT_RESET;
		if (*word == 1U)
			result = RESULT_EVENT_SET;
	} else if (word != NULL && opcode == GPU_OP_SET_EVENT) {
		*word = 1U;
		result = (uint32_t)VK_SUCCESS;
	} else if (word != NULL) {
		*word = 0U;
		result = (uint32_t)VK_SUCCESS;
	}

	/* The result. */
	if (flags != 0U)
		drv_i915_wire_reply_u32(&build->reply, result);

	/* Succeeded: the event is reported. */
	return 0;
}

/*
 * vkGetQueryPoolResults: [device][pool][u32 first][u32 count][u64 bytes]
 * [u64 bytes][u64 stride][u32 flags] -> [result][u64 bytes][the bytes].
 * The results are copied from the pool's slots when the frame completes;
 * with VK_QUERY_RESULT_WAIT_BIT the frame first waits for the session's
 * work to finish.
 */
static int
query_results(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	struct ghci_vk_patch *patch;
	uint64_t pool;
	uint64_t bytes;
	uint64_t stride;
	uint32_t first;
	uint32_t count;
	uint32_t query_flags;
	uint32_t offset;
	int error;

	/* The device, the pool, the range, the bytes, the stride and the flags. */
	(void)drv_i915_wire_read_u64(reader);
	pool = drv_i915_wire_read_u64(reader);
	first = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u32(reader);
	bytes = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	stride = drv_i915_wire_read_u64(reader);
	query_flags = drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A pool the firmware placed, and a range inside it that the bytes hold. */
	object = ghci_vk_object_find(build->session, GHCI_VK_QUERY_POOL, pool);
	if (object == NULL || !object->as.heap.known)
		return EINVAL;
	if (first > object->as.heap.count || count > object->as.heap.count - first)
		return EINVAL;
	if (stride == 0U || stride > 0xffffU)
		return EINVAL;
	if ((stride & 3U) != 0U)
		return EINVAL;
	if (count != 0U && bytes < stride * (count - 1U) + 4U)
		return EINVAL;
	if (bytes > 0xffffffU || (bytes & 3U) != 0U)
		return EINVAL;

	/* A command that wants no reply copies nothing. */
	if (flags == 0U)
		return 0;

	/* The result word and the bytes, laid down now and filled at the frame's end. */
	offset = (uint32_t)(build->reply.base - build->reply_blob->view) + (uint32_t)build->reply.offset;
	drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);
	drv_i915_wire_reply_u64(&build->reply, bytes);
	if (build->reply.error != 0)
		return EINVAL;
	if (bytes > build->reply.size - build->reply.offset)
		return EINVAL;
	kern_memset(build->reply.base + build->reply.offset, 0, (size_t)bytes);
	build->reply.offset += (size_t)bytes;

	/* The copy, at the frame's end. */
	error = ghci_vk_patch_add(build, &patch);
	if (error != 0)
		return error;
	patch->kind = GHCI_VK_PATCH_QUERY_RESULTS;
	patch->offset = offset;
	patch->identity = pool;
	patch->first = first;
	patch->count = count;
	patch->stride = (uint32_t)stride;
	patch->flags = query_flags;
	patch->bytes = (uint32_t)bytes;

	/* Waiting for the results waits for the session's work. */
	if ((query_flags & VK_QUERY_RESULT_WAIT_BIT) != 0U)
		build->wait_fence = 1;

	/* Succeeded: the results are copied when the frame completes. */
	return 0;
}

/* Reports the processor's view of a run of the firmware's heap, or NULL for a run outside it. */
static volatile uint32_t *
heap_words(
	struct ghci_device *device,
	uint32_t address,
	uint32_t bytes)
{
	struct ghci_allocation *heap;

	/* A word-aligned run inside the heap the processor reaches. */
	heap = device->vk->heap;
	if (heap->view == NULL)
		return NULL;
	if ((address & 3U) != 0U)
		return NULL;
	if (address < heap->gpu_address)
		return NULL;
	if (bytes > heap->bytes)
		return NULL;
	if (address - heap->gpu_address > heap->bytes - bytes)
		return NULL;

	/* Succeeded: the view of the run. */
	return (volatile uint32_t *)(void *)(heap->view + (address - heap->gpu_address));
}
