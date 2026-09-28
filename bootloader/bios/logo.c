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
 *
 * A picture whose header has the comment "fit=cover" (the Kei boot splash,
 * ws035-p107) is scaled to cover the whole screen instead, its proportions
 * kept and its middle on the middle, by nearest pixels.  The decoder only
 * shrinks it (a pixel of the file makes at most one pixel of the screen, so
 * a sector's writes fit); a picture the screen would have to enlarge is
 * drawn in the middle as before.
 */

#include "logo.h"

#include <stddef.h>

_Static_assert(offsetof(struct zbl_bios_logo, fill_now) == ZBL_BIOS_LOGO_FILL_NOW, "logo fill_now offset");
_Static_assert(offsetof(struct zbl_bios_logo, background) == ZBL_BIOS_LOGO_BACKGROUND, "logo background offset");
_Static_assert(offsetof(struct zbl_bios_logo, count) == ZBL_BIOS_LOGO_COUNT, "logo count offset");
_Static_assert(offsetof(struct zbl_bios_logo, offsets) == ZBL_BIOS_LOGO_OFFSETS, "logo offsets offset");
_Static_assert(offsetof(struct zbl_bios_logo, pixels) == ZBL_BIOS_LOGO_PIXELS, "logo pixels offset");
_Static_assert(offsetof(struct zbl_bios_logo, cover) == ZBL_BIOS_LOGO_TAIL, "logo cover offset");
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
static void logo_comment_end(struct zbl_bios_logo *logo);
static void logo_header_done(struct zbl_bios_logo *logo);
static int logo_cover_fits(struct zbl_bios_logo *logo);
static void logo_pixel_byte(struct zbl_bios_logo *logo, uint8_t byte);
static void logo_cover_writes(struct zbl_bios_logo *logo, uint32_t pixel);
static uint32_t logo_cover_first(uint32_t source, uint32_t scaled, uint32_t size, uint32_t cut);
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

	/* A comment runs to the line's end; its first bytes are kept to be read there. */
	if (logo->in_comment) {
		if (byte == '\n') {
			logo->in_comment = 0;
			logo_comment_end(logo);
			return;
		}

		/* One more byte of it, while there is room. */
		if (logo->comment_length < ZBL_BIOS_LOGO_COMMENT) {
			logo->comment[logo->comment_length] = byte;
			logo->comment_length++;
		}

		/* The comment goes on. */
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
		logo->comment_length = 0;
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

/* Reads a finished header comment: "fit=cover" (blanks around it allowed) asks for a cover. */
static void
logo_comment_end(
	struct zbl_bios_logo *logo)
{
	static const char word[] = "fit=cover";
	uint32_t start;
	uint32_t end;
	uint32_t index;
	int blank;

	/* The comment without the blanks before it. */
	for (start = 0; start < logo->comment_length; start++) {
		blank = logo_space(logo->comment[start]);
		if (!blank)
			break;
	}

	/* Nor the blanks after it. */
	for (end = logo->comment_length; end > start; end--) {
		blank = logo_space(logo->comment[end - 1U]);
		if (!blank)
			break;
	}

	/* The word, whole. */
	if (end - start != sizeof(word) - 1U)
		return;
	for (index = 0; index + 1U < sizeof(word); index++) {
		if (logo->comment[start + index] != (uint8_t)word[index])
			return;
	}

	/* Succeeded: the picture asks to cover the screen. */
	logo->cover = 1;
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

	/* A cover the decoder can draw by shrinking needs no more. */
	if (logo->cover) {
		logo->cover = (uint32_t)logo_cover_fits(logo);
		if (logo->cover) {
			logo->stage = LOGO_STAGE_PIXELS;
			return;
		}
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

/*
 * Works out a cover's size on the screen and the part cut from it; returns
 * 1 when the screen does not enlarge the picture (the cover is drawn), 0
 * otherwise (the picture is centred instead).
 */
static int
logo_cover_fits(
	struct zbl_bios_logo *logo)
{
	/* As wide as the screen when the screen is the wider in proportion, else as tall. */
	if (logo->screen_width * logo->height >= logo->screen_height * logo->width) {
		logo->scaled_width = logo->screen_width;
		logo->scaled_height = logo->height * logo->screen_width / logo->width;
	} else {
		logo->scaled_height = logo->screen_height;
		logo->scaled_width = logo->width * logo->screen_height / logo->height;
	}

	/* An enlarged picture would give a sector more writes than there is room for. */
	if (logo->scaled_width > logo->width || logo->scaled_height > logo->height)
		return 0;

	/* The part cut on the left and at the top (the middle stays). */
	logo->cut_x = (logo->scaled_width - logo->screen_width) / 2U;
	logo->cut_y = (logo->scaled_height - logo->screen_height) / 2U;

	/* Succeeded: the cover is drawn. */
	return 1;
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

	/* A cover's pixel goes to the screen pixels that take it. */
	if (logo->cover) {
		logo_cover_writes(logo, pixel);
	} else if (logo->x == 0U && logo->y == 0U) {
		/* The first pixel's colour is the background, filled before any write. */
		logo->background = pixel;
		logo->fill_now = 1;
	}

	/* A pixel inside the part on the screen is written. */
	if (logo->cover == 0U &&
	    logo->x >= logo->skip_x && logo->x - logo->skip_x < logo->screen_width &&
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

/*
 * Writes a cover's pixel of the file to the screen pixels whose nearest
 * pixel of the file it is (at most one, the picture being shrunk).
 */
static void
logo_cover_writes(
	struct zbl_bios_logo *logo,
	uint32_t pixel)
{
	uint32_t first_x;
	uint32_t end_x;
	uint32_t first_y;
	uint32_t end_y;
	uint32_t row;
	uint32_t column;

	/* The screen's columns and rows that sample this pixel. */
	first_x = logo_cover_first(logo->x, logo->scaled_width, logo->width, logo->cut_x);
	end_x = logo_cover_first(logo->x + 1U, logo->scaled_width, logo->width, logo->cut_x);
	first_y = logo_cover_first(logo->y, logo->scaled_height, logo->height, logo->cut_y);
	end_y = logo_cover_first(logo->y + 1U, logo->scaled_height, logo->height, logo->cut_y);
	if (end_x > logo->screen_width)
		end_x = logo->screen_width;
	if (end_y > logo->screen_height)
		end_y = logo->screen_height;

	/* Each of them, while the sector's writes have room. */
	for (row = first_y; row < end_y; row++) {
		for (column = first_x; column < end_x; column++) {
			if (logo->count >= (uint32_t)ZBL_BIOS_LOGO_WRITES)
				return;
			logo->offsets[logo->count] = (row * logo->stride + column) * 4U;
			logo->pixels[logo->count] = pixel;
			logo->count++;
		}
	}
}

/*
 * The first screen column (or row) whose nearest pixel of the file is at
 * or after a source column: the smallest d with (2(d + cut) + 1) size >=
 * 2 source scaled, the part cut taken off (0 when it falls before the
 * screen).
 */
static uint32_t
logo_cover_first(
	uint32_t source,
	uint32_t scaled,
	uint32_t size,
	uint32_t cut)
{
	uint32_t wanted;
	uint32_t first;

	/* Nothing before the first pixel. */
	if (source == 0U)
		return 0U;

	/* The smallest place d + cut, rounded up. */
	wanted = 2U * source * scaled;
	first = 0U;
	if (wanted > size)
		first = (wanted - size + 2U * size - 1U) / (2U * size);

	/* On the screen, after the part cut. */
	if (first <= cut)
		return 0U;

	/* Succeeded: the screen's column or row. */
	return first - cut;
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
