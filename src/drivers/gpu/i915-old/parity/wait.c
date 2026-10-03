/*
 * WS031 Linux-parity — real-time register waits and delay (see wait.h).
 *
 * The time source is the kernel/HAL common backend, kern_rtc_read_counter(),
 * which returns a monotonic counter and its frequency.  The HAL guard now clamps
 * a small cross-CPU backwards step to the last (monotonic) value instead of
 * disabling the source, so the backend stays usable under KVM multi-vCPU; this
 * file therefore contains NO rdtsc inline assembly and NO scheduler-tick
 * calibration -- it only expresses register conditions, fast/slow timeouts, and
 * result handling against the backend.
 *
 * The atomic stage never sleeps (safe with a spinlock held / IRQs off); the slow
 * stage sleeps on the kernel wait infrastructure (sleepable context only).  A
 * time-base read failure is distinct: it returns -EIO and latches a sticky,
 * shared fault so the probe cannot mistake it for a normal completion or an HW
 * -ETIMEDOUT.  Each wait captures (counter, frequency) as one pair at its start
 * and refuses to keep computing deltas if the frequency later changes or reads
 * zero (a unit change), rather than producing a nonsense elapsed time.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/clock.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include <kern/thread.h>
#include <kern/sched.h>
#include <kern/device-io.h>
#include <errno.h>
#include "osdep/mmio.h"
#include "wait.h"

/* Latched time-base fault cause (never overwritten after the first). */
#define PARITY_TB_OK         0
#define PARITY_TB_NO_COUNTER 1   /* counter/frequency unavailable at wait start */
#define PARITY_TB_READ_FAIL  2   /* counter read failed / frequency changed mid-wait */
#define PARITY_TB_WAIT_API   3   /* waitq_sleep() returned a wait-API error */

/*
 * Sticky, shared, IRQ-safe fault latch.  Set from any CPU/context via a
 * compare-exchange that only records the FIRST cause (0 -> cause); later faults
 * leave the original reason intact so it is never overwritten.
 */
static volatile int parity_time_base_fault;

static void
parity_time_base_set_fault(int cause)
{
	int expected = PARITY_TB_OK;

	(void)__atomic_compare_exchange_n(&parity_time_base_fault, &expected, cause,
	    0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

int
parity_wait_time_base_faulted(void)
{
	return __atomic_load_n(&parity_time_base_fault, __ATOMIC_RELAXED);
}

int
parity_wait_time_base_ok(void)
{
	uint64_t counter = 0, freq = 0;

	return kern_rtc_read_counter(&counter, &freq) && freq != 0u;
}

/* Test-only time-source injection (NULL in production -> the real HAL source). */
static const struct parity_time_test_ops *g_time_test;

void
parity_wait_test_set(const struct parity_time_test_ops *ops)
{
	g_time_test = ops;
}

/* Test-only: clear the sticky time-base fault latch so a fresh case starts clean. */
void
parity_wait_test_reset_fault(void)
{
	__atomic_store_n(&parity_time_base_fault, PARITY_TB_OK, __ATOMIC_RELAXED);
}

static int
time_read(uint64_t *counter, uint64_t *freq)
{
	if (g_time_test != 0)
		return g_time_test->read(g_time_test->ctx, counter, freq);
	return kern_rtc_read_counter(counter, freq);
}

static int
time_sleep(struct wait_queue *wq, struct spinlock *lk, uint64_t seq, uint64_t deadline)
{
	if (g_time_test != 0 && g_time_test->slow_sleep_override)
		return g_time_test->slow_sleep_rc;   /* lock stays held, like waitq_sleep */
	return waitq_sleep(wq, lk, seq, deadline, 0u);
}

/*
 * Counter ticks spanning `us` microseconds at `freq` Hz, ROUNDED UP (never a
 * shorter wait than requested) and overflow-guarded.  Result is at least 1.
 */
static uint64_t
us_to_ticks(uint64_t us, uint64_t freq)
{
	uint64_t product;

	if (us == 0u || freq == 0u)
		return 1u;
	if (us > (UINT64_MAX / freq))
		return UINT64_MAX;               /* saturate rather than wrap */
	product = us * freq;
	if (product > UINT64_MAX - 999999u)
		return product / 1000000u + 1u;  /* round up without overflowing the +offset */
	product = (product + 999999u) / 1000000u;
	return product != 0u ? product : 1u;
}

/*
 * Re-read the counter, requiring the same frequency captured at the wait's
 * start.  Returns 1 with *now set on success; 0 (and latches READ_FAIL) if the
 * read fails or the frequency changed underneath us.
 */
static int
read_counter_consistent(uint64_t base_freq, uint64_t *now)
{
	uint64_t now_freq = 0;

	if (!time_read(now, &now_freq) || now_freq != base_freq) {
		parity_time_base_set_fault(PARITY_TB_READ_FAIL);
		return 0;
	}
	return 1;
}

int
parity_udelay(unsigned us)
{
	uint64_t base = 0, now = 0, freq = 0, target;

	if (!time_read(&base, &freq) || freq == 0u) {
		parity_time_base_set_fault(PARITY_TB_NO_COUNTER);
		kern_logf("i915: parity udelay: time-base fault latched (no counter)\n");
		return -EIO;   /* caller releases lock/forcewake and stops, never proceeds */
	}
	target = us_to_ticks(us, freq);
	for (;;) {
		if (!read_counter_consistent(freq, &now))
			return -EIO;
		if (now - base >= target)
			return 0;
		kern_compiler_barrier();
	}
}

int
parity_wait_reg(struct osdep_mmio *m, uint32_t reg, uint32_t mask, uint32_t value,
	unsigned fast_us, unsigned slow_ms, uint32_t *out)
{
	uint64_t base = 0, now = 0, freq = 0, target;
	uint32_t v = 0;

	if (!time_read(&base, &freq) || freq == 0u) {
		parity_time_base_set_fault(PARITY_TB_NO_COUNTER);
		if (out != 0)
			*out = osdep_mmio_raw_read32(m, reg);
		return -EIO;
	}

	/* Atomic (busy) stage: never sleeps, safe with IRQs off / a spinlock held. */
	if (fast_us != 0u) {
		target = us_to_ticks(fast_us, freq);
		for (;;) {
			v = osdep_mmio_raw_read32(m, reg);
			if ((v & mask) == value) {
				if (out != 0)
					*out = v;
				return 0;
			}
			if (!read_counter_consistent(freq, &now)) {
				if (out != 0)
					*out = v;
				return -EIO;
			}
			if (now - base >= target)
				break;
			kern_compiler_barrier();
		}
	}

	/* Slow stage: sleeps between reads on the kernel wait infrastructure. */
	if (slow_ms != 0u) {
		struct spinlock lk;
		struct wait_queue wq;

		spin_init(&lk, LOCK_RANK_DEVICE, "parity-wait-slow");
		waitq_init(&wq, "parity-wait-slow");

		/* Re-capture (counter, frequency) as one pair for this stage. */
		if (!time_read(&base, &freq) || freq == 0u) {
			parity_time_base_set_fault(PARITY_TB_NO_COUNTER);
			if (out != 0)
				*out = v;
			return -EIO;
		}
		target = us_to_ticks((uint64_t)slow_ms * 1000u, freq);
		for (;;) {
			uint64_t sleep_deadline = 0;
			int drc;

			v = osdep_mmio_raw_read32(m, reg);
			if ((v & mask) == value) {
				if (out != 0)
					*out = v;
				return 0;
			}
			if (!read_counter_consistent(freq, &now)) {
				if (out != 0)
					*out = v;
				return -EIO;
			}
			if (now - base >= target)
				break;

			/*
			 * Sleepable interval on the existing wait infrastructure.  Its
			 * granularity is one scheduler tick; a finer sleep-range is a
			 * common-time-layer dependency to add.
			 */
			drc = kern_deadline_after(sched_ticks(), 1u, &sleep_deadline);
			if (drc != 0) {
				parity_time_base_set_fault(PARITY_TB_READ_FAIL);
				if (out != 0)
					*out = v;
				return -EIO;
			}
			spin_lock(&lk);
			drc = time_sleep(&wq, &lk, waitq_sequence(&wq), sleep_deadline);
			spin_unlock(&lk);

			/*
			 * Distinguish the sleep's outcome; do NOT convert every nonzero
			 * return into an HW -ETIMEDOUT:
			 *   0 / ETIMEDOUT / EAGAIN -> our one-tick interval elapsed or a
			 *      (spurious) wake -- loop and re-check the register plus the
			 *      overall deadline (the loop's own `now - base >= target`
			 *      guard, not this per-interval expiry, decides HW timeout).
			 *   anything else -> a wait-API error; latch it and propagate -EIO
			 *      so the caller stops rather than spinning or timing out.
			 */
			if (drc != 0 && drc != ETIMEDOUT && drc != EAGAIN) {
				parity_time_base_set_fault(PARITY_TB_WAIT_API);
				if (out != 0)
					*out = v;
				return -EIO;
			}
		}
	}

	if (out != 0)
		*out = v;
	return -ETIMEDOUT;
}
