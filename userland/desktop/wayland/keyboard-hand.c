/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The handwriting face's ink and its recognizer (ws102-p008,
 * plan/ws102/design.md §2.7).
 *
 * The ink is the strokes written since it was last cleared, each the
 * points the pen or finger passed through (a point that does not move from
 * the last is not kept).  kwl_hand_recognize answers the candidates, the
 * likeliest first (ws165-p003): the point clouds of hand-cloud.c against
 * the templates of the package hand-hershey, read the first time; then
 * the characters that differ only by their size are put in the order the
 * ink's size on the writing area says (a small c before C), and a small
 * kana is offered before its full size one when the ink is small.  It
 * also measures the ink (its strokes, points and bounds) for the log.
 */

#include "keyboard.h"
#include "hand-cloud.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The templates' file (the package hand-hershey), and the largest one read. */
#define HAND_TEMPLATES_PATH	KEILAND_DATADIR "/keiland/hand/hershey.txt"
#define HAND_TEMPLATES_MAX	(1024U * 1024U)

/* The note when no template could be read. */
#define HAND_NO_DATA_NOTE	"No handwriting data"

/* The share of the writing area's height under which ink is small, and the most candidates looked at. */
#define HAND_SMALL_SHARE	0.40f
#define HAND_LOOKED		8U

/*
 * The pairs that differ only by size, the small one first (a small ink
 * takes the small one).
 */
static const uint32_t hand_sizes[][2] = {
	{ 'c', 'C' }, { 's', 'S' }, { 'v', 'V' }, { 'w', 'W' }, { 'x', 'X' }, { 'z', 'Z' }, { 'o', 'O' }, { 'p', 'P' },
	{ 0x3041, 0x3042 }, { 0x3043, 0x3044 }, { 0x3045, 0x3046 }, { 0x3047, 0x3048 }, { 0x3049, 0x304a }, { 0x3063, 0x3064 },
	{ 0x3083, 0x3084 }, { 0x3085, 0x3086 }, { 0x3087, 0x3088 }, { 0x308e, 0x308f },
	{ 0x30a1, 0x30a2 }, { 0x30a3, 0x30a4 }, { 0x30a5, 0x30a6 }, { 0x30a7, 0x30a8 }, { 0x30a9, 0x30aa }, { 0x30c3, 0x30c4 },
	{ 0x30e3, 0x30e4 }, { 0x30e5, 0x30e6 }, { 0x30e7, 0x30e8 }, { 0x30ee, 0x30ef }, { 0x30f5, 0x30ab }, { 0x30f6, 0x30b1 }
};

/*
 * The templates, read the first time a recognition needs them (hand_state:
 * 0 not tried, 1 read, -1 failed); the event loop's alone.
 */
static struct hand_templates hand_templates;
static int hand_state;

static void hand_sized(uint32_t *codes, size_t *count, int small);
static void hand_utf8(uint32_t code, char *text, size_t size);

/*
 * Clears the ink: no strokes.
 */
void
kwl_hand_clear(
	struct kwl_hand_ink *ink)
{
	/* No stroke is kept (their points are left as they were, unread). */
	ink->count = 0;
}

/*
 * Begins a stroke at a point.  Returns 1, or 0 when the ink has no room
 * for another stroke (the stroke is then not kept).
 */
int
kwl_hand_begin(
	struct kwl_hand_ink *ink,
	int32_t x,
	int32_t y)
{
	struct kwl_hand_stroke *stroke;

	/* No room for another stroke. */
	if (ink->count >= KWL_HAND_STROKES)
		return 0;

	/* The new stroke with its first point. */
	stroke = &ink->strokes[ink->count];
	stroke->count = 1;
	stroke->points[0].x = (int16_t)x;
	stroke->points[0].y = (int16_t)y;
	ink->count++;

	/* Succeeded: the stroke is kept. */
	return 1;
}

/*
 * Adds a point to the stroke being written.  Returns 1 when it was kept,
 * 0 when there is no stroke, it did not move, or the stroke is full.
 */
int
kwl_hand_add(
	struct kwl_hand_ink *ink,
	int32_t x,
	int32_t y)
{
	struct kwl_hand_stroke *stroke;
	const struct kwl_hand_point *last;

	/* The stroke being written. */
	if (ink->count == 0U)
		return 0;
	stroke = &ink->strokes[ink->count - 1U];

	/* A point where the last one was adds nothing. */
	last = &stroke->points[stroke->count - 1U];
	if (last->x == (int16_t)x && last->y == (int16_t)y)
		return 0;

	/* A full stroke keeps no more. */
	if (stroke->count >= KWL_HAND_POINTS)
		return 0;

	/* The point. */
	stroke->points[stroke->count].x = (int16_t)x;
	stroke->points[stroke->count].y = (int16_t)y;
	stroke->count++;

	/* Succeeded: the point is kept. */
	return 1;
}

/*
 * Counts the points of all the ink's strokes.
 */
unsigned
kwl_hand_points(
	const struct kwl_hand_ink *ink)
{
	unsigned stroke;
	unsigned total;

	/* Each stroke's points. */
	total = 0;
	for (stroke = 0; stroke < ink->count; stroke++)
		total += ink->strokes[stroke].count;

	/* The total. */
	return total;
}

/*
 * Works out the rectangle (x, y, width, height) the ink's points cover;
 * all zero without ink.
 */
void
kwl_hand_bounds(
	const struct kwl_hand_ink *ink,
	int32_t *rect)
{
	const struct kwl_hand_point *point;
	unsigned stroke;
	unsigned index;
	int32_t left;
	int32_t top;
	int32_t right;
	int32_t bottom;
	int any;

	/* The points' extremes. */
	any = 0;
	left = 0;
	top = 0;
	right = 0;
	bottom = 0;
	for (stroke = 0; stroke < ink->count; stroke++) {
		for (index = 0; index < ink->strokes[stroke].count; index++) {
			/* The first point starts the extremes; the others widen them. */
			point = &ink->strokes[stroke].points[index];
			if (!any) {
				left = point->x;
				right = point->x;
				top = point->y;
				bottom = point->y;
				any = 1;
				continue;
			}

			/* Wider across. */
			if (point->x < left)
				left = point->x;
			if (point->x > right)
				right = point->x;

			/* Wider down. */
			if (point->y < top)
				top = point->y;
			if (point->y > bottom)
				bottom = point->y;
		}
	}

	/* The rectangle (a point is one pixel). */
	rect[0] = left;
	rect[1] = top;
	rect[2] = 0;
	rect[3] = 0;
	if (any) {
		rect[2] = right - left + 1;
		rect[3] = bottom - top + 1;
	}
}

/*
 * Reads the templates from a file (the package hand-hershey's).  Returns 0,
 * or an errno value (the recognition then answers no candidates).
 */
int
kwl_hand_load(
	const char *path)
{
	FILE *file;
	char *text;
	size_t length;
	int error;

	/* The file, whole. */
	hand_templates_free(&hand_templates);
	hand_state = -1;
	file = fopen(path, "r");
	if (file == NULL) {
		error = errno;
		printf("KWL OSK hand templates path=%s error=%d\n", path, error);
		return error;
	}

	/* Room for its text. */
	text = malloc(HAND_TEMPLATES_MAX);
	if (text == NULL) {
		fclose(file);
		return ENOMEM;
	}

	/* Its text. */
	length = fread(text, 1U, HAND_TEMPLATES_MAX, file);
	fclose(file);

	/* The templates of it. */
	error = hand_templates_parse(&hand_templates, text, length);
	free(text);
	printf("KWL OSK hand templates path=%s count=%lu error=%d\n", path, (unsigned long)hand_templates.count, error);
	if (error != 0)
		return error;

	/* Succeeded: read. */
	hand_state = 1;
	return 0;
}

/*
 * Recognizes the ink: the candidates for the character written, the
 * likeliest first.  area is the writing area's height (0 when it is not
 * known: the sizes are then not used).
 */
void
kwl_hand_recognize(
	const struct kwl_hand_ink *ink,
	int32_t area,
	struct kwl_hand_result *result)
{
	static float xs[HAND_CLOUD_INPUT_MAX];
	static float ys[HAND_CLOUD_INPUT_MAX];
	static unsigned char starts[HAND_CLOUD_INPUT_MAX];
	struct hand_cloud_input input;
	uint32_t codes[HAND_LOOKED];
	float distances[HAND_LOOKED];
	int32_t bounds[4];
	size_t count;
	unsigned stroke;
	unsigned point;
	int small;

	/* Nothing yet. */
	memset(result, 0, sizeof(*result));

	/* Without ink, no candidates. */
	if (ink->count == 0U)
		return;

	/* The templates, read the first time. */
	if (hand_state == 0)
		(void)kwl_hand_load(HAND_TEMPLATES_PATH);
	if (hand_state != 1) {
		(void)snprintf(result->note, sizeof(result->note), "%s", HAND_NO_DATA_NOTE);
		return;
	}

	/* The ink's points, each stroke's first marked. */
	input.count = 0U;
	for (stroke = 0U; stroke < ink->count; stroke++) {
		for (point = 0U; point < ink->strokes[stroke].count && input.count < HAND_CLOUD_INPUT_MAX; point++) {
			xs[input.count] = (float)ink->strokes[stroke].points[point].x;
			ys[input.count] = (float)ink->strokes[stroke].points[point].y;
			starts[input.count] = (unsigned char)(point == 0U);
			input.count++;
		}
	}

	/* The points as the recognizer takes them. */
	input.x = xs;
	input.y = ys;
	input.starts = starts;

	/* The nearest characters, put in the order the ink's size says. */
	count = hand_recognize_strokes(&hand_templates, &input, codes, distances, HAND_LOOKED);
	kwl_hand_bounds(ink, bounds);
	small = 0;
	if (area > 0 && (float)bounds[3] < HAND_SMALL_SHARE * (float)area && (float)bounds[2] < HAND_SMALL_SHARE * (float)area)
		small = 1;
	if (area > 0)
		hand_sized(codes, &count, small);

	/* The first ones, as text. */
	for (point = 0U; point < count && result->count < KWL_HAND_CANDIDATES; point++) {
		hand_utf8(codes[point], result->candidates[result->count], sizeof(result->candidates[0]));
		result->count++;
	}
}

/*
 * Puts the characters that differ only by size in the order the ink's size
 * says: the first candidate's pair, its small one first for small ink and
 * its large one otherwise, the other right after it (added when it was not
 * among the candidates).
 */
static void
hand_sized(
	uint32_t *codes,
	size_t *count,
	int small)
{
	uint32_t chosen;
	uint32_t other;
	size_t pair;
	size_t index;

	/* The first candidate's pair. */
	if (*count == 0U)
		return;
	for (pair = 0U; pair < sizeof(hand_sizes) / sizeof(hand_sizes[0]); pair++) {
		if (codes[0] == hand_sizes[pair][0] || codes[0] == hand_sizes[pair][1])
			break;
	}

	/* Not one of the pairs. */
	if (pair == sizeof(hand_sizes) / sizeof(hand_sizes[0]))
		return;

	/* The one the size says, then the other. */
	chosen = hand_sizes[pair][1];
	other = hand_sizes[pair][0];
	if (small) {
		chosen = hand_sizes[pair][0];
		other = hand_sizes[pair][1];
	}

	/* Both taken out of the list where they are. */
	for (index = 0U; index < *count;) {
		if (codes[index] == chosen || codes[index] == other) {
			memmove(&codes[index], &codes[index + 1U], (*count - index - 1U) * sizeof(codes[0]));
			(*count)--;
			continue;
		}

		/* The next one. */
		index++;
	}

	/* Put first, the list kept within its room. */
	if (*count > HAND_LOOKED - 2U)
		*count = HAND_LOOKED - 2U;
	memmove(&codes[2], &codes[0], *count * sizeof(codes[0]));
	codes[0] = chosen;
	codes[1] = other;
	*count += 2U;
}

/* Writes a code point as UTF-8 (one to three bytes, as the templates have). */
static void
hand_utf8(
	uint32_t code,
	char *text,
	size_t size)
{
	unsigned char bytes[4];

	/* Its bytes. */
	memset(bytes, 0, sizeof(bytes));
	if (code < 0x80U) {
		bytes[0] = (unsigned char)code;
	} else if (code < 0x800U) {
		bytes[0] = (unsigned char)(0xc0U | (code >> 6));
		bytes[1] = (unsigned char)(0x80U | (code & 0x3fU));
	} else {
		bytes[0] = (unsigned char)(0xe0U | (code >> 12));
		bytes[1] = (unsigned char)(0x80U | ((code >> 6) & 0x3fU));
		bytes[2] = (unsigned char)(0x80U | (code & 0x3fU));
	}

	/* As text. */
	(void)snprintf(text, size, "%s", (const char *)bytes);
}
