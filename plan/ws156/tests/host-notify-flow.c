/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the notifications' popup movement (ws156-p003,
 * userland/desktop/wayland/notify-flow.c) with the model (notify.c), as
 * notify-popup.c drives them every 10 ms: the stages' times, the place and
 * opacity along them, the shorter stay when others wait (and the next
 * entering as one leaves), the pointer holding a board, an urgent board
 * waiting for a touch (at most 30 s), a board taken off at once, and what
 * the log holds: shown ones in the order they went, dismissed ones not.
 * Prints "ok ..." lines, then a summary; exits 1 on a failure.
 */

#include "userland/desktop/wayland/notify-flow.h"
#include "userland/desktop/wayland/notify.h"

#include <stdio.h>
#include <string.h>

/* The screen's and the board's widths the places are checked with. */
#define FIXTURE_SCREEN	1280
#define FIXTURE_BOARD	320

/* The checks run and failed. */
static unsigned fixture_checks;
static unsigned fixture_failures;

static void fixture_check(int passed, const char *what);
static uint64_t fixture_run(struct kwl_notify_flow *flow, uint64_t from, uint64_t to, int waiting, int hover, struct kwl_notify_flow_events *seen);
static void test_stages(void);
static void test_places(void);
static void test_waiting(void);
static void test_hover(void);
static void test_urgent(void);
static void test_remove(void);
static void test_with_model(void);

/* Runs the tests. */
int
main(void)
{
	/* Each. */
	test_stages();
	test_places();
	test_waiting();
	test_hover();
	test_urgent();
	test_remove();
	test_with_model();

	/* The summary. */
	printf("%u checks, %u failures\n", fixture_checks, fixture_failures);
	if (fixture_failures != 0U)
		return 1;
	return 0;
}

/* Counts a check and says how it went. */
static void
fixture_check(
	int passed,
	const char *what)
{
	/* Counted, and printed. */
	fixture_checks++;
	if (passed) {
		printf("ok %s\n", what);
	} else {
		fixture_failures++;
		printf("FAIL %s\n", what);
	}
}

/*
 * Steps a flow every 10 ms from one time to another (both included), the
 * events of every step gathered (the last of each kind kept; show_next
 * kept once seen).  Returns the time of the first started_leaving (0:
 * none).
 */
static uint64_t
fixture_run(
	struct kwl_notify_flow *flow,
	uint64_t from,
	uint64_t to,
	int waiting,
	int hover,
	struct kwl_notify_flow_events *seen)
{
	struct kwl_notify_flow_events events;
	uint64_t now;
	uint64_t leaving;

	/* Every 10 ms. */
	memset(seen, 0, sizeof(*seen));
	leaving = 0U;
	for (now = from; now <= to; now += 10U) {
		kwl_notify_flow_step(flow, now, waiting, hover, &events);
		if (events.started_leaving != 0U) {
			seen->started_leaving = events.started_leaving;
			if (leaving == 0U)
				leaving = now;
		}
		if (events.left != 0U)
			seen->left = events.left;
		if (events.show_next)
			seen->show_next = 1;
	}
	return leaving;
}

/* One board alone: 280 ms in, 3000 ms staying, 280 ms out. */
static void
test_stages(void)
{
	struct kwl_notify_flow flow;
	struct kwl_notify_flow_events seen;
	uint64_t leaving;

	/* Shown at 1000. */
	kwl_notify_flow_init(&flow);
	kwl_notify_flow_show(&flow, 7U, 0, 1000U);
	fixture_check(flow.current.stage == KWL_NOTIFY_FLOW_ENTER, "a shown board enters");

	/* Staying from 1280 on. */
	(void)fixture_run(&flow, 1000U, 1270U, 0, 0, &seen);
	fixture_check(flow.current.stage == KWL_NOTIFY_FLOW_ENTER, "still entering at 270 ms");
	(void)fixture_run(&flow, 1280U, 1280U, 0, 0, &seen);
	fixture_check(flow.current.stage == KWL_NOTIFY_FLOW_STAY, "staying at 280 ms");

	/* Leaving 3000 ms later, the next free at once. */
	leaving = fixture_run(&flow, 1290U, 4400U, 0, 0, &seen);
	fixture_check(leaving == 4280U, "leaves after a 3000 ms stay");
	fixture_check(seen.started_leaving == 7U && flow.current.stage == KWL_NOTIFY_FLOW_NONE, "the board is done with as it starts to leave");
	fixture_check(flow.leaving.stage == KWL_NOTIFY_FLOW_LEAVE && flow.leaving.id == 7U, "and it is on the leaving board");

	/* Gone 280 ms after. */
	(void)fixture_run(&flow, 4410U, 4550U, 0, 0, &seen);
	fixture_check(seen.left == 0U, "not gone at 270 ms of leaving");
	(void)fixture_run(&flow, 4560U, 4560U, 0, 0, &seen);
	fixture_check(seen.left == 7U && flow.leaving.stage == KWL_NOTIFY_FLOW_NONE, "gone at 280 ms of leaving");
	fixture_check(!kwl_notify_flow_moving(&flow), "nothing moves after");
}

/* The places and opacities along the stages. */
static void
test_places(void)
{
	struct kwl_notify_board board;
	int32_t left;
	float opacity;
	int32_t middle;

	/* Entering: from the right edge, eased out, fading in. */
	middle = (FIXTURE_SCREEN - FIXTURE_BOARD) / 2;
	memset(&board, 0, sizeof(board));
	board.stage = KWL_NOTIFY_FLOW_ENTER;
	board.since = 1000U;
	kwl_notify_flow_place(&board, 1000U, FIXTURE_SCREEN, FIXTURE_BOARD, &left, &opacity);
	fixture_check(left == FIXTURE_SCREEN && opacity == 0.0f, "entering starts at the right edge, unseen");
	kwl_notify_flow_place(&board, 1140U, FIXTURE_SCREEN, FIXTURE_BOARD, &left, &opacity);
	fixture_check(opacity > 0.874f && opacity < 0.876f, "half way in, eased out: 0.875 opaque");
	fixture_check(left == (int32_t)((float)FIXTURE_SCREEN + ((float)middle - (float)FIXTURE_SCREEN) * 0.875f), "half way in, 7/8 of the way");
	kwl_notify_flow_place(&board, 1280U, FIXTURE_SCREEN, FIXTURE_BOARD, &left, &opacity);
	fixture_check(left == middle && opacity == 1.0f, "in, in the middle, opaque");

	/* Staying: the middle. */
	board.stage = KWL_NOTIFY_FLOW_STAY;
	kwl_notify_flow_place(&board, 2000U, FIXTURE_SCREEN, FIXTURE_BOARD, &left, &opacity);
	fixture_check(left == middle && opacity == 1.0f, "staying in the middle");

	/* Leaving: to beyond the left edge, eased in, fading out. */
	board.stage = KWL_NOTIFY_FLOW_LEAVE;
	board.since = 5000U;
	kwl_notify_flow_place(&board, 5140U, FIXTURE_SCREEN, FIXTURE_BOARD, &left, &opacity);
	fixture_check(opacity > 0.874f && opacity < 0.876f, "half way out, eased in: 0.875 opaque");
	fixture_check(left == (int32_t)((float)middle + (-(float)FIXTURE_BOARD - (float)middle) * 0.125f), "half way out, 1/8 of the way");
	kwl_notify_flow_place(&board, 5280U, FIXTURE_SCREEN, FIXTURE_BOARD, &left, &opacity);
	fixture_check(left == -FIXTURE_BOARD && opacity == 0.0f, "out: beyond the left edge, unseen");
}

/* Others waiting: a 1500 ms stay, and the next free as one starts to leave. */
static void
test_waiting(void)
{
	struct kwl_notify_flow flow;
	struct kwl_notify_flow_events seen;
	uint64_t leaving;

	/* One shown, others waiting. */
	kwl_notify_flow_init(&flow);
	kwl_notify_flow_show(&flow, 1U, 0, 0U);
	(void)fixture_run(&flow, 0U, 280U, 1, 0, &seen);
	leaving = fixture_run(&flow, 290U, 2000U, 1, 0, &seen);
	fixture_check(leaving == 1780U, "with others waiting it leaves after 1500 ms");
	fixture_check(seen.show_next, "and the next may show at once");

	/* The next enters while the first leaves. */
	kwl_notify_flow_show(&flow, 2U, 0, 1780U);
	fixture_check(flow.current.id == 2U && flow.leaving.id == 1U, "one enters while the other leaves");

	/* Nothing waiting and nothing shown: no next. */
	kwl_notify_flow_init(&flow);
	(void)fixture_run(&flow, 0U, 100U, 0, 0, &seen);
	fixture_check(!seen.show_next, "no next without one waiting");
}

/* The pointer on the board holds it: the stay is counted without that time. */
static void
test_hover(void)
{
	struct kwl_notify_flow flow;
	struct kwl_notify_flow_events seen;
	uint64_t leaving;

	/* In at 280, the pointer on it from 1000 to 4000. */
	kwl_notify_flow_init(&flow);
	kwl_notify_flow_show(&flow, 3U, 0, 0U);
	(void)fixture_run(&flow, 0U, 990U, 0, 0, &seen);
	leaving = fixture_run(&flow, 1000U, 4000U, 0, 1, &seen);
	fixture_check(leaving == 0U, "the pointer on it keeps it");
	leaving = fixture_run(&flow, 4010U, 9000U, 0, 0, &seen);
	fixture_check(leaving >= 6270U && leaving <= 6290U, "the stay goes on from where it was (about 3000 ms after the pointer left, less 720)");
}

/* An urgent board stays until touched (at most 30 s), then its ordinary time. */
static void
test_urgent(void)
{
	struct kwl_notify_flow flow;
	struct kwl_notify_flow_events seen;
	uint64_t leaving;

	/* Untouched: 30 s. */
	kwl_notify_flow_init(&flow);
	kwl_notify_flow_show(&flow, 4U, 1, 0U);
	leaving = fixture_run(&flow, 0U, 40000U, 0, 0, &seen);
	fixture_check(leaving == 30280U, "an untouched urgent board stays 30 s");

	/* Touched at 10 s: 3 s from then. */
	kwl_notify_flow_init(&flow);
	kwl_notify_flow_show(&flow, 5U, 1, 0U);
	(void)fixture_run(&flow, 0U, 10000U, 0, 0, &seen);
	kwl_notify_flow_touch(&flow);
	leaving = fixture_run(&flow, 10010U, 20000U, 0, 0, &seen);
	fixture_check(leaving >= 13000U && leaving <= 13020U, "a touched urgent board stays its ordinary 3 s from the touch");
}

/* A board taken off (dismissed): gone at once, the next free. */
static void
test_remove(void)
{
	struct kwl_notify_flow flow;
	struct kwl_notify_flow_events seen;

	/* Shown, then removed. */
	kwl_notify_flow_init(&flow);
	kwl_notify_flow_show(&flow, 6U, 0, 0U);
	(void)fixture_run(&flow, 0U, 500U, 1, 0, &seen);
	kwl_notify_flow_remove(&flow, 6U);
	fixture_check(!kwl_notify_flow_moving(&flow), "a removed board is gone");
	(void)fixture_run(&flow, 510U, 510U, 1, 0, &seen);
	fixture_check(seen.show_next, "and the next may show");
}

/* The flow with the model as the popup drives them: shown ones logged as they leave, dismissed ones not. */
static void
test_with_model(void)
{
	struct kwl_notify_model model;
	struct kwl_notify_flow flow;
	struct kwl_notify_flow_events events;
	struct kwl_notify_closed closed[2];
	const struct kwl_notification *log[8];
	size_t closed_count;
	size_t count;
	uint32_t ids[3];
	uint32_t id;
	uint64_t now;
	int error;

	/* Three posted. */
	kwl_notify_model_init(&model);
	kwl_notify_flow_init(&flow);
	error = kwl_notify_post(&model, 5U, 9U, 0U, "app", "one", "body one", 0U, &ids[0], closed, &closed_count);
	error |= kwl_notify_post(&model, 5U, 9U, 0U, "app", "two", "body two", 0U, &ids[1], closed, &closed_count);
	error |= kwl_notify_post(&model, 5U, 9U, 0U, "app", "three", "body three", 0U, &ids[2], closed, &closed_count);
	fixture_check(error == 0, "three posted");

	/* The popup's loop for 20 s; the second one is dismissed while it shows. */
	for (now = 10U; now <= 20000U; now += 10U) {
		kwl_notify_flow_step(&flow, now, kwl_notify_waiting(&model) > 0U, 0, &events);
		if (events.started_leaving != 0U)
			(void)kwl_notify_hide(&model, closed, &closed_count);
		if (events.show_next) {
			error = kwl_notify_show_next(&model, &id);
			if (error == 0)
				kwl_notify_flow_show(&flow, id, 0, now);
		}
		if (flow.current.id == ids[1] && flow.current.stage == KWL_NOTIFY_FLOW_STAY) {
			(void)kwl_notify_dismiss(&model, ids[1], &closed[0]);
			kwl_notify_flow_remove(&flow, ids[1]);
		}
	}

	/* The log: the third, then the first (newest first); the dismissed one not. */
	count = kwl_notify_log(&model, log, 8U);
	fixture_check(count == 2U, "two logged");
	fixture_check(count == 2U && log[0]->id == ids[2] && log[1]->id == ids[0], "the third and the first, newest first");
	fixture_check(kwl_notify_find(&model, ids[1]) == NULL, "the dismissed one is gone");
	fixture_check(kwl_notify_waiting(&model) == 0U && kwl_notify_shown(&model) == NULL, "none waiting or shown at the end");
	kwl_notify_model_free(&model);
}
