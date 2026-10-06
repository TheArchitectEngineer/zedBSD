/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the handwriting face's recognition in the compositor
 * (ws165-p003, userland/desktop/wayland/keyboard-hand.c): the templates
 * of hand-hershey read by kwl_hand_load, a character written as ink (a
 * template's strokes drawn at a size on a writing area of 300 pixels) and
 * kwl_hand_recognize's candidates: あ is あ; c written large is C first
 * and c next, written small c first; や written small offers ゃ first;
 * a voiced kana (が) keeps its mark.
 *
 *   host-hand-keyboard TEMPLATES
 */

#include "keyboard.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The writing area's height, in pixels. */
#define TEST_AREA	300

/* The checks that failed. */
static unsigned failures;

/* The templates' text. */
static char test_text[1024U * 1024U];

static void check(int condition, const char *what);
static int write_glyph(const char *code, int size, struct kwl_hand_ink *ink);

/*
 * Runs the checks; the exit status says whether they all held.
 */
int
main(
	int argc,
	char **argv)
{
	static struct kwl_hand_ink ink;
	struct kwl_hand_result result;
	FILE *file;
	size_t length;
	int error;

	/* The templates, read by the face's loader and by the test. */
	if (argc < 2) {
		fprintf(stderr, "usage: host-hand-keyboard TEMPLATES\n");
		return 2;
	}

	/* Read by the face's loader, then for the test. */
	error = kwl_hand_load(argv[1]);
	check(error == 0, "the templates read");
	file = fopen(argv[1], "r");
	if (file == NULL)
		return 2;
	length = fread(test_text, 1U, sizeof(test_text) - 1U, file);
	fclose(file);
	test_text[length] = '\0';

	/* あ written at two thirds of the area. */
	error = write_glyph("U+3042", 200, &ink);
	kwl_hand_recognize(&ink, TEST_AREA, &result);
	check(error == 0 && result.count > 0U && strcmp(result.candidates[0], "あ") == 0, "あ is あ");

	/* c written large: C, then c. */
	error = write_glyph("U+0063", 200, &ink);
	kwl_hand_recognize(&ink, TEST_AREA, &result);
	check(error == 0 && result.count > 1U && strcmp(result.candidates[0], "C") == 0 && strcmp(result.candidates[1], "c") == 0, "c written large: C, then c");

	/* c written small: c first. */
	error = write_glyph("U+0063", 60, &ink);
	kwl_hand_recognize(&ink, TEST_AREA, &result);
	check(error == 0 && result.count > 1U && strcmp(result.candidates[0], "c") == 0, "c written small: c first");

	/* や written small: ゃ first. */
	error = write_glyph("U+3084", 60, &ink);
	kwl_hand_recognize(&ink, TEST_AREA, &result);
	check(error == 0 && result.count > 1U && strcmp(result.candidates[0], "ゃ") == 0 && strcmp(result.candidates[1], "や") == 0, "や written small: ゃ, then や");

	/* が keeps its mark. */
	error = write_glyph("U+304C", 200, &ink);
	kwl_hand_recognize(&ink, TEST_AREA, &result);
	check(error == 0 && result.count > 0U && strcmp(result.candidates[0], "が") == 0, "が is が");

	/* The verdict. */
	if (failures != 0U) {
		printf("host-hand-keyboard: %u FAILED\n", failures);
		return 1;
	}

	/* Every check held. */
	printf("host-hand-keyboard: PASS\n");
	return 0;
}

/* Counts and reports a check that does not hold. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that holds is said too. */
	if (condition) {
		printf("ok: %s\n", what);
		return;
	}

	/* A failure. */
	failures++;
	printf("FAIL: %s\n", what);
}

/*
 * Writes a template's strokes as ink, scaled so that its height is size
 * pixels, from (20, 20).  Returns 0, or -1 when the template is not there.
 */
static int
write_glyph(
	const char *code,
	int size,
	struct kwl_hand_ink *ink)
{
	char line[16384];
	const char *found;
	const char *end;
	char *word;
	float x;
	float y;
	float min_y;
	float max_y;
	float min_x;
	float scale;
	int fresh;
	int pass;
	int separator;

	/* The template's line. */
	found = strstr(test_text, code);
	if (found == NULL)
		return -1;
	end = strchr(found, '\n');
	if (end == NULL || (size_t)(end - found) >= sizeof(line))
		return -1;

	/* Twice: the bounds first, then the ink. */
	min_x = 1e9f;
	min_y = 1e9f;
	max_y = -1e9f;
	scale = 1.0f;
	kwl_hand_clear(ink);
	for (pass = 0; pass < 2; pass++) {
		memcpy(line, found, (size_t)(end - found));
		line[end - found] = '\0';
		word = strtok(line, " ");
		word = strtok(NULL, " ");
		fresh = 1;
		for (word = strtok(NULL, " "); word != NULL; word = strtok(NULL, " ")) {
			separator = strcmp(word, "/");
			if (separator == 0) {
				fresh = 1;
				continue;
			}

			/* A point. */
			x = strtof(word, &word);
			y = strtof(word + 1, NULL);
			if (pass == 0) {
				min_x = fminf(min_x, x);
				min_y = fminf(min_y, y);
				max_y = fmaxf(max_y, y);
				continue;
			}

			/* Drawn at the size. */
			x = 20.0f + (x - min_x) * scale;
			y = 20.0f + (y - min_y) * scale;
			if (fresh)
				(void)kwl_hand_begin(ink, (int32_t)x, (int32_t)y);
			else
				(void)kwl_hand_add(ink, (int32_t)x, (int32_t)y);
			fresh = 0;
		}

		/* The scale for the second pass. */
		scale = (float)size / (max_y - min_y);
	}

	/* Written. */
	return 0;
}
