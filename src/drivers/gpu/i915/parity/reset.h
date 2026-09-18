/*
 * WS031 Linux-parity — GT reset (see reset.c).
 *
 * __intel_gt_reset(ALL_ENGINES) for an empty engine set, extracted so the
 * control flow (uncore-lock section, retry-only-on-ETIMEDOUT, two write/poll
 * passes, real-time ack poll + settle) is testable against a mock MMIO backend.
 */
#ifndef PARITY_RESET_H
#define PARITY_RESET_H

#include <stdint.h>

struct osdep_mmio;
struct spinlock;

/*
 * Reset every graphics domain (GEN11_GRDOM_FULL).  Holds FORCEWAKE_ALL across
 * the reset and, per attempt, the uncore lock (spin_lock_irqsave); retries up to
 * RESET_MAX_RETRIES only while the ack times out; for ADL-P (IP < 12.70) issues
 * two write/poll passes per attempt plus a udelay(50) settle.  Each ack poll is
 * a real-time atomic wait bounded by fast_us.  Returns 0, a forcewake error, or
 * -ETIMEDOUT.
 */
int parity_gt_reset_all(struct spinlock *uncore_lock, struct osdep_mmio *m,
	unsigned fast_us);

#endif /* PARITY_RESET_H */
