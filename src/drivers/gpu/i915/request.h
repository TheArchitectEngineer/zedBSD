/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Requests.
 *
 * The execution side writes a request into a context's ring: the command
 * stream is re-derived from the reference (gen8_engine_cs.c,
 * intel_workarounds.c, intel_execlists_submission.c) for Alder Lake-P, in the
 * order the reference emits it:
 *
 *   request creation         the invalidate flush (a 4-level address space,
 *                            so no page-directory reload)
 *   context workarounds      a barrier flush, one LRI with every entry, a
 *                            NOOP, another barrier flush; nothing at all
 *                            when the engine's context list is empty
 *   render state             nothing: Gen12 has no null render state
 *   request add              the final breadcrumb and its tail
 *
 * There is no initial breadcrumb: the reference emits it only from the
 * selftests, execbuf and the GSC path.
 *
 * The Alder Lake-P specifics that shape the stream:
 *
 *   - every engine needs the AUX table invalidate, so the render flush runs
 *     its flush block even for a pure invalidate, and every invalidate
 *     carries the AUX invalidate with its semaphore poll;
 *   - the render flush flushes L3 only when a flush was asked for, and the
 *     final breadcrumb always flushes it;
 *   - Wa_1409600907 adds a depth stall to the render flush and breadcrumb;
 *   - the engines have semaphores, so the breadcrumb tail waits on the
 *     status page's PREEMPT dword to be zero;
 *   - a video engine's invalidate also invalidates BSD, a copy engine's also
 *     flushes CCS.
 *
 * The reference wraps the ring by filling its end with NOOPs.  A request here
 * is about 0.2 KiB on a fresh ring, so it never wraps, and a request that
 * would is refused instead of carrying a wrap path nothing exercises.
 *
 * The software request queue of the device (the jobs sessions submit) is a
 * separate part of this file and header.
 */

#ifndef DRIVERS_GPU_I915_REQUEST_H
#define DRIVERS_GPU_I915_REQUEST_H

#include <stdint.h>

struct i915_gt_context;
struct i915_mmio;
struct i915_wa_list;

/*
 * One request written into a context's ring.
 *
 * The owner embeds it; it is valid from drv_i915_request_create() until the
 * owner reuses it.  The breadcrumb is the seqno written to the timeline's
 * status-page slot once the request's commands have run.
 */
struct i915_gt_request {
	/* The context whose ring holds the request. */
	struct i915_gt_context *ce;

	/* The value the breadcrumb writes. */
	uint32_t seqno;

	/* Where the breadcrumb lands: the timeline's status-page slot. */
	uint32_t hwsp_ggtt;
	volatile uint32_t *hwsp_cpu;

	/* The GGTT address of the PREEMPT dword of the engine's status page. */
	uint32_t preempt_ggtt;

	/* The ring offset where the request starts. */
	uint32_t head;

	/* The ring offset after the breadcrumb, before the workaround tail. */
	uint32_t tail;

	/* The ring offset the ring tail is programmed to. */
	uint32_t wa_tail;

	/* The first error writing the request met; every later step refuses. */
	int error;

	/* Nonzero once the final breadcrumb has been written. */
	int added;
};

uint32_t *drv_i915_ring_begin(struct i915_gt_request *rq, unsigned num_dwords);
void drv_i915_ring_advance(struct i915_gt_request *rq, uint32_t *cs);
uint32_t *drv_i915_gen12_emit_aux_table_inv(int engine_id, uint32_t *cs);
int drv_i915_emit_ctx_wa(struct i915_gt_request *rq, const struct i915_wa_list *wal, struct i915_mmio *mmio);
int drv_i915_request_create(struct i915_gt_request *rq, struct i915_gt_context *ce, uint32_t seqno, uint32_t hwsp_ggtt, volatile uint32_t *hwsp_cpu);
int drv_i915_request_add(struct i915_gt_request *rq);
int drv_i915_request_completed(const struct i915_gt_request *rq);

#endif
