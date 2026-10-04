/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A mouse's pointer acceleration (pointer-accel.c, ws089-p024): turns a
 * relative motion in the mouse's counts into pixels at the user's speed
 * (a percentage) and a gain that grows with how fast the mouse moves.
 *
 * It knows nothing of the seat: the caller hands it each report's motion
 * with the report's time and moves the pointer by what it gives back
 * (input.c).  So the host tests run it alone.
 */

#ifndef ZWL_POINTER_ACCEL_H
#define ZWL_POINTER_ACCEL_H

#include <stdint.h>

/* The acceleration's levels, as the settings mouse.acceleration and touchpad.acceleration hold them. */
#define ZWL_ACCEL_NONE		0
#define ZWL_ACCEL_MILD		1
#define ZWL_ACCEL_MEDIUM	2
#define ZWL_ACCEL_STRONG	3

/*
 * One mouse's acceleration: the fractions of a pixel the last reports left
 * over (in 1/25600 of a pixel: hundredths of the speed times 1/256 of the
 * gain), and the time of the last report that moved (started is zero
 * before the first).
 */
struct zwl_pointer_accel {
	int64_t remainder_x;
	int64_t remainder_y;
	uint64_t last_us;
	unsigned started;
};

void zwl_pointer_accel_init(struct zwl_pointer_accel *accel);
int64_t zwl_pointer_accel_gain(int64_t counts_per_second, int32_t level);
void zwl_pointer_accel_move(struct zwl_pointer_accel *accel, int64_t dx, int64_t dy, uint64_t time_us, int32_t speed, int32_t level, int64_t *pixels_x, int64_t *pixels_y);

#endif
