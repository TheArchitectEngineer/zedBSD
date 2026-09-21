/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Bounded writing of a batch buffer (see batch.h).
 */

#include "batch.h"

#include <stdint.h>

#include "../intel/commands.h"
#include "../intel/genxml.h"

/* The dwords of a Gen12 PIPE_CONTROL: the header, the flags and four address and data words. */
#define I915_PIPE_CONTROL_DWORDS	6U

/*
 * Appends one dword to a batch.
 *
 * A dword that does not fit is not written; the batch is marked overflowed
 * and still counted.
 */
void
drv_i915_batch_emit(
	struct i915_gfx_batch *batch,
	uint32_t dword)
{
	/* Writes the dword while there is room, and remembers the overflow otherwise. */
	if (batch->count < batch->capacity) {
		batch->cmds[batch->count] = dword;
	} else {
		batch->overflow = 1;
	}

	/* Counts the dword either way, so the needed length stays known. */
	batch->count++;
}

/*
 * Appends a 3D state packet whose body is all zeros.
 *
 * The header carries the opcode and the packet's total dword count.
 */
void
drv_i915_batch_zero(
	struct i915_gfx_batch *batch,
	uint32_t opcode,
	uint32_t dwords)
{
	uint32_t index;

	/* Writes the packet header. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(opcode, dwords));

	/* Zeroes every body dword. */
	for (index = 1U; index < dwords; index++)
		drv_i915_batch_emit(batch, 0U);
}

/*
 * Appends a run of prepared dwords.
 */
void
drv_i915_batch_words(
	struct i915_gfx_batch *batch,
	const uint32_t *words,
	unsigned count)
{
	unsigned index;

	/* Copies the words in order. */
	for (index = 0U; index < count; index++)
		drv_i915_batch_emit(batch, words[index]);
}

/*
 * Appends a two-dword state pointer packet.
 *
 * The value is the pointer itself, an offset from the base the packet's
 * state is relative to, with any flag bits the packet defines.
 */
void
drv_i915_batch_pointer(
	struct i915_gfx_batch *batch,
	uint32_t opcode,
	uint32_t value)
{
	/* Writes the header and the pointer. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(opcode, GEN12_3DSTATE_POINTERS_DWORDS));
	drv_i915_batch_emit(batch, value);
}

/*
 * Appends a PIPE_CONTROL with the given flags and no post-sync write.
 *
 * A render target cache flush also asks for the HDC pipeline flush in the
 * header dword, as the Gen12 flush of the render target requires.
 */
void
drv_i915_batch_pipe_control(
	struct i915_gfx_batch *batch,
	uint32_t flags)
{
	uint32_t header;
	unsigned index;

	/* Builds the header; a render target flush adds the HDC pipeline flush to it. */
	header = GFX_OP_PIPE_CONTROL(6);
	if ((flags & PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH) != 0U)
		header |= PIPE_CONTROL0_HDC_PIPELINE_FLUSH;

	/* Writes the header and the flags. */
	drv_i915_batch_emit(batch, header);
	drv_i915_batch_emit(batch, flags);

	/* Zeroes the address and the immediate data: nothing is written after the sync. */
	for (index = 2U; index < I915_PIPE_CONTROL_DWORDS; index++)
		drv_i915_batch_emit(batch, 0U);
}
