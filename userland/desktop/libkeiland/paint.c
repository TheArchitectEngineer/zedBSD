/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The library's canvas (Text Editor's canvas.c, with the shapes the file
 * chooser adds): the few shapes a window of the library is made of, drawn
 * on the CPU into premultiplied 0xAARRGGBB words.
 *
 * Colours are given as 0xAARRGGBB, not premultiplied.  Every shape is
 * clipped to the canvas's clip rectangle (the whole canvas unless
 * kl_paint_clip narrows it).  Curved edges are smoothed by the distance of
 * each pixel's centre from the shape's edge.
 */

#include "paint.h"

#include <math.h>

static void paint_span(const struct kl_canvas *canvas, int *x, int *y, int *width, int *height);
static uint32_t paint_over(uint32_t pixel, uint32_t color, unsigned coverage);
static double paint_rounded_distance(double px, double py, double x, double y, double width, double height, double radius);
static unsigned paint_coverage(double amount);
static void paint_blend_at(struct kl_canvas *canvas, int x, int y, uint32_t color, unsigned coverage);

/*
 * Sets a canvas over a block of pixels, clipped to the whole of it.
 */
void
kl_paint_init(
	struct kl_canvas *canvas,
	uint32_t *pixels,
	int width,
	int height,
	size_t stride)
{
	/* The pixels and their shape. */
	canvas->pixels = pixels;
	canvas->width = width;
	canvas->height = height;
	canvas->stride = stride;

	/* Drawing reaches the whole canvas. */
	kl_paint_unclip(canvas);
}

/*
 * Clips drawing to a rectangle (within the canvas) until kl_paint_unclip.
 */
void
kl_paint_clip(
	struct kl_canvas *canvas,
	int x,
	int y,
	int width,
	int height)
{
	/* The whole canvas first, cut to the rectangle. */
	kl_paint_unclip(canvas);
	paint_span(canvas, &x, &y, &width, &height);
	canvas->clip_x = x;
	canvas->clip_y = y;
	canvas->clip_width = width;
	canvas->clip_height = height;
}

/*
 * Lets drawing reach the whole canvas again.
 */
void
kl_paint_unclip(
	struct kl_canvas *canvas)
{
	/* The clip is the canvas. */
	canvas->clip_x = 0;
	canvas->clip_y = 0;
	canvas->clip_width = canvas->width;
	canvas->clip_height = canvas->height;
}

/*
 * Fills a rectangle with a colour, replacing what was there.
 */
void
kl_paint_fill(
	struct kl_canvas *canvas,
	int x,
	int y,
	int width,
	int height,
	uint32_t color)
{
	uint32_t premultiplied;
	uint32_t *row;
	unsigned alpha;
	int column;
	int line;

	/* Keeps the rectangle within the clip. */
	paint_span(canvas, &x, &y, &width, &height);

	/* The colour, premultiplied. */
	alpha = color >> 24;
	premultiplied = (alpha << 24) |
	    ((((color >> 16) & 0xffU) * alpha / 255U) << 16) |
	    ((((color >> 8) & 0xffU) * alpha / 255U) << 8) |
	    ((color & 0xffU) * alpha / 255U);

	/* Writes each pixel. */
	for (line = 0; line < height; line++) {
		row = canvas->pixels + (size_t)(y + line) * canvas->stride + (size_t)x;
		for (column = 0; column < width; column++)
			row[column] = premultiplied;
	}
}

/*
 * Fills a rectangle with a vertical gradient of two opaque colours,
 * replacing what was there.
 */
void
kl_paint_gradient(
	struct kl_canvas *canvas,
	int x,
	int y,
	int width,
	int height,
	uint32_t top,
	uint32_t bottom)
{
	uint32_t color;
	unsigned channel;
	unsigned mixed;
	unsigned shift;
	int total;
	int line;
	int first;

	/* The whole gradient's height, kept before the clip cuts it. */
	total = height;
	if (total < 1)
		return;
	first = y;

	/* Keeps the rectangle within the clip. */
	paint_span(canvas, &x, &y, &width, &height);

	/* Each row in its mixture of the two colours. */
	for (line = 0; line < height; line++) {
		color = 0xff000000U;
		for (channel = 0U; channel < 3U; channel++) {
			shift = channel * 8U;
			mixed = (((top >> shift) & 0xffU) * (unsigned)(total - (y + line - first)) +
			    ((bottom >> shift) & 0xffU) * (unsigned)(y + line - first)) / (unsigned)total;
			color |= (mixed & 0xffU) << shift;
		}

		/* The row. */
		kl_paint_fill(canvas, x, y + line, width, 1, color);
	}
}

/*
 * Blends a rectangle with rounded corners, smoothed at its curved edges.
 */
void
kl_paint_round(
	struct kl_canvas *canvas,
	int x,
	int y,
	int width,
	int height,
	int radius,
	uint32_t color)
{
	double distance;
	int column;
	int line;

	/* The radius fits the rectangle. */
	if (radius * 2 > width)
		radius = width / 2;
	if (radius * 2 > height)
		radius = height / 2;

	/* Each pixel, covered by how far inside the edge its centre is. */
	for (line = y; line < y + height; line++) {
		for (column = x; column < x + width; column++) {
			distance = paint_rounded_distance((double)column + 0.5, (double)line + 0.5, (double)x, (double)y, (double)width, (double)height, (double)radius);
			paint_blend_at(canvas, column, line, color, paint_coverage(0.5 - distance));
		}
	}
}

/*
 * Blends a one-pixel outline just inside a rectangle with rounded corners.
 */
void
kl_paint_round_border(
	struct kl_canvas *canvas,
	int x,
	int y,
	int width,
	int height,
	int radius,
	uint32_t color)
{
	double distance;
	double amount;
	int column;
	int line;

	/* The radius fits the rectangle. */
	if (radius * 2 > width)
		radius = width / 2;
	if (radius * 2 > height)
		radius = height / 2;

	/* Each pixel, covered by how much of the band one pixel inside the edge it holds. */
	for (line = y; line < y + height; line++) {
		for (column = x; column < x + width; column++) {
			distance = paint_rounded_distance((double)column + 0.5, (double)line + 0.5, (double)x, (double)y, (double)width, (double)height, (double)radius);

			/* Inside the shape's edge, less what is also inside the edge a pixel further in. */
			amount = (double)paint_coverage(0.5 - distance) - (double)paint_coverage(-0.5 - distance);
			paint_blend_at(canvas, column, line, color, (unsigned)amount);
		}
	}
}

/*
 * Blends a straight line of a thickness with round ends between two points.
 */
void
kl_paint_line(
	struct kl_canvas *canvas,
	double x0,
	double y0,
	double x1,
	double y1,
	double thickness,
	uint32_t color)
{
	double length_squared;
	double along;
	double near_x;
	double near_y;
	double distance;
	double centre_x;
	double centre_y;
	double reach;
	int left;
	int right;
	int top;
	int bottom;
	int column;
	int line;

	/* The box the line and its ends cover. */
	reach = thickness / 2.0 + 1.0;
	left = (int)floor((x0 < x1 ? x0 : x1) - reach);
	right = (int)ceil((x0 > x1 ? x0 : x1) + reach);
	top = (int)floor((y0 < y1 ? y0 : y1) - reach);
	bottom = (int)ceil((y0 > y1 ? y0 : y1) + reach);
	length_squared = (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);

	/* Each pixel of the box, covered by its centre's distance from the segment. */
	for (line = top; line < bottom; line++) {
		for (column = left; column < right; column++) {
			centre_x = (double)column + 0.5;
			centre_y = (double)line + 0.5;

			/* The nearest point of the segment. */
			along = 0.0;
			if (length_squared > 0.0)
				along = ((centre_x - x0) * (x1 - x0) + (centre_y - y0) * (y1 - y0)) / length_squared;
			if (along < 0.0)
				along = 0.0;
			if (along > 1.0)
				along = 1.0;
			near_x = x0 + along * (x1 - x0);
			near_y = y0 + along * (y1 - y0);

			/* The pixel's share of the stroke. */
			distance = sqrt((centre_x - near_x) * (centre_x - near_x) + (centre_y - near_y) * (centre_y - near_y));
			paint_blend_at(canvas, column, line, color, paint_coverage(thickness / 2.0 + 0.5 - distance));
		}
	}
}

/*
 * Blends a filled circle.
 */
void
kl_paint_circle(
	struct kl_canvas *canvas,
	double x,
	double y,
	double radius,
	uint32_t color)
{
	double distance;
	int column;
	int line;

	/* Each pixel of the circle's box, covered by how far inside the edge its centre is. */
	for (line = (int)floor(y - radius - 1.0); line < (int)ceil(y + radius + 1.0); line++) {
		for (column = (int)floor(x - radius - 1.0); column < (int)ceil(x + radius + 1.0); column++) {
			distance = sqrt(((double)column + 0.5 - x) * ((double)column + 0.5 - x) + ((double)line + 0.5 - y) * ((double)line + 0.5 - y));
			paint_blend_at(canvas, column, line, color, paint_coverage(radius + 0.5 - distance));
		}
	}
}

/*
 * Blends a circle's outline of a thickness, centred on the radius.
 */
void
kl_paint_ring(
	struct kl_canvas *canvas,
	double x,
	double y,
	double radius,
	double thickness,
	uint32_t color)
{
	double distance;
	double reach;
	int column;
	int line;

	/* Each pixel of the ring's box, covered by its centre's distance from the circle. */
	reach = radius + thickness / 2.0 + 1.0;
	for (line = (int)floor(y - reach); line < (int)ceil(y + reach); line++) {
		for (column = (int)floor(x - reach); column < (int)ceil(x + reach); column++) {
			distance = sqrt(((double)column + 0.5 - x) * ((double)column + 0.5 - x) + ((double)line + 0.5 - y) * ((double)line + 0.5 - y));
			paint_blend_at(canvas, column, line, color, paint_coverage(thickness / 2.0 + 0.5 - fabs(distance - radius)));
		}
	}
}

/*
 * Blends a colour through an 8-bit coverage mask (a glyph), stride bytes a
 * row, with its top left at a place.
 */
void
kl_paint_mask(
	struct kl_canvas *canvas,
	int x,
	int y,
	const unsigned char *mask,
	int width,
	int height,
	size_t stride,
	uint32_t color)
{
	unsigned coverage;
	int line;
	int column;

	/* Each covered pixel. */
	for (line = 0; line < height; line++) {
		for (column = 0; column < width; column++) {
			coverage = mask[(size_t)line * stride + (size_t)column];
			if (coverage != 0U)
				paint_blend_at(canvas, x + column, y + line, color, coverage);
		}
	}
}

/* Cuts a rectangle to the clip; an empty result has no width or height. */
static void
paint_span(
	const struct kl_canvas *canvas,
	int *x,
	int *y,
	int *width,
	int *height)
{
	int right;
	int bottom;

	/* The left edge. */
	if (*x < canvas->clip_x) {
		*width -= canvas->clip_x - *x;
		*x = canvas->clip_x;
	}

	/* The top edge. */
	if (*y < canvas->clip_y) {
		*height -= canvas->clip_y - *y;
		*y = canvas->clip_y;
	}

	/* The right and bottom edges. */
	right = canvas->clip_x + canvas->clip_width;
	bottom = canvas->clip_y + canvas->clip_height;
	if (*x + *width > right)
		*width = right - *x;
	if (*y + *height > bottom)
		*height = bottom - *y;

	/* Nothing left is an empty rectangle. */
	if (*width < 0)
		*width = 0;
	if (*height < 0)
		*height = 0;
}

/* Blends a colour at a coverage over one pixel, when the pixel is within the clip. */
static void
paint_blend_at(
	struct kl_canvas *canvas,
	int x,
	int y,
	uint32_t color,
	unsigned coverage)
{
	uint32_t *pixel;

	/* Nothing covered, nothing drawn. */
	if (coverage == 0U)
		return;

	/* Left of or above the clip. */
	if (x < canvas->clip_x || y < canvas->clip_y)
		return;

	/* Right of or below it. */
	if (x >= canvas->clip_x + canvas->clip_width)
		return;
	if (y >= canvas->clip_y + canvas->clip_height)
		return;

	/* The pixel with the colour over it. */
	pixel = canvas->pixels + (size_t)y * canvas->stride + (size_t)x;
	*pixel = paint_over(*pixel, color, coverage);
}

/* Turns a covered share (any number; 1 or more is full) into a coverage of 0 to 255. */
static unsigned
paint_coverage(
	double amount)
{
	/* Outside. */
	if (amount <= 0.0)
		return 0U;

	/* Fully inside. */
	if (amount >= 1.0)
		return 255U;

	/* On the edge: the share of the pixel. */
	return (unsigned)(amount * 255.0 + 0.5);
}

/* Reports how far a point is outside a rounded rectangle (below zero inside). */
static double
paint_rounded_distance(
	double px,
	double py,
	double x,
	double y,
	double width,
	double height,
	double radius)
{
	double half_width;
	double half_height;
	double qx;
	double qy;
	double outside_x;
	double outside_y;
	double inside;

	/* The point folded into one quarter, from the corner circle's centre. */
	half_width = width / 2.0;
	half_height = height / 2.0;
	qx = fabs(px - (x + half_width)) - (half_width - radius);
	qy = fabs(py - (y + half_height)) - (half_height - radius);

	/* The part of it beyond the straight edges. */
	outside_x = qx;
	if (outside_x < 0.0)
		outside_x = 0.0;
	outside_y = qy;
	if (outside_y < 0.0)
		outside_y = 0.0;

	/* How deep inside it is along the nearer straight edge. */
	inside = qx;
	if (qy > inside)
		inside = qy;
	if (inside > 0.0)
		inside = 0.0;

	/* The distance from the edge. */
	return sqrt(outside_x * outside_x + outside_y * outside_y) + inside - radius;
}

/* Blends a colour (not premultiplied) at a coverage (0 to 255) over a premultiplied pixel. */
static uint32_t
paint_over(
	uint32_t pixel,
	uint32_t color,
	unsigned coverage)
{
	unsigned alpha;
	unsigned keep;
	unsigned channel;
	unsigned result[4];
	unsigned source[4];
	unsigned shift;
	int index;

	/* The source's alpha by the coverage, and what of the pixel stays. */
	alpha = (color >> 24) * coverage / 255U;
	keep = 255U - alpha;

	/* The source premultiplied, alpha first. */
	source[0] = alpha;
	source[1] = ((color >> 16) & 0xffU) * alpha / 255U;
	source[2] = ((color >> 8) & 0xffU) * alpha / 255U;
	source[3] = (color & 0xffU) * alpha / 255U;

	/* Each channel: the source over what stays of the pixel. */
	for (index = 0; index < 4; index++) {
		shift = (unsigned)(24 - index * 8);
		channel = (pixel >> shift) & 0xffU;
		result[index] = source[index] + channel * keep / 255U;
		if (result[index] > 255U)
			result[index] = 255U;
	}

	/* Reports the blended pixel. */
	return (result[0] << 24) | (result[1] << 16) | (result[2] << 8) | result[3];
}
