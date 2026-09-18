/*
 * WS031 Linux-parity — pure one-shot timer next-event / tick-update calculation.
 *
 * This is the platform-independent arithmetic a one-shot clockevent would use to
 * (a) serve the logical KERN_CLOCK_HZ tick and (b) also honour the earliest
 * pending sleep deadline, on a SINGLE timer.  It touches no hardware and takes
 * the monotonic time as input, so it can be verified with a fake clock before
 * any LAPIC / HAL boundary change.  All times are absolute monotonic-counter
 * units; the logical tick count keeps its existing unit/meaning.
 */
#ifndef PARITY_TIMER_CALC_H
#define PARITY_TIMER_CALC_H

#include <stdint.h>

struct parity_timer_calc {
	uint64_t period;         /* counter units per logical tick (freq / KERN_CLOCK_HZ) */
	uint64_t next_tick;      /* absolute deadline of the next logical tick */
	uint64_t sleep_deadline; /* earliest pending sleep deadline (absolute) */
	int      has_sleep;      /* 1 if a sleep deadline is pending */
	uint64_t ticks;          /* logical tick count delivered so far */
};

/* Initialise: first tick one period after `now`, no sleep pending. */
void parity_timer_calc_init(struct parity_timer_calc *c, uint64_t now, uint64_t period);

/* Register the earliest pending sleep deadline (keeps the minimum). */
void parity_timer_set_sleep(struct parity_timer_calc *c, uint64_t deadline);

/* Cancel the pending sleep deadline. */
void parity_timer_clear_sleep(struct parity_timer_calc *c);

/*
 * The next event to program the one-shot for: min(next_tick, sleep_deadline).
 * A sleep-only event does NOT move next_tick.
 */
uint64_t parity_timer_next_event(const struct parity_timer_calc *c);

/*
 * Handle a timer fire at absolute time `now`.  Returns the number of WHOLE
 * logical ticks that elapsed (0 for a sleep-only wake); advances next_tick past
 * `now` so a past/late deadline is delivered, not waited on.  Never counts a
 * single sub-tick fire as a full tick.
 */
unsigned parity_timer_on_fire(struct parity_timer_calc *c, uint64_t now);

#endif /* PARITY_TIMER_CALC_H */
