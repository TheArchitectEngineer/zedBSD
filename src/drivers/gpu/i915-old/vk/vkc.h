/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's side of libvulkan's struct codec (WS031 E-127).
 *
 * codec-generated.inc is derived from userland/base/libvulkan/codec.c by
 * plan/ws031/handover/tools/gen_vk_server_codec.py: a decoder for every record the library encodes and
 * an encoder for every record it decodes.  This header carries what those generated functions stand
 * on: the per-command arena the decoded pointers live in, and the byte / string / float primitives of
 * the wire (userland/base/libvulkan/wire.c: byte runs are padded to four, a string is its length
 * including the terminator followed by its bytes, a float travels as its 32 bits).
 *
 * A module that wants the codec includes this header and then codec-generated.inc; every generated
 * function is static and unused ones are dropped.
 */

#ifndef I915_VK_VKC_H
#define I915_VK_VKC_H

#include "vk-internal.h"
#include "cmd.h"

#include <vulkan/vulkan_core.h>

#include <string.h>

/* Returns `count` zeroed elements from the command's arena, or NULL with the reader failed. */
void *
i915_vkc_array(
	struct i915_vk_reader *reader,
	struct i915_vk_arena *arena,
	uint64_t count,
	size_t element);

/* Reads a string (NULL for an absent one) into the arena. */
const char *
i915_vkc_read_string(
	struct i915_vk_reader *reader,
	struct i915_vk_arena *arena);

/* Reads `bytes` bytes and the padding that rounds them to four. */
void
i915_vkc_read_bytes(
	struct i915_vk_reader *reader,
	void *destination,
	size_t bytes);

/* Reads a float as its bits; no floating-point register is involved. */
void
i915_vkc_read_float(
	struct i915_vk_reader *reader,
	float *destination);

/* Skips the one optional external-memory declaration libvulkan may chain to a buffer or an image. */
void
i915_vkc_skip_external_chain(
	struct i915_vk_reader *reader);

/* Writes `bytes` bytes and the padding that rounds them to four. */
void
i915_vkc_reply_bytes(
	struct i915_vk_writer *writer,
	const void *source,
	size_t bytes);

/* Writes a float as its bits. */
void
i915_vkc_reply_float(
	struct i915_vk_writer *writer,
	const float *source);

#endif /* I915_VK_VKC_H */
