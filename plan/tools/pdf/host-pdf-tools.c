/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of ws216-p002 (plan/ws216/phase002/phase.md section 4):
 * libpdf's outlines of factors and of ranges, the writer's states, Flate
 * content and grains, and the reader's Flate hash and tiling patterns.
 *
 *   host-pdf-tools outline
 *       the pen's outline against main f9077504e's outline.c (linked as
 *       old_pdf_outline_stroke), the style's widths, the ranges' seams and
 *       areas, and the refusals (A2, A3).
 *   host-pdf-tools base DIR
 *       writes DIR/base.pdf, an A4 page the script turns a quarter (qpdf).
 *   host-pdf-tools write DIR
 *       writes DIR/states.pdf, tools.pdf, seam.pdf and update.pdf (over
 *       DIR/base-rot.pdf) and checks their hashes and states (A4, A7).
 *   host-pdf-tools render PDF PREFIX DPI
 *       draws every page with libpdf into PREFIX-N.ppm, with the list's
 *       counts and the time (A5, A8).
 *   host-pdf-tools compare A.ppm B.ppm [MEAN FRACTION]
 *       compares two pictures (A5, A9, A10).
 *   host-pdf-tools grain PPM DPI
 *       checks a page of the grain layout: the grain's blocks where the page
 *       as shown puts them, black, opaque above the middle and at half
 *       below (A6).
 *   host-pdf-tools clean PDF OUT
 *       writes the clean copy (A9).
 *   host-pdf-tools patterns PDF
 *       writes a hand-made page of general tiling patterns (a cell of
 *       fills, a turned cell larger than its step, a cell that paints with
 *       another pattern, an uncoloured pattern), which the script draws
 *       with libpdf and pdftoppm (M-7).
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pdf.h>

/* The grain: its side, its blocks a side, the size of one repeat on the page, and the pencil's floor. */
#define GRAIN_SIDE 256
#define GRAIN_BLOCKS 8
#define GRAIN_CELL 48.0
#define GRAIN_FLOOR 0.35

/* The pixel difference counted as real, and compare's default tolerances (as run-pdf-render.sh's). */
#define COMPARE_THRESHOLD 64
#define COMPARE_MEAN_MAX 2.5
#define COMPARE_FRACTION_MAX 0.006

/* How far a grain block's measured alpha may be from its value, and the share of blocks that may miss. */
#define GRAIN_TOLERANCE 0.04
#define GRAIN_MISS_MAX 0.002

/* The rows the area of a polygon is measured on, a point apart in hundredths. */
#define AREA_STEP 0.01

/* A span of one row inside a polygon. */
struct span {
	double left;
	double right;
};

/* The spans of one row, growing. */
struct spans {
	struct span *items;
	size_t count;
	size_t capacity;
};

/* main f9077504e's pdf_outline_stroke(), compiled with its names changed by run-pdf-tools.sh. */
int old_pdf_outline_stroke(const struct pdf_stroke_point *points, size_t count, double width, struct pdf_point **outline, size_t *outline_count);
void old_pdf_outline_free(struct pdf_point *outline);

static int run_outline(void);
static void test_golden(void);
static void test_style(void);
static void test_ranges(void);
static void test_loop(void);
static void test_refusals(void);
static void sine_points(struct pdf_factor_point *points, size_t count, double length, double amplitude);
static int measure_rows(const struct pdf_point *whole, size_t whole_count, const struct pdf_outline_range *ranges, size_t range_count, double *sum, double *uni, double *whole_area, double *worst);
static void row_spans(const struct pdf_point *corners, size_t count, double y, struct spans *spans);
static double spans_length(const struct spans *spans);
static double union_length(struct spans *spans);
static int compare_spans(const void *left, const void *right);
static void span_add(struct spans *spans, double left, double right);
static void bounds_of(const struct pdf_point *corners, size_t count, double *top, double *bottom);
static int write_base(const char *directory);
static int write_documents(const char *directory);
static int write_states(const char *path);
static int write_tools(const char *path);
static int write_seam(const char *path);
static int write_update(const char *base_path, const char *path);
static void make_grain(unsigned char *grain);
static int draw_pencil(struct pdf_writer *writer, size_t grain, double x, double y, double length, double width, double red, double green, double blue);
static int draw_watercolor(struct pdf_writer *writer, double x, double y, double length, double width, double red, double green, double blue);
static int draw_brush(struct pdf_writer *writer, double x, double y, double length, double width);
static int fill_grain_halves(struct pdf_writer *writer, size_t grain, double width, double height);
static int fill_rectangle(struct pdf_writer *writer, double left, double top, double right, double bottom);
static int check_hashes(struct pdf_writer *writer, const char *path, size_t first, size_t count);
static int file_contains(const char *path, const char *text, int *found);
static int render(const char *path, const char *prefix, double dpi);
static int compare(const char *first, const char *second, double mean_max, double fraction_max);
static int check_grain(const char *path, double dpi);
static int clean(const char *path, const char *out);
static int write_patterns(const char *path);
static int read_ppm(const char *path, unsigned char **pixels, int *width, int *height);
static int read_file(const char *path, unsigned char **data, size_t *size);
static double next_unit(unsigned long *state);
static void check(int ok, const char *what);

/* How many checks failed; the program fails when any did. */
static long failures;

/* How many checks passed, for the summary line. */
static long passes;

/*
 * Runs the mode the arguments name.
 */
int
main(
	int argc,
	char **argv)
{
	int error;

	/* The mode, then its arguments. */
	error = EINVAL;
	if (argc == 2 && strcmp(argv[1], "outline") == 0) {
		error = run_outline();
	} else if (argc == 3 && strcmp(argv[1], "base") == 0) {
		error = write_base(argv[2]);
	} else if (argc == 3 && strcmp(argv[1], "write") == 0) {
		error = write_documents(argv[2]);
	} else if (argc == 5 && strcmp(argv[1], "render") == 0) {
		error = render(argv[2], argv[3], atof(argv[4]));
	} else if (argc == 4 && strcmp(argv[1], "compare") == 0) {
		error = compare(argv[2], argv[3], COMPARE_MEAN_MAX, COMPARE_FRACTION_MAX);
	} else if (argc == 6 && strcmp(argv[1], "compare") == 0) {
		error = compare(argv[2], argv[3], atof(argv[4]), atof(argv[5]));
	} else if (argc == 4 && strcmp(argv[1], "grain") == 0) {
		error = check_grain(argv[2], atof(argv[3]));
	} else if (argc == 4 && strcmp(argv[1], "clean") == 0) {
		error = clean(argv[2], argv[3]);
	} else if (argc == 3 && strcmp(argv[1], "patterns") == 0) {
		error = write_patterns(argv[2]);
	} else {
		fprintf(stderr, "usage: host-pdf-tools outline | base DIR | write DIR | render PDF PREFIX DPI | compare A B [MEAN FRACTION] | grain PPM DPI | clean PDF OUT\n");
		return 2;
	}

	/* Reports the mode's failure. */
	if (error != 0) {
		printf("error %d\n", error);
		return 1;
	}
	if (failures != 0) {
		printf("FAILED (%ld of %ld checks)\n", failures, failures + passes);
		return 1;
	}

	/* Succeeded. */
	printf("ok (%ld checks)\n", passes);
	return 0;
}

/* Runs the outline tests (A2, A3). */
static int
run_outline(void)
{
	/* The pen against the old outline, the style, the ranges, a loop, the refusals. */
	test_golden();
	test_style();
	test_ranges();
	test_loop();
	test_refusals();

	/* The checks counted their failures. */
	return 0;
}

/*
 * Compares pdf_outline_stroke() with main f9077504e's on many strokes:
 * random walks with repeated points and pressures outside 0..1, zigzags,
 * hairpins and dots.  Every corner must be the same double (A2).
 */
static void
test_golden(void)
{
	struct pdf_stroke_point *points;
	struct pdf_point *now;
	struct pdf_point *before;
	size_t now_count;
	size_t before_count;
	size_t count;
	size_t index;
	unsigned long state;
	double width;
	double x;
	double y;
	double heading;
	int stroke;
	int error_now;
	int error_before;
	int same;
	long differing;

	/* 600 strokes of up to 300 points. */
	points = malloc(300 * sizeof(*points));
	if (points == NULL) {
		check(0, "golden: memory");
		return;
	}
	state = 12345UL;
	differing = 0;
	for (stroke = 0; stroke < 600; stroke++) {
		count = 1 + (size_t)(next_unit(&state) * 299.0);
		if (stroke % 50 == 0)
			count = 1;
		width = 0.2 + next_unit(&state) * 30.0;
		x = 100.0 + next_unit(&state) * 400.0;
		y = 100.0 + next_unit(&state) * 600.0;
		heading = next_unit(&state) * 6.283;
		for (index = 0; index < count; index++) {
			/* Smooth walks, sharp zigzags (every third stroke), repeated points and wild pressures. */
			heading += (next_unit(&state) - 0.5) * 0.6;
			if (stroke % 3 == 1 && index % 2 == 1)
				heading += 2.8;
			if (next_unit(&state) > 0.1) {
				x += cos(heading) * (0.05 + next_unit(&state) * 4.0);
				y += sin(heading) * (0.05 + next_unit(&state) * 4.0);
			}
			points[index].x = floor(x * 64.0) / 64.0;
			points[index].y = floor(y * 64.0) / 64.0;
			points[index].pressure = next_unit(&state) * 1.4 - 0.2;
		}

		/* Both outlines of the stroke. */
		error_now = pdf_outline_stroke(points, count, width, &now, &now_count);
		error_before = old_pdf_outline_stroke(points, count, width, &before, &before_count);
		same = 0;
		if (error_now == error_before) {
			same = 1;
			if (error_now == 0) {
				if (now_count != before_count)
					same = 0;
				else if (memcmp(now, before, now_count * sizeof(*now)) != 0)
					same = 0;
			}
		}
		if (!same)
			differing++;
		if (error_now == 0)
			pdf_outline_free(now);
		if (error_before == 0)
			old_pdf_outline_free(before);
	}
	free(points);
	printf("golden: 600 strokes, %ld differ from f9077504e\n", differing);
	check(differing == 0, "golden: the pen's outline is bit for bit the old one");
}

/*
 * The style's widths: the pen's style through pdf_outline_stroke_style()
 * gives the pen's outline; a factor of 2 doubles the width; radius_min
 * holds a factor of 0 (floor 0) at its width.
 */
static void
test_style(void)
{
	struct pdf_stroke_point pen[40];
	struct pdf_factor_point factors[40];
	struct pdf_outline_style style;
	struct pdf_point *first;
	struct pdf_point *second;
	size_t first_count;
	size_t second_count;
	size_t index;
	double widest;
	int error;

	/* A curve with pressures, as the pen and as factors of the pen's style. */
	for (index = 0; index < 40; index++) {
		pen[index].x = 50.0 + (double)index * 3.0;
		pen[index].y = 80.0 + 20.0 * sin((double)index * 0.2);
		pen[index].pressure = 0.1 + 0.02 * (double)index;
		factors[index].x = pen[index].x;
		factors[index].y = pen[index].y;
		factors[index].factor = pen[index].pressure;
	}
	style.floor = 0.15;
	style.exponent = 0.7;
	style.factor_max = 1.0;
	style.radius_min = 0.0;
	error = pdf_outline_stroke(pen, 40, 6.0, &first, &first_count);
	check(error == 0, "style: pen outline");
	error = pdf_outline_stroke_style(factors, 40, 6.0, &style, &second, &second_count);
	check(error == 0, "style: factor outline");
	if (error == 0) {
		check(first_count == second_count && memcmp(first, second, first_count * sizeof(*first)) == 0, "style: the pen's style is the pen's outline");
		pdf_outline_free(first);
		pdf_outline_free(second);
	}

	/* A straight line at a factor of 2 (floor 0, exponent 1) is twice as wide. */
	for (index = 0; index < 40; index++) {
		factors[index].x = 10.0 + (double)index * 2.0;
		factors[index].y = 50.0;
		factors[index].factor = 2.5;
	}
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;
	error = pdf_outline_stroke_style(factors, 40, 8.0, &style, &first, &first_count);
	check(error == 0, "style: factor 2");
	if (error == 0) {
		widest = 0.0;
		for (index = 0; index < first_count; index++) {
			if (fabs(first[index].y - 50.0) > widest)
				widest = fabs(first[index].y - 50.0);
		}
		check(fabs(widest - 8.0) < 1e-9, "style: a factor clamped to 2 draws twice the width");
		pdf_outline_free(first);
	}

	/* A factor of 0 keeps the least half width. */
	for (index = 0; index < 40; index++)
		factors[index].factor = 0.0;
	error = pdf_outline_stroke_style(factors, 40, 8.0, &style, &first, &first_count);
	check(error == 0, "style: factor 0");
	if (error == 0) {
		widest = 0.0;
		for (index = 0; index < first_count; index++) {
			if (fabs(first[index].y - 50.0) > widest)
				widest = fabs(first[index].y - 50.0);
		}
		check(fabs(widest - 0.05) < 1e-9, "style: radius_min holds a factor of 0");
		pdf_outline_free(first);
	}
}

/*
 * The ranges of gentle strokes: the seams' corners shared bit for bit, the
 * seams' edges where section 1.2 says, the starts the cuts', and the areas
 * row by row the whole stroke's without overlap (A3).
 */
static void
test_ranges(void)
{
	struct pdf_factor_point *points;
	struct pdf_outline_style style;
	struct pdf_outline_range *ranges;
	struct pdf_point *whole;
	size_t cuts[40];
	size_t whole_count;
	size_t range_count;
	size_t cut_count;
	size_t count;
	size_t index;
	size_t taken;
	double sum;
	double uni;
	double whole_area;
	double worst;
	int shape;
	int error;
	int seams_ok;
	int starts_ok;

	/* Notes' pencil style. */
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;

	/* Three gentle strokes: a long low wave, a tighter wave, a wide arc. */
	count = 400;
	points = malloc(count * sizeof(*points));
	if (points == NULL) {
		check(0, "ranges: memory");
		return;
	}
	for (shape = 0; shape < 3; shape++) {
		if (shape == 0)
			sine_points(points, count, 400.0, 20.0);
		if (shape == 1)
			sine_points(points, count, 200.0, 12.0);
		if (shape == 2) {
			for (index = 0; index < count; index++) {
				points[index].x = 300.0 + 150.0 * cos((double)index / (double)count * 2.5);
				points[index].y = 300.0 + 150.0 * sin((double)index / (double)count * 2.5);
				points[index].factor = 0.6 + 0.4 * sin((double)index * 0.05);
			}
		}

		/* A cut every 20 points. */
		cut_count = 0;
		for (index = 20; index < count; index += 20)
			cuts[cut_count++] = index;

		/* The whole outline and the ranges. */
		error = pdf_outline_stroke_style(points, count, 2.4, &style, &whole, &whole_count);
		check(error == 0, "ranges: whole");
		if (error != 0)
			continue;
		error = pdf_outline_stroke_ranges(points, count, 2.4, &style, cuts, cut_count, &ranges, &range_count);
		check(error == 0, "ranges: ranges");
		if (error != 0) {
			pdf_outline_free(whole);
			continue;
		}

		/* The seams: the ends of the stroke round, the cuts flat, each cut's two corners the next range's. */
		seams_ok = 1;
		if (ranges[0].seam_start != PDF_OUTLINE_NO_SEAM)
			seams_ok = 0;
		if (ranges[range_count - 1].seam_end != PDF_OUTLINE_NO_SEAM)
			seams_ok = 0;
		for (index = 0; index + 1 < range_count; index++) {
			if (ranges[index].seam_end == PDF_OUTLINE_NO_SEAM || ranges[index + 1].seam_start != ranges[index + 1].count - 1) {
				seams_ok = 0;
				continue;
			}
			if (memcmp(&ranges[index].corners[ranges[index].seam_end], &ranges[index + 1].corners[0], sizeof(struct pdf_point)) != 0)
				seams_ok = 0;
			if (memcmp(&ranges[index].corners[ranges[index].seam_end + 1], &ranges[index + 1].corners[ranges[index + 1].count - 1], sizeof(struct pdf_point)) != 0)
				seams_ok = 0;
		}
		check(seams_ok, "ranges: the seams' corners are shared bit for bit and named as promised");

		/* The starts: 0, then cuts in order. */
		starts_ok = 1;
		if (ranges[0].start != 0)
			starts_ok = 0;
		taken = 0;
		for (index = 1; index < range_count; index++) {
			while (taken < cut_count && cuts[taken] < ranges[index].start)
				taken++;
			if (taken == cut_count || cuts[taken] != ranges[index].start)
				starts_ok = 0;
		}
		check(starts_ok, "ranges: each range starts at a cut's point");
		check(range_count > cut_count / 2, "ranges: most cuts of a gentle stroke are taken");

		/* The areas, row by row. */
		error = measure_rows(whole, whole_count, ranges, range_count, &sum, &uni, &whole_area, &worst);
		check(error == 0, "ranges: area measure");
		printf("ranges %d: %lu cuts, %lu ranges, area whole %.6f, sum %.6f, union %.6f, worst row %.3g\n",
		    shape, (unsigned long)cut_count, (unsigned long)range_count, whole_area, sum, uni, worst);
		check(fabs(sum - whole_area) <= 1e-6 * whole_area, "ranges: the ranges' areas add up to the whole stroke's");
		check(fabs(uni - whole_area) <= 1e-6 * whole_area, "ranges: the ranges cover the whole stroke and nothing else");
		check(worst <= 1e-9, "ranges: no row has the ranges overlapping or leaving a gap");
		pdf_outline_ranges_free(ranges, range_count);
		pdf_outline_free(whole);
	}
	free(points);

	/* A tight zigzag, wider than its teeth, has no gentle point: its cuts are left out, one range with round ends. */
	points = malloc(30 * sizeof(*points));
	if (points == NULL)
		return;
	for (index = 0; index < 30; index++) {
		points[index].x = 20.0 + (double)index * 1.0;
		points[index].y = 40.0 + ((index % 2 == 0) ? 0.0 : 1.5);
		points[index].factor = 1.0;
	}
	cuts[0] = 10;
	cuts[1] = 20;
	error = pdf_outline_stroke_ranges(points, 30, 6.0, &style, cuts, 2, &ranges, &range_count);
	check(error == 0, "ranges: zigzag");
	if (error == 0) {
		check(range_count == 1, "ranges: a cut without a gentle point is left out");
		check(ranges[0].start == 0 && ranges[0].seam_start == PDF_OUTLINE_NO_SEAM && ranges[0].seam_end == PDF_OUTLINE_NO_SEAM,
		    "ranges: one range has no seams");
		pdf_outline_ranges_free(ranges, range_count);
	}

	/* A stroke that never moved is one dot, its cuts ignored. */
	for (index = 0; index < 30; index++) {
		points[index].x = 70.0;
		points[index].y = 70.0;
	}
	error = pdf_outline_stroke_ranges(points, 30, 2.0, &style, cuts, 2, &ranges, &range_count);
	check(error == 0 && range_count == 1 && ranges[0].count > 3, "ranges: a dot");
	if (error == 0)
		pdf_outline_ranges_free(ranges, range_count);
	free(points);
}

/*
 * A loop that crosses itself between ranges: the ranges' areas add up to
 * more than the whole stroke's, which fills the crossing once (the limit
 * section 1.2 records).
 */
static void
test_loop(void)
{
	struct pdf_factor_point points[300];
	struct pdf_outline_style style;
	struct pdf_outline_range *ranges;
	struct pdf_point *whole;
	size_t cuts[40];
	size_t whole_count;
	size_t range_count;
	size_t cut_count;
	size_t index;
	double sum;
	double uni;
	double whole_area;
	double worst;
	int error;

	/* One and a quarter turns of a circle of 40 pt, the pencil's style. */
	for (index = 0; index < 300; index++) {
		points[index].x = 200.0 + 40.0 * cos((double)index / 300.0 * 7.85);
		points[index].y = 200.0 + 40.0 * sin((double)index / 300.0 * 7.85);
		points[index].factor = 1.0;
	}
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;
	cut_count = 0;
	for (index = 25; index < 300; index += 25)
		cuts[cut_count++] = index;
	error = pdf_outline_stroke_style(points, 300, 3.0, &style, &whole, &whole_count);
	check(error == 0, "loop: whole");
	if (error != 0)
		return;
	error = pdf_outline_stroke_ranges(points, 300, 3.0, &style, cuts, cut_count, &ranges, &range_count);
	check(error == 0, "loop: ranges");
	if (error != 0) {
		pdf_outline_free(whole);
		return;
	}
	(void)measure_rows(whole, whole_count, ranges, range_count, &sum, &uni, &whole_area, &worst);
	printf("loop: %lu ranges, area whole %.4f, sum %.4f (%.4f of the whole), union %.4f\n",
	    (unsigned long)range_count, whole_area, sum, sum / whole_area, uni);
	check(sum > whole_area * 1.01, "loop: the crossing between ranges is painted twice (the recorded limit)");
	check(fabs(uni - whole_area) <= 1e-6 * whole_area, "loop: the ranges still cover the whole stroke");
	pdf_outline_ranges_free(ranges, range_count);
	pdf_outline_free(whole);
}

/* The refusals of bad input (A3). */
static void
test_refusals(void)
{
	struct pdf_factor_point points[10];
	struct pdf_outline_style style;
	struct pdf_outline_range *ranges;
	struct pdf_point *outline;
	size_t outline_count;
	size_t range_count;
	size_t cuts[3];
	size_t index;
	int error;

	/* A straight stroke and a sound style. */
	for (index = 0; index < 10; index++) {
		points[index].x = (double)index;
		points[index].y = 1.0;
		points[index].factor = 1.0;
	}
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;

	/* Cuts out of order, at 0, past the points, missing. */
	cuts[0] = 5;
	cuts[1] = 3;
	error = pdf_outline_stroke_ranges(points, 10, 1.0, &style, cuts, 2, &ranges, &range_count);
	check(error == EINVAL, "refuse: cuts out of order");
	cuts[0] = 0;
	error = pdf_outline_stroke_ranges(points, 10, 1.0, &style, cuts, 1, &ranges, &range_count);
	check(error == EINVAL, "refuse: a cut at 0");
	cuts[0] = 10;
	error = pdf_outline_stroke_ranges(points, 10, 1.0, &style, cuts, 1, &ranges, &range_count);
	check(error == EINVAL, "refuse: a cut past the points");
	error = pdf_outline_stroke_ranges(points, 10, 1.0, &style, NULL, 1, &ranges, &range_count);
	check(error == EINVAL, "refuse: missing cuts");

	/* A missing style, and styles out of range. */
	error = pdf_outline_stroke_style(points, 10, 1.0, NULL, &outline, &outline_count);
	check(error == EINVAL, "refuse: no style");
	style.floor = 1.5;
	error = pdf_outline_stroke_style(points, 10, 1.0, &style, &outline, &outline_count);
	check(error == EINVAL, "refuse: a floor past 1");
	style.floor = 0.0;
	style.exponent = 0.0;
	error = pdf_outline_stroke_style(points, 10, 1.0, &style, &outline, &outline_count);
	check(error == EINVAL, "refuse: an exponent of 0");
	style.exponent = 1.0;
	style.factor_max = -1.0;
	error = pdf_outline_stroke_style(points, 10, 1.0, &style, &outline, &outline_count);
	check(error == EINVAL, "refuse: a negative factor limit");
	style.factor_max = 2.0;

	/* A factor that is not a number, and a width of 0. */
	points[4].factor = sqrt(-1.0);
	error = pdf_outline_stroke_style(points, 10, 1.0, &style, &outline, &outline_count);
	check(error == EINVAL, "refuse: a NaN factor");
	points[4].factor = 1.0;
	error = pdf_outline_stroke_style(points, 10, 0.0, &style, &outline, &outline_count);
	check(error == EINVAL, "refuse: a width of 0");
}

/* Fills points along a sine wave from (40, 300), its factor changing slowly. */
static void
sine_points(
	struct pdf_factor_point *points,
	size_t count,
	double length,
	double amplitude)
{
	size_t index;
	double t;

	/* Each point along the wave. */
	for (index = 0; index < count; index++) {
		t = (double)index / (double)(count - 1);
		points[index].x = 40.0 + t * length;
		points[index].y = 300.0 + amplitude * sin(t * 12.0);
		points[index].factor = 0.6 + 0.5 * sin(t * 9.0);
	}
}

/*
 * Measures, row by row, the nonzero length of the whole polygon, the sum
 * of the ranges' and their union's; worst is the largest difference of
 * either from the whole's on one row.
 */
static int
measure_rows(
	const struct pdf_point *whole,
	size_t whole_count,
	const struct pdf_outline_range *ranges,
	size_t range_count,
	double *sum,
	double *uni,
	double *whole_area,
	double *worst)
{
	struct spans row;
	struct spans all;
	double top;
	double bottom;
	double y;
	double whole_length;
	double sum_length;
	double union_row;
	size_t index;

	/* The rows across the whole polygon, offset so that none passes through a corner exactly. */
	bounds_of(whole, whole_count, &top, &bottom);
	memset(&row, 0, sizeof(row));
	memset(&all, 0, sizeof(all));
	*sum = 0.0;
	*uni = 0.0;
	*whole_area = 0.0;
	*worst = 0.0;
	for (y = top + AREA_STEP * 0.5 + 1.234567e-7; y < bottom; y += AREA_STEP) {
		row.count = 0;
		row_spans(whole, whole_count, y, &row);
		whole_length = spans_length(&row);
		all.count = 0;
		for (index = 0; index < range_count; index++)
			row_spans(ranges[index].corners, ranges[index].count, y, &all);
		sum_length = spans_length(&all);
		union_row = union_length(&all);
		*whole_area += whole_length * AREA_STEP;
		*sum += sum_length * AREA_STEP;
		*uni += union_row * AREA_STEP;
		if (fabs(sum_length - whole_length) > *worst)
			*worst = fabs(sum_length - whole_length);
		if (fabs(union_row - whole_length) > *worst)
			*worst = fabs(union_row - whole_length);
	}
	free(row.items);
	free(all.items);
	return 0;
}

/* Adds to spans the stretches of the row y inside a polygon by the nonzero rule. */
static void
row_spans(
	const struct pdf_point *corners,
	size_t count,
	double y,
	struct spans *spans)
{
	struct span *crossings;
	size_t crossing_count;
	size_t index;
	size_t next;
	size_t place;
	double x0;
	double y0;
	double x1;
	double y1;
	double left;
	int winding;
	struct span moving;

	/* Each edge that crosses the row gives its x and its direction (kept in the span's right). */
	crossings = malloc(count * sizeof(*crossings));
	if (crossings == NULL)
		return;
	crossing_count = 0;
	for (index = 0; index < count; index++) {
		next = (index + 1) % count;
		x0 = corners[index].x;
		y0 = corners[index].y;
		x1 = corners[next].x;
		y1 = corners[next].y;
		if (y0 <= y && y < y1) {
			crossings[crossing_count].left = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
			crossings[crossing_count].right = 1.0;
			crossing_count++;
		} else if (y1 <= y && y < y0) {
			crossings[crossing_count].left = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
			crossings[crossing_count].right = -1.0;
			crossing_count++;
		}
	}

	/* The crossings from the left. */
	for (index = 1; index < crossing_count; index++) {
		moving = crossings[index];
		place = index;
		while (place > 0 && crossings[place - 1].left > moving.left) {
			crossings[place] = crossings[place - 1];
			place--;
		}
		crossings[place] = moving;
	}

	/* The stretches where the winding is not zero. */
	winding = 0;
	left = 0.0;
	for (index = 0; index < crossing_count; index++) {
		if (winding == 0)
			left = crossings[index].left;
		winding += (int)crossings[index].right;
		if (winding == 0)
			span_add(spans, left, crossings[index].left);
	}
	free(crossings);
}

/* Adds up the lengths of spans. */
static double
spans_length(
	const struct spans *spans)
{
	double length;
	size_t index;

	/* Each span's length. */
	length = 0.0;
	for (index = 0; index < spans->count; index++)
		length += spans->items[index].right - spans->items[index].left;
	return length;
}

/* Measures the length of the union of spans (sorting them). */
static double
union_length(
	struct spans *spans)
{
	double length;
	double left;
	double right;
	size_t index;

	/* Nothing covers nothing. */
	if (spans->count == 0)
		return 0.0;

	/* Merges the spans from the left. */
	qsort(spans->items, spans->count, sizeof(*spans->items), compare_spans);
	length = 0.0;
	left = spans->items[0].left;
	right = spans->items[0].right;
	for (index = 1; index < spans->count; index++) {
		if (spans->items[index].left <= right) {
			if (spans->items[index].right > right)
				right = spans->items[index].right;
			continue;
		}
		length += right - left;
		left = spans->items[index].left;
		right = spans->items[index].right;
	}
	length += right - left;
	return length;
}

/* Orders spans by their left ends. */
static int
compare_spans(
	const void *left,
	const void *right)
{
	const struct span *a;
	const struct span *b;

	/* The left ends decide. */
	a = left;
	b = right;
	if (a->left < b->left)
		return -1;
	if (a->left > b->left)
		return 1;
	return 0;
}

/* Appends a span. */
static void
span_add(
	struct spans *spans,
	double left,
	double right)
{
	struct span *grown;
	size_t capacity;

	/* Grows the array when full. */
	if (spans->count == spans->capacity) {
		capacity = spans->capacity * 2;
		if (capacity == 0)
			capacity = 64;
		grown = realloc(spans->items, capacity * sizeof(*grown));
		if (grown == NULL)
			return;
		spans->items = grown;
		spans->capacity = capacity;
	}
	spans->items[spans->count].left = left;
	spans->items[spans->count].right = right;
	spans->count++;
}

/* Finds the top and bottom of a polygon. */
static void
bounds_of(
	const struct pdf_point *corners,
	size_t count,
	double *top,
	double *bottom)
{
	size_t index;

	/* The smallest and largest y. */
	*top = corners[0].y;
	*bottom = corners[0].y;
	for (index = 1; index < count; index++) {
		if (corners[index].y < *top)
			*top = corners[index].y;
		if (corners[index].y > *bottom)
			*bottom = corners[index].y;
	}
}

/* Writes the base page an update draws over: an A4 page with a white frame, turned a quarter by the script. */
static int
write_base(
	const char *directory)
{
	struct pdf_writer *writer;
	char path[1024];
	int error;

	/* One page with a white frame (content that does not change the grain's background). */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	error = pdf_writer_begin_page(writer, 595.0, 842.0);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 1.0, 1.0, 1.0, 1.0);
	if (error == 0)
		error = fill_rectangle(writer, 20.0, 20.0, 575.0, 30.0);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	snprintf(path, sizeof(path), "%s/base.pdf", directory);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	return error;
}

/* Writes the documents of the test and checks their states and hashes (A4, A7). */
static int
write_documents(
	const char *directory)
{
	char path[1024];
	char base[1024];
	int error;

	/* The states without Flate and Multiply. */
	snprintf(path, sizeof(path), "%s/states.pdf", directory);
	error = write_states(path);
	if (error != 0)
		return error;

	/* The tools with Flate, grains and Multiply. */
	snprintf(path, sizeof(path), "%s/tools.pdf", directory);
	error = write_tools(path);
	if (error != 0)
		return error;

	/* The seam's two pages. */
	snprintf(path, sizeof(path), "%s/seam.pdf", directory);
	error = write_seam(path);
	if (error != 0)
		return error;

	/* The update over the turned page. */
	snprintf(base, sizeof(base), "%s/base-rot.pdf", directory);
	snprintf(path, sizeof(path), "%s/update.pdf", directory);
	error = write_update(base, path);
	if (error != 0)
		return error;
	return 0;
}

/*
 * Writes a page whose fills save and restore the state, and checks that a
 * fill after the restore selects its opacity again, and that a document
 * without Multiply or Flate writes neither /BM nor a filter (A4, A7).
 */
static int
write_states(
	const char *path)
{
	struct pdf_writer *writer;
	unsigned char *data;
	size_t size;
	int found;
	int error;

	/* Opacity 0.5; saved; opacity 0.3; restored (0.5 again); opacity 0.3 must be selected again. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	error = pdf_writer_begin_page(writer, 200.0, 200.0);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 1.0, 0.0, 0.0, 0.5);
	if (error == 0)
		error = fill_rectangle(writer, 10.0, 10.0, 60.0, 60.0);
	if (error == 0)
		error = pdf_writer_save_state(writer);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.0, 1.0, 0.0, 0.3);
	if (error == 0)
		error = fill_rectangle(writer, 40.0, 40.0, 90.0, 90.0);
	if (error == 0)
		error = pdf_writer_restore_state(writer);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.0, 0.0, 1.0, 0.3);
	if (error == 0)
		error = fill_rectangle(writer, 70.0, 70.0, 120.0, 120.0);
	check(pdf_writer_restore_state(writer) == EINVAL, "states: a restore without a save is refused");
	if (error == 0)
		error = pdf_writer_save_state(writer);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	pdf_writer_destroy(writer);
	if (error != 0)
		return error;

	/* The page's content as written. */
	error = read_file(path, &data, &size);
	if (error != 0)
		return error;
	free(data);
	error = file_contains(path, "Q\n0 0 1 rg\n/GS1 gs\n", &found);
	check(error == 0 && found, "states: the fill after Q selects its opacity again");
	error = file_contains(path, "q\nQ\n", &found);
	check(error == 0 && found, "states: a page ended with a save restores it");
	error = file_contains(path, "/BM", &found);
	check(error == 0 && !found, "states: no blend mode without Multiply");
	error = file_contains(path, "/Filter", &found);
	check(error == 0 && !found, "states: no filter without Flate");
	return 0;
}

/*
 * Writes the tools' document with Flate: page 1 the pen, the brush, the
 * watercolour and the pencil; page 2 (a page of another height) the grain
 * layout; page 3 two hundred pencil strokes of three ranges.  Checks the
 * hashes and the blend modes written (A4, A7, A8).
 */
static int
write_tools(
	const char *path)
{
	struct pdf_writer *writer;
	struct pdf_stroke_point pen[60];
	struct pdf_point *outline;
	unsigned char *grain_data;
	size_t outline_count;
	size_t grain;
	size_t index;
	int found;
	int stroke;
	int error;

	/* The writer, with Flate and the grain. */
	error = pdf_writer_create(&writer);
	if (error != 0)
		return error;
	error = pdf_writer_set_content_flate(writer, 1);
	grain_data = malloc(GRAIN_SIDE * GRAIN_SIDE);
	if (grain_data == NULL)
		error = ENOMEM;
	if (error == 0) {
		make_grain(grain_data);
		error = pdf_writer_add_grain(writer, grain_data, GRAIN_SIDE, GRAIN_SIDE, &grain);
	}
	free(grain_data);

	/* Page 1: a pen stroke, the brush, the watercolour over itself, the pencil. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.0, 842.0);
	for (index = 0; index < 60; index++) {
		pen[index].x = 60.0 + (double)index * 7.0;
		pen[index].y = 80.0 + 15.0 * sin((double)index * 0.15);
		pen[index].pressure = 0.3 + 0.01 * (double)index;
	}
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.1, 0.1, 0.1, 1.0);
	if (error == 0)
		error = pdf_outline_stroke(pen, 60, 3.0, &outline, &outline_count);
	if (error == 0) {
		error = pdf_writer_fill_outline(writer, outline, outline_count);
		pdf_outline_free(outline);
	}
	if (error == 0)
		error = draw_brush(writer, 60.0, 160.0, 420.0, 12.0);
	if (error == 0)
		error = draw_watercolor(writer, 60.0, 260.0, 400.0, 28.0, 0.15, 0.39, 0.92);
	if (error == 0)
		error = draw_watercolor(writer, 120.0, 280.0, 300.0, 28.0, 0.92, 0.35, 0.05);
	for (stroke = 0; stroke < 5 && error == 0; stroke++)
		error = draw_pencil(writer, grain, 60.0, 400.0 + stroke * 30.0, 420.0, 2.4, 0.1 * stroke, 0.1, 0.2);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Page 2: the grain layout on a page of another height. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.0, 600.0);
	if (error == 0)
		error = fill_grain_halves(writer, grain, 595.0, 600.0);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Page 3: two hundred pencil strokes. */
	if (error == 0)
		error = pdf_writer_begin_page(writer, 595.0, 842.0);
	for (stroke = 0; stroke < 200 && error == 0; stroke++)
		error = draw_pencil(writer, grain, 30.0 + (stroke % 4) * 140.0, 30.0 + (stroke / 4) * 16.0, 120.0, 1.4, 0.2, 0.2, 0.3);
	if (error == 0)
		error = pdf_writer_end_page(writer);

	/* Saves and checks the hashes of the compressed pages. */
	if (error == 0)
		error = pdf_writer_save(writer, path);
	if (error == 0)
		error = check_hashes(writer, path, 0, 3);
	pdf_writer_destroy(writer);
	if (error != 0)
		return error;

	/* The blend modes: Multiply, and Normal named once Multiply is used. */
	error = file_contains(path, "/BM /Multiply", &found);
	check(error == 0 && found, "tools: the watercolour multiplies");
	error = file_contains(path, "/BM /Normal", &found);
	check(error == 0 && found, "tools: a state of Normal is named in a document that multiplies");
	error = file_contains(path, "/Filter /FlateDecode /Length", &found);
	check(error == 0 && found, "tools: the content is compressed");
	error = file_contains(path, "/PatternType 1 /PaintType 1", &found);
	check(error == 0 && found, "tools: the grain is a colored tiling pattern");
	return 0;
}

/*
 * Writes the seam's pages: one stroke cut into ranges filled one by one in
 * one solid colour and alpha, and the same stroke whole (A10).
 */
static int
write_seam(
	const char *path)
{
	struct pdf_writer *writer;
	struct pdf_factor_point points[200];
	struct pdf_outline_style style;
	struct pdf_outline_range *ranges;
	struct pdf_point *whole;
	size_t cuts[20];
	size_t cut_count;
	size_t range_count;
	size_t whole_count;
	size_t index;
	int error;

	/* A gentle wave, cut every 20 points. */
	sine_points(points, 200, 160.0, 10.0);
	for (index = 0; index < 200; index++)
		points[index].y -= 250.0;
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;
	cut_count = 0;
	for (index = 20; index < 200; index += 20)
		cuts[cut_count++] = index;
	error = pdf_outline_stroke_ranges(points, 200, 2.4, &style, cuts, cut_count, &ranges, &range_count);
	if (error != 0)
		return error;
	error = pdf_outline_stroke_style(points, 200, 2.4, &style, &whole, &whole_count);
	if (error != 0) {
		pdf_outline_ranges_free(ranges, range_count);
		return error;
	}

	/* Page 1 the ranges, page 2 the whole stroke, both in black at 0.6. */
	error = pdf_writer_create(&writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 240.0, 100.0);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.0, 0.0, 0.0, 0.6);
	for (index = 0; index < range_count && error == 0; index++)
		error = pdf_writer_fill_outline(writer, ranges[index].corners, ranges[index].count);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 240.0, 100.0);
	if (error == 0)
		error = pdf_writer_set_fill_color(writer, 0.0, 0.0, 0.0, 0.6);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, whole, whole_count);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	printf("seam: %lu ranges\n", (unsigned long)range_count);
	pdf_writer_destroy(writer);
	pdf_outline_ranges_free(ranges, range_count);
	pdf_outline_free(whole);
	return error;
}

/*
 * Writes an update over a page turned a quarter: the grain layout on the
 * page as shown, and an added page with the grain, compressed (A6, A7).
 */
static int
write_update(
	const char *base_path,
	const char *path)
{
	struct pdf_document *base;
	struct pdf_writer *writer;
	struct pdf_page_box box;
	unsigned char *grain_data;
	size_t grain;
	int error;

	/* The base and its page as shown. */
	error = pdf_document_open(base_path, &base);
	if (error != 0)
		return error;
	error = pdf_document_page_box(base, 0, &box);
	printf("update: base page shown %gx%g, rotation %d\n", box.width, box.height, box.rotation);
	check(error == 0 && box.rotation == 90, "update: the base page is turned a quarter");

	/* The update, compressed, with the grain. */
	if (error == 0)
		error = pdf_writer_create_update(base, &writer);
	if (error != 0) {
		pdf_document_close(base);
		return error;
	}
	error = pdf_writer_set_content_flate(writer, 1);
	grain_data = malloc(GRAIN_SIDE * GRAIN_SIDE);
	if (grain_data == NULL)
		error = ENOMEM;
	if (error == 0) {
		make_grain(grain_data);
		error = pdf_writer_add_grain(writer, grain_data, GRAIN_SIDE, GRAIN_SIDE, &grain);
	}
	free(grain_data);

	/* The turned page drawn over with the grain layout, then an added page with it. */
	if (error == 0)
		error = pdf_writer_begin_page_over(writer, 0, PDF_PAGE_OVERLAY);
	if (error == 0)
		error = fill_grain_halves(writer, grain, box.width, box.height);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_begin_page(writer, 300.0, 400.0);
	if (error == 0)
		error = fill_grain_halves(writer, grain, 300.0, 400.0);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_save(writer, path);
	if (error == 0)
		error = check_hashes(writer, path, 1, 1);
	pdf_writer_destroy(writer);
	pdf_document_close(base);
	return error;
}

/*
 * Makes the test's grain: eight by eight blocks of 32 texels, each its own
 * level (20 + 3 * (x + 8 y)), so that a grain turned or moved is seen.
 */
static void
make_grain(
	unsigned char *grain)
{
	size_t x;
	size_t y;
	size_t block_x;
	size_t block_y;

	/* Each texel takes its block's level. */
	for (y = 0; y < GRAIN_SIDE; y++) {
		for (x = 0; x < GRAIN_SIDE; x++) {
			block_x = x / (GRAIN_SIDE / GRAIN_BLOCKS);
			block_y = y / (GRAIN_SIDE / GRAIN_BLOCKS);
			grain[y * GRAIN_SIDE + x] = (unsigned char)(20 + 3 * (block_x + GRAIN_BLOCKS * block_y));
		}
	}
}

/*
 * Draws a pencil stroke as Notes will: ranges of steps of darkness, each a
 * plain underlay at the floor's share of its alpha, then the grain at its
 * alpha (plan/ws216/phase001/phase.md section 3.2).
 */
static int
draw_pencil(
	struct pdf_writer *writer,
	size_t grain,
	double x,
	double y,
	double length,
	double width,
	double red,
	double green,
	double blue)
{
	struct pdf_factor_point points[120];
	struct pdf_outline_style style;
	struct pdf_outline_range *ranges;
	size_t cuts[2];
	size_t range_count;
	size_t index;
	double alpha;
	int error;

	/* A gently waving stroke, its darkness stepping up twice. */
	for (index = 0; index < 120; index++) {
		points[index].x = x + (double)index / 119.0 * length;
		points[index].y = y + 3.0 * sin((double)index * 0.08);
		points[index].factor = 0.8 + 0.2 * sin((double)index * 0.1);
	}
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;
	cuts[0] = 40;
	cuts[1] = 80;
	error = pdf_outline_stroke_ranges(points, 120, width, &style, cuts, 2, &ranges, &range_count);
	if (error != 0)
		return error;

	/* Each range: the underlay, then the grain. */
	for (index = 0; index < range_count && error == 0; index++) {
		alpha = 0.30 + 0.70 * (double)(ranges[index].start / 40 + 3) / 7.0;
		if (alpha > 1.0)
			alpha = 1.0;
		error = pdf_writer_set_fill_color(writer, red, green, blue, alpha * GRAIN_FLOOR);
		if (error == 0)
			error = pdf_writer_fill_outline(writer, ranges[index].corners, ranges[index].count);
		if (error == 0)
			error = pdf_writer_set_fill_grain(writer, grain, red, green, blue, alpha, GRAIN_CELL);
		if (error == 0)
			error = pdf_writer_fill_outline(writer, ranges[index].corners, ranges[index].count);
	}
	pdf_outline_ranges_free(ranges, range_count);
	return error;
}

/*
 * Draws a watercolour stroke as Notes will: a core and two wavering edges
 * of the same points, each multiplied at a small alpha (section 3.3).
 */
static int
draw_watercolor(
	struct pdf_writer *writer,
	double x,
	double y,
	double length,
	double width,
	double red,
	double green,
	double blue)
{
	static const double alphas[3] = { 0.22, 0.10, 0.06 };
	static const double scales[3] = { 1.0, 1.18, 1.36 };
	struct pdf_factor_point points[100];
	struct pdf_outline_style style;
	struct pdf_point *outline;
	size_t outline_count;
	size_t index;
	int shape;
	int error;

	/* Multiplies the three shapes. */
	error = pdf_writer_set_blend_mode(writer, PDF_BLEND_MULTIPLY);
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;
	for (shape = 0; shape < 3 && error == 0; shape++) {
		for (index = 0; index < 100; index++) {
			points[index].x = x + (double)index / 99.0 * length;
			points[index].y = y + 20.0 * sin((double)index * 0.05);
			points[index].factor = scales[shape] * (0.85 + 0.15 * 0.8) * (1.0 + 0.08 * sin((double)index * (0.7 + shape)));
		}
		error = pdf_outline_stroke_style(points, 100, width, &style, &outline, &outline_count);
		if (error == 0)
			error = pdf_writer_set_fill_color(writer, red, green, blue, alphas[shape]);
		if (error == 0) {
			error = pdf_writer_fill_outline(writer, outline, outline_count);
			pdf_outline_free(outline);
		}
	}

	/* Back to Normal for what follows. */
	if (error == 0)
		error = pdf_writer_set_blend_mode(writer, PDF_BLEND_NORMAL);
	return error;
}

/* Draws a brush stroke: its factor thin at both ends and fuller in the middle (section 3.1). */
static int
draw_brush(
	struct pdf_writer *writer,
	double x,
	double y,
	double length,
	double width)
{
	struct pdf_factor_point points[100];
	struct pdf_outline_style style;
	struct pdf_point *outline;
	size_t outline_count;
	size_t index;
	double t;
	int error;

	/* Thin in and out, up to 1.15 in the middle. */
	for (index = 0; index < 100; index++) {
		t = (double)index / 99.0;
		points[index].x = x + t * length;
		points[index].y = y + 10.0 * sin(t * 5.0);
		points[index].factor = 0.08 + 1.07 * sin(t * 3.14159);
	}
	style.floor = 0.0;
	style.exponent = 1.0;
	style.factor_max = 2.0;
	style.radius_min = 0.05;
	error = pdf_outline_stroke_style(points, 100, width, &style, &outline, &outline_count);
	if (error != 0)
		return error;
	error = pdf_writer_set_fill_color(writer, 0.05, 0.05, 0.05, 1.0);
	if (error == 0)
		error = pdf_writer_fill_outline(writer, outline, outline_count);
	pdf_outline_free(outline);
	return error;
}

/* Fills the grain layout: the grain in black, opaque above the middle of the page, at half below. */
static int
fill_grain_halves(
	struct pdf_writer *writer,
	size_t grain,
	double width,
	double height)
{
	int error;

	/* The upper half opaque. */
	error = pdf_writer_set_fill_grain(writer, grain, 0.0, 0.0, 0.0, 1.0, GRAIN_CELL);
	if (error == 0)
		error = fill_rectangle(writer, 0.0, 0.0, width, height / 2.0);

	/* The lower half at half. */
	if (error == 0)
		error = pdf_writer_set_fill_grain(writer, grain, 0.0, 0.0, 0.0, 0.5, GRAIN_CELL);
	if (error == 0)
		error = fill_rectangle(writer, 0.0, height / 2.0, width, height);
	return error;
}

/* Fills a rectangle of the page as drawn. */
static int
fill_rectangle(
	struct pdf_writer *writer,
	double left,
	double top,
	double right,
	double bottom)
{
	struct pdf_point corners[4];

	/* The four corners. */
	corners[0].x = left;
	corners[0].y = top;
	corners[1].x = right;
	corners[1].y = top;
	corners[2].x = right;
	corners[2].y = bottom;
	corners[3].x = left;
	corners[3].y = bottom;
	return pdf_writer_fill_outline(writer, corners, 4);
}

/* Checks that the reader's hashes of saved pages are the writer's (A7). */
static int
check_hashes(
	struct pdf_writer *writer,
	const char *path,
	size_t first,
	size_t count)
{
	struct pdf_document *document;
	unsigned char written[32];
	unsigned char read[32];
	char what[128];
	size_t page;
	int error;

	/* Opens the saved file. */
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;

	/* Each page's two hashes. */
	for (page = first; page < first + count; page++) {
		error = pdf_writer_get_page_content_hash(writer, page, written);
		snprintf(what, sizeof(what), "hash: %s page %lu has a writer's hash", path, (unsigned long)(page + 1));
		check(error == 0, what);
		error = pdf_document_page_content_hash(document, page, read);
		snprintf(what, sizeof(what), "hash: %s page %lu, the reader decodes and hashes", path, (unsigned long)(page + 1));
		check(error == 0, what);
		snprintf(what, sizeof(what), "hash: %s page %lu, the reader's hash is the writer's", path, (unsigned long)(page + 1));
		check(memcmp(written, read, 32) == 0, what);
	}
	pdf_document_close(document);
	return 0;
}

/* Tells whether a file's bytes hold a text. */
static int
file_contains(
	const char *path,
	const char *text,
	int *found)
{
	unsigned char *data;
	size_t size;
	size_t length;
	size_t index;
	int error;

	/* Reads the file and looks for the text. */
	error = read_file(path, &data, &size);
	if (error != 0)
		return error;
	*found = 0;
	length = strlen(text);
	for (index = 0; index + length <= size; index++) {
		if (memcmp(data + index, text, length) == 0) {
			*found = 1;
			break;
		}
	}
	free(data);
	return 0;
}

/*
 * Draws every page of a document with libpdf into PREFIX-N.ppm, on white,
 * and prints its list's flags, items, path points, patterns, pixels kept
 * and the time of the drawing (A5, A8).
 */
static int
render(
	const char *path,
	const char *prefix,
	double dpi)
{
	struct pdf_document *document;
	struct pdf_display_list *list;
	const unsigned char *seen[64];
	size_t seen_count;
	uint32_t *pixels;
	unsigned char *rgb;
	char name[1024];
	FILE *file;
	size_t pages;
	size_t page;
	size_t width;
	size_t height;
	size_t index;
	size_t item;
	size_t points;
	size_t patterns;
	size_t kept;
	size_t other;
	double scale;
	clock_t start;
	double elapsed;
	int error;

	/* Opens the document. */
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	pages = pdf_document_page_count(document);
	scale = dpi / 72.0;
	for (page = 0; page < pages; page++) {
		error = pdf_page_render(document, page, &list);
		if (error != 0) {
			pdf_document_close(document);
			return error;
		}

		/* The list's counts: points, pattern fills, and the distinct pixels of images and tiles. */
		points = 0;
		patterns = 0;
		kept = 0;
		seen_count = 0;
		for (item = 0; item < list->count; item++) {
			points += list->items[item].point_count;
			if (list->items[item].type == PDF_ITEM_PATTERN)
				patterns++;
			if (list->items[item].type != PDF_ITEM_PATTERN && list->items[item].type != PDF_ITEM_IMAGE)
				continue;
			for (other = 0; other < seen_count; other++) {
				if (seen[other] == list->items[item].pixels)
					break;
			}
			if (other < seen_count)
				continue;
			if (seen_count < 64)
				seen[seen_count++] = list->items[item].pixels;
			kept += list->items[item].image_width * list->items[item].image_height;
		}

		/* A white target of the page's size at the resolution, drawn. */
		width = (size_t)ceil(list->width * scale - 1e-6);
		height = (size_t)ceil(list->height * scale - 1e-6);
		pixels = malloc(width * height * 4);
		if (pixels == NULL) {
			pdf_display_list_destroy(list);
			pdf_document_close(document);
			return ENOMEM;
		}
		for (index = 0; index < width * height; index++)
			pixels[index] = 0xffffffffU;
		start = clock();
		error = pdf_display_list_rasterize(list, pixels, width, width, height, scale, 0.0, 0.0);
		elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
		printf("render %s page %lu: %lux%lu flags %u items %lu points %lu patterns %lu pixels kept %lu raster %d %.3f s\n",
		    path, (unsigned long)(page + 1), (unsigned long)width, (unsigned long)height, list->flags, (unsigned long)list->count,
		    (unsigned long)points, (unsigned long)patterns, (unsigned long)kept, error, elapsed);
		check(error == 0, "render: the page is drawn");
		check((list->flags & PDF_DISPLAY_LIMITED) == 0, "render: no limit is reached");

		/* Writes the picture. */
		rgb = malloc(width * height * 3);
		if (rgb != NULL) {
			for (index = 0; index < width * height; index++) {
				rgb[index * 3 + 0] = (unsigned char)(pixels[index] >> 16);
				rgb[index * 3 + 1] = (unsigned char)(pixels[index] >> 8);
				rgb[index * 3 + 2] = (unsigned char)pixels[index];
			}
			snprintf(name, sizeof(name), "%s-%lu.ppm", prefix, (unsigned long)(page + 1));
			file = fopen(name, "wb");
			if (file != NULL) {
				fprintf(file, "P6\n%lu %lu\n255\n", (unsigned long)width, (unsigned long)height);
				fwrite(rgb, 1, width * height * 3, file);
				fclose(file);
			}
			free(rgb);
		}
		free(pixels);
		pdf_display_list_destroy(list);
	}
	pdf_document_close(document);
	return 0;
}

/*
 * Compares two pictures over the area they share: the mean difference of
 * the channels and the share of pixels past the threshold (A5).
 */
static int
compare(
	const char *first,
	const char *second,
	double mean_max,
	double fraction_max)
{
	unsigned char *a;
	unsigned char *b;
	int width_a;
	int height_a;
	int width_b;
	int height_b;
	int width;
	int height;
	int x;
	int y;
	int channel;
	int difference;
	int largest;
	long over;
	long any;
	double total;
	double mean;
	double fraction;
	int error;

	/* Reads both pictures. */
	error = read_ppm(first, &a, &width_a, &height_a);
	if (error != 0)
		return error;
	error = read_ppm(second, &b, &width_b, &height_b);
	if (error != 0) {
		free(a);
		return error;
	}

	/* Compares the shared area. */
	width = width_a < width_b ? width_a : width_b;
	height = height_a < height_b ? height_a : height_b;
	total = 0.0;
	over = 0;
	any = 0;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			largest = 0;
			for (channel = 0; channel < 3; channel++) {
				difference = abs((int)a[((size_t)y * width_a + x) * 3 + channel] - (int)b[((size_t)y * width_b + x) * 3 + channel]);
				total += difference;
				if (difference > largest)
					largest = difference;
			}
			if (largest > COMPARE_THRESHOLD)
				over++;
			if (largest > 8)
				any++;
		}
	}
	mean = total / ((double)width * height * 3.0);
	fraction = (double)over / ((double)width * height);
	printf("compare %s %s: %dx%d vs %dx%d, mean %.3f, over %d: %ld (%.4f%%), over 8: %ld\n", first, second, width_a, height_a, width_b, height_b,
	    mean, COMPARE_THRESHOLD, over, fraction * 100.0, any);
	check(mean <= mean_max && fraction <= fraction_max, "compare: within the tolerance");
	check(abs(width_a - width_b) <= 1 && abs(height_a - height_b) <= 1, "compare: the same size");
	free(a);
	free(b);
	return 0;
}

/*
 * Checks the grain layout on a picture at a resolution: at the centre of
 * every block of every repeat (cell points from the page's top left), the
 * black grain's alpha is the block's level, times 1 above the middle of the
 * page and 0.5 below; blocks within two pixels of the middle or an edge are
 * left out (A6).
 */
static int
check_grain(
	const char *path,
	double dpi)
{
	unsigned char *pixels;
	int width;
	int height;
	double scale;
	double page_width;
	double page_height;
	double block;
	double x;
	double y;
	double expected;
	double measured;
	double worst;
	long blocks;
	long misses;
	long column;
	long row;
	int pixel_x;
	int pixel_y;
	int block_x;
	int block_y;
	int error;

	/* Reads the picture and the page's size as shown. */
	error = read_ppm(path, &pixels, &width, &height);
	if (error != 0)
		return error;
	scale = dpi / 72.0;
	page_width = (double)width / scale;
	page_height = (double)height / scale;
	block = GRAIN_CELL / GRAIN_BLOCKS;

	/* Each block centre on the page. */
	blocks = 0;
	misses = 0;
	worst = 0.0;
	for (row = 0; (double)row * block < page_height; row++) {
		for (column = 0; (double)column * block < page_width; column++) {
			x = ((double)column + 0.5) * block;
			y = ((double)row + 0.5) * block;
			if (x > page_width - 2.0 / scale || y > page_height - 2.0 / scale)
				continue;
			if (fabs(y - page_height / 2.0) < block)
				continue;
			pixel_x = (int)floor(x * scale);
			pixel_y = (int)floor(y * scale);
			block_x = (int)(column % GRAIN_BLOCKS);
			block_y = (int)(row % GRAIN_BLOCKS);
			expected = (double)(20 + 3 * (block_x + GRAIN_BLOCKS * block_y)) / 255.0;
			if (y > page_height / 2.0)
				expected *= 0.5;
			measured = 1.0 - (double)pixels[((size_t)pixel_y * (size_t)width + (size_t)pixel_x) * 3] / 255.0;
			blocks++;
			if (fabs(measured - expected) > worst)
				worst = fabs(measured - expected);
			if (fabs(measured - expected) > GRAIN_TOLERANCE)
				misses++;
		}
	}
	free(pixels);
	printf("grain %s: %ld blocks, %ld miss, worst %.4f\n", path, blocks, misses, worst);
	check(blocks > 100, "grain: blocks measured");
	check((double)misses <= (double)blocks * GRAIN_MISS_MAX, "grain: the grain lies where the page as shown puts it, at its alpha");
	return 0;
}

/* Writes the clean copy of a document (A9). */
static int
clean(
	const char *path,
	const char *out)
{
	struct pdf_document *document;
	struct pdf_clean_counts counts;
	int error;

	/* Opens the document and writes its copy. */
	error = pdf_document_open(path, &document);
	if (error != 0)
		return error;
	error = pdf_document_save_clean(document, out, NULL, &counts);
	pdf_document_close(document);
	if (error != 0)
		return error;
	printf("clean %s: %lu objects, %lu resources left out\n", out, (unsigned long)counts.objects, (unsigned long)counts.dropped);
	return 0;
}

/*
 * Writes a page of general tiling patterns by hand (M-7): P1 a cell of two
 * squares, P2 a cell larger than its step and turned, P3 a cell that
 * paints with P1; on page 2, P4 an uncoloured pattern the reader leaves
 * out (the list says it skipped something).
 */
static int
write_patterns(
	const char *path)
{
	static const char *const objects[] = {
		"<< /Type /Catalog /Pages 2 0 R >>",
		"<< /Type /Pages /Kids [3 0 R 9 0 R] /Count 2 >>",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Contents 4 0 R "
		    "/Resources << /Pattern << /P1 5 0 R /P2 6 0 R /P3 7 0 R >> >> >>",
		"STREAM/Pattern cs /P1 scn 10 10 130 130 re f /P2 scn 160 10 130 130 re f "
		    "/P3 scn 10 160 130 130 re f",
		"/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 20 20] /XStep 20 /YStep 20 /Resources << >>"
		    "STREAM1 0 0 rg 0 0 10 10 re f 0 0 1 rg 10 10 10 10 re f",
		"/PatternType 1 /PaintType 1 /TilingType 1 /BBox [-5 -5 15 15] /XStep 10 /YStep 10 "
		    "/Matrix [0.866 0.5 -0.5 0.866 0 0] /Resources << >>"
		    "STREAM0 0.6 0 rg -5 -2 m 15 18 l 15 22 l -5 2 l h f",
		"/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 30 30] /XStep 30 /YStep 30 "
		    "/Resources << /Pattern << /Q 5 0 R >> >>"
		    "STREAM/Pattern cs /Q scn 2 2 26 26 re f 0 0 0 rg 0 0 30 1 re f",
		"/PatternType 1 /PaintType 2 /TilingType 1 /BBox [0 0 10 10] /XStep 10 /YStep 10 /Resources << >>"
		    "STREAM0 0 5 5 re f",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Contents 10 0 R "
		    "/Resources << /Pattern << /P4 8 0 R >> >> >>",
		"STREAM/Pattern cs /P4 scn 160 160 130 130 re f"
	};
	unsigned char *pdf;
	size_t offsets[11];
	size_t length;
	size_t capacity;
	size_t index;
	size_t xref;
	const char *split;
	FILE *file;
	int written;

	/* The objects, a stream's dictionary before STREAM and its content after. */
	capacity = 16384;
	pdf = malloc(capacity);
	if (pdf == NULL)
		return ENOMEM;
	length = (size_t)sprintf((char *)pdf, "%%PDF-1.7\n");
	for (index = 0; index < 10; index++) {
		offsets[index + 1] = length;
		split = strstr(objects[index], "STREAM");
		if (split == NULL) {
			written = sprintf((char *)pdf + length, "%lu 0 obj\n%s\nendobj\n", (unsigned long)(index + 1), objects[index]);
		} else {
			written = sprintf((char *)pdf + length, "%lu 0 obj\n<< %.*s /Length %lu >>\nstream\n%s\nendstream\nendobj\n",
			    (unsigned long)(index + 1), (int)(split - objects[index]), objects[index], (unsigned long)strlen(split + 6), split + 6);
		}
		length += (size_t)written;
	}

	/* The cross-reference table and the trailer, which names where the table starts. */
	xref = length;
	written = sprintf((char *)pdf + length, "xref\n0 11\n0000000000 65535 f \n");
	length += (size_t)written;
	for (index = 1; index <= 10; index++) {
		written = sprintf((char *)pdf + length, "%010lu 00000 n \n", (unsigned long)offsets[index]);
		length += (size_t)written;
	}
	written = sprintf((char *)pdf + length, "trailer\n<< /Size 11 /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n", (unsigned long)xref);
	length += (size_t)written;

	/* Writes the file. */
	file = fopen(path, "wb");
	if (file == NULL) {
		free(pdf);
		return errno;
	}
	fwrite(pdf, 1, length, file);
	fclose(file);
	free(pdf);
	return 0;
}

/* Reads a binary PPM (P6, 255). */
static int
read_ppm(
	const char *path,
	unsigned char **pixels,
	int *width,
	int *height)
{
	FILE *file;
	int maximum;
	int character;
	size_t size;

	/* Reads the header (skipping a comment line, as Ghostscript writes one) and the samples. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	if (fscanf(file, "P6 ") != 0) {
		fclose(file);
		return EINVAL;
	}
	character = fgetc(file);
	while (character == '#') {
		while (character != '\n' && character != EOF)
			character = fgetc(file);
		character = fgetc(file);
	}
	ungetc(character, file);
	if (fscanf(file, "%d %d %d", width, height, &maximum) != 3 || maximum != 255 || *width <= 0 || *height <= 0) {
		fclose(file);
		return EINVAL;
	}
	fgetc(file);
	size = (size_t)*width * (size_t)*height * 3;
	*pixels = malloc(size);
	if (*pixels == NULL || fread(*pixels, 1, size, file) != size) {
		fclose(file);
		free(*pixels);
		return EIO;
	}
	fclose(file);
	return 0;
}

/* Reads a whole file. */
static int
read_file(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	FILE *file;
	long length;

	/* Opens, measures and reads it. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	*data = malloc((size_t)length + 1);
	if (*data == NULL) {
		fclose(file);
		return ENOMEM;
	}
	*size = fread(*data, 1, (size_t)length, file);
	fclose(file);
	return 0;
}

/* The next number of 0..1 of a linear congruential generator. */
static double
next_unit(
	unsigned long *state)
{
	/* Steps the generator in 32 bits. */
	*state = (*state * 1103515245UL + 12345UL) & 0xffffffffUL;
	return (double)(*state >> 8) / 16777216.0;
}

/* Counts a check, printing it when it failed. */
static void
check(
	int ok,
	const char *what)
{
	/* Passed or failed. */
	if (ok) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}
