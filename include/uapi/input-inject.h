/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * /dev/input-inject: the test-only pen and touch screen injector
 * (CONFIG_INPUT_TEST_INJECT).
 *
 * The first write on an open is one struct input_inject_setup.  Its kind
 * chooses the device:
 *
 * INPUT_INJECT_KIND_PEN declares a virtual pen: ABS_X 0..x_max, ABS_Y
 * 0..y_max, ABS_PRESSURE 0..4095, ABS_TILT_X/Y -60..60 degrees,
 * BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOUCH, BTN_STYLUS and BTN_STYLUS2
 * (report_contacts and reserved are 0).  Every later write is an array of
 * struct input_event; each event must be one of the declared codes with a
 * value in its range, or the whole write is refused.
 *
 * INPUT_INJECT_KIND_TOUCH declares a virtual touch screen of ten slots
 * whose fingers lie in 0..x_max and 0..y_max (evdev multitouch protocol B:
 * ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X/Y, BTN_TOUCH, ABS_X and
 * ABS_Y).  Every later write is one struct input_inject_touch_frame: the
 * fingers of one frame, as a USB touch screen reports them (a finger left
 * out, or given with tip 0, lifts).  The kernel splits the frame into
 * reports of report_contacts fingers (1..INPUT_INJECT_TOUCH_CONTACTS; fewer
 * than the frame's fingers makes the reports a USB "hybrid" screen sends)
 * and runs them through the USB touch screen's state machine, so readers
 * see what a USB touch screen would give them.
 *
 * The device appears as /dev/input/eventN.  Closing the file removes it.
 */

#ifndef KERN_UAPI_INPUT_INJECT_H
#define KERN_UAPI_INPUT_INJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define INPUT_INJECT_MAGIC		0x6e706e69U
#define INPUT_INJECT_KIND_PEN		1U
#define INPUT_INJECT_KIND_TOUCH		2U
#define INPUT_INJECT_AXIS_MAX		65535
#define INPUT_INJECT_PRESSURE_MAX	4095
#define INPUT_INJECT_TILT_MAX		60
#define INPUT_INJECT_EVENTS_MAX		64U

/* The most fingers one touch frame carries, and the largest contact identifier. */
#define INPUT_INJECT_TOUCH_CONTACTS	10U
#define INPUT_INJECT_CONTACT_ID_MAX	255

struct input_inject_setup {
	uint32_t magic;
	uint32_t kind;
	int32_t x_max;
	int32_t y_max;
	/* Touch: fingers per report (1..INPUT_INJECT_TOUCH_CONTACTS); pen: 0. */
	uint32_t report_contacts;
	uint32_t reserved;
};

/* One finger of a touch frame. */
struct input_inject_contact {
	/* The finger's contact identifier, 0..INPUT_INJECT_CONTACT_ID_MAX. */
	int32_t contact_id;
	/* 1 while the finger touches, 0 in the frame it lifts. */
	int32_t tip;
	int32_t x;
	int32_t y;
};

/* One frame of a touch screen: count fingers (0 lifts every finger). */
struct input_inject_touch_frame {
	uint32_t count;
	uint32_t reserved;
	struct input_inject_contact contacts[INPUT_INJECT_TOUCH_CONTACTS];
};

#ifdef __cplusplus
}
#endif

#endif
