/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of a touch pad's two-finger scrolling in the terminal
 * (ws090-p019, userland/desktop/terminal/touch.c's TERMINAL_TOUCH_PAD and
 * _PAD_STOP) on libkeiland's scroller, without Wayland.
 *
 * A grid of 40 rows of 17-pixel lines over a scrollback of 1000 lines,
 * the test's own clock, the compositor's times 3 s behind it (another
 * origin, as touch_time takes them), and a frame every 16 ms; the test
 * plays the main loop.  It checks: the fingers moving up (a wheel's
 * negative) take the view back into the scrollback at once, as far as
 * they moved; their lift flings it on further, and it rests; fingers that
 * rest before lifting throw nothing; a move during a flight catches it;
 * fingers on the screen keep the view from the pad.
 */

#include "../../../userland/desktop/terminal/touch.h"

#include <stdio.h>
#include <string.h>

/* The line's height, the rows, the lines kept, the pad's report interval and the frame interval (microseconds). */
#define TEST_CELL	17U
#define TEST_ROWS	40U
#define TEST_HISTORY	1000U
#define TEST_REPORT_US	10000U
#define TEST_FRAME_US	16000U

/* How far the compositor's clock is behind the test's (microseconds). */
#define TEST_BEHIND_US	3000000U

/* The touch screen under test. */
static struct terminal_touch touch;

/* The screen's view as the main loop keeps it (lines back from the live screen), and the offset within a line. */
static unsigned screen_view;
static int screen_offset;

/* The screen, whose place is the token the fingers tell screens apart by. */
static int screen;

/* The test's clock (microseconds). */
static uint64_t clock_us;

/* The checks run and the ones that failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *what);
static void round_trip(void);
static void run(double milliseconds);
static void pad(unsigned type, float dy);
static void swipe(unsigned moves, float dy);
static double back(void);
static void reset(void);
static void test_drag_fling(void);
static void test_rest(void);
static void test_catch(void);
static void test_screen_finger(void);

/*
 * Runs every test and reports the result.
 */
int
main(void)
{
	int error;

	/* The touch screen, at the live screen. */
	error = terminal_touch_open(&touch);
	check(error == 0, "the touch screen opens");
	clock_us = 10000000U;
	round_trip();

	/* The tests. */
	test_drag_fling();
	test_rest();
	test_catch();
	test_screen_finger();
	terminal_touch_close(&touch);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-pad-terminal: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-pad-terminal: ok (%u checks)\n", checks);
	return 0;
}

/* Counts one check and prints it when it fails. */
static void
check(
	int passed,
	const char *what)
{
	/* Counts the check; a passed one prints nothing. */
	checks++;
	if (passed)
		return;

	/* Prints the failure. */
	failures++;
	printf("FAIL: %s\n", what);
}

/* Plays one round of the main loop: the screen's view to the fingers, the view they set back. */
static void
round_trip(void)
{
	unsigned view;
	int offset;
	int moved;

	/* The screen's view, time, and the view the fingers set. */
	terminal_touch_layout(&touch, &screen, TEST_CELL, TEST_HISTORY, TEST_ROWS * TEST_CELL, screen_view, screen_offset);
	(void)terminal_touch_tick(&touch, clock_us);
	moved = terminal_touch_view(&touch, &view, &offset);
	if (moved) {
		screen_view = view;
		screen_offset = offset;
	}
}

/* Runs frames for a while. */
static void
run(
	double milliseconds)
{
	uint64_t until;

	/* Frames until the time. */
	until = clock_us + (uint64_t)(milliseconds * 1000.0);
	while (clock_us < until) {
		clock_us += TEST_FRAME_US;
		round_trip();
	}
}

/* Sends one input of the pad at the clock's time (the compositor's time behind it). */
static void
pad(
	unsigned type,
	float dy)
{
	struct terminal_touch_event event;

	/* The input, as the window queues it. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.y = dy;
	event.time = (uint32_t)((clock_us - TEST_BEHIND_US) / 1000U);
	event.arrival = clock_us;
	terminal_touch_event(&touch, &event);
}

/* Moves the fingers a number of times by dy each, every TEST_REPORT_US, with a round after each. */
static void
swipe(
	unsigned moves,
	float dy)
{
	unsigned index;

	/* Each move, and a round. */
	for (index = 0U; index < moves; index++) {
		pad(TERMINAL_TOUCH_PAD, dy);
		clock_us += TEST_REPORT_US;
		round_trip();
	}
}

/* Gives how far back from the live screen the view is (pixels). */
static double
back(void)
{
	/* Lines and the offset into the next one. */
	return (double)screen_view * (double)TEST_CELL + (double)screen_offset;
}

/* Puts the view back at the live screen at rest, a while later. */
static void
reset(void)
{
	/* Set elsewhere: the fingers take it over at rest. */
	run(3000.0);
	screen_view = 0U;
	screen_offset = 0;
	round_trip();
	run(100.0);
}

/* The fingers drag the view back into the scrollback, and their lift flings it on. */
static void
test_drag_fling(void)
{
	double dragged;
	double flown;

	/* Ten moves of 30 px up are 300 px back, at once. */
	reset();
	swipe(10U, -30.0f);
	dragged = back();
	check(dragged >= 299.0 && dragged <= 301.0, "pad: the view follows the fingers at once");

	/* The lift at 3000 px/s flings it further back, and it rests. */
	pad(TERMINAL_TOUCH_PAD_STOP, 0.0f);
	run(3000.0);
	flown = back();
	check(flown > dragged + 300.0, "pad: the lift flings the view on");
	run(500.0);
	check(back() == flown, "pad: the view rests");
}

/* Fingers that rest before lifting throw nothing. */
static void
test_rest(void)
{
	double dragged;

	/* The moves, a rest of 200 ms, then the lift. */
	reset();
	swipe(10U, -30.0f);
	dragged = back();
	run(200.0);
	pad(TERMINAL_TOUCH_PAD_STOP, 0.0f);
	run(1000.0);
	check(back() >= dragged - 1.0 && back() <= dragged + 1.0, "pad: rested fingers throw nothing");
}

/* A move during a flight catches it. */
static void
test_catch(void)
{
	double caught;

	/* A flight, caught after 100 ms by a move and held. */
	reset();
	swipe(10U, -30.0f);
	pad(TERMINAL_TOUCH_PAD_STOP, 0.0f);
	run(100.0);
	pad(TERMINAL_TOUCH_PAD, 0.0f);
	round_trip();
	caught = back();
	run(500.0);
	check(back() >= caught - 1.0 && back() <= caught + 1.0, "pad: a move catches the flight, and the view waits for the fingers");
	pad(TERMINAL_TOUCH_PAD_STOP, 0.0f);
	run(1000.0);
}

/* Fingers on the screen keep the view from the pad. */
static void
test_screen_finger(void)
{
	struct terminal_touch_event event;
	double before;

	/* A finger down on the screen. */
	reset();
	memset(&event, 0, sizeof(event));
	event.type = TERMINAL_TOUCH_DOWN;
	event.id = 1;
	event.x = 100.0f;
	event.y = 300.0f;
	event.time = (uint32_t)((clock_us - TEST_BEHIND_US) / 1000U);
	event.arrival = clock_us;
	terminal_touch_event(&touch, &event);
	round_trip();
	before = back();

	/* The pad moves nothing meanwhile. */
	swipe(5U, -30.0f);
	check(back() == before, "pad: a finger on the screen keeps the view");

	/* The finger lifts. */
	event.type = TERMINAL_TOUCH_UP;
	event.time = (uint32_t)((clock_us - TEST_BEHIND_US) / 1000U);
	event.arrival = clock_us;
	terminal_touch_event(&touch, &event);
	run(1000.0);
}
