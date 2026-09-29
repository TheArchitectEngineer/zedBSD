/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of Files' touch screen (ws081-p010,
 * userland/desktop/files/touch.c) on libkeiland, without Wayland and
 * Vulkan.
 *
 * The items scroll 0..3000 pixels in a 600-pixel area and the sidebar
 * 0..200 in a 500-pixel one; the test's own clock, fingers at 60 Hz and a
 * frame every 16 ms; the test plays the main loop (it applies the scroll
 * the fingers set and gives the areas back each round).  It checks: away
 * from the scrolled areas a finger is the left button; on the items a
 * flick scrolls and glides, the top stretches and springs back; a tap is a
 * click and a caught glide taps nothing; a long press that lifts is the
 * right button's click, and one that moves holds the left button where it
 * pressed (the scroll stays); a cancel lets a held button go; the sidebar
 * scrolls on its own; a scroll set elsewhere, or another folder, stops a
 * glide.
 */

#include "../../../userland/desktop/files/touch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The finger's report and frame intervals, in microseconds. */
#define TEST_REPORT_US	16667U
#define TEST_FRAME_US	16000U

/* The touch screen under test. */
static struct fm_touch touch;

/* The scrolled areas as the main loop keeps them (FM_TOUCH_CONTENT and _SIDEBAR). */
static struct fm_touch_area areas[FM_TOUCH_AREAS];

/* Two folders, whose places are the tokens of the items' area. */
static int folders[2];

/* The pointer events the fingers made, as the main loop took them. */
static struct fm_touch_pointer made[128];

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
static void finger(unsigned type, int32_t id, float x, float y, unsigned area);
static void stroke(int32_t id, float x, float y, float dy, double milliseconds, int down, int up, unsigned area);
static void reset(int scroll);
static unsigned count_kind(unsigned kind, unsigned button);
static void test_pointer(void);
static void test_scroll(void);
static void test_taps(void);
static void test_long_press(void);
static void test_sidebar(void);
static void test_elsewhere(void);

/*
 * Runs every test and reports the result.
 */
int
main(void)
{
	int error;

	/* The areas and the touch screen. */
	areas[FM_TOUCH_CONTENT].token = &folders[0];
	areas[FM_TOUCH_CONTENT].largest = 3000;
	areas[FM_TOUCH_CONTENT].height = 600;
	areas[FM_TOUCH_SIDEBAR].token = &areas;
	areas[FM_TOUCH_SIDEBAR].largest = 200;
	areas[FM_TOUCH_SIDEBAR].height = 500;
	error = fm_touch_open(&touch);
	check(error == 0, "the touch screen opens");
	clock_us = 5000000U;
	round_trip();

	/* The tests. */
	test_pointer();
	test_scroll();
	test_taps();
	test_long_press();
	test_sidebar();
	test_elsewhere();
	fm_touch_close(&touch);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-filestouch: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-filestouch: ok (%u checks)\n", checks);
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

/* Plays one round of the main loop: the areas to the fingers, their scroll and pointer events back. */
static void
round_trip(void)
{
	struct fm_touch_pointer pointer;
	unsigned which;
	int scroll;
	int moved;
	int taken;

	/* The areas, time, and the scroll the fingers set. */
	fm_touch_layout(&touch, FM_TOUCH_CONTENT, &areas[FM_TOUCH_CONTENT]);
	fm_touch_layout(&touch, FM_TOUCH_SIDEBAR, &areas[FM_TOUCH_SIDEBAR]);
	(void)fm_touch_tick(&touch, clock_us);
	moved = fm_touch_scroll(&touch, &which, &scroll);
	if (moved)
		areas[which].scroll = scroll;

	/* The pointer events they made. */
	for (;;) {
		taken = fm_touch_take_pointer(&touch, &pointer);
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
	float y,
	unsigned area)
{
	struct fm_touch_event event;

	/* The input, read as it is made. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time = (uint32_t)(clock_us / 1000U);
	event.serial = 91U;
	event.area = area;
	event.arrival = clock_us;
	fm_touch_event(&touch, &event);
}

/*
 * Moves a finger from a place by dy over a time at 60 Hz, with frames
 * between reports; it touches first when down says so (over an area), and
 * lifts at the end when up says so.
 */
static void
stroke(
	int32_t id,
	float x,
	float y,
	float dy,
	double milliseconds,
	int down,
	int up,
	unsigned area)
{
	uint64_t start;
	uint64_t next;
	float share;

	/* The touch. */
	if (down)
		finger(FM_TOUCH_DOWN, id, x, y, area);
	start = clock_us;

	/* The reports along the way, frames between them. */
	next = start + TEST_REPORT_US;
	while (next <= start + (uint64_t)(milliseconds * 1000.0)) {
		while (clock_us + TEST_FRAME_US <= next)
			frame_tick();
		clock_us = next;
		share = (float)((double)(next - start) / (milliseconds * 1000.0));
		finger(FM_TOUCH_MOTION, id, x, y + dy * share, area);
		next += TEST_REPORT_US;
	}

	/* The lift, a few milliseconds after the last report. */
	if (up) {
		clock_us += 4000U;
		finger(FM_TOUCH_UP, id, 0.0f, 0.0f, area);
	}
}

/* Puts the items at a scroll, as the wheel would, and lets everything rest. */
static void
reset(
	int scroll)
{
	/* The scroll, taken over by the fingers next round. */
	areas[FM_TOUCH_CONTENT].scroll = scroll;
	run(3000.0);
	made_count = 0;
}

/* Counts the pointer events of a kind and a button taken so far. */
static unsigned
count_kind(
	unsigned kind,
	unsigned button)
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

/* Away from the scrolled areas a finger is the left button. */
static void
test_pointer(void)
{
	/* A finger on a button: down presses, a move moves, up releases where it was. */
	reset(0);
	stroke(1, 50.0f, 20.0f, 30.0f, 100.0f, 1, 1, FM_TOUCH_OTHER);
	run(100.0);
	check(made_count >= 4U && made[0].kind == FM_TOUCH_POINTER_MOTION && made[1].kind == FM_TOUCH_POINTER_PRESS &&
	      made[1].button == FM_TOUCH_LEFT && made[1].serial == 91U && made[made_count - 1U].kind == FM_TOUCH_POINTER_RELEASE &&
	      made[made_count - 1U].y == made[made_count - 2U].y && made[made_count - 1U].y > 40, "a finger away from the areas is the left button (%u events)", made_count);
	check(areas[FM_TOUCH_CONTENT].scroll == 0, "and scrolls nothing");
}

/* On the items a flick scrolls and glides; the top stretches and springs back. */
static void
test_scroll(void)
{
	int released;
	int later;

	/* A flick up by 300 px in 100 ms. */
	reset(500);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1, FM_TOUCH_CONTENT);
	released = areas[FM_TOUCH_CONTENT].scroll;
	run(200.0);
	later = areas[FM_TOUCH_CONTENT].scroll;
	check(released > 700 && later > released + 150, "a flick scrolls down and glides on: %d then %d", released, later);
	run(3000.0);
	check(fabs((double)(areas[FM_TOUCH_CONTENT].scroll - released) - 1159.0) < 1159.0 * 0.15, "it rests a 3000 px/s fling further: %d",
	      areas[FM_TOUCH_CONTENT].scroll - released);
	check(made_count == 0U, "a scroll makes no pointer events (%u)", made_count);

	/* At the top, pulled down: it stretches less than the finger and springs back. */
	reset(0);
	stroke(1, 400.0f, 200.0f, 200.0f, 300.0f, 1, 0, FM_TOUCH_CONTENT);
	run(50.0);
	check(areas[FM_TOUCH_CONTENT].scroll < -50 && areas[FM_TOUCH_CONTENT].scroll > -110, "the top stretches: %d",
	      areas[FM_TOUCH_CONTENT].scroll);
	clock_us += 4000U;
	finger(FM_TOUCH_UP, 1, 0.0f, 0.0f, FM_TOUCH_CONTENT);
	run(1500.0);
	check(areas[FM_TOUCH_CONTENT].scroll == 0, "and springs back: %d", areas[FM_TOUCH_CONTENT].scroll);
}

/* A tap is a click; a tap that caught a glide is not. */
static void
test_taps(void)
{
	/* A tap. */
	reset(300);
	finger(FM_TOUCH_DOWN, 1, 120.0f, 240.0f, FM_TOUCH_CONTENT);
	clock_us += 70000U;
	finger(FM_TOUCH_UP, 1, 0.0f, 0.0f, FM_TOUCH_CONTENT);
	run(50.0);
	check(made_count == 3U && made[1].kind == FM_TOUCH_POINTER_PRESS && made[2].kind == FM_TOUCH_POINTER_RELEASE && made[1].x == 120 &&
	      made[1].y == 240 && made[1].button == FM_TOUCH_LEFT, "a tap is a click there (%u)", made_count);

	/* A glide, and a tap that catches it. */
	reset(300);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1, FM_TOUCH_CONTENT);
	run(150.0);
	made_count = 0;
	finger(FM_TOUCH_DOWN, 2, 120.0f, 240.0f, FM_TOUCH_CONTENT);
	clock_us += 70000U;
	finger(FM_TOUCH_UP, 2, 0.0f, 0.0f, FM_TOUCH_CONTENT);
	run(500.0);
	check(touch.caught && made_count == 0U, "a tap that caught the glide clicks nothing (%u)", made_count);
}

/* A long press that lifts is the right button's click; one that moves holds the left button where it pressed. */
static void
test_long_press(void)
{
	int before;
	unsigned motions;

	/* A long press, lifted still: the right button's click at its place. */
	reset(300);
	finger(FM_TOUCH_DOWN, 1, 200.0f, 300.0f, FM_TOUCH_CONTENT);
	run(700.0);
	check(made_count == 0U, "a long press waits for the lift or a move");
	clock_us += 4000U;
	finger(FM_TOUCH_UP, 1, 0.0f, 0.0f, FM_TOUCH_CONTENT);
	run(50.0);
	check(count_kind(FM_TOUCH_POINTER_PRESS, FM_TOUCH_RIGHT) == 1U && count_kind(FM_TOUCH_POINTER_RELEASE, FM_TOUCH_RIGHT) == 1U &&
	      count_kind(FM_TOUCH_POINTER_PRESS, FM_TOUCH_LEFT) == 0U, "lifted, it is the right button's click");

	/* A long press, then a move: the left button pressed where it pressed, following the finger; the scroll stays. */
	reset(300);
	before = areas[FM_TOUCH_CONTENT].scroll;
	finger(FM_TOUCH_DOWN, 1, 200.0f, 300.0f, FM_TOUCH_CONTENT);
	run(700.0);
	stroke(1, 200.0f, 300.0f, 150.0f, 250.0f, 0, 1, FM_TOUCH_CONTENT);
	run(100.0);
	motions = count_kind(FM_TOUCH_POINTER_MOTION, FM_TOUCH_LEFT);
	check(made_count > 3U && made[1].kind == FM_TOUCH_POINTER_PRESS && made[1].button == FM_TOUCH_LEFT && made[1].x == 200 &&
	      made[1].y == 300 && motions >= 10U && made[made_count - 1U].kind == FM_TOUCH_POINTER_RELEASE,
	      "moved, the left button is held from the press and follows the finger (%u events, %u motions)", made_count, motions);
	check(count_kind(FM_TOUCH_POINTER_PRESS, FM_TOUCH_RIGHT) == 0U, "and no context menu");
	check(areas[FM_TOUCH_CONTENT].scroll == before, "the items did not scroll: %d", areas[FM_TOUCH_CONTENT].scroll);

	/* A held press the compositor takes (a drag and drop): the button is let go (the main loop drops it during the drag). */
	reset(300);
	finger(FM_TOUCH_DOWN, 1, 200.0f, 300.0f, FM_TOUCH_CONTENT);
	run(700.0);
	stroke(1, 200.0f, 300.0f, 100.0f, 150.0f, 0, 0, FM_TOUCH_CONTENT);
	round_trip();
	made_count = 0;
	finger(FM_TOUCH_CANCEL, -1, 0.0f, 0.0f, FM_TOUCH_OTHER);
	run(100.0);
	check(made_count == 1U && made[0].kind == FM_TOUCH_POINTER_RELEASE, "a cancel lets the held button go (%u)", made_count);
}

/* The sidebar scrolls on its own. */
static void
test_sidebar(void)
{
	int items;

	/* A drag up on the sidebar by 150 px. */
	reset(300);
	items = areas[FM_TOUCH_CONTENT].scroll;
	areas[FM_TOUCH_SIDEBAR].scroll = 0;
	stroke(1, 50.0f, 400.0f, -150.0f, 400.0f, 1, 1, FM_TOUCH_SIDEBAR);
	run(3000.0);
	check(areas[FM_TOUCH_SIDEBAR].scroll > 100 && areas[FM_TOUCH_SIDEBAR].scroll <= 200, "the sidebar scrolls: %d",
	      areas[FM_TOUCH_SIDEBAR].scroll);
	check(areas[FM_TOUCH_CONTENT].scroll == items, "and the items do not: %d", areas[FM_TOUCH_CONTENT].scroll);
}

/* A scroll set elsewhere, or another folder, stops a glide. */
static void
test_elsewhere(void)
{
	/* A glide, and the wheel moves the items. */
	reset(300);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1, FM_TOUCH_CONTENT);
	run(100.0);
	areas[FM_TOUCH_CONTENT].scroll = 1000;
	run(1000.0);
	check(!touch.moving && areas[FM_TOUCH_CONTENT].scroll == 1000, "the wheel stopped the glide: %d", areas[FM_TOUCH_CONTENT].scroll);

	/* A glide, and another folder at the same scroll. */
	reset(300);
	stroke(1, 400.0f, 500.0f, -300.0f, 100.0f, 1, 1, FM_TOUCH_CONTENT);
	run(100.0);
	areas[FM_TOUCH_CONTENT].token = &folders[1];
	run(1000.0);
	check(!touch.moving, "another folder stopped the glide");
}
