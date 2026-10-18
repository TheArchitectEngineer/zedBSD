/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Verifies zero logical font metrics and coverage through the real font API. */

#include "text/text.h"

#include <stdio.h>

/* Retains independent successful and failed observations until process exit. */
static unsigned checks;

/* Counts failures without suppressing later independent font observations. */
static unsigned failures;

static void zero_check(int condition, const char *name);
static int zero_case(struct text_system *system, int monospace, int weight, uint32_t code_point);

/*
 * Compares empty glyphs and live positive-size glyphs with the same real fonts.
 */
int
main(
	void)
{
	struct text_system system;
	struct text_font_paths paths = {
	    "userland/desktop/fonts/Mahora-Regular.ttf",
	    "userland/desktop/fonts/JetBrainsMono-Regular.ttf",
	    "userland/desktop/fonts/DroidSansFallbackFull.ttf"};
	struct text_font positive;
	struct text_glyph before;
	struct text_glyph after;
	int error;
	int printed;

	/* Opens ordinary checked-in fonts before any native glyph query. */
	error = text_system_open(&system, &paths);
	if (error != 0) {
		text_system_close(&system);
		return 2;
	}

	/* Keeps a positive glyph live across the independent zero-size requests. */
	text_select_font(&system, 0, 16.0f, 400, &positive);
	error = text_glyph(&system, &positive, 'M', 1, &before);
	if (error != 0) {
		text_system_close(&system);
		return 2;
	}

	/* Zero regular glyphs must not gain the rasterizer's minimum-size advance. */
	error = zero_case(&system, 0, 400, 'M');
	zero_check(error == 0, "regular zero glyph query");

	/* Synthetic bold must not add its positive-size widening to an empty glyph. */
	error = zero_case(&system, 0, 700, 'M');
	zero_check(error == 0, "bold zero glyph query");

	/* A fallback character has the same no-advance, no-coverage contract. */
	error = zero_case(&system, 0, 400, 0x4e00U);
	zero_check(error == 0, "fallback zero glyph query");

	/* Missing characters in a monospaced selection must also stay empty. */
	error = zero_case(&system, 1, 400, 0x10ffffU);
	zero_check(error == 0, "missing zero glyph query");

	/* Positive cached glyphs remain usable after zero selections and bitmap requests. */
	error = text_glyph(&system, &positive, 'M', 1, &after);
	if (error != 0) {
		text_system_close(&system);
		return 2;
	}

	/* Compares positive geometry without depending on a particular font's numeric width. */
	zero_check(before.advance_units > 0, "positive glyph has advance");
	zero_check(after.advance_units == before.advance_units, "positive advance preserved");
	zero_check(after.width == before.width, "positive coverage width preserved");
	zero_check(after.height == before.height, "positive coverage height preserved");

	/* Releases the real glyph cache and font files after the final borrowed glyph observation. */
	text_system_close(&system);

	/* Publishes all independent observations after native resources are released. */
	printed = printf("native zero font: %u/%u passed\n", checks - failures, checks);
	if (printed < 0)
		return 2;

	/* Any incorrect empty glyph or changed positive glyph rejects the fixture. */
	if (failures != 0)
		return 1;

	/* Succeeded: zero metrics, empty coverage and positive glyph lifetime agree. */
	return 0;
}

/* Records an independent externally observable font property. */
static void
zero_check(
	int condition,
	const char *name)
{
	int printed;

	/* Keeps later checks running after an incorrect native result. */
	checks++;
	if (!condition) {
		failures++;
		printed = printf("FAIL zero font: %s\n", name);
		if (printed < 0)
			return;
	}

	/* Succeeded: this observation is retained for the process result. */
	return;
}

/* Observes zero metrics and a drawn glyph for one ordinary font selection. */
static int
zero_case(
	struct text_system *system,
	int monospace,
	int weight,
	uint32_t code_point)
{
	struct text_font font;
	struct text_metrics metrics;
	struct text_glyph glyph;
	int average;
	int maximum;
	int error;

	/* Selects CSS logical zero while retaining a safe positive rasterizer request. */
	text_select_font(system, monospace, 0.0f, weight, &font);
	zero_check(font.size == 0.0f, "logical size is zero");
	zero_check(font.pixels > 0U, "rasterizer pixel size is positive");

	/* Reads native line metrics without substituting a separate geometry implementation. */
	error = text_font_metrics(system, &font, &metrics);
	if (error != 0)
		return error;

	/* Zero font metrics cannot enlarge the surrounding line strut. */
	zero_check(metrics.ascent == 0, "zero ascent");
	zero_check(metrics.descent == 0, "zero descent");
	zero_check(metrics.line_height == 0, "zero normal line height");

	/* Natural character widths use the same logical size as inline advances. */
	error = text_font_char_widths(system, &font, &average, &maximum);
	if (error != 0)
		return error;

	/* Empty font character metrics cannot add control content width. */
	zero_check(average == 0, "zero average character width");
	zero_check(maximum == 0, "zero maximum character width");

	/* Requests actual drawable coverage, including bold and fallback selection paths. */
	error = text_glyph(system, &font, code_point, 1, &glyph);
	if (error != 0)
		return error;

	/* Empty glyphs contribute neither whole nor fractional advance or visible coverage. */
	zero_check(glyph.advance == 0, "zero integer advance");
	zero_check(glyph.advance_units == 0, "zero fractional advance");
	zero_check(glyph.width == 0, "zero coverage width");
	zero_check(glyph.height == 0, "zero coverage height");
	zero_check(glyph.bitmap == NULL, "no visible glyph coverage");

	/* Succeeded: the real font API supplied a complete empty glyph. */
	return 0;
}
