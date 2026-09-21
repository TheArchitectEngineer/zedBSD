/*
 * WS031 Linux-parity — real-time register waits and delay (see wait.c).
 *
 * Ports __intel_wait_for_register_fw()'s two-stage wait and udelay() onto a
 * monotonic, IRQ-independent, CPU-migration-safe time source (the kernel's
 * rtc counter).  The atomic stage never sleeps (safe under a spinlock / IRQs
 * off); the optional slow stage yields (sleepable context only).
 */
#ifndef PARITY_WAIT_H
#define PARITY_WAIT_H

#include <stdint.h>

struct osdep_mmio;

/*
 * Busy-wait for `us` microseconds using the monotonic counter.  Returns 0 on
 * success, or -EIO if the time base faults mid-delay (the caller must then
 * release any lock/forcewake and stop, not proceed as if the delay elapsed).
 */
int parity_udelay(unsigned us);

/* Non-zero once a TSC frequency has been cached (i.e. a usable time base exists). */
int parity_wait_time_base_ok(void);

/* Non-zero if any time-base read has failed during the probe (sticky). */
int parity_wait_time_base_faulted(void);

/*
 * Wait until (raw_read(reg) & mask) == value: an atomic (busy) poll for up to
 * fast_us, then, if slow_ms > 0, a sleepable (yielding) poll for up to slow_ms.
 * Returns 0 on match or -ETIMEDOUT on timeout; *out (optional) receives the last
 * value read.  slow_ms > 0 must only be used from a sleepable context.
 */
int parity_wait_reg(struct osdep_mmio *m, uint32_t reg, uint32_t mask, uint32_t value,
	unsigned fast_us, unsigned slow_ms, uint32_t *out);

/*
 * Test-only time-source injection (unit B).  Production leaves it NULL and the
 * waits use the real HAL counter; a test passes its own context + response
 * sequence and drives the SAME wait/udelay/reset function bodies.
 */
struct wait_queue;
struct spinlock;
struct parity_time_test_ops {
	int (*read)(void *ctx, uint64_t *counter, uint64_t *freq);  /* 1 ok / 0 fail */
	void *ctx;
	int slow_sleep_override;   /* 1 = the slow-stage sleep returns slow_sleep_rc */
	int slow_sleep_rc;
};
void parity_wait_test_set(const struct parity_time_test_ops *ops);
/* Test-only: clear the sticky time-base fault latch. */
void parity_wait_test_reset_fault(void);

#endif /* PARITY_WAIT_H */
