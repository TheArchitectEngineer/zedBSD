/*
 * WS031 Linux-parity — P6-c1: per-engine setup for execlists submission.
 * See gt_engine.h for what each piece stands for in the reference.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/device-io.h>
#include <errno.h>
#include <string.h>
#include "gt_mmio.h"
#include "gt_mem.h"
#include "gt_engine.h"
#include "osdep/mmio.h"
#include "wait.h"

/* A masked register write: the changed bits are named in the upper half. */
#define MASKED_ENABLE(bits)   ((((uint32_t)(bits)) << 16) | ((uint32_t)(bits)))
#define MASKED_DISABLE(bits)  (((uint32_t)(bits)) << 16)

/* --- init_status_page ----------------------------------------------------- */

int
parity_engine_setup_common(struct parity_gt_engine *ge,
	struct parity_engine *info, struct parity_gt_mem *gm,
	const struct parity_sseu *sseu)
{
	unsigned i;
	int rc;

	if (ge == 0 || info == 0 || gm == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*ge); i++)
		((char *)ge)[i] = 0;
	ge->info = info;

	/*
	 * init_status_page(): one page, pinned in the GGTT and zeroed.  The
	 * reference maps it I915_MAP_WB and relies on LLC coherency; the DMA
	 * vector behind a GT object is coherent for the same reason.
	 */
	ge->status_page = parity_gt_object_create(gm, PARITY_GT_PAGE_BYTES);
	if (ge->status_page == 0)
		return -ENOMEM;
	rc = parity_gt_ggtt_bind(gm, ge->status_page);
	if (rc != 0) {
		parity_gt_object_destroy(gm, ge->status_page);
		ge->status_page = 0;
		return rc;
	}
	ge->hwsp = (volatile uint32_t *)ge->status_page->cpu;
	ge->hwsp_ggtt = ge->status_page->ggtt_offset;

	/* intel_engine_init_execlists(): one port pair, nothing in flight. */
	ge->port_mask = 1u;

	/* "Use the whole device by default." */
	if (sseu != 0) {
		ge->sseu_slice_mask = sseu->slice_mask;
		ge->sseu_has_slice_pg = sseu->has_slice_pg;
	}

	ge->setup_done = 1;
	return 0;
}

/* --- intel_execlists_submission_setup ------------------------------------- */

void
parity_execlists_submission_setup(struct parity_gt_engine *ge)
{
	uint32_t base;

	if (ge == 0 || !ge->setup_done)
		return;
	base = ge->info->mmio_base;

	/*
	 * HAS_LOGICAL_RING_ELSQ on gen11+: submission is the 64-bit ELSQ
	 * register pair plus a control write, not the four-dword ELSP.
	 */
	ge->submit_reg = PARITY_RING_EXECLIST_SQ_CONTENTS(base);
	ge->ctrl_reg = PARITY_RING_EXECLIST_CONTROL(base);

	ge->csb_status = (volatile uint64_t *)&ge->hwsp[PARITY_HWS_CSB_BUF0_INDEX];
	ge->csb_write = &ge->hwsp[PARITY_ICL_HWS_CSB_WRITE_INDEX];
	ge->csb_size = PARITY_GEN11_CSB_ENTRIES;

	/*
	 * GRAPHICS_VER >= 11 and GRAPHICS_VER_FULL < 12.50: the engine class and
	 * instance travel in the upper dword of the context descriptor, so they
	 * are pre-shifted by 32 here exactly as the reference does.
	 */
	ge->ccid = 0u;
	ge->ccid |= (uint32_t)ge->info->instance <<
		(PARITY_GEN11_ENGINE_INSTANCE_SHIFT - 32);
	ge->ccid |= (uint32_t)ge->info->class <<
		(PARITY_GEN11_ENGINE_CLASS_SHIFT - 32);
}

/* --- enable_execlists ----------------------------------------------------- */

static void
wr(struct parity_gt_engine *ge, struct osdep_mmio *m, uint32_t reg, uint32_t v)
{
	osdep_mmio_write32(m, reg, v);
	ge->enable_writes++;
}

/* enable_error_interrupt(): clear, look, then unmask only the fatal one. */
static void
enable_error_interrupt(struct parity_gt_engine *ge, struct osdep_mmio *m)
{
	uint32_t base = ge->info->mmio_base;

	wr(ge, m, PARITY_RING_EMR(base), ~0u);
	wr(ge, m, PARITY_RING_EIR(base), ~0u);   /* clear all existing errors */

	ge->esr_at_resume = osdep_mmio_read32(m, PARITY_RING_ESR(base));
	if (ge->esr_at_resume != 0u) {
		/*
		 * The reference resets the engine here.  Record it loudly instead
		 * of resetting behind the caller's back: a non-zero ESR at resume
		 * is a condition the caller must see, not one to paper over.
		 */
		kern_logf("i915: parity engine '%s' resumed still in error: %08x\n",
			ge->info->name, ge->esr_at_resume);
	}

	/*
	 * Only I915_ERROR_INSTRUCTION is unmasked.  CP_PRIV fires for cases the
	 * hardware already suppresses, so the reference leaves it masked.
	 */
	wr(ge, m, PARITY_RING_EMR(base), ~PARITY_I915_ERROR_INSTRUCTION);
}

void
parity_execlists_enable(struct parity_gt_engine *ge, struct osdep_mmio *m)
{
	uint32_t base;

	if (ge == 0 || !ge->setup_done || m == 0)
		return;
	base = ge->info->mmio_base;

	/* intel_engine_set_hwsp_writemask(engine, ~0u) -> RING_HWSTAM. */
	wr(ge, m, PARITY_RING_HWSTAM(base), ~0u);

	/* gen11+: disable the legacy ring-buffer mode, not GFX_RUN_LIST_ENABLE. */
	wr(ge, m, PARITY_RING_MODE_GEN7(base),
		MASKED_ENABLE(PARITY_GEN11_GFX_DISABLE_LEGACY_MODE));

	wr(ge, m, PARITY_RING_MI_MODE(base), MASKED_DISABLE(PARITY_STOP_RING));

	wr(ge, m, PARITY_RING_HWS_PGA(base), (uint32_t)ge->hwsp_ggtt);
	osdep_mmio_posting_read32(m, PARITY_RING_HWS_PGA(base));

	enable_error_interrupt(ge, m);
	ge->resumed = 1;
}

/* --- reset_csb_pointers --------------------------------------------------- */

void
parity_execlists_reset_csb_pointers(struct parity_gt_engine *ge,
	struct osdep_mmio *m)
{
	uint32_t base;
	uint32_t reset_value;
	uint32_t ptr;
	unsigned i;

	if (ge == 0 || !ge->setup_done || m == 0)
		return;
	base = ge->info->mmio_base;
	reset_value = (uint32_t)ge->csb_size - 1u;
	ptr = (0xffffu << 16) | (reset_value << 8) | reset_value;

	/*
	 * ring_set_paused(engine, 0).  reset.prepare left PREEMPT at 1, and every
	 * breadcrumb tail ends in a semaphore wait for it to be 0: without this
	 * the engine would finish each request and then spin forever.
	 */
	parity_ring_set_paused(ge, 0);

	/*
	 * Icelake sometimes forgets to reset its pointers over a GPU reset, so
	 * the reference writes them by hand -- twice, with the software head and
	 * the HWSP write pointer set BETWEEN the two writes.
	 */
	osdep_mmio_write32(m, PARITY_RING_CONTEXT_STATUS_PTR(base), ptr);
	osdep_mmio_posting_read32(m, PARITY_RING_CONTEXT_STATUS_PTR(base));
	ge->csb_reset_writes++;

	/*
	 * After a reset the hardware starts writing at entry [0], so the head is
	 * parked one entry BEHIND it: the first entry compared is entry 0 even
	 * though no interrupt has arrived yet.
	 */
	ge->csb_head = reset_value;
	*ge->csb_write = reset_value;
	kern_io_write_barrier();

	/* Every entry is poisoned, so a stale entry cannot pass for a real one. */
	for (i = 0u; i <= reset_value; i++)
		ge->csb_status[i] = ~(uint64_t)0;

	osdep_mmio_write32(m, PARITY_RING_CONTEXT_STATUS_PTR(base), ptr);
	osdep_mmio_posting_read32(m, PARITY_RING_CONTEXT_STATUS_PTR(base));
	ge->csb_reset_writes++;
}

void
parity_engine_release(struct parity_gt_engine *ge, struct parity_gt_mem *gm)
{
	if (ge == 0 || gm == 0)
		return;
	if (ge->status_page != 0) {
		parity_gt_object_destroy(gm, ge->status_page);
		ge->status_page = 0;
	}
	ge->hwsp = 0;
	ge->csb_status = 0;
	ge->csb_write = 0;
	ge->setup_done = 0;
	ge->resumed = 0;
}

/* --- execlists reset.prepare pieces ---------------------------------------- */

void
parity_ring_set_paused(struct parity_gt_engine *ge, int state)
{
	if (ge == 0 || ge->hwsp == 0)
		return;
	ge->hwsp[PARITY_I915_GEM_HWS_PREEMPT] = (uint32_t)state;
	if (state)
		kern_io_write_barrier();
}

int
parity_engine_stop_cs(struct parity_gt_engine *ge, struct osdep_mmio *m)
{
	uint32_t base, mode;
	int rc;

	if (ge == 0 || m == 0)
		return -EINVAL;
	base = ge->info->mmio_base;
	mode = PARITY_RING_MI_MODE(base);

	osdep_mmio_write32(m, mode, MASKED_ENABLE(PARITY_STOP_RING));

	/*
	 * Wa_22011802037 (gen11 .. < 12.70, so ADL-P): stop the prefetcher too,
	 * so the CS is really halted before the reset that follows.
	 */
	osdep_mmio_write32(m, PARITY_RING_MODE_GEN7(base),
		MASKED_ENABLE(PARITY_GEN12_GFX_PREFETCH_DISABLE));

	/* __intel_wait_for_register_fw(mode, MODE_IDLE, 1000 us, stop_timeout). */
	rc = parity_wait_reg(m, mode, PARITY_MODE_IDLE, PARITY_MODE_IDLE,
		1000u, 100u /* CONFIG_DRM_I915_STOP_TIMEOUT */, 0);
	osdep_mmio_posting_read32(m, mode);

	if (rc == -EIO)
		return rc;   /* the time base, not the hardware */
	if (rc != 0) {
		uint32_t head = osdep_mmio_read32(m, PARITY_RING_HEAD_REG(base)) &
			PARITY_HEAD_ADDR;
		uint32_t tail = osdep_mmio_read32(m, PARITY_RING_TAIL_REG(base)) &
			PARITY_TAIL_ADDR;

		/*
		 * MODE_IDLE sometimes stays clear on an empty ring; only a ring
		 * that still holds work is a real timeout.
		 */
		kern_logf("i915: parity %s stop_cs: MODE_IDLE timeout, head=%04x tail=%04x\n",
			ge->info->name, head, tail);
		if (head != tail)
			return -ETIMEDOUT;
	}
	return 0;
}

static uint32_t
msg_idle_reg(int engine_id)
{
	switch (engine_id) {
	case PARITY_RCS0:  return 0x8000u;   /* MSG_IDLE_CS */
	case PARITY_VCS0:  return 0x8004u;   /* MSG_IDLE_VCS0 */
	case PARITY_BCS0:  return 0x800cu;   /* MSG_IDLE_BCS */
	case PARITY_VECS0: return 0x8010u;   /* MSG_IDLE_VECS0 */
	case PARITY_VCS2:  return 0x80c0u;   /* MSG_IDLE_VCS2 */
	default:           return 0u;
	}
}

void
parity_engine_wait_for_pending_mi_fw(struct parity_gt_engine *ge,
	struct osdep_mmio *m)
{
	uint32_t reg, val, pending;

	if (ge == 0 || m == 0)
		return;
	reg = msg_idle_reg(ge->info->id);
	if (reg == 0u)
		return;

	/* __cs_pending_mi_force_wakes(): bits[29:25] & bits[13:9], shifted down. */
	val = osdep_mmio_read32(m, reg);
	pending = (val & (val >> 16) & PARITY_MSG_IDLE_FW_MASK) >> PARITY_MSG_IDLE_FW_SHIFT;
	ge->mi_fw_pending = pending;
	if (pending == 0u)
		return;

	/* __gpm_wait_for_fw_complete(): let GPM see the stop, wait, let CS see it. */
	(void)parity_udelay(1u);
	if (parity_wait_reg(m, PARITY_GEN9_PWRGT_DOMAIN_STATUS, pending, pending,
			5000u, 0u, 0) != 0)
		kern_logf("i915: parity %s: pending forcewake 0x%x did not complete\n",
			ge->info->name, pending);
	(void)parity_udelay(1u);
}

void
parity_execlists_reset_prepare(struct parity_gt_engine *ge, struct osdep_mmio *m)
{
	if (ge == 0 || m == 0)
		return;
	/* __tasklet_disable_sync_once(): there is no tasklet here to disable. */
	parity_ring_set_paused(ge, 1);
	ge->stop_cs_rc = parity_engine_stop_cs(ge, m);
	/* Wa_22011802037 */
	parity_engine_wait_for_pending_mi_fw(ge, m);
}
