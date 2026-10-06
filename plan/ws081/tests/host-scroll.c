/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libkeiland's scroller and gestures (ws081-p005,
 * userland/desktop/libkeiland/scroll.c and gesture.c, plan/ws081/design.md
 * section 5).
 *
 * The scroller: the fling's closed form (stop time and distance), a
 * diagonal fling that keeps its direction, the slowest fling, the rubber
 * band while dragging, the spring back, a fling into a bound, a catch and
 * the fling after it, cancel, the axis lock, the bounds shrinking, and
 * positions that do not depend on how often the frames come.  The
 * gestures: tap, double tap, long press, a drag with its resampled offset
 * and its velocity at the lift, a second finger joining and leaving
 * without a jump, a pinch, and cancel.
 */

#include <keiland/keiland.h>

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A microsecond count the times start from, so that none is zero. */
#define TIME_BASE	1000000ULL

/* The number of checks run. */
static unsigned checks;

/* The number of checks that failed. */
static unsigned failures;

static void check(int passed, const char *format, ...);
static uint64_t at(double seconds);
static struct kl_scroller *page(double start);
static void fling(struct kl_scroller *scroller, double t, double vx, double vy);
static void test_fling(void);
static void test_diagonal(void);
static void test_edges(void);
static void test_catch(void);
static void test_misc(void);
static int gestures(struct kl_gesture *gesture, double t, struct kl_gesture_event *events, int capacity);
static void test_taps(void);
static void test_drag(void);
static void test_late_reader(void);

/*
 * Runs every test and reports the result.
 */
int
main(void)
{
	/* The scroller first, then the gestures. */
	test_fling();
	test_diagonal();
	test_edges();
	test_catch();
	test_misc();
	test_taps();
	test_drag();
	test_late_reader();

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-scroll: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-scroll: ok (%u checks)\n", checks);
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

/* Gives the microseconds of a time in seconds after the base. */
static uint64_t
at(
	double seconds)
{
	return TIME_BASE + (uint64_t)llround(seconds * 1.0e6);
}

/* A scroller over a long page: y from 0 to 100000, no x, a viewport of 800. */
static struct kl_scroller *
page(
	double start)
{
	struct kl_scroller *scroller;

	/* The page's bounds and the start. */
	scroller = kl_scroller_create();
	(void)kl_scroller_set_bounds(scroller, 0.0, 0.0, 0.0, 100000.0, 600.0, 800.0);
	kl_scroller_set_position(scroller, 0.0, start);
	return scroller;
}

/* Flings a scroller: a press, no drag, a release with the finger's velocity. */
static void
fling(
	struct kl_scroller *scroller,
	double t,
	double vx,
	double vy)
{
	(void)kl_scroller_press(scroller, at(t));
	kl_scroller_release(scroller, at(t), vx, vy);
}

/* The fling's closed form, its stop, and the slowest fling. */
static void
test_fling(void)
{
	struct kl_scroller *scroller;
	double x;
	double y;
	double expected;
	double stop;
	double t;
	int moving;

	/* A finger flicked up at 3000 px/s: the page glides down 1159 px and stops at 1.415 s. */
	scroller = page(10000.0);
	fling(scroller, 0.0, 0.0, -3000.0);
	stop = 0.45 * log(1.0 + 3000.0 / (300.0 * 0.45));
	for (t = 0.1; t < 1.4; t += 0.1) {
		(void)kl_scroller_step(scroller, at(t), &x, &y);
		expected = 10000.0 + (3000.0 + 135.0) * 0.45 * (1.0 - exp(-t / 0.45)) - 135.0 * t;
		check(fabs(y - expected) < 0.01, "fling at %.1f s: %.3f wanted %.3f", t, y, expected);
	}

	/* It stops where the closed form says. */
	moving = kl_scroller_step(scroller, at(stop + 0.01), &x, &y);
	expected = 10000.0 + 0.45 * 3000.0 - 300.0 * 0.45 * stop;
	check(!moving && fabs(y - expected) < 0.01, "fling stops at %.3f s at %.2f (%.2f), moving %d", stop, y, expected, moving);
	check(fabs(y - 10000.0 - 1159.0) < 1.0, "a 3000 px/s fling goes 1159 px: %.1f", y - 10000.0);
	kl_scroller_destroy(scroller);

	/* Slower than 300 px/s: no glide. */
	scroller = page(5000.0);
	fling(scroller, 0.0, 0.0, -299.0);
	moving = kl_scroller_step(scroller, at(0.2), &x, &y);
	check(!moving && y == 5000.0, "a 299 px/s release does not glide: %.2f moving %d", y, moving);
	kl_scroller_destroy(scroller);

	/* No faster than 8000 px/s. */
	scroller = page(10000.0);
	fling(scroller, 0.0, 0.0, -20000.0);
	(void)kl_scroller_step(scroller, at(5.0), &x, &y);
	expected = 0.45 * 8000.0 - 300.0 * 0.45 * 0.45 * log(1.0 + 8000.0 / 135.0);
	check(fabs(y - 10000.0 - expected) < 1.0, "a fling is capped at 8000 px/s: %.1f wanted %.1f", y - 10000.0, expected);
	kl_scroller_destroy(scroller);
}

/* A diagonal fling keeps its direction, both axes stopping together. */
static void
test_diagonal(void)
{
	struct kl_scroller *scroller;
	double x;
	double y;
	double t;
	int moving;
	int stopped_x;

	/* A two-way page, flung up and left at 2000 px/s. */
	scroller = kl_scroller_create();
	(void)kl_scroller_set_bounds(scroller, 0.0, 100000.0, 0.0, 100000.0, 800.0, 800.0);
	kl_scroller_set_position(scroller, 50000.0, 50000.0);
	fling(scroller, 0.0, -1200.0, -1600.0);
	stopped_x = 1;
	for (t = 0.05; t < 2.0; t += 0.05) {
		moving = kl_scroller_step(scroller, at(t), &x, &y);
		if (x - 50000.0 > 0.5)
			check(fabs((y - 50000.0) / (x - 50000.0) - 1600.0 / 1200.0) < 1e-6, "diagonal direction at %.2f s", t);
		if (!moving)
			break;
	}

	/* Both axes stop at once, as far as a straight fling of that speed. */
	check(!moving && stopped_x, "the diagonal fling stops");
	check(fabs(hypot(x - 50000.0, y - 50000.0) - (0.45 * 2000.0 - 135.0 * 0.45 * log(1.0 + 2000.0 / 135.0))) < 1.0,
	      "the diagonal fling goes as far as a 2000 px/s one: %.1f", hypot(x - 50000.0, y - 50000.0));
	kl_scroller_destroy(scroller);
}

/* The rubber band while dragging, the spring back, a fling into a bound. */
static void
test_edges(void)
{
	struct kl_scroller *scroller;
	double x;
	double y;
	double last;
	double peak;
	double t;
	int moving;
	int monotone;

	/* Dragged 100 px past the top of an 800 px viewport: 51.5 px shows. */
	scroller = page(0.0);
	(void)kl_scroller_press(scroller, at(0.0));
	kl_scroller_drag(scroller, 0.0, 100.0);
	(void)kl_scroller_step(scroller, at(0.01), &x, &y);
	check(fabs(y + 800.0 * 0.55 * 100.0 / (0.55 * 100.0 + 800.0)) < 1e-9, "rubber band of 100 px: %.3f", y);

	/* 400 px past: 172.5 px. */
	kl_scroller_drag(scroller, 0.0, 400.0);
	(void)kl_scroller_step(scroller, at(0.02), &x, &y);
	check(fabs(y + 172.54) < 0.01, "rubber band of 400 px: %.3f", y);

	/* Let go still: it springs back without passing the bound, within a second. */
	kl_scroller_release(scroller, at(0.02), 0.0, 0.0);
	monotone = 1;
	last = y;
	for (t = 0.03; t < 1.0; t += 0.005) {
		moving = kl_scroller_step(scroller, at(t), &x, &y);
		if (y < last - 1e-9 || y > 1e-9)
			monotone = 0;
		last = y;
		if (!moving)
			break;
	}

	/* The spring's result. */
	check(monotone, "the spring back never passes the bound");
	check(!moving && y == 0.0, "the spring back rests at the bound by %.2f s (%.3f)", t, y);
	kl_scroller_destroy(scroller);

	/* A fling into the top at 3000 px/s: it passes by a little and comes back. */
	scroller = page(300.0);
	fling(scroller, 0.0, 0.0, 3000.0);
	peak = 0.0;
	for (t = 0.005; t < 3.0; t += 0.005) {
		moving = kl_scroller_step(scroller, at(t), &x, &y);
		if (-y > peak)
			peak = -y;
		if (!moving)
			break;
	}

	/* The fling's result. */
	check(peak > 5.0 && peak < 60.0, "a fling into the bound passes it by %.1f px", peak);
	check(!moving && y == 0.0, "and rests at the bound (%.3f)", y);
	kl_scroller_destroy(scroller);

	/* A press during the spring holds what shows; a drag goes on from it through the band. */
	scroller = page(0.0);
	(void)kl_scroller_press(scroller, at(0.0));
	kl_scroller_drag(scroller, 0.0, 300.0);
	kl_scroller_release(scroller, at(0.0), 0.0, 0.0);
	(void)kl_scroller_step(scroller, at(0.05), &x, &peak);
	(void)kl_scroller_press(scroller, at(0.05));
	(void)kl_scroller_step(scroller, at(0.05), &x, &y);
	check(fabs(y - peak) < 1e-9, "a press in the spring holds %.3f (%.3f)", peak, y);
	kl_scroller_drag(scroller, 0.0, 10.0);
	(void)kl_scroller_step(scroller, at(0.06), &x, &last);
	check(last < y && y - last < 10.0 * 0.55, "and a drag goes on through the band (%.3f to %.3f)", y, last);
	kl_scroller_destroy(scroller);
}

/* A catch stops the glide; a fling soon after, the same way, adds the caught speed. */
static void
test_catch(void)
{
	struct kl_scroller *scroller;
	struct kl_scroller *plain;
	double x;
	double y;
	double y_plain;
	double before;
	int caught;

	/* A press during a glide catches it: the page stays where it was caught. */
	scroller = page(10000.0);
	fling(scroller, 0.0, 0.0, -3000.0);
	(void)kl_scroller_step(scroller, at(0.3), &x, &before);
	caught = kl_scroller_press(scroller, at(0.3));
	(void)kl_scroller_step(scroller, at(0.8), &x, &y);
	check(caught == 1 && y == before, "a press catches the glide (%d, %.2f against %.2f)", caught, y, before);

	/* A press at rest catches nothing. */
	caught = kl_scroller_press(scroller, at(0.9));
	check(caught == 0, "a press at rest catches nothing");
	kl_scroller_destroy(scroller);

	/* A fling within 400 ms of a catch, the same way, goes further than the same fling alone. */
	scroller = page(10000.0);
	fling(scroller, 0.0, 0.0, -3000.0);
	(void)kl_scroller_press(scroller, at(0.2));
	kl_scroller_release(scroller, at(0.35), 0.0, -2000.0);
	(void)kl_scroller_step(scroller, at(0.35), &x, &before);
	(void)kl_scroller_step(scroller, at(5.0), &x, &y);
	plain = page(before);
	fling(plain, 0.35, 0.0, -2000.0);
	(void)kl_scroller_step(plain, at(5.0), &x, &y_plain);
	check(y - before > y_plain - before + 100.0, "a fling after a catch adds speed: %.0f px against %.0f", y - before, y_plain - before);
	kl_scroller_destroy(plain);
	kl_scroller_destroy(scroller);

	/* The other way, it does not. */
	scroller = page(10000.0);
	fling(scroller, 0.0, 0.0, -3000.0);
	(void)kl_scroller_press(scroller, at(0.2));
	kl_scroller_release(scroller, at(0.35), 0.0, 2000.0);
	(void)kl_scroller_step(scroller, at(0.35), &x, &before);
	(void)kl_scroller_step(scroller, at(5.0), &x, &y);
	plain = page(before);
	fling(plain, 0.35, 0.0, 2000.0);
	(void)kl_scroller_step(plain, at(5.0), &x, &y_plain);
	check(fabs(y - y_plain) < 0.01, "a fling the other way adds nothing: %.2f against %.2f", y, y_plain);
	kl_scroller_destroy(plain);
	kl_scroller_destroy(scroller);

	/* After 400 ms, it does not either. */
	scroller = page(10000.0);
	fling(scroller, 0.0, 0.0, -3000.0);
	(void)kl_scroller_press(scroller, at(0.2));
	kl_scroller_release(scroller, at(0.65), 0.0, -2000.0);
	(void)kl_scroller_step(scroller, at(0.65), &x, &before);
	(void)kl_scroller_step(scroller, at(5.0), &x, &y);
	plain = page(before);
	fling(plain, 0.65, 0.0, -2000.0);
	(void)kl_scroller_step(plain, at(5.0), &x, &y_plain);
	check(fabs(y - y_plain) < 0.01, "a fling 450 ms after the catch adds nothing: %.2f against %.2f", y, y_plain);
	kl_scroller_destroy(plain);
	kl_scroller_destroy(scroller);
}

/* Cancel, the axis lock, the bounds shrinking, frames of any rate, bad bounds. */
static void
test_misc(void)
{
	struct kl_scroller *scroller;
	struct kl_scroller *other;
	double x;
	double y;
	double ox;
	double oy;
	double phase;
	double gap;
	double t;
	int moving;
	int error;
	int same;

	/* Cancel after a fast drag: no glide. */
	scroller = page(1000.0);
	(void)kl_scroller_press(scroller, at(0.0));
	kl_scroller_drag(scroller, 0.0, -200.0);
	kl_scroller_cancel(scroller, at(0.1));
	moving = kl_scroller_step(scroller, at(0.3), &x, &y);
	check(!moving && y == 1200.0, "cancel does not glide: %.2f moving %d", y, moving);
	kl_scroller_destroy(scroller);

	/* A drag 100 across and 20 down on a two-way scroller locks the vertical axis. */
	scroller = kl_scroller_create();
	(void)kl_scroller_set_bounds(scroller, 0.0, 10000.0, 0.0, 10000.0, 800.0, 800.0);
	kl_scroller_set_position(scroller, 5000.0, 5000.0);
	(void)kl_scroller_press(scroller, at(0.0));
	kl_scroller_drag(scroller, 50.0, 10.0);
	kl_scroller_drag(scroller, 100.0, 20.0);
	(void)kl_scroller_step(scroller, at(0.1), &x, &y);
	check(x == 4900.0 && y == 5000.0, "the drag locks the vertical axis: %.1f, %.1f", x, y);
	kl_scroller_release(scroller, at(0.1), 1000.0, 400.0);
	(void)kl_scroller_step(scroller, at(3.0), &x, &y);
	check(x < 4900.0 && y == 5000.0, "the locked fling moves only across: %.1f, %.1f", x, y);
	kl_scroller_destroy(scroller);

	/* The page shrinks under the position: it springs back to the new end. */
	scroller = page(5000.0);
	(void)kl_scroller_set_bounds(scroller, 0.0, 0.0, 0.0, 3000.0, 600.0, 800.0);
	moving = kl_scroller_step(scroller, at(0.0), &x, &y);
	check(moving, "a position left past the new end springs");
	(void)kl_scroller_step(scroller, at(3.0), &x, &y);
	check(y == 3000.0, "and rests at the new end: %.2f", y);
	kl_scroller_destroy(scroller);

	/* The same fling stepped every 4 ms or every 33 ms is at the same place at the same times. */
	scroller = page(10000.0);
	other = page(10000.0);
	fling(scroller, 0.0, 0.0, -2500.0);
	fling(other, 0.0, 0.0, -2500.0);
	same = 1;
	for (t = 0.004; t < 1.5; t += 0.004) {
		(void)kl_scroller_step(scroller, at(t), &x, &y);
		phase = fmod(t, 0.132);
		if (phase < 0.004) {
			(void)kl_scroller_step(other, at(t), &ox, &oy);
			gap = fabs(y - oy);
			if (gap > 1e-6)
				same = 0;
		}
	}

	/* The result of the two rates. */
	check(same, "the position does not depend on the frames' rate");
	kl_scroller_destroy(other);
	kl_scroller_destroy(scroller);

	/* Bounds the wrong way round and an empty viewport are refused. */
	scroller = kl_scroller_create();
	error = kl_scroller_set_bounds(scroller, 10.0, 0.0, 0.0, 0.0, 1.0, 1.0);
	check(error == EINVAL, "bounds the wrong way round are refused");
	error = kl_scroller_set_bounds(scroller, 0.0, 10.0, 0.0, 0.0, 0.0, 1.0);
	check(error == EINVAL, "an empty viewport is refused");
	kl_scroller_destroy(scroller);
}

/* Collects the gestures waiting at a time into kinds; returns how many. */
static int
gestures(
	struct kl_gesture *gesture,
	double t,
	struct kl_gesture_event *events,
	int capacity)
{
	int count;
	int more;

	/* Takes the gestures until none is left or the room is full. */
	count = 0;
	while (count < capacity) {
		more = kl_gesture_next(gesture, at(t), &events[count]);
		if (!more)
			break;
		count++;
	}

	/* The number taken. */
	return count;
}

/* Tap, double tap, long press, cancel. */
static void
test_taps(void)
{
	struct kl_gesture *gesture;
	struct kl_gesture_event events[8];
	int count;

	/* One recognizer for the whole sequence. */
	gesture = kl_gesture_create();

	/* A tap: down, a wobble of 3 px, up at 120 ms. */
	(void)kl_gesture_down(gesture, 1, at(0.0), at(0.002), 100.0, 200.0);
	(void)kl_gesture_motion(gesture, 1, at(0.05), at(0.052), 103.0, 200.0);
	(void)kl_gesture_up(gesture, 1, at(0.12));
	count = gestures(gesture, 0.13, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_TAP && events[0].x == 100.0, "a tap");

	/* A second tap 200 ms later nearby: a tap and a double tap. */
	(void)kl_gesture_down(gesture, 2, at(0.30), at(0.302), 106.0, 204.0);
	(void)kl_gesture_up(gesture, 2, at(0.32));
	count = gestures(gesture, 0.33, events, 8);
	check(count == 2 && events[0].kind == KL_GESTURE_TAP && events[1].kind == KL_GESTURE_DOUBLE_TAP, "a double tap (%d)", count);

	/* A third tap soon after: only a tap (the double tap used the last one). */
	(void)kl_gesture_down(gesture, 3, at(0.45), at(0.452), 106.0, 204.0);
	(void)kl_gesture_up(gesture, 3, at(0.47));
	count = gestures(gesture, 0.48, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_TAP, "a third tap is a tap (%d)", count);

	/* A tap far from the last is not a double tap. */
	(void)kl_gesture_down(gesture, 4, at(0.60), at(0.602), 300.0, 204.0);
	(void)kl_gesture_up(gesture, 4, at(0.62));
	count = gestures(gesture, 0.63, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_TAP, "a tap far away is a tap (%d)", count);

	/* A long press at 500 ms, and no tap at the lift. */
	(void)kl_gesture_down(gesture, 5, at(1.0), at(1.002), 50.0, 60.0);
	count = gestures(gesture, 1.4, events, 8);
	check(count == 0, "no long press at 400 ms");
	count = gestures(gesture, 1.51, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_LONG_PRESS && events[0].x == 50.0, "a long press at 500 ms");
	(void)kl_gesture_up(gesture, 5, at(1.7));
	count = gestures(gesture, 1.71, events, 8);
	check(count == 0, "no tap after a long press");

	/* A long press, then a drag: the drag says so. */
	(void)kl_gesture_down(gesture, 6, at(2.0), at(2.002), 50.0, 60.0);
	(void)gestures(gesture, 2.6, events, 8);
	(void)kl_gesture_motion(gesture, 6, at(2.7), at(2.702), 70.0, 60.0);
	count = gestures(gesture, 2.71, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_DRAG_BEGIN && events[0].after_long_press, "a drag after a long press");
	(void)kl_gesture_up(gesture, 6, at(2.8));
	(void)gestures(gesture, 2.81, events, 8);

	/* Cancel: no tap, a cancel. */
	(void)kl_gesture_down(gesture, 7, at(3.0), at(3.002), 50.0, 60.0);
	kl_gesture_cancel(gesture);
	count = gestures(gesture, 3.01, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_CANCEL, "cancel");
	check(kl_gesture_up(gesture, 7, at(3.1)) == ENOENT, "a finger cancelled is not down");

	/* Refusals: a finger down twice, a sixth finger, a finger not down. */
	(void)kl_gesture_down(gesture, 10, at(4.0), at(4.0), 0.0, 0.0);
	check(kl_gesture_down(gesture, 10, at(4.0), at(4.0), 0.0, 0.0) == EEXIST, "a finger down twice");
	(void)kl_gesture_down(gesture, 11, at(4.0), at(4.0), 0.0, 0.0);
	(void)kl_gesture_down(gesture, 12, at(4.0), at(4.0), 0.0, 0.0);
	(void)kl_gesture_down(gesture, 13, at(4.0), at(4.0), 0.0, 0.0);
	(void)kl_gesture_down(gesture, 14, at(4.0), at(4.0), 0.0, 0.0);
	check(kl_gesture_down(gesture, 15, at(4.0), at(4.0), 0.0, 0.0) == EBUSY, "a sixth finger");
	check(kl_gesture_motion(gesture, 99, at(4.0), at(4.0), 0.0, 0.0) == ENOENT, "a finger not down");
	kl_gesture_destroy(gesture);
}

/*
 * A drag at 60 Hz reports of 1500 px/s: DRAG_BEGIN after 8 px, an offset
 * resampled on the line, the velocity at the lift; a second finger joining
 * and leaving does not make the offset jump; a pinch.
 */
static void
test_drag(void)
{
	struct kl_gesture *gesture;
	struct kl_gesture_event events[8];
	double dx;
	double dy;
	double before;
	double scale;
	double cx;
	double cy;
	double t;
	int count;
	int error;
	int k;

	/* One recognizer for the whole drag. */
	gesture = kl_gesture_create();

	/* Down at 100,400; up the screen at 1500 px/s, a report every 16 ms. */
	(void)kl_gesture_down(gesture, 1, at(0.0), at(0.002), 100.0, 400.0);
	count = 0;
	for (k = 1; k <= 20; k++) {
		t = 0.016 * k;
		(void)kl_gesture_motion(gesture, 1, at(t), at(t + 0.002), 100.0, 400.0 - 1500.0 * t);
		count += gestures(gesture, t + 0.002, events + count, 8 - count);
	}

	/* The drag began once, from the press. */
	check(count == 1 && events[0].kind == KL_GESTURE_DRAG_BEGIN && events[0].x == 100.0 && events[0].y == 400.0,
	      "one DRAG_BEGIN, from where it pressed (%d)", count);

	/* The offset for a frame 10 ms after the last report is on the line (behind by 16 + 2 - 12 = 6 ms). */
	error = kl_gesture_drag_offset(gesture, at(0.32 + 0.010), &dx, &dy);
	check(error == 0 && fabs(dx) < 1e-6 && fabs(dy + 1500.0 * (0.32 + 0.004)) < 0.01, "the drag's offset %.3f,%.3f", dx, dy);

	/* A second finger joins at 300,400: the offset does not jump. */
	(void)kl_gesture_drag_offset(gesture, at(0.330), &dx, &before);
	(void)kl_gesture_down(gesture, 2, at(0.330), at(0.332), 300.0, 400.0);
	(void)kl_gesture_drag_offset(gesture, at(0.330), &dx, &dy);
	check(fabs(dy - before) < 0.5, "a finger joining does not move the drag: %.2f against %.2f", dy, before);

	/* A pinch starts at their distance (1:1), grows as they part. */
	error = kl_gesture_pinch(gesture, at(0.330), &scale, &cx, &cy);
	check(error == 0 && fabs(scale - 1.0) < 0.05, "a pinch starts at 1 (%.3f)", scale);
	for (k = 1; k <= 5; k++) {
		t = 0.330 + 0.016 * k;
		(void)kl_gesture_motion(gesture, 1, at(t), at(t + 0.002), 100.0 - 10.0 * k, 400.0 - 1500.0 * 0.32);
		(void)kl_gesture_motion(gesture, 2, at(t), at(t + 0.002), 300.0 + 10.0 * k, 400.0);
	}

	/* The pinch after the parting. */
	error = kl_gesture_pinch(gesture, at(0.330 + 0.080 + 0.1), &scale, &cx, &cy);
	check(error == 0 && scale > 1.0, "the pinch grows as the fingers part (%.3f)", scale);

	/* The first finger lifts: the offset does not jump. */
	(void)kl_gesture_drag_offset(gesture, at(0.42), &dx, &before);
	(void)kl_gesture_up(gesture, 1, at(0.42));
	(void)kl_gesture_drag_offset(gesture, at(0.42), &dx, &dy);
	check(fabs(dy - before) < 0.5, "a finger lifting does not move the drag: %.2f against %.2f", dy, before);
	check(kl_gesture_pinch(gesture, at(0.42), &scale, &cx, &cy) == ENOENT, "one finger does not pinch");

	/* The last finger flicks up and lifts: DRAG_END with its velocity. */
	for (k = 1; k <= 8; k++) {
		t = 0.42 + 0.016 * k;
		(void)kl_gesture_motion(gesture, 2, at(t), at(t + 0.002), 350.0, 400.0 - 2000.0 * 0.016 * k);
	}

	/* The lift. */
	(void)kl_gesture_up(gesture, 2, at(0.42 + 0.016 * 8 + 0.005));
	count = gestures(gesture, 0.6, events, 8);
	check(count == 1 && events[0].kind == KL_GESTURE_DRAG_END && fabs(events[0].vy + 2000.0) < 200.0 && fabs(events[0].vx) < 50.0,
	      "DRAG_END with the flick's velocity (%.0f, %.0f)", events[0].vx, events[0].vy);
	check(kl_gesture_drag_offset(gesture, at(0.6), &dx, &dy) == ENOENT, "no drag after the lift");
	kl_gesture_destroy(gesture);
}

/*
 * A program that reads the reports 100 ms late (busy drawing, ws081-p010):
 * after strokes that taught the device that delay, a finger that rests and
 * then moves up is never drawn moving down.
 */
static void
test_late_reader(void)
{
	struct kl_gesture *gesture;
	struct kl_gesture_event events[8];
	double dx;
	double dy;
	double worst;
	double t;
	int stroke_index;
	int error;
	int k;

	/* One recognizer; each stroke rests 100 ms, then moves up 150 px in 100 ms, read 100 ms late. */
	gesture = kl_gesture_create();
	worst = 0.0;
	for (stroke_index = 0; stroke_index < 6; stroke_index++) {
		t = 10.0 * stroke_index;
		(void)kl_gesture_down(gesture, 1, at(t), at(t + 0.1), 400.0, 500.0);
		for (k = 1; k <= 12; k++) {
			(void)kl_gesture_motion(gesture, 1, at(t + 0.0167 * k), at(t + 0.0167 * k + 0.1), 400.0,
						     k <= 6 ? 500.0 : 500.0 - 25.0 * (k - 6));
			(void)gestures(gesture, t + 0.0167 * k + 0.1, events, 8);

			/* The drag's offset at a frame drawn when the report was read. */
			error = kl_gesture_drag_offset(gesture, at(t + 0.0167 * k + 0.1), &dx, &dy);
			if (error == 0 && dy > worst)
				worst = dy;
		}
		(void)kl_gesture_up(gesture, 1, at(t + 0.3));
		(void)gestures(gesture, t + 0.4, events, 8);
	}

	/* The result. */
	check(worst <= 1.0, "a late reader's drag up is never drawn down: %.1f px", worst);
	kl_gesture_destroy(gesture);
}
