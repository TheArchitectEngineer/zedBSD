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
 * the last is not kept).  kwl_hand_recognize is the one place a real
 * recognizer is to be put in later: it takes the ink and answers
 * candidates, the likeliest first.  Until then it is a stub that answers
 * three fixed candidates and a note saying recognition is not there yet;
 * it still measures the ink (its strokes, points and bounds) for the log.
 */

#include "keyboard.h"

#include <stdio.h>
#include <string.h>

/* The stub's note over its candidates. */
#define HAND_STUB_NOTE		"認識はまだ"

/*
 * The stub recognizer's candidates, the same for any ink (fixed, for the
 * tests); they live as long as the program.
 */
static const char *const hand_stub_candidates[] = {
	"あ",
	"い",
	"う"
};

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
 * Recognizes the ink: the candidates for the character written, the
 * likeliest first.  This is a stub until a recognizer is put in: without
 * ink it answers nothing, with ink three fixed candidates and a note that
 * recognition is not there yet.
 */
void
kwl_hand_recognize(
	const struct kwl_hand_ink *ink,
	struct kwl_hand_result *result)
{
	unsigned index;

	/* Nothing yet. */
	memset(result, 0, sizeof(*result));

	/* Without ink, no candidates. */
	if (ink->count == 0U)
		return;

	/* The stub's candidates. */
	for (index = 0; index < sizeof(hand_stub_candidates) / sizeof(hand_stub_candidates[0]) && index < KWL_HAND_CANDIDATES; index++) {
		(void)snprintf(result->candidates[index], sizeof(result->candidates[index]), "%s", hand_stub_candidates[index]);
		result->count++;
	}

	/* The note that recognition is not there yet. */
	(void)snprintf(result->note, sizeof(result->note), "%s", HAND_STUB_NOTE);
}
