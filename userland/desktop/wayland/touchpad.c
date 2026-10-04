/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The touch pad layer (ws159-p004): a touch pad's fingers made into a
 * pointer.
 *
 * The kernel reports a touch pad as a multitouch device of protocol B with
 * BTN_LEFT for the pad pressed (a click pad), and makes no gesture of the
 * fingers (plan/ws159/phase001 D1).  This file makes them, as the
 * 2026-10-04 user's touch pad rules (BUG-166) say:
 *
 *   - one finger moving moves the pointer, faster the faster it moves;
 *   - a short touch that hardly moves is a tap: one finger the left
 *     button, two the right one, three the middle one;
 *   - a finger that touches again within TAP_DRAG_MS of a tap and moves
 *     drags with the left button held until it lifts; one that lifts again
 *     at once makes the second click of a double click;
 *   - the pad pressed is the left button (the right one with two fingers on
 *     the pad), and moving while it is pressed drags; the finger's first
 *     millimetre after the press is not motion, so the press itself does
 *     not move the pointer;
 *   - two fingers moving together scroll, by wheel notches, in the natural
 *     direction (the content follows the fingers) unless that is turned
 *     off.
 *
 * A tap's press is given when the finger lifts (only then is it a tap), and
 * its release when TAP_DRAG_MS have passed without another touch
 * (zwl_touchpad_tick): a press-to-act control reacts at the lift, a click
 * completes then.  The file knows nothing of the seat; input.c applies the
 * actions.  The evdev codes are written here as numbers: they are the same
 * on every operating system.
 */

#include "touchpad.h"

#include <stddef.h>
#include <string.h>

/* The evdev event types and codes a touch pad's reports use. */
#define TOUCHPAD_EV_KEY			0x01U
#define TOUCHPAD_EV_ABS			0x03U
#define TOUCHPAD_ABS_MT_SLOT		0x2fU
#define TOUCHPAD_ABS_MT_POSITION_X	0x35U
#define TOUCHPAD_ABS_MT_POSITION_Y	0x36U
#define TOUCHPAD_ABS_MT_TRACKING_ID	0x39U

/* A tap: at most this long and this far (micrometres), and the time a drag may start after it. */
#define TAP_MS				180U
#define TAP_TRAVEL_UM			3000
#define TAP_DRAG_MS			300U

/* The travel that makes a touch after a tap a drag, and the press's quiet travel (micrometres). */
#define DRAG_START_UM			1000
#define PRESS_QUIET_UM			1000

/* The finger travel of one wheel notch when two fingers scroll (micrometres). */
#define SCROLL_NOTCH_UM			2500

/*
 * The pointer's gain in pixels per millimetre, in 1/256: GAIN_SLOW up to
 * SPEED_SLOW millimetres per second, rising to GAIN_FAST at SPEED_FAST and
 * above.
 */
#define GAIN_SLOW			(5 * 256)
#define GAIN_FAST			(15 * 256)
#define SPEED_SLOW			20
#define SPEED_FAST			150

/* The time between reports the speed is measured over, at least and at most (milliseconds). */
#define FRAME_MS_LEAST			1U
#define FRAME_MS_MOST			50U

static unsigned active_fingers(const struct zwl_touchpad *pad);
static void take_button(struct zwl_touchpad *pad, unsigned fingers, struct zwl_touchpad_actions *actions);
static void touch_begin(struct zwl_touchpad *pad, uint64_t now_ms, unsigned fingers, struct zwl_touchpad_actions *actions);
static void take_motion(struct zwl_touchpad *pad, uint64_t now_ms, unsigned fingers, struct zwl_touchpad_actions *actions);
static void touch_end(struct zwl_touchpad *pad, uint64_t now_ms, struct zwl_touchpad_actions *actions);
static void pointer_motion(struct zwl_touchpad *pad, uint64_t now_ms, int64_t dx_um, int64_t dy_um, struct zwl_touchpad_actions *actions);
static void scroll(struct zwl_touchpad *pad, int64_t dx_um, int64_t dy_um, struct zwl_touchpad_actions *actions);
static void tap_finish(struct zwl_touchpad *pad, struct zwl_touchpad_actions *actions);
static void push_button(struct zwl_touchpad_actions *actions, uint32_t button, uint32_t pressed);
static void push_motion(struct zwl_touchpad_actions *actions, int32_t dx, int32_t dy);
static void push_scroll(struct zwl_touchpad_actions *actions, int32_t vertical, int32_t horizontal);
static int64_t magnitude(int64_t value);

/*
 * Starts a touch pad with no finger on it, of a resolution in units per
 * millimetre (a resolution below one is taken as one).
 */
void
zwl_touchpad_init(
	struct zwl_touchpad *pad,
	int32_t resolution_x,
	int32_t resolution_y)
{
	unsigned index;

	/* Nothing is known yet. */
	memset(pad, 0, sizeof(*pad));

	/* No slot holds a finger. */
	for (index = 0; index < ZWL_TOUCHPAD_SLOTS; index++)
		pad->fingers[index].tracking = -1;

	/* The units, at least one per millimetre. */
	pad->resolution_x = resolution_x;
	if (pad->resolution_x < 1)
		pad->resolution_x = 1;
	pad->resolution_y = resolution_y;
	if (pad->resolution_y < 1)
		pad->resolution_y = 1;

	/* Scrolling follows the fingers unless the user turns it off. */
	pad->natural_scroll = 1;
	pad->tap = ZWL_TOUCHPAD_TAP_NONE;
}

/*
 * Takes one event of a report: a slot chosen, a finger's tracking
 * identifier or place, or the pad's button.
 */
void
zwl_touchpad_event(
	struct zwl_touchpad *pad,
	uint16_t type,
	uint16_t code,
	int32_t value)
{
	struct zwl_touchpad_finger *finger;

	/* The pad pressed or let go. */
	if (type == TOUCHPAD_EV_KEY && code == ZWL_TOUCHPAD_BUTTON_LEFT) {
		pad->button_down = 0;
		if (value != 0)
			pad->button_down = 1;
		pad->button_changed = 1;
		return;
	}

	/* Every other event of a finger is an absolute one. */
	if (type != TOUCHPAD_EV_ABS)
		return;

	/* The slot the next events address. */
	if (code == TOUCHPAD_ABS_MT_SLOT) {
		pad->slot = value;
		return;
	}

	/* The finger of the slot addressed; a slot past the table is set aside. */
	if (pad->slot < 0 || (uint32_t)pad->slot >= ZWL_TOUCHPAD_SLOTS)
		return;
	finger = &pad->fingers[pad->slot];

	/* A finger comes (a new identifier) or goes (-1). */
	if (code == TOUCHPAD_ABS_MT_TRACKING_ID) {
		finger->tracking = value;
		finger->fresh = 0;
		if (value >= 0)
			finger->fresh = 1;
		return;
	}

	/* Its place across; a new finger starts where it is. */
	if (code == TOUCHPAD_ABS_MT_POSITION_X) {
		finger->x = value;
		if (finger->fresh)
			finger->last_x = value;
		return;
	}

	/* Its place down. */
	if (code == TOUCHPAD_ABS_MT_POSITION_Y) {
		finger->y = value;
		if (finger->fresh)
			finger->last_y = value;
	}
}

/*
 * Ends a report: the button, the touch's beginning, the motion or the
 * scrolling, and the touch's end (a tap) become actions.
 */
void
zwl_touchpad_frame(
	struct zwl_touchpad *pad,
	uint64_t now_ms,
	struct zwl_touchpad_actions *actions)
{
	unsigned fingers;
	unsigned index;

	/* Nothing to do yet; the fingers now on the pad. */
	actions->count = 0;
	fingers = active_fingers(pad);

	/* The pad pressed or let go. */
	if (pad->button_changed)
		take_button(pad, fingers, actions);

	/* A touch begins when the first finger comes. */
	if (pad->fingers_before == 0U && fingers != 0U)
		touch_begin(pad, now_ms, fingers, actions);

	/* The most fingers the touch had. */
	if (fingers > pad->touch_fingers)
		pad->touch_fingers = fingers;

	/* The fingers' motion moves the pointer or scrolls. */
	if (fingers != 0U)
		take_motion(pad, now_ms, fingers, actions);

	/* A touch ends when the last finger goes: perhaps a tap. */
	if (pad->fingers_before != 0U && fingers == 0U)
		touch_end(pad, now_ms, actions);

	/* The places at this report's end are where the next report's motion is measured from. */
	for (index = 0; index < ZWL_TOUCHPAD_SLOTS; index++) {
		pad->fingers[index].last_x = pad->fingers[index].x;
		pad->fingers[index].last_y = pad->fingers[index].y;
		pad->fingers[index].fresh = 0;
	}

	/* Succeeded: the next report starts from these fingers. */
	pad->fingers_before = fingers;
	pad->last_frame_ms = now_ms;
}

/*
 * Lets time pass: a tap whose drag did not come in time completes its
 * click.
 */
void
zwl_touchpad_tick(
	struct zwl_touchpad *pad,
	uint64_t now_ms,
	struct zwl_touchpad_actions *actions)
{
	/* Nothing to do yet. */
	actions->count = 0;

	/* A tap waiting for a drag that did not come releases its button. */
	if (pad->tap == ZWL_TOUCHPAD_TAP_PENDING && now_ms >= pad->tap_deadline_ms)
		tap_finish(pad, actions);
}

/*
 * Lets every button the pad holds go (the device is going away).
 */
void
zwl_touchpad_release_all(
	struct zwl_touchpad *pad,
	struct zwl_touchpad_actions *actions)
{
	/* Nothing to do yet. */
	actions->count = 0;

	/* A tap's or a tap drag's left button. */
	if (pad->tap != ZWL_TOUCHPAD_TAP_NONE)
		push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 0U);
	pad->tap = ZWL_TOUCHPAD_TAP_NONE;

	/* The pad's own press. */
	if (pad->button_sent != 0U)
		push_button(actions, pad->button_sent, 0U);
	pad->button_sent = 0;
}

/* Counts the fingers on the pad. */
static unsigned
active_fingers(
	const struct zwl_touchpad *pad)
{
	unsigned count;
	unsigned index;

	/* Every slot with a tracking identifier holds a finger. */
	count = 0;
	for (index = 0; index < ZWL_TOUCHPAD_SLOTS; index++) {
		if (pad->fingers[index].tracking >= 0)
			count++;
	}

	/* Succeeded: the number of fingers. */
	return count;
}

/*
 * Takes the pad pressed (the left button, the right one with two fingers
 * on the pad, the middle one with three) or let go (the button the press
 * was given as).
 */
static void
take_button(
	struct zwl_touchpad *pad,
	unsigned fingers,
	struct zwl_touchpad_actions *actions)
{
	uint32_t button;

	/* The change is taken. */
	pad->button_changed = 0;

	/* A press: a tap waiting for its drag is done first. */
	if (pad->button_down && pad->button_sent == 0U) {
		tap_finish(pad, actions);

		/* The button of the fingers on the pad. */
		button = ZWL_TOUCHPAD_BUTTON_LEFT;
		if (fingers == 2U) {
			button = ZWL_TOUCHPAD_BUTTON_RIGHT;
		} else if (fingers >= 3U) {
			button = ZWL_TOUCHPAD_BUTTON_MIDDLE;
		}

		/* Presses it; this touch is no tap, and the press's own jolt is no motion. */
		push_button(actions, button, 1U);
		pad->button_sent = button;
		pad->touch_clicked = 1;
		pad->press_quiet = 1;
		pad->touch_travel_um = 0;
		return;
	}

	/* A release lets the pressed button go. */
	if (!pad->button_down && pad->button_sent != 0U) {
		push_button(actions, pad->button_sent, 0U);
		pad->button_sent = 0;
		pad->press_quiet = 0;
	}
}

/*
 * Begins a touch: its time and fingers; a touch within a tap's drag time
 * may become a drag or the second tap of a double click.
 */
static void
touch_begin(
	struct zwl_touchpad *pad,
	uint64_t now_ms,
	unsigned fingers,
	struct zwl_touchpad_actions *actions)
{
	/* A tap whose time ran out completes before this touch counts. */
	if (pad->tap == ZWL_TOUCHPAD_TAP_PENDING && now_ms >= pad->tap_deadline_ms)
		tap_finish(pad, actions);

	/* One finger within the time: the tap's button stays held for a drag or a second tap. */
	if (pad->tap == ZWL_TOUCHPAD_TAP_PENDING) {
		if (fingers == 1U) {
			pad->tap = ZWL_TOUCHPAD_TAP_SECOND;
		} else {
			tap_finish(pad, actions);
		}
	}

	/* The touch: when it began, its fingers, no travel yet, and whether the pad is pressed. */
	pad->touch_start_ms = now_ms;
	pad->touch_fingers = fingers;
	pad->touch_travel_um = 0;
	pad->touch_clicked = 0;
	if (pad->button_sent != 0U)
		pad->touch_clicked = 1;
	pad->scroll_travel_x_um = 0;
	pad->scroll_travel_y_um = 0;
}

/*
 * Takes the fingers' motion since the last report: with one finger, the
 * pad pressed or a tap drag, the finger that moved most moves the pointer;
 * with two fingers and nothing pressed, they scroll.
 */
static void
take_motion(
	struct zwl_touchpad *pad,
	uint64_t now_ms,
	unsigned fingers,
	struct zwl_touchpad_actions *actions)
{
	const struct zwl_touchpad_finger *finger;
	int64_t dx_um;
	int64_t dy_um;
	int64_t best_dx_um;
	int64_t best_dy_um;
	int64_t sum_dx_um;
	int64_t sum_dy_um;
	int64_t best;
	int64_t size;
	unsigned moving;
	unsigned index;

	/* Measures each finger that was on the pad at the last report too. */
	best_dx_um = 0;
	best_dy_um = 0;
	best = 0;
	sum_dx_um = 0;
	sum_dy_um = 0;
	moving = 0;
	for (index = 0; index < ZWL_TOUCHPAD_SLOTS; index++) {
		/* A slot without a finger, or with one that came in this report, has no motion. */
		finger = &pad->fingers[index];
		if (finger->tracking < 0 || finger->fresh)
			continue;

		/* Its motion in micrometres. */
		dx_um = (int64_t)(finger->x - finger->last_x) * 1000 / pad->resolution_x;
		dy_um = (int64_t)(finger->y - finger->last_y) * 1000 / pad->resolution_y;
		sum_dx_um += dx_um;
		sum_dy_um += dy_um;
		moving++;

		/* The finger that moved most. */
		size = magnitude(dx_um) + magnitude(dy_um);
		if (size > best) {
			best = size;
			best_dx_um = dx_um;
			best_dy_um = dy_um;
		}
	}

	/* No finger to measure (every one came in this report). */
	if (moving == 0U)
		return;

	/* The touch's travel, which decides a tap and a drag. */
	pad->touch_travel_um += best;

	/* A touch after a tap that moves far enough is a drag. */
	if (pad->tap == ZWL_TOUCHPAD_TAP_SECOND && pad->touch_travel_um >= DRAG_START_UM)
		pad->tap = ZWL_TOUCHPAD_TAP_DRAG;

	/* Two fingers with nothing pressed scroll by their mean motion. */
	if (fingers == 2U && pad->button_sent == 0U && pad->tap == ZWL_TOUCHPAD_TAP_NONE) {
		scroll(pad, sum_dx_um / (int64_t)moving, sum_dy_um / (int64_t)moving, actions);
		return;
	}

	/* The press's first millimetre is not motion. */
	if (pad->press_quiet) {
		if (pad->touch_travel_um < PRESS_QUIET_UM)
			return;
		pad->press_quiet = 0;
	}

	/* Otherwise the finger that moved most moves the pointer. */
	pointer_motion(pad, now_ms, best_dx_um, best_dy_um, actions);
}

/*
 * Ends a touch: a tap drag lets its button go; a second quick tap makes
 * the second click of a double click; a tap of one finger presses the left
 * button until its drag time is over; a tap of two or three fingers clicks
 * the right or the middle button.
 */
static void
touch_end(
	struct zwl_touchpad *pad,
	uint64_t now_ms,
	struct zwl_touchpad_actions *actions)
{
	uint64_t duration;
	unsigned tapped;

	/* Whether the touch was a tap: short, hardly moving, the pad not pressed. */
	duration = now_ms - pad->touch_start_ms;
	tapped = 0;
	if (duration <= TAP_MS && pad->touch_travel_um < TAP_TRAVEL_UM && !pad->touch_clicked)
		tapped = 1;

	/* The scrolling's remainder goes with the touch. */
	pad->scroll_travel_x_um = 0;
	pad->scroll_travel_y_um = 0;

	/* A tap drag ends with its finger. */
	if (pad->tap == ZWL_TOUCHPAD_TAP_DRAG) {
		push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 0U);
		pad->tap = ZWL_TOUCHPAD_TAP_NONE;
		return;
	}

	/* A touch after a tap: the first click ends; a quick one is the second click. */
	if (pad->tap == ZWL_TOUCHPAD_TAP_SECOND) {
		push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 0U);
		pad->tap = ZWL_TOUCHPAD_TAP_NONE;

		/* A quick second touch is the second click. */
		if (tapped) {
			push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 1U);
			push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 0U);
		}

		/* The touch after the tap is done. */
		return;
	}

	/* A touch that was no tap ends here. */
	if (!tapped)
		return;

	/* A tap of one finger: the left button, held for the drag time. */
	if (pad->touch_fingers == 1U) {
		push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 1U);
		pad->tap = ZWL_TOUCHPAD_TAP_PENDING;
		pad->tap_deadline_ms = now_ms + TAP_DRAG_MS;
		return;
	}

	/* A tap of two fingers clicks the right button, of three or more the middle one. */
	if (pad->touch_fingers == 2U) {
		push_button(actions, ZWL_TOUCHPAD_BUTTON_RIGHT, 1U);
		push_button(actions, ZWL_TOUCHPAD_BUTTON_RIGHT, 0U);
	} else {
		push_button(actions, ZWL_TOUCHPAD_BUTTON_MIDDLE, 1U);
		push_button(actions, ZWL_TOUCHPAD_BUTTON_MIDDLE, 0U);
	}
}

/*
 * Moves the pointer by a finger's motion: micrometres to pixels at a gain
 * that grows with the finger's speed, the fractions kept for the next
 * report.
 */
static void
pointer_motion(
	struct zwl_touchpad *pad,
	uint64_t now_ms,
	int64_t dx_um,
	int64_t dy_um,
	struct zwl_touchpad_actions *actions)
{
	uint64_t elapsed;
	int64_t speed;
	int64_t gain;
	int64_t x;
	int64_t y;

	/* The time since the last report, within reason. */
	elapsed = now_ms - pad->last_frame_ms;
	if (elapsed < FRAME_MS_LEAST)
		elapsed = FRAME_MS_LEAST;
	if (elapsed > FRAME_MS_MOST)
		elapsed = FRAME_MS_MOST;

	/* The finger's speed in millimetres per second (micrometres per millisecond). */
	speed = (magnitude(dx_um) + magnitude(dy_um)) / (int64_t)elapsed;

	/* The gain: slow, fast, or between them in proportion. */
	gain = GAIN_SLOW;
	if (speed >= SPEED_FAST) {
		gain = GAIN_FAST;
	} else if (speed > SPEED_SLOW) {
		gain = GAIN_SLOW + (GAIN_FAST - GAIN_SLOW) * (speed - SPEED_SLOW) / (SPEED_FAST - SPEED_SLOW);
	}

	/* The motion in 1/256 pixels, with what the last reports left over. */
	x = dx_um * gain / 1000 + pad->motion_remainder_x;
	y = dy_um * gain / 1000 + pad->motion_remainder_y;

	/* Whole pixels move the pointer; the fractions wait. */
	pad->motion_remainder_x = x % 256;
	pad->motion_remainder_y = y % 256;
	x /= 256;
	y /= 256;
	if (x != 0 || y != 0)
		push_motion(actions, (int32_t)x, (int32_t)y);
}

/*
 * Scrolls by two fingers' motion: one notch for every SCROLL_NOTCH_UM, the
 * content following the fingers when the scrolling is natural.
 */
static void
scroll(
	struct zwl_touchpad *pad,
	int64_t dx_um,
	int64_t dy_um,
	struct zwl_touchpad_actions *actions)
{
	int64_t vertical;
	int64_t horizontal;

	/* The fingers' travel, with what earlier reports left over. */
	pad->scroll_travel_x_um += dx_um;
	pad->scroll_travel_y_um += dy_um;

	/* Whole notches; the rest waits. */
	vertical = pad->scroll_travel_y_um / SCROLL_NOTCH_UM;
	horizontal = pad->scroll_travel_x_um / SCROLL_NOTCH_UM;
	pad->scroll_travel_y_um -= vertical * SCROLL_NOTCH_UM;
	pad->scroll_travel_x_um -= horizontal * SCROLL_NOTCH_UM;

	/* Natural scrolling moves the content with the fingers: the other way from the fingers' direction. */
	if (pad->natural_scroll) {
		vertical = -vertical;
		horizontal = -horizontal;
	}

	/* The notches, when there are any. */
	if (vertical != 0 || horizontal != 0)
		push_scroll(actions, (int32_t)vertical, (int32_t)horizontal);
}

/* Completes a tap's click: its left button goes. */
static void
tap_finish(
	struct zwl_touchpad *pad,
	struct zwl_touchpad_actions *actions)
{
	/* Only a tap waiting for its drag holds the button here. */
	if (pad->tap != ZWL_TOUCHPAD_TAP_PENDING)
		return;

	/* The click completes. */
	push_button(actions, ZWL_TOUCHPAD_BUTTON_LEFT, 0U);
	pad->tap = ZWL_TOUCHPAD_TAP_NONE;
}

/* Adds a button's press or release to the actions (a full list keeps the first ones). */
static void
push_button(
	struct zwl_touchpad_actions *actions,
	uint32_t button,
	uint32_t pressed)
{
	struct zwl_touchpad_action *action;

	/* A full list takes no more. */
	if (actions->count >= ZWL_TOUCHPAD_ACTIONS)
		return;

	/* The button. */
	action = &actions->actions[actions->count];
	memset(action, 0, sizeof(*action));
	action->kind = ZWL_TOUCHPAD_BUTTON;
	action->button = button;
	action->pressed = pressed;
	actions->count++;
}

/* Adds a motion to the actions. */
static void
push_motion(
	struct zwl_touchpad_actions *actions,
	int32_t dx,
	int32_t dy)
{
	struct zwl_touchpad_action *action;

	/* A full list takes no more. */
	if (actions->count >= ZWL_TOUCHPAD_ACTIONS)
		return;

	/* The motion. */
	action = &actions->actions[actions->count];
	memset(action, 0, sizeof(*action));
	action->kind = ZWL_TOUCHPAD_MOTION;
	action->dx = dx;
	action->dy = dy;
	actions->count++;
}

/* Adds scrolling to the actions. */
static void
push_scroll(
	struct zwl_touchpad_actions *actions,
	int32_t vertical,
	int32_t horizontal)
{
	struct zwl_touchpad_action *action;

	/* A full list takes no more. */
	if (actions->count >= ZWL_TOUCHPAD_ACTIONS)
		return;

	/* The scrolling. */
	action = &actions->actions[actions->count];
	memset(action, 0, sizeof(*action));
	action->kind = ZWL_TOUCHPAD_SCROLL;
	action->vertical = vertical;
	action->horizontal = horizontal;
	actions->count++;
}

/* Gives a value without its sign. */
static int64_t
magnitude(
	int64_t value)
{
	/* A negative value turned round. */
	if (value < 0)
		return -value;

	/* Succeeded: the value is not negative. */
	return value;
}
