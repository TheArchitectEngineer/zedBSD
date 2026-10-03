/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the browser shell's touch screen (ws081-p006,
 * userland/desktop/browser/shell/touch.c) on libkeiland, without Wayland
 * and the engine.
 *
 * A page 3000 pixels past a 600-pixel view, the test's own clock, fingers
 * at 60 Hz and a frame every 16 ms; the page keeps the scroll inside the
 * document as the view does.  It checks: a flick scrolls and glides on to
 * rest at the fling's distance; past the top and the end the content
 * stretches (the overscroll) less than the finger moved and springs back;
 * a fling into an end stops there after a stretch; a tap is a click of the
 * primary button there, and a tap that catches a glide clicks nothing; a
 * long press lifted is a click of the secondary button, and one that then
 * moves scrolls and clicks nothing; two fingers scroll by their centroid;
 * a scroll set elsewhere or another page stops a glide, and a finger down
 * drags on from the new scroll; the compositor's cancel leaves no glide.
 */

#include "../../../userland/desktop/browser/shell/touch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The page's range and the view's height, in pixels. */
#define TEST_LARGEST	3000.0
#define TEST_HEIGHT	600.0

/* The finger's report interval and the frame interval, in microseconds. */
#define TEST_REPORT_US	16667U
#define TEST_FRAME_US	16000U

/* The touch screen under test. */
static struct shell_touch touch;

/* The page's token as the view has it (another page is another number). */
static unsigned long page_token;

/* The page's scroll as the view keeps it, within the document. */
static double page_scroll;

/* The page's overscroll as the view last got it. */
static double page_overscroll;

/* The largest overscroll seen since the last reset. */
static double stretch_most;

/* The smallest overscroll seen since the last reset. */
static double stretch_least;

/* The pointer events taken since the last reset. */
static struct shell_touch_pointer made[64];

/* How many of made hold events. */
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
static void reset(double scroll);
static unsigned count_kind(unsigned kind, int button);
static void test_flick(void);
static void test_ends(void);
static void test_taps(void);
static void test_long_press(void);
static void test_two_fingers(void);
static void test_elsewhere(void);

/*
 * Runs every test and reports the result.
 */
int
main(void)
{
	int error;

	/* The touch screen. */
	error = shell_touch_open(&touch);
	check(error == 0, "the touch screen opens");
	clock_us = 5000000U;
	page_token = 1U;
	round_trip();

	/* The tests. */
	test_flick();
	test_ends();
	test_taps();
	test_long_press();
	test_two_fingers();
	test_elsewhere();
	shell_touch_close(&touch);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-browsertouch: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-browsertouch: ok (%u checks)\n", checks);
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

/* Plays one round of the main loop: the page to the fingers, their scroll and pointer events back. */
static void
round_trip(void)
{
	struct shell_touch_pointer pointer;
	double scroll;
	double overscroll;
	int moved;
	int taken;

	/* The page, time, and the scroll the fingers set, kept inside the document as the view does. */
	shell_touch_layout(&touch, page_token, page_scroll, TEST_LARGEST, TEST_HEIGHT);
	(void)shell_touch_tick(&touch, clock_us);
	moved = shell_touch_scroll(&touch, &scroll, &overscroll);
	if (moved) {
		page_scroll = scroll;
		if (page_scroll < 0.0)
			page_scroll = 0.0;
		if (page_scroll > TEST_LARGEST)
			page_scroll = TEST_LARGEST;
		page_overscroll = overscroll;
	}

	/* The stretch seen. */
	if (page_overscroll > stretch_most)
		stretch_most = page_overscroll;
	if (page_overscroll < stretch_least)
		stretch_least = page_overscroll;

	/* The pointer events they made. */
	for (;;) {
		taken = shell_touch_take_pointer(&touch, &pointer);
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
	struct shell_touch_event event;

	/* The input, read as it is made. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time = (uint32_t)(clock_us / 1000U);
	event.arrival = clock_us;
	shell_touch_event(&touch, &event);
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
		finger(SHELL_TOUCH_DOWN, id, x, y);
	start = clock_us;

	/* The reports along the way, frames between them. */
	next = start + TEST_REPORT_US;
	while (next <= start + (uint64_t)(milliseconds * 1000.0)) {
		while (clock_us + TEST_FRAME_US <= next)
			frame_tick();
		clock_us = next;
		share = (float)((double)(next - start) / (milliseconds * 1000.0));
		finger(SHELL_TOUCH_MOTION, id, x, y + dy * share);
		next += TEST_REPORT_US;
	}

	/* The lift, a few milliseconds after the last report. */
	if (up) {
		clock_us += 4000U;
		finger(SHELL_TOUCH_UP, id, 0.0f, 0.0f);
	}
}

/* Puts the page at a scroll, as a key would, and lets everything rest. */
static void
reset(
	double scroll)
{
	/* The scroll, taken over by the fingers next round. */
	page_scroll = scroll;
	run(3000.0);
	made_count = 0;
	stretch_most = 0.0;
	stretch_least = 0.0;
}

/* Counts the pointer events of a kind and a button taken so far. */
static unsigned
count_kind(
	unsigned kind,
	int button)
{
	unsigned index;
	unsigned count;

	/* Each event. */
	count = 0;
	for (index = 0; index < made_count; index++) {
		if (made[index].kind == kind && made[index].button == button)
			count++;
	}

	/* The count. */
	return count;
}

/* A flick scrolls and glides on. */
static void
test_flick(void)
{
	double released;

	/* 300 px up in 100 ms (3000 px/s), lifted. */
	reset(0.0);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 0);
	released = page_scroll;
	clock_us += 4000U;
	finger(SHELL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(150.0);
	check(released > 200.0 && page_scroll > released + 150.0, "a flick scrolls and glides on: %.0f then %.0f", released, page_scroll);
	run(3000.0);
	check(fabs(page_scroll - released - 1159.0) < 1159.0 * 0.15, "it rests a 3000 px/s fling further: %.0f from %.0f", page_scroll, released);
	check(page_overscroll == 0.0 && made_count == 0U, "with no stretch and no click (%u)", made_count);
}

/* Past either end the content stretches and springs back. */
static void
test_ends(void)
{
	double held;

	/* Pulled down 200 px at the top: less than the finger, and back. */
	reset(0.0);
	stroke(1, 400.0f, 200.0f, 200.0f, 300.0f, 1, 0);
	run(100.0);
	held = page_overscroll;
	check(page_scroll == 0.0 && held > 30.0 && held < 190.0, "the top stretches less than the finger: %.1f", held);
	clock_us += 4000U;
	finger(SHELL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(2000.0);
	check(page_overscroll == 0.0 && page_scroll == 0.0, "and springs back: %.1f", page_overscroll);

	/* Pushed up 200 px at the end: the content moves up, and back. */
	reset(TEST_LARGEST);
	stroke(1, 400.0f, 500.0f, -200.0f, 300.0f, 1, 0);
	run(100.0);
	held = page_overscroll;
	check(page_scroll == TEST_LARGEST && held < -30.0 && held > -190.0, "the end stretches the other way: %.1f", held);
	clock_us += 4000U;
	finger(SHELL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(2000.0);
	check(page_overscroll == 0.0 && page_scroll == TEST_LARGEST, "and springs back: %.1f", page_overscroll);

	/* A fling into the end: it stretches past it on the way, and stops at the end. */
	reset(TEST_LARGEST - 200.0);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1);
	run(3000.0);
	check(page_scroll == TEST_LARGEST && page_overscroll == 0.0 && stretch_least < -1.0, "a fling into the end stretches (%.1f) and stops there", stretch_least);
}

/* A tap is a click; a tap that catches a glide is not. */
static void
test_taps(void)
{
	/* A tap. */
	reset(500.0);
	finger(SHELL_TOUCH_DOWN, 1, 120.0f, 300.0f);
	run(70.0);
	finger(SHELL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(100.0);
	check(made_count == 3U && made[0].kind == SHELL_TOUCH_POINTER_MOTION && made[1].kind == SHELL_TOUCH_POINTER_PRESS &&
	      made[2].kind == SHELL_TOUCH_POINTER_RELEASE && made[1].button == SHELL_TOUCH_PRIMARY && made[1].x == 120.0f && made[1].y == 300.0f,
	      "a tap is a click of the primary button there (%u)", made_count);
	check(page_scroll == 500.0, "and scrolls nothing");

	/* A tap on a glide catches it and clicks nothing. */
	reset(0.0);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1);
	run(100.0);
	made_count = 0;
	finger(SHELL_TOUCH_DOWN, 2, 200.0f, 300.0f);
	run(60.0);
	finger(SHELL_TOUCH_UP, 2, 0.0f, 0.0f);
	run(100.0);
	check(touch.caught && made_count == 0U && !touch.moving, "a tap that caught the glide clicks nothing (%u)", made_count);
}

/* A long press lifted is the secondary button's click; one that moves scrolls. */
static void
test_long_press(void)
{
	double before;

	/* Held 700 ms and lifted. */
	reset(500.0);
	finger(SHELL_TOUCH_DOWN, 1, 150.0f, 250.0f);
	run(700.0);
	check(made_count == 0U, "a long press waits for the lift");
	finger(SHELL_TOUCH_UP, 1, 0.0f, 0.0f);
	run(100.0);
	check(count_kind(SHELL_TOUCH_POINTER_PRESS, SHELL_TOUCH_SECONDARY) == 1U &&
	      count_kind(SHELL_TOUCH_POINTER_RELEASE, SHELL_TOUCH_SECONDARY) == 1U && made[1].x == 150.0f,
	      "a long press lifted is the secondary button's click (%u)", made_count);

	/* Held, then moved: it scrolls and clicks nothing. */
	reset(500.0);
	before = page_scroll;
	finger(SHELL_TOUCH_DOWN, 1, 150.0f, 400.0f);
	run(700.0);
	stroke(1, 150.0f, 400.0f, -150.0f, 200.0f, 0, 1);
	run(100.0);
	check(made_count == 0U && page_scroll > before + 100.0, "a long press that moves scrolls: %.0f (%u)", page_scroll, made_count);
}

/* Two fingers scroll by their centroid. */
static void
test_two_fingers(void)
{
	int k;

	/* Two fingers up 200 px together. */
	reset(1000.0);
	finger(SHELL_TOUCH_DOWN, 1, 300.0f, 500.0f);
	finger(SHELL_TOUCH_DOWN, 2, 500.0f, 500.0f);
	for (k = 1; k <= 12; k++) {
		clock_us += TEST_REPORT_US;
		finger(SHELL_TOUCH_MOTION, 1, 300.0f, 500.0f - 200.0f * (float)k / 12.0f);
		finger(SHELL_TOUCH_MOTION, 2, 500.0f, 500.0f - 200.0f * (float)k / 12.0f);
		frame_tick();
	}

	/* Held still, then lifted. */
	run(200.0);
	check(page_scroll > 1150.0 && page_scroll < 1220.0, "two fingers scroll by their centroid: %.0f", page_scroll);
	finger(SHELL_TOUCH_UP, 1, 0.0f, 0.0f);
	finger(SHELL_TOUCH_UP, 2, 0.0f, 0.0f);
	run(1000.0);
	check(made_count == 0U, "and click nothing");
}

/* A scroll set elsewhere, or another page, stops a glide; a finger drags on from it; a cancel leaves no glide. */
static void
test_elsewhere(void)
{
	double from;

	/* A glide, then a key puts the page elsewhere. */
	reset(0.0);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1);
	run(100.0);
	page_scroll = 2000.0;
	run(500.0);
	check(!touch.moving && page_scroll == 2000.0, "a scroll set elsewhere stops the glide: %.0f", page_scroll);

	/* A glide, then another page shown at the same scroll. */
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1);
	run(100.0);
	page_token++;
	from = page_scroll;
	run(500.0);
	check(!touch.moving && page_scroll == from, "another page stops the glide: %.0f (%.0f)", page_scroll, from);

	/* A finger down when the page is put elsewhere drags on from there. */
	reset(100.0);
	stroke(1, 400.0f, 500.0f, -100.0f, 100.0f, 1, 0);
	from = page_scroll;
	page_scroll = 1500.0;
	stroke(1, 400.0f, 400.0f, -100.0f, 100.0f, 0, 0);
	check(page_scroll > 1550.0 && page_scroll < 1650.0, "a finger drags on from a scroll set elsewhere: %.0f (was %.0f)", page_scroll, from);

	/* The compositor's cancel: no glide. */
	stroke(1, 400.0f, 400.0f, -300.0f, 100.0f, 0, 0);
	from = page_scroll;
	finger(SHELL_TOUCH_CANCEL, -1, 0.0f, 0.0f);
	run(1000.0);
	check(fabs(page_scroll - from) < 40.0 && !touch.moving, "a cancel leaves no glide: %.0f from %.0f", page_scroll, from);
}
