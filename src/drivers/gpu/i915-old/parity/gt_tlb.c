/*
 * WS031 Linux-parity -- the GT TLB invalidation (see gt_tlb.h).  zedBSD project code; register numbers and the
 * request / done encodings are those of the reference (gt/intel_gt_regs.h, i915_perf_oa_regs.h,
 * gt/intel_engine_cs.c intel_engine_init_tlb_invalidation(), gt/intel_tlb.c mmio_invalidate_full()).
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <errno.h>
#include "osdep/mmio.h"
#include "wait.h"
#include "gt_mmio.h"
#include "gt_resume.h"
#include "gt_tlb.h"

#define GEN12_GFX_TLB_INV_CR      0xced8u
#define GEN12_VD_TLB_INV_CR       0xcedcu
#define GEN12_VE_TLB_INV_CR       0xcee0u
#define GEN12_BLT_TLB_INV_CR      0xcee4u
#define GEN12_COMPCTX_TLB_INV_CR  0xcf04u
#define GEN12_OA_TLB_INV_CR       0xceecu
#define TLB_INVAL_TIMEOUT_US      100u
#define TLB_INVAL_TIMEOUT_MS      4u
#define COMPUTE_CLASS             5

int
parity_gt_tlb_engine_reg(int class, int instance, uint32_t *reg, uint32_t *request, uint32_t *done)
{
	uint32_t r, val;

	switch (class) {
	case PARITY_RENDER_CLASS:            r = GEN12_GFX_TLB_INV_CR; break;
	case PARITY_VIDEO_DECODE_CLASS:      r = GEN12_VD_TLB_INV_CR; break;
	case PARITY_VIDEO_ENHANCEMENT_CLASS: r = GEN12_VE_TLB_INV_CR; break;
	case PARITY_COPY_ENGINE_CLASS:       r = GEN12_BLT_TLB_INV_CR; break;
	case COMPUTE_CLASS:                  r = GEN12_COMPCTX_TLB_INV_CR; break;
	default:                             return -ERANGE;
	}
	if (instance < 0 || instance > 15)
		return -ERANGE;
	val = 1u << (unsigned)instance;
	*reg = r;
	*done = val;
	/* GRAPHICS_VER >= 12: VD / VE / compute use a masked register */
	*request = (class == PARITY_VIDEO_DECODE_CLASS || class == PARITY_VIDEO_ENHANCEMENT_CLASS || class == COMPUTE_CLASS) ?
		(val << 16) | val : val;
	return 0;
}

int
parity_gt_invalidate_tlb_full(struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m,
	struct spinlock *uncore_lock)
{
	static const int fwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT, OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2,
		OSDEP_FW_MEDIA_VEBOX0 };
	uint32_t reg[16], done[16], request;
	unsigned i, n = 0u, held = 0u;
	unsigned long flags;
	int rc = 0;

	if (tlb == 0 || es == 0 || m == 0 || uncore_lock == 0 || es->n > 16u)
		return -EINVAL;
	/* intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL) */
	while (held < 5u && osdep_fw_get(m, fwd[held]) == 0)
		held++;
	if (held != 5u) {
		tlb->fw_failures++;
		rc = -EIO;
		goto out;
	}
	/* serialise invalidate with GT reset */
	flags = spin_lock_irqsave(uncore_lock);
	for (i = 0u; i < es->n; i++) {
		if (parity_gt_tlb_engine_reg(es->ge[i].info->class, es->ge[i].info->instance, &reg[n], &request, &done[n]) != 0)
			continue;
		osdep_mmio_raw_write32(m, reg[n], request);
		n++;
	}
	/* Wa_2207587034:tgl,dg1,rkl,adl-s,adl-p */
	if (n != 0u)
		osdep_mmio_raw_write32(m, GEN12_OA_TLB_INV_CR, 1u);
	spin_unlock_irqrestore(uncore_lock, flags);

	for (i = 0u; i < n; i++) {
		int w = parity_wait_reg(m, reg[i], done[i], 0u, TLB_INVAL_TIMEOUT_US, TLB_INVAL_TIMEOUT_MS, 0);

		if (w == -ETIMEDOUT) {
			tlb->timeouts++;
			kern_logf("i915: parity TLB invalidation did not complete in %ums (reg 0x%x done bit 0x%x still set) "
				"backend=%s test=%s expected_fault=%d\n", TLB_INVAL_TIMEOUT_MS, reg[i], done[i],
				tlb->backend != 0 ? tlb->backend : "HW", tlb->test_id != 0 ? tlb->test_id : "-", tlb->expected_fault);
			if (rc == 0)
				rc = -ETIMEDOUT;
		} else if (w != 0) {
			tlb->time_faults++;
			rc = -EIO;
		}
	}
	tlb->engines_invalidated = n;
	if (rc == 0) {
		tlb->invalidations++;
		tlb->seqno += 2u;       /* write_seqcount_invalidate(&gt->tlb.seqno) */
	}
out:
	while (held-- > 0u)
		osdep_fw_put(m, fwd[held]);
	return rc;
}
