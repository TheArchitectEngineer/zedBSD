/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the i915 request worker's park (ws052-p009, park.c).
 *
 * It plays the protocol as worker.c drives it: a suspend asks for the
 * park, the worker inside the display window leaves it, the worker outside
 * it parks, the suspend sees the park reached, the resume ends it and the
 * worker serves again; a stop wins over a park; a suspend that gives up
 * ends a park the worker had not reached, and a late worker does not park.
 *
 *   make -C plan/ws052/tests i915-park && build/ws052/host/i915-park
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <uapi/errno.h>

#include "drivers/gpu/i915/park.h"

static void check(bool condition, const char *what);
static void test_window(void);
static void test_outside(void);
static void test_stop(void);
static void test_give_up(void);
static void test_generations(void);

/* How many checks failed. */
static unsigned failures;

/*
 * Runs the checks and reports how many failed.
 */
int
main(void)
{
	/* Runs each scenario. */
	test_window();
	test_outside();
	test_stop();
	test_give_up();
	test_generations();

	/* Reports the outcome. */
	if (failures != 0) {
		printf("i915-park: %u checks failed\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("i915-park: every check passed\n");
	return 0;
}

/* Counts a check that failed and names it. */
static void
check(
	bool condition,
	const char *what)
{
	/* A check that held says nothing. */
	if (condition)
		return;

	/* Names the check. */
	printf("FAIL %s\n", what);
	failures++;
}

/* A park asked for while the worker is inside the display window. */
static void
test_window(void)
{
	struct i915_park park;
	uint32_t generation;
	int error;

	/* Without a park the worker serves, in the window or outside it. */
	drv_i915_park_init(&park);
	check(drv_i915_park_action(&park, 1, 0) == I915_PARK_SERVE, "window: serves without a park");
	check(drv_i915_park_action(&park, 0, 0) == I915_PARK_SERVE, "window: serves outside without a park");

	/* The suspend asks; inside the window the worker leaves it first. */
	error = drv_i915_park_begin(&park);
	generation = park.generation;
	check(error == 0, "window: the park is asked for");
	check(drv_i915_park_action(&park, 1, 0) == I915_PARK_LEAVE_WINDOW, "window: the window is left first");
	check(!drv_i915_park_reached(&park, generation), "window: not reached while in the window");

	/* Outside the window, the worker parks. */
	check(drv_i915_park_action(&park, 0, 0) == I915_PARK_PARK, "window: then the worker parks");
	drv_i915_park_entered(&park);
	check(drv_i915_park_reached(&park, generation), "window: the suspend sees the park");
	check(drv_i915_park_stays(&park, 0), "window: the parked worker sleeps");

	/* The resume ends it; the worker leaves the park and serves. */
	check(drv_i915_park_end(&park) == 1, "window: the resume ends a reached park");
	check(!drv_i915_park_stays(&park, 0), "window: the worker leaves the park");
	drv_i915_park_left(&park);
	check(drv_i915_park_action(&park, 1, 0) == I915_PARK_SERVE, "window: and serves again");
}

/* A park asked for while the worker is outside the window: it parks at once. */
static void
test_outside(void)
{
	struct i915_park park;
	int error;

	/* Asks, and the worker parks without a window to leave. */
	drv_i915_park_init(&park);
	error = drv_i915_park_begin(&park);
	check(error == 0, "outside: the park is asked for");
	check(drv_i915_park_action(&park, 0, 0) == I915_PARK_PARK, "outside: the worker parks");

	/* A second park while one is asked for is refused. */
	error = drv_i915_park_begin(&park);
	check(error == EBUSY, "outside: a park inside a park is EBUSY");
}

/* A stop wins over a park: the worker goes to withdraw the node. */
static void
test_stop(void)
{
	struct i915_park park;
	uint32_t generation;

	/* Asked for with a stop: the worker serves on to its end, in the window and outside. */
	drv_i915_park_init(&park);
	(void)drv_i915_park_begin(&park);
	generation = park.generation;
	check(drv_i915_park_action(&park, 1, 1) == I915_PARK_SERVE, "stop: the window is not left for the park");
	check(drv_i915_park_action(&park, 0, 1) == I915_PARK_SERVE, "stop: the worker does not park");
	check(!drv_i915_park_reached(&park, generation), "stop: the suspend never sees the park");

	/* A stop that comes while the worker is parked ends the park. */
	drv_i915_park_entered(&park);
	check(!drv_i915_park_stays(&park, 1), "stop: a parked worker leaves for the stop");
}

/* A suspend that gives up ends a park the worker did not reach; the late worker serves on. */
static void
test_give_up(void)
{
	struct i915_park park;

	/* The worker never looked: the suspend gives up. */
	drv_i915_park_init(&park);
	(void)drv_i915_park_begin(&park);
	check(drv_i915_park_end(&park) == 0, "give up: the worker had not parked");

	/* The worker looks late and serves, without parking. */
	check(drv_i915_park_action(&park, 0, 0) == I915_PARK_SERVE, "give up: the late worker serves");
	check(drv_i915_park_action(&park, 1, 0) == I915_PARK_SERVE, "give up: and stays in its window");
}

/* A waiter of an earlier park does not take a later one for its own. */
static void
test_generations(void)
{
	struct i915_park park;
	uint32_t first;
	uint32_t second;

	/* The first park is given up, a second one is reached. */
	drv_i915_park_init(&park);
	(void)drv_i915_park_begin(&park);
	first = park.generation;
	(void)drv_i915_park_end(&park);
	(void)drv_i915_park_begin(&park);
	second = park.generation;
	drv_i915_park_entered(&park);

	/* Only the second waiter sees it. */
	check(first != second, "generations: each park has its own");
	check(!drv_i915_park_reached(&park, first), "generations: the earlier waiter does not see it");
	check(drv_i915_park_reached(&park, second), "generations: the later waiter does");
}
