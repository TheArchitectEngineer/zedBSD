/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The canvas of Image Viewer: the few shapes its frame is made of, drawn on
 * the CPU into premultiplied 0xAARRGGBB words.
 *
 * Colours are given as 0xAARRGGBB, not premultiplied.  Every shape is
 * clipped to the canvas.
 */

#include "imageview.h"

#include <math.h>

static void clip_span(const struct iv_canvas *canvas, int *x, int *y, int *width, int *height);
static uint32_t over(uint32_t pixel, uint32_t color, unsigned coverage);

/*
 * Fills a rectangle with a colour, replacing what was there.
 */
void
iv_canvas_fill(
	struct iv_canvas *canvas,
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
		/* The row's pixels in the rectangle. */
		row = canvas->pixels + (size_t)(y + line) * canvas->stride + (size_t)x;
		for (column = 0; column < width; column++)
			row[column] = premultiplied;
	}
}

/*
 * Blends a colour over a rectangle by the colour's alpha.
 */
void
iv_canvas_blend(
	struct iv_canvas *canvas,
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
		/* The row's pixels in the rectangle. */
		row = canvas->pixels + (size_t)(y + line) * canvas->stride + (size_t)x;
		for (column = 0; column < width; column++)
			row[column] = over(row[column], color, 255U);
	}
}

/*
 * Blends a rectangle with rounded corners, smoothed at its curved edges.
 */
void
iv_canvas_round(
	struct iv_canvas *canvas,
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

	/* The radius fits the rectangle, across and down. */
	if (radius * 2 > width)
		radius = width / 2;
	if (radius * 2 > height)
		radius = height / 2;

	/* Each pixel of the rectangle, covered fully except in the corners. */
	for (line = 0; line < height; line++) {
		/* A row off the canvas is not drawn. */
		pixel_y = y + line;
		if (pixel_y < 0 || pixel_y >= canvas->height)
			continue;

		/* Each pixel of the row. */
		for (column = 0; column < width; column++) {
			/* A pixel off the canvas is not drawn. */
			pixel_x = x + column;
			if (pixel_x < 0 || pixel_x >= canvas->width)
				continue;

			/* The centre of the corner's circle across, when the pixel is in a corner's column. */
			coverage = 1.0;
			centre_x = -1.0;
			if (column < radius)
				centre_x = (double)radius;
			if (column >= width - radius)
				centre_x = (double)(width - radius);

			/* And down, when it is in a corner's row. */
			centre_y = -1.0;
			if (line < radius)
				centre_y = (double)radius;
			if (line >= height - radius)
				centre_y = (double)(height - radius);

			/* The corner's circle decides a corner pixel's coverage; a pixel outside it is not drawn. */
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
 * Blends a colour through an 8-bit coverage mask (a glyph), width bytes a
 * row, with its top left at a place.
 */
void
iv_canvas_mask(
	struct iv_canvas *canvas,
	int x,
	int y,
	const unsigned char *mask,
	int width,
	int height,
	uint32_t color)
{
	uint32_t *pixel;
	unsigned coverage;
	int line;
	int column;

	/* Each covered pixel within the canvas. */
	for (line = 0; line < height; line++) {
		/* A row off the canvas is not drawn. */
		if (y + line < 0 || y + line >= canvas->height)
			continue;

		/* Each pixel of the row. */
		for (column = 0; column < width; column++) {
			/* A pixel off the canvas is not drawn. */
			if (x + column < 0 || x + column >= canvas->width)
				continue;

			/* A pixel the mask does not cover is left as it is. */
			coverage = mask[(size_t)line * (size_t)width + (size_t)column];
			if (coverage == 0U)
				continue;

			/* The colour over the pixel by the mask's coverage. */
			pixel = canvas->pixels + (size_t)(y + line) * canvas->stride + (size_t)(x + column);
			*pixel = over(*pixel, color, coverage);
		}
	}
}

/* Cuts a rectangle to the canvas; an empty result has no width or height. */
static void
clip_span(
	const struct iv_canvas *canvas,
	int *x,
	int *y,
	int *width,
	int *height)
{
	/* The left edge. */
	if (*x < 0) {
		*width += *x;
		*x = 0;
	}

	/* The top edge. */
	if (*y < 0) {
		*height += *y;
		*y = 0;
	}

	/* The right edge. */
	if (*x + *width > canvas->width)
		*width = canvas->width - *x;

	/* The bottom edge. */
	if (*y + *height > canvas->height)
		*height = canvas->height - *y;

	/* Nothing left across is an empty rectangle. */
	if (*width < 0)
		*width = 0;

	/* Nor down. */
	if (*height < 0)
		*height = 0;
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
		/* The channel of the pixel, alpha first. */
		shift = (unsigned)(24 - index * 8);
		channel = (pixel >> shift) & 0xffU;

		/* The sum, which rounding may carry past the channel's largest. */
		result[index] = source[index] + channel * keep / 255U;
		if (result[index] > 255U)
			result[index] = 255U;
	}

	/* Reports the blended pixel. */
	return (result[0] << 24) | (result[1] << 16) | (result[2] << 8) | result[3];
}
