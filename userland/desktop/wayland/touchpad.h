/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The touch pad layer (touchpad.c, ws159-p004): turns a touch pad's
 * multitouch reports into a pointer's motion, buttons and scrolling.
 *
 * It knows nothing of the seat: the caller hands it each event of a report
 * and the report's end, and applies the actions it gives back (input.c).
 * So the host tests run it alone.
 */

#ifndef ZWL_TOUCHPAD_H
#define ZWL_TOUCHPAD_H

#include <stdint.h>

/* The fingers a touch pad's reports address (its slots), and the actions one call may give. */
#define ZWL_TOUCHPAD_SLOTS	10U
#define ZWL_TOUCHPAD_ACTIONS	16U

/* The evdev codes of the left, right and middle buttons (the same on every OS). */
#define ZWL_TOUCHPAD_BUTTON_LEFT	0x110U
#define ZWL_TOUCHPAD_BUTTON_RIGHT	0x111U
#define ZWL_TOUCHPAD_BUTTON_MIDDLE	0x112U

/* What an action asks of the seat. */
enum zwl_touchpad_action_kind {
	ZWL_TOUCHPAD_MOTION,
	ZWL_TOUCHPAD_BUTTON,
	ZWL_TOUCHPAD_SCROLL
};

/*
 * One action: a relative motion in output pixels (dx, dy), a button's
 * press or release (button, pressed), or scrolling in wheel notches
 * (vertical positive down, horizontal positive right, as the seat takes
 * them).  It lives in the caller's list.
 */
struct zwl_touchpad_action {
	enum zwl_touchpad_action_kind kind;
	int32_t dx;
	int32_t dy;
	uint32_t button;
	uint32_t pressed;
	int32_t vertical;
	int32_t horizontal;
};

/* The actions of one call, in order; the caller owns the storage. */
struct zwl_touchpad_actions {
	unsigned count;
	struct zwl_touchpad_action actions[ZWL_TOUCHPAD_ACTIONS];
};

/*
 * One finger as the reports left it: its tracking identifier (-1 for no
 * finger), its place, its place at the last report's end, and whether it
 * came in this report.
 */
struct zwl_touchpad_finger {
	int32_t tracking;
	int32_t x;
	int32_t y;
	int32_t last_x;
	int32_t last_y;
	uint32_t fresh;
};

/*
 * The states of the tap: none; a tap's press given, its release waiting
 * for the time a drag may start; a finger down within that time (a drag or
 * a second tap); and a tap drag, whose button is held until the finger
 * lifts.
 */
enum zwl_touchpad_tap {
	ZWL_TOUCHPAD_TAP_NONE,
	ZWL_TOUCHPAD_TAP_PENDING,
	ZWL_TOUCHPAD_TAP_SECOND,
	ZWL_TOUCHPAD_TAP_DRAG
};

/*
 * One touch pad's state: its fingers and the slot the reports address, its
 * resolution in units per millimetre, the physical button as the reports
 * say and the button that press was given as, the tap, the touch now on
 * the pad (when it began, the most fingers, how far it went), and the
 * remainders of the motion and the scrolling that did not make a whole
 * pixel or notch.
 *
 * It lives in its input device from attach (zwl_touchpad_init) to detach.
 */
struct zwl_touchpad {
	struct zwl_touchpad_finger fingers[ZWL_TOUCHPAD_SLOTS];
	int32_t slot;
	int32_t resolution_x;
	int32_t resolution_y;
	uint32_t button_down;
	uint32_t button_changed;
	uint32_t button_sent;
	uint32_t press_quiet;
	enum zwl_touchpad_tap tap;
	uint64_t tap_deadline_ms;
	uint64_t touch_start_ms;
	uint32_t touch_fingers;
	uint32_t touch_clicked;
	int64_t touch_travel_um;
	uint32_t fingers_before;
	int64_t motion_remainder_x;
	int64_t motion_remainder_y;
	int64_t scroll_travel_x_um;
	int64_t scroll_travel_y_um;
	uint64_t last_frame_ms;
	int32_t natural_scroll;
};

void zwl_touchpad_init(struct zwl_touchpad *pad, int32_t resolution_x, int32_t resolution_y);
void zwl_touchpad_event(struct zwl_touchpad *pad, uint16_t type, uint16_t code, int32_t value);
void zwl_touchpad_frame(struct zwl_touchpad *pad, uint64_t now_ms, struct zwl_touchpad_actions *actions);
void zwl_touchpad_tick(struct zwl_touchpad *pad, uint64_t now_ms, struct zwl_touchpad_actions *actions);
void zwl_touchpad_release_all(struct zwl_touchpad *pad, struct zwl_touchpad_actions *actions);

#endif
