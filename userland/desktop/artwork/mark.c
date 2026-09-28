/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Kei mark (ws035-p108), as in kei-boot-splash.png and
 * plan/ws035/kei-identity-design.md: a tall bar with rounded corners (the
 * lower left one wide) and a leaf, a lens leaning from the bar's foot to
 * the upper right, over it.  Each layer is a square of coverage (0..255)
 * sampled sixteen times a pixel; the programs tint them (the bar pale blue,
 * the leaf deeper, the shades deeper still where they are strong) and the
 * overlap darkens as translucent glass does.
 *
 * The mark is measured in units of its own height, 0.9 wide, centred in the
 * square with a small margin.
 */

#include "mark.h"

/* The mark's width in units of its height, and how much of the square its height fills. */
#define MARK_WIDTH		1.00f
#define MARK_FILL		0.94f

/* The bar: its right edge and its foot, and the radii of its corners (top left, top right, bottom left, bottom right). */
#define MARK_BAR_RIGHT		0.47f
#define MARK_BAR_FOOT		0.98f
#define MARK_BAR_TOP_LEFT	0.10f
#define MARK_BAR_TOP_RIGHT	0.14f
#define MARK_BAR_BOTTOM_LEFT	0.24f
#define MARK_BAR_BOTTOM_RIGHT	0.06f

/*
 * The leaf: the part two circles share.  The upper side is an arc that
 * rises steeply from the lower point and runs almost level to the tip; the
 * lower side a rounder arc along the foot and up the right to the tip.  The
 * circles were fitted to points of the two sides in the splash.
 */
#define MARK_LEAF_UPPER_X	0.769f
#define MARK_LEAF_UPPER_Y	0.997f
#define MARK_LEAF_UPPER_RADIUS	0.602f
#define MARK_LEAF_LOWER_X	0.474f
#define MARK_LEAF_LOWER_Y	0.488f
#define MARK_LEAF_LOWER_RADIUS	0.538f

/* The leaf's lower point and its tip, which its shade runs between. */
#define MARK_LEAF_FROM_X	0.17f
#define MARK_LEAF_FROM_Y	0.97f
#define MARK_LEAF_TO_X		0.97f
#define MARK_LEAF_TO_Y		0.42f

/* How many samples a pixel has along each side. */
#define MARK_SAMPLES		4

/*
 * The leaf as the part two circles share: the circle of its upper side and
 * that of its lower side, their centres and radii (squared).
 */
struct mark_leaf {
	float centre_a[2];
	float centre_b[2];
	float radius_a_squared;
	float radius_b_squared;
};

static void mark_leaf_circles(struct mark_leaf *leaf);
static int mark_in_bar(float u, float v);
static int mark_in_corner(float u, float v, float cx, float cy, float radius);
static int mark_in_leaf(const struct mark_leaf *leaf, float u, float v);
static float mark_weight(unsigned layer, float u, float v);

/*
 * Renders one layer of the mark into a square of coverage pixels a side
 * (rows stride bytes apart).
 */
void
keiland_mark_raster(
	unsigned layer,
	unsigned pixels,
	uint8_t *coverage,
	size_t stride)
{
	struct mark_leaf leaf;
	float scale;
	float left;
	float top;
	float u;
	float v;
	float sum;
	unsigned x;
	unsigned y;
	unsigned sx;
	unsigned sy;
	int inside;

	/* The mark's unit and where it starts in the square. */
	mark_leaf_circles(&leaf);
	scale = (float)pixels * MARK_FILL;
	left = ((float)pixels - MARK_WIDTH * scale) / 2.0f;
	top = ((float)pixels - scale) / 2.0f;

	/* Each pixel: the weighted share of its samples inside the layer's shape. */
	for (y = 0; y < pixels; y++) {
		for (x = 0; x < pixels; x++) {
			sum = 0.0f;
			for (sy = 0; sy < MARK_SAMPLES; sy++) {
				for (sx = 0; sx < MARK_SAMPLES; sx++) {
					u = ((float)x + ((float)sx + 0.5f) / MARK_SAMPLES - left) / scale;
					v = ((float)y + ((float)sy + 0.5f) / MARK_SAMPLES - top) / scale;

					/* The bar's layers, or the leaf's. */
					if (layer == KEILAND_MARK_BAR || layer == KEILAND_MARK_BAR_SHADE)
						inside = mark_in_bar(u, v);
					else
						inside = mark_in_leaf(&leaf, u, v);

					/* A sample inside counts as much as the layer weighs there. */
					if (inside)
						sum += mark_weight(layer, u, v);
				}
			}

			/* The pixel's coverage. */
			coverage[(size_t)y * stride + x] = (uint8_t)(sum * 255.0f / (MARK_SAMPLES * MARK_SAMPLES) + 0.5f);
		}
	}
}

/* Fills in the two circles whose shared part is the leaf. */
static void
mark_leaf_circles(
	struct mark_leaf *leaf)
{
	/* The upper side's circle. */
	leaf->centre_a[0] = MARK_LEAF_UPPER_X;
	leaf->centre_a[1] = MARK_LEAF_UPPER_Y;
	leaf->radius_a_squared = MARK_LEAF_UPPER_RADIUS * MARK_LEAF_UPPER_RADIUS;

	/* The lower side's circle. */
	leaf->centre_b[0] = MARK_LEAF_LOWER_X;
	leaf->centre_b[1] = MARK_LEAF_LOWER_Y;
	leaf->radius_b_squared = MARK_LEAF_LOWER_RADIUS * MARK_LEAF_LOWER_RADIUS;
}

/* Reports whether a point is inside the bar with its four rounded corners. */
static int
mark_in_bar(
	float u,
	float v)
{
	int inside;

	/* Outside its box. */
	if (u < 0.0f || v < 0.0f || u > MARK_BAR_RIGHT || v > MARK_BAR_FOOT)
		return 0;

	/* The top left corner. */
	if (u < MARK_BAR_TOP_LEFT && v < MARK_BAR_TOP_LEFT) {
		inside = mark_in_corner(u, v, MARK_BAR_TOP_LEFT, MARK_BAR_TOP_LEFT, MARK_BAR_TOP_LEFT);
		return inside;
	}

	/* The top right corner. */
	if (u > MARK_BAR_RIGHT - MARK_BAR_TOP_RIGHT && v < MARK_BAR_TOP_RIGHT) {
		inside = mark_in_corner(u, v, MARK_BAR_RIGHT - MARK_BAR_TOP_RIGHT, MARK_BAR_TOP_RIGHT, MARK_BAR_TOP_RIGHT);
		return inside;
	}

	/* The wide bottom left corner. */
	if (u < MARK_BAR_BOTTOM_LEFT && v > MARK_BAR_FOOT - MARK_BAR_BOTTOM_LEFT) {
		inside = mark_in_corner(u, v, MARK_BAR_BOTTOM_LEFT, MARK_BAR_FOOT - MARK_BAR_BOTTOM_LEFT, MARK_BAR_BOTTOM_LEFT);
		return inside;
	}

	/* The bottom right corner. */
	if (u > MARK_BAR_RIGHT - MARK_BAR_BOTTOM_RIGHT && v > MARK_BAR_FOOT - MARK_BAR_BOTTOM_RIGHT) {
		inside = mark_in_corner(u, v, MARK_BAR_RIGHT - MARK_BAR_BOTTOM_RIGHT, MARK_BAR_FOOT - MARK_BAR_BOTTOM_RIGHT, MARK_BAR_BOTTOM_RIGHT);
		return inside;
	}

	/* Anywhere else in the box. */
	return 1;
}

/* Reports whether a point is within a corner's circle. */
static int
mark_in_corner(
	float u,
	float v,
	float cx,
	float cy,
	float radius)
{
	float dx;
	float dy;

	/* The distance from the corner's centre, squared, against the radius's. */
	dx = u - cx;
	dy = v - cy;
	if (dx * dx + dy * dy <= radius * radius)
		return 1;

	/* Outside the rounding. */
	return 0;
}

/* Reports whether a point is inside the leaf: inside both of its circles. */
static int
mark_in_leaf(
	const struct mark_leaf *leaf,
	float u,
	float v)
{
	float dx;
	float dy;

	/* The first circle. */
	dx = u - leaf->centre_a[0];
	dy = v - leaf->centre_a[1];
	if (dx * dx + dy * dy > leaf->radius_a_squared)
		return 0;

	/* And the second. */
	dx = u - leaf->centre_b[0];
	dy = v - leaf->centre_b[1];
	if (dx * dx + dy * dy > leaf->radius_b_squared)
		return 0;

	/* Succeeded: the point is in the leaf. */
	return 1;
}

/* How much a layer weighs at a point: the shapes are even, the shades grow towards the deep end. */
static float
mark_weight(
	unsigned layer,
	float u,
	float v)
{
	float dx;
	float dy;
	float along;

	/* The bar's shade deepens towards its foot. */
	if (layer == KEILAND_MARK_BAR_SHADE)
		return v / MARK_BAR_FOOT;

	/* The leaf's shade deepens towards its lower point. */
	if (layer == KEILAND_MARK_LEAF_SHADE) {
		dx = MARK_LEAF_TO_X - MARK_LEAF_FROM_X;
		dy = MARK_LEAF_TO_Y - MARK_LEAF_FROM_Y;
		along = ((u - MARK_LEAF_FROM_X) * dx + (v - MARK_LEAF_FROM_Y) * dy) / (dx * dx + dy * dy);
		if (along < 0.0f)
			along = 0.0f;
		if (along > 1.0f)
			along = 1.0f;
		return 1.0f - along;
	}

	/* The shapes themselves are even. */
	return 1.0f;
}
