/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The BIOS loader's boot logo (ws035-p096 follow-up).
 *
 * The loader in real mode reads the logo file (logo=PATH, a binary PPM like
 * the UEFI loader's) one 512-byte sector at a time; each sector is fed here,
 * and the pixels it completes come back as framebuffer writes (a byte offset
 * into the linear framebuffer and a pixel) that the loader stores through
 * its flat GS segment.  The first pixel's colour is the background the whole
 * screen is filled with first, and the logo is drawn in the middle (its
 * middle part when it is larger than the screen).
 */

#include "logo.h"

#include <stddef.h>

_Static_assert(offsetof(struct zbl_bios_logo, fill_now) == ZBL_BIOS_LOGO_FILL_NOW, "logo fill_now offset");
_Static_assert(offsetof(struct zbl_bios_logo, background) == ZBL_BIOS_LOGO_BACKGROUND, "logo background offset");
_Static_assert(offsetof(struct zbl_bios_logo, count) == ZBL_BIOS_LOGO_COUNT, "logo count offset");
_Static_assert(offsetof(struct zbl_bios_logo, offsets) == ZBL_BIOS_LOGO_OFFSETS, "logo offsets offset");
_Static_assert(offsetof(struct zbl_bios_logo, pixels) == ZBL_BIOS_LOGO_PIXELS, "logo pixels offset");
_Static_assert(sizeof(struct zbl_bios_logo) == ZBL_BIOS_LOGO_SIZE, "logo size");

/* The largest logo side the loader draws. */
#define LOGO_SIDE_MAX		4096U

/* What the decoder expects next. */
#define LOGO_STAGE_MAGIC_P	0U
#define LOGO_STAGE_MAGIC_6	1U
#define LOGO_STAGE_HEADER	2U
#define LOGO_STAGE_PIXELS	3U
#define LOGO_STAGE_DONE		4U
#define LOGO_STAGE_BAD		5U

/* The framebuffer's pixel layout with red in the low byte (ZBL6_FRAMEBUFFER_RGBX8888). */
#define LOGO_FORMAT_RGBX	1U

static void logo_header_byte(struct zbl_bios_logo *logo, uint8_t byte);
static void logo_header_done(struct zbl_bios_logo *logo);
static void logo_pixel_byte(struct zbl_bios_logo *logo, uint8_t byte);
static int logo_space(uint8_t byte);

/*
 * Starts decoding a logo for a framebuffer of a size, a stride in pixels
 * and a pixel layout.
 */
void
zbl_bios_logo_begin(
	struct zbl_bios_logo *logo,
	uint32_t screen_width,
	uint32_t screen_height,
	uint32_t stride,
	uint32_t format)
{
	uint8_t *bytes;
	uint32_t index;

	/* Everything starts at zero (no string functions in the loader). */
	bytes = (uint8_t *)logo;
	for (index = 0; index < sizeof(*logo); index++)
		bytes[index] = 0;

	/* The screen. */
	logo->stage = LOGO_STAGE_MAGIC_P;
	logo->screen_width = screen_width;
	logo->screen_height = screen_height;
	logo->stride = stride;
	logo->format = format;
}

/*
 * Feeds the bytes of one sector of the file.
 *
 * logo->count writes (and logo->fill_now) come back.  Returns 0 while more
 * of the file is wanted, 1 when the logo is complete, and -1 when the file
 * is not a PPM the loader draws.
 */
int
zbl_bios_logo_feed(
	struct zbl_bios_logo *logo,
	const uint8_t *bytes,
	uint32_t size)
{
	uint32_t index;

	/* No writes yet from this sector. */
	logo->count = 0;

	/* Each byte where the decoder is. */
	for (index = 0; index < size; index++) {
		if (logo->stage == LOGO_STAGE_MAGIC_P) {
			/* The magic's P. */
			logo->stage = LOGO_STAGE_BAD;
			if (bytes[index] == 'P')
				logo->stage = LOGO_STAGE_MAGIC_6;
		} else if (logo->stage == LOGO_STAGE_MAGIC_6) {
			/* The magic's 6. */
			logo->stage = LOGO_STAGE_BAD;
			if (bytes[index] == '6')
				logo->stage = LOGO_STAGE_HEADER;
		} else if (logo->stage == LOGO_STAGE_HEADER) {
			/* The width, the height and the maximum value. */
			logo_header_byte(logo, bytes[index]);
		} else if (logo->stage == LOGO_STAGE_PIXELS) {
			/* A byte of a pixel. */
			logo_pixel_byte(logo, bytes[index]);
		}

		/* The end, good or bad. */
		if (logo->stage == LOGO_STAGE_DONE || logo->stage == LOGO_STAGE_BAD)
			break;
	}

	/* A file that is not a PPM the loader draws. */
	if (logo->stage == LOGO_STAGE_BAD)
		return -1;

	/* The whole logo is there. */
	if (logo->stage == LOGO_STAGE_DONE)
		return 1;

	/* Succeeded: more of the file is wanted. */
	return 0;
}

/* Takes one byte of the header: numbers between blanks and # comments. */
static void
logo_header_byte(
	struct zbl_bios_logo *logo,
	uint8_t byte)
{
	int blank;

	/* A comment runs to the line's end. */
	if (logo->in_comment) {
		if (byte == '\n')
			logo->in_comment = 0;
		return;
	}

	/* A digit adds to the number, at most six of them. */
	if (byte >= '0' && byte <= '9') {
		if (logo->digits >= 6U) {
			logo->stage = LOGO_STAGE_BAD;
			return;
		}

		/* The digit on the right. */
		logo->numbers[logo->number_index] = logo->numbers[logo->number_index] * 10U + (uint32_t)(byte - '0');
		logo->digits++;
		return;
	}

	/* Anything else but a blank or a comment is not a header. */
	blank = logo_space(byte);
	if (!blank && byte != '#') {
		logo->stage = LOGO_STAGE_BAD;
		return;
	}

	/* A comment starts between the numbers only. */
	if (byte == '#' && logo->digits == 0U) {
		logo->in_comment = 1;
		return;
	}

	/* A blank ends a number (the one after the third is the header's last byte). */
	if (logo->digits != 0U) {
		logo->digits = 0;
		logo->number_index++;
		if (logo->number_index == 3U)
			logo_header_done(logo);
	}
}

/* Checks the header and works out where the logo goes on the screen. */
static void
logo_header_done(
	struct zbl_bios_logo *logo)
{
	uint32_t shown_width;
	uint32_t shown_height;

	/* 255 is the only maximum value, and the sides are bounded. */
	logo->width = logo->numbers[0];
	logo->height = logo->numbers[1];
	if (logo->numbers[2] != 255U || logo->width == 0U || logo->height == 0U ||
	    logo->width > LOGO_SIDE_MAX || logo->height > LOGO_SIDE_MAX) {
		logo->stage = LOGO_STAGE_BAD;
		return;
	}

	/* The part of the logo on the screen, in its middle. */
	shown_width = logo->width;
	shown_height = logo->height;
	if (shown_width > logo->screen_width) {
		logo->skip_x = (shown_width - logo->screen_width) / 2U;
		shown_width = logo->screen_width;
	}

	/* The same for its height. */
	if (shown_height > logo->screen_height) {
		logo->skip_y = (shown_height - logo->screen_height) / 2U;
		shown_height = logo->screen_height;
	}

	/* Where its top-left pixel on the screen is; the pixels come next. */
	logo->origin_x = (logo->screen_width - shown_width) / 2U;
	logo->origin_y = (logo->screen_height - shown_height) / 2U;
	logo->stage = LOGO_STAGE_PIXELS;
}

/* Takes one byte of a pixel; a whole pixel on the screen becomes a write. */
static void
logo_pixel_byte(
	struct zbl_bios_logo *logo,
	uint8_t byte)
{
	uint32_t pixel;
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t row;
	uint32_t column;

	/* The pixel's red, green and blue. */
	logo->channel[logo->channels] = byte;
	logo->channels++;
	if (logo->channels < 3U)
		return;
	logo->channels = 0;

	/* In the framebuffer's layout. */
	red = logo->channel[0];
	green = logo->channel[1];
	blue = logo->channel[2];
	pixel = blue | (green << 8) | (red << 16);
	if (logo->format == LOGO_FORMAT_RGBX)
		pixel = red | (green << 8) | (blue << 16);

	/* The first pixel's colour is the background, filled before any write. */
	if (logo->x == 0U && logo->y == 0U) {
		logo->background = pixel;
		logo->fill_now = 1;
	}

	/* A pixel inside the part on the screen is written. */
	if (logo->x >= logo->skip_x && logo->x - logo->skip_x < logo->screen_width &&
	    logo->y >= logo->skip_y && logo->y - logo->skip_y < logo->screen_height &&
	    logo->count < (uint32_t)ZBL_BIOS_LOGO_WRITES) {
		row = logo->origin_y + logo->y - logo->skip_y;
		column = logo->origin_x + logo->x - logo->skip_x;
		logo->offsets[logo->count] = (row * logo->stride + column) * 4U;
		logo->pixels[logo->count] = pixel;
		logo->count++;
	}

	/* The next pixel, and the end after the last row. */
	logo->x++;
	if (logo->x == logo->width) {
		logo->x = 0;
		logo->y++;
		if (logo->y == logo->height)
			logo->stage = LOGO_STAGE_DONE;
	}
}

/* Reports whether a byte is a blank of a PPM header. */
static int
logo_space(
	uint8_t byte)
{
	/* The four blanks. */
	if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n')
		return 1;

	/* Anything else. */
	return 0;
}
