/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The icons zdesktop draws for the titlebar's controls (WS070 p009,
 * plan/ws070/titlebar-design.md section 7): back, forward, home, search,
 * the views, and the other roles.
 *
 * Each icon is a few strokes, rings, dots and rounded boxes on a grid of
 * 24 units.  zwl_icon_raster turns one into a square of coverage at a size
 * in pixels by measuring, for each pixel's centre, how far it is from the
 * nearest part: a stroke covers the pixel as far as the pixel lies within
 * half its width, which gives smooth edges without an outline rasterizer.
 * glass.c places the results in the glyph atlas when the look opens.
 */

#include "icons.h"

#include <math.h>
#include <string.h>

/* The grid the icons are drawn on, and the width of their strokes, in units. */
#define ICON_GRID		24.0f
#define ICON_STROKE		1.8f

/* The most parts one icon has. */
#define ICON_PARTS		8

/*
 * The kinds of part an icon is made of: a stroke between two points, a
 * ring (a stroked circle), a dot (a filled circle), and a filled box with
 * rounded corners.
 */
enum icon_kind {
	ICON_END,
	ICON_SEGMENT,
	ICON_RING,
	ICON_DOT,
	ICON_BOX
};

/*
 * One part of an icon, in grid units: a segment's two ends (a, b, c, d),
 * a ring's or a dot's centre and radius (a, b, c), or a box's corners and
 * corner radius (a, b, c, d, e).
 */
struct icon_part {
	unsigned kind;
	float a;
	float b;
	float c;
	float d;
	float e;
};

/* The parts of each icon, in the order of enum glass_icon, each list ended by ICON_END. */
static const struct icon_part icon_parts[GLASS_ICON_COUNT][ICON_PARTS] = {
	/* Back: a chevron pointing left. */
	{
		{ ICON_SEGMENT, 15.0f, 5.0f, 8.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 8.0f, 12.0f, 15.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Forward: a chevron pointing right. */
	{
		{ ICON_SEGMENT, 9.0f, 5.0f, 16.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 16.0f, 12.0f, 9.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Up: a chevron pointing up. */
	{
		{ ICON_SEGMENT, 5.0f, 15.0f, 12.0f, 8.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 8.0f, 19.0f, 15.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Home: a roof over a house. */
	{
		{ ICON_SEGMENT, 3.5f, 11.5f, 12.0f, 4.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 4.0f, 20.5f, 11.5f, 0.0f },
		{ ICON_SEGMENT, 6.0f, 9.5f, 6.0f, 20.0f, 0.0f },
		{ ICON_SEGMENT, 6.0f, 20.0f, 18.0f, 20.0f, 0.0f },
		{ ICON_SEGMENT, 18.0f, 20.0f, 18.0f, 9.5f, 0.0f },
		{ ICON_SEGMENT, 10.5f, 20.0f, 10.5f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 10.5f, 15.0f, 13.5f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 13.5f, 15.0f, 13.5f, 20.0f, 0.0f }
	},
	/* Search: a magnifier. */
	{
		{ ICON_RING, 10.0f, 10.0f, 6.0f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 14.5f, 14.5f, 20.0f, 20.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Icons (grid): four rounded squares. */
	{
		{ ICON_BOX, 4.0f, 4.0f, 10.8f, 10.8f, 1.6f },
		{ ICON_BOX, 13.2f, 4.0f, 20.0f, 10.8f, 1.6f },
		{ ICON_BOX, 4.0f, 13.2f, 10.8f, 20.0f, 1.6f },
		{ ICON_BOX, 13.2f, 13.2f, 20.0f, 20.0f, 1.6f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* List: three dots and three lines. */
	{
		{ ICON_DOT, 5.0f, 6.0f, 1.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 6.0f, 20.0f, 6.0f, 0.0f },
		{ ICON_DOT, 5.0f, 12.0f, 1.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 12.0f, 20.0f, 12.0f, 0.0f },
		{ ICON_DOT, 5.0f, 18.0f, 1.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 18.0f, 20.0f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Columns: a frame cut in three. */
	{
		{ ICON_SEGMENT, 3.5f, 5.0f, 20.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 5.0f, 20.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 19.0f, 3.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 19.0f, 3.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 9.2f, 5.0f, 9.2f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 14.8f, 5.0f, 14.8f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Sort: three lines, shorter and shorter. */
	{
		{ ICON_SEGMENT, 4.0f, 6.0f, 20.0f, 6.0f, 0.0f },
		{ ICON_SEGMENT, 4.0f, 12.0f, 15.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 4.0f, 18.0f, 10.0f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Filter: three centred lines, narrower and narrower. */
	{
		{ ICON_SEGMENT, 4.0f, 6.0f, 20.0f, 6.0f, 0.0f },
		{ ICON_SEGMENT, 7.5f, 12.0f, 16.5f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 10.5f, 18.0f, 13.5f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Sidebar: a frame with a panel on the left. */
	{
		{ ICON_SEGMENT, 3.5f, 5.0f, 20.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 5.0f, 20.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 19.0f, 3.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 19.0f, 3.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 5.0f, 9.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Preview: a frame with a panel on the right. */
	{
		{ ICON_SEGMENT, 3.5f, 5.0f, 20.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 5.0f, 20.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 19.0f, 3.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 19.0f, 3.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 15.0f, 5.0f, 15.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Plus. */
	{
		{ ICON_SEGMENT, 12.0f, 5.0f, 12.0f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 5.0f, 12.0f, 19.0f, 12.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Close: a cross. */
	{
		{ ICON_SEGMENT, 7.0f, 7.0f, 17.0f, 17.0f, 0.0f },
		{ ICON_SEGMENT, 17.0f, 7.0f, 7.0f, 17.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Overflow: three dots. */
	{
		{ ICON_DOT, 6.0f, 12.0f, 1.9f, 0.0f, 0.0f },
		{ ICON_DOT, 12.0f, 12.0f, 1.9f, 0.0f, 0.0f },
		{ ICON_DOT, 18.0f, 12.0f, 1.9f, 0.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	}
};

static float icon_distance(const struct icon_part *part, float x, float y);
static float icon_segment_distance(float x, float y, float x0, float y0, float x1, float y1);
static float icon_box_distance(float x, float y, float x0, float y0, float x1, float y1, float radius);
static float icon_clamp(float value);

/*
 * Draws an icon (GLASS_ICON_*) into a square of coverage of a size in
 * pixels, a byte a pixel (255 fully covered), rows stride bytes apart.
 */
void
zwl_icon_raster(
	unsigned icon,
	unsigned pixels,
	uint8_t *coverage,
	size_t stride)
{
	const struct icon_part *part;
	float scale;
	float half;
	float distance;
	float amount;
	float best;
	float x;
	float y;
	unsigned column;
	unsigned row;
	unsigned index;

	/* An icon the table does not have is left empty. */
	for (row = 0; row < pixels; row++)
		memset(coverage + (size_t)row * stride, 0, pixels);
	if (icon >= GLASS_ICON_COUNT || pixels == 0U)
		return;

	/* Pixels a unit, and half a stroke's width in pixels. */
	scale = (float)pixels / ICON_GRID;
	half = ICON_STROKE * scale * 0.5f;

	/* Each pixel: how much of it the nearest part covers. */
	for (row = 0; row < pixels; row++) {
		for (column = 0; column < pixels; column++) {
			x = ((float)column + 0.5f) / scale;
			y = ((float)row + 0.5f) / scale;
			best = 0.0f;

			/* Each part of the icon. */
			for (index = 0; index < ICON_PARTS; index++) {
				part = &icon_parts[icon][index];
				if (part->kind == ICON_END)
					break;
				distance = icon_distance(part, x, y) * scale;

				/* A stroke or a ring covers within half its width, a dot or a box inside it. */
				if (part->kind == ICON_SEGMENT || part->kind == ICON_RING) {
					amount = icon_clamp(half - distance + 0.5f);
				} else {
					amount = icon_clamp(0.5f - distance);
				}

				/* The part that covers most wins. */
				if (amount > best)
					best = amount;
			}

			/* The pixel's coverage. */
			coverage[(size_t)row * stride + column] = (uint8_t)(best * 255.0f + 0.5f);
		}
	}
}

/* Measures how far a point (in units) is from a part: from a stroke's line or a ring's circle, or outside a dot or a box (negative inside). */
static float
icon_distance(
	const struct icon_part *part,
	float x,
	float y)
{
	float distance;
	float dx;
	float dy;
	float from_centre;

	/* No part is as far as can be; the centre's distance serves the ring and the dot. */
	distance = 1000.0f;
	dx = x - part->a;
	dy = y - part->b;
	from_centre = sqrtf(dx * dx + dy * dy);

	/* Each kind of part. */
	switch (part->kind) {
	case ICON_SEGMENT:
		distance = icon_segment_distance(x, y, part->a, part->b, part->c, part->d);
		break;
	case ICON_RING:
		distance = fabsf(from_centre - part->c);
		break;
	case ICON_DOT:
		distance = from_centre - part->c;
		break;
	case ICON_BOX:
		distance = icon_box_distance(x, y, part->a, part->b, part->c, part->d, part->e);
		break;
	default:
		break;
	}

	/* Reports the distance. */
	return distance;
}

/* Measures how far a point is from a segment. */
static float
icon_segment_distance(
	float x,
	float y,
	float x0,
	float y0,
	float x1,
	float y1)
{
	float distance;
	float along;
	float length;
	float dx;
	float dy;
	float px;
	float py;

	/* The segment's direction, and where along it the point falls (clamped to its ends). */
	dx = x1 - x0;
	dy = y1 - y0;
	length = dx * dx + dy * dy;
	along = 0.0f;
	if (length > 0.0f)
		along = ((x - x0) * dx + (y - y0) * dy) / length;
	if (along < 0.0f)
		along = 0.0f;
	if (along > 1.0f)
		along = 1.0f;

	/* The distance to that nearest point. */
	px = x - (x0 + along * dx);
	py = y - (y0 + along * dy);
	distance = sqrtf(px * px + py * py);

	/* Reports the distance. */
	return distance;
}

/* Measures the signed distance from a point to a box with rounded corners (negative inside). */
static float
icon_box_distance(
	float x,
	float y,
	float x0,
	float y0,
	float x1,
	float y1,
	float radius)
{
	float cx;
	float cy;
	float qx;
	float qy;
	float outside;
	float inside;

	/* The point folded into the box's first quadrant, from the inner rectangle the corners round off. */
	cx = (x0 + x1) * 0.5f;
	cy = (y0 + y1) * 0.5f;
	qx = fabsf(x - cx) - ((x1 - x0) * 0.5f - radius);
	qy = fabsf(y - cy) - ((y1 - y0) * 0.5f - radius);

	/* Outside: the distance to the inner rectangle's corner; inside: how deep. */
	outside = sqrtf(fmaxf(qx, 0.0f) * fmaxf(qx, 0.0f) + fmaxf(qy, 0.0f) * fmaxf(qy, 0.0f));
	inside = fminf(fmaxf(qx, qy), 0.0f);

	/* Reports the distance, less the corners' radius. */
	return outside + inside - radius;
}

/* Keeps a coverage between 0 and 1. */
static float
icon_clamp(
	float value)
{
	/* Below and above the range. */
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;

	/* Inside it. */
	return value;
}
