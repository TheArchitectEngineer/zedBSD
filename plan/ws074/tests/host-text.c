/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p010: the host test of browser's text: fonts, glyphs,
 * the fallback, bold and line breaking.
 *
 *   host-text SANS MONO FALLBACK
 *
 * (the zdesktop fonts: build/ws035-fonts/Mahora-Regular.ttf, JetBrainsMono-Regular.ttf,
 * DroidSansFallbackFull.ttf).  Prints one line per failed check and a summary.
 */

#include "text/text.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

static void check(int condition, const char *what);
static int width_of(struct text_system *system, const struct text_font *font, const char *ascii);

int
main(
	int argc,
	char **argv)
{
	struct text_system system;
	struct text_font_paths paths;
	struct text_font regular;
	struct text_font bold;
	struct text_font mono;
	struct text_font large;
	struct text_metrics metrics;
	struct text_glyph glyph;
	int error;
	int covered;
	int x;

	if (argc < 4) {
		fprintf(stderr, "usage: host-text SANS MONO FALLBACK\n");
		return 2;
	}
	paths.sans = argv[1];
	paths.mono = argv[2];
	paths.fallback = argv[3];
	error = text_system_open(&system, &paths);
	check(error == 0, "fonts open");
	if (error != 0)
		return 1;

	text_select_font(&system, 0, 16.0f, 400, &regular);
	text_select_font(&system, 0, 16.4f, 700, &bold);
	text_select_font(&system, 1, 16.0f, 400, &mono);
	text_select_font(&system, 0, 32.0f, 400, &large);
	check(regular.pixels == 16 && regular.bold == 0 && regular.face == TEXT_FACE_SANS, "select: regular 16px sans");
	check(bold.pixels == 16 && bold.bold == 1, "select: bold rounds to 16px");
	check(mono.face == TEXT_FACE_MONO, "select: monospace face");

	error = text_font_metrics(&system, &regular, &metrics);
	check(error == 0 && metrics.ascent > 10 && metrics.ascent < 20 && metrics.descent > 0 && metrics.line_height >= 16,
	    "metrics: 16px ascent, descent, line height");
	printf("host-text: 16px sans: ascent %d descent %d line height %d\n", metrics.ascent, metrics.descent, metrics.line_height);

	printf("host-text: width of \"Hello\": regular %d bold %d mono %d large %d\n",
	    width_of(&system, &regular, "Hello"), width_of(&system, &bold, "Hello"),
	    width_of(&system, &mono, "Hello"), width_of(&system, &large, "Hello"));
	check(width_of(&system, &regular, "Hello") > 25 && width_of(&system, &regular, "Hello") < 60, "width: Hello at 16px");
	check(width_of(&system, &bold, "Hello") == width_of(&system, &regular, "Hello") + 5, "width: bold adds a pixel a glyph");
	check(width_of(&system, &mono, "iiiii") == width_of(&system, &mono, "MMMMM"), "width: monospace is fixed");
	check(width_of(&system, &large, "Hello") > width_of(&system, &regular, "Hello") * 3 / 2, "width: 32px is wider");

	error = text_glyph(&system, &regular, 0x3042U, 1, &glyph);
	check(error == 0 && glyph.face == TEXT_FACE_FALLBACK && glyph.index != 0, "fallback: あ comes from the fallback face");
	check(glyph.bitmap != NULL && glyph.width > 4 && glyph.height > 4, "fallback: あ has a bitmap");
	error = text_glyph(&system, &regular, 'H', 1, &glyph);
	covered = 0;
	for (x = 0; glyph.bitmap != NULL && x < glyph.width * glyph.height; x++) {
		if (glyph.bitmap[x] > 128)
			covered++;
	}
	check(error == 0 && glyph.face == TEXT_FACE_SANS && covered > 10, "glyph: H is drawn");
	error = text_glyph(&system, &regular, ' ', 1, &glyph);
	check(error == 0 && glyph.advance > 0, "glyph: space has an advance");

	check(text_break_between(' ', 'a') == 1, "break: after a space");
	check(text_break_between('a', 'b') == 0, "break: not inside a word");
	check(text_break_between(0x3042U, 0x3044U) == 1, "break: between kana");
	check(text_break_between(0x3042U, 0x3002U) == 0, "break: not before 。");
	check(text_break_between(0x300cU, 0x3042U) == 0, "break: not after 「");
	check(text_break_between('a', 0x65e5U) == 1, "break: between Latin and an ideograph");
	check(text_is_space('\n') && !text_is_space('a'), "space: line feed and a letter");

	text_system_close(&system);
	printf("host-text: %d checks, %d failed\n", checks, failures);
	if (failures != 0)
		return 1;
	return 0;
}

static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (!condition) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static int
width_of(
	struct text_system *system,
	const struct text_font *font,
	const char *ascii)
{
	struct text_glyph glyph;
	int width;

	width = 0;
	for (; *ascii != '\0'; ascii++) {
		if (text_glyph(system, font, (unsigned char)*ascii, 0, &glyph) == 0)
			width += glyph.advance;
	}
	return width;
}
