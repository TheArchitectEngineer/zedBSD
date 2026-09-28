/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * /dev/input-inject: the test-only pen injector (CONFIG_INPUT_TEST_INJECT).
 *
 * The first write on an open is one struct input_inject_setup.  It declares
 * a virtual pen: ABS_X 0..x_max, ABS_Y 0..y_max, ABS_PRESSURE 0..4095,
 * ABS_TILT_X/Y -60..60 degrees, BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOUCH,
 * BTN_STYLUS and BTN_STYLUS2.  The pen then appears as /dev/input/eventN.
 * Every later write is an array of struct input_event; each event must be
 * one of the declared codes with a value in its range, or the whole write
 * is refused.  Closing the file removes the pen.
 */

#ifndef KERN_UAPI_INPUT_INJECT_H
#define KERN_UAPI_INPUT_INJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define INPUT_INJECT_MAGIC		0x6e706e69U
#define INPUT_INJECT_KIND_PEN		1U
#define INPUT_INJECT_AXIS_MAX		65535
#define INPUT_INJECT_PRESSURE_MAX	4095
#define INPUT_INJECT_TILT_MAX		60
#define INPUT_INJECT_EVENTS_MAX		64U

struct input_inject_setup {
	uint32_t magic;
	uint32_t kind;
	int32_t x_max;
	int32_t y_max;
};

#ifdef __cplusplus
}
#endif

#endif
