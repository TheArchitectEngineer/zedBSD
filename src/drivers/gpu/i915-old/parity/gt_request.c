/*
 * WS031 Linux-parity — P6-c3a: what a request writes into its ring.
 * See gt_request.h for the reference order and the ADL-P specifics.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include "gt_mmio.h"
#include "gt_mem.h"
#include "gt_engine.h"
#include "gt_lrc.h"
#include "gt_init.h"
#include "gt_request.h"
#include "osdep/mmio.h"

/* --- intel_ring_begin / intel_ring_advance -------------------------------- */

uint32_t *
parity_ring_begin(struct parity_gt_request *rq, unsigned num_dwords)
{
	struct parity_gt_ring *ring;
	uint32_t bytes = (uint32_t)num_dwords * 4u;
	uint32_t space;

	if (rq == 0 || rq->ce == 0 || rq->error != 0)
		return 0;
	ring = &rq->ce->ring;

	/*
	 * Every command group the reference emits is an even number of dwords,
	 * which is what keeps RING_TAIL qword aligned.  An odd count here is a
	 * porting mistake, not something to pad over.
	 */
	if ((num_dwords & 1u) != 0u) {
		rq->error = -EINVAL;
		return 0;
	}

	/* __intel_ring_space(): keep a cacheline between tail and head. */
	space = (ring->head - ring->emit - PARITY_CACHELINE_BYTES) & (ring->size - 1u);
	if (bytes > space) {
		rq->error = -ENOSPC;
		return 0;
	}
	/* See the header: a request never wraps here, so refuse one that would. */
	if (ring->emit + bytes > ring->size) {
		rq->error = -ENOSPC;
		return 0;
	}
	return (uint32_t *)((char *)ring->vaddr + ring->emit);
}

void
parity_ring_advance(struct parity_gt_request *rq, uint32_t *cs)
{
	struct parity_gt_ring *ring = &rq->ce->ring;

	ring->emit = (uint32_t)((char *)cs - (char *)ring->vaddr);
	ring->space = (ring->head - ring->emit - PARITY_CACHELINE_BYTES) &
		(ring->size - 1u);
}

/* --- shared pieces --------------------------------------------------------- */

uint32_t *
parity_gen12_emit_aux_table_inv(int engine_id, uint32_t *cs)
{
	uint32_t inv_reg = parity_lrc_aux_inv_reg(engine_id);

	if (inv_reg == 0u)
		return cs;
	*cs++ = PARITY_MI_LOAD_REGISTER_IMM(1) | PARITY_MI_LRI_MMIO_REMAP_EN;
	*cs++ = inv_reg;   /* + gsi_offset, which is 0 on the primary GT */
	*cs++ = PARITY_AUX_INV;

	*cs++ = PARITY_MI_SEMAPHORE_WAIT_TOKEN |
		PARITY_MI_SEMAPHORE_REGISTER_POLL |
		PARITY_MI_SEMAPHORE_POLL |
		PARITY_MI_SEMAPHORE_SAD_EQ_SDD;
	*cs++ = 0u;
	*cs++ = inv_reg;
	*cs++ = 0u;
	*cs++ = 0u;
	return cs;
}

static uint32_t
preparser_disable(int state)
{
	return PARITY_MI_ARB_CHECK | (1u << 8) | (state ? 1u : 0u);
}

/* __gen8_emit_pipe_control(): six dwords, the last three left zero. */
static uint32_t *
emit_pipe_control(uint32_t *cs, uint32_t bit_group_0, uint32_t bit_group_1,
	uint32_t offset)
{
	cs[0] = PARITY_GFX_OP_PIPE_CONTROL(6) | bit_group_0;
	cs[1] = bit_group_1;
	cs[2] = offset;
	cs[3] = 0u;
	cs[4] = 0u;
	cs[5] = 0u;
	return cs + 6;
}

/* --- gen12_emit_flush_rcs -------------------------------------------------- */

static int
emit_flush_rcs(struct parity_gt_request *rq, uint32_t mode)
{
	int engine_id = rq->ce->ge->info->id;
	uint32_t *cs;

	/*
	 * `mode & EMIT_FLUSH || gen12_needs_ccs_aux_inv(engine)`: the second
	 * half is true on ADL-P, so this block runs even for a pure invalidate.
	 * mtl_dummy_pipe_control() is Wa_14016712196 (12.70..12.74, DG2): no-op.
	 */
	{
		uint32_t bg0 = PARITY_PIPE_CONTROL0_HDC_PIPELINE_FLUSH;
		uint32_t bg1 = 0u;

		/* 12.70+ would add PIPE_CONTROL_CCS_FLUSH to bg0; ADL-P is 12.0. */
		if ((mode & PARITY_EMIT_FLUSH) != 0u)
			bg1 |= PARITY_PIPE_CONTROL_FLUSH_L3;
		bg1 |= PARITY_PIPE_CONTROL_TILE_CACHE_FLUSH;
		bg1 |= PARITY_PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH;
		bg1 |= PARITY_PIPE_CONTROL_DEPTH_CACHE_FLUSH;
		bg1 |= PARITY_PIPE_CONTROL_DEPTH_STALL;   /* Wa_1409600907:tgl,adl-p */
		bg1 |= PARITY_PIPE_CONTROL_DC_FLUSH_ENABLE;
		bg1 |= PARITY_PIPE_CONTROL_FLUSH_ENABLE;
		bg1 |= PARITY_PIPE_CONTROL_STORE_DATA_INDEX;
		bg1 |= PARITY_PIPE_CONTROL_QW_WRITE;
		bg1 |= PARITY_PIPE_CONTROL_CS_STALL;
		/* HAS_3D_PIPELINE and not COMPUTE_CLASS: nothing is masked off. */

		cs = parity_ring_begin(rq, 6u);
		if (cs == 0)
			return rq->error;
		cs = emit_pipe_control(cs, bg0, bg1, PARITY_LRC_PPHWSP_SCRATCH_ADDR);
		parity_ring_advance(rq, cs);
	}

	if ((mode & PARITY_EMIT_INVALIDATE) != 0u) {
		uint32_t flags = 0u;

		flags |= PARITY_PIPE_CONTROL_COMMAND_CACHE_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_TLB_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_VF_CACHE_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_CONST_CACHE_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_STATE_CACHE_INVALIDATE;
		flags |= PARITY_PIPE_CONTROL_STORE_DATA_INDEX;
		flags |= PARITY_PIPE_CONTROL_QW_WRITE;
		flags |= PARITY_PIPE_CONTROL_CS_STALL;

		/* count = 8, + 8 for the AUX invalidate. */
		cs = parity_ring_begin(rq, 16u);
		if (cs == 0)
			return rq->error;
		/*
		 * Keep the pre-parser from running past the TLB invalidate and
		 * fetching a stale page for the request payload.
		 */
		*cs++ = preparser_disable(1);
		cs = emit_pipe_control(cs, 0u, flags, PARITY_LRC_PPHWSP_SCRATCH_ADDR);
		cs = parity_gen12_emit_aux_table_inv(engine_id, cs);
		*cs++ = preparser_disable(0);
		parity_ring_advance(rq, cs);
	}
	return 0;
}

/* --- gen12_emit_flush_xcs -------------------------------------------------- */

static int
emit_flush_xcs(struct parity_gt_request *rq, uint32_t mode)
{
	int cls = rq->ce->ge->info->class;
	int engine_id = rq->ce->ge->info->id;
	uint32_t count = 4u;
	uint32_t cmd;
	uint32_t *cs;

	if ((mode & PARITY_EMIT_INVALIDATE) != 0u)
		count += 2u + 8u;   /* the pre-parser pair and the AUX invalidate */

	cs = parity_ring_begin(rq, count);
	if (cs == 0)
		return rq->error;

	if ((mode & PARITY_EMIT_INVALIDATE) != 0u)
		*cs++ = preparser_disable(1);

	/*
	 * A command barrier is always required, so subsequent commands (the
	 * breadcrumb interrupt) are ordered after the write-cache flush.
	 */
	cmd = (PARITY_MI_FLUSH_DW + 1u) |
		PARITY_MI_FLUSH_DW_STORE_INDEX | PARITY_MI_FLUSH_DW_OP_STOREDW;
	if ((mode & PARITY_EMIT_INVALIDATE) != 0u) {
		cmd |= PARITY_MI_INVALIDATE_TLB;
		if (cls == PARITY_VIDEO_DECODE_CLASS)
			cmd |= PARITY_MI_INVALIDATE_BSD;
		/* gen12_needs_ccs_aux_inv() && COPY_ENGINE_CLASS */
		if (cls == PARITY_COPY_ENGINE_CLASS)
			cmd |= PARITY_MI_FLUSH_DW_CCS;
	}
	*cs++ = cmd;
	*cs++ = PARITY_LRC_PPHWSP_SCRATCH_ADDR;
	*cs++ = 0u;   /* upper addr */
	*cs++ = 0u;   /* value */

	/*
	 * gen12_emit_aux_table_inv() is called unconditionally here, but the
	 * ring space for it was only reserved for EMIT_INVALIDATE; the
	 * reference relies on callers passing EMIT_INVALIDATE whenever the
	 * engine needs AUX invalidation (EMIT_FLUSH alone is never used on
	 * this path).  Refuse a flush-only call instead of overrunning.
	 */
	if ((mode & PARITY_EMIT_INVALIDATE) != 0u) {
		cs = parity_gen12_emit_aux_table_inv(engine_id, cs);
		*cs++ = preparser_disable(0);
	} else if (parity_lrc_aux_inv_reg(engine_id) != 0u) {
		rq->error = -EINVAL;
		return rq->error;
	}
	parity_ring_advance(rq, cs);
	return 0;
}

int
parity_emit_flush(struct parity_gt_request *rq, uint32_t mode)
{
	if (rq == 0 || rq->ce == 0)
		return -EINVAL;
	if (rq->ce->ge->info->class == PARITY_RENDER_CLASS)
		return emit_flush_rcs(rq, mode);
	return emit_flush_xcs(rq, mode);
}

/* --- intel_engine_emit_ctx_wa --------------------------------------------- */

int
parity_emit_ctx_wa(struct parity_gt_request *rq,
	const struct parity_wa_list *wal, struct osdep_mmio *m)
{
	uint32_t *cs;
	unsigned i;
	int rc;

	if (rq == 0 || wal == 0)
		return -EINVAL;
	if (wal->count == 0u)
		return 0;

	rc = parity_emit_flush(rq, PARITY_EMIT_BARRIER);
	if (rc != 0)
		return rc;

	cs = parity_ring_begin(rq, wal->count * 2u + 2u);
	if (cs == 0)
		return rq->error;

	*cs++ = PARITY_MI_LOAD_REGISTER_IMM(wal->count);
	for (i = 0u; i < wal->count; i++) {
		const struct parity_wa *wa = &wal->list[i];
		uint32_t val;

		/* Skip reading the register when it is not really needed. */
		if (wa->kind == PARITY_WA_MASKED || (wa->clr | wa->set) == 0xffffffffu) {
			val = wa->set;
		} else {
			if (m == 0) {
				rq->error = -EINVAL;
				return rq->error;
			}
			/* is_mcr would be intel_gt_mcr_read_any_fw(); none on ADL-P. */
			val = osdep_mmio_read32(m, wa->reg);
			val &= ~wa->clr;
			val |= wa->set;
		}
		*cs++ = wa->reg;
		*cs++ = val;
	}
	*cs++ = PARITY_MI_NOOP;
	parity_ring_advance(rq, cs);

	return parity_emit_flush(rq, PARITY_EMIT_BARRIER);
}

/* --- request create / add ------------------------------------------------- */

int
parity_request_create(struct parity_gt_request *rq, struct parity_gt_context *ce,
	uint32_t seqno, uint32_t hwsp_ggtt, volatile uint32_t *hwsp_cpu)
{
	unsigned i;

	if (rq == 0 || ce == 0 || !ce->allocated)
		return -EINVAL;
	for (i = 0u; i < sizeof(*rq); i++)
		((char *)rq)[i] = 0;
	rq->ce = ce;
	rq->seqno = seqno;
	rq->hwsp_ggtt = hwsp_ggtt;
	rq->hwsp_cpu = hwsp_cpu;
	rq->preempt_ggtt = (uint32_t)ce->ge->hwsp_ggtt +
		PARITY_I915_GEM_HWS_PREEMPT_ADDR;
	rq->head = ce->ring.emit;

	/*
	 * The breadcrumb is a qword write, and MI_FLUSH_DW needs bit 5 of its
	 * address clear: both are reference GEM_BUG_ONs, kept as refusals.
	 */
	if ((hwsp_ggtt & 7u) != 0u || (hwsp_ggtt & (1u << 5)) != 0u)
		return -EINVAL;

	/* execlists_request_alloc(): the vm is 4-level, so no emit_pdps. */
	return parity_emit_flush(rq, PARITY_EMIT_INVALIDATE);
}

static uint32_t *
emit_fini_breadcrumb_tail(struct parity_gt_request *rq, uint32_t *cs)
{
	struct parity_gt_ring *ring = &rq->ce->ring;

	*cs++ = PARITY_MI_USER_INTERRUPT;
	*cs++ = PARITY_MI_ARB_ON_OFF | PARITY_MI_ARB_ENABLE;

	/* has semaphores and no GuC: gen12_emit_preempt_busywait(). */
	*cs++ = PARITY_MI_ARB_CHECK;   /* trigger IDLE->ACTIVE first */
	*cs++ = PARITY_MI_SEMAPHORE_WAIT_TOKEN |
		PARITY_MI_SEMAPHORE_GLOBAL_GTT |
		PARITY_MI_SEMAPHORE_POLL |
		PARITY_MI_SEMAPHORE_SAD_EQ_SDD;
	*cs++ = 0u;
	*cs++ = rq->preempt_ggtt;
	*cs++ = 0u;
	*cs++ = 0u;

	/* Wa_14014475959 is DG2 only. */
	rq->tail = (uint32_t)((char *)cs - (char *)ring->vaddr);

	/* gen8_emit_wa_tail(): at least one preemption point per request. */
	*cs++ = PARITY_MI_ARB_CHECK;
	*cs++ = PARITY_MI_NOOP;
	rq->wa_tail = (uint32_t)((char *)cs - (char *)ring->vaddr);
	return cs;
}

int
parity_request_add(struct parity_gt_request *rq)
{
	uint32_t *cs;

	if (rq == 0 || rq->ce == 0 || rq->error != 0)
		return rq != 0 ? (rq->error != 0 ? rq->error : -EINVAL) : -EINVAL;

	/*
	 * __i915_request_commit() reserves exactly emit_fini_breadcrumb_dw
	 * (measure_breadcrumb_dw) and the breadcrumb fills it: 22 dwords for
	 * render (PIPE_CONTROL 6 + qword write 6 + tail 10), 18 for the others
	 * (MI_FLUSH_DW 4 + store 4 + tail 10).  No padding.
	 */
	cs = parity_ring_begin(rq,
		rq->ce->ge->info->class == PARITY_RENDER_CLASS ? 22u : 18u);
	if (cs == 0)
		return rq->error;

	if (rq->ce->ge->info->class == PARITY_RENDER_CLASS) {
		uint32_t flags = PARITY_PIPE_CONTROL_CS_STALL |
			PARITY_PIPE_CONTROL_TLB_INVALIDATE |
			PARITY_PIPE_CONTROL_TILE_CACHE_FLUSH |
			PARITY_PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
			PARITY_PIPE_CONTROL_DEPTH_CACHE_FLUSH |
			PARITY_PIPE_CONTROL_DC_FLUSH_ENABLE |
			PARITY_PIPE_CONTROL_FLUSH_ENABLE;

		flags |= PARITY_PIPE_CONTROL_FLUSH_L3;     /* < 12.70 */
		/* Wa_14016712196 (12.70..12.74, DG2): not taken. */
		flags |= PARITY_PIPE_CONTROL_DEPTH_STALL;  /* Wa_1409600907 */

		cs = emit_pipe_control(cs, PARITY_PIPE_CONTROL0_HDC_PIPELINE_FLUSH,
			flags, 0u);

		/* gen12_emit_ggtt_write_rcs(seqno, hwsp, 0, FLUSH_ENABLE|CS_STALL). */
		*cs++ = PARITY_GFX_OP_PIPE_CONTROL(6);
		*cs++ = PARITY_PIPE_CONTROL_FLUSH_ENABLE | PARITY_PIPE_CONTROL_CS_STALL |
			PARITY_PIPE_CONTROL_GLOBAL_GTT_IVB | PARITY_PIPE_CONTROL_QW_WRITE;
		*cs++ = rq->hwsp_ggtt;
		*cs++ = 0u;
		*cs++ = rq->seqno;
		*cs++ = 0u;   /* the reference thrashes one extra dword */
	} else {
		/* Stalling flush before the seqno write; the post-sync is not. */
		*cs++ = PARITY_MI_FLUSH_DW + 1u;
		*cs++ = 0u;
		*cs++ = 0u;
		*cs++ = 0u;

		/* gen8_emit_ggtt_write(seqno, hwsp, 0). */
		*cs++ = (PARITY_MI_FLUSH_DW + 1u) | PARITY_MI_FLUSH_DW_OP_STOREDW;
		*cs++ = rq->hwsp_ggtt | PARITY_MI_FLUSH_DW_USE_GTT;
		*cs++ = 0u;
		*cs++ = rq->seqno;
	}

	cs = emit_fini_breadcrumb_tail(rq, cs);
	parity_ring_advance(rq, cs);
	rq->added = 1;
	return 0;
}
