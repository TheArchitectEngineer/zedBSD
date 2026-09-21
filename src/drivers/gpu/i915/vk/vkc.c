/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Wire primitives and the per-command arena under the generated struct codec (see vkc.h).
 */

#include "vkc.h"

/* Returns `count` zeroed elements from the command's arena, or NULL with the reader failed. */
void *
i915_vkc_array(
	struct i915_vk_reader *reader,
	struct i915_vk_arena *arena,
	uint64_t count,
	size_t element)
{
	size_t bytes;
	size_t start;

	if (reader->error != 0)
		return NULL;

	/*
	 * Every element occupies at least one byte of the command, so a count beyond the bytes that
	 * remain is malformed and must not size an allocation.
	 */
	if (count > (uint64_t)(reader->size - reader->offset) || element == 0U ||
	    count > (uint64_t)(SIZE_MAX / element)) {
		reader->error = 1;
		return NULL;
	}

	bytes = ((size_t)count * element + 7U) & ~(size_t)7U;
	start = (arena->used + 7U) & ~(size_t)7U;
	if (arena->base == NULL || start > arena->size || bytes > arena->size - start) {
		reader->error = 1;
		return NULL;
	}

	arena->used = start + bytes;
	memset(arena->base + start, 0, bytes);
	return arena->base + start;
}

/* Reads a string (NULL for an absent one) into the arena. */
const char *
i915_vkc_read_string(
	struct i915_vk_reader *reader,
	struct i915_vk_arena *arena)
{
	uint64_t bytes;
	char *text;

	bytes = i915_vk_read_u64(reader);
	if (reader->error != 0 || bytes == 0U)
		return NULL;

	text = i915_vkc_array(reader, arena, bytes, 1U);
	if (text == NULL)
		return NULL;

	i915_vkc_read_bytes(reader, text, (size_t)bytes);
	if (reader->error != 0)
		return NULL;

	/* The length includes the terminator; a sender that omitted it does not get to overrun. */
	text[bytes - 1U] = '\0';
	return text;
}

/* Reads `bytes` bytes and the padding that rounds them to four. */
void
i915_vkc_read_bytes(
	struct i915_vk_reader *reader,
	void *destination,
	size_t bytes)
{
	size_t padded;

	if (reader->error != 0)
		return;

	if (bytes > SIZE_MAX - 3U) {
		reader->error = 1;
		return;
	}

	padded = (bytes + 3U) & ~(size_t)3U;
	if (padded > reader->size - reader->offset) {
		reader->error = 1;
		return;
	}

	if (bytes != 0U)
		memcpy(destination, reader->base + reader->offset, bytes);
	reader->offset += padded;
}

/* Reads a float as its bits; no floating-point register is involved. */
void
i915_vkc_read_float(
	struct i915_vk_reader *reader,
	float *destination)
{
	uint32_t bits;

	bits = i915_vk_read_u32(reader);
	memcpy(destination, &bits, sizeof(bits));
}

/* Skips the one optional external-memory declaration libvulkan may chain to a buffer or an image. */
void
i915_vkc_skip_external_chain(
	struct i915_vk_reader *reader)
{
	uint64_t present;

	/* wire.c: [present] then, when present, [sType][pNext = 0][handle types]. */
	present = i915_vk_read_u64(reader);
	if (present == 0U)
		return;

	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
}

/* Writes `bytes` bytes and the padding that rounds them to four. */
void
i915_vkc_reply_bytes(
	struct i915_vk_writer *writer,
	const void *source,
	size_t bytes)
{
	static const uint8_t zero[4] = { 0U, 0U, 0U, 0U };
	size_t padded;

	padded = (bytes + 3U) & ~(size_t)3U;
	i915_vk_reply_blob(writer, source, bytes);
	if (padded != bytes)
		i915_vk_reply_blob(writer, zero, padded - bytes);
}

/* Writes a float as its bits. */
void
i915_vkc_reply_float(
	struct i915_vk_writer *writer,
	const float *source)
{
	uint32_t bits;

	memcpy(&bits, source, sizeof(bits));
	i915_vk_reply_u32(writer, bits);
}
