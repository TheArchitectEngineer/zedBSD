/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws197-p007a1: the xHCI isochronous bookkeeping on the host
 * (src/drivers/pci/pci-xhci-isoch.c, design 2.1a of
 * plan/ws197/phase007/phase.md): the ring's room, the FIFO, BEI, the skip
 * rule, the skip wait, Stopped from the worker's Stop at three points,
 * a Stop not the bookkeeping's, the endpoint-wide cancel (a Stopped of an
 * earlier URB meanwhile, EALREADY, EBUSY), a halt and its recovery, the
 * consumer after a flush, the ring's wrap and stray events.  Prints a
 * line for each failed check and "xhci-isoch: all PASS" at the end.
 */

#include <drivers/pci/pci-xhci-isoch.h>

#include <uapi/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A ring of 256 TRBs less its link. */
#define TEST_SLOTS	255U

static unsigned failures;

static void check(int good, const char *what);
static int queue(struct drv_xhci_isoch *isoch, uintptr_t tag, unsigned count, unsigned *first);
static void test_room(void);
static void test_bei(void);
static void test_order(void);
static void test_skip(void);
static void test_skip_wait(void);
static void test_stop(void);
static void test_foreign(void);
static void test_cancel(void);
static void test_halt(void);
static void test_wrap(void);
static void test_retain(void);

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	/* A failure is printed and counted. */
	if (!good) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Queues a request of count packets of 48 bytes. */
static int
queue(
	struct drv_xhci_isoch *isoch,
	uintptr_t tag,
	unsigned count,
	unsigned *first)
{
	uint32_t lengths[DRV_XHCI_ISOCH_PACKETS_MAX];
	unsigned index;
	int error;

	/* The lengths. */
	for (index = 0U; index < DRV_XHCI_ISOCH_PACKETS_MAX; index++)
		lengths[index] = 48U;

	/* The request. */
	error = drv_xhci_isoch_enqueue(isoch, tag, count, lengths, first);
	return error;
}

/* The ring's room, the FIFO's limit and the count's range. */
static void
test_room(
	void)
{
	struct drv_xhci_isoch isoch;
	unsigned first;
	int error;

	/* 254 TDs fit a ring of 255 slots (one kept free). */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	check(drv_xhci_isoch_room(&isoch) == 254U, "room of an empty ring");

	/* Four requests of 32, then the FIFO is full. */
	error = queue(&isoch, 1U, 32U, &first);
	check(error == 0 && first == 0U, "first request");
	error = queue(&isoch, 2U, 32U, &first);
	check(error == 0 && first == 32U, "second request");
	(void)queue(&isoch, 3U, 32U, &first);
	(void)queue(&isoch, 4U, 32U, &first);
	error = queue(&isoch, 5U, 1U, &first);
	check(error == EAGAIN, "a fifth request is EAGAIN");
	check(drv_xhci_isoch_room(&isoch) == 126U, "room after 128 TDs");

	/* A small ring: 2 requests of 8 in 20 slots, a third does not fit. */
	drv_xhci_isoch_init(&isoch, 20U);
	(void)queue(&isoch, 1U, 8U, &first);
	(void)queue(&isoch, 2U, 8U, &first);
	error = queue(&isoch, 3U, 8U, &first);
	check(error == EAGAIN, "a full ring is EAGAIN");
	error = queue(&isoch, 3U, 3U, &first);
	check(error == 0, "the last 3 slots fit");

	/* Counts out of range. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	error = queue(&isoch, 1U, 0U, &first);
	check(error == EINVAL, "a request of no TD");
	error = queue(&isoch, 1U, DRV_XHCI_ISOCH_PACKETS_MAX + 1U, &first);
	check(error == EINVAL, "a request of too many TDs");
}

/* BEI on all TDs but each eighth and the last. */
static void
test_bei(
	void)
{
	unsigned index;
	unsigned interrupts;
	int blocked;
	int last;

	/* 8 TDs: only the last interrupts. */
	interrupts = 0U;
	for (index = 0U; index < 8U; index++) {
		blocked = drv_xhci_isoch_bei(index, 8U);
		if (!blocked)
			interrupts++;
	}

	/* The count, and the last. */
	last = drv_xhci_isoch_bei(7U, 8U);
	check(interrupts == 1U && !last, "BEI of 8 TDs");

	/* 32 TDs: the 8th, 16th, 24th and 32nd. */
	interrupts = 0U;
	for (index = 0U; index < 32U; index++) {
		blocked = drv_xhci_isoch_bei(index, 32U);
		if (!blocked)
			interrupts++;
	}

	/* The count, the 16th and the 17th. */
	last = drv_xhci_isoch_bei(15U, 32U);
	blocked = drv_xhci_isoch_bei(16U, 32U);
	check(interrupts == 4U && !last && blocked, "BEI of 32 TDs");

	/* 3 TDs: the last; 1 TD: itself. */
	check(drv_xhci_isoch_bei(0U, 3U) && drv_xhci_isoch_bei(1U, 3U) && !drv_xhci_isoch_bei(2U, 3U), "BEI of 3 TDs");
	check(!drv_xhci_isoch_bei(0U, 1U), "BEI of 1 TD");
}

/* Events in order: results, short, error with a halt check, completion and the consumer. */
static void
test_order(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;
	unsigned index;
	int got;

	/* One request of 4. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 7U, 4U, &first);

	/* Success, short by 8, babble (check the halt), success. */
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 0U, 0U);
	check(flags == 0U && isoch.consumer == 1U && isoch.outstanding == 3U, "first TD retired");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SHORT_PACKET, 1, 1U, 8U);
	check(flags == 0U, "short TD");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_BABBLE, 1, 2U, 0U);
	check(flags == DRV_XHCI_ISOCH_CHECK_HALT, "babble asks for the halt check");
	got = drv_xhci_isoch_take(&isoch, &taken);
	check(!got, "an unfinished request is not taken");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 3U, 0U);
	check(flags == DRV_XHCI_ISOCH_COMPLETE, "the last TD completes the request");

	/* Taken with its results. */
	got = drv_xhci_isoch_take(&isoch, &taken);
	check(got && taken.tag == 7U && taken.done == 4U, "request taken");
	check(taken.result[0] == DRV_XHCI_ISO_OK && taken.actual[0] == 48U, "OK result");
	check(taken.result[1] == DRV_XHCI_ISO_SHORT && taken.actual[1] == 40U, "short result");
	check(taken.result[2] == DRV_XHCI_ISO_ERROR && taken.actual[2] == 0U, "error result");
	check(isoch.queued == 0U && isoch.outstanding == 0U && isoch.consumer == 4U, "empty after the take");

	/* A full request of 8, each event in turn. */
	(void)queue(&isoch, 8U, 8U, &first);
	flags = 0U;
	for (index = 0U; index < 8U; index++)
		flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, first + index, 0U);
	check(flags == DRV_XHCI_ISOCH_COMPLETE && drv_xhci_isoch_room(&isoch) == 254U, "eight in turn");
}

/* The skip rule within a request and across requests. */
static void
test_skip(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned second;
	unsigned flags;
	unsigned index;
	int good;

	/* Two requests of 8; an event for the second's TD 2. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 8U, &first);
	(void)queue(&isoch, 2U, 8U, &second);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, second + 2U, 0U);
	check(flags == DRV_XHCI_ISOCH_COMPLETE, "an event in the second request completes the first");
	check(isoch.consumer == second + 3U && isoch.outstanding == 5U, "consumer after the skip");

	/* The first: all missed. */
	(void)drv_xhci_isoch_take(&isoch, &taken);
	good = 1;
	for (index = 0U; index < 8U; index++) {
		if (taken.result[index] != DRV_XHCI_ISO_MISSED)
			good = 0;
	}

	/* All of them. */
	check(good && taken.tag == 1U, "the first request's TDs missed");

	/* The second: 0 and 1 missed, 2 OK, the rest outstanding. */
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, second + 7U, 0U);
	(void)drv_xhci_isoch_take(&isoch, &taken);
	check(taken.result[0] == DRV_XHCI_ISO_MISSED && taken.result[1] == DRV_XHCI_ISO_MISSED, "earlier TDs missed");
	check(taken.result[2] == DRV_XHCI_ISO_OK && taken.result[6] == DRV_XHCI_ISO_MISSED && taken.result[7] == DRV_XHCI_ISO_OK, "skip within");
}

/* Missed Service, Underrun and Overrun: the skip wait, and nothing with no TD out. */
static void
test_skip_wait(
	void)
{
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;
	int error;

	/* An Underrun with nothing queued: counted only. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_RING_UNDERRUN, 0, 0U, 0U);
	check(flags == 0U && isoch.state == DRV_XHCI_ISOCH_NORMAL && isoch.underruns == 1U, "underrun of an empty ring");

	/* A Missed Service with TDs out: the skip wait, and the worker woken. */
	(void)queue(&isoch, 1U, 8U, &first);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_MISSED_SERVICE, 0, 0U, 0U);
	check(flags == DRV_XHCI_ISOCH_WAKE && isoch.state == DRV_XHCI_ISOCH_SKIP_WAIT, "missed service waits");
	check(isoch.outstanding == 8U, "missed service ends nothing");

	/* Enqueue still allowed; the next TD's event ends the wait with the skip rule. */
	error = queue(&isoch, 2U, 8U, &first);
	check(error == 0, "enqueue in the skip wait");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 4U, 0U);
	check(isoch.state == DRV_XHCI_ISOCH_NORMAL && isoch.consumer == 5U, "the next event ends the wait");

	/* An Overrun in the wait does not start another. */
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_RING_OVERRUN, 0, 0U, 0U);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_RING_OVERRUN, 0, 0U, 0U);
	check(flags == DRV_XHCI_ISOCH_WAKE && isoch.overruns == 2U, "overrun counted");
}

/*
 * The worker's Stop after a skip wait at three points of TD 4: its Stopped
 * (26) event, its Stopped - Length Invalid (27) event, and its Success
 * before the Stop.
 */
static void
test_stop(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;
	unsigned code;
	unsigned round;
	int error;

	/* Each of the two Stopped codes. */
	for (round = 0U; round < 2U; round++) {
		/* 8 TDs, a missed service, the worker owns the endpoint. */
		drv_xhci_isoch_init(&isoch, TEST_SLOTS);
		(void)queue(&isoch, 1U, 8U, &first);
		(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_MISSED_SERVICE, 0, 0U, 0U);
		error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_STOPPING);
		check(error == 0, "the worker stops");

		/* The Stop's event for TD 4: counted, nothing ended. */
		code = DRV_XHCI_COMPLETION_STOPPED;
		if (round == 1U)
			code = DRV_XHCI_COMPLETION_STOPPED_LENGTH;
		flags = drv_xhci_isoch_event(&isoch, code, 1, 4U, 0U);
		check(flags == 0U && isoch.outstanding == 8U && isoch.foreign_stops == 0U, "the Stop's own Stopped ends nothing");

		/* Enqueue waits for the owner. */
		error = queue(&isoch, 2U, 4U, &first);
		check(error == EBUSY, "enqueue while stopping");

		/* TR Dequeue at TD 4: 0 to 3 missed, 4 to 7 stay; the endpoint is rung. */
		flags = drv_xhci_isoch_stop_done(&isoch, 4U);
		check(flags == DRV_XHCI_ISOCH_RING && isoch.state == DRV_XHCI_ISOCH_NORMAL, "rung after the stop");
		check(isoch.consumer == 4U && isoch.outstanding == 4U, "TD 4 and after stay");

		/* TD 4 runs again and ends; then the rest. */
		(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 4U, 0U);
		flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 7U, 0U);
		(void)drv_xhci_isoch_take(&isoch, &taken);
		check(flags == DRV_XHCI_ISOCH_COMPLETE && taken.result[3] == DRV_XHCI_ISO_MISSED && taken.result[4] == DRV_XHCI_ISO_OK,
		    "TD 4 ran again");
	}

	/* TD 4's Success came before the Stop: kept, TR Dequeue at TD 5. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 8U, &first);
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_MISSED_SERVICE, 0, 0U, 0U);
	(void)drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_STOPPING);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 4U, 0U);
	check(flags == 0U && isoch.outstanding == 8U, "a TD event while stopping is only recorded");
	flags = drv_xhci_isoch_stop_done(&isoch, 5U);
	check(isoch.consumer == 5U && isoch.outstanding == 3U && flags == DRV_XHCI_ISOCH_RING, "TD 4's result kept");
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 7U, 0U);
	(void)drv_xhci_isoch_take(&isoch, &taken);
	check(taken.result[2] == DRV_XHCI_ISO_MISSED && taken.result[4] == DRV_XHCI_ISO_OK && taken.result[5] == DRV_XHCI_ISO_MISSED,
	    "results around the stop");

	/* A Stop with every TD done: TR Dequeue at the producer, nothing to ring. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 4U, &first);
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_RING_UNDERRUN, 0, 0U, 0U);
	(void)drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_STOPPING);
	flags = drv_xhci_isoch_stop_done(&isoch, 4U);
	check(flags == DRV_XHCI_ISOCH_COMPLETE && isoch.outstanding == 0U, "stop at the producer");

	/* The worker cannot stop an endpoint not waiting. */
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_STOPPING);
	check(error == EBUSY, "no stop without a skip wait");
}

/* A Stopped event while nothing owns the endpoint (another Stop): counted, nothing ended. */
static void
test_foreign(
	void)
{
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;

	/* One request out. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 8U, &first);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_STOPPED, 1, 2U, 0U);
	check(flags == 0U && isoch.foreign_stops == 1U && isoch.outstanding == 8U, "foreign Stopped counted");

	/* An event with no pointer, and one for a slot with no TD. */
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 0, 0U, 0U);
	check(flags == 0U && isoch.strays == 1U, "no pointer");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 100U, 0U);
	check(flags == 0U && isoch.strays == 2U, "a slot with no TD");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 1000U, 0U);
	check(flags == 0U && isoch.strays == 3U, "a slot off the ring");
}

/* The endpoint-wide cancel: W's Stopped meanwhile, EALREADY, EBUSY, results kept, the ring emptied. */
static void
test_cancel(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;
	unsigned count;
	int error;
	int got;

	/* Three requests (W, X, Y); W's first two TDs done. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 4U, &first);
	(void)queue(&isoch, 2U, 4U, &first);
	(void)queue(&isoch, 3U, 4U, &first);
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 1U, 0U);

	/* X's cancel owns the endpoint; another cancel (of Y) is EALREADY; enqueue is EBUSY. */
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	check(error == 0, "cancel begins");
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	check(error == EALREADY, "a second cancel is EALREADY");
	error = queue(&isoch, 4U, 1U, &first);
	check(error == EBUSY, "enqueue while cancelling");

	/* The Stop's Stopped for W's TD 2: recorded nothing, ended nothing. */
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_STOPPED, 1, 2U, 0U);
	check(flags == 0U && isoch.outstanding == 10U, "W's Stopped ends nothing");

	/* After dequeue_all: every TD without a result cancelled, the ring empty. */
	flags = drv_xhci_isoch_flush(&isoch, DRV_XHCI_ISO_CANCELLED);
	check(flags == DRV_XHCI_ISOCH_COMPLETE && isoch.consumer == isoch.producer && isoch.outstanding == 0U, "flushed");
	check(drv_xhci_isoch_room(&isoch) == 254U && isoch.state == DRV_XHCI_ISOCH_NORMAL, "the room after the flush");

	/* W, X and Y in order, W's done TDs kept. */
	got = drv_xhci_isoch_take(&isoch, &taken);
	check(got && taken.tag == 1U && taken.result[1] == DRV_XHCI_ISO_OK && taken.result[0] == DRV_XHCI_ISO_MISSED &&
	    taken.result[2] == DRV_XHCI_ISO_CANCELLED, "W's results");
	count = 1U;
	for (;;) {
		got = drv_xhci_isoch_take(&isoch, &taken);
		if (!got)
			break;
		count++;
	}

	/* X and Y. */
	check(count == 3U && taken.tag == 3U && taken.result[3] == DRV_XHCI_ISO_CANCELLED, "X and Y cancelled");

	/* A cancel waits for the worker's Stop. */
	(void)queue(&isoch, 5U, 4U, &first);
	check(first == 12U, "the ring goes on after the flush");
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_MISSED_SERVICE, 0, 0U, 0U);
	(void)drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_STOPPING);
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	check(error == EBUSY, "cancel while the worker stops is EBUSY");

	/* A cancel takes over a skip wait. */
	(void)drv_xhci_isoch_stop_done(&isoch, 12U);
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_RING_UNDERRUN, 0, 0U, 0U);
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	check(error == 0, "cancel takes over a skip wait");
}

/* A halt: the context read, HALT_PENDING, the recovery, the rest ERROR. */
static void
test_halt(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;
	int error;

	/* A transaction error on TD 1 of 4. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 4U, &first);
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_TRANSACTION, 1, 1U, 0U);
	check(flags == DRV_XHCI_ISOCH_CHECK_HALT, "transaction error asks for the halt check");

	/* The context said Halted. */
	drv_xhci_isoch_halted(&isoch);
	check(isoch.state == DRV_XHCI_ISOCH_HALT_PENDING, "halt pending");
	error = queue(&isoch, 2U, 4U, &first);
	check(error == EBUSY, "enqueue while halted");
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_STOPPING);
	check(error == EBUSY, "no stop while halted");

	/* The worker recovers: the rest ERROR. */
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_RECOVERING);
	check(error == 0, "recovery begins");
	flags = drv_xhci_isoch_flush(&isoch, DRV_XHCI_ISO_ERROR);
	(void)drv_xhci_isoch_take(&isoch, &taken);
	check(taken.result[0] == DRV_XHCI_ISO_MISSED && taken.result[1] == DRV_XHCI_ISO_ERROR && taken.result[3] == DRV_XHCI_ISO_ERROR,
	    "recovered results");

	/* A cancel takes over a halt. */
	(void)queue(&isoch, 3U, 4U, &first);
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_BABBLE, 1, first, 0U);
	drv_xhci_isoch_halted(&isoch);
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	check(error == 0, "cancel takes over a halt");
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_RECOVERING);
	check(error == EBUSY, "no recovery during a cancel");
}

/* Many rounds across the ring's wrap, with a TD retired while its request waits to be taken. */
static void
test_wrap(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first[3];
	unsigned round;
	unsigned index;
	unsigned flags;
	unsigned position;
	unsigned packet;
	int good;
	int got;
	int error;

	/* A ring of 20 slots and three requests of 6 at a time, 50 rounds. */
	drv_xhci_isoch_init(&isoch, 20U);
	good = 1;
	for (round = 0U; round < 50U; round++) {
		for (index = 0U; index < 3U; index++) {
			error = queue(&isoch, round * 3U + index, 6U, &first[index]);
			if (error != 0)
				good = 0;
		}

		/* The middle TD of each, in order, then the last ones. */
		for (index = 0U; index < 3U; index++) {
			error = drv_xhci_isoch_locate(&isoch, (first[index] + 5U) % 20U, &position, &packet);
			if (error != 0 || position != index || packet != 5U)
				good = 0;
			flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, (first[index] + 5U) % 20U, 0U);
			if ((flags & DRV_XHCI_ISOCH_COMPLETE) == 0U)
				good = 0;
		}

		/* All three taken. */
		for (index = 0U; index < 3U; index++) {
			got = drv_xhci_isoch_take(&isoch, &taken);
			if (!got || taken.tag != round * 3U + index || taken.result[5] != DRV_XHCI_ISO_OK)
				good = 0;
		}

		/* The ring empty again. */
		if (isoch.outstanding != 0U || isoch.consumer != isoch.producer)
			good = 0;
	}

	/* Every round. */
	check(good, "fifty rounds across the wrap");

	/* A finished request not yet taken: its slots are reused, and an event finds the new TD. */
	drv_xhci_isoch_init(&isoch, 10U);
	(void)queue(&isoch, 1U, 6U, &first[0]);
	(void)drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 5U, 0U);
	error = queue(&isoch, 2U, 6U, &first[1]);
	check(error == 0 && first[1] == 6U, "slots of a finished request reused");
	error = drv_xhci_isoch_locate(&isoch, 1U, &position, &packet);
	check(error == 0 && position == 1U && packet == 5U, "the new TD is found, not the retired one");
}

/*
 * A failed command (design 2.1b): the endpoint retained with its TDs,
 * enqueue and owners refused, a Stopped - Short Packet counted, and the
 * flush after the DMA is quiesced.
 */
static void
test_retain(
	void)
{
	struct drv_xhci_isoch_request taken;
	struct drv_xhci_isoch isoch;
	unsigned first;
	unsigned flags;
	int error;
	int got;

	/* A cancel whose Stop timed out. */
	drv_xhci_isoch_init(&isoch, TEST_SLOTS);
	(void)queue(&isoch, 1U, 8U, &first);
	(void)drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	drv_xhci_isoch_retain(&isoch);
	check(isoch.state == DRV_XHCI_ISOCH_RETAINED && isoch.outstanding == 8U, "retained with its TDs");

	/* Enqueue and every owner refused. */
	error = queue(&isoch, 2U, 1U, &first);
	check(error == EIO, "enqueue on a retained endpoint");
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_CANCELLING);
	check(error == EIO, "cancel on a retained endpoint");
	error = drv_xhci_isoch_begin(&isoch, DRV_XHCI_ISOCH_RECOVERING);
	check(error == EIO, "recovery on a retained endpoint");

	/* A late Stopped - Short Packet and a TD event: recorded or counted only. */
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_STOPPED_SHORT, 1, 2U, 0U);
	check(flags == 0U && isoch.stopped == 1U && isoch.foreign_stops == 0U, "Stopped - Short Packet counted");
	flags = drv_xhci_isoch_event(&isoch, DRV_XHCI_COMPLETION_SUCCESS, 1, 0U, 0U);
	check(flags == 0U && isoch.outstanding == 8U, "a TD event on a retained endpoint is only recorded");

	/* The quiesce's flush ends them. */
	flags = drv_xhci_isoch_flush(&isoch, DRV_XHCI_ISO_CANCELLED);
	got = drv_xhci_isoch_take(&isoch, &taken);
	check(flags == DRV_XHCI_ISOCH_COMPLETE && got && taken.result[0] == DRV_XHCI_ISO_OK && taken.result[7] == DRV_XHCI_ISO_CANCELLED,
	    "flushed after the quiesce");
	check(isoch.state == DRV_XHCI_ISOCH_NORMAL, "normal after the flush");
}

/* Runs every group. */
int
main(
	void)
{
	/* Each group. */
	test_room();
	test_bei();
	test_order();
	test_skip();
	test_skip_wait();
	test_stop();
	test_foreign();
	test_cancel();
	test_halt();
	test_wrap();
	test_retain();

	/* The summary. */
	if (failures != 0U) {
		printf("xhci-isoch: %u FAIL\n", failures);
		return EXIT_FAILURE;
	}

	/* Succeeded: every check passed. */
	printf("xhci-isoch: all PASS\n");
	return EXIT_SUCCESS;
}
