/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p002b: the host test of the characters a font's codes stand for
 * (libpdf's font.c pdf_font_unicode), on make-edit-samples.py's page 6 and
 * the host's DejaVu Sans:
 *
 *   host-font-unicode FILE FONT
 *
 * F1 (Helvetica, WinAnsiEncoding): A and the euro by the encoding; F2 (the
 * same with a /ToUnicode CMap): <41> is Z by the CMap, <42> B by the
 * encoding under it; F3 (DejaVu Sans, Identity-H, no /ToUnicode): the
 * glyph of A is A by the program's character map read backward, glyph 0
 * U+FFFD.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>
#include <truetype.h>

#include "internal.h"

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static struct pdf_font *page_font(struct pdf_document *document, const char *name);
static uint32_t first(struct pdf_document *document, struct pdf_font *font, unsigned code, int single_byte);
static unsigned dejavu_glyph(const char *path, unsigned character);

int
main(
	int argc,
	char **argv)
{
	struct pdf_document *document;
	struct pdf_font *font;
	unsigned glyph;
	int error;

	/* The sample. */
	if (argc != 3)
		return 2;
	error = pdf_document_open(argv[1], &document);
	check(error == 0, "open edit-images.pdf");
	if (error != 0)
		return 1;

	/* F1: the encoding. */
	font = page_font(document, "F1");
	check(font != NULL, "F1 read");
	if (font != NULL) {
		check(first(document, font, 0x41U, 1) == 0x41U, "F1 <41> is A");
		check(first(document, font, 0x80U, 1) == 0x20acU, "F1 <80> is the euro (WinAnsi)");
	}

	/* F2: the CMap over the encoding. */
	font = page_font(document, "F2");
	check(font != NULL, "F2 read");
	if (font != NULL) {
		check(first(document, font, 0x41U, 1) == 0x5aU, "F2 <41> is Z (the CMap)");
		check(first(document, font, 0x42U, 1) == 0x42U, "F2 <42> is B (the encoding under the CMap)");
	}

	/* F3: the program's character map read backward. */
	font = page_font(document, "F3");
	glyph = dejavu_glyph(argv[2], 0x41U);
	check(font != NULL && glyph != 0U, "F3 read, and the glyph of A in DejaVu Sans");
	if (font != NULL && glyph != 0U) {
		check(first(document, font, glyph, 0) == 0x41U, "F3 the glyph of A is A");
		check(first(document, font, 0U, 0) == 0xfffdU, "F3 glyph 0 is U+FFFD");
	}

	/* The summary. */
	pdf_document_close(document);
	printf("host-font-unicode: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Reads a font of page 6's resources by its name; NULL when it cannot. */
static struct pdf_font *
page_font(
	struct pdf_document *document,
	const char *name)
{
	struct pdf_object *page;
	struct pdf_object *resources;
	struct pdf_object *fonts;
	struct pdf_object *dictionary;
	struct pdf_font *font;
	int error;

	/* The page's Font dictionary, and the font's. */
	error = pdf_reader_page(document, 5, &page, &resources);
	if (error == 0)
		error = pdf_reader_resolve_key(document, resources, "Font", &fonts);
	if (error == 0)
		error = pdf_reader_resolve_key(document, fonts, name, &dictionary);
	if (error != 0)
		return NULL;

	/* The font as the reader reads it. */
	error = pdf_font_get(document, dictionary, &font);
	if (error != 0)
		return NULL;
	return font;
}

/* Gives the first character a code stands for (0 on a failure). */
static uint32_t
first(
	struct pdf_document *document,
	struct pdf_font *font,
	unsigned code,
	int single_byte)
{
	uint32_t characters[4];
	size_t count;
	int error;

	/* The characters. */
	error = pdf_font_unicode(document, font, code, single_byte, characters, 4, &count);
	if (error != 0 || count == 0)
		return 0U;
	return characters[0];
}

/* Finds a character's glyph in the font file (0 when it cannot). */
static unsigned
dejavu_glyph(
	const char *path,
	unsigned character)
{
	struct truetype_face *face;
	unsigned char *data;
	unsigned glyph;
	FILE *file;
	long length;
	size_t got;
	int error;

	/* The file's bytes. */
	file = fopen(path, "rb");
	if (file == NULL)
		return 0U;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	data = malloc((size_t)length);
	got = 0;
	if (data != NULL)
		got = fread(data, 1, (size_t)length, file);
	fclose(file);
	if (data == NULL || got != (size_t)length) {
		free(data);
		return 0U;
	}

	/* The face, and the character's glyph. */
	glyph = 0U;
	error = truetype_open(data, got, 0, &face);
	if (error == 0) {
		glyph = truetype_glyph_index(face, character);
		truetype_close(face);
	}
	free(data);
	return glyph;
}

/* Counts and prints one check. */
static void
check(
	int ok,
	const char *what)
{
	/* A check that holds. */
	if (ok) {
		test_passed++;
		printf("ok   %s\n", what);
		return;
	}

	/* One that does not. */
	test_failed++;
	printf("FAIL %s\n", what);
}
