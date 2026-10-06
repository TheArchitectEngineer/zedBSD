/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One swipe as one step (ws142-p009; swipe.h says what a swipe is).
 */

#include "swipe.h"

static int32_t swipe_magnitude(int32_t value);

/*
 * Takes the fingers' travel since the last report: once the swipe's travel
 * across or down passes ZWL_SWIPE_STEP_UM, it decides by the way it went
 * most (left or right across, down or up), and decides nothing more until
 * it ends.  Returns what it decided now (ZWL_SWIPE_NONE otherwise).
 */
unsigned
zwl_swipe_take(
	struct zwl_swipe *swipe,
	int32_t across_um,
	int32_t down_um)
{
	int32_t across;
	int32_t down;

	/* A swipe that decided already waits for its end. */
	if (swipe->decided)
		return ZWL_SWIPE_NONE;

	/* The travel so far; not far enough yet decides nothing. */
	swipe->across_um += across_um;
	swipe->down_um += down_um;
	across = swipe_magnitude(swipe->across_um);
	down = swipe_magnitude(swipe->down_um);
	if (across < ZWL_SWIPE_STEP_UM && down < ZWL_SWIPE_STEP_UM)
		return ZWL_SWIPE_NONE;

	/* Decided, once. */
	swipe->decided = 1U;

	/* Across when it went across most. */
	if (across >= down) {
		if (swipe->across_um < 0)
			return ZWL_SWIPE_LEFT;
		return ZWL_SWIPE_RIGHT;
	}

	/* Otherwise down or up. */
	if (swipe->down_um > 0)
		return ZWL_SWIPE_DOWN;
	return ZWL_SWIPE_UP;
}

/*
 * Ends a swipe (the fingers lifted): the next travel begins another.
 */
void
zwl_swipe_end(
	struct zwl_swipe *swipe)
{
	/* Nothing travelled, nothing decided. */
	swipe->across_um = 0;
	swipe->down_um = 0;
	swipe->decided = 0U;
}

/*
 * Names what a swipe decided, for the log.
 */
const char *
zwl_swipe_name(
	unsigned direction)
{
	/* Each direction. */
	switch (direction) {
	case ZWL_SWIPE_LEFT:
		return "left";
	case ZWL_SWIPE_RIGHT:
		return "right";
	case ZWL_SWIPE_DOWN:
		return "down";
	case ZWL_SWIPE_UP:
		return "up";
	default:
		return "none";
	}
}

/* Gives a value without its sign. */
static int32_t
swipe_magnitude(
	int32_t value)
{
	/* A negative value turned round. */
	if (value < 0)
		return -value;
	return value;
}
