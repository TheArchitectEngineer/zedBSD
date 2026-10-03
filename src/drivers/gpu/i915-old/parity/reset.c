/*
 * WS031 Linux-parity — GT reset (see reset.h).
 *
 * Faithful port of __intel_gt_reset(ALL_ENGINES) -> gen8_reset_engines ->
 * __gen11_reset_engines -> gen6_hw_domain_reset for an empty engine set (the P1
 * engine list is not yet created, so the reference's per-engine prepare/cancel
 * iterations are empty and no engines are pre-created for them).
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <errno.h>
#include "osdep/mmio.h"
#include "wait.h"
#include "reset.h"

#define PARITY_GDRST_REG   0x941cu   /* GEN6_GDRST */
#define PARITY_GRDOM_FULL  0x1u      /* GEN11_GRDOM_FULL: ALL_ENGINES full soft reset */
#define RESET_MAX_RETRIES  3u
#define RESET_SETTLE_US    50u       /* udelay(50) after the ack */

int
parity_gt_reset_all(struct spinlock *uncore_lock, struct osdep_mmio *m, unsigned fast_us)
{
	const uint32_t reset_mask = PARITY_GRDOM_FULL;
	unsigned attempt;
	int ret = -ETIMEDOUT;   /* retry only while the ack times out */
	int rc;

	/* FORCEWAKE_ALL, held across the whole reset (outside the uncore lock). */
	rc = osdep_fw_get(m, OSDEP_FW_RENDER);
	if (rc != 0)
		return rc;
	rc = osdep_fw_get(m, OSDEP_FW_GT);
	if (rc != 0) {
		osdep_fw_put(m, OSDEP_FW_RENDER);
		return rc;
	}

	for (attempt = 0u; ret == -ETIMEDOUT && attempt < RESET_MAX_RETRIES; attempt++) {
		unsigned long flags;
		unsigned passes = 0u;
		int loops = 2;          /* ADL-P (IP < 12.70): up to two passes */
		int loop_err = 0;

		/* gen8_reset_engines: spin_lock_irqsave(&uncore->lock). */
		flags = spin_lock_irqsave(uncore_lock);

		/* for_each_engine_masked(prepare): P1 engine set empty -> no-op. */

		/* __gen11_reset_engines(ALL_ENGINES) -> gen6_hw_domain_reset(). */
		do {
			osdep_mmio_raw_write32(m, PARITY_GDRST_REG, reset_mask);
			loop_err = parity_wait_reg(m, PARITY_GDRST_REG, reset_mask, 0u,
				fast_us, 0u, 0);
			passes++;
		} while (loop_err == 0 && --loops);

		/* udelay(50): engine state stays volatile briefly after the ack. */
		rc = parity_udelay(RESET_SETTLE_US);
		if (rc != 0) {
			/*
			 * Time base faulted mid-reset: release the uncore lock and
			 * abandon the reset with the fault code -- do NOT retry (a
			 * broken clock cannot bound the ack wait).  Forcewake is
			 * dropped by the shared exit path below.
			 */
			spin_unlock_irqrestore(uncore_lock, flags);
			ret = rc;
			kern_logf("i915: parity P1 gt_reset attempt=%u passes=%u "
				"time-base fault rc=%d\n", attempt, passes, rc);
			break;
		}

		/* for_each_engine_masked(cancel): P1 engine set empty -> no-op. */

		spin_unlock_irqrestore(uncore_lock, flags);

		ret = loop_err;
		kern_logf("i915: parity P1 gt_reset attempt=%u passes=%u rc=%d\n",
			attempt, passes, ret);
	}

	osdep_fw_put(m, OSDEP_FW_GT);
	osdep_fw_put(m, OSDEP_FW_RENDER);
	return ret;
}
