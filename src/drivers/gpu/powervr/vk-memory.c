/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's memory and buffers: VkDeviceMemory, VkBuffer, the
 * binding of a buffer to memory, and what a buffer requires.
 *
 * Every allocation takes its storage at once: a buffer of the render
 * node's own (contiguous system memory with an uncached view), mapped
 * for the core in the session's address space.  The blob libvulkan
 * creates for a host-visible allocation (its blob id is the
 * allocation's identity) maps that same buffer once more for the
 * client.  Both mappings go around the core's system level cache, as
 * every mapping the processor reaches does, so the attributes of one
 * page never differ between them.
 *
 * An allocation may declare that it is exported (its blob is then shared
 * with the display node or another session, and its mapping goes around
 * the cache for that reason too), or import a blob of the session's
 * (one the session imported from another's export): it then maps that
 * blob's buffer, holding it, instead of a buffer of its own.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/vk.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

static int allocate(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int allocate_storage(struct powervr_vk_session *session, const struct powervr_vk_memory_chain *chain, uint64_t bytes, struct powervr_resource **storage);
static int commitment(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int bind(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int requirements(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int create_buffer(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int memory_destroy(struct powervr_vk_session *session, void *payload);
static int buffer_destroy(struct powervr_vk_session *session, void *payload);

/*
 * Runs a memory, buffer, binding or requirements command; *handled stays
 * clear for another handler's opcode.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/bcm2711/vulkan-memory.c
 * and vulkan-resource.c (the wire of each command).
 */
int
powervr_vk_memory_route(
	struct powervr_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another handler. */
	(void)requested;
	*handled = 1;
	switch (opcode) {
	case GPU_OP_ALLOCATE_MEMORY:
		error = allocate(session, reader, reply);
		break;
	case GPU_OP_FREE_MEMORY:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_MEMORY);
		break;
	case GPU_OP_GET_DEVICE_MEMORY_COMMITMENT:
		error = commitment(session, reader, reply);
		break;
	case GPU_OP_BIND_BUFFER_MEMORY:
		error = bind(session, reader, reply);
		break;
	case GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS:
		error = requirements(session, reader, reply);
		break;
	case GPU_OP_CREATE_BUFFER:
		error = create_buffer(session, reader, reply);
		break;
	case GPU_OP_DESTROY_BUFFER:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_BUFFER);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran and wrote its reply. */
	return 0;
}

/*
 * Gives the buffer of a VkDeviceMemory for the blob libvulkan creates for
 * it, with a reference of the caller's.  EINVAL for an identity that
 * names no allocation or one of another size.
 */
int
powervr_vk_memory_buffer(
	struct powervr_vk_session *session,
	uint64_t identity,
	uint64_t bytes,
	struct powervr_buffer **buffer)
{
	struct powervr_vk_memory *memory;
	struct powervr_buffer *storage;

	/* The allocation the blob names, of the blob's size. */
	*buffer = NULL;
	memory = powervr_vk_payload(session, I915_VK_OBJ_MEMORY, identity);
	if (memory == NULL || memory->bytes != bytes)
		return EINVAL;

	/* The caller's reference on its buffer. */
	storage = memory->storage->buffer;
	powervr_buffer_retain(session->owner->render, storage);

	/* Succeeded: the blob maps the allocation's buffer. */
	*buffer = storage;
	return 0;
}

/* Reports a bound buffer's device address; EIO when it is not bound. */
int
powervr_vk_buffer_address(
	const struct powervr_vk_buffer *buffer,
	uint64_t *address)
{
	const struct powervr_vk_memory *memory;

	/* A buffer bound to memory. */
	if (buffer->memory == NULL)
		return EIO;
	memory = buffer->memory->payload;

	/* The memory's address moved by the buffer's offset. */
	*address = memory->storage->address + buffer->offset;

	/* Succeeded: the buffer's address. */
	return 0;
}

/*
 * vkAllocateMemory: [device][present][sType][chain] with, when chain is
 * 1, [extension sType][next][value], then [size][type][allocator]
 * [present][identity] -> [result][present][identity].  The extension
 * is an export declaration or an import of a blob of the session's
 * (powervr_vk_memory_chain).
 */
static int
allocate(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct powervr_vk_memory_chain parsed;
	struct powervr_vk_memory *memory;
	uint64_t chain;
	uint64_t bytes;
	uint64_t identity;
	uint32_t extension;
	uint32_t value;
	uint32_t type;
	int error;

	/* The device, the presence word, sType and whether an extension follows. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	chain = drv_i915_wire_read_u64(reader);

	/* The extension, when one is chained: its type and its value. */
	extension = 0;
	value = 0;
	if (chain != 0) {
		extension = drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		value = drv_i915_wire_read_u32(reader);
	}

	/* The size, the type and the identity. */
	bytes = drv_i915_wire_read_u64(reader);
	type = drv_i915_wire_read_u32(reader);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A nonempty allocation of a type, with an extension the executor takes. */
	error = 0;
	if (bytes == 0 || bytes > POWERVR_RENDER_MAX_BYTES || type >= POWERVR_VK_MEMORY_TYPES || chain > 1)
		error = EINVAL;
	if (error == 0)
		error = powervr_vk_memory_chain(chain != 0, extension, value, &parsed);

	/* The record. */
	memory = NULL;
	if (error == 0) {
		memory = kern_calloc(1, sizeof(*memory));
		if (memory == NULL)
			error = ENOMEM;
	}

	/* Its storage: a buffer of its own, or the imported blob's. */
	if (error == 0) {
		memory->bytes = bytes;
		memory->type = type;
		memory->exported = parsed.exported;
		memory->imported = parsed.imported != 0;
		error = allocate_storage(session, &parsed, bytes, &memory->storage);
	}

	/* Publishes the record and answers; a refused one is destroyed there. */
	powervr_vk_create_reply(session, reply, I915_VK_OBJ_MEMORY, identity, memory, memory_destroy, error);

	/* Succeeded: the reply carries the allocation's result. */
	return 0;
}

/*
 * Classifies an allocation's one extension: none, an export declaration
 * of a handle type the session shares by (an opaque or a dma-buf
 * handle), or an import of a blob of the session's by its identifier.
 * ENOTSUP for any other extension, EINVAL for a malformed one.
 */
int
powervr_vk_memory_chain(
	bool present,
	uint32_t extension,
	uint32_t value,
	struct powervr_vk_memory_chain *chain)
{
	/* Nothing chained: a plain allocation. */
	chain->exported = false;
	chain->imported = 0;
	if (!present)
		return 0;

	/* An export names one or both of the handle types the session shares by. */
	if (extension == VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO) {
		if (value == 0 || (value & ~POWERVR_VK_EXTERNAL_HANDLES) != 0)
			return ENOTSUP;
		chain->exported = true;
		return 0;
	}

	/* An import names a blob; zero names none. */
	if (extension == GPU_OP_RECORD_IMPORT_RESOURCE) {
		if (value == 0)
			return EINVAL;
		chain->imported = value;
		return 0;
	}

	/* Any other extension is not taken. */
	return ENOTSUP;
}

/*
 * Makes an allocation's storage: the imported blob's buffer, which must
 * hold the allocation, or a buffer of its own.  Either is mapped for the
 * core as the processor's mappings are, around the system level cache
 * (powervr_gpu_buffer_flags), which an exported one needs for the
 * display too.
 */
static int
allocate_storage(
	struct powervr_vk_session *session,
	const struct powervr_vk_memory_chain *chain,
	uint64_t bytes,
	struct powervr_resource **storage)
{
	struct powervr_render *render;
	struct powervr_resource *blob;
	struct powervr_buffer *buffer;
	int shareable;
	int error;

	/* The imported blob's buffer, with a reference of the storage's own. */
	render = session->owner->render;
	if (chain->imported != 0) {
		blob = powervr_session_blob(session->owner, chain->imported);
		if (blob == NULL || blob->buffer == NULL || bytes > blob->buffer->bytes)
			return EINVAL;
		buffer = blob->buffer;
		powervr_buffer_retain(render, buffer);
	} else {
		error = powervr_buffer_create(render, bytes, &buffer);
		if (error != 0)
			return error;
	}

	/* The session's mapping of it; the mapping takes the reference. */
	shareable = 0;
	if (chain->exported || chain->imported != 0)
		shareable = 1;
	error = powervr_resource_make(session->owner, buffer, 1, shareable, storage);
	powervr_buffer_put(render, buffer);
	if (error != 0)
		return error;

	/* Succeeded: the allocation's storage. */
	return 0;
}

/* vkGetDeviceMemoryCommitment: [device][memory][present] -> [present][bytes]; all of it. */
static int
commitment(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	const struct powervr_vk_memory *memory;
	uint64_t identity;
	uint64_t committed;

	/* The allocation between the device and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An allocation has all its storage from its start. */
	committed = 0;
	memory = powervr_vk_payload(session, I915_VK_OBJ_MEMORY, identity);
	if (memory != NULL)
		committed = memory->bytes;

	/* The bytes behind their presence word. */
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u64(reply, committed);

	/* Succeeded: the reply carries the commitment. */
	return 0;
}

/*
 * vkBindBufferMemory: [device][buffer][memory][offset] -> [result].  A
 * buffer is bound once, on the boundary, inside the allocation; it holds
 * the allocation from then on.
 */
static int
bind(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct powervr_vk_object *memory;
	struct powervr_vk_buffer *buffer;
	const struct powervr_vk_memory *storage;
	uint64_t buffer_id;
	uint64_t memory_id;
	uint64_t offset;
	uint32_t result;
	int error;

	/* The buffer, the allocation and the offset behind the device. */
	(void)drv_i915_wire_read_u64(reader);
	buffer_id = drv_i915_wire_read_u64(reader);
	memory_id = drv_i915_wire_read_u64(reader);
	offset = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An unbound buffer and an allocation that exist. */
	error = 0;
	buffer = powervr_vk_payload(session, I915_VK_OBJ_BUFFER, buffer_id);
	memory = powervr_vk_object_find(session, I915_VK_OBJ_MEMORY, memory_id);
	if (buffer == NULL || memory == NULL || buffer->memory != NULL)
		error = EINVAL;

	/* On the boundary, wholly inside the allocation. */
	if (error == 0) {
		storage = memory->payload;
		if ((offset % POWERVR_VK_RESOURCE_ALIGN) != 0 || offset > storage->bytes || buffer->size > storage->bytes - offset)
			error = EINVAL;
	}

	/* The buffer holds the allocation. */
	if (error == 0)
		error = powervr_vk_object_retain(memory);
	if (error == 0) {
		buffer->memory = memory;
		buffer->offset = offset;
	}

	/* The bind's result. */
	result = powervr_vk_result(error);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/*
 * vkGetBufferMemoryRequirements: [device][buffer][present] -> [present]
 * [VkMemoryRequirements].  Whole runs of the boundary, of either type.
 */
static int
requirements(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkMemoryRequirements record;
	const struct powervr_vk_buffer *buffer;
	uint64_t identity;
	uint64_t bytes;

	/* The buffer between the device and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* How many bytes the buffer occupies; an unknown one needs no memory of any type. */
	bytes = 0;
	buffer = powervr_vk_payload(session, I915_VK_OBJ_BUFFER, identity);
	if (buffer != NULL)
		bytes = buffer->size;

	/* The requirements. */
	kern_memset(&record, 0, sizeof(record));
	record.size = (bytes + POWERVR_VK_RESOURCE_ALIGN - 1U) & ~(uint64_t)(POWERVR_VK_RESOURCE_ALIGN - 1U);
	record.alignment = POWERVR_VK_RESOURCE_ALIGN;
	record.memoryTypeBits = 0;
	if (bytes != 0)
		record.memoryTypeBits = (1U << POWERVR_VK_MEMORY_DEVICE) | (1U << POWERVR_VK_MEMORY_HOST);

	/* The requirements behind their presence word. */
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkMemoryRequirements(reply, &record);

	/* Succeeded: the reply carries the requirements. */
	return 0;
}

/* vkCreateBuffer: [device][present][VkBufferCreateInfo] and the create's tail -> the create's reply. */
static int
create_buffer(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkBufferCreateInfo info;
	struct powervr_vk_buffer *buffer;
	uint64_t identity;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkBufferCreateInfo(reader, &session->arena, &info);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A buffer of some bytes, not sparse. */
	error = 0;
	if (info.size == 0 || info.size > POWERVR_RENDER_MAX_BYTES) {
		error = EINVAL;
	} else if (info.flags != 0) {
		error = ENOTSUP;
	}

	/* The buffer keeps its size and usage; it has no storage until it is bound. */
	buffer = NULL;
	if (error == 0) {
		buffer = kern_calloc(1, sizeof(*buffer));
		if (buffer != NULL) {
			buffer->size = info.size;
			buffer->usage = info.usage;
		}
	}

	/* Publishes the buffer and answers. */
	powervr_vk_create_reply(session, reply, I915_VK_OBJ_BUFFER, identity, buffer, buffer_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* Destroys an allocation nothing holds: its mapping and its hold on the buffer go. */
static int
memory_destroy(
	struct powervr_vk_session *session,
	void *payload)
{
	struct powervr_vk_memory *memory;

	/* The storage, when it was made, then the record. */
	memory = payload;
	if (memory->storage != NULL)
		powervr_resource_unmake(session->owner, memory->storage);
	kern_free(memory);

	/* Succeeded: the allocation is gone. */
	return 0;
}

/* Destroys a buffer nothing holds: it lets its allocation go. */
static int
buffer_destroy(
	struct powervr_vk_session *session,
	void *payload)
{
	struct powervr_vk_buffer *buffer;
	int error;

	/* The allocation, then the record. */
	(void)session;
	buffer = payload;
	error = powervr_vk_object_release(buffer->memory);
	kern_free(buffer);
	if (error != 0)
		return error;

	/* Succeeded: the buffer is gone. */
	return 0;
}
