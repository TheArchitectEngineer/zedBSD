/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The boot logo (ws035-p096).
 *
 * zedbsd.cfg names the logo with logo=PATH, a path relative to the boot
 * volume like kernel=.  The file is a binary PPM (P6, maximum value 255, at
 * most 4096 pixels a side).  The loader fills the screen with the colour of
 * the logo's top-left pixel and draws the logo in the middle, so a logo
 * with a plain background reads as one picture on any screen size.  The
 * kernel does not know logo=; it ignores it as an unknown name.
 */

#include "logo.h"

/* The largest logo side the loader draws. */
#define LOGO_SIDE_MAX	4096U

static int logo_header_number(const uint8_t *data, size_t size, size_t *at, uint32_t *number);
static void logo_skip_space(const uint8_t *data, size_t size, size_t *at);
static uint32_t logo_pixel(const struct zbl6_framebuffer *framebuffer, const uint8_t *rgb);

/*
 * Draws a binary PPM in the middle of the framebuffer over its top-left
 * pixel's colour.
 *
 * Returns 1 when the logo was drawn, 0 when the file is not a PPM the
 * loader takes (the screen is then left as it was).
 */
int
zbl_uefi_logo_draw(
	const uint8_t *data,
	size_t size,
	const struct zbl6_framebuffer *framebuffer)
{
	volatile uint32_t *pixels;
	const uint8_t *image;
	uint32_t width;
	uint32_t height;
	uint32_t maximum;
	uint32_t background;
	uint32_t origin_x;
	uint32_t origin_y;
	uint32_t shown_width;
	uint32_t shown_height;
	uint32_t skip_x;
	uint32_t skip_y;
	uint32_t x;
	uint32_t y;
	size_t at;
	int error;

	/* The magic: P6. */
	if (data == NULL || framebuffer == NULL || size < 2U || data[0] != 'P' || data[1] != '6')
		return 0;

	/* The width, the height and the maximum value, which must be 255. */
	at = 2U;
	error = logo_header_number(data, size, &at, &width);
	if (error != 0)
		return 0;
	error = logo_header_number(data, size, &at, &height);
	if (error != 0)
		return 0;
	error = logo_header_number(data, size, &at, &maximum);
	if (error != 0 || maximum != 255U)
		return 0;
	if (width == 0U || height == 0U || width > LOGO_SIDE_MAX || height > LOGO_SIDE_MAX)
		return 0;

	/* One whitespace byte, then the pixels, all of them. */
	at++;
	if (at > size || (size_t)width * height * 3U > size - at)
		return 0;
	image = data + at;

	/* A framebuffer the loader can write whole pixels to. */
	if (framebuffer->physical_base == 0U || framebuffer->width == 0U || framebuffer->height == 0U)
		return 0;
	pixels = (volatile uint32_t *)(uintptr_t)framebuffer->physical_base;

	/* The screen in the logo's background colour. */
	background = logo_pixel(framebuffer, image);
	for (y = 0; y < framebuffer->height; y++) {
		for (x = 0; x < framebuffer->width; x++)
			pixels[(uint64_t)y * framebuffer->stride + x] = background;
	}

	/* The logo in the middle, cut to the screen when it is larger. */
	shown_width = width;
	shown_height = height;
	skip_x = 0U;
	skip_y = 0U;
	if (shown_width > framebuffer->width) {
		skip_x = (shown_width - framebuffer->width) / 2U;
		shown_width = framebuffer->width;
	}

	/* The same for its height. */
	if (shown_height > framebuffer->height) {
		skip_y = (shown_height - framebuffer->height) / 2U;
		shown_height = framebuffer->height;
	}

	/* Its pixels, from its top-left corner in the middle of the screen. */
	origin_x = (framebuffer->width - shown_width) / 2U;
	origin_y = (framebuffer->height - shown_height) / 2U;
	for (y = 0; y < shown_height; y++) {
		for (x = 0; x < shown_width; x++) {
			pixels[(uint64_t)(origin_y + y) * framebuffer->stride + origin_x + x] =
			    logo_pixel(framebuffer, image + ((size_t)(y + skip_y) * width + x + skip_x) * 3U);
		}
	}

	/* Succeeded: the logo is on the screen. */
	__asm__ volatile("sfence" : : : "memory");
	return 1;
}

/* Reads one decimal number of a PPM header after whitespace and comments. */
static int
logo_header_number(
	const uint8_t *data,
	size_t size,
	size_t *at,
	uint32_t *number)
{
	uint32_t value;
	unsigned digits;

	/* The whitespace and comments before it. */
	logo_skip_space(data, size, at);

	/* Its digits, bounded. */
	value = 0U;
	digits = 0U;
	while (*at < size && data[*at] >= '0' && data[*at] <= '9') {
		if (digits >= 6U)
			return -1;
		value = value * 10U + (uint32_t)(data[*at] - '0');
		digits++;
		(*at)++;
	}

	/* A number has at least one digit. */
	if (digits == 0U)
		return -1;

	/* Succeeded: the number. */
	*number = value;
	return 0;
}

/* Steps over whitespace and # comments in a PPM header. */
static void
logo_skip_space(
	const uint8_t *data,
	size_t size,
	size_t *at)
{
	/* Blanks, and comments to their line's end. */
	while (*at < size) {
		if (data[*at] == '#') {
			while (*at < size && data[*at] != '\n')
				(*at)++;
			continue;
		}

		/* Anything but a blank is where the next field starts. */
		if (data[*at] != ' ' && data[*at] != '\t' && data[*at] != '\r' && data[*at] != '\n')
			return;
		(*at)++;
	}
}

/* Packs an RGB triple the way the framebuffer lays its pixels out. */
static uint32_t
logo_pixel(
	const struct zbl6_framebuffer *framebuffer,
	const uint8_t *rgb)
{
	uint32_t red;
	uint32_t green;
	uint32_t blue;

	/* The three channels. */
	red = rgb[0];
	green = rgb[1];
	blue = rgb[2];

	/* RGBX keeps red in the low byte. */
	if (framebuffer->format == ZBL6_FRAMEBUFFER_RGBX8888)
		return red | (green << 8) | (blue << 16);

	/* BGRX keeps blue in the low byte. */
	return blue | (green << 8) | (red << 16);
}
