/*
 * WS031 Linux-parity — pure one-shot timer next-event / tick-update calculation
 * (see timer_calc.h).  Hardware-independent; verified with a fake clock.
 */
#include "../internal.h"
#include "timer_calc.h"

void
parity_timer_calc_init(struct parity_timer_calc *c, uint64_t now, uint64_t period)
{
	c->period = period;
	c->next_tick = now + period;   /* first logical tick one period from now */
	c->sleep_deadline = 0u;
	c->has_sleep = 0;
	c->ticks = 0u;
}

void
parity_timer_set_sleep(struct parity_timer_calc *c, uint64_t deadline)
{
	if (!c->has_sleep || deadline < c->sleep_deadline) {
		c->sleep_deadline = deadline;
		c->has_sleep = 1;
	}
}

void
parity_timer_clear_sleep(struct parity_timer_calc *c)
{
	c->has_sleep = 0;
	c->sleep_deadline = 0u;
}

uint64_t
parity_timer_next_event(const struct parity_timer_calc *c)
{
	if (c->has_sleep && c->sleep_deadline < c->next_tick)
		return c->sleep_deadline;   /* an earlier sleep wins; tick unmoved */
	return c->next_tick;
}

unsigned
parity_timer_on_fire(struct parity_timer_calc *c, uint64_t now)
{
	unsigned delivered = 0u;

	/*
	 * Deliver every WHOLE tick whose deadline is at/behind `now`.  A sleep-only
	 * fire (now < next_tick) delivers 0 ticks and leaves next_tick alone.  A
	 * late fire that skipped several boundaries delivers them all and moves
	 * next_tick past `now`, so we never wait on a deadline already in the past.
	 */
	if (c->period != 0u) {
		while (now >= c->next_tick) {
			c->ticks++;
			delivered++;
			c->next_tick += c->period;
		}
	}
	return delivered;
}
