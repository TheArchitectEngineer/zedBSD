/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The canvas of Text Editor (PDF Viewer's canvas.c): the few shapes its
 * frame is made of, drawn on the CPU into premultiplied 0xAARRGGBB words.
 *
 * Colours are given as 0xAARRGGBB, not premultiplied.  Every shape is
 * clipped to the canvas's clip rectangle (the whole canvas unless
 * te_canvas_clip narrows it, as for the text scrolled under the gutter).
 */

#include "textedit.h"

#include <math.h>

static void clip_span(const struct te_canvas *canvas, int *x, int *y, int *width, int *height);
static int clip_inside(const struct te_canvas *canvas, int x, int y);
static uint32_t over(uint32_t pixel, uint32_t color, unsigned coverage);

/*
 * Clips drawing to a rectangle (within the canvas) until te_canvas_unclip.
 */
void
te_canvas_clip(
	struct te_canvas *canvas,
	int x,
	int y,
	int width,
	int height)
{
	/* The whole canvas first, cut to the rectangle. */
	te_canvas_unclip(canvas);
	clip_span(canvas, &x, &y, &width, &height);
	canvas->clip_x = x;
	canvas->clip_y = y;
	canvas->clip_width = width;
	canvas->clip_height = height;
}

/*
 * Lets drawing reach the whole canvas again.
 */
void
te_canvas_unclip(
	struct te_canvas *canvas)
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
te_canvas_fill(
	struct te_canvas *canvas,
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

	/* Keeps the rectangle within the canvas. */
	clip_span(canvas, &x, &y, &width, &height);

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
 * Blends a colour over a rectangle by the colour's alpha.
 */
void
te_canvas_blend(
	struct te_canvas *canvas,
	int x,
	int y,
	int width,
	int height,
	uint32_t color)
{
	uint32_t *row;
	int column;
	int line;

	/* Keeps the rectangle within the canvas. */
	clip_span(canvas, &x, &y, &width, &height);

	/* Blends each pixel. */
	for (line = 0; line < height; line++) {
		row = canvas->pixels + (size_t)(y + line) * canvas->stride + (size_t)x;
		for (column = 0; column < width; column++)
			row[column] = over(row[column], color, 255U);
	}
}

/*
 * Blends a rectangle with rounded corners, smoothed at its curved edges.
 */
void
te_canvas_round(
	struct te_canvas *canvas,
	int x,
	int y,
	int width,
	int height,
	int radius,
	uint32_t color)
{
	double centre_x;
	double centre_y;
	double distance;
	double coverage;
	int column;
	int line;
	int pixel_x;
	int pixel_y;
	int inside;

	/* The radius fits the rectangle. */
	if (radius * 2 > width)
		radius = width / 2;
	if (radius * 2 > height)
		radius = height / 2;

	/* Each pixel of the rectangle, covered fully except in the corners. */
	for (line = 0; line < height; line++) {
		pixel_y = y + line;
		for (column = 0; column < width; column++) {
			/* Only pixels within the clip. */
			pixel_x = x + column;
			inside = clip_inside(canvas, pixel_x, pixel_y);
			if (!inside)
				continue;

			/* The corner's circle decides a corner pixel's coverage. */
			coverage = 1.0;
			centre_x = -1.0;
			centre_y = -1.0;
			if (column < radius)
				centre_x = (double)radius;
			if (column >= width - radius)
				centre_x = (double)(width - radius);
			if (line < radius)
				centre_y = (double)radius;
			if (line >= height - radius)
				centre_y = (double)(height - radius);
			if (centre_x >= 0.0 && centre_y >= 0.0) {
				distance = sqrt(((double)column + 0.5 - centre_x) * ((double)column + 0.5 - centre_x) +
				    ((double)line + 0.5 - centre_y) * ((double)line + 0.5 - centre_y));
				coverage = (double)radius + 0.5 - distance;
				if (coverage <= 0.0)
					continue;
				if (coverage > 1.0)
					coverage = 1.0;
			}

			/* Blends the pixel by its coverage. */
			canvas->pixels[(size_t)pixel_y * canvas->stride + (size_t)pixel_x] =
			    over(canvas->pixels[(size_t)pixel_y * canvas->stride + (size_t)pixel_x], color, (unsigned)(coverage * 255.0 + 0.5));
		}
	}
}

/*
 * Blends a colour through an 8-bit coverage mask (a glyph), stride bytes a
 * row, with its top left at a place.
 */
void
te_canvas_mask(
	struct te_canvas *canvas,
	int x,
	int y,
	const unsigned char *mask,
	int width,
	int height,
	size_t stride,
	uint32_t color)
{
	uint32_t *pixel;
	unsigned coverage;
	int line;
	int column;
	int inside;

	/* Each covered pixel within the clip. */
	for (line = 0; line < height; line++) {
		for (column = 0; column < width; column++) {
			inside = clip_inside(canvas, x + column, y + line);
			if (!inside)
				continue;
			coverage = mask[(size_t)line * stride + (size_t)column];
			if (coverage == 0U)
				continue;
			pixel = canvas->pixels + (size_t)(y + line) * canvas->stride + (size_t)(x + column);
			*pixel = over(*pixel, color, coverage);
		}
	}
}

/* Cuts a rectangle to the clip; an empty result has no width or height. */
static void
clip_span(
	const struct te_canvas *canvas,
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

/* Tells whether a pixel is within the clip. */
static int
clip_inside(
	const struct te_canvas *canvas,
	int x,
	int y)
{
	/* Left of or above the clip. */
	if (x < canvas->clip_x || y < canvas->clip_y)
		return 0;

	/* Right of or below it. */
	if (x >= canvas->clip_x + canvas->clip_width)
		return 0;
	if (y >= canvas->clip_y + canvas->clip_height)
		return 0;

	/* Inside. */
	return 1;
}

/* Blends a colour (not premultiplied) at a coverage (0 to 255) over a premultiplied pixel. */
static uint32_t
over(
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
