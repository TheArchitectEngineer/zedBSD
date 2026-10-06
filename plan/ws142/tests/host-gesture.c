/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the touch pad layer's gestures (ws142-p003,
 * userland/desktop/wayland/touchpad.c, compiled unchanged).
 *
 * Scripted multitouch reports of a pad of the Latitude 5330's size (1336
 * by 760 units, 12 a millimetre; its contacts reach the edges) check:
 * two fingers up from the bottom edge, right from the left edge and left
 * from the right edge are BOTTOM2, LEFT2 and RIGHT2, which begin, follow
 * the fingers' travel inward and end with their speed; fingers that land
 * in different reports are still a gesture; two fingers elsewhere, or
 * moving along an edge, scroll; three fingers moving up are UP3, moving
 * otherwise nothing; a tap of three fingers is TAP3 and pressing the pad
 * with three is the middle button (D1); a finger more gives the gesture
 * up, a finger less or the pad pressed ends or gives it up, and the
 * fingers left do nothing; without the pad's size there are no edges.
 * Two fingers down from the top edge are TOP2 (ws142-p009); a touch of two
 * fingers that scrolled ends with one SWIPE2 end, after its scrolling (when
 * one finger lifts, or both), and no other touch says one.
 *
 *   plan/ws142/tests/run-host-gesture.sh
 */

#include "touchpad.h"

#include <stdio.h>
#include <string.h>

/* The evdev codes the reports use. */
#define EV_KEY_TYPE		0x01U
#define EV_ABS_TYPE		0x03U
#define CODE_SLOT		0x2fU
#define CODE_X			0x35U
#define CODE_Y			0x36U
#define CODE_TRACKING		0x39U
#define CODE_BUTTON		0x110U

/* The 5330 pad: units per millimetre, and its largest place. */
#define RESOLUTION		12
#define PAD_X_MAX		1336
#define PAD_Y_MAX		760

/* The speed of a flick (micrometres a second). */
#define FLICK			100000

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

/* The pad, the fake clock and the next tracking identifier. */
static struct kwl_touchpad pad;
static uint64_t now_ms;
static int32_t next_tracking;

/* The actions of the whole of one case, gathered from every call. */
static struct kwl_touchpad_action seen[512];
static unsigned seen_count;

static void check(int condition, const char *what);
static void gather(const struct kwl_touchpad_actions *actions);
static void frame(void);
static void frame_after(uint64_t milliseconds);
static void finger_down(int32_t slot, int32_t x, int32_t y);
static void finger_move(int32_t slot, int32_t x, int32_t y);
static void finger_up(int32_t slot);
static void button(int32_t pressed);
static void start_case(int sized);
static void move_fingers(unsigned count, const int32_t *x, const int32_t *y, int32_t dx, int32_t dy, int steps, uint64_t interval);
static unsigned gesture_count(uint32_t gesture, uint32_t phase);
static unsigned gestures(void);
static unsigned kind_count(uint32_t kind);
static int32_t last_travel(uint32_t phase);
static int32_t last_speed(uint32_t phase);
static int travel_rises(void);
static int swipe_after_scroll(void);
static unsigned button_count(uint32_t code, uint32_t pressed);

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Keeps the actions one call gave. */
static void
gather(
	const struct kwl_touchpad_actions *actions)
{
	unsigned index;

	/* Each action after the ones already seen. */
	for (index = 0; index < actions->count; index++) {
		if (seen_count < sizeof(seen) / sizeof(seen[0])) {
			seen[seen_count] = actions->actions[index];
			seen_count++;
		}
	}
}

/* Ends a report, 8 ms after the last. */
static void
frame(void)
{
	/* The pad's usual interval. */
	frame_after(8U);
}

/* Ends a report some time after the last. */
static void
frame_after(
	uint64_t milliseconds)
{
	struct kwl_touchpad_actions actions;

	/* Time passes, then the report ends. */
	now_ms += milliseconds;
	kwl_touchpad_frame(&pad, now_ms, &actions);
	gather(&actions);
}

/* A finger touches at a place (in the pad's units). */
static void
finger_down(
	int32_t slot,
	int32_t x,
	int32_t y)
{
	/* Its slot, its new identifier and its place. */
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_SLOT, slot);
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_TRACKING, next_tracking);
	next_tracking++;
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_X, x);
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_Y, y);
}

/* A finger moves to a place. */
static void
finger_move(
	int32_t slot,
	int32_t x,
	int32_t y)
{
	/* Its slot and its place. */
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_SLOT, slot);
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_X, x);
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_Y, y);
}

/* A finger lifts. */
static void
finger_up(
	int32_t slot)
{
	/* Its slot ends its identifier. */
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_SLOT, slot);
	kwl_touchpad_event(&pad, EV_ABS_TYPE, CODE_TRACKING, -1);
}

/* The pad pressed or let go. */
static void
button(
	int32_t pressed)
{
	/* BTN_LEFT. */
	kwl_touchpad_event(&pad, EV_KEY_TYPE, CODE_BUTTON, pressed);
}

/* Starts a case on a fresh pad (of the 5330's size, or of no known size), long after any earlier one. */
static void
start_case(
	int sized)
{
	/* A pad with nothing on it, and no action seen. */
	memset(&pad, 0, sizeof(pad));
	kwl_touchpad_init(&pad, RESOLUTION, RESOLUTION);
	if (sized)
		kwl_touchpad_set_size(&pad, PAD_X_MAX, PAD_Y_MAX);
	now_ms += 10000U;
	seen_count = 0;
}

/* Moves the first count fingers (slots 0 up) from their places by dx, dy in steps reports, interval apart. */
static void
move_fingers(
	unsigned count,
	const int32_t *x,
	const int32_t *y,
	int32_t dx,
	int32_t dy,
	int steps,
	uint64_t interval)
{
	unsigned finger;
	int step;

	/* Each report a part of the way further. */
	for (step = 1; step <= steps; step++) {
		for (finger = 0; finger < count; finger++)
			finger_move((int32_t)finger, x[finger] + dx * step / steps, y[finger] + dy * step / steps);
		frame_after(interval);
	}
}

/* Counts a gesture's actions of one phase. */
static unsigned
gesture_count(
	uint32_t gesture,
	uint32_t phase)
{
	unsigned count;
	unsigned index;

	/* Every gesture action of that kind and phase. */
	count = 0;
	for (index = 0; index < seen_count; index++) {
		if (seen[index].kind == KWL_TOUCHPAD_GESTURE &&
		    seen[index].gesture == gesture &&
		    seen[index].phase == phase)
			count++;
	}

	/* The count. */
	return count;
}

/* Counts every gesture action but SWIPE2's end, which only says that a scrolling touch lifted. */
static unsigned
gestures(void)
{
	unsigned count;
	unsigned swipes;

	/* Of the gesture kind, less the swipes' ends. */
	count = kind_count(KWL_TOUCHPAD_GESTURE);
	swipes = gesture_count(KWL_TOUCHPAD_GESTURE_SWIPE2, KWL_TOUCHPAD_PHASE_END);

	/* Succeeded: the count. */
	return count - swipes;
}

/* Tells whether a swipe's end came after every scroll (none: no scroll either). */
static int
swipe_after_scroll(void)
{
	unsigned index;
	unsigned last_scroll;
	unsigned swipe;
	int scrolled;
	int found;

	/* The last scroll and the swipe's end. */
	last_scroll = 0;
	swipe = 0;
	scrolled = 0;
	found = 0;
	for (index = 0; index < seen_count; index++) {
		/* A scroll. */
		if (seen[index].kind == KWL_TOUCHPAD_SCROLL) {
			last_scroll = index;
			scrolled = 1;
		}

		/* The swipe's end. */
		if (seen[index].kind == KWL_TOUCHPAD_GESTURE && seen[index].gesture == KWL_TOUCHPAD_GESTURE_SWIPE2) {
			swipe = index;
			found = 1;
		}
	}

	/* Without a scroll there is nothing to be after. */
	if (!scrolled || !found)
		return 0;

	/* Succeeded: whether the end came last. */
	if (swipe > last_scroll)
		return 1;
	return 0;
}

/* Counts the actions of a kind. */
static unsigned
kind_count(
	uint32_t kind)
{
	unsigned count;
	unsigned index;

	/* Every action of the kind. */
	count = 0;
	for (index = 0; index < seen_count; index++) {
		if (seen[index].kind == kind)
			count++;
	}

	/* The count. */
	return count;
}

/* Gives the travel of the last gesture action of a phase (0 when there is none). */
static int32_t
last_travel(
	uint32_t phase)
{
	int32_t travel;
	unsigned index;

	/* The last one of the phase. */
	travel = 0;
	for (index = 0; index < seen_count; index++) {
		if (seen[index].kind == KWL_TOUCHPAD_GESTURE && seen[index].phase == phase)
			travel = seen[index].travel_um;
	}

	/* Its travel. */
	return travel;
}

/* Gives the speed of the last gesture action of a phase (0 when there is none). */
static int32_t
last_speed(
	uint32_t phase)
{
	int32_t speed;
	unsigned index;

	/* The last one of the phase. */
	speed = 0;
	for (index = 0; index < seen_count; index++) {
		if (seen[index].kind == KWL_TOUCHPAD_GESTURE && seen[index].phase == phase)
			speed = seen[index].speed;
	}

	/* Its speed. */
	return speed;
}

/* Tells whether the gesture's travel never falls from one action to the next. */
static int
travel_rises(void)
{
	int32_t before;
	unsigned index;

	/* Every gesture action against the one before. */
	before = 0;
	for (index = 0; index < seen_count; index++) {
		if (seen[index].kind != KWL_TOUCHPAD_GESTURE)
			continue;
		if (seen[index].travel_um < before)
			return 0;
		before = seen[index].travel_um;
	}

	/* It rose throughout. */
	return 1;
}

/* Counts the presses or releases of a button. */
static unsigned
button_count(
	uint32_t code,
	uint32_t pressed)
{
	unsigned count;
	unsigned index;

	/* Every button action of that button and state. */
	count = 0;
	for (index = 0; index < seen_count; index++) {
		if (seen[index].kind == KWL_TOUCHPAD_BUTTON &&
		    seen[index].button == code &&
		    seen[index].pressed == pressed)
			count++;
	}

	/* The count. */
	return count;
}

/* Runs every case. */
int
main(void)
{
	int32_t x[3];
	int32_t y[3];
	struct kwl_touchpad_actions released;
	int32_t travel;
	int burst;
	int step;

	/* 1. Two fingers up 20 mm from the bottom edge, quickly: BOTTOM2 begins, follows and ends fast. */
	start_case(1);
	x[0] = 500;
	y[0] = 750;
	x[1] = 700;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_BEGIN) == 1U, "bottom2: begins once");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_UPDATE) >= 5U, "bottom2: follows the fingers");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_END) == 1U, "bottom2: ends at the lift");
	travel = last_travel(KWL_TOUCHPAD_PHASE_END);
	check(travel >= 19000 && travel <= 21000, "bottom2: travels 20 mm up");
	check(travel_rises(), "bottom2: the travel rises");
	check(last_speed(KWL_TOUCHPAD_PHASE_END) >= FLICK, "bottom2: 250 mm/s is a flick");
	check(kind_count(KWL_TOUCHPAD_SCROLL) == 0U, "bottom2: does not scroll");
	check(kind_count(KWL_TOUCHPAD_BUTTON) == 0U, "bottom2: clicks nothing");

	/* 2. The same, slowly (2 mm every 80 ms): the end is no flick. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 80U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_END) == 1U, "slow bottom2: ends");
	check(last_speed(KWL_TOUCHPAD_PHASE_END) < FLICK, "slow bottom2: 25 mm/s is no flick");

	/* 3. The fingers land in different reports: still BOTTOM2. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	frame();
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_BEGIN) == 1U, "staggered bottom2: begins");
	check(kind_count(KWL_TOUCHPAD_SCROLL) == 0U, "staggered bottom2: does not scroll");

	/* 4. Two fingers up in the middle: a scroll, no gesture. */
	start_case(1);
	x[0] = 500;
	y[0] = 500;
	x[1] = 700;
	y[1] = 500;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "middle: no gesture");
	check(kind_count(KWL_TOUCHPAD_SCROLL) > 0U, "middle: scrolls");

	/* 5. Two fingers at the bottom edge moving across: a scroll, no gesture. */
	start_case(1);
	x[0] = 500;
	y[0] = 750;
	x[1] = 700;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 240, 0, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "along the bottom edge: no gesture");
	check(kind_count(KWL_TOUCHPAD_SCROLL) > 0U, "along the bottom edge: scrolls");

	/* 6. Only one finger in the bottom edge: a scroll. */
	start_case(1);
	y[1] = 500;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "one finger in the edge: no gesture");

	/* 7. Two fingers right 30 mm from the left edge: LEFT2. */
	start_case(1);
	x[0] = 30;
	y[0] = 300;
	x[1] = 40;
	y[1] = 450;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 360, 0, 12, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_LEFT2, KWL_TOUCHPAD_PHASE_BEGIN) == 1U, "left2: begins");
	travel = last_travel(KWL_TOUCHPAD_PHASE_END);
	check(travel >= 29000 && travel <= 31000, "left2: travels 30 mm inward");
	check(kind_count(KWL_TOUCHPAD_SCROLL) == 0U, "left2: does not scroll");

	/* 8. Two fingers left 30 mm from the right edge: RIGHT2, its travel inward positive. */
	start_case(1);
	x[0] = 1320;
	y[0] = 300;
	x[1] = 1300;
	y[1] = 450;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, -360, 0, 12, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_RIGHT2, KWL_TOUCHPAD_PHASE_BEGIN) == 1U, "right2: begins");
	travel = last_travel(KWL_TOUCHPAD_PHASE_END);
	check(travel >= 29000 && travel <= 31000, "right2: travels 30 mm inward");

	/* 9. From the left edge, but moving left (outward): no gesture. */
	start_case(1);
	x[0] = 60;
	x[1] = 65;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, -60, 0, 6, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "outward from the left edge: no gesture");

	/* 10. Three fingers up 15 mm anywhere: UP3, and no pointer motion. */
	start_case(1);
	x[0] = 400;
	y[0] = 500;
	x[1] = 550;
	y[1] = 480;
	x[2] = 700;
	y[2] = 500;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	finger_down(2, x[2], y[2]);
	frame();
	move_fingers(3U, x, y, 0, -180, 10, 8U);
	finger_up(0);
	finger_up(1);
	finger_up(2);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_UP3, KWL_TOUCHPAD_PHASE_BEGIN) == 1U, "up3: begins");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_UP3, KWL_TOUCHPAD_PHASE_END) == 1U, "up3: ends");
	travel = last_travel(KWL_TOUCHPAD_PHASE_END);
	check(travel >= 14000 && travel <= 16000, "up3: travels 15 mm");
	check(kind_count(KWL_TOUCHPAD_MOTION) == 0U, "up3: moves no pointer");

	/* 11. Three fingers across: nothing at all. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	finger_down(2, x[2], y[2]);
	frame();
	move_fingers(3U, x, y, 180, 0, 10, 8U);
	finger_up(0);
	finger_up(1);
	finger_up(2);
	frame();
	check(seen_count == 0U, "three fingers across: nothing");

	/* 12. A tap of three fingers: TAP3, not the middle button (D1). */
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	finger_down(2, x[2], y[2]);
	frame();
	finger_up(0);
	finger_up(1);
	finger_up(2);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_TAP3, KWL_TOUCHPAD_PHASE_END) == 1U, "tap3: the gesture");
	check(kind_count(KWL_TOUCHPAD_BUTTON) == 0U, "tap3: no button");

	/* 13. The pad pressed with three fingers: the middle button. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	finger_down(2, x[2], y[2]);
	frame();
	button(1);
	frame();
	button(0);
	frame();
	finger_up(0);
	finger_up(1);
	finger_up(2);
	frame();
	check(button_count(KWL_TOUCHPAD_BUTTON_MIDDLE, 1U) == 1U && button_count(KWL_TOUCHPAD_BUTTON_MIDDLE, 0U) == 1U, "three-finger press: the middle button");
	check(gestures() == 0U, "three-finger press: no gesture");

	/* 14. A two-finger tap in the bottom edge is still the right button. */
	start_case(1);
	finger_down(0, 500, 750);
	finger_down(1, 700, 745);
	frame();
	finger_up(0);
	finger_up(1);
	frame();
	check(button_count(KWL_TOUCHPAD_BUTTON_RIGHT, 1U) == 1U, "two-finger tap in the edge: the right button");
	check(gestures() == 0U, "two-finger tap in the edge: no gesture");

	/* 15. A third finger during BOTTOM2: given up, and the rest of the touch does nothing. */
	start_case(1);
	x[0] = 500;
	y[0] = 750;
	x[1] = 700;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -120, 5, 8U);
	y[0] -= 120;
	y[1] -= 120;
	x[2] = 900;
	y[2] = 500;
	finger_down(2, x[2], y[2]);
	frame();
	move_fingers(3U, x, y, 0, -180, 10, 8U);
	finger_up(0);
	finger_up(1);
	finger_up(2);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_CANCEL) == 1U, "a finger more: bottom2 given up");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_END) == 0U, "a finger more: no end");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_UP3, KWL_TOUCHPAD_PHASE_BEGIN) == 0U, "a finger more: no up3 after");
	check(kind_count(KWL_TOUCHPAD_SCROLL) == 0U && kind_count(KWL_TOUCHPAD_MOTION) == 0U, "a finger more: nothing after");

	/* 16. One finger lifted during BOTTOM2: it ends, and the finger left moves no pointer. */
	start_case(1);
	x[0] = 500;
	y[0] = 750;
	x[1] = 700;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	y[0] -= 240;
	finger_up(1);
	frame();
	move_fingers(1U, x, y, 120, 0, 5, 8U);
	finger_up(0);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_END) == 1U, "a finger less: bottom2 ends once");
	check(kind_count(KWL_TOUCHPAD_MOTION) == 0U, "a finger less: the finger left moves no pointer");
	check(kind_count(KWL_TOUCHPAD_BUTTON) == 0U, "a finger less: no tap");

	/* 17. The pad pressed during BOTTOM2: given up. */
	start_case(1);
	y[0] = 750;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -120, 5, 8U);
	button(1);
	frame();
	button(0);
	frame();
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_CANCEL) == 1U, "pressed: bottom2 given up");

	/* 18. The device going away during BOTTOM2: given up. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -120, 5, 8U);
	kwl_touchpad_release_all(&pad, &released);
	gather(&released);
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_CANCEL) == 1U, "released: bottom2 given up");

	/* 19. Without the pad's size there is no edge: two fingers at the bottom scroll. */
	start_case(0);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "no size: no gesture");
	check(kind_count(KWL_TOUCHPAD_SCROLL) > 0U, "no size: scrolls");

	/* 20. Slow fingers (2 mm every 60 ms) whose reports come in bursts of three every 180 ms: no flick (T1-126). */
	start_case(1);
	x[0] = 500;
	y[0] = 750;
	x[1] = 700;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	for (burst = 0; burst < 4; burst++) {
		for (step = 0; step < 3; step++) {
			y[0] -= 24;
			y[1] -= 24;
			finger_move(0, x[0], y[0]);
			finger_move(1, x[1], y[1]);
			frame_after((uint64_t)(step == 0) * 180U);
		}
	}

	/* Lifted. */
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_BOTTOM2, KWL_TOUCHPAD_PHASE_END) == 1U, "bursts: bottom2 ends");
	check(last_speed(KWL_TOUCHPAD_PHASE_END) < FLICK, "bursts: 33 mm/s read in bursts is no flick");
	check(last_speed(KWL_TOUCHPAD_PHASE_END) > 20000, "bursts: about 33 mm/s");

	/* 21. A quick swipe that stops 200 ms before the lift: no flick, with or without reports while still. */
	start_case(1);
	y[0] = 750;
	y[1] = 745;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	frame_after(200U);
	finger_up(0);
	finger_up(1);
	frame();
	check(last_speed(KWL_TOUCHPAD_PHASE_END) < FLICK, "stopped, no reports: no flick");
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -240, 10, 8U);
	y[0] -= 240;
	y[1] -= 240;
	for (step = 0; step < 25; step++) {
		finger_move(0, x[0], y[0]);
		finger_move(1, x[1], y[1]);
		frame();
	}

	/* Lifted. */
	finger_up(0);
	finger_up(1);
	frame();
	check(last_speed(KWL_TOUCHPAD_PHASE_END) < FLICK, "stopped, reports while still: no flick");

	/* 22. Two fingers down 20 mm from the top edge: TOP2 (ws142-p009, BUG-224), its travel down positive. */
	start_case(1);
	x[0] = 500;
	y[0] = 10;
	x[1] = 700;
	y[1] = 20;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, 240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_TOP2, KWL_TOUCHPAD_PHASE_BEGIN) == 1U, "top2: begins");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_TOP2, KWL_TOUCHPAD_PHASE_END) == 1U, "top2: ends");
	travel = last_travel(KWL_TOUCHPAD_PHASE_END);
	check(travel >= 19000 && travel <= 21000, "top2: travels 20 mm down");
	check(kind_count(KWL_TOUCHPAD_SCROLL) == 0U, "top2: does not scroll");
	check(gesture_count(KWL_TOUCHPAD_GESTURE_SWIPE2, KWL_TOUCHPAD_PHASE_END) == 0U, "top2: no swipe's end");

	/* 23. From the top edge, but moving up (outward), and one finger only in the top edge: no gesture. */
	start_case(1);
	y[0] = 60;
	y[1] = 65;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, -50, 5, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "outward from the top edge: no gesture");
	start_case(1);
	y[0] = 10;
	y[1] = 300;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 0, 240, 10, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gestures() == 0U, "one finger in the top edge: no gesture");

	/* 24. A scroll's touch ends with one swipe's end, after its scrolling. */
	start_case(1);
	x[0] = 500;
	y[0] = 400;
	x[1] = 700;
	y[1] = 400;
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 120, 0, 6, 8U);
	finger_up(0);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_SWIPE2, KWL_TOUCHPAD_PHASE_END) == 1U, "scroll: one swipe's end");
	check(swipe_after_scroll(), "scroll: the swipe's end after the scrolling");

	/* 25. One finger of a scroll lifting first: the swipe ends then, once. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	finger_down(1, x[1], y[1]);
	frame();
	move_fingers(2U, x, y, 120, 0, 6, 8U);
	finger_up(1);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_SWIPE2, KWL_TOUCHPAD_PHASE_END) == 1U, "a finger lifts: the swipe ends");
	finger_move(0, x[0] + 60, y[0]);
	frame();
	finger_up(0);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_SWIPE2, KWL_TOUCHPAD_PHASE_END) == 1U, "the last finger: no second end");

	/* 26. A touch that did not scroll (one finger moving, a gesture, a tap) says no swipe's end. */
	start_case(1);
	finger_down(0, x[0], y[0]);
	frame();
	move_fingers(1U, x, y, 120, 0, 6, 8U);
	finger_up(0);
	frame();
	check(gesture_count(KWL_TOUCHPAD_GESTURE_SWIPE2, KWL_TOUCHPAD_PHASE_END) == 0U, "one finger: no swipe's end");

	/* The result. */
	if (failures != 0) {
		printf("host-gesture: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-gesture: ok (%d checks)\n", checks);
	return 0;
}
