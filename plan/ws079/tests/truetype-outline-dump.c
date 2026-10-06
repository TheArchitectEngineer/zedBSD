/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Prints every glyph's outline as truetype_glyph_outline() reads it.
 *
 * truetype-outline-check.py compares the text with what fontTools reads
 * from the same font.  Each glyph is asked for twice: first with no arrays,
 * which must report ENOSPC (or success for an empty glyph) with the counts
 * it needs, then with arrays of exactly that size.
 *
 *     truetype-outline-dump FONT GLYPH-COUNT
 *
 * One line per glyph:
 *
 *     G advance lsb xmin ymin xmax ymax | end end ... | x,y,on x,y,on ...
 */

#include <truetype/truetype.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void *load(const char *path, size_t *size);
static int dump_glyph(struct truetype_face *face, unsigned glyph);

/* Opens the font and prints each glyph in turn. */
int
main(
	int argc,
	char **argv)
{
	struct truetype_face *face;
	unsigned glyph_count;
	unsigned glyph;
	size_t size;
	void *data;
	int error;

	/* Refuses a call without a font and a glyph count. */
	if (argc != 3) {
		fprintf(stderr, "usage: truetype-outline-dump FONT GLYPH-COUNT\n");
		return 2;
	}

	/* Reads the font into memory the face will point into. */
	data = load(argv[1], &size);
	if (data == NULL) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 1;
	}

	/* Opens the face over the font data. */
	error = truetype_open(data, size, 0, &face);
	if (error != 0) {
		fprintf(stderr, "truetype_open: %d\n", error);
		free(data);
		return 1;
	}

	/* Prints every glyph the font has. */
	glyph_count = (unsigned)strtoul(argv[2], NULL, 10);
	for (glyph = 0; glyph < glyph_count; glyph++) {
		error = dump_glyph(face, glyph);
		if (error != 0) {
			fprintf(stderr, "glyph %u: error %d\n", glyph, error);
			truetype_close(face);
			free(data);
			return 1;
		}
	}

	/* Checks that a glyph past the end is refused. */
	error = dump_glyph(face, glyph_count);
	if (error != EINVAL) {
		fprintf(stderr, "glyph past the end: error %d\n", error);
		truetype_close(face);
		free(data);
		return 1;
	}

	truetype_close(face);
	free(data);

	/* Succeeded: every glyph is printed. */
	return 0;
}

/* Reads a whole file into memory the caller keeps. */
static void *
load(
	const char *path,
	size_t *size)
{
	struct stat information;
	void *data;
	FILE *file;
	size_t count;

	/* Opens the file. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;

	/* Finds how big it is. */
	if (fstat(fileno(file), &information) != 0) {
		fclose(file);
		return NULL;
	}

	/* Allocates room for all of it. */
	*size = (size_t)information.st_size;
	data = malloc(*size);
	if (data == NULL) {
		fclose(file);
		return NULL;
	}

	/* Reads all of it. */
	count = fread(data, 1, *size, file);
	fclose(file);
	if (count != *size) {
		free(data);
		return NULL;
	}

	/* Succeeded: the caller owns the data. */
	return data;
}

/* Asks for one glyph's size, then for its outline, and prints it. */
static int
dump_glyph(
	struct truetype_face *face,
	unsigned glyph)
{
	struct truetype_glyph_outline outline;
	unsigned index;
	int error;

	/* Asks with no arrays, which reports the counts the glyph needs. */
	memset(&outline, 0, sizeof(outline));
	error = truetype_glyph_outline(face, glyph, &outline);
	if (error != 0 && error != ENOSPC)
		return error;

	/* A glyph with points must have reported that the arrays were short. */
	if (outline.point_count != 0 && error != ENOSPC)
		return EPROTO;

	/* Allocates arrays of exactly the reported size. */
	outline.point_capacity = outline.point_count;
	outline.contour_capacity = outline.contour_count;
	outline.points = malloc(sizeof(*outline.points) * (outline.point_capacity + 1U));
	outline.contour_ends = malloc(sizeof(*outline.contour_ends) * (outline.contour_capacity + 1U));
	if (outline.points == NULL || outline.contour_ends == NULL) {
		free(outline.points);
		free(outline.contour_ends);
		return ENOMEM;
	}

	/* Reads the outline into the arrays. */
	error = truetype_glyph_outline(face, glyph, &outline);
	if (error != 0) {
		free(outline.points);
		free(outline.contour_ends);
		return error;
	}

	/* Prints the metrics, the contour ends and the points. */
	printf("%u %d %d %d %d %d %d |", glyph, outline.advance,
	       outline.left_side_bearing, outline.x_min, outline.y_min,
	       outline.x_max, outline.y_max);
	for (index = 0; index < outline.contour_count; index++)
		printf(" %u", outline.contour_ends[index]);
	printf(" |");
	for (index = 0; index < outline.point_count; index++) {
		printf(" %.3f,%.3f,%u", (double)outline.points[index].x,
		       (double)outline.points[index].y,
		       outline.points[index].on_curve);
	}
	printf("\n");

	free(outline.points);
	free(outline.contour_ends);

	/* Succeeded: the glyph is printed. */
	return 0;
}
