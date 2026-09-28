/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of Notes' touch screen (ws081-p013,
 * userland/desktop/notes/touch.c) on libkeiland, without Wayland and
 * Vulkan.
 *
 * An A4 page in a 1024x768 window under the 68-pixel toolbar, the test's
 * own clock, fingers at 60 Hz and a frame every 16 ms.  It checks: at the
 * zoom of 1 the page is where notes_view_layout puts it and a drag does not
 * move it; two fingers zoom about the place between them, at most as far
 * as the limits allow; a zoomed page follows a flick and glides on, and
 * stretches past its top and springs back; a double tap zooms in about the
 * tap and back; a tap on the toolbar waits for the main loop and a drag
 * there moves nothing; a finger near the pen, or just after it left, is a
 * palm and moves nothing, and the pen coming near cancels a drag and stops
 * a glide; another page starts at its top.
 */

#include "../../../userland/desktop/notes/touch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The window, the toolbar's band, the margin and the page (A4 in points). */
#define TEST_WIDTH	1024U
#define TEST_HEIGHT	768U
#define TEST_TOP	68.0f
#define TEST_MARGIN	16.0f
#define TEST_PAGE_W	595.0f
#define TEST_PAGE_H	842.0f

/* The finger's report interval and the frame interval, in microseconds. */
#define TEST_REPORT_US	16667U
#define TEST_FRAME_US	16000U

/* The touch screen under test. */
static struct notes_touch touch;

/* The test's clock, microseconds. */
static uint64_t clock_us;

/* The number of checks run. */
static unsigned checks;

/* The number of checks that failed. */
static unsigned failures;

static void check(int passed, const char *format, ...);
static float fit_scale(void);
static void layout(void);
static void frame_tick(void);
static void run(double milliseconds);
static void finger(unsigned type, int32_t id, float x, float y);
static void stroke(int32_t id, float x, float y, float dx, float dy, double milliseconds, int down, int up);
static void pinch(float x, float y, float from, float to, int steps);
static void rest(void);
static void test_whole_page(void);
static void test_pinch(void);
static void test_scroll(void);
static void test_double_tap(void);
static void test_toolbar(void);
static void test_palm(void);

/*
 * Runs every test and reports the result.
 */
int
main(void)
{
	int error;

	/* The touch screen at the whole page. */
	error = notes_touch_open(&touch);
	check(error == 0, "the touch screen opens");
	clock_us = 5000000U;
	layout();

	/* The tests. */
	test_whole_page();
	test_pinch();
	test_scroll();
	test_double_tap();
	test_toolbar();
	test_palm();
	notes_touch_close(&touch);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-notestouch: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-notestouch: ok (%u checks)\n", checks);
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

/* The scale at which the whole page fits, as notes_view_layout finds it. */
static float
fit_scale(void)
{
	float room_width;
	float room_height;
	float scale;

	/* The room below the toolbar, inside the margin, and the larger scale at which both sides fit. */
	room_width = (float)TEST_WIDTH - 2.0f * TEST_MARGIN;
	room_height = (float)TEST_HEIGHT - TEST_TOP - 2.0f * TEST_MARGIN;
	scale = room_width / TEST_PAGE_W;
	if (room_height / TEST_PAGE_H < scale)
		scale = room_height / TEST_PAGE_H;
	return scale;
}

/* Gives the touch screen the layout, as each frame does. */
static void
layout(void)
{
	/* The layout of the page in the window. */
	notes_touch_layout(&touch, TEST_WIDTH, TEST_HEIGHT, TEST_TOP, TEST_MARGIN, TEST_PAGE_W, TEST_PAGE_H, fit_scale());
}

/* Moves the clock on by a frame: the fingers' tick and the layout, as the main loop does. */
static void
frame_tick(void)
{
	/* The next frame. */
	clock_us += TEST_FRAME_US;
	(void)notes_touch_tick(&touch, clock_us);
	layout();
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
	struct notes_touch_event event;

	/* The input, read as it is made. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time = (uint32_t)(clock_us / 1000U);
	event.arrival = clock_us;
	notes_touch_event(&touch, &event);
}

/*
 * Moves a finger from a place by dx, dy over a time at 60 Hz, with frames
 * between reports; it touches first when down says so, and lifts at the
 * end when up says so.
 */
static void
stroke(
	int32_t id,
	float x,
	float y,
	float dx,
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
		finger(NOTES_TOUCH_DOWN, id, x, y);
	start = clock_us;

	/* The reports along the way, frames between them. */
	next = start + TEST_REPORT_US;
	while (next <= start + (uint64_t)(milliseconds * 1000.0)) {
		while (clock_us + TEST_FRAME_US <= next)
			frame_tick();
		clock_us = next;
		share = (float)((double)(next - start) / (milliseconds * 1000.0));
		finger(NOTES_TOUCH_MOTION, id, x + dx * share, y + dy * share);
		next += TEST_REPORT_US;
	}

	/* The lift, a few milliseconds after the last report. */
	if (up) {
		clock_us += 4000U;
		finger(NOTES_TOUCH_UP, id, 0.0f, 0.0f);
	}
}

/*
 * Two fingers either side of a place, from a distance to another in a
 * number of 60 Hz reports; they then hold still (reported as a touch
 * screen does) and lift.
 */
static void
pinch(
	float x,
	float y,
	float from,
	float to,
	int steps)
{
	float half;
	int reached;
	int k;

	/* Down either side. */
	finger(NOTES_TOUCH_DOWN, 1, x - from / 2.0f, y);
	finger(NOTES_TOUCH_DOWN, 2, x + from / 2.0f, y);

	/* Apart (or together), then still. */
	for (k = 1; k <= steps + 6; k++) {
		clock_us += TEST_REPORT_US;
		reached = k;
		if (reached > steps)
			reached = steps;
		half = (from + (to - from) * (float)reached / (float)steps) / 2.0f;
		finger(NOTES_TOUCH_MOTION, 1, x - half, y);
		finger(NOTES_TOUCH_MOTION, 2, x + half, y);
		frame_tick();
	}

	/* The lift. */
	clock_us += 4000U;
	finger(NOTES_TOUCH_UP, 1, 0.0f, 0.0f);
	finger(NOTES_TOUCH_UP, 2, 0.0f, 0.0f);
}

/* Lets everything rest. */
static void
rest(void)
{
	/* Long enough for any glide or spring. */
	run(3000.0);
}

/* At the whole page the place is notes_view_layout's, and a drag moves nothing. */
static void
test_whole_page(void)
{
	float x;
	float y;
	float scale;
	float want_x;
	float want_y;

	/* The whole page's place (geometry.c's formula). */
	scale = fit_scale();
	want_x = (float)floor(((double)TEST_WIDTH - (double)(TEST_PAGE_W * scale)) / 2.0);
	want_y = (float)floor((double)TEST_TOP + ((double)TEST_HEIGHT - (double)TEST_TOP - (double)(TEST_PAGE_H * scale)) / 2.0);
	notes_touch_view(&touch, &x, &y, &scale);
	check(x == want_x && y == want_y && scale == fit_scale(), "the whole page is where the layout puts it: %.1f,%.1f %.4f", (double)x,
	      (double)y, (double)scale);

	/* A drag at the whole page moves nothing. */
	stroke(1, 500.0f, 500.0f, 0.0f, -200.0f, 100.0f, 1, 1);
	rest();
	notes_touch_view(&touch, &x, &y, &scale);
	check(x == want_x && y == want_y, "a drag does not move the whole page: %.1f,%.1f", (double)x, (double)y);
}

/* Two fingers zoom about the place between them, within the limits. */
static void
test_pinch(void)
{
	float x;
	float y;
	float scale;
	float anchor_x;
	float anchor_y;
	float after_x;
	float after_y;

	/* The page's point between the fingers before. */
	notes_touch_view(&touch, &x, &y, &scale);
	anchor_x = (512.0f - x) / scale;
	anchor_y = (400.0f - y) / scale;

	/* 200 px apart to 400 px. */
	pinch(512.0f, 400.0f, 200.0f, 400.0f, 20);
	rest();
	notes_touch_view(&touch, &x, &y, &scale);
	after_x = (512.0f - x) / scale;
	after_y = (400.0f - y) / scale;
	check(touch.zoom > 1.75f && touch.zoom < 2.0f, "the fingers zoomed in: %.3f", (double)touch.zoom);
	check(fabsf(after_x - anchor_x) < 1.0f && fabsf(after_y - anchor_y) < 1.0f, "about the place between them: %.1f,%.1f then %.1f,%.1f",
	      (double)anchor_x, (double)anchor_y, (double)after_x, (double)after_y);
	check(!touch.zooming && !touch.pinching, "the zoom ended when they lifted");

	/* Far apart: no further than four times (the picture's limit, 4096 px, would allow 6.1 here), about the same place both ways. */
	notes_touch_view(&touch, &x, &y, &scale);
	anchor_x = (512.0f - x) / scale;
	anchor_y = (400.0f - y) / scale;
	pinch(512.0f, 400.0f, 100.0f, 900.0f, 30);
	rest();
	check(touch.zoom == 4.0f, "the zoom stops at four times: %.3f", (double)touch.zoom);
	notes_touch_view(&touch, &x, &y, &scale);
	after_x = (512.0f - x) / scale;
	after_y = (400.0f - y) / scale;
	check(fabsf(after_x - anchor_x) < 1.0f && fabsf(after_y - anchor_y) < 1.0f,
	      "about the place between the fingers across too: %.1f,%.1f then %.1f,%.1f", (double)anchor_x, (double)anchor_y,
	      (double)after_x, (double)after_y);

	/* Together again: never below the whole page. */
	pinch(512.0f, 400.0f, 800.0f, 50.0f, 30);
	rest();
	check(touch.zoom == 1.0f, "the zoom stops at the whole page: %.3f", (double)touch.zoom);

	/* Two fingers whose distance changes by 3% do not zoom. */
	pinch(512.0f, 400.0f, 300.0f, 309.0f, 10);
	rest();
	check(touch.zoom == 1.0f, "two fingers that hardly part do not zoom: %.3f", (double)touch.zoom);
}

/* A zoomed page follows a flick and glides; it stretches past its top and springs back. */
static void
test_scroll(void)
{
	float x;
	float y;
	float scale;
	float released;
	float later;
	float top_y;

	/* Twice the whole page, at its top. */
	pinch(512.0f, 300.0f, 200.0f, 420.0f, 20);
	rest();
	notes_touch_top(&touch);
	notes_touch_view(&touch, &x, &y, &scale);
	top_y = y;
	check(touch.zoom > 1.8f && fabsf(y - (TEST_TOP + TEST_MARGIN)) < 0.01f, "zoomed at the top: %.3f, %.1f", (double)touch.zoom, (double)y);

	/* A flick up: the page follows and glides on. */
	stroke(1, 500.0f, 600.0f, 0.0f, -300.0f, 100.0f, 1, 1);
	notes_touch_view(&touch, &x, &y, &scale);
	released = y;
	run(200.0);
	notes_touch_view(&touch, &x, &y, &scale);
	later = y;
	check(released < top_y - 200.0f && later < released - 150.0f, "the page follows the flick and glides on: %.1f, %.1f, %.1f",
	      (double)top_y, (double)released, (double)later);
	rest();
	check(!touch.moving, "the glide rests");

	/* At the top, pulled down: it stretches less than the finger and springs back. */
	notes_touch_top(&touch);
	stroke(1, 500.0f, 300.0f, 0.0f, 200.0f, 300.0f, 1, 0);
	run(50.0);
	notes_touch_view(&touch, &x, &y, &scale);
	check(y > top_y + 50.0f && y < top_y + 200.0f * 0.55f, "the top stretches: %.1f from %.1f", (double)y, (double)top_y);
	clock_us += 4000U;
	finger(NOTES_TOUCH_UP, 1, 0.0f, 0.0f);
	rest();
	notes_touch_view(&touch, &x, &y, &scale);
	check(y == top_y, "and springs back: %.1f", (double)y);
}

/* A double tap zooms back to the whole page, and in twice about the tap. */
static void
test_double_tap(void)
{
	float x;
	float y;
	float scale;
	float anchor_y;

	/* Zoomed now: a double tap goes back to the whole page. */
	finger(NOTES_TOUCH_DOWN, 3, 500.0f, 400.0f);
	clock_us += 60000U;
	finger(NOTES_TOUCH_UP, 3, 0.0f, 0.0f);
	run(120.0);
	finger(NOTES_TOUCH_DOWN, 4, 502.0f, 401.0f);
	clock_us += 60000U;
	finger(NOTES_TOUCH_UP, 4, 0.0f, 0.0f);
	run(300.0);
	check(touch.zoom == 1.0f, "a double tap goes back to the whole page: %.3f", (double)touch.zoom);

	/*
	 * Another zooms in twice, the tapped place staying under the finger down
	 * the page (across, a page twice as large still fits the window, and is
	 * centred).
	 */
	run(500.0);
	notes_touch_view(&touch, &x, &y, &scale);
	anchor_y = (450.0f - y) / scale;
	finger(NOTES_TOUCH_DOWN, 5, 400.0f, 450.0f);
	clock_us += 60000U;
	finger(NOTES_TOUCH_UP, 5, 0.0f, 0.0f);
	run(120.0);
	finger(NOTES_TOUCH_DOWN, 6, 400.0f, 450.0f);
	clock_us += 60000U;
	finger(NOTES_TOUCH_UP, 6, 0.0f, 0.0f);
	run(300.0);
	notes_touch_view(&touch, &x, &y, &scale);
	check(fabsf(touch.zoom - 2.0f) < 1e-5f, "a double tap zooms in twice: %.3f", (double)touch.zoom);
	check(fabsf((450.0f - y) / scale - anchor_y) < 1.0f, "about the tapped place: %.1f then %.1f", (double)anchor_y, (double)((450.0f - y) / scale));
	check(x == (float)floor(((double)TEST_WIDTH - (double)(TEST_PAGE_W * scale)) / 2.0), "centred across, where the page fits");
	rest();
}

/* A tap on the toolbar waits for the main loop; a drag there moves nothing. */
static void
test_toolbar(void)
{
	float x;
	float y;
	float scale;
	float tap_x;
	float tap_y;
	int taken;

	/* A tap on the toolbar. */
	finger(NOTES_TOUCH_DOWN, 7, 120.0f, 30.0f);
	clock_us += 70000U;
	finger(NOTES_TOUCH_UP, 7, 0.0f, 0.0f);
	run(50.0);
	taken = notes_touch_take_tap(&touch, &tap_x, &tap_y);
	check(taken == 1 && tap_x == 120.0f && tap_y == 30.0f, "a tap on the toolbar waits to be taken");
	taken = notes_touch_take_tap(&touch, &tap_x, &tap_y);
	check(taken == 0, "once");

	/* A tap on the page does not. */
	run(400.0);
	finger(NOTES_TOUCH_DOWN, 8, 500.0f, 500.0f);
	clock_us += 70000U;
	finger(NOTES_TOUCH_UP, 8, 0.0f, 0.0f);
	run(400.0);
	taken = notes_touch_take_tap(&touch, &tap_x, &tap_y);
	check(taken == 0, "a tap on the page is not the toolbar's");

	/* A drag that starts on the toolbar moves nothing. */
	notes_touch_view(&touch, &x, &y, &scale);
	stroke(9, 300.0f, 30.0f, 0.0f, 300.0f, 100.0f, 1, 1);
	rest();
	notes_touch_view(&touch, &tap_x, &tap_y, &scale);
	check(tap_x == x && tap_y == y, "a drag from the toolbar moves nothing");
}

/* A palm near the pen moves nothing; the pen cancels a drag and stops a glide. */
static void
test_palm(void)
{
	float x;
	float y;
	float scale;
	float after_x;
	float after_y;

	/* Zoomed (from the last test), at rest. */
	notes_touch_view(&touch, &x, &y, &scale);
	check(touch.zoom > 1.9f, "zoomed for the palm tests");

	/* The pen near: a finger's drag moves nothing. */
	notes_touch_pen(&touch, 1, clock_us);
	stroke(1, 500.0f, 600.0f, 0.0f, -300.0f, 100.0f, 1, 1);
	rest();
	notes_touch_view(&touch, &after_x, &after_y, &scale);
	check(after_x == x && after_y == y, "a palm near the pen moves nothing");

	/* The pen left 200 ms ago: still a palm. */
	notes_touch_pen(&touch, 0, clock_us);
	run(200.0);
	stroke(1, 500.0f, 600.0f, 0.0f, -300.0f, 100.0f, 1, 1);
	rest();
	notes_touch_view(&touch, &after_x, &after_y, &scale);
	check(after_x == x && after_y == y, "a palm just after the pen left moves nothing");

	/* Long after: a finger again. */
	stroke(1, 500.0f, 600.0f, 0.0f, -100.0f, 200.0f, 1, 1);
	rest();
	notes_touch_view(&touch, &after_x, &after_y, &scale);
	check(after_y < y - 50.0f, "a finger long after the pen left scrolls: %.1f from %.1f", (double)after_y, (double)y);

	/* A drag the pen interrupts: cancelled, no glide, and the finger's rest moves nothing. */
	notes_touch_top(&touch);
	notes_touch_view(&touch, &x, &y, &scale);
	stroke(1, 500.0f, 600.0f, 0.0f, -150.0f, 100.0f, 1, 0);
	notes_touch_view(&touch, &after_x, &after_y, &scale);
	notes_touch_pen(&touch, 1, clock_us);
	stroke(1, 500.0f, 450.0f, 0.0f, -150.0f, 100.0f, 0, 1);
	rest();
	notes_touch_view(&touch, &x, &y, &scale);
	check(y == after_y && !touch.moving, "the pen cancelled the drag where it was: %.1f (%.1f)", (double)y, (double)after_y);
	notes_touch_pen(&touch, 0, clock_us);
	run(1000.0);

	/* A glide the pen stops. */
	stroke(1, 500.0f, 600.0f, 0.0f, -300.0f, 100.0f, 1, 1);
	run(100.0);
	notes_touch_pen(&touch, 1, clock_us);
	notes_touch_view(&touch, &x, &y, &scale);
	run(500.0);
	notes_touch_view(&touch, &after_x, &after_y, &scale);
	check(!touch.moving && after_y == y, "the pen stopped the glide: %.1f then %.1f", (double)y, (double)after_y);
	notes_touch_pen(&touch, 0, clock_us);
	run(1000.0);

	/* Another page starts at its top. */
	notes_touch_top(&touch);
	notes_touch_view(&touch, &x, &y, &scale);
	check(fabsf(y - (TEST_TOP + TEST_MARGIN)) < 0.01f, "another page starts at its top: %.1f", (double)y);
}
