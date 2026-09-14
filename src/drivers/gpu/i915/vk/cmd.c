/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command decoder, object/handle table and opcode routing.
 *
 * libvulkan frames every command as a little-endian u32 opcode, a u32
 * reply-request flag, then the encoded parameters (see userland/base/libvulkan
 * wire.c vulkan_command_begin).  cmd reads the header, routes the opcode to the
 * owning module's dispatch entry, and offers the object table and the wire
 * reader/writer primitives every module shares.
 */

#include "vk-internal.h"
#include "cmd.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/* One live Vulkan object under its wire handle. */
struct i915_vk_object_entry {
	enum i915_vk_object_kind kind;
	i915_vk_handle handle;
	void *object;
};

/* The per-device object table: a growable flat array scanned linearly. */
struct i915_vk_object_table {
	struct i915_vk_object_entry *entries;
	unsigned count;
	unsigned capacity;
};

static enum i915_vk_object_kind i915_vk_route(uint32_t opcode);
static int i915_vk_cmd_builtin(struct i915_vk_session *session, uint32_t opcode, struct i915_vk_reader *reader, struct i915_vk_writer *reply);

/* Creates the empty object table for a device. */
int
i915_vk_object_table_create(
	struct i915_vk_object_table **out)
{
	struct i915_vk_object_table *table;

	/* The caller receives nothing on failure. */
	*out = NULL;

	table = kern_calloc(1U, sizeof(*table));
	if (table == NULL)
		return ENOMEM;

	/* An empty table owns no storage until the first insert. */
	table->entries = NULL;
	table->count = 0U;
	table->capacity = 0U;

	/* Succeeded: the device can register objects. */
	*out = table;
	return 0;
}

/* Releases the object table; object bodies are freed by their owners. */
void
i915_vk_object_table_destroy(
	struct i915_vk_object_table *table)
{
	/* A never-created table is nothing to release. */
	if (table == NULL)
		return;

	/* Only the index storage belongs to the table. */
	if (table->entries != NULL)
		kern_free(table->entries);

	kern_free(table);
}

/* Records a live object under its handle, replacing any prior entry. */
int
i915_vk_obj_insert(
	struct i915_vk_device *vk,
	enum i915_vk_object_kind kind,
	i915_vk_handle handle,
	void *object)
{
	struct i915_vk_object_table *table;
	struct i915_vk_object_entry *grown;
	unsigned capacity;
	unsigned index;

	/* A reused handle of the same kind overwrites in place. */
	table = vk->objects;
	for (index = 0U; index < table->count; index++) {
		if (table->entries[index].kind != kind)
			continue;
		if (table->entries[index].handle != handle)
			continue;

		table->entries[index].object = object;
		return 0;
	}

	/* The index grows geometrically so inserts amortize to constant time. */
	if (table->count == table->capacity) {
		capacity = table->capacity == 0U ? 16U : table->capacity * 2U;
		grown = kern_calloc(capacity, sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;

		/* The existing entries move to the larger array before it replaces them. */
		if (table->entries != NULL) {
			memcpy(grown, table->entries, (size_t)table->count * sizeof(*grown));
			kern_free(table->entries);
		}

		table->entries = grown;
		table->capacity = capacity;
	}

	/* The new entry takes the next free slot. */
	table->entries[table->count].kind = kind;
	table->entries[table->count].handle = handle;
	table->entries[table->count].object = object;
	table->count++;

	/* Succeeded: the handle now resolves to this object. */
	return 0;
}

/* Returns the object for a handle of the given kind, or NULL. */
void *
i915_vk_obj_lookup(
	struct i915_vk_device *vk,
	enum i915_vk_object_kind kind,
	i915_vk_handle handle)
{
	struct i915_vk_object_table *table;
	unsigned index;

	/* A linear scan is enough at the baseline object counts. */
	table = vk->objects;
	for (index = 0U; index < table->count; index++) {
		if (table->entries[index].kind != kind)
			continue;
		if (table->entries[index].handle != handle)
			continue;

		return table->entries[index].object;
	}

	return NULL;
}

/* Drops a handle from the table by moving the last entry into its slot. */
void
i915_vk_obj_remove(
	struct i915_vk_device *vk,
	enum i915_vk_object_kind kind,
	i915_vk_handle handle)
{
	struct i915_vk_object_table *table;
	unsigned index;

	table = vk->objects;
	for (index = 0U; index < table->count; index++) {
		if (table->entries[index].kind != kind)
			continue;
		if (table->entries[index].handle != handle)
			continue;

		table->entries[index] = table->entries[table->count - 1U];
		table->count--;
		return;
	}
}

/* Reads one little-endian wire word; a short read latches the error. */
uint32_t
i915_vk_read_u32(
	struct i915_vk_reader *reader)
{
	const uint8_t *bytes;
	uint32_t word;

	/* A prior error keeps every later read from advancing. */
	if (reader->error != 0)
		return 0U;

	/* A word that would run past the end fails the whole command. */
	if (reader->offset + 4U > reader->size) {
		reader->error = 1;
		return 0U;
	}

	/* Wire order is little endian independent of the host. */
	bytes = reader->base + reader->offset;
	word = (uint32_t)bytes[0];
	word |= (uint32_t)bytes[1] << 8;
	word |= (uint32_t)bytes[2] << 16;
	word |= (uint32_t)bytes[3] << 24;
	reader->offset += 4U;

	return word;
}

/* Reads one little-endian 64-bit wire word. */
uint64_t
i915_vk_read_u64(
	struct i915_vk_reader *reader)
{
	uint64_t low;
	uint64_t high;

	/* Two words compose the value; either short read latches the error. */
	low = i915_vk_read_u32(reader);
	high = i915_vk_read_u32(reader);

	return low | (high << 32);
}

/* Reads a wire handle, which is one 64-bit word. */
i915_vk_handle
i915_vk_read_handle(
	struct i915_vk_reader *reader)
{
	return i915_vk_read_u64(reader);
}

/* Returns a pointer to count*element wire bytes and advances past them. */
const void *
i915_vk_read_array(
	struct i915_vk_reader *reader,
	size_t count,
	size_t element)
{
	const void *data;
	size_t bytes;

	/* A prior error yields nothing. */
	if (reader->error != 0)
		return NULL;

	/* The span must lie inside the command. */
	bytes = count * element;
	if (reader->offset + bytes > reader->size) {
		reader->error = 1;
		return NULL;
	}

	/* The caller reads in place; the reader advances past the span. */
	data = reader->base + reader->offset;
	reader->offset += bytes;

	return data;
}

/* Appends one little-endian word to the reply; overflow latches the error. */
void
i915_vk_reply_u32(
	struct i915_vk_writer *writer,
	uint32_t value)
{
	uint8_t *bytes;

	/* A prior error or a full reply stops further writes. */
	if (writer->error != 0)
		return;
	if (writer->offset + 4U > writer->size) {
		writer->error = 1;
		return;
	}

	/* The reply uses the same wire byte order as the request. */
	bytes = writer->base + writer->offset;
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
	writer->offset += 4U;
}

/* Appends one 64-bit word to the reply. */
void
i915_vk_reply_u64(
	struct i915_vk_writer *writer,
	uint64_t value)
{
	/* Low word first matches the reader's composition order. */
	i915_vk_reply_u32(writer, (uint32_t)value);
	i915_vk_reply_u32(writer, (uint32_t)(value >> 32));
}

/* Appends raw bytes to the reply. */
void
i915_vk_reply_blob(
	struct i915_vk_writer *writer,
	const void *data,
	size_t bytes)
{
	/* A prior error or an overflow drops the blob whole. */
	if (writer->error != 0)
		return;
	if (writer->offset + bytes > writer->size) {
		writer->error = 1;
		return;
	}

	memcpy(writer->base + writer->offset, data, bytes);
	writer->offset += bytes;
}

/* Decodes one command header and routes the opcode to its module. */
int
i915_vk_cmd_dispatch(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	enum i915_vk_object_kind route;
	uint32_t opcode;
	uint32_t reply_requested;

	/* The header is the opcode and a reply-request flag. */
	opcode = i915_vk_read_u32(reader);
	reply_requested = i915_vk_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/*
	 * A reply-requested command opens its reply with the echoed opcode; the
	 * owning module then appends the VkResult and any output parameters, so
	 * the reply reads back as libvulkan expects (opcode, result, payload).
	 */
	if (reply_requested != 0)
		i915_vk_reply_u32(reply, opcode);

	/* The opcode range selects the owning module; NONE means cmd handles it. */
	route = i915_vk_route(opcode);
	switch (route) {
	case I915_VK_OBJ_MEMORY:
		return i915_vk_res_dispatch(session, opcode, reader, reply);
	case I915_VK_OBJ_PIPELINE:
		return i915_vk_pipe_dispatch(session, opcode, reader, reply);
	case I915_VK_OBJ_COMMAND_BUFFER:
		return i915_vk_cmdbuf_dispatch(session, opcode, reader, reply);
	case I915_VK_OBJ_FENCE:
		return i915_vk_sync_dispatch(session, opcode, reader, reply);
	case I915_VK_OBJ_SWAPCHAIN:
		return i915_vk_wsi_dispatch(session, opcode, reader, reply);
	default:
		return i915_vk_cmd_builtin(session, opcode, reader, reply);
	}
}

/* Maps an opcode to the object kind naming its owning module's dispatch. */
static enum i915_vk_object_kind
i915_vk_route(
	uint32_t opcode)
{
	/* Fences, semaphores, events and queries belong to sync. */
	if (opcode >= 35U && opcode <= 49U)
		return I915_VK_OBJ_FENCE;

	/* Memory, buffer, image, sampler and descriptor objects belong to res. */
	if (opcode >= 21U && opcode <= 34U)
		return I915_VK_OBJ_MEMORY;
	if (opcode >= 50U && opcode <= 58U)
		return I915_VK_OBJ_MEMORY;
	if (opcode >= 70U && opcode <= 79U)
		return I915_VK_OBJ_MEMORY;

	/* Shader modules, pipelines, framebuffers and render passes belong to pipe. */
	if (opcode >= 59U && opcode <= 69U)
		return I915_VK_OBJ_PIPELINE;
	if (opcode >= 80U && opcode <= 84U)
		return I915_VK_OBJ_PIPELINE;

	/* Command pools, command buffers, every vkCmd* and queue submit are cmdbuf's. */
	if (opcode == 18U)		/* vkQueueSubmit */
		return I915_VK_OBJ_COMMAND_BUFFER;
	if (opcode >= 85U && opcode <= 136U)
		return I915_VK_OBJ_COMMAND_BUFFER;

	/* Everything else (instance/device/queue/version) is handled by cmd. */
	return I915_VK_OBJ_NONE;
}

/*
 * vkSetReplyCommandStreamMESA: [present][resource][offset][capacity].  Points
 * the reply writer at the named session resource so command replies land in the
 * shared region libvulkan reads back (the submit ioctl returns no inline reply).
 */
static int
i915_vk_cmd_set_reply(
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	(void)i915_vk_read_u64(reader);			/* present */
	(void)i915_vk_read_u32(reader);			/* resource */
	(void)i915_vk_read_u64(reader);			/* offset */
	(void)i915_vk_read_u64(reader);			/* capacity */
	if (reader->error != 0)
		return EINVAL;

	/* The i915 command path already targeted the writer; the cursor restarts. */
	reply->offset = 0U;
	return 0;
}

/*
 * vkSeekReplyCommandStreamMESA: [offset].  Moves the reply cursor so the
 * following version probe writes the completion trailer at its fixed position.
 */
static int
i915_vk_cmd_seek_reply(
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	uint64_t offset;

	offset = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (offset > reply->size)
		return EINVAL;

	reply->offset = offset;
	return 0;
}

/* Handles the transport and reflective commands cmd owns directly. */
static int
i915_vk_cmd_builtin(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	/* The reply-stream transport selects and seeks the shared reply resource. */
	if (opcode == 178U)
		return i915_vk_cmd_set_reply(reader, reply);
	if (opcode == 179U)
		return i915_vk_cmd_seek_reply(reader, reply);

	/*
	 * vkEnumerateInstanceVersion reports the version and doubles as the trailer
	 * completion probe: [result][pApiVersion present][apiVersion], the version
	 * word published last.
	 */
	if (opcode == 137U) {
		(void)i915_vk_read_u64(reader);			/* pApiVersion present */
		i915_vk_reply_u32(reply, 0U);			/* VK_SUCCESS */
		i915_vk_reply_u64(reply, 1U);			/* pApiVersion present */
		i915_vk_reply_u32(reply, (1U << 22) | (1U << 12));	/* apiVersion 1.1.0 */
		return 0;
	}

	/* Remaining builtin commands are accepted without a reply for now. */
	(void)session;
	(void)reader;
	return 0;
}
