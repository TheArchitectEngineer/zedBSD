/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Logical ring contexts: image construction, descriptors, ELSQ
 * submission and context status buffer decoding.
 *
 * A context image is one page of per-process status followed by a
 * register state page the engine restores as a batch of
 * MI_LOAD_REGISTER_IMM pairs.  References: Linux intel_lrc.c
 * set_offsets, init_common_regs, init_ppgtt_regs, __reset_stop_ring,
 * lrc_update_regs and lrc_descriptor; intel_execlists_submission.c
 * write_desc, execlists_submit_ports, reset_csb_pointers, csb_read
 * and gen12_csb_parse, re-expressed.
 */

#include "internal.h"
#include "linux/i915-workarounds.inc"

#include <kern/device-io.h>
#include <kern/klog.h>

#include <errno.h>
#include <string.h>

#include "linux/i915-commands.inc"
#include "linux/i915-lrc-offsets.inc"

/* A masked register value carries the changed bits in its upper half. */
#define I915_LRC_MASKED_ENABLE(bits)	(((bits) << 16) | (bits))
#define I915_LRC_MASKED_DISABLE(bits)	((bits) << 16)

/* Gen12 keeps the MI_MODE pair at this state index and the batch state pair at 0x70. */
#define I915_LRC_MI_MODE_INDEX		0x60U
#define I915_LRC_BB_OFFSET_INDEX	0x70U

/* Every unmapped status-buffer entry reads as all ones until the engine writes it. */
#define I915_CSB_UNWRITTEN		0xffffffffffffffffULL

static uint32_t i915_lrc_render_power_state(struct i915_device *device);
static unsigned i915_lrc_set_offsets(uint32_t *regs, const uint8_t *data, uint32_t base);
static void i915_lrc_init_regs(struct i915_context *context, struct i915_engine *engine);
static uint64_t i915_lrc_csb_read(struct i915_engine *engine, unsigned index);
static unsigned i915_lrc_csb_parse(uint64_t entry);

/*
 * Builds a context image and its ring for one engine and address space.
 */
int
drv_i915_lrc_create(
	struct i915_device *device,
	struct i915_engine *engine,
	struct i915_ppgtt *vm,
	uint32_t sw_id,
	struct i915_context *context)
{
	uint64_t image_bytes;
	uint32_t descriptor;
	int error;

	/* A context is created once per engine and session. */
	if (context->created != 0U)
		return EBUSY;
	memset(context, 0, sizeof(*context));
	context->engine = engine;
	context->vm = vm;
	context->sw_id = sw_id;

	/* Render contexts carry far more state than copy contexts. */
	image_bytes = GEN8_LR_CONTEXT_OTHER_SIZE;
	if (engine->class == I915_CLASS_RENDER)
		image_bytes = GEN11_LR_CONTEXT_RENDER_SIZE;

	/* The image lives in the GGTT because the engine names it by GGTT offset. */
	error = drv_i915_gem_create(device, image_bytes, &context->image);
	if (error != 0)
		return error;
	error = drv_i915_gem_bind_ggtt(device, context->image);
	if (error != 0) {
		drv_i915_lrc_destroy(device, context);
		return error;
	}

	/* The ring is also GGTT-mapped; the image's RING_START names it. */
	error = drv_i915_gem_create(device, I915_RING_BYTES, &context->ring);
	if (error != 0) {
		drv_i915_lrc_destroy(device, context);
		return error;
	}
	error = drv_i915_gem_bind_ggtt(device, context->ring);
	if (error != 0) {
		drv_i915_lrc_destroy(device, context);
		return error;
	}

	/* The state page follows the per-process status page. */
	context->state = (uint32_t *)((uint8_t *)context->image->address + LRC_STATE_OFFSET);
	context->ring_dwords = context->ring->address;
	context->ring_tail = 0U;
	i915_lrc_init_regs(context, engine);

	/* The descriptor: 48-bit addressing, valid, privileged, naming the image. */
	descriptor = I915_LEGACY_64B_CONTEXT << GEN8_CTX_ADDRESSING_MODE_SHIFT;
	descriptor |= GEN8_CTX_VALID | GEN8_CTX_PRIVILEGE;
	descriptor |= context->image->ggtt_offset;
	context->descriptor_low = descriptor;

	/* The upper half carries the software context id and the engine's class and instance. */
	context->descriptor_high = (sw_id << (GEN11_SW_CTX_ID_SHIFT - 32U)) | engine->ccid;
	context->created = 1U;
	kern_io_write_barrier();

	/* Succeeded: the context can be submitted once its ring holds commands. */
	return 0;
}

/*
 * Releases a context's ring and image after the engine stopped using them.
 */
void
drv_i915_lrc_destroy(
	struct i915_device *device,
	struct i915_context *context)
{
	/* The ring is unmapped and freed first; the image may still be read by a reset path. */
	if (context->ring != NULL) {
		drv_i915_gem_unbind_ggtt(device, context->ring);
		drv_i915_gem_destroy(device, context->ring);
		context->ring = NULL;
	}

	if (context->image != NULL) {
		drv_i915_gem_unbind_ggtt(device, context->image);
		drv_i915_gem_destroy(device, context->image);
		context->image = NULL;
	}

	context->state = NULL;
	context->ring_dwords = NULL;
	context->created = 0U;
}

/*
 * Loads the context into the engine's submit queue with its current ring tail.
 *
 * Both ports are written so the queue never keeps a stale second entry; the
 * force-restore bit makes the engine reload the image even when the same
 * context ran last.
 */
void
drv_i915_lrc_submit(
	struct i915_context *context)
{
	struct i915_engine *engine;
	struct i915_device *device;
	uint32_t low;

	engine = context->engine;
	device = engine->device;

	/* The image tail tells the engine how far the ring is filled. */
	context->state[CTX_RING_TAIL] = context->ring_tail;
	kern_io_write_barrier();

	/* Port one is cleared; port zero carries this context. */
	low = context->descriptor_low | (uint32_t)CTX_DESC_FORCE_RESTORE;
	drv_i915_write32(device, RING_EXECLIST_SQ_CONTENTS(engine->base) + 8U, 0U);
	drv_i915_write32(device, RING_EXECLIST_SQ_CONTENTS(engine->base) + 12U, 0U);
	drv_i915_write32(device, RING_EXECLIST_SQ_CONTENTS(engine->base), low);
	drv_i915_write32(device, RING_EXECLIST_SQ_CONTENTS(engine->base) + 4U, context->descriptor_high);

	/* The load bit hands the queue to the engine. */
	drv_i915_write32(device, RING_EXECLIST_CONTROL(engine->base), EL_CTRL_LOAD);
}

/*
 * Resets the status buffer pointers after an engine reset or at bring-up.
 */
void
drv_i915_lrc_reset_csb(
	struct i915_engine *engine)
{
	struct i915_device *device;
	uint32_t reset_value;
	unsigned index;

	device = engine->device;
	reset_value = I915_CSB_ENTRIES - 1U;

	/* Writing both pointers to the last entry makes the engine start at entry zero. */
	drv_i915_write32(device, RING_CONTEXT_STATUS_PTR(engine->base), (0xffffU << 16) | (reset_value << 8) | reset_value);
	(void)drv_i915_read32(device, RING_CONTEXT_STATUS_PTR(engine->base));

	/* The driver's head and the status-page write pointer mirror the register. */
	engine->csb_head = reset_value;
	engine->status[I915_CSB_WRITE_INDEX] = reset_value;

	/* Unwritten entries are all ones so a stale entry is detectable. */
	for (index = 0U; index < I915_CSB_ENTRIES; index++) {
		engine->status[I915_HWS_CSB_BUF0_INDEX + 2U * index] = 0xffffffffU;
		engine->status[I915_HWS_CSB_BUF0_INDEX + 2U * index + 1U] = 0xffffffffU;
	}

	kern_io_write_barrier();

	/* A second write settles hardware that ignored the first. */
	drv_i915_write32(device, RING_CONTEXT_STATUS_PTR(engine->base), (0xffffU << 16) | (reset_value << 8) | reset_value);
	(void)drv_i915_read32(device, RING_CONTEXT_STATUS_PTR(engine->base));
}

/*
 * Consumes every new status-buffer entry and counts context switches.
 *
 * Returns the number of completion events (a context left the engine) and
 * stores the number of promotions (a context started) for the caller.
 */
unsigned
drv_i915_lrc_csb_consume(
	struct i915_engine *engine,
	unsigned *promotions)
{
	unsigned head;
	unsigned tail;
	unsigned completions;
	unsigned promoted;
	uint64_t entry;

	/* The engine publishes its write pointer in the status page. */
	*promotions = 0U;
	completions = 0U;
	head = engine->csb_head;
	tail = engine->status[I915_CSB_WRITE_INDEX] & 0xffU;
	if (head == tail)
		return 0U;

	/* Entries are read only after the write pointer, never speculatively before it. */
	kern_io_read_barrier();

	/* Each entry is one context switch event between head and tail. */
	while (head != tail) {
		head++;
		if (head == I915_CSB_ENTRIES)
			head = 0U;

		/* An entry the engine never wrote means the pointer ran ahead of the data. */
		entry = i915_lrc_csb_read(engine, head);
		if (entry == I915_CSB_UNWRITTEN) {
			engine->csb_errors++;
			continue;
		}

		/* A promotion starts a context; anything else ends the running one. */
		promoted = i915_lrc_csb_parse(entry);
		if (promoted != 0U) {
			*promotions += 1U;
		} else {
			completions++;
		}
	}

	engine->csb_head = head;
	engine->csb_promotions += *promotions;
	engine->csb_completions += completions;

	return completions;
}

/*
 * Reports whether the ring has room for the given dwords before the saved head.
 */
int
drv_i915_lrc_ring_space(
	const struct i915_context *context,
	unsigned dwords)
{
	uint32_t head;
	uint32_t tail;
	uint32_t space;

	/* The head saved in the image is where the engine stopped reading. */
	head = context->state[CTX_RING_HEAD] & HEAD_ADDR;
	tail = context->ring_tail;

	/* Space is measured to one qword before the head so tail never equals head when full. */
	if (head > tail) {
		space = head - tail;
	} else {
		space = I915_RING_BYTES - tail + head;
	}
	if (space < 8U)
		return ENOSPC;
	space -= 8U;

	/* A wrap may be needed in addition to the payload itself. */
	if ((uint64_t)dwords * 4U + (I915_RING_BYTES - tail) > space && tail + dwords * 4U > I915_RING_BYTES)
		return ENOSPC;
	if ((uint64_t)dwords * 4U > space)
		return ENOSPC;

	/* Succeeded: the dwords fit without overtaking the head. */
	return 0;
}

/*
 * Appends dwords at the ring tail, padding with no-ops when the end is near.
 */
void
drv_i915_lrc_ring_emit(
	struct i915_context *context,
	const uint32_t *dwords,
	unsigned count)
{
	uint32_t tail;
	unsigned index;

	/* Commands never straddle the ring end; the remainder is filled with no-ops. */
	tail = context->ring_tail / 4U;
	if (tail + count > I915_RING_DWORDS) {
		while (tail < I915_RING_DWORDS) {
			context->ring_dwords[tail] = MI_NOOP;
			tail++;
		}

		tail = 0U;
	}

	/* The payload is copied in order; the tail advances past it. */
	for (index = 0U; index < count; index++)
		context->ring_dwords[tail + index] = dwords[index];
	tail += count;
	context->ring_tail = tail * 4U;
	kern_io_write_barrier();
}

/* Expands the offset table into MI_LOAD_REGISTER_IMM headers and register offsets. */
static unsigned
i915_lrc_set_offsets(
	uint32_t *regs,
	const uint8_t *data,
	uint32_t base)
{
	unsigned position;
	unsigned count;
	unsigned flags;
	unsigned remaining;
	uint32_t offset;
	uint8_t byte;

	/* Each table entry is either a skip, or a header followed by encoded offsets. */
	position = 0U;
	while (*data != I915_LRC_END) {
		/* A high bit marks a skip of the given number of state dwords. */
		if ((*data & 0x80U) != 0U) {
			position += *data & 0x7fU;
			data++;
			continue;
		}

		/* The header names the register count and whether the load is posted. */
		count = *data & 0x3fU;
		flags = *data >> 6;
		data++;
		regs[position] = MI_LOAD_REGISTER_IMM(count) | MI_LRI_LRM_CS_MMIO;
		if ((flags & I915_LRC_POSTED) != 0U)
			regs[position] |= MI_LRI_FORCE_POSTED;
		position++;

		/* Each offset is one or two bytes of seven-bit groups, high group first. */
		remaining = count;
		while (remaining != 0U) {
			offset = 0U;
			do {
				byte = *data;
				data++;
				offset = (offset << 7) | (byte & 0x7fU);
			} while ((byte & 0x80U) != 0U);
			regs[position] = base + (offset << 2);
			position += 2U;
			remaining--;
		}
	}

	/* The image ends like a batch so a stray restore stops here. */
	regs[position] = MI_BATCH_BUFFER_END | 1U;

	return position;
}

/* Writes the initial register state for a context that has never run. */
static void
i915_lrc_init_regs(
	struct i915_context *context,
	struct i915_engine *engine)
{
	uint32_t *regs;
	const uint8_t *offsets;
	uint32_t control;

	/* Both pages start zeroed; the state page is then laid out from the offset table. */
	regs = context->state;
	memset(context->image->address, 0, (size_t)context->image->bytes);
	offsets = gen12_xcs_offsets;
	if (engine->class == I915_CLASS_RENDER)
		offsets = gen12_rcs_offsets;
	(void)i915_lrc_set_offsets(regs, offsets, engine->base);

	/* Synchronous context switches are inhibited; the first restore is inhibited too. */
	control = I915_LRC_MASKED_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH);
	control |= I915_LRC_MASKED_DISABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT);
	control |= CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT;
	regs[CTX_CONTEXT_CONTROL] = control;
	regs[CTX_TIMESTAMP] = 0U;
	regs[I915_LRC_BB_OFFSET_INDEX + 1U] = 0U;

	/* The 48-bit space is named by its top table in the PDP0 pair. */
	regs[CTX_PDP0_UDW] = (uint32_t)((uint64_t)context->vm->pml4.paddr >> 32);
	regs[CTX_PDP0_LDW] = (uint32_t)context->vm->pml4.paddr;

	/* The MI_MODE pair clears the stop bit so the restored engine runs. */
	regs[I915_LRC_MI_MODE_INDEX + 1U] &= ~STOP_RING;
	regs[I915_LRC_MI_MODE_INDEX + 1U] |= STOP_RING << 16;

	/* The ring registers name this context's empty ring. */
	regs[CTX_RING_START] = context->ring->ggtt_offset;
	regs[CTX_RING_HEAD] = 0U;
	regs[CTX_RING_TAIL] = 0U;
	regs[CTX_RING_CTL] = RING_CTL_SIZE(I915_RING_BYTES) | RING_VALID;

	/*
	 * Render contexts request full slice enablement.  Left at zero, render
	 * power gating can keep every execution unit powered down: fixed-function
	 * stages run, but the first pixel or vertex thread never dispatches and
	 * the pipeline stalls forever.
	 */
	if (engine->class == I915_CLASS_RENDER)
		regs[CTX_R_PWR_CLK_STATE] = i915_lrc_render_power_state(engine->device);
}

/* Builds the render power request from the slice fuse, as intel_sseu_make_rpcs does. */
static uint32_t
i915_lrc_render_power_state(
	struct i915_device *device)
{
	uint32_t slices;
	uint32_t enabled;

	/* One bit per slice; every Gen12 part this driver targets has exactly one. */
	enabled = drv_i915_read32(device, GEN11_GT_SLICE_ENABLE) & GEN11_GT_S_ENA_MASK;
	slices = 0U;
	while (enabled != 0U) {
		slices += enabled & 1U;
		enabled >>= 1;
	}
	if (slices == 0U)
		slices = 1U;

	return GEN12_RPCS_SLICES(slices);
}

/* Reads one status-buffer entry, falling back to the register mirror when unwritten. */
static uint64_t
i915_lrc_csb_read(
	struct i915_engine *engine,
	unsigned index)
{
	struct i915_device *device;
	uint64_t entry;
	uint32_t low;
	uint32_t high;
	uint32_t mirror;
	unsigned poll;

	/* The status page copy is preferred because a stale entry is recognizable there. */
	device = engine->device;
	entry = I915_CSB_UNWRITTEN;
	for (poll = 0U; poll < 64U; poll++) {
		low = engine->status[I915_HWS_CSB_BUF0_INDEX + 2U * index];
		high = engine->status[I915_HWS_CSB_BUF0_INDEX + 2U * index + 1U];
		entry = ((uint64_t)high << 32) | low;
		if (entry != I915_CSB_UNWRITTEN)
			break;

		kern_compiler_barrier();
	}

	/* The register mirror is the last resort for an entry the engine wrote late. */
	if (entry == I915_CSB_UNWRITTEN) {
		mirror = GEN8_EXECLISTS_STATUS_BUF;
		if (index >= 6U)
			mirror = GEN11_EXECLISTS_STATUS_BUF2 + (index - 6U) * 8U;
		else
			mirror += index * 8U;
		low = drv_i915_read32(device, engine->base + mirror);
		high = drv_i915_read32(device, engine->base + mirror + 4U);
		entry = ((uint64_t)high << 32) | low;
	}

	/* Consuming the entry marks it so its future reuse is detectable. */
	engine->status[I915_HWS_CSB_BUF0_INDEX + 2U * index] = 0xffffffffU;
	engine->status[I915_HWS_CSB_BUF0_INDEX + 2U * index + 1U] = 0xffffffffU;

	return entry;
}

/* Reports whether an entry promotes a new context (nonzero) or completes one (zero). */
static unsigned
i915_lrc_csb_parse(
	uint64_t entry)
{
	uint32_t low;
	uint32_t high;
	unsigned to_valid;
	unsigned away_valid;
	unsigned new_queue;

	/* The lower dword describes the context switched to, the upper the one switched away. */
	low = (uint32_t)entry;
	high = (uint32_t)(entry >> 32);
	to_valid = 0U;
	if (((low & GEN12_CSB_SW_CTX_ID_MASK) >> 15) != GEN12_IDLE_CTX_ID)
		to_valid = 1U;
	away_valid = 0U;
	if (((high & GEN12_CSB_SW_CTX_ID_MASK) >> 15) != GEN12_IDLE_CTX_ID)
		away_valid = 1U;
	new_queue = low & GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE;

	/* Nothing ran before, or a new queue was loaded: a context is starting. */
	if (away_valid == 0U || new_queue != 0U)
		return to_valid;

	return 0U;
}
