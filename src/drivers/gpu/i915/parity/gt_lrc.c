/*
 * WS031 Linux-parity — P6-c2a: the logical ring context image and its ring.
 * See gt_lrc.h for the reference mapping and for what c2b still has to add.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "gt_mmio.h"
#include "gt_mem.h"
#include "gt_engine.h"
#include "gt_lrc.h"
#include "gt_request.h"

#include "gt_lrc_offsets.inc"

#define MASKED_ENABLE(bits)   ((((uint32_t)(bits)) << 16) | ((uint32_t)(bits)))
#define MASKED_DISABLE(bits)  (((uint32_t)(bits)) << 16)

static unsigned
hweight8v(uint8_t v)
{
	unsigned n = 0u;

	while (v != 0u) { n += (unsigned)(v & 1u); v = (uint8_t)(v >> 1); }
	return n;
}

const uint8_t *
parity_gen12_rcs_offsets_ref(void)
{
	return parity_gen12_rcs_offsets;
}

const uint8_t *
parity_gen12_xcs_offsets_ref(void)
{
	return parity_gen12_xcs_offsets;
}

/* --- __lrc_alloc_state() sizing ------------------------------------------- */

uint32_t
parity_lrc_state_size(uint32_t context_size, unsigned *wa_bb_page_out)
{
	uint32_t size = (context_size + 4095u) & ~4095u;

	/*
	 * CONFIG_DRM_I915_DEBUG_GEM would add a redzone page here; it is off.
	 * On gen12 INDIRECT_CTX and PER_CTX_BB need a page each, and wa_bb_page
	 * names the FIRST of them (an index in pages, before they are added).
	 */
	if (wa_bb_page_out != 0)
		*wa_bb_page_out = size / 4096u;
	size += 4096u * 2u;
	return size;
}

/* --- set_offsets() -------------------------------------------------------- */

unsigned
parity_lrc_set_offsets(uint32_t *regs, const uint8_t *data, uint32_t mmio_base,
	int close)
{
	uint32_t *start = regs;

	while (*data != 0u) {
		uint8_t count, flags;

		/* A high bit marks a skip of that many state dwords. */
		if ((*data & 0x80u) != 0u) {
			count = (uint8_t)(*data++ & (uint8_t)~0x80u);
			regs += count;
			continue;
		}

		count = (uint8_t)(*data & 0x3fu);
		flags = (uint8_t)(*data >> 6);
		data++;

		*regs = PARITY_MI_LOAD_REGISTER_IMM(count);
		if ((flags & PARITY_LRC_POSTED) != 0u)
			*regs |= PARITY_MI_LRI_FORCE_POSTED;
		/* GRAPHICS_VER >= 11 */
		*regs |= PARITY_MI_LRI_LRM_CS_MMIO;
		regs++;

		do {
			uint32_t offset = 0u;
			uint8_t v;

			/* Seven-bit groups, high group first, bit 7 continues. */
			do {
				v = *data++;
				offset <<= 7;
				offset |= (uint32_t)(v & (uint8_t)~0x80u);
			} while ((v & 0x80u) != 0u);

			regs[0] = mmio_base + (offset << 2);
			regs += 2;
		} while (--count);
	}

	/*
	 * The reference closes the batch only when it also inhibited the restore
	 * (set_offsets is called with close == inhibit); gen11+ sets bit 0.
	 */
	if (close)
		*regs = PARITY_MI_BATCH_BUFFER_END | 1u;

	return (unsigned)(regs - start);
}

/* --- intel_sseu_make_rpcs (gen12) ----------------------------------------- */

uint32_t
parity_sseu_make_rpcs(uint8_t slice_mask, int has_slice_pg)
{
	uint32_t rpcs = 0u;
	uint32_t val;

	/*
	 * gen12 reports ONLY has_slice_pg (gen12_sseu_info_init), so neither the
	 * subslice nor the EU fields are emitted.  Requesting them anyway would
	 * not be "more enablement": the SScount field is three bits wide and the
	 * reference documents that path as gen11-specific.
	 */
	if (!has_slice_pg)
		return 0u;

	val = (uint32_t)hweight8v(slice_mask);
	val <<= PARITY_GEN11_RPCS_S_CNT_SHIFT;
	val &= PARITY_GEN11_RPCS_S_CNT_MASK;

	rpcs |= PARITY_GEN8_RPCS_ENABLE | PARITY_GEN8_RPCS_S_CNT_ENABLE | val;
	return rpcs;
}

/* --- allocation ----------------------------------------------------------- */

int
parity_lrc_alloc(struct parity_gt_context *ce, struct parity_gt_engine *ge,
	struct parity_gt_ppgtt *vm, struct parity_gt_mem *gm,
	uint32_t ring_size, uint32_t sw_id)
{
	unsigned i;
	int rc;

	if (ce == 0 || ge == 0 || vm == 0 || gm == 0 || ring_size == 0u)
		return -EINVAL;
	for (i = 0u; i < sizeof(*ce); i++)
		((char *)ce)[i] = 0;
	ce->ge = ge;
	ce->vm = vm;
	ce->sw_id = sw_id;
	ce->tag = -1;

	ce->state_bytes = parity_lrc_state_size(ge->info->context_size,
		&ce->wa_bb_page);
	ce->state = parity_gt_object_create(gm, ce->state_bytes);
	if (ce->state == 0)
		return -ENOMEM;
	rc = parity_gt_ggtt_bind(gm, ce->state);
	if (rc != 0)
		goto err;
	ce->lrc_reg_state = (uint32_t *)((char *)ce->state->cpu +
		PARITY_LRC_STATE_OFFSET);

	/* intel_engine_create_ring(): the size must be a power of two. */
	if ((ring_size & (ring_size - 1u)) != 0u) {
		rc = -EINVAL;
		goto err;
	}
	ce->ring.obj = parity_gt_object_create(gm, ring_size);
	if (ce->ring.obj == 0) {
		rc = -ENOMEM;
		goto err;
	}
	rc = parity_gt_ggtt_bind(gm, ce->ring.obj);
	if (rc != 0)
		goto err;
	ce->ring.vaddr = (uint32_t *)ce->ring.obj->cpu;
	ce->ring.size = ring_size;
	ce->ring.ggtt_offset = ce->ring.obj->ggtt_offset;
	ce->ring.head = 0u;
	ce->ring.tail = 0u;
	ce->ring.emit = 0u;
	/* intel_ring_update_space() on a fresh ring: everything but one dword. */
	ce->ring.space = ring_size - 8u;

	ce->allocated = 1;
	return 0;

err:
	parity_lrc_release(ce, gm);
	return rc;
}

/* --- lrc_init_state / __lrc_init_regs ------------------------------------- */

static void
init_common_regs(struct parity_gt_context *ce, int inhibit)
{
	uint32_t *regs = ce->lrc_reg_state;
	uint32_t ctl;

	ctl = MASKED_ENABLE(PARITY_CTX_CTRL_INHIBIT_SYN_CTX_SWITCH);
	ctl |= MASKED_DISABLE(PARITY_CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT);
	if (inhibit)
		ctl |= PARITY_CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT;
	/*
	 * The GRAPHICS_VER < 11 branch (SAVE_INHIBIT / RS_CTX_ENABLE) does not
	 * apply, and ctx_needs_runalone() is false for a kernel context.
	 */
	regs[PARITY_CTX_CONTEXT_CONTROL] = ctl;
	regs[PARITY_CTX_TIMESTAMP] = 0u;   /* ce->stats.runtime.last */

	/* lrc_ring_bb_offset() is 0x70 on gen12, so the slot exists. */
	regs[PARITY_LRC_BB_OFFSET_INDEX + 1] = 0u;
}

static void
init_ppgtt_regs(struct parity_gt_context *ce)
{
	uint32_t *regs = ce->lrc_reg_state;
	uint64_t addr = ce->vm->top_pd_dma;

	/*
	 * ASSIGN_CTX_PML4: on a 4-level ppgtt PDP0 carries the top directory and
	 * every other PDP pair is ignored.  Writing the PDP1..3 pairs as well
	 * would be the 3-level branch, not extra safety.
	 */
	regs[PARITY_CTX_PDP0_UDW] = (uint32_t)(addr >> 32);
	regs[PARITY_CTX_PDP0_LDW] = (uint32_t)addr;
}

static void
reset_stop_ring(struct parity_gt_context *ce)
{
	uint32_t *regs = ce->lrc_reg_state;

	/* __reset_stop_ring(): clear the bit and mask it in. */
	regs[PARITY_LRC_MI_MODE_INDEX + 1] &= ~PARITY_LRC_STOP_RING;
	regs[PARITY_LRC_MI_MODE_INDEX + 1] |= PARITY_LRC_STOP_RING << 16;
}

void
parity_lrc_init_regs(struct parity_gt_context *ce, int inhibit)
{
	const uint8_t *offsets;

	if (ce == 0 || !ce->allocated)
		return;

	/* __lrc_init_regs(regs, ce, engine, inhibit). */
	if (inhibit)
		memset(ce->lrc_reg_state, 0, 4096u);

	offsets = parity_gen12_xcs_offsets;
	if (ce->ge->info->class == PARITY_RENDER_CLASS)
		offsets = parity_gen12_rcs_offsets;
	ce->reg_state_dwords = parity_lrc_set_offsets(ce->lrc_reg_state, offsets,
		ce->ge->info->mmio_base, inhibit);

	init_common_regs(ce, inhibit);
	init_ppgtt_regs(ce);
	/*
	 * init_wa_bb_regs() only acts when engine->wa_ctx has a size; the gen12
	 * path installs its batches from lrc_update_regs() instead (c2b).
	 */
	reset_stop_ring(ce);
}

void
parity_lrc_init_state(struct parity_gt_context *ce)
{
	int inhibit = 1;   /* engine->default_state is NULL until P6-c4 records it */

	if (ce == 0 || !ce->allocated)
		return;

	/* Clear the ppHWSP (including the per-context counters). */
	memset(ce->state->cpu, 0, 4096u);

	/* Clear the indirect wa and storage page. */
	if (ce->wa_bb_page != 0u)
		memset((char *)ce->state->cpu + ce->wa_bb_page * 4096u, 0, 4096u);

	parity_lrc_init_regs(ce, inhibit);
}

void
parity_lrc_reset(struct parity_gt_context *ce)
{
	if (ce == 0 || !ce->allocated)
		return;
	/* intel_ring_reset(ring, ring->emit): head = tail = emit. */
	ce->ring.head = ce->ring.emit;
	ce->ring.tail = ce->ring.emit;
	ce->ring.space = (ce->ring.head - ce->ring.emit - PARITY_CACHELINE_BYTES) &
		(ce->ring.size - 1u);
	/* Scrub away the garbage. */
	parity_lrc_init_regs(ce, 1);
	(void)parity_lrc_update_regs(ce, ce->ring.tail);
}

/* --- c2b: the INDIRECT_CTX and PER_CTX_BB batches -------------------------- */

uint32_t
parity_lrc_aux_inv_reg(int engine_id)
{
	switch (engine_id) {
	case PARITY_RCS0:  return 0x4208u;   /* GEN12_CCS_AUX_INV */
	case PARITY_BCS0:  return 0x4248u;   /* GEN12_BCS0_AUX_INV */
	case PARITY_VCS0:  return 0x4218u;   /* GEN12_VD0_AUX_INV */
	case PARITY_VCS2:  return 0x4298u;   /* GEN12_VD2_AUX_INV */
	case PARITY_VECS0: return 0x4238u;   /* GEN12_VE0_AUX_INV */
	default:           return 0u;
	}
}

/* Where one of the two extra pages starts, in the CPU view and in the GGTT. */
static uint32_t *
context_wabb(struct parity_gt_context *ce, int per_ctx)
{
	char *p = (char *)ce->state->cpu;

	p += ce->wa_bb_page * 4096u;
	if (per_ctx)
		p += 4096u;
	return (uint32_t *)p;
}

static uint32_t
lrc_indirect_bb(const struct parity_gt_context *ce)
{
	return (uint32_t)ce->state->ggtt_offset + ce->wa_bb_page * 4096u;
}

/*
 * gen12_emit_timestamp_wa(): the context timestamp is reloaded from the saved
 * register state through GPR0.  The second LRR is deliberate in the reference
 * (the register needs the write twice), so it is kept.
 */
static uint32_t *
emit_timestamp_wa(struct parity_gt_context *ce, uint32_t *cs)
{
	uint32_t base = ce->ge->info->mmio_base;

	*cs++ = PARITY_MI_LOAD_REGISTER_MEM_GEN8 |
		PARITY_MI_SRM_LRM_GLOBAL_GTT | PARITY_MI_LRI_LRM_CS_MMIO;
	*cs++ = PARITY_GEN8_RING_CS_GPR(base, 0);
	*cs++ = (uint32_t)ce->state->ggtt_offset + PARITY_LRC_STATE_OFFSET +
		PARITY_CTX_TIMESTAMP * 4u;
	*cs++ = 0u;

	*cs++ = PARITY_MI_LOAD_REGISTER_REG |
		PARITY_MI_LRR_SOURCE_CS_MMIO | PARITY_MI_LRI_LRM_CS_MMIO;
	*cs++ = PARITY_GEN8_RING_CS_GPR(base, 0);
	*cs++ = PARITY_RING_CTX_TIMESTAMP_REG(base);

	*cs++ = PARITY_MI_LOAD_REGISTER_REG |
		PARITY_MI_LRR_SOURCE_CS_MMIO | PARITY_MI_LRI_LRM_CS_MMIO;
	*cs++ = PARITY_GEN8_RING_CS_GPR(base, 0);
	*cs++ = PARITY_RING_CTX_TIMESTAMP_REG(base);

	return cs;
}

/* gen12_emit_cmd_buf_wa(): RENDER only (lrc_ring_cmd_buf_cctl is -1 elsewhere). */
static uint32_t *
emit_cmd_buf_wa(struct parity_gt_context *ce, uint32_t *cs)
{
	uint32_t base = ce->ge->info->mmio_base;

	*cs++ = PARITY_MI_LOAD_REGISTER_MEM_GEN8 |
		PARITY_MI_SRM_LRM_GLOBAL_GTT | PARITY_MI_LRI_LRM_CS_MMIO;
	*cs++ = PARITY_GEN8_RING_CS_GPR(base, 0);
	*cs++ = (uint32_t)ce->state->ggtt_offset + PARITY_LRC_STATE_OFFSET +
		(PARITY_LRC_RING_CMD_BUF_CCTL + 1) * 4u;
	*cs++ = 0u;

	*cs++ = PARITY_MI_LOAD_REGISTER_REG |
		PARITY_MI_LRR_SOURCE_CS_MMIO | PARITY_MI_LRI_LRM_CS_MMIO;
	*cs++ = PARITY_GEN8_RING_CS_GPR(base, 0);
	*cs++ = PARITY_RING_CMD_BUF_CCTL_REG(base);

	return cs;
}

static uint32_t *
emit_restore_scratch(struct parity_gt_context *ce, uint32_t *cs)
{
	uint32_t base = ce->ge->info->mmio_base;

	*cs++ = PARITY_MI_LOAD_REGISTER_MEM_GEN8 |
		PARITY_MI_SRM_LRM_GLOBAL_GTT | PARITY_MI_LRI_LRM_CS_MMIO;
	*cs++ = PARITY_GEN8_RING_CS_GPR(base, 0);
	*cs++ = (uint32_t)ce->state->ggtt_offset + PARITY_LRC_STATE_OFFSET +
		(PARITY_LRC_RING_GPR0 + 1) * 4u;
	*cs++ = 0u;

	return cs;
}

/*
 * gen12_emit_aux_table_inv(): ask for the invalidate, then WAIT until the
 * register reads back zero.  gsi_offset is 0 on the primary GT.
 */
static uint32_t *
emit_aux_table_inv(struct parity_gt_context *ce, uint32_t *cs)
{
	/*
	 * One emitter for both the ring flush and this batch: the sequence ends
	 * in a semaphore poll, and two copies of it are two chances to diverge.
	 */
	return parity_gen12_emit_aux_table_inv(ce->ge->info->id, cs);
}

/* Wa_18022495364: gfx IP 12.0 .. 12.10, which includes ADL-P. */
static uint32_t *
emit_invalidate_state_cache(uint32_t *cs)
{
	*cs++ = PARITY_MI_LOAD_REGISTER_IMM(1);
	*cs++ = PARITY_GEN12_CS_DEBUG_MODE2;
	*cs++ = MASKED_ENABLE(PARITY_INSTRUCTION_STATE_CACHE_INVALIDATE);
	return cs;
}

/*
 * setup_predicate_disable_wa(): the reference writes this at a FIXED offset
 * (DG2_PREDICATE_RESULT_BB) that lies OUTSIDE the executed range -- the
 * indirect size covers only the emitted commands -- so on ADL-P these dwords
 * are present but never run.  They are written anyway so the page matches the
 * reference byte for byte.
 */
static void
setup_predicate_disable_wa(struct parity_gt_context *ce, uint32_t *cs)
{
	*cs++ = PARITY_MI_STORE_DWORD_IMM_GEN4 | PARITY_MI_USE_GGTT;
	*cs++ = lrc_indirect_bb(ce) + PARITY_DG2_PREDICATE_RESULT_WA;
	*cs++ = 0u;
	*cs++ = 0u;   /* no predication */

	*cs++ = PARITY_MI_BATCH_BUFFER_END | (1u << 15);
	*cs++ = PARITY_MI_SET_PREDICATE;   /* MI_SET_PREDICATE_DISABLE is 0 */

	*cs++ = PARITY_MI_STORE_DWORD_IMM_GEN4 | PARITY_MI_USE_GGTT;
	*cs++ = lrc_indirect_bb(ce) + PARITY_DG2_PREDICATE_RESULT_WA;
	*cs++ = 0u;
	*cs++ = 1u;   /* enable predication before the next BB */

	*cs++ = PARITY_MI_BATCH_BUFFER_END;
}

static void
setup_indirect_ctx_bb(struct parity_gt_context *ce)
{
	uint32_t * const start = context_wabb(ce, 0);
	uint32_t *regs = ce->lrc_reg_state;
	uint32_t *cs = start;
	uint32_t size;

	cs = emit_timestamp_wa(ce, cs);
	if (ce->ge->info->class == PARITY_RENDER_CLASS)
		cs = emit_cmd_buf_wa(ce, cs);
	cs = emit_restore_scratch(ce, cs);
	/* Wa_16013000631 is DG2_G11 only. */
	cs = emit_aux_table_inv(ce, cs);
	if (ce->ge->info->class == PARITY_RENDER_CLASS)
		cs = emit_invalidate_state_cache(cs);
	/* Wa_16014892111 is 12.70/12.71 A0..B0 or DG2 only. */

	/* The executed size must be a whole number of cachelines. */
	while ((((uintptr_t)cs - (uintptr_t)start) % PARITY_CACHELINE_BYTES) != 0u)
		*cs++ = PARITY_MI_NOOP;

	size = (uint32_t)((cs - start) * 4u);
	setup_predicate_disable_wa(ce,
		start + PARITY_DG2_PREDICATE_RESULT_BB / 4u);

	/* lrc_setup_indirect_ctx(). */
	regs[PARITY_LRC_RING_INDIRECT_PTR + 1] =
		lrc_indirect_bb(ce) | (size / PARITY_CACHELINE_BYTES);
	regs[PARITY_LRC_RING_INDIRECT_OFFSET + 1] =
		PARITY_GEN12_INDIRECT_CTX_OFFSET_DEFAULT << 6;

	ce->indirect_bb_ggtt = lrc_indirect_bb(ce);
	ce->indirect_bb_dwords = (unsigned)(cs - start);
}

static void
setup_per_ctx_bb(struct parity_gt_context *ce)
{
	uint32_t * const start = context_wabb(ce, 1);
	uint32_t *regs = ce->lrc_reg_state;
	uint32_t *cs = start;

	/*
	 * xehp_emit_per_ctx_bb() emits the fastcolor BLT workaround only when
	 * NEEDS_FASTCOLOR_BLT_WABB (Xe_HP), so on ADL-P the batch is just its
	 * terminator -- which PER_CTX_BB must still carry by hand.
	 */
	*cs++ = PARITY_MI_BATCH_BUFFER_END;

	regs[PARITY_LRC_RING_WA_BB_PER_CTX + 1] =
		(lrc_indirect_bb(ce) + 4096u) |
		PARITY_PER_CTX_BB_FORCE | PARITY_PER_CTX_BB_VALID;
	ce->per_ctx_bb_set = 1;
}

/* --- lrc_update_regs ------------------------------------------------------ */

uint32_t
parity_lrc_descriptor(const struct parity_gt_context *ce)
{
	uint32_t desc;

	/* The kernel context always has a 4-level ppgtt on ADL-P. */
	desc = PARITY_INTEL_LEGACY_64B_CONTEXT;
	desc <<= PARITY_GEN8_CTX_ADDRESSING_MODE_SHIFT;
	desc |= PARITY_GEN8_CTX_VALID | PARITY_GEN8_CTX_PRIVILEGE;
	/* GEN8_CTX_L3LLC_COHERENT is GRAPHICS_VER == 8 only. */

	return (uint32_t)ce->state->ggtt_offset | desc;
}

uint32_t
parity_lrc_update_regs(struct parity_gt_context *ce, uint32_t head)
{
	uint32_t *regs;

	if (ce == 0 || !ce->allocated)
		return 0u;
	regs = ce->lrc_reg_state;

	regs[PARITY_CTX_RING_START] = (uint32_t)ce->ring.ggtt_offset;
	regs[PARITY_CTX_RING_HEAD] = head;
	regs[PARITY_CTX_RING_TAIL] = ce->ring.tail;
	regs[PARITY_CTX_RING_CTL] = PARITY_RING_CTL_SIZE(ce->ring.size) |
		PARITY_RING_VALID;

	if (ce->ge->info->class == PARITY_RENDER_CLASS) {
		/*
		 * Without this the render engine may come up with its execution
		 * units power gated: the fixed-function stages run and the first
		 * thread never dispatches.
		 */
		regs[PARITY_CTX_R_PWR_CLK_STATE] =
			parity_sseu_make_rpcs(ce->ge->sseu_slice_mask,
				ce->ge->sseu_has_slice_pg);
	}

	if (ce->wa_bb_page != 0u) {
		/*
		 * engine->wa_ctx.indirect_ctx.size is zero on gen12 (the global
		 * wa_ctx batch is mutually exclusive with these per-context
		 * ones), so both batches are built here.
		 */
		setup_indirect_ctx_bb(ce);
		setup_per_ctx_bb(ce);
	}

	ce->lrca = parity_lrc_descriptor(ce);
	/* ce->lrc.lrca = lrc_update_regs(...): the low dword of lrc.desc. */
	ce->lrc_desc = (ce->lrc_desc & 0xffffffff00000000ull) |
		(uint64_t)(ce->lrca | PARITY_CTX_DESC_FORCE_RESTORE);
	return ce->lrca | PARITY_CTX_DESC_FORCE_RESTORE;
}

void
parity_lrc_release(struct parity_gt_context *ce, struct parity_gt_mem *gm)
{
	if (ce == 0 || gm == 0)
		return;
	if (ce->ring.obj != 0) {
		parity_gt_object_destroy(gm, ce->ring.obj);
		ce->ring.obj = 0;
	}
	if (ce->state != 0) {
		parity_gt_object_destroy(gm, ce->state);
		ce->state = 0;
	}
	ce->ring.vaddr = 0;
	ce->lrc_reg_state = 0;
	ce->allocated = 0;
}
