/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Requests: slots, command emission into a context ring, submission
 * and seqno-based retirement.
 *
 * Each engine runs one request at a time. A request waits in the
 * engine queue until the engine is idle, is then emitted into its
 * context's ring and loaded, and retires when the breadcrumb it wrote
 * reaches the status page.
 *
 * References: Linux gen8_engine_cs.c gen8_emit_bb_start,
 * gen12_emit_flush_xcs, gen12_emit_flush_rcs,
 * gen12_emit_fini_breadcrumb_xcs/rcs and gen8_emit_wa_tail,
 * re-expressed for this driver.
 */

#include "internal.h"

#include <drivers/gpu.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/sched.h>

#include <errno.h>
#include <string.h>

#include "linux/i915-commands.inc"

/* The pre-parser is disabled around invalidations with this arbitration check form. */
#define I915_PREPARSER_DISABLE		(MI_ARB_CHECK | (1U << 8) | 1U)
#define I915_PREPARSER_ENABLE		(MI_ARB_CHECK | (1U << 8))

/* Flushes issued before a batch on the render engine invalidate every cache. */
#define I915_RCS_INVALIDATE_FLAGS	(PIPE_CONTROL_COMMAND_CACHE_INVALIDATE | PIPE_CONTROL_TLB_INVALIDATE | \
					 PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE | PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | \
					 PIPE_CONTROL_VF_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE | \
					 PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_STORE_DATA_INDEX | \
					 PIPE_CONTROL_QW_WRITE | PIPE_CONTROL_CS_STALL)

/* The render breadcrumb flushes every cache the batch may have written. */
#define I915_RCS_FLUSH_FLAGS		(PIPE_CONTROL_CS_STALL | PIPE_CONTROL_TLB_INVALIDATE | PIPE_CONTROL_TILE_CACHE_FLUSH | \
					 PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH | \
					 PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_L3 | \
					 PIPE_CONTROL_DEPTH_STALL)

/* The largest command sequence one request emits: prologue, extras, batch start, breadcrumb. */
#define I915_REQUEST_RING_DWORDS	(28U + I915_REQUEST_MAX_DWORDS + 6U + 16U)

static unsigned i915_request_emit(struct i915_request *request, uint32_t *dwords);
static unsigned i915_request_emit_prologue(struct i915_request *request, uint32_t *dwords);
static unsigned i915_request_emit_breadcrumb(struct i915_request *request, uint32_t *dwords);
static void i915_request_unlink(struct i915_engine *engine, struct i915_request *request);

/*
 * Takes a free slot for a new request; the caller holds the IRQ lock.
 */
int
drv_i915_request_alloc(
	struct i915_engine *engine,
	struct i915_session *session,
	struct drv_gpu_completion *completion,
	struct i915_request **result)
{
	struct i915_request *request;
	unsigned index;

	/* No caller receives a slot it does not own. */
	*result = NULL;

	/* A free slot is one that retired and had its callback delivered. */
	request = NULL;
	for (index = 0U; index < I915_REQUEST_SLOTS; index++) {
		/* The first free slot is taken. */
		if (engine->slots[index].state == I915_REQUEST_FREE) {
			request = &engine->slots[index];
			break;
		}
	}

	/* Every slot busy means the engine's bounded queue is full. */
	if (request == NULL)
		return EAGAIN;

	/* The slot starts as a queued request without commands yet. */
	memset(request, 0, sizeof(*request));
	request->state = I915_REQUEST_QUEUED;
	request->session = session;
	request->completion = completion;
	*result = request;

	/* Succeeded: the caller fills the request and queues it. */
	return 0;
}

/*
 * Returns a slot that will never be queued; the caller holds the IRQ lock.
 */
void
drv_i915_request_release(
	struct i915_engine *engine,
	struct i915_request *request)
{
	/* A request still linked or active cannot be released. */
	(void)engine;
	request->state = I915_REQUEST_FREE;
}

/*
 * Appends a filled request to the engine queue; the caller holds the IRQ lock.
 */
void
drv_i915_request_queue(
	struct i915_engine *engine,
	struct i915_request *request)
{
	/* The queue is first in, first out across every session. */
	request->next = NULL;
	if (engine->queue_tail == NULL) {
		engine->queue_head = request;
	} else {
		engine->queue_tail->next = request;
	}

	engine->queue_tail = request;
	if (request->session != NULL)
		request->session->pending_requests++;
}

/*
 * Loads the next queued request when the engine is idle; the caller holds the IRQ lock.
 */
void
drv_i915_request_kick(
	struct i915_engine *engine)
{
	struct i915_request *request;
	struct i915_context *context;
	uint32_t dwords[I915_REQUEST_RING_DWORDS];
	unsigned count;
	int error;

	/* Only an idle engine with an empty port takes new work; a reset in progress waits. */
	if (engine->hw_active != 0U || engine->active != NULL || engine->resetting != 0U)
		return;
	if (engine->queue_head == NULL)
		return;

	/* The head request leaves the queue and becomes the active one. */
	request = engine->queue_head;
	engine->queue_head = request->next;
	if (engine->queue_head == NULL)
		engine->queue_tail = NULL;
	request->next = NULL;

	/* Commands are built in a local buffer, then copied into the context ring. */
	context = request->context;
	request->seqno = engine->next_seqno;
	engine->next_seqno++;
	count = i915_request_emit(request, dwords);
	error = drv_i915_lrc_ring_space(context, count);
	if (error != 0) {
		/* A full ring means the context never retired; the request fails without running. */
		request->error = error;
		request->state = I915_REQUEST_DONE;
		request->next = engine->active;
		engine->completed_seqno = request->seqno;
		kern_logf("i915: engine %u ring full; request %u failed\n", engine->index, request->seqno);
		return;
	}

	/* The ring receives the commands and the engine is pointed at the new tail. */
	drv_i915_lrc_ring_emit(context, dwords, count);
	request->state = I915_REQUEST_ACTIVE;
	request->submitted_tick = sched_ticks();
	engine->active = request;
	engine->hw_active = 1U;
	drv_i915_lrc_submit(context);
}

/*
 * Retires the active request when its breadcrumb landed; the caller holds the IRQ lock.
 *
 * Retired requests are chained through their next pointer for the caller to
 * complete outside the lock.
 */
void
drv_i915_request_retire(
	struct i915_engine *engine,
	struct i915_request **retired)
{
	struct i915_request *request;
	uint32_t seqno;

	/* Nothing to retire without an active request. */
	request = engine->active;
	if (request == NULL)
		return;

	/* The status page holds the last seqno the engine wrote. */
	kern_io_read_barrier();
	seqno = engine->status[I915_GEM_HWS_SEQNO];
	if (seqno != request->seqno)
		return;

	/* The request completed; the slot stays owned until its callback ran. */
	engine->completed_seqno = seqno;
	engine->active = NULL;
	request->state = I915_REQUEST_DONE;
	request->error = 0;
	request->next = *retired;
	*retired = request;
}

/*
 * Ends every queued and active request of a session, or of all sessions, with an error.
 *
 * The caller holds the IRQ lock and has already stopped or reset the engine
 * when an active request is failed.
 */
void
drv_i915_request_fail(
	struct i915_engine *engine,
	struct i915_session *session,
	int error,
	struct i915_request **retired)
{
	struct i915_request *request;
	struct i915_request *next;
	unsigned index;

	/* Queued requests are unlinked and retired without ever running. */
	request = engine->queue_head;
	while (request != NULL) {
		next = request->next;
		if (session == NULL || request->session == session) {
			i915_request_unlink(engine, request);
			request->state = I915_REQUEST_DONE;
			request->error = error;
			request->next = *retired;
			*retired = request;
		}

		request = next;
	}

	/* The active request is withdrawn only when the caller asked for its session. */
	request = engine->active;
	if (request != NULL && (session == NULL || request->session == session)) {
		engine->active = NULL;
		engine->hw_active = 0U;
		engine->completed_seqno = request->seqno;
		request->state = I915_REQUEST_DONE;
		request->error = error;
		request->next = *retired;
		*retired = request;
	}

	/* Reserved and retained slots still owe the framework a callback. */
	for (index = 0U; index < I915_REQUEST_SLOTS; index++) {
		request = &engine->slots[index];
		if (request->state != I915_REQUEST_RESERVED && request->state != I915_REQUEST_RETAINED)
			continue;
		if (session != NULL && request->session != session)
			continue;

		request->state = I915_REQUEST_DONE;
		request->error = error;
		request->next = *retired;
		*retired = request;
	}
}

/*
 * Delivers the callbacks of retired requests and frees their slots.
 */
void
drv_i915_request_complete_list(
	struct i915_engine *engine,
	struct i915_request *retired)
{
	struct i915_device *device;
	struct i915_request *request;
	struct i915_request *next;
	struct drv_gpu_completion *completion;
	int error;
	unsigned freed;

	device = engine->device;
	freed = 0U;

	/* Each callback runs without a driver lock; the slot is released afterwards. */
	request = retired;
	while (request != NULL) {
		next = request->next;
		completion = request->completion;
		error = request->error;

		/* The framework learns the outcome before the slot can be reused. */
		if (completion != NULL)
			drv_gpu_complete(completion, error);

		/* The slot returns under the IRQ lock so a concurrent alloc sees it consistently. */
		spin_lock(&device->irq_lock);

		if (request->session != NULL && request->session->pending_requests != 0U)
			request->session->pending_requests--;

		/* A batch object goes back to its session pool for the next submission. */
		if (request->batch != NULL)
			request->batch->busy = 0U;
		request->batch = NULL;
		request->completion = NULL;
		request->state = I915_REQUEST_FREE;

		spin_unlock(&device->irq_lock);

		freed++;
		request = next;
	}

	/* Drain and capacity waiters observe the released slots. */
	if (freed != 0U) {
		waitq_wake_all(&device->retire_waitq);
		if (device->gpu != NULL)
			drv_gpu_capacity_changed(device->gpu);
	}
}

/* Builds the complete command sequence of one request. */
static unsigned
i915_request_emit(
	struct i915_request *request,
	uint32_t *dwords)
{
	unsigned count;
	unsigned index;

	/* Caches are invalidated so the batch sees the pages bound before submission. */
	count = i915_request_emit_prologue(request, dwords);

	/* Driver-owned commands, such as the selftest store, precede the batch. */
	for (index = 0U; index < request->extra_count; index++)
		dwords[count + index] = request->extra[index];
	count += request->extra_count;

	/* The batch runs with arbitration enabled and in the context's address space. */
	if (request->batch != NULL) {
		dwords[count] = MI_ARB_ON_OFF | MI_ARB_ENABLE;
		dwords[count + 1U] = MI_BATCH_BUFFER_START_GEN8 | MI_BATCH_NON_SECURE_I965;
		dwords[count + 2U] = (uint32_t)request->batch_va;
		dwords[count + 3U] = (uint32_t)(request->batch_va >> 32);
		dwords[count + 4U] = MI_ARB_ON_OFF | MI_ARB_DISABLE;
		dwords[count + 5U] = MI_NOOP;
		count += 6U;
	}

	/* The breadcrumb publishes the seqno and raises the user interrupt. */
	count += i915_request_emit_breadcrumb(request, dwords + count);

	return count;
}

/* Emits the pre-batch invalidation for the request's engine class. */
static unsigned
i915_request_emit_prologue(
	struct i915_request *request,
	uint32_t *dwords)
{
	unsigned count;

	count = 0U;

	if (request->context->engine->class == I915_CLASS_COPY) {
		dwords[count++] = I915_PREPARSER_DISABLE;
		dwords[count++] = (MI_FLUSH_DW + 1U) | MI_FLUSH_DW_STORE_INDEX | MI_FLUSH_DW_OP_STOREDW | MI_INVALIDATE_TLB;
		dwords[count++] = LRC_PPHWSP_SCRATCH_ADDR;
		dwords[count++] = 0U;
		dwords[count++] = 0U;
		dwords[count++] = I915_PREPARSER_ENABLE;
		return count;
	}

	/*
	 * Gen12 RCS EMIT_INVALIDATE (Linux gen12_emit_flush_rcs): a stalling flush,
	 * then the cache/TLB invalidate wrapped in a pre-parser disable so the parser
	 * cannot prefetch through the stale pages, then the CCS AUX invalidate and a
	 * register poll that waits for it to retire.  The invalidate-only prologue that
	 * preceded this omitted the pre-flush and the AUX handling.
	 */
	dwords[count++] = 0x7a000204U;		/* PIPE_CONTROL(6) | HDC pipeline flush */
	dwords[count++] = 0x103070a1U;		/* CS_STALL|RT/DEPTH/DC flush|depth stall|store-index|QW write */
	dwords[count++] = LRC_PPHWSP_SCRATCH_ADDR;
	dwords[count++] = 0U;
	dwords[count++] = 0U;
	dwords[count++] = 0U;
	dwords[count++] = I915_PREPARSER_DISABLE;
	dwords[count++] = 0x7a000004U;		/* PIPE_CONTROL(6): invalidate */
	dwords[count++] = 0x20344c1cU;		/* command/TLB/instruction/state/const/tex/VF invalidate | CS_STALL | store-index | QW write */
	dwords[count++] = LRC_PPHWSP_SCRATCH_ADDR;
	dwords[count++] = 0U;
	dwords[count++] = 0U;
	dwords[count++] = 0U;
	dwords[count++] = 0x11020001U;		/* MI_LOAD_REGISTER_IMM(1) | MMIO remap */
	dwords[count++] = 0x00004208U;		/* GEN12_CCS_AUX_INV (RCS0) */
	dwords[count++] = 0x00000001U;
	dwords[count++] = 0x0e01c003U;		/* MI_SEMAPHORE_WAIT: register poll, wait for == 0 */
	dwords[count++] = 0U;
	dwords[count++] = 0x00004208U;		/* GEN12_CCS_AUX_INV */
	dwords[count++] = 0U;
	dwords[count++] = 0U;
	dwords[count++] = I915_PREPARSER_ENABLE;

	return count;
}

/* Emits the flush, seqno write, user interrupt and arbitration tail. */
static unsigned
i915_request_emit_breadcrumb(
	struct i915_request *request,
	uint32_t *dwords)
{
	struct i915_engine *engine;
	uint32_t address;
	unsigned count;

	/* The seqno lands in the engine's status page through the GGTT. */
	engine = request->context->engine;
	address = engine->hwsp->ggtt_offset + I915_GEM_HWS_SEQNO_ADDR;
	count = 0U;

	/* Copy engines stall on a flush and store through a second flush. */
	if (engine->class == I915_CLASS_COPY) {
		dwords[count] = MI_FLUSH_DW + 1U;
		dwords[count + 1U] = 0U;
		dwords[count + 2U] = 0U;
		dwords[count + 3U] = 0U;
		dwords[count + 4U] = (MI_FLUSH_DW + 1U) | MI_FLUSH_DW_OP_STOREDW;
		dwords[count + 5U] = address | MI_FLUSH_DW_USE_GTT;
		dwords[count + 6U] = 0U;
		dwords[count + 7U] = request->seqno;
		count += 8U;
	} else {
		dwords[count] = GFX_OP_PIPE_CONTROL(6) | PIPE_CONTROL0_HDC_PIPELINE_FLUSH;
		dwords[count + 1U] = I915_RCS_FLUSH_FLAGS;
		dwords[count + 2U] = 0U;
		dwords[count + 3U] = 0U;
		dwords[count + 4U] = 0U;
		dwords[count + 5U] = 0U;
		dwords[count + 6U] = GFX_OP_PIPE_CONTROL(6);
		dwords[count + 7U] = PIPE_CONTROL_FLUSH_ENABLE | PIPE_CONTROL_CS_STALL | PIPE_CONTROL_GLOBAL_GTT_IVB | PIPE_CONTROL_QW_WRITE;
		dwords[count + 8U] = address;
		dwords[count + 9U] = 0U;
		dwords[count + 10U] = request->seqno;
		dwords[count + 11U] = 0U;
		count += 12U;
	}

	/* The interrupt follows the store; arbitration is re-enabled and a check point added. */
	dwords[count] = MI_USER_INTERRUPT;
	dwords[count + 1U] = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	dwords[count + 2U] = MI_ARB_CHECK;
	dwords[count + 3U] = MI_NOOP;
	count += 4U;

	return count;
}

/* Removes a request from the engine queue. */
static void
i915_request_unlink(
	struct i915_engine *engine,
	struct i915_request *request)
{
	struct i915_request **position;

	/* The singly linked queue is scanned for the request's predecessor. */
	position = &engine->queue_head;
	while (*position != NULL && *position != request)
		position = &(*position)->next;
	if (*position != request)
		return;

	*position = request->next;
	if (engine->queue_tail == request) {
		engine->queue_tail = NULL;
		position = &engine->queue_head;
		while (*position != NULL) {
			engine->queue_tail = *position;
			position = &(*position)->next;
		}
	}

	request->next = NULL;
}
