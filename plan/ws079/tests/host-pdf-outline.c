/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of pdf_outline_stroke()'s smoothing and round joins
 * (ws079-p004).
 *
 * It outlines strokes that turn sharply: zigzags of three sharpnesses, a
 * hairpin that doubles back on itself exactly, one that doubles back beside
 * itself, and a loop of a few sparse samples.  For each it checks that the
 * outline is the same on a second call, that every sample lies inside it,
 * and that the stroke does not narrow at a sample: points at 85% of the
 * half width across the arriving, the leaving and the mean direction, each
 * turned five degrees either way, are all inside (nonzero winding).  It
 * then draws the strokes on one page, with the samples of the loop as
 * small dots, for a look with pdftoppm.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

/* The most samples a test stroke has. */
#define TEST_POINTS_MAX 128

/* The number of test strokes. */
#define TEST_STROKES 6

/* The full width every test stroke is drawn with. */
#define TEST_WIDTH 12.0

/* Pi, which C89's math.h does not name. */
#define TEST_PI 3.14159265358979323846

/*
 * One test stroke: its samples and where it is drawn.
 */
struct test_stroke {
	const char *name;
	struct pdf_stroke_point points[TEST_POINTS_MAX];
	size_t count;
};

static void make_strokes(struct test_stroke *strokes);
static void make_zigzag(struct test_stroke *stroke, const char *name, double top, double step, double depth);
static int check_stroke(const struct test_stroke *stroke);
static int winding(const struct pdf_point *outline, size_t count, double x, double y);
static double half_width(double pressure);
static int check_point(const struct test_stroke *stroke, const struct pdf_point *outline, size_t count, size_t sample, double direction_x, double direction_y);
static int write_page(const struct test_stroke *strokes, const char *path);
static int fill_stroke(struct pdf_writer *writer, const struct test_stroke *stroke);

/*
 * Checks the test strokes and writes them to the PDF the first argument names.
 */
int
main(
	int argc,
	char **argv)
{
	static struct test_stroke strokes[TEST_STROKES];
	size_t index;
	int failures;
	int error;

	/* Needs the output path. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-pdf-outline out.pdf\n");
		return 2;
	}

	/* Builds the strokes. */
	make_strokes(strokes);

	/* Checks each stroke's outline. */
	failures = 0;
	for (index = 0; index < TEST_STROKES; index++) {
		error = check_stroke(&strokes[index]);
		if (error != 0)
			failures++;
	}

	/* Draws the strokes on one page. */
	error = write_page(strokes, argv[1]);
	if (error != 0) {
		fprintf(stderr, "host-pdf-outline: writing failed: %s\n", strerror(error));
		return 1;
	}

	/* Reports a stroke whose outline failed a check. */
	if (failures != 0) {
		fprintf(stderr, "host-pdf-outline: %d stroke(s) failed\n", failures);
		return 1;
	}

	/* Succeeded: every outline passed. */
	printf("host-pdf-outline: ok\n");
	return 0;
}

/* Builds the six test strokes. */
static void
make_strokes(
	struct test_stroke *strokes)
{
	size_t index;
	double angle;

	/* Three zigzags: corners of about 53, 22 and 6 degrees. */
	make_zigzag(&strokes[0], "zigzag-53", 80.0, 40.0, 80.0);
	make_zigzag(&strokes[1], "zigzag-22", 200.0, 16.0, 80.0);
	make_zigzag(&strokes[2], "zigzag-6", 320.0, 4.0, 80.0);

	/* A hairpin that goes right and comes back along the same line, the pressure rising. */
	strokes[3].name = "hairpin-exact";
	strokes[3].count = 0;
	for (index = 0; index <= 12; index++) {
		strokes[3].points[strokes[3].count].x = 60.0 + 20.0 * (double)index;
		strokes[3].points[strokes[3].count].y = 470.0;
		strokes[3].points[strokes[3].count].pressure = 0.2 + 0.05 * (double)index;
		strokes[3].count++;
	}
	for (index = 1; index <= 12; index++) {
		strokes[3].points[strokes[3].count].x = 300.0 - 20.0 * (double)index;
		strokes[3].points[strokes[3].count].y = 470.0;
		strokes[3].points[strokes[3].count].pressure = 0.8;
		strokes[3].count++;
	}

	/* A hairpin that comes back three points beside itself. */
	strokes[4].name = "hairpin-beside";
	strokes[4].count = 0;
	for (index = 0; index <= 12; index++) {
		strokes[4].points[strokes[4].count].x = 330.0 + 16.0 * (double)index;
		strokes[4].points[strokes[4].count].y = 470.0;
		strokes[4].points[strokes[4].count].pressure = 0.9;
		strokes[4].count++;
	}
	for (index = 0; index <= 12; index++) {
		strokes[4].points[strokes[4].count].x = 522.0 - 16.0 * (double)index;
		strokes[4].points[strokes[4].count].y = 473.0;
		strokes[4].points[strokes[4].count].pressure = 0.9;
		strokes[4].count++;
	}

	/* A loop drawn with only nine sparse samples, which the spline must round. */
	strokes[5].name = "sparse-loop";
	strokes[5].count = 0;
	for (index = 0; index < 9; index++) {
		angle = (double)index * 2.0 * TEST_PI / 7.0;
		strokes[5].points[strokes[5].count].x = 180.0 + 60.0 * (double)index / 8.0 * 3.0 + 70.0 * cos(angle);
		strokes[5].points[strokes[5].count].y = 640.0 + 70.0 * sin(angle);
		strokes[5].points[strokes[5].count].pressure = 0.3 + 0.7 * (double)index / 8.0;
		strokes[5].count++;
	}
}

/* Builds a zigzag across the page whose corners get sharper as the step shrinks. */
static void
make_zigzag(
	struct test_stroke *stroke,
	const char *name,
	double top,
	double step,
	double depth)
{
	size_t index;

	/* Alternates between the top and the bottom line, the pressure swinging with the index. */
	stroke->name = name;
	stroke->count = 0;
	for (index = 0; index < 24 && 60.0 + step * (double)index <= 540.0; index++) {
		stroke->points[index].x = 60.0 + step * (double)index;
		stroke->points[index].y = top;
		if (index % 2 == 1)
			stroke->points[index].y = top + depth;
		stroke->points[index].pressure = 0.35 + 0.3 * (double)(index % 3);
		stroke->count++;
	}
}

/* Checks one stroke's outline: stable, through every sample, and never narrower than the width. */
static int
check_stroke(
	const struct test_stroke *stroke)
{
	struct pdf_point *outline;
	struct pdf_point *again;
	size_t count;
	size_t again_count;
	size_t sample;
	double in_x;
	double in_y;
	double out_x;
	double out_y;
	double length;
	int failures;
	int error;

	/* Outlines the stroke twice. */
	error = pdf_outline_stroke(stroke->points, stroke->count, TEST_WIDTH, &outline, &count);
	if (error != 0) {
		fprintf(stderr, "%s: outline failed: %s\n", stroke->name, strerror(error));
		return error;
	}
	error = pdf_outline_stroke(stroke->points, stroke->count, TEST_WIDTH, &again, &again_count);
	if (error != 0) {
		pdf_outline_free(outline);
		return error;
	}

	/* The two outlines must be the same, point for point. */
	failures = 0;
	if (again_count != count || memcmp(again, outline, count * sizeof(*outline)) != 0) {
		fprintf(stderr, "%s: the second outline differs\n", stroke->name);
		failures++;
	}
	pdf_outline_free(again);

	/* Checks each sample. */
	for (sample = 0; sample < stroke->count; sample++) {
		/* The sample itself is inside. */
		if (winding(outline, count, stroke->points[sample].x, stroke->points[sample].y) == 0) {
			fprintf(stderr, "%s: sample %lu is outside\n", stroke->name, (unsigned long)sample);
			failures++;
		}

		/* Finds the arriving and the leaving direction, or reuses one at an end. */
		in_x = 0.0;
		in_y = 0.0;
		out_x = 0.0;
		out_y = 0.0;
		if (sample > 0) {
			in_x = stroke->points[sample].x - stroke->points[sample - 1].x;
			in_y = stroke->points[sample].y - stroke->points[sample - 1].y;
		}
		if (sample + 1 < stroke->count) {
			out_x = stroke->points[sample + 1].x - stroke->points[sample].x;
			out_y = stroke->points[sample + 1].y - stroke->points[sample].y;
		}
		if (sample == 0) {
			in_x = out_x;
			in_y = out_y;
		}
		if (sample + 1 == stroke->count) {
			out_x = in_x;
			out_y = in_y;
		}
		length = sqrt(in_x * in_x + in_y * in_y);
		in_x /= length;
		in_y /= length;
		length = sqrt(out_x * out_x + out_y * out_y);
		out_x /= length;
		out_y /= length;

		/* Checks across the arriving direction, the leaving one and their mean, both ways. */
		failures += check_point(stroke, outline, count, sample, -in_y, in_x);
		failures += check_point(stroke, outline, count, sample, in_y, -in_x);
		failures += check_point(stroke, outline, count, sample, -out_y, out_x);
		failures += check_point(stroke, outline, count, sample, out_y, -out_x);
		length = sqrt((in_x + out_x) * (in_x + out_x) + (in_y + out_y) * (in_y + out_y));
		if (length > 1e-6) {
			failures += check_point(stroke, outline, count, sample, -(in_y + out_y) / length, (in_x + out_x) / length);
			failures += check_point(stroke, outline, count, sample, (in_y + out_y) / length, -(in_x + out_x) / length);
		}
	}
	pdf_outline_free(outline);

	/* Reports a failed check. */
	printf("%s: %lu samples, %lu outline corners, %d failure(s)\n",
	       stroke->name,
	       (unsigned long)stroke->count,
	       (unsigned long)count,
	       failures);
	if (failures != 0)
		return EPROTO;

	/* Succeeded: the outline passed. */
	return 0;
}

/*
 * Checks that the points at 85% of the half width from a sample, in a
 * direction turned five degrees either way, are inside the outline.
 *
 * It reports the number of points that are outside.
 */
static int
check_point(
	const struct test_stroke *stroke,
	const struct pdf_point *outline,
	size_t count,
	size_t sample,
	double direction_x,
	double direction_y)
{
	static const double turns[2] = { 5.0, -5.0 };
	double reach;
	double angle;
	double x;
	double y;
	size_t turn;
	int failures;

	/* Reaches 85% of the half width the sample's pressure draws. */
	reach = 0.85 * half_width(stroke->points[sample].pressure);
	failures = 0;
	for (turn = 0; turn < 2; turn++) {
		angle = turns[turn] * TEST_PI / 180.0;
		x = stroke->points[sample].x + reach * (direction_x * cos(angle) - direction_y * sin(angle));
		y = stroke->points[sample].y + reach * (direction_x * sin(angle) + direction_y * cos(angle));
		if (winding(outline, count, x, y) == 0) {
			fprintf(stderr,
				"%s: the stroke narrows at sample %lu toward (%.3f, %.3f)\n",
				stroke->name,
				(unsigned long)sample,
				direction_x,
				direction_y);
			failures++;
		}
	}

	/* Reports the points that were outside. */
	return failures;
}

/* Computes the half width pdf_outline_stroke() gives a pressure. */
static double
half_width(
	double pressure)
{
	/* Follows the library's curve: 15% of the width at no pressure, rising with pressure^0.7. */
	return TEST_WIDTH * (0.15 + 0.85 * pow(pressure, 0.7)) / 2.0;
}

/* Computes the winding number of a closed polygon around a point. */
static int
winding(
	const struct pdf_point *outline,
	size_t count,
	double x,
	double y)
{
	size_t index;
	size_t next;
	double cross;
	int number;

	/* Counts the edges that cross the horizontal ray to the right of the point, signed by direction. */
	number = 0;
	for (index = 0; index < count; index++) {
		next = (index + 1) % count;
		cross = (outline[next].x - outline[index].x) * (y - outline[index].y) - (x - outline[index].x) * (outline[next].y - outline[index].y);
		if (outline[index].y <= y) {
			if (outline[next].y > y && cross > 0.0)
				number++;
		} else {
			if (outline[next].y <= y && cross < 0.0)
				number--;
		}
	}

	/* Reports the winding number. */
	return number;
}

/* Draws every test stroke on one A4 page, with the loop's samples as red dots of 3 points. */
static int
write_page(
	const struct test_stroke *strokes,
	const char *path)
{
	struct pdf_writer *writer;
	struct test_stroke dot;
	size_t index;
	int error;

	/* Creates the document with a fixed identifier and dates, so the output is reproducible. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	error = pdf_writer_set_document_id(writer, (const unsigned char *)"ws079p004outline");
	if (error == 0)
		error = pdf_writer_set_dates(writer, 1790564400, 1790564400);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.276, 841.89);

	/* Fills each stroke in dark blue. */
	for (index = 0; index < TEST_STROKES && error == 0; index++) {
		error = pdf_writer_set_fill_color(writer, 0.1, 0.15, 0.5, 1.0);
		if (error == 0)
			error = fill_stroke(writer, &strokes[index]);
	}

	/* Marks each sample of the loop with a red dot of 3 points. */
	for (index = 0; index < strokes[5].count && error == 0; index++) {
		dot.name = "dot";
		dot.count = 1;
		dot.points[0] = strokes[5].points[index];
		dot.points[0].pressure = 0.0;
		error = pdf_writer_set_fill_color(writer, 0.9, 0.1, 0.1, 1.0);
		if (error == 0)
			error = fill_stroke(writer, &dot);
	}

	/* Saves the page. */
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	if (error != 0)
		return error;

	/* Succeeded: the page is written. */
	return 0;
}

/* Outlines one stroke and fills it with the current color. */
static int
fill_stroke(
	struct pdf_writer *writer,
	const struct test_stroke *stroke)
{
	struct pdf_point *outline;
	size_t count;
	double width;
	int error;

	/* A dot is drawn with a width of 20 points, which its zero pressure makes 3 points. */
	width = TEST_WIDTH;
	if (stroke->count == 1)
		width = 20.0;

	/* Outlines and fills the stroke. */
	error = pdf_outline_stroke(stroke->points, stroke->count, width, &outline, &count);
	if (error != 0)
		return error;
	error = pdf_writer_fill_outline(writer, outline, count);
	pdf_outline_free(outline);
	if (error != 0)
		return error;

	/* Succeeded: the stroke is drawn. */
	return 0;
}
