/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws079-p011: writes a Notes PDF whose first page carries many strokes, for
 * measuring Notes' frame time on a full page (plan/ws079/tests/notes-perf.sh).
 *
 * The strokes are small handwriting-like loops, 40 samples each with a
 * pressure that rises and falls, laid out in lines across an A4 page like a
 * page of notes.  The layout is fixed (no randomness), so every run draws
 * the same page.
 *
 *   notes-many OUTPUT.pdf COUNT
 */

#include "notes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The samples of one stroke. */
#define MANY_SAMPLES		40U

/* The strokes on one line of the page, and the lines' spacing in points. */
#define MANY_PER_LINE		25U
#define MANY_LINE_HEIGHT	32.0f

static struct notes_stroke *many_stroke(struct notes_document *document, unsigned number);

/*
 * Writes the PDF.
 */
int
main(
	int argc,
	char **argv)
{
	struct notes_document document;
	struct notes_stroke *stroke;
	unsigned long count;
	unsigned number;
	size_t bytes;
	int error;

	/* The output and the number of strokes. */
	if (argc != 3) {
		fprintf(stderr, "usage: notes-many OUTPUT.pdf COUNT\n");
		return 2;
	}
	count = strtoul(argv[2], NULL, 10);

	/* An empty notebook, at a fixed time. */
	error = notes_document_init(&document, 1790000000000ULL);
	if (error != 0) {
		fprintf(stderr, "notes-many: init: %d\n", error);
		return 1;
	}

	/* The strokes, one after another, on the first page. */
	for (number = 0; number < count; number++) {
		stroke = many_stroke(&document, number);
		if (stroke == NULL) {
			fprintf(stderr, "notes-many: out of memory\n");
			return 1;
		}

		/* The stroke goes on top of the page. */
		error = notes_document_add_stroke(&document, 0U, stroke);
		if (error != 0) {
			fprintf(stderr, "notes-many: add: %d\n", error);
			return 1;
		}
	}

	/* The PDF. */
	error = notes_save_pdf(&document, argv[1], &bytes);
	if (error != 0) {
		fprintf(stderr, "notes-many: save: %d\n", error);
		return 1;
	}

	/* Succeeded: the file is written. */
	printf("notes-many: %lu strokes, %lu bytes, %s\n", count, (unsigned long)bytes, argv[1]);
	notes_document_free(&document);
	return 0;
}

/* Makes the stroke of a number: a small loop at its place on its line. */
static struct notes_stroke *
many_stroke(
	struct notes_document *document,
	unsigned number)
{
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned sample;
	uint32_t id;
	float left;
	float base;
	float share;
	float angle;
	float size;
	int error;

	/* The stroke's place: its column and its line, the lines wrapping down the page. */
	left = 40.0f + (float)(number % MANY_PER_LINE) * 21.0f;
	base = 60.0f + (float)((number / MANY_PER_LINE) % 24U) * MANY_LINE_HEIGHT;
	size = 7.0f + (float)(number % 5U);

	/* The stroke, in black or blue, 3 points wide. */
	id = document->next_id;
	document->next_id++;
	stroke = notes_stroke_create(id, NOTES_TOOL_PEN, (number % 7U) == 0 ? 0x1d4ed8ffU : 0x1f1f1fffU, 3.0f,
				     document->time_base + 1000U * id);
	if (stroke == NULL)
		return NULL;

	/* A loop that drifts right, with a pressure that rises and falls. */
	for (sample = 0; sample < MANY_SAMPLES; sample++) {
		share = (float)sample / (float)(MANY_SAMPLES - 1U);
		angle = share * 2.0f * 3.14159265f * 1.5f + (float)number;
		memset(&point, 0, sizeof(point));
		point.x = notes_quantize(left + share * 14.0f + size * 0.5f * (float)cos((double)angle));
		point.y = notes_quantize(base + size * (float)sin((double)angle));
		point.pressure = (uint16_t)(20000.0 + 40000.0 * sin((double)share * 3.14159265));
		point.time_ms = sample * 8U;

		/* The sample joins the stroke. */
		error = notes_stroke_append(stroke, &point);
		if (error != 0) {
			notes_stroke_free(stroke);
			return NULL;
		}
	}

	/* Succeeded: the stroke. */
	return stroke;
}
