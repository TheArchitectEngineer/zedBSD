/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws102-p019: the colour glyphs on the host: libtruetype's
 * truetype_color_glyph over Noto Color Emoji (CBDT) and
 * userland/desktop/picture/color-glyph.c's decoding and scaling.
 *
 *   host-emoji EMOJI-FONT TEXT-FONT
 */

#include "userland/desktop/picture/color-glyph.h"
#include "userland/desktop/wayland/keyboard.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);
static unsigned char *load(const char *path, size_t *size);
static void channels(uint32_t pixel, unsigned *red, unsigned *green, unsigned *blue, unsigned *alpha);
static int emoji_point(const char *text, uint32_t *point);
static unsigned emoji_missing(struct truetype_face *face);

/* Runs the checks. */
int
main(
	int argc,
	char **argv)
{
	struct truetype_color_glyph found;
	struct keiland_color_image image;
	struct truetype_face *face;
	struct truetype_face *text;
	unsigned char *bytes;
	unsigned char *copy;
	size_t size;
	size_t text_size;
	unsigned char *text_bytes;
	unsigned glyph;
	unsigned red;
	unsigned green;
	unsigned blue;
	unsigned alpha;
	uint32_t offset;
	unsigned missing;
	unsigned index;
	int differs;
	int error;

	/* The two fonts. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-emoji EMOJI-FONT TEXT-FONT\n");
		return 2;
	}

	/* Their bytes. */
	bytes = load(argv[1], &size);
	text_bytes = load(argv[2], &text_size);
	if (bytes == NULL || text_bytes == NULL) {
		printf("host-emoji: FAIL (fonts)\n");
		return 1;
	}

	/* A face of colour bitmaps alone opens, and maps emoji but not letters. */
	error = truetype_open(bytes, size, 0U, &face);
	check(error == 0, "the emoji font opens (CBDT, no outlines)");
	if (error != 0)
		return 1;
	glyph = truetype_glyph_index(face, 0x1f600U);
	check(glyph != 0U, "U+1F600 has a glyph");
	check(truetype_glyph_index(face, 0x41U) == 0U, "the letter A has none");

	/* Its colour image at the strike: a PNG, 136 by 128 at 109 pixels per em. */
	error = truetype_set_pixel_size(face, 109U);
	error |= truetype_color_glyph(face, glyph, &found);
	check(error == 0 && found.png_size > 8U && memcmp(found.png, "\x89PNG", 4U) == 0, "U+1F600's image is a PNG");
	check(found.width == 136U && found.height == 128U && found.ppem == 109U, "... 136x128 at 109 ppem");
	printf("  left=%d top=%d advance=%d png=%lu\n", found.left, found.top, found.advance, (unsigned long)found.png_size);

	/* Decoded and scaled to 32 pixels: about 40 by 38, a yellow face, clear corners. */
	error = kl_color_glyph(face, glyph, 32U, &image);
	check(error == 0 && image.width == 40 && image.height == 38, "at 32 px it is 40x38");
	if (error == 0) {
		channels(image.pixels[(size_t)(image.height / 2) * (size_t)image.width + (size_t)(image.width / 4)], &red, &green, &blue, &alpha);
		printf("  32 px: left=%d top=%d advance=%d middle-left=%u,%u,%u,%u\n", image.left, image.top, image.advance, red, green, blue, alpha);
		check(alpha == 255U && red > 200U && green > 140U && blue < 100U, "... its face is opaque yellow");
		channels(image.pixels[0], &red, &green, &blue, &alpha);
		check(alpha == 0U && red == 0U && green == 0U && blue == 0U, "... its corner is clear (premultiplied)");
		check(image.advance > 30 && image.advance < 50 && image.top > 20 && image.top <= 40, "... its advance and top fit the size");
		free(image.pixels);
	}

	/* The red heart at 20 pixels: red over green and blue. */
	glyph = truetype_glyph_index(face, 0x2764U);
	error = kl_color_glyph(face, glyph, 20U, &image);
	check(glyph != 0U && error == 0, "U+2764 at 20 px");
	if (error == 0) {
		channels(image.pixels[(size_t)(image.height / 2) * (size_t)image.width + (size_t)(image.width / 2)], &red, &green, &blue, &alpha);
		printf("  heart middle=%u,%u,%u,%u (%dx%d)\n", red, green, blue, alpha, image.width, image.height);
		check(alpha > 200U && red > 150U && green < 100U && blue < 100U, "... its middle is red");
		free(image.pixels);
	}

	/* Glyph 0 (no image in the index) is refused. */
	error = truetype_color_glyph(face, 0U, &found);
	check(error == ENOENT || error == 0, "glyph 0: ENOENT or an image");

	/* Every emoji of the keyboard's emoji face (ws102-p022) has a colour image. */
	missing = emoji_missing(face);
	check(missing == 0U, "every emoji of the keyboard's emoji face has a colour glyph");
	truetype_close(face);

	/* A text font has no colour glyphs: ENOENT, and it opens and draws as before. */
	error = truetype_open(text_bytes, text_size, 0U, &text);
	check(error == 0, "the text font opens");
	if (error == 0) {
		error = truetype_color_glyph(text, truetype_glyph_index(text, 0x41U), &found);
		check(error == ENOENT, "the text font's A has no colour image (ENOENT)");
		truetype_close(text);
	}

	/* A CBLC whose count of strikes runs past the table is refused (EINVAL), the face still opening. */
	copy = malloc(size);
	if (copy != NULL) {
		memcpy(copy, bytes, size);
		offset = 0;
		for (index = 0; index < ((unsigned)copy[4] << 8 | copy[5]); index++) {
			differs = memcmp(copy + 12U + index * 16U, "CBLC", 4U);
			if (differs != 0)
				continue;
			offset = (uint32_t)copy[12U + index * 16U + 8U] << 24 | (uint32_t)copy[12U + index * 16U + 9U] << 16;
			offset |= (uint32_t)copy[12U + index * 16U + 10U] << 8 | copy[12U + index * 16U + 11U];
		}

		/* Its count of strikes made huge. */
		memset(copy + offset + 4U, 0xff, 4U);
		error = truetype_open(copy, size, 0U, &face);
		check(offset != 0U && error == 0, "a damaged CBLC: the face opens");
		if (error == 0) {
			error = truetype_color_glyph(face, truetype_glyph_index(face, 0x1f600U), &found);
			check(error == EINVAL, "... and its colour glyph is refused (EINVAL)");
			truetype_close(face);
		}

		/* The copy is done with. */
		free(copy);
	}

	/* The outcome. */
	free(bytes);
	free(text_bytes);
	if (failures != 0) {
		printf("host-emoji: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-emoji: PASS\n");
	return 0;
}

/* Prints a check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *text)
{
	/* A failure is counted. */
	if (!condition) {
		printf("FAILED: %s\n", text);
		failures++;
		return;
	}

	/* A check that held. */
	printf("ok: %s\n", text);
}

/* Reads a whole file; NULL when it cannot. */
static unsigned char *
load(
	const char *path,
	size_t *size)
{
	unsigned char *bytes;
	size_t read;
	FILE *file;
	long length;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Its bytes. */
	bytes = malloc((size_t)length);
	if (bytes != NULL) {
		read = fread(bytes, 1U, (size_t)length, file);
		if (read != (size_t)length) {
			free(bytes);
			bytes = NULL;
		}
	}

	/* The file is done with. */
	fclose(file);
	*size = (size_t)length;
	return bytes;
}

/* Splits a premultiplied 0xAARRGGBB pixel. */
static void
channels(
	uint32_t pixel,
	unsigned *red,
	unsigned *green,
	unsigned *blue,
	unsigned *alpha)
{
	*alpha = pixel >> 24;
	*red = (pixel >> 16) & 0xffU;
	*green = (pixel >> 8) & 0xffU;
	*blue = pixel & 0xffU;
}

/* Decodes the one code point of an emoji (UTF-8, two to four bytes).  Returns 1 with it, or 0 for anything else. */
static int
emoji_point(
	const char *text,
	uint32_t *point)
{
	const unsigned char *byte;
	unsigned length;
	unsigned index;
	uint32_t value;

	/* The lead byte gives the length and the first bits. */
	byte = (const unsigned char *)text;
	length = 0U;
	value = 0U;
	if ((byte[0] & 0xe0U) == 0xc0U) {
		length = 2U;
		value = byte[0] & 0x1fU;
	} else if ((byte[0] & 0xf0U) == 0xe0U) {
		length = 3U;
		value = byte[0] & 0x0fU;
	} else if ((byte[0] & 0xf8U) == 0xf0U) {
		length = 4U;
		value = byte[0] & 0x07U;
	}

	/* Not a lead byte of a character above ASCII. */
	if (length == 0U)
		return 0;

	/* The continuation bytes. */
	for (index = 1U; index < length; index++) {
		if ((byte[index] & 0xc0U) != 0x80U)
			return 0;
		value = value << 6 | (byte[index] & 0x3fU);
	}

	/* Exactly one character. */
	if (byte[length] != '\0')
		return 0;

	/* Succeeded: the code point. */
	*point = value;
	return 1;
}

/* Counts the emoji of the keyboard's emoji face that have no colour image in the font (printing each). */
static unsigned
emoji_missing(
	struct truetype_face *face)
{
	struct truetype_color_glyph found;
	const char *text;
	unsigned category;
	unsigned count;
	unsigned index;
	unsigned missing;
	unsigned glyph;
	uint32_t point;
	int decoded;
	int error;

	/* Each emoji of each category. */
	missing = 0U;
	for (category = 0U; category < KWL_EMOJI_CATEGORIES; category++) {
		count = kwl_emoji_count(category);
		for (index = 0U; index < count; index++) {
			/* Its code point and glyph. */
			text = kwl_emoji(category, index);
			point = 0U;
			decoded = 0;
			if (text != NULL)
				decoded = emoji_point(text, &point);
			glyph = 0U;
			if (decoded)
				glyph = truetype_glyph_index(face, point);

			/* Its colour image. */
			error = ENOENT;
			if (glyph != 0U)
				error = truetype_color_glyph(face, glyph, &found);
			if (error != 0) {
				printf("  emoji without a colour glyph: category %u index %u U+%04X\n", category, index, (unsigned)point);
				missing++;
			}
		}
	}

	/* The count. */
	return missing;
}
