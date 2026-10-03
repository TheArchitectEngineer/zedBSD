/*
 * WS031 Linux-parity — common GEN6+ PCODE (sideband) mailbox (see pcode.h).
 *
 * Direct port of intel_pcode.c: the caller-owned sb_lock (mutex) serialises the
 * transaction, the fast stage is an atomic poll and the slow stage is sleepable
 * (parity_wait_reg), and the gen7 mailbox status maps to a negative errno.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/sched.h>
#include <errno.h>
#include "osdep/mmio.h"
#include "wait.h"
#include "pcode.h"

/* GEN6_PCODE_* registers (i915_reg.h). */
#define PCODE_MAILBOX   0x138124u
#define PCODE_DATA      0x138128u
#define PCODE_DATA1     0x13812cu
#define PCODE_READY     0x80000000u   /* GEN6_PCODE_READY = (1 << 31) */
#define PCODE_ERR_MASK  0xffu         /* GEN6_PCODE_ERROR_MASK */

#define PCODE_FAST_US   500u          /* snb_pcode_read fast_timeout_us */
#define PCODE_SLOW_MS   20u           /* snb_pcode_read slow_timeout_ms */

/* gen7_check_mailbox_status: ADL-P is GRAPHICS_VER 12 (> 6). */
static int
pcode_check_status(uint32_t mbox)
{
	switch (mbox & PCODE_ERR_MASK) {
	case 0x0:  return 0;            /* GEN6_PCODE_SUCCESS */
	case 0x1:  return -ENXIO;       /* GEN6_PCODE_ILLEGAL_CMD */
	case 0x2:  return -ETIMEDOUT;   /* GEN7_PCODE_TIMEOUT */
	case 0x3:  return -EINVAL;      /* GEN7_PCODE_ILLEGAL_DATA */
	case 0x4:  return -ENXIO;       /* GEN11_PCODE_ILLEGAL_SUBCOMMAND */
	case 0x6:  return -EBUSY;       /* GEN11_PCODE_LOCKED */
	case 0x11: return -EACCES;      /* GEN11_PCODE_REJECTED */
	case 0x10: return -EOVERFLOW;   /* GEN7_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE */
	default:   return 0;
	}
}

/* __snb_pcode_rw: caller holds sb_lock.  GEN6_PCODE_* are outside forcewake. */
static int
parity_snb_pcode_rw(struct osdep_mmio *m, uint32_t mbox, uint32_t *val, uint32_t *val1,
	unsigned fast_us, unsigned slow_ms, int is_read)
{
	uint32_t observed = 0;
	int err;

	if (osdep_mmio_raw_read32(m, PCODE_MAILBOX) & PCODE_READY)
		return -EAGAIN;

	osdep_mmio_raw_write32(m, PCODE_DATA, *val);
	osdep_mmio_raw_write32(m, PCODE_DATA1, val1 ? *val1 : 0u);
	osdep_mmio_raw_write32(m, PCODE_MAILBOX, PCODE_READY | mbox);

	/* Fast atomic poll, then sleepable slow poll (mutex held, so sleep is OK). */
	err = parity_wait_reg(m, PCODE_MAILBOX, PCODE_READY, 0u, fast_us, slow_ms, &observed);
	if (err != 0)
		return err;   /* propagate -ETIMEDOUT or the -EIO time-base anomaly distinctly */

	if (is_read) {
		*val = osdep_mmio_raw_read32(m, PCODE_DATA);
		if (val1)
			*val1 = osdep_mmio_raw_read32(m, PCODE_DATA1);
	}

	/* GRAPHICS_VER > 6: gen7 status check on the final mailbox value. */
	return pcode_check_status(observed);
}

int
parity_pcode_read(struct mutex *sb_lock, struct osdep_mmio *m,
	uint32_t mbox, uint32_t *val, uint32_t *val1)
{
	int err;

	mutex_lock(sb_lock);
	err = parity_snb_pcode_rw(m, mbox, val, val1, PCODE_FAST_US, PCODE_SLOW_MS, 1);
	mutex_unlock(sb_lock);

	if (err != 0)
		kern_logf("i915: parity pcode read mbox=0x%08x err=%d\n", mbox, err);
	return err;
}

int
parity_snb_pcode_write_timeout(struct mutex *sb_lock, struct osdep_mmio *m,
	uint32_t mbox, uint32_t val, unsigned fast_us, unsigned slow_ms)
{
	uint32_t v = val;
	int err;

	mutex_lock(sb_lock);
	err = parity_snb_pcode_rw(m, mbox, &v, 0, fast_us, slow_ms, 0 /* write */);
	mutex_unlock(sb_lock);

	if (err != 0)
		kern_logf("i915: parity pcode write mbox=0x%08x err=%d\n", mbox, err);
	return err;
}

int
parity_snb_pcode_write(struct mutex *sb_lock, struct osdep_mmio *m, uint32_t mbox, uint32_t val)
{
	/* snb_pcode_write(mbox, val) = snb_pcode_write_timeout(mbox, val, 500, 0). */
	return parity_snb_pcode_write_timeout(sb_lock, m, mbox, val, PCODE_FAST_US, 0u);
}

/*
 * skl_pcode_try_request(): send one request (atomic: fast stage only) and check
 * (reply & reply_mask) == reply against the read-back value.  *status carries the
 * transaction result (0, a PCODE errno, -ETIMEDOUT, or -EIO).
 */
static int
parity_skl_pcode_try_request(struct osdep_mmio *m, uint32_t mbox, uint32_t request,
	uint32_t reply_mask, uint32_t reply, int *status)
{
	uint32_t reqval = request;

	/* __snb_pcode_rw(mbox, &request, NULL, 500, 0, true). */
	*status = parity_snb_pcode_rw(m, mbox, &reqval, 0, PCODE_FAST_US, 0u, 1);
	return (*status == 0) && ((reqval & reply_mask) == reply);
}

/* Bounded re-request poll over `ms` milliseconds (real-time counter). */
static int
parity_pcode_poll(struct osdep_mmio *m, uint32_t mbox, uint32_t request,
	uint32_t reply_mask, uint32_t reply, int *status, unsigned ms, int sleep_between)
{
	uint64_t base = 0, freq = 0, now = 0, nfreq = 0, target;

	if (!kern_rtc_read_counter(&base, &freq) || freq == 0u)
		return -EIO;
	target = ((uint64_t)ms * freq) / 1000u;
	for (;;) {
		if (parity_skl_pcode_try_request(m, mbox, request, reply_mask, reply, status))
			return 0;
		if (*status == -EIO)
			return -EIO;   /* time-base anomaly: stop (do not mask as PCODE timeout) */
		if (!kern_rtc_read_counter(&now, &nfreq) || nfreq != freq)
			return -EIO;
		if (now - base >= target)
			return -ETIMEDOUT;
		if (sleep_between)
			kern_usleep_range(10u, 20u);   /* normal region: usleep_range(10, 20) */
		else
			kern_compiler_barrier();       /* preemption-off region: no sleep */
	}
}

int
parity_skl_pcode_request(struct mutex *sb_lock, struct osdep_mmio *m, uint32_t mbox,
	uint32_t request, uint32_t reply_mask, uint32_t reply, int timeout_base_ms)
{
	int status = 0;
	int ret;

	mutex_lock(sb_lock);

	/* Prime the PCODE with an explicit first request (reference: send it now). */
	if (parity_skl_pcode_try_request(m, mbox, request, reply_mask, reply, &status)) {
		ret = 0;
		goto out;
	}
	if (status == -EIO) {   /* time-base anomaly: stop and release the lock */
		ret = -EIO;
		goto out;
	}

	/* Normal region: sleepable re-request poll (usleep_range(10,20) between). */
	ret = parity_pcode_poll(m, mbox, request, reply_mask, reply, &status,
		(unsigned)timeout_base_ms, 1 /* sleep between */);
	if (ret == 0 || ret == -EIO)
		goto out;

	/*
	 * Additional region: retry with PREEMPTION DISABLED for 50ms to maximise the
	 * number of requests (reference).  Preemption is genuinely disabled here (a
	 * scheduler preempt-count, not an interrupts-off spin), and there is no sleep
	 * in this region; the preempt state is restored on every exit.
	 */
	kern_logf("i915: parity pcode request mbox=0x%08x timeout, retrying (preempt off)\n", mbox);
	kern_preempt_disable();
	ret = parity_pcode_poll(m, mbox, request, reply_mask, reply, &status, 50u, 0 /* no sleep */);
	kern_preempt_enable();

out:
	mutex_unlock(sb_lock);
	/* Reference: return the PCODE status if set, else the wait result. */
	return status ? status : ret;
}
