/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the terminal's touch screen (ws081-p011,
 * userland/desktop/terminal/touch.c) on libkeiland, without Wayland and
 * Vulkan.
 *
 * A grid of 40 rows of 17-pixel lines over a scrollback of 1000 lines, the
 * test's own clock, fingers at 60 Hz and a frame every 16 ms; the test
 * plays the main loop (it applies the view the fingers set, and gives the
 * screen's view back each round).  It checks: a flick down shows older
 * lines and glides on, a pixel at a time; a flick past the oldest line
 * rests there; a pull past the live screen stretches and springs back; a
 * tap is a click and a caught glide taps nothing; a long press holds the
 * button (the main loop turns the hold into a word selection or a held
 * press on the selection, ws081-p014) and a drag after it moves the
 * pointer, not the view; a view set elsewhere (new output, a key, another tab) stops a
 * glide and is taken over.
 */

#include "../../../userland/desktop/terminal/touch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The line's height, the rows, the lines kept, and the finger's report and frame intervals (microseconds). */
#define TEST_CELL	17U
#define TEST_ROWS	40U
#define TEST_HISTORY	1000U
#define TEST_REPORT_US	16667U
#define TEST_FRAME_US	16000U

/* The touch screen under test. */
static struct terminal_touch touch;

/* The screen's view as the main loop keeps it, in lines back from the live screen. */
static unsigned screen_view;

/* The screen's offset within a line, in pixels. */
static int screen_offset;

/* Two screens (two tabs), whose places are the tokens the fingers tell screens apart by. */
static int screens[2];

/* Which of the two screens is shown. */
static int shown;

/* The pointer events the fingers made, as the main loop took them. */
static struct terminal_touch_pointer made[64];

/* How many pointer events were taken. */
static unsigned made_count;

/* The test's clock, microseconds. */
static uint64_t clock_us;

/* The number of checks run. */
static unsigned checks;

/* The number of checks that failed. */
static unsigned failures;

static void check(int passed, const char *format, ...);
static void round_trip(void);
static void frame_tick(void);
static void run(double milliseconds);
static void finger(unsigned type, int32_t id, float x, float y);
static void stroke(int32_t id, float x, float y, float dy, double milliseconds, int down, int up);
static double position(void);
static void reset(unsigned view);
static void test_scroll(void);
static void test_edges(void);
static void test_taps(void);
static void test_select(void);
static void test_elsewhere(void);

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
	clock_us = 5000000U;
	round_trip();

	/* The tests. */
	test_scroll();
	test_edges();
	test_taps();
	test_select();
	test_elsewhere();
	terminal_touch_close(&touch);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-termtouch: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-termtouch: ok (%u checks)\n", checks);
	return 0;
}

/* Counts one check and prints it when it fails. */
static void
check(
	int passed,
	const char *format,
	...)
{
	va_list arguments;

	/* Counts the check; a passed one prints nothing. */
	checks++;
	if (passed)
		return;

	/* Prints the failure. */
	failures++;
	printf("FAIL: ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* Plays one round of the main loop: the screen's view to the fingers, their view and pointer events back. */
static void
round_trip(void)
{
	struct terminal_touch_pointer pointer;
	unsigned view;
	int offset;
	int moved;
	int taken;

	/* The screen's view, time, and the view the fingers set. */
	terminal_touch_layout(&touch, &screens[shown], TEST_CELL, TEST_HISTORY, TEST_ROWS * TEST_CELL, screen_view, screen_offset);
	(void)terminal_touch_tick(&touch, clock_us);
	moved = terminal_touch_view(&touch, &view, &offset);
	if (moved) {
		screen_view = view;
		screen_offset = offset;
	}

	/* The pointer events they made. */
	for (;;) {
		taken = terminal_touch_take_pointer(&touch, &pointer);
		if (!taken)
			break;
		if (made_count < sizeof(made) / sizeof(made[0])) {
			made[made_count] = pointer;
			made_count++;
		}
	}
}

/* Moves the clock on by a frame and plays a round. */
static void
frame_tick(void)
{
	/* The next frame. */
	clock_us += TEST_FRAME_US;
	round_trip();
}

/* Runs frames for a while. */
static void
run(
	double milliseconds)
{
	uint64_t until;

	/* Frames until the time. */
	until = clock_us + (uint64_t)(milliseconds * 1000.0);
	while (clock_us < until)
		frame_tick();
}

/* Sends one touch input at the clock's time. */
static void
finger(
	unsigned type,
	int32_t id,
	float x,
	float y)
{
	struct terminal_touch_event event;

	/* The input, read as it is made. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time = (uint32_t)(clock_us / 1000U);
	event.serial = 77U;
	event.arrival = clock_us;
	terminal_touch_event(&touch, &event);
}

/*
 * Moves a finger from a place by dy over a time at 60 Hz, with frames
 * between reports; it touches first when down says so, and lifts at the
 * end when up says so.
 */
static void
stroke(
	int32_t id,
	float x,
	float y,
	float dy,
	double milliseconds,
	int down,
	int up)
{
	uint64_t start;
	uint64_t next;
	float share;

	/* The touch. */
	if (down)
		finger(TERMINAL_TOUCH_DOWN, id, x, y);
	start = clock_us;

	/* The reports along the way, frames between them. */
	next = start + TEST_REPORT_US;
	while (next <= start + (uint64_t)(milliseconds * 1000.0)) {
		while (clock_us + TEST_FRAME_US <= next)
			frame_tick();
		clock_us = next;
		share = (float)((double)(next - start) / (milliseconds * 1000.0));
		finger(TERMINAL_TOUCH_MOTION, id, x, y + dy * share);
		next += TEST_REPORT_US;
	}

	/* The lift, a few milliseconds after the last report. */
	if (up) {
		clock_us += 4000U;
		finger(TERMINAL_TOUCH_UP, id, 0.0f, 0.0f);
	}
}

/* The screen's view in pixels back from the live screen. */
static double
position(void)
{
	/* Whole lines and the offset. */
	return (double)screen_view * (double)TEST_CELL + (double)screen_offset;
}

/* Puts the screen at a view, as a key or the wheel would, and lets everything rest. */
static void
reset(
	unsigned view)
{
	/* The view, taken over by the fingers next round. */
	screen_view = view;
	screen_offset = 0;
	run(3000.0);
	made_count = 0;
}

/* A flick down shows older lines, a pixel at a time, and glides on. */
static void
test_scroll(void)
{
	double released;
	double before;
	double later;
	double moved;
	double within;
	int smooth;
	int steps;

	/* A finger flicked down by 300 px in 100 ms, at the live screen. */
	reset(0U);
	stroke(1, 400.0f, 200.0f, 300.0f, 100.0f, 1, 1);
	released = position();
	check(released > 200.0 && released < 320.0, "the text followed the finger: %.0f px back", released);

	/* It glides on, in pixels, not whole lines only. */
	smooth = 0;
	for (steps = 0; steps < 12; steps++) {
		before = position();
		frame_tick();
		moved = position() - before;
		within = fmod(moved, (double)TEST_CELL);
		if (within != 0.0)
			smooth = 1;
	}

	/* The glide's result. */
	later = position();
	check(later > released + 150.0, "the view glides on: %.0f then %.0f px back", released, later);
	check(smooth, "a pixel at a time, not a line at a time");
	check(screen_offset >= 0 && screen_offset < (int)TEST_CELL, "the offset stays within a line: %d", screen_offset);

	/* It rests where the fling stops (a 3000 px/s fling goes 1159 px). */
	run(3000.0);
	check(fabs(position() - released - 1159.0) < 1159.0 * 0.15, "it rests about a 3000 px/s fling further: %.0f px",
	      position() - released);
	check(made_count == 0U, "a scroll presses nothing (%u pointer events)", made_count);
}

/* Past the oldest line it rests there; past the live screen it stretches and springs back. */
static void
test_edges(void)
{
	double peak;
	double now_at;

	/* A hard flick near the oldest line: it rests on the oldest line. */
	reset(TEST_HISTORY - 20U);
	stroke(1, 400.0f, 200.0f, 400.0f, 80.0f, 1, 1);
	peak = 0.0;
	while (touch.moving) {
		frame_tick();
		now_at = position();
		if (now_at > peak)
			peak = now_at;
	}

	/* The glide's result. */
	check(peak > (double)(TEST_HISTORY * TEST_CELL) + 5.0, "a flick passes the oldest line: %.0f", peak);
	check(screen_view == TEST_HISTORY && screen_offset == 0, "and rests on it: %u, %d", screen_view, screen_offset);

	/* At the live screen, pulled up: the text moves up less than the finger and springs back. */
	reset(0U);
	stroke(1, 400.0f, 500.0f, -200.0f, 300.0f, 1, 0);
	run(50.0);
	check(screen_view == 0U && screen_offset < -50 && screen_offset > -110, "the live screen stretches: %d", screen_offset);
	clock_us += 4000U;
	finger(TERMINAL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(1500.0);
	check(screen_view == 0U && screen_offset == 0, "and springs back: %u, %d", screen_view, screen_offset);
}

/* A tap is a click; a tap that caught a glide is not. */
static void
test_taps(void)
{
	/* A tap. */
	reset(10U);
	finger(TERMINAL_TOUCH_DOWN, 1, 100.0f, 120.0f);
	clock_us += 70000U;
	finger(TERMINAL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(50.0);
	check(made_count == 2U && made[0].kind == TERMINAL_TOUCH_PRESS && made[1].kind == TERMINAL_TOUCH_RELEASE &&
	      made[0].x == 100 && made[0].y == 120 && made[0].serial == 77U, "a tap is a press and a release there (%u)", made_count);

	/* A glide, and a tap that catches it: no click, and the view stays. */
	reset(10U);
	stroke(1, 400.0f, 200.0f, 300.0f, 100.0f, 1, 1);
	run(150.0);
	made_count = 0;
	finger(TERMINAL_TOUCH_DOWN, 2, 100.0f, 120.0f);
	clock_us += 70000U;
	finger(TERMINAL_TOUCH_UP, 2, 0.0f, 0.0f);
	run(500.0);
	check(touch.caught && made_count == 0U, "a tap that caught the glide clicks nothing (%u)", made_count);
}

/* A long press holds the button (the main loop selects or holds the selection); a drag after it moves the pointer, not the view. */
static void
test_select(void)
{
	double before;
	unsigned index;
	int motions;

	/* A long press. */
	reset(5U);
	before = position();
	finger(TERMINAL_TOUCH_DOWN, 1, 200.0f, 300.0f);
	run(600.0);
	check(made_count == 1U && made[0].kind == TERMINAL_TOUCH_HOLD && made[0].x == 200 && made[0].y == 300 && made[0].serial == 77U,
	      "a long press holds the button there (%u)", made_count);

	/* A drag after it: the pointer moves, the view does not. */
	stroke(1, 200.0f, 300.0f, 120.0f, 200.0f, 0, 1);
	run(500.0);
	motions = 0;
	for (index = 1U; index < made_count; index++) {
		if (made[index].kind == TERMINAL_TOUCH_POINTER_MOTION)
			motions++;
	}

	/* The selection moved, the view did not. */
	check(motions >= 10 && made[made_count - 1U].kind == TERMINAL_TOUCH_RELEASE, "the drag moved the selection (%d motions) and the lift released it",
	      motions);
	check(position() == before, "the view did not move: %.0f then %.0f", before, position());
}

/* A view set elsewhere stops a glide and is taken over; another screen too. */
static void
test_elsewhere(void)
{
	unsigned view;
	int offset;

	/* A glide, and new output moves the view on (as the terminal keeps a view back on its text). */
	reset(10U);
	stroke(1, 400.0f, 200.0f, 300.0f, 100.0f, 1, 1);
	run(100.0);
	screen_view += 3U;
	view = screen_view;
	offset = screen_offset;
	run(1000.0);
	check(!touch.moving && screen_view == view && screen_offset == offset, "the output stopped the glide where it put the view: %u,%d",
	      screen_view, screen_offset);

	/* A key shows the live screen: taken over at once. */
	screen_view = 0U;
	screen_offset = 0;
	run(200.0);
	check(screen_view == 0U && screen_offset == 0, "a key's live screen stays");

	/* A glide, and another tab's screen that happens to be at the same view: the glide stops on the tab switch. */
	reset(10U);
	stroke(1, 400.0f, 200.0f, 300.0f, 100.0f, 1, 1);
	run(100.0);
	shown = 1 - shown;
	view = screen_view;
	offset = screen_offset;
	run(1000.0);
	check(!touch.moving && screen_view == view && screen_offset == offset, "another tab stopped the glide: %u,%d (%u,%d)", screen_view,
	      screen_offset, view, offset);

	/* Another tab's screen (another token) at its own view: taken over, and a drag starts from it. */
	shown = 1 - shown;
	screen_view = 42U;
	stroke(1, 400.0f, 200.0f, 34.0f, 300.0f, 1, 1);
	run(2000.0);
	check(screen_view >= 43U && screen_view <= 46U, "another screen's view is taken over: %u", screen_view);
}
