/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The RPS contract (ws075-p020), checked on the host.
 *
 * Runs gt-power.c's RPS against the mock register file: the frequency caps,
 * the enable at the minimum with the LOW_POWER thresholds, the start that
 * enables and unmasks the up and down events and raises the request to the
 * efficient frequency, the interrupt that masks its events and queues the
 * work, the work that steps up (doubling) and down, switches the power mode
 * and keeps PMINTRMSK and RP_INTERRUPT_LIMITS with the frequency, the boost
 * of a waiting client, the idle check that lowers an idle GT, and the stop.
 * The work queue's and the timer's threads never run on the host, so the
 * checks run the callbacks themselves.  It proves the contract, not the
 * hardware.
 */

#include "contract.h"
#include "mock_mmio.h"

#include "../../gt-power.h"
#include "../../irq.h"
#include "../../mmio.h"
#include "../../trace.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

/* The registers the checks read back. */
#define RPS_RPNSWREQ		0xa008U
#define RPS_INTERRUPT_LIMITS	0xa014U
#define RPS_CONTROL		0xa024U
#define RPS_UP_THRESHOLD	0xa02cU
#define RPS_DOWN_THRESHOLD	0xa030U
#define RPS_UP_EI		0xa068U
#define RPS_DOWN_EI		0xa06cU
#define RPS_IDLE_HYSTERSIS	0xa070U
#define RPS_PMINTRMSK		0xa168U
#define RPS_GPM_ENABLE		0x19003cU
#define RPS_GPM_MASK		0x1900ecU
#define RPS_STATE_CAP		(0x140000U + 0x5998U)
#define RPS_FREQ_INFO		(0x140000U + 0x5ef0U)

/* The command streamer clock of the checks, 19.2 MHz as on Alder Lake-P. */
#define RPS_CLOCK		19200000U

/* The RP control value every power mode writes. */
#define RPS_CONTROL_VALUE	0x592U

/* The RPS state under test, its register access and the interrupt device it registers with. */
static struct i915_rps rps;

/* The register access the RPS state writes through. */
static struct i915_mmio mmio;

/* The register file behind the access. */
static struct mock_mmio mock;

/* The interrupt device the start registers the handler with. */
static struct i915_irq_dev irq;

/* How many times the start and the stop cleared the pending GTPM source. */
static int reset_iir_calls;

static void rps_check_caps_and_enable(void);
static void rps_check_start(void);
static void rps_check_up(void);
static void rps_check_down(void);
static void rps_check_boost(void);
static void rps_check_idle(void);
static void rps_check_stop(void);
static void rps_check_interval(void);
static void rps_run_work(void);
static void rps_event(uint32_t events);

/*
 * Runs the RPS contract checks.
 */
int
main(void)
{
	static struct i915_trace trace;
	const struct i915_mmio_range *ranges;
	unsigned range_count;
	int status;

	contract_begin("RPS contract tests (mock, GPU-free, ws075-p020)");

	/* Binds the register access to an empty mock register file. */
	drv_i915_trace_init(&trace);
	mock_mmio_reset(&mock);
	ranges = mock_mmio_ranges(&range_count);
	drv_i915_mmio_init(&mmio, mock_mmio_ops(), &mock, ranges, range_count, &trace);
	memset(&irq, 0, sizeof(irq));
	irq.m = &mmio;

	/* The checks, in the order the device uses RPS. */
	rps_check_caps_and_enable();
	rps_check_start();
	rps_check_up();
	rps_check_down();
	rps_check_boost();
	rps_check_idle();
	rps_check_stop();
	rps_check_interval();

	/* Reports whether every check held. */
	status = contract_end();
	if (status != 0)
		return status;

	/* Succeeded: RPS keeps its contract. */
	return 0;
}

/*
 * Stands in for the PCODE mailbox: it does not answer, so the efficient
 * frequency stays RP1.
 */
int
drv_i915_pcode_read(
	struct mutex *sb_lock,
	struct i915_mmio *access,
	uint32_t mbox,
	uint32_t *val,
	uint32_t *val1)
{
	(void)sb_lock;
	(void)access;
	(void)mbox;
	(void)val;
	(void)val1;

	/* The mailbox does not answer. */
	return EIO;
}

/* Stands in for the clearing of the pending GTPM source: counts it. */
void
drv_i915_gt_pm_reset_iir(
	struct i915_irq_dev *device)
{
	(void)device;

	/* One more clearing. */
	reset_iir_calls++;
}

/* Stands in for waiting for a running interrupt handler: none runs on the host. */
int
drv_i915_synchronize_irq(
	struct i915_irq_dev *device)
{
	(void)device;

	/* No handler was running. */
	return 0;
}

/* Checks the caps, and the enable at the minimum with the LOW_POWER thresholds. */
static void
rps_check_caps_and_enable(void)
{
	contract_section("init and enable: caps, minimum request, LOW_POWER thresholds");

	/* RP0 24 and the minimum 2 in 50 MHz units, RP1 6. */
	mock_mmio_preset(&mock, RPS_STATE_CAP, 24U | (2U << 16));
	mock_mmio_preset(&mock, RPS_FREQ_INFO, 6U << 8);
	memset(&rps, 0, sizeof(rps));
	drv_i915_rps_init(&rps, NULL, &mmio);
	contract_check(rps.rp0_freq == 72U && rps.min_freq == 6U && rps.rp1_freq == 18U, "caps scaled to 16.67 MHz units");
	contract_check(rps.efficient_freq == 18U && rps.pcode_ok == 0, "without the PCODE the efficient frequency is RP1");
	contract_check(rps.boost_freq == 72U && rps.min_softlimit == 6U && rps.max_softlimit == 72U, "soft limits are the range, the boost is RP0");

	/* The enable. */
	drv_i915_rps_enable(&rps, &mmio, RPS_CLOCK);
	contract_check(rps.enabled == 1 && rps.cur_freq == 6U && rps.power_mode == I915_RPS_POWER_LOW, "enabled at the minimum in LOW_POWER");
	contract_check(mock_mmio_peek(&mock, RPS_IDLE_HYSTERSIS) == 0xaU, "RP_IDLE_HYSTERSIS is 10");
	contract_check(mock_mmio_peek(&mock, RPS_RPNSWREQ) == 6U << 23, "RPNSWREQ asks for the minimum");
	contract_check(mock_mmio_peek(&mock, RPS_UP_EI) == 19200U, "UP_EI is 16 ms in 16-clock units");
	contract_check(mock_mmio_peek(&mock, RPS_UP_THRESHOLD) == 18240U, "UP_THRESHOLD is 95 % of it");
	contract_check(mock_mmio_peek(&mock, RPS_DOWN_EI) == 38400U, "DOWN_EI is 32 ms");
	contract_check(mock_mmio_peek(&mock, RPS_DOWN_THRESHOLD) == 32640U, "DOWN_THRESHOLD is 85 % of it");
	contract_check(mock_mmio_peek(&mock, RPS_CONTROL) == RPS_CONTROL_VALUE, "RP_CONTROL enables RP with busy and idle averaging");
	contract_check(rps.pm_events == (I915_RPS_UP_THRESHOLD | I915_RPS_DOWN_THRESHOLD), "the events are the up and down thresholds");
}

/* Checks the start: the handler, the enabled and unmasked events, the efficient frequency. */
static void
rps_check_start(void)
{
	int error;

	contract_section("start: handler, events enabled and unmasked, efficient frequency");

	/* The start. */
	error = drv_i915_rps_start(&rps, &irq, &mmio);
	contract_check(error == 0 && rps.work_ready == 1 && rps.timers_ready == 1, "the work queue and the idle timer exist");
	contract_check(rps.idle_work.armed == 1, "above the minimum the idle check is armed");
	contract_check(irq.pm_handler == drv_i915_rps_irq && irq.pm_context == &rps, "the handler is registered with its state");
	contract_check(reset_iir_calls == 1, "the pending GTPM source was cleared first");
	contract_check(mock_mmio_peek(&mock, RPS_GPM_ENABLE) == 0x30U << 16, "the up and down events are enabled in the upper half");
	contract_check(mock_mmio_peek(&mock, RPS_GPM_MASK) == ~0x30U << 16, "and unmasked");

	/* The request rose to the efficient frequency, with the mask and limits for it. */
	contract_check(rps.cur_freq == 18U && mock_mmio_peek(&mock, RPS_RPNSWREQ) == 18U << 23, "the request is the efficient frequency");
	contract_check(mock_mmio_peek(&mock, RPS_PMINTRMSK) == ~0x30U, "PMINTRMSK lets up and down through between the limits");
	contract_check(mock_mmio_peek(&mock, RPS_INTERRUPT_LIMITS) == 72U << 23, "RP_INTERRUPT_LIMITS holds the maximum only");
}

/* Checks the up events: masked by the handler, steps that double, the power modes, the top. */
static void
rps_check_up(void)
{
	uint32_t before;
	unsigned rounds;

	contract_section("up: the handler masks, the work steps up doubling, the modes follow");

	/* An event RPS does not follow queues nothing. */
	rps_event(I915_RPS_UP_EI_EXPIRED);
	contract_check(rps.pm_iir == 0U && rps.work.queued == 0, "an unfollowed event queues nothing");

	/* An up event is masked and queues the work. */
	rps_event(I915_RPS_UP_THRESHOLD);
	contract_check(rps.pm_iir == I915_RPS_UP_THRESHOLD && rps.work.queued == 1, "an up event is kept and the work queued");
	contract_check(mock_mmio_peek(&mock, RPS_GPM_MASK) == ~0x10U << 16, "the up event is masked until the work took it");

	/* The work goes up by one and unmasks. */
	rps_run_work();
	contract_check(rps.cur_freq == 19U && rps.last_adj == 1, "the first up step is one");
	contract_check(mock_mmio_peek(&mock, RPS_GPM_MASK) == ~0x30U << 16, "the work unmasked the events");
	contract_check(rps.power_mode == I915_RPS_POWER_LOW, "just above the efficient frequency the mode stays LOW_POWER");

	/* The next goes up by two and enters BETWEEN, with its shorter up interval. */
	rps_event(I915_RPS_UP_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 21U && rps.last_adj == 2, "the next up step doubles");
	contract_check(rps.power_mode == I915_RPS_POWER_BETWEEN && mock_mmio_peek(&mock, RPS_UP_EI) == 15600U, "past the efficient frequency the mode is BETWEEN (13 ms up)");

	/* Up events until the top. */
	for (rounds = 0U; rounds < 10U; rounds++) {
		rps_event(I915_RPS_UP_THRESHOLD);
		rps_run_work();
	}

	/* Where they ended. */
	contract_check(rps.cur_freq == 72U && mock_mmio_peek(&mock, RPS_RPNSWREQ) == 72U << 23, "up events reach RP0");
	contract_check(rps.power_mode == I915_RPS_POWER_HIGH && mock_mmio_peek(&mock, RPS_UP_EI) == 12000U, "at RP0 the mode is HIGH_POWER (10 ms up)");
	contract_check(mock_mmio_peek(&mock, RPS_PMINTRMSK) == ~0x10U, "at the top only the down event is let through");

	/* Another up event at the top changes nothing. */
	before = rps.changes;
	rps_event(I915_RPS_UP_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 72U && rps.changes == before && rps.last_adj == 0, "an up event at the top writes nothing");
}

/* Checks the down events: steps that double, down to the minimum, and the limits there. */
static void
rps_check_down(void)
{
	unsigned rounds;

	contract_section("down: the work steps down doubling to the minimum");

	/* The first down step after the top is one. */
	rps_event(I915_RPS_DOWN_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 71U && rps.last_adj == -1, "the first down step is one");

	/* The next doubles. */
	rps_event(I915_RPS_DOWN_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 69U && rps.last_adj == -2, "the next down step doubles");

	/* Down events until the minimum. */
	for (rounds = 0U; rounds < 10U; rounds++) {
		rps_event(I915_RPS_DOWN_THRESHOLD);
		rps_run_work();
	}

	/* Where they ended. */
	contract_check(rps.cur_freq == 6U && mock_mmio_peek(&mock, RPS_RPNSWREQ) == 6U << 23, "down events reach the minimum");
	contract_check(rps.power_mode == I915_RPS_POWER_LOW && mock_mmio_peek(&mock, RPS_UP_EI) == 19200U, "at the minimum the mode is LOW_POWER");
	contract_check(mock_mmio_peek(&mock, RPS_PMINTRMSK) == ~0x20U, "at the minimum only the up event is let through");
	contract_check(mock_mmio_peek(&mock, RPS_INTERRUPT_LIMITS) == ((72U << 23) | (6U << 14)), "RP_INTERRUPT_LIMITS holds the minimum there too");
	contract_check(rps.up_events == 13U && rps.down_events == 12U, "the handler counted the events");
}

/* Checks the boost: a waiting client jumps to RP0 and holds it until done. */
static void
rps_check_boost(void)
{
	contract_section("boost: a client waiting on unstarted work jumps to RP0");

	/* The first waiter queues the work, which jumps to RP0. */
	drv_i915_rps_boost_begin(&rps);
	contract_check(rps.waiters == 1U && rps.boosts == 1U && rps.work.queued == 1, "the first waiter queues the work");
	rps_run_work();
	contract_check(rps.cur_freq == 72U && mock_mmio_peek(&mock, RPS_RPNSWREQ) == 72U << 23, "the work jumps to RP0");

	/* A second waiter adds nothing; a down event while waiting keeps the frequency. */
	drv_i915_rps_boost_begin(&rps);
	contract_check(rps.waiters == 2U && rps.boosts == 1U, "a second waiter does not boost again");
	rps_event(I915_RPS_DOWN_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 72U && rps.last_adj == 0, "a down event while a client waits keeps the frequency");

	/* Once the waiters are done a down event lowers it again. */
	drv_i915_rps_boost_end(&rps);
	drv_i915_rps_boost_end(&rps);
	drv_i915_rps_boost_end(&rps);
	contract_check(rps.waiters == 0U, "the waiters are done (an extra end changes nothing)");
	rps_event(I915_RPS_DOWN_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 71U, "then a down event lowers it");
}

/* Checks the idle check: no activity lowers to the efficient frequency, then the minimum; activity or a waiter keeps it. */
static void
rps_check_idle(void)
{
	contract_section("idle: an idle GT drops to the efficient frequency, then to the minimum");

	/* Up to RP0 with a boost, which is activity: the first check keeps the frequency. */
	drv_i915_rps_boost_begin(&rps);
	rps_run_work();
	drv_i915_rps_boost_end(&rps);
	contract_check(rps.cur_freq == 72U, "a boost raised it to RP0");
	rps.idle_work.work.function(rps.idle_work.work.context);
	contract_check(rps.cur_freq == 72U && rps.idle_drops == 0U, "a check after activity keeps the frequency");

	/* A waiting client keeps it too. */
	drv_i915_rps_boost_begin(&rps);
	rps.idle_seen = 0UL;
	rps.idle_work.work.function(rps.idle_work.work.context);
	rps.idle_work.work.function(rps.idle_work.work.context);
	contract_check(rps.cur_freq == 72U, "a check while a client waits keeps the frequency");
	drv_i915_rps_boost_end(&rps);

	/* Nothing happened since: the efficient frequency, then the minimum, then the check stops. */
	rps.idle_work.work.function(rps.idle_work.work.context);
	contract_check(rps.cur_freq == 18U && rps.idle_drops == 1U, "an idle GT drops to the efficient frequency");
	contract_check(mock_mmio_peek(&mock, RPS_RPNSWREQ) == 18U << 23, "RPNSWREQ follows");
	rps.idle_work.work.function(rps.idle_work.work.context);
	contract_check(rps.cur_freq == 6U && rps.idle_drops == 2U, "then to the minimum");

	/* Activity (an engine interrupt) before the next event keeps the minimum anyway. */
	irq.engine_wakeups++;
	rps_event(I915_RPS_UP_THRESHOLD);
	rps_run_work();
	contract_check(rps.cur_freq == 7U, "an up event from the minimum steps up again");
}

/* Checks the stop: the handler goes, the events are disabled and masked, the queue goes. */
static void
rps_check_stop(void)
{
	contract_section("stop: handler removed, events disabled and masked");

	/*
	 * The work queue's and the timer's threads never ran on the host, so
	 * they are marked as gone for the destroys, which wait for them.
	 */
	rps.workqueue.worker_alive = 0;
	rps.timers.alive = 0;
	drv_i915_rps_stop(&rps);
	contract_check(irq.pm_handler == NULL && irq.pm_context == NULL && rps.irq == NULL, "the handler is removed");
	contract_check(rps.work_ready == 0 && rps.timers_ready == 0 && reset_iir_calls == 2, "the queue and the timer are gone and the source cleared");
	contract_check(mock_mmio_peek(&mock, RPS_GPM_ENABLE) == 0U, "the events are disabled");
	contract_check(mock_mmio_peek(&mock, RPS_GPM_MASK) == 0xffff0000U, "and masked");
	contract_check(mock_mmio_peek(&mock, RPS_PMINTRMSK) == 0xffffffffU, "PMINTRMSK masks everything");
	contract_check(mock_mmio_peek(&mock, RPS_RPNSWREQ) == 7U << 23, "the last request stays");
}

/* Checks the conversion to the PM evaluation units, which rounds up twice. */
static void
rps_check_interval(void)
{
	contract_section("interval: nanoseconds to sixteen-clock units");

	/* One microsecond at 19.2 MHz is 19.2 clocks: 20, then 2 units. */
	contract_check(drv_i915_rps_pm_interval(RPS_CLOCK, 1000U) == 2U, "rounds up the clocks and the units");
	contract_check(drv_i915_rps_pm_interval(RPS_CLOCK, 0U) == 0U, "no time is no units");
	contract_check(drv_i915_rps_pm_interval(RPS_CLOCK, 32000000U) == 38400U, "32 ms is 38400 units");
}

/* Runs the queued work's callback, as the work queue's thread would. */
static void
rps_run_work(void)
{
	/* The callback, and the work leaves the queue. */
	rps.work.queued = 0;
	rps.work.state = I915_WORK_IDLE;
	rps.workqueue.count = 0U;
	rps.workqueue.head = 0U;
	rps.workqueue.tail = 0U;
	rps.work.function(rps.work.context);
}

/* Delivers GT power management events to the handler, as the interrupt would. */
static void
rps_event(
	uint32_t events)
{
	/* The handler the start registered. */
	irq.pm_handler(irq.pm_context, events);
}
