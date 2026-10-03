/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command decoder, object/handle table and opcode routing.  cmd decodes one
 * wire command and routes it to the owning module through that module's
 * i915_vk_<mod>_dispatch entry.  Contract for p002; see external-design.md
 * sections 4.2 and the routing convention every module implements.
 */

#ifndef I915_VK_CMD_H
#define I915_VK_CMD_H

#include "vk-internal.h"

/* Creates the empty per-device object table. */
int
i915_vk_object_table_create(
	struct i915_vk_object_table **out);

/* Releases the object table; object bodies are freed by their owners. */
void
i915_vk_object_table_destroy(
	struct i915_vk_object_table *table);

/* Decodes one wire command and routes it; writes any reply. */
int
i915_vk_cmd_dispatch(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/* Records a live object under its handle. */
int
i915_vk_obj_insert(
	struct i915_vk_device *vk,
	enum i915_vk_object_kind kind,
	i915_vk_handle handle,
	void *object);

/* Returns the object for a handle of the given kind, or NULL. */
void *
i915_vk_obj_lookup(
	struct i915_vk_device *vk,
	enum i915_vk_object_kind kind,
	i915_vk_handle handle);

/* Drops a handle from the table. */
void
i915_vk_obj_remove(
	struct i915_vk_device *vk,
	enum i915_vk_object_kind kind,
	i915_vk_handle handle);

/* Wire reader primitives; a failed read latches reader->error. */
uint32_t
i915_vk_read_u32(
	struct i915_vk_reader *reader);

uint64_t
i915_vk_read_u64(
	struct i915_vk_reader *reader);

i915_vk_handle
i915_vk_read_handle(
	struct i915_vk_reader *reader);

const void *
i915_vk_read_array(
	struct i915_vk_reader *reader,
	size_t count,
	size_t element);

/* Reply writer primitives; a failed write latches writer->error. */
void
i915_vk_reply_u32(
	struct i915_vk_writer *writer,
	uint32_t value);

void
i915_vk_reply_u64(
	struct i915_vk_writer *writer,
	uint64_t value);

void
i915_vk_reply_blob(
	struct i915_vk_writer *writer,
	const void *data,
	size_t bytes);

/*
 * Routing entries each module implements.  cmd calls the one owning the
 * opcode range; the module reads its arguments from reader and writes any
 * reply.  Unhandled opcodes return EINVAL.
 */
int
i915_vk_res_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

int
i915_vk_pipe_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

int
i915_vk_cmdbuf_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

int
i915_vk_sync_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

int
i915_vk_wsi_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/*
 * inst.c: instance, physical-device, device and queue commands, and the external command stream.
 * `handled` is cleared for an opcode the module does not own.
 */
int
i915_vk_inst_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply,
	int *handled);

#endif /* I915_VK_CMD_H */
