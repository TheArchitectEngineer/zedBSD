/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rules of the screen edges' gestures (WS181; edge.h says what they
 * are).  Each answers one question the shell or Home asks at one moment:
 * where a press is, what a press held in the top band has become, what a
 * drag on Home is, how far a point is from another.
 */

#include "edge.h"

/*
 * Tells where a press is for the edges' gestures: the bottom edge's strip
 * (the swipe up to App Home; any pointer), the top edge's band (the swipe
 * down to Wiseview; a touch only, the 2026-10-07 user decision, and not
 * over the launcher or the top-right corner), or nowhere special.
 */
unsigned
kwl_edge_classify(
	int32_t x,
	int32_t y,
	int32_t width,
	int32_t height,
	int touch)
{
	/* The bottom edge's strip. */
	if (y >= height - KWL_EDGE_BOTTOM_HEIGHT)
		return KWL_EDGE_BOTTOM_STRIP;

	/* The band is a touch's only. */
	if (!touch)
		return KWL_EDGE_NONE;

	/* Below the band. */
	if (y >= KWL_EDGE_BAND)
		return KWL_EDGE_NONE;

	/* Over the launcher (and App Home's top-left corner) or the top-right corner of Notes. */
	if (x < KWL_EDGE_LAUNCHER_WIDTH)
		return KWL_EDGE_NONE;
	if (x >= width - KWL_EDGE_CORNER)
		return KWL_EDGE_NONE;

	/* Succeeded: the top edge's band. */
	return KWL_EDGE_TOP_BAND;
}

/*
 * Tells what a press held in the top band is after it moved by (dx, dy):
 * down by KWL_EDGE_BAND_START and more down than across, Wiseview's swipe;
 * across or up by more than KWL_EDGE_BAND_SLIP, the press of what is under
 * it; otherwise still waiting.
 */
unsigned
kwl_edge_band_motion(
	int32_t dx,
	int32_t dy)
{
	int32_t across;

	/* How far across, whichever way. */
	across = dx;
	if (across < 0)
		across = -across;

	/* Down far enough, and more down than across: the swipe. */
	if (dy >= KWL_EDGE_BAND_START && dy > across)
		return KWL_EDGE_BAND_WISEVIEW;

	/* Across or up too far: not the swipe. */
	if (across > KWL_EDGE_BAND_SLIP)
		return KWL_EDGE_BAND_REPLAY;
	if (dy < -KWL_EDGE_BAND_SLIP)
		return KWL_EDGE_BAND_REPLAY;

	/* Succeeded: still waiting to know. */
	return KWL_EDGE_BAND_WAIT;
}

/*
 * Tells what a drag on Home that went (dx, dy) is: sideways at least as
 * far as up or down, the pages; more down, closing Home; more up, nothing.
 */
unsigned
kwl_edge_drag_axis(
	int32_t dx,
	int32_t dy)
{
	int32_t across;

	/* How far across, whichever way. */
	across = dx;
	if (across < 0)
		across = -across;

	/* Sideways at least as far as down or up: the pages. */
	if (across >= dy && across >= -dy)
		return KWL_EDGE_DRAG_PAGES;

	/* Down: closing Home. */
	if (dy > 0)
		return KWL_EDGE_DRAG_CLOSE;

	/* Succeeded: up, which does nothing. */
	return KWL_EDGE_DRAG_NONE;
}

/*
 * Gives the distance of (dx, dy) from the origin in whole pixels (rounded
 * down), without floating point: how far a docked title has been pulled.
 */
int32_t
kwl_edge_distance(
	int32_t dx,
	int32_t dy)
{
	int64_t square;
	int64_t root;
	int64_t bit;

	/* The square of the distance. */
	square = (int64_t)dx * dx + (int64_t)dy * dy;

	/* Its integer square root, one bit at a time from the highest (a screen is far less than 2^21 pixels across). */
	root = 0;
	for (bit = (int64_t)1 << 21; bit != 0; bit >>= 1) {
		if ((root + bit) * (root + bit) <= square)
			root += bit;
	}

	/* Succeeded: the distance. */
	return (int32_t)root;
}
