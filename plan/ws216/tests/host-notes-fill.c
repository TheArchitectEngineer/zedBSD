/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p005: the fill tool on the host -- fill.c and fill-page.c, linked
 * as they are with Notes' model (document.c and the rest), render.c and
 * geometry.c over the model of a Vulkan device (host-vulkan-mock.c) --
 * checked against the acceptance of plan/ws216/phase005/phase.md section 5:
 *
 *   F2  the areas of drawn rasters (the soft edge drawn as fill.frag
 *       draws it): a closed ring, rings with small gaps (closed) and a wide
 *       gap (leaks), a ring in a ring (a hole), letters, a light pencil
 *       through the grain, a watercolour multiplied, the page with nothing,
 *       a corner cut off
 *   F3  the same area whatever the bands' heights
 *   F4  the area of a 0.225 pt line's side does not pass the line
 *   F5  a page of dots is too complex, specks leave no holes, A3's scale
 *   F6  bands refused for memory are halved; refused at the fewest, ENOMEM
 *   F7  where a fill goes among the strokes, undo and redo, the journal
 *   F8  the eraser's rule for fills (D7 (c), review-2 M-11)
 *   F9  notes_fill_page() through the renderer's model: the band's
 *       picture, buffer and pass, a copy a band, the strokes of the band,
 *       nothing left after
 *   F10 the time of a 2048 x 2048 flood and of a whole A4 page
 *
 *   host-notes-fill LOG DIR   (the NOTES lines go to LOG, the journal under DIR)
 */

#include "app.h"
#include "host-vulkan-mock.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* The page sizes: A4, A3 landscape, a small page, a square of 2048 pixels at 4 a point. */
#define TEST_A4_WIDTH		595.276f
#define TEST_A4_HEIGHT		841.890f
#define TEST_SMALL_WIDTH	200.0f
#define TEST_SMALL_HEIGHT	150.0f
#define TEST_SQUARE		512.0f

/* The ways a test raster lays a colour: over, through the grain, multiplied. */
#define PAINT_OVER		0
#define PAINT_GRAIN		1
#define PAINT_MULTIPLY		2

/* A page drawn on the host at the fill's scale, which the fill reads in bands. */
struct raster {
	float page_width;
	float page_height;
	double scale;
	uint32_t width;
	uint32_t height;
	unsigned char *rgba;
	uint32_t refuse_above;
	unsigned fail_after;
	int above;
	unsigned calls;
};

/* The window the renderer opens on (only its size is read). */
static struct notes_window window;

/* The log the NOTES lines go to. */
static const char *log_path;

/* The grain's tile, made once. */
static unsigned char grain_tile[NOTES_GRAIN_SIDE * NOTES_GRAIN_SIDE];

/* The raster the renderer's model copies into its band buffer, and what the copies saw. */
static struct raster *band_raster;
static unsigned band_wrong;

/* ws216-p006: the background painter's calls, the band top it was last given, whether it fails, and whether the bands show it. */
static unsigned painted;
static uint32_t painted_top;
static uint32_t painted_height;
static int paint_fails;
static int band_background;
static const struct notes_page *band_page;

/* The checks run, and those that failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *format, ...);
static size_t log_count(const char *needle);
static void raster_open(struct raster *raster, float page_width, float page_height);
static void raster_close(struct raster *raster);
static int raster_band(void *context, uint32_t top, uint32_t height, unsigned char *pixels);
static void raster_paint(struct raster *raster, uint32_t x, uint32_t y, uint32_t color, double alpha, int paint, double px, double py);
static double coverage_of(double distance);
static void raster_ring(struct raster *raster, double cx, double cy, double radius, double width, double gap_angle, double gap, uint32_t color, double alpha, int paint);
static void raster_segment(struct raster *raster, double x0, double y0, double x1, double y1, double width, uint32_t color);
static void raster_dots(struct raster *raster, double spacing, double radius);
static int find(struct raster *raster, float x, float y, struct notes_fill_region *region);
static void box_of(const struct notes_fill_region *region, size_t contour, double *box);
static double area_of(const struct notes_fill_region *region, size_t contour);
static int same_region(const struct notes_fill_region *a, const struct notes_fill_region *b);
static struct notes_stroke *make_line(struct notes_document *document, unsigned tool, uint32_t color, float width, float x0, float y0, float x1, float y1, unsigned count);
static struct notes_stroke *make_fill(struct notes_document *document, float x, float y, float size);
static struct notes_stroke *make_fill_stroke(struct notes_document *document, float x, float y, float size);
static double now_ms(void);
static void test_areas(void);
static void test_bands(void);
static void test_thin(void);
static void test_limits(void);
static void test_time(void);
static void test_review(void);
static void test_words(void);
static void test_seen(void);
static void test_eraser(void);
static void test_page(const char *dir);
static void copy_band(unsigned char *bytes, uint32_t width, uint32_t height);
static int paint_band(void *context, uint32_t top, uint32_t width, uint32_t height, double scale);

int
main(
	int argc,
	char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: host-notes-fill LOG DIR\n");
		return 2;
	}
	log_path = argv[1];
	if (freopen(log_path, "w", stdout) == NULL) {
		fprintf(stderr, "host-notes-fill: cannot write %s\n", log_path);
		return 2;
	}
	window.width = 1280U;
	window.height = 800U;
	notes_grain_tile(grain_tile);

	test_areas();
	test_bands();
	test_thin();
	test_limits();
	test_time();
	test_review();
	test_words();
	test_seen();
	test_eraser();
	test_page(argv[2]);

	mock_reset();
	fprintf(stderr, "host-notes-fill: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;
	return 0;
}

/* Counts a check, and reports it when it fails. */
static void
check(
	int passed,
	const char *format,
	...)
{
	va_list arguments;

	checks++;
	if (passed)
		return;
	failures++;
	fprintf(stderr, "FAIL: ");
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fprintf(stderr, "\n");
}

/* Counts the log lines that hold a text. */
static size_t
log_count(
	const char *needle)
{
	char line[512];
	FILE *file;
	size_t count;

	fflush(stdout);
	file = fopen(log_path, "r");
	if (file == NULL)
		return 0;
	count = 0;
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strstr(line, needle) != NULL)
			count++;
	}
	fclose(file);
	return count;
}

/* Makes a white page at the fill's scale. */
static void
raster_open(
	struct raster *raster,
	float page_width,
	float page_height)
{
	memset(raster, 0, sizeof(*raster));
	raster->page_width = page_width;
	raster->page_height = page_height;
	raster->scale = notes_fill_scale(page_width, page_height);
	raster->width = (uint32_t)ceil((double)page_width * raster->scale);
	raster->height = (uint32_t)ceil((double)page_height * raster->scale);
	raster->rgba = malloc((size_t)raster->width * raster->height * 4U);
	if (raster->rgba == NULL) {
		fprintf(stderr, "host-notes-fill: out of memory\n");
		exit(2);
	}
	memset(raster->rgba, 0xff, (size_t)raster->width * raster->height * 4U);
}

static void
raster_close(
	struct raster *raster)
{
	free(raster->rgba);
	raster->rgba = NULL;
}

/* The band function over a raster: its rows, or ENOMEM for a band taller than the refusal. */
static int
raster_band(
	void *context,
	uint32_t top,
	uint32_t height,
	unsigned char *pixels)
{
	struct raster *raster;

	raster = context;
	raster->calls++;
	if (raster->refuse_above != 0U && height > raster->refuse_above)
		return ENOMEM;
	if (raster->fail_after != 0U && raster->calls > raster->fail_after)
		return EIO;
	if (top + height > raster->height) {
		check(0, "a band past the page: %u + %u of %u", top, height, raster->height);
		return EIO;
	}
	memcpy(pixels, raster->rgba + (size_t)top * raster->width * 4U, (size_t)height * raster->width * 4U);
	return 0;
}

/* The soft edge as fill.frag gives it: full inside (distance <= 0), fading over one pixel outside. */
static double
coverage_of(
	double distance)
{
	double t;

	if (distance <= 0.0)
		return 1.0;
	if (distance >= 1.0)
		return 0.0;
	t = distance;
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

/* Lays a colour (0xRRGGBB00) at an opacity on a pixel, over, through the grain at the page point, or multiplied. */
static void
raster_paint(
	struct raster *raster,
	uint32_t x,
	uint32_t y,
	uint32_t color,
	double alpha,
	int paint,
	double px,
	double py)
{
	unsigned char *pixel;
	double channel[3];
	double a;
	double h;
	unsigned index;
	long u;
	long v;

	pixel = raster->rgba + ((size_t)y * raster->width + x) * 4U;
	channel[0] = (double)((color >> 24) & 0xffU) / 255.0;
	channel[1] = (double)((color >> 16) & 0xffU) / 255.0;
	channel[2] = (double)((color >> 8) & 0xffU) / 255.0;
	a = alpha;
	if (paint == PAINT_GRAIN) {
		u = (long)floor(px * NOTES_GRAIN_SIDE / NOTES_GRAIN_CELL);
		v = (long)floor(py * NOTES_GRAIN_SIDE / NOTES_GRAIN_CELL);
		u = ((u % (long)NOTES_GRAIN_SIDE) + (long)NOTES_GRAIN_SIDE) % (long)NOTES_GRAIN_SIDE;
		v = ((v % (long)NOTES_GRAIN_SIDE) + (long)NOTES_GRAIN_SIDE) % (long)NOTES_GRAIN_SIDE;
		h = (double)grain_tile[v * NOTES_GRAIN_SIDE + u] / 255.0;
		a = 1.0 - (1.0 - alpha * NOTES_GRAIN_FLOOR) * (1.0 - alpha * h);
	}
	for (index = 0; index < 3U; index++) {
		double dst;

		dst = (double)pixel[index] / 255.0;
		if (paint == PAINT_MULTIPLY)
			dst = dst * (1.0 - a + a * channel[index]);
		else
			dst = dst * (1.0 - a) + channel[index] * a;
		pixel[index] = (unsigned char)floor(dst * 255.0 + 0.5);
	}
}

/*
 * Draws a ring of a radius and line width (points) round a centre, with a
 * gap of a length (points along the ring) starting at an angle (radians;
 * no gap when 0 long).
 */
static void
raster_ring(
	struct raster *raster,
	double cx,
	double cy,
	double radius,
	double width,
	double gap_angle,
	double gap,
	uint32_t color,
	double alpha,
	int paint)
{
	double px;
	double py;
	double distance;
	double angle;
	double along;
	double reach;
	uint32_t x;
	uint32_t y;
	uint32_t x0;
	uint32_t x1;
	uint32_t y0;
	uint32_t y1;

	reach = radius + width / 2.0 + 1.0;
	x0 = (uint32_t)fmax(0.0, floor((cx - reach) * raster->scale));
	y0 = (uint32_t)fmax(0.0, floor((cy - reach) * raster->scale));
	x1 = (uint32_t)fmin((double)raster->width - 1.0, ceil((cx + reach) * raster->scale));
	y1 = (uint32_t)fmin((double)raster->height - 1.0, ceil((cy + reach) * raster->scale));
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			px = ((double)x + 0.5) / raster->scale;
			py = ((double)y + 0.5) / raster->scale;
			distance = (fabs(hypot(px - cx, py - cy) - radius) - width / 2.0) * raster->scale;
			if (distance >= 1.0)
				continue;
			if (gap > 0.0) {
				angle = atan2(py - cy, px - cx) - gap_angle;
				while (angle < 0.0)
					angle += 2.0 * M_PI;
				along = angle * radius;
				if (along < gap)
					continue;
			}
			raster_paint(raster, x, y, color, alpha * coverage_of(distance), paint, px, py);
		}
	}
}

/* Draws a straight line of a width (points) from one point to another, its ends square. */
static void
raster_segment(
	struct raster *raster,
	double x0,
	double y0,
	double x1,
	double y1,
	double width,
	uint32_t color)
{
	double px;
	double py;
	double dx;
	double dy;
	double length;
	double along;
	double across;
	double distance;
	uint32_t x;
	uint32_t y;

	dx = x1 - x0;
	dy = y1 - y0;
	length = hypot(dx, dy);
	for (y = 0; y < raster->height; y++) {
		for (x = 0; x < raster->width; x++) {
			px = ((double)x + 0.5) / raster->scale - x0;
			py = ((double)y + 0.5) / raster->scale - y0;
			along = (px * dx + py * dy) / length;
			across = fabs(px * dy - py * dx) / length;
			if (along < 0.0 || along > length)
				continue;
			distance = (across - width / 2.0) * raster->scale;
			if (distance >= 1.0)
				continue;
			raster_paint(raster, x, y, color, coverage_of(distance), PAINT_OVER, 0.0, 0.0);
		}
	}
}

/* Draws black dots of a radius on a grid of a spacing over the whole page (a model of a photograph's specks). */
static void
raster_dots(
	struct raster *raster,
	double spacing,
	double radius)
{
	double cx;
	double cy;

	for (cy = spacing / 2.0; cy < (double)raster->page_height; cy += spacing) {
		for (cx = spacing / 2.0; cx < (double)raster->page_width; cx += spacing)
			raster_ring(raster, cx, cy, radius / 2.0, radius, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	}
}

/* Finds the area from a point of a raster. */
static int
find(
	struct raster *raster,
	float x,
	float y,
	struct notes_fill_region *region)
{
	return notes_fill_find(raster->page_width, raster->page_height, x, y, raster->above == 0, raster_band, raster, region);
}

/* The box of a contour (left, top, right, bottom), in points. */
static void
box_of(
	const struct notes_fill_region *region,
	size_t contour,
	double *box)
{
	size_t start;
	size_t index;

	start = 0;
	for (index = 0; index < contour; index++)
		start += region->contours[index];
	box[0] = box[2] = region->points[start].x;
	box[1] = box[3] = region->points[start].y;
	for (index = start; index < start + region->contours[contour]; index++) {
		box[0] = fmin(box[0], region->points[index].x);
		box[1] = fmin(box[1], region->points[index].y);
		box[2] = fmax(box[2], region->points[index].x);
		box[3] = fmax(box[3], region->points[index].y);
	}
}

/* The signed area of a contour in square points (positive: the outside, clockwise on the screen). */
static double
area_of(
	const struct notes_fill_region *region,
	size_t contour)
{
	size_t start;
	size_t index;
	size_t count;
	size_t next;
	double sum;

	start = 0;
	for (index = 0; index < contour; index++)
		start += region->contours[index];
	count = region->contours[contour];
	sum = 0.0;
	for (index = 0; index < count; index++) {
		next = (index + 1U) % count;
		sum += region->points[start + index].x * region->points[start + next].y - region->points[start + next].x * region->points[start + index].y;
	}
	return sum / 2.0;
}

/* Tells whether two areas have the same contours and pixels. */
static int
same_region(
	const struct notes_fill_region *a,
	const struct notes_fill_region *b)
{
	if (a->point_count != b->point_count || a->contour_count != b->contour_count || a->pixels != b->pixels)
		return 0;
	if (memcmp(a->contours, b->contours, a->contour_count * sizeof(a->contours[0])) != 0)
		return 0;
	if (memcmp(a->points, b->points, a->point_count * sizeof(a->points[0])) != 0)
		return 0;
	return 1;
}

/* F2: the areas of drawn pages. */
static void
test_areas(void)
{
	struct raster raster;
	struct notes_fill_region region;
	double box[4];
	double cx;
	double cy;
	int error;
	unsigned index;

	cx = 200.0;
	cy = 300.0;

	/*
	 * A closed black ring of 2 points: the inside, reaching the line's edge
	 * (within a pixel, 0.25 point) and not past the line.
	 */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, cx, cy, 60.0, 2.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	error = find(&raster, (float)cx, (float)cy, &region);
	check(error == 0 && region.contour_count == 1U, "ring: one contour (%d, %lu)", error, (unsigned long)region.contour_count);
	if (error == 0) {
		box_of(&region, 0, box);
		check(fabs(box[0] - (cx - 59.0)) <= 0.25 && fabs(box[2] - (cx + 59.0)) <= 0.25 && fabs(box[1] - (cy - 59.0)) <= 0.25 &&
		      fabs(box[3] - (cy + 59.0)) <= 0.25,
		      "ring: the area reaches the line's edge (%.3f %.3f %.3f %.3f)", box[0], box[1], box[2], box[3]);
		check(area_of(&region, 0) > 0.0, "ring: the outside turns clockwise on the screen");
		fprintf(stderr, "ring: %lu corners, %u bands, %lu pixels\n", (unsigned long)region.point_count, region.bands, (unsigned long)region.pixels);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* Rings with gaps of 1 and 2.5 points close; one of 4.5 points leaks into the page. */
	for (index = 0; index < 3U; index++) {
		static const double gaps[3] = { 1.0, 2.5, 4.5 };

		raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
		raster_ring(&raster, cx, cy, 60.0, 2.0, 0.7, gaps[index], 0x00000000U, 1.0, PAINT_OVER);
		error = find(&raster, (float)cx, (float)cy, &region);
		check(error == 0, "gap %.1f: an area (%d)", gaps[index], error);
		if (error == 0) {
			box_of(&region, 0, box);
			if (index < 2U)
				check(box[2] < cx + 61.0 && box[3] < cy + 61.0 && box[0] > cx - 61.0, "gap %.1f: the gap is closed", gaps[index]);
			else
				check(box[2] > 500.0 && box[3] > 800.0, "gap %.1f: the area leaks through the gap", gaps[index]);
		}
		notes_fill_region_free(&region);
		raster_close(&raster);
	}

	/* A ring in a ring: the area between them has the outside and a hole turning the other way. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, cx, cy, 100.0, 2.0, 0.0, 0.0, 0x1d4ed800U, 1.0, PAINT_OVER);
	raster_ring(&raster, cx, cy, 40.0, 2.0, 0.0, 0.0, 0x1d4ed800U, 1.0, PAINT_OVER);
	error = find(&raster, (float)cx, (float)(cy - 70.0), &region);
	check(error == 0 && region.contour_count == 2U, "annulus: two contours (%lu)", (unsigned long)region.contour_count);
	if (error == 0 && region.contour_count == 2U) {
		check(area_of(&region, 0) > 0.0 && area_of(&region, 1) < 0.0, "annulus: the hole turns the other way (%.1f, %.1f)", area_of(&region, 0),
		      area_of(&region, 1));
		check(fabs(area_of(&region, 0) + area_of(&region, 1) - M_PI * (99.0 * 99.0 - 41.0 * 41.0)) < 0.03 * M_PI * (99.0 * 99.0 - 41.0 * 41.0),
		      "annulus: the area is the ring's (%.1f)", area_of(&region, 0) + area_of(&region, 1));
	}
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* Letters: a row of small o's and bars; the inside of an o is filled alone. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	for (index = 0; index < 8U; index++) {
		raster_ring(&raster, 100.0 + 12.0 * index, 500.0, 3.5, 0.9, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
		raster_segment(&raster, 106.0 + 12.0 * index, 494.0, 106.0 + 12.0 * index, 506.0, 0.9, 0x00000000U);
	}
	error = find(&raster, 136.0f, 500.0f, &region);
	check(error == 0 && region.contour_count == 1U, "letters: the inside of an o (%d)", error);
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] > 132.0 && box[2] < 140.0 && box[1] > 496.0 && box[3] < 504.0, "letters: the o's inside only (%.2f %.2f %.2f %.2f)", box[0], box[1],
		      box[2], box[3]);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* A light pencil through the grain and a watercolour multiplied: each closes its ring. */
	for (index = 0; index < 2U; index++) {
		raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
		if (index == 0U)
			raster_ring(&raster, cx, cy, 60.0, 1.4, 0.0, 0.0, 0x11111100U, 0.30, PAINT_GRAIN);
		else
			raster_ring(&raster, cx, cy, 60.0, 16.0, 0.0, 0.0, 0x2563eb00U, 0.22, PAINT_MULTIPLY);
		error = find(&raster, (float)cx, (float)cy, &region);
		check(error == 0, "%s: an area (%d)", index == 0U ? "pencil" : "watercolour", error);
		if (error == 0) {
			box_of(&region, 0, box);
			check(box[2] < cx + 69.0 && box[3] < cy + 69.0, "%s: the ring holds the area (%.2f %.2f)", index == 0U ? "pencil" : "watercolour", box[2],
			      box[3]);
		}
		notes_fill_region_free(&region);
		raster_close(&raster);
	}

	/* A page with nothing: the whole page, one contour of four corners. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	error = find(&raster, 300.0f, 400.0f, &region);
	check(error == 0 && region.contour_count == 1U && region.point_count == 4U, "blank: the page, four corners (%lu)", (unsigned long)region.point_count);
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] == 0.0 && box[1] == 0.0 && fabs(box[2] - TEST_A4_WIDTH) < 0.02 && fabs(box[3] - TEST_A4_HEIGHT) < 0.02, "blank: the page's corners");
	}
	notes_fill_region_free(&region);

	/* A line across the page's corner: the corner, bounded by the page's edges. */
	raster_segment(&raster, 0.0, 120.0, 120.0, 0.0, 2.0, 0x00000000U);
	error = find(&raster, 20.0f, 20.0f, &region);
	check(error == 0 && region.contour_count == 1U, "corner: an area");
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] == 0.0 && box[1] == 0.0 && box[2] < 120.0 && box[3] < 120.0 && fabs(area_of(&region, 0) - 0.5 * 118.6 * 118.6) < 150.0,
		      "corner: the triangle (%.1f)", area_of(&region, 0));
	}
	notes_fill_region_free(&region);

	/* A point off the page. */
	check(find(&raster, -1.0f, 20.0f, &region) == EINVAL && find(&raster, 20.0f, TEST_A4_HEIGHT, &region) == EINVAL, "off the page: EINVAL");
	raster_close(&raster);

	/* A point near the line moves off the closing, and a place narrower than the closing floods without it. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, cx, cy, 60.0, 2.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	raster_segment(&raster, 400.0, 100.0, 400.0, 700.0, 0.8, 0x00000000U);
	raster_segment(&raster, 401.6, 100.0, 401.6, 700.0, 0.8, 0x00000000U);
	raster_segment(&raster, 399.6, 100.0, 402.0, 100.0, 0.8, 0x00000000U);
	raster_segment(&raster, 399.6, 700.0, 402.0, 700.0, 0.8, 0x00000000U);
	error = find(&raster, (float)(cx + 58.5), (float)cy, &region);
	check(error == 0, "near the line: an area");
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[2] < cx + 61.0 && box[0] < cx - 58.7, "near the line: the ring's inside (%.2f %.2f)", box[0], box[2]);
	}
	notes_fill_region_free(&region);
	error = find(&raster, 400.8f, 400.0f, &region);
	check(error == 0, "narrow: an area (%d)", error);
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] > 399.9 && box[2] < 401.7 && box[1] < 101.0 && box[1] > 99.0 && box[3] > 699.0 && box[3] < 701.0, "narrow: the strip between the lines (%.3f %.3f %.3f %.3f)", box[0],
		      box[1], box[2], box[3]);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);
}

/* F3, F6: the same area whatever the bands, and the bands halved when refused. */
static void
test_bands(void)
{
	struct raster raster;
	struct notes_fill_region whole;
	struct notes_fill_region banded;
	unsigned round;
	int error;

	/* A small page in one band, and in bands of 75 rows: rings with gaps across the bands' edges. */
	raster_open(&raster, TEST_SMALL_WIDTH, TEST_SMALL_HEIGHT);
	raster_ring(&raster, 100.0, 75.0, 50.0, 1.5, 0.3, 1.0, 0x00000000U, 1.0, PAINT_OVER);
	raster_ring(&raster, 100.0, 75.0, 18.75, 1.0, 1.4, 1.0, 0x00000000U, 1.0, PAINT_OVER);
	error = find(&raster, 100.0f, 40.0f, &whole);
	check(error == 0 && whole.bands == 1U, "bands: the small page in one band (%u)", whole.bands);
	raster.refuse_above = 100U;
	error = find(&raster, 100.0f, 40.0f, &banded);
	check(error == 0 && banded.band_height == 75U && banded.bands > 1U, "bands: the small page in bands of 75 rows (%u, %u)", banded.band_height,
	      banded.bands);
	check(error == 0 && same_region(&whole, &banded), "bands: the same area in one band and in bands");
	notes_fill_region_free(&whole);
	notes_fill_region_free(&banded);
	raster_close(&raster);

	/* A4 in bands of 440 and of 110 rows, the seed in several places: the same area. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, 300.0, 330.0, 250.0, 2.0, 0.2, 1.0, 0x00000000U, 1.0, PAINT_OVER);
	raster_ring(&raster, 300.0, 330.0, 82.5, 1.0, 2.0, 1.0, 0x00000000U, 1.0, PAINT_OVER);
	for (round = 0; round < 3U; round++) {
		static const float seeds[3][2] = { { 300.0f, 160.0f }, { 300.0f, 500.0f }, { 110.0f, 330.0f } };

		raster.refuse_above = 0U;
		error = find(&raster, seeds[round][0], seeds[round][1], &whole);
		check(error == 0 && whole.band_height == 440U, "bands: A4 in bands of 440 rows (%u)", whole.band_height);
		raster.refuse_above = 110U;
		error = find(&raster, seeds[round][0], seeds[round][1], &banded);
		check(error == 0 && banded.band_height == 110U, "bands: A4 halved to bands of 110 rows (%u)", banded.band_height);
		check(error == 0 && same_region(&whole, &banded), "bands: seed %u: the same area in bands of 440 and of 110 rows", round);
		notes_fill_region_free(&whole);
		notes_fill_region_free(&banded);
	}

	/* Bands refused down to fewer than 64 rows: ENOMEM. */
	raster.refuse_above = 60U;
	error = find(&raster, 300.0f, 160.0f, &whole);
	check(error == ENOMEM, "bands: refused at every height, ENOMEM (%d)", error);
	raster_close(&raster);
}

/* F4: the area on each side of a 0.225 point line reaches under it and not past it. */
static void
test_thin(void)
{
	struct raster raster;
	struct notes_fill_region region;
	double box[4];
	int error;

	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_segment(&raster, 300.0, -1.0, 300.0, 900.0, 0.225, 0x00000000U);
	error = find(&raster, 100.0f, 400.0f, &region);
	check(error == 0, "thin: the left side");
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[2] < 300.1125 && box[2] > 299.6, "thin: the left side stops under the line (%.4f)", box[2]);
	}
	notes_fill_region_free(&region);
	error = find(&raster, 500.0f, 400.0f, &region);
	check(error == 0, "thin: the right side");
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] > 299.8875 && box[0] < 300.4, "thin: the right side stops under the line (%.4f)", box[0]);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);
}

/* F5: too complex, specks dropped, the scale of a wide page. */
static void
test_limits(void)
{
	struct raster raster;
	struct notes_fill_region region;
	int error;

	/* Dots of 0.75 points every 6 points: thousands of holes, too complex. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_dots(&raster, 6.0, 0.75);
	error = find(&raster, 0.5f, 0.5f, &region);
	check(error == E2BIG && region.point_count > 8192U, "dots: too complex (%d, %lu corners)", error, (unsigned long)region.point_count);
	raster_close(&raster);

	/* Specks of 0.25 points: their holes are smaller than a square point and dropped. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_dots(&raster, 20.0, 0.25);
	error = find(&raster, 0.5f, 0.5f, &region);
	check(error == 0 && region.contour_count == 1U, "specks: no holes (%d, %lu)", error, (unsigned long)region.contour_count);
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* A3 landscape is drawn at most 4096 pixels wide. */
	check(fabs(notes_fill_scale(1190.551f, 841.890f) - 4096.0 / 1190.551) < 1e-6, "A3: the scale %.5f", notes_fill_scale(1190.551f, 841.890f));
	check(notes_fill_scale(TEST_A4_WIDTH, TEST_A4_HEIGHT) == 4.0, "A4: 4 pixels a point");
}

/* F10: the time of a 2048 pixels square and of a whole A4 page (an empty page: the flood and the contours only). */
static void
test_time(void)
{
	struct raster raster;
	struct notes_fill_region region;
	double started;
	double square;
	double page;
	int error;

	raster_open(&raster, TEST_SQUARE, TEST_SQUARE);
	raster_ring(&raster, 256.0, 256.0, 200.0, 2.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	started = now_ms();
	error = find(&raster, 256.0f, 256.0f, &region);
	square = now_ms() - started;
	check(error == 0 && raster.width == 2048U, "time: the 2048 square");
	notes_fill_region_free(&region);
	raster_close(&raster);

	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	started = now_ms();
	error = find(&raster, 300.0f, 400.0f, &region);
	page = now_ms() - started;
	check(error == 0, "time: the A4 page");
	notes_fill_region_free(&region);
	raster_close(&raster);
	fprintf(stderr, "time: 2048x2048 disc %.1f ms, A4 page %.1f ms (host)\n", square, page);
}

/* The cases of the design's review (review-1 M-2, M-3, M-7, L-3, L-4, L-9). */
static void
test_review(void)
{
	struct raster raster;
	struct notes_fill_region region;
	double box[4];
	unsigned dy;
	int error;

	/* M-2: a fill put above (z above 0) stops short of a thin line's edge: it does not cover it. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_segment(&raster, 300.0, -1.0, 300.0, 900.0, 0.225, 0x00000000U);
	raster.above = 1;
	error = find(&raster, 100.0f, 400.0f, &region);
	check(error == 0, "above: the left side");
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[2] <= 299.8875 && box[2] > 299.4, "above: the area stops short of the line's edge (%.4f)", box[2]);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* M-3: inside a ring too small for any area: ENOENT. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, 200.0, 200.0, 0.9, 0.5, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	error = find(&raster, 200.0f, 200.0f, &region);
	check(error == ENOENT || (error == 0 && region.contour_count >= 1U && area_of(&region, 0) > 0.0), "tiny ring: no area, or an outside (%d)", error);
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* p005 code review M2: a dot of 0.75 point tapped at its middle is too small to fill (ENOENT, nothing allocated past the page). */
	raster_open(&raster, 100.0f, 100.0f);
	for (dy = 0; dy < 3U; dy++)
		memset(raster.rgba + ((size_t)(198U + dy) * raster.width + 198U) * 4U, 0, 3U * 4U);
	error = find(&raster, 49.875f, 49.875f, &region);
	check(error == ENOENT, "dot: too small (%d)", error);
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* L-3: a channel 2 points wide between thick lines floods along the channel, not the page. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_segment(&raster, 100.0, 98.5, 100.0, 301.5, 3.0, 0x00000000U);
	raster_segment(&raster, 105.0, 98.5, 105.0, 301.5, 3.0, 0x00000000U);
	raster_segment(&raster, 98.5, 300.0, 106.5, 300.0, 3.0, 0x00000000U);
	raster_segment(&raster, 98.5, 100.0, 106.5, 100.0, 3.0, 0x00000000U);
	error = find(&raster, 102.5f, 200.0f, &region);
	check(error == 0, "channel: an area (%d)", error);
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] > 100.0 && box[2] < 105.0 && box[1] > 100.0 && box[3] < 300.0 && box[3] - box[1] > 190.0, "channel: the channel only (%.2f %.2f %.2f %.2f)", box[0], box[1],
		      box[2], box[3]);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* L-4: a point on a line's soft edge fills the paper's side or the line, not a grey sliver. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, 200.0, 300.0, 60.0, 2.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	error = find(&raster, (float)(200.0 + 61.0 + 0.125), 300.0f, &region);
	check(error == 0 && region.pixels > 1000U, "soft edge: a real area (%d, %lu pixels)", error, (unsigned long)region.pixels);
	notes_fill_region_free(&region);

	/* On the line itself: the line's colour, the line filled (narrower than the closing). */
	error = find(&raster, 260.0f, 300.0f, &region);
	check(error == 0, "on the line: an area (%d)", error);
	if (error == 0) {
		box_of(&region, 0, box);
		check(box[0] > 138.0 && box[2] < 262.0 && box[3] - box[1] > 110.0, "on the line: the ring (%.2f %.2f %.2f %.2f)", box[0], box[1], box[2], box[3]);
	}
	notes_fill_region_free(&region);

	/* L-9: a device that fails after the first band: the error, and nothing leaks (ASan). */
	raster.fail_after = 1U;
	error = find(&raster, 300.0f, 700.0f, &region);
	check(error == EIO, "a device failing midway: EIO (%d)", error);
	raster.fail_after = 0U;
	raster_close(&raster);

	/* L-9: the lightest orange pencil ring closes. */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, 200.0, 300.0, 60.0, 0.8, 0.0, 0.0, 0xea580c00U, 0.30, PAINT_GRAIN);
	error = find(&raster, 200.0f, 300.0f, &region);
	check(error == 0, "orange pencil: an area");
	if (error == 0) {
		box_of(&region, 0, box);
		fprintf(stderr, "orange pencil: box %.1f %.1f %.1f %.1f, %lu contours\n", box[0], box[1], box[2], box[3], (unsigned long)region.contour_count);
	}
	notes_fill_region_free(&region);
	raster_close(&raster);

	/* L-9: A0 (scale below 4) and M-7: a tall narrow page (bands of at most 4096 rows). */
	raster_open(&raster, 2383.94f, 3370.39f);
	check(raster.scale < 4.0 && raster.width <= 4096U, "A0: scale %.3f, %u pixels wide", raster.scale, raster.width);
	raster_ring(&raster, 1000.0, 1000.0, 300.0, 3.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	error = find(&raster, 1000.0f, 1000.0f, &region);
	check(error == 0, "A0: the ring's inside (%d)", error);
	notes_fill_region_free(&region);
	raster_close(&raster);
	raster_open(&raster, 50.0f, 2000.0f);
	error = find(&raster, 25.0f, 1000.0f, &region);
	check(error == 0 && region.band_height <= 4096U && region.contour_count == 1U, "tall page: bands of %u rows", region.band_height);
	notes_fill_region_free(&region);
	raster_close(&raster);
}

/*
 * M-6: a page of about 200 handwritten words (each a run of small loops and
 * strokes of a 0.8 point pen), filled from its margin: the corners and the
 * time, for the decision on FILL_POINTS_MAX.
 */
static void
test_words(void)
{
	struct raster raster;
	struct notes_fill_region region;
	double started;
	double spent;
	double x;
	double y;
	unsigned word;
	unsigned letter;
	unsigned letters;
	unsigned seed;
	int error;

	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	seed = 7U;
	word = 0;
	for (y = 80.0; y < 780.0 && word < 200U; y += 28.0) {
		for (x = 60.0; x < 520.0 && word < 200U; word++) {
			letters = 3U + seed % 5U;
			for (letter = 0; letter < letters; letter++) {
				seed = seed * 1103515245U + 12345U;
				if (seed % 3U == 0U) {
					raster_segment(&raster, x + 2.0, y - 9.0, x + 3.0, y + 4.0, 0.8, 0x10101000U);
				} else {
					raster_ring(&raster, x + 3.0, y, 2.6 + (double)(seed % 7U) / 10.0, 0.8, (double)(seed % 6U), 0.4, 0x10101000U, 1.0, PAINT_OVER);
				}
				x += 6.5;
			}
			x += 7.0;
		}
	}
	started = now_ms();
	error = find(&raster, 20.0f, 20.0f, &region);
	spent = now_ms() - started;
	fprintf(stderr, "words: %u words, error %d, %lu corners, %lu contours, %.1f ms (host)\n", word, error, (unsigned long)region.point_count,
		(unsigned long)region.contour_count, spent);
	printf("NOTES TEST words=%u error=%d corners=%lu contours=%lu ms=%.1f\n", word, error, (unsigned long)region.point_count,
	       (unsigned long)region.contour_count, spent);
	check(error == 0 || error == E2BIG, "words: an area or too complex (%d)", error);
	notes_fill_region_free(&region);
	raster_close(&raster);
}

/* Makes a straight stroke of a tool with a rising pressure, on the first page. */
static struct notes_stroke *
make_line(
	struct notes_document *document,
	unsigned tool,
	uint32_t color,
	float width,
	float x0,
	float y0,
	float x1,
	float y1,
	unsigned count)
{
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned index;
	double share;

	stroke = notes_stroke_create(document->next_id, tool, color, width, document->time_base);
	document->next_id++;
	for (index = 0; index < count; index++) {
		share = (double)index / (double)(count - 1U);
		memset(&point, 0, sizeof(point));
		point.x = (float)(x0 + (x1 - x0) * share);
		point.y = (float)(y0 + (y1 - y0) * share);
		point.pressure = (uint16_t)(65535.0 * (0.6 + 0.3 * sin(share * 3.14159)));
		point.time_ms = 8U * index;
		(void)notes_stroke_append(stroke, &point);
	}
	check(notes_document_add_stroke(document, 0, stroke) == 0 && notes_stroke_outline(stroke) == 0, "a stroke of tool %u", tool);
	return stroke;
}

/* Makes a fill of a square on the first page (on top). */
static struct notes_stroke *
make_fill(
	struct notes_document *document,
	float x,
	float y,
	float size)
{
	struct notes_stroke *stroke;

	stroke = make_fill_stroke(document, x, y, size);
	check(notes_document_add_stroke(document, 0, stroke) == 0, "a fill on the page");
	return stroke;
}

/* Makes a fill of a square, off the page. */
static struct notes_stroke *
make_fill_stroke(
	struct notes_document *document,
	float x,
	float y,
	float size)
{
	static const float corners[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
	struct notes_stroke *stroke;
	struct notes_point point;
	unsigned index;

	stroke = notes_stroke_create(document->next_id, NOTES_TOOL_FILL, 0xe85d04ffU, NOTES_FILL_WIDTH, 0U);
	document->next_id++;
	memset(&point, 0, sizeof(point));
	for (index = 0; index < 4U; index++) {
		point.x = x + size * corners[index][0];
		point.y = y + size * corners[index][1];
		(void)notes_stroke_append(stroke, &point);
	}
	stroke->contours = malloc(sizeof(stroke->contours[0]));
	stroke->contours[0] = 4U;
	stroke->contour_count = 1U;
	return stroke;
}

/* F7: what is seen at a point, and where a fill goes. */
static void
test_seen(void)
{
	struct notes_document document;
	struct notes_stroke *water;
	struct notes_stroke *pen;
	struct notes_stroke *fill;
	size_t index;
	size_t page;
	float y;
	float faint;
	int found;
	int strong;
	int any;

	check(notes_document_init(&document, 1700000000000ULL) == 0, "seen: the document");
	pen = make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 3.0f, 50.0f, 100.0f, 400.0f, 100.0f, 30U);
	water = make_line(&document, NOTES_TOOL_WATERCOLOR, 0x2563ebffU, 28.0f, 50.0f, 300.0f, 400.0f, 300.0f, 40U);
	(void)make_fill(&document, 100.0f, 400.0f, 100.0f);

	/* The paper shows nothing; the pen shows the pen; the fill shows the fill. */
	found = notes_document_seen_at(&document, 0, 300.0f, 600.0f, &index);
	check(!found, "seen: the paper");
	found = notes_document_seen_at(&document, 0, 200.0f, 100.0f, &index);
	check(found && document.pages[0]->strokes[index] == pen, "seen: the pen");
	found = notes_document_seen_at(&document, 0, 150.0f, 450.0f, &index);
	check(found && index == 2U, "seen: the fill");

	/* The watercolour's faint outer edge is not seen (its change of the paper within the threshold); its core is. */
	faint = -1.0f;
	for (y = 300.0f; y < 330.0f; y += 0.05f) {
		strong = notes_stroke_contains(water, 200.0f, y, 1);
		any = notes_stroke_contains(water, 200.0f, y, 0);
		if (any && !strong) {
			faint = y;
			break;
		}
	}
	check(faint > 0.0f, "seen: the watercolour has a faint edge");
	found = notes_document_seen_at(&document, 0, 200.0f, faint, &index);
	check(!found, "seen: the faint edge at %.2f is not seen", faint);
	found = notes_document_seen_at(&document, 0, 200.0f, 300.0f, &index);
	check(found && document.pages[0]->strokes[index] == water, "seen: the watercolour's core");

	/* A stroke added at a place, taken back and done again at the same place; a place past the top is refused. */
	fill = make_fill_stroke(&document, 0.0f, 0.0f, 10.0f);
	check(notes_document_add_stroke_at(&document, 0, 9U, fill) == EINVAL, "seen: a place past the top");
	check(notes_document_add_stroke_at(&document, 0, 1U, fill) == 0 && document.pages[0]->strokes[1] == fill, "seen: a stroke at place 1");
	check(notes_document_undo(&document, &page) == 0 && document.pages[0]->strokes[0] == pen && document.pages[0]->strokes[1] == water,
	      "seen: undo takes it off");
	check(notes_document_redo(&document, &page) == 0 && document.pages[0]->strokes[1] == fill, "seen: redo puts it back at place 1");
	notes_document_free(&document);
}

/* F8: the eraser and the fills, by the user's choice for D7 (c): what is seen comes first. */
static void
test_eraser(void)
{
	struct notes_document document;
	struct notes_stroke *fill;
	struct notes_stroke *line;
	struct notes_stroke *top;
	struct notes_stroke *ring;
	size_t removed;
	size_t page;
	unsigned kind;

	for (kind = 0; kind < 2U; kind++) {
		/* A fill under a pen line across it. */
		check(notes_document_init(&document, 1700000000000ULL) == 0, "eraser: the document");
		fill = make_fill(&document, 100.0f, 100.0f, 200.0f);
		line = make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 3.0f, 80.0f, 200.0f, 320.0f, 200.0f, 40U);

		/* Started on the line: the line goes, and the drag on over the fill leaves the fill. */
		notes_document_erase_begin(&document);
		if (kind == 0U) {
			check(notes_document_erase_at(&document, 0, 150.0f, 200.5f, 10.0f, &removed) == 0 && removed == 1U, "eraser: on the line, the line");
			check(notes_document_erase_at(&document, 0, 150.0f, 150.0f, 10.0f, &removed) == 0 && removed == 0U, "eraser: a line's drag leaves the fill");
		} else {
			check(notes_document_erase_parts_at(&document, 0, 150.0f, 200.5f, 10.0f, &removed) == 0 && removed == 1U, "eraser of parts: the line cut");
			check(notes_document_erase_parts_at(&document, 0, 150.0f, 150.0f, 10.0f, &removed) == 0 && removed == 0U,
			      "eraser of parts: a line's drag leaves the fill");
		}
		notes_document_erase_end(&document);
		check(document.pages[0]->stroke_count >= 1U && document.pages[0]->strokes[0] == fill, "eraser: the fill stays");
		check(notes_document_undo(&document, &page) == 0, "eraser: undo");
		check(document.pages[0]->stroke_count == 2U && document.pages[0]->strokes[1] == line, "eraser: the line is back");

		/* Started on the fill away from the line: the fill goes whole, and then the line where the centre is on it. */
		notes_document_erase_begin(&document);
		if (kind == 0U) {
			check(notes_document_erase_at(&document, 0, 150.0f, 130.0f, 10.0f, &removed) == 0 && removed == 1U, "eraser: the fill");
			check(document.pages[0]->stroke_count == 1U && document.pages[0]->strokes[0] == line, "eraser: the fill went whole");
			check(notes_document_erase_at(&document, 0, 150.0f, 200.5f, 10.0f, &removed) == 0 && removed == 1U, "eraser: then the line under the centre");
		} else {
			check(notes_document_erase_parts_at(&document, 0, 150.0f, 130.0f, 10.0f, &removed) == 0 && removed == 1U, "eraser of parts: the fill");
			check(document.pages[0]->stroke_count == 1U && document.pages[0]->strokes[0] == line, "eraser of parts: the fill went whole");
		}
		notes_document_erase_end(&document);
		check(notes_document_undo(&document, &page) == 0 && document.pages[0]->stroke_count == 2U && document.pages[0]->strokes[0] == fill,
		      "eraser: undo gives the fill back at its place");

		/* The centre 3 points off the line (within its half width and 2 points): the line only. */
		notes_document_erase_begin(&document);
		check(notes_document_erase_at(&document, 0, 150.0f, 203.0f, 10.0f, &removed) == 0 && removed == 1U, "eraser: next to the line, one removed");
		notes_document_erase_end(&document);
		check(document.pages[0]->stroke_count == 1U && document.pages[0]->strokes[0] == fill, "eraser: next to the line, the line went");
		check(notes_document_undo(&document, &page) == 0 && document.pages[0]->stroke_count == 2U, "eraser: undo");

		/* The centre 6 points off the line, the circle on it: the fill under the centre goes, the line stays (accepted). */
		notes_document_erase_begin(&document);
		check(notes_document_erase_at(&document, 0, 150.0f, 206.0f, 10.0f, &removed) == 0 && removed == 1U, "eraser: 6 points off, one removed");
		notes_document_erase_end(&document);
		check(document.pages[0]->stroke_count == 1U && document.pages[0]->strokes[0] == line, "eraser: 6 points off, the fill went");
		notes_document_free(&document);
	}

	/* A fill 20 points across inside a pen ring: the eraser at its centre removes it. */
	check(notes_document_init(&document, 1700000000000ULL) == 0, "eraser: the document");
	fill = make_fill(&document, 393.0f, 593.0f, 14.0f);
	ring = make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 2.0f, 390.0f, 590.0f, 410.0f, 590.0f, 10U);
	(void)make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 2.0f, 410.0f, 590.0f, 410.0f, 610.0f, 10U);
	(void)make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 2.0f, 410.0f, 610.0f, 390.0f, 610.0f, 10U);
	(void)make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 2.0f, 390.0f, 610.0f, 390.0f, 590.0f, 10U);
	check(notes_document_erase_at(&document, 0, 400.0f, 600.0f, 10.0f, &removed) == 0 && removed == 1U && document.pages[0]->strokes[0] == ring,
	      "eraser: a fill 20 points across goes, its frame stays");

	/* A fill poured over a line (above it): the eraser on the line removes the fill, the hidden line stays. */
	line = make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 3.0f, 80.0f, 200.0f, 320.0f, 200.0f, 40U);
	top = make_fill(&document, 100.0f, 150.0f, 100.0f);
	notes_document_erase_begin(&document);
	check(notes_document_erase_at(&document, 0, 150.0f, 200.0f, 10.0f, &removed) == 0 && removed == 1U, "eraser: on a covered line, one removed");
	notes_document_erase_end(&document);
	check(document.pages[0]->strokes[document.pages[0]->stroke_count - 1U] == line, "eraser: the fill over the line went, the line stays");

	/* Fills on fills: only the top one under the centre goes, and the drag inside it leaves the one below. */
	fill = make_fill(&document, 100.0f, 300.0f, 200.0f);
	top = make_fill(&document, 200.0f, 400.0f, 200.0f);
	notes_document_erase_begin(&document);
	check(notes_document_erase_at(&document, 0, 250.0f, 450.0f, 10.0f, &removed) == 0 && removed == 1U, "eraser: overlapping fills, one removed");
	check(notes_document_erase_at(&document, 0, 260.0f, 460.0f, 10.0f, &removed) == 0 && removed == 0U, "eraser: the drag inside it leaves the fill below");
	notes_document_erase_end(&document);
	check(document.pages[0]->strokes[document.pages[0]->stroke_count - 1U] == fill, "eraser: the top fill went, the one below stays");
	(void)top;
	notes_document_free(&document);
}

/* The renderer's model's copy: the raster's rows of the band, its top read from the band's first draw (the white page). */
static void
copy_band(
	unsigned char *bytes,
	uint32_t width,
	uint32_t height)
{
	const struct mock_object *buffer;
	const struct mock_object *memory;
	const float *vertex;
	const struct notes_stroke *stroke;
	size_t expected;
	size_t index;
	double top_points;
	double bottom_points;
	long top;

	if (mock.draw_count == 0U || band_raster == NULL) {
		band_wrong++;
		return;
	}
	buffer = mock_get((const void *)(uintptr_t)((mock.draws[0].buffer + 1U) * 16U));
	memory = mock_get((const void *)(uintptr_t)((buffer->memory + 1U) * 16U));
	vertex = (const float *)(memory->bytes + (size_t)mock.draws[0].first * NOTES_VERTEX_FLOATS * sizeof(float));
	top = -(long)vertex[1];
	if (width != band_raster->width || top < 0 || (uint32_t)top + height > band_raster->height) {
		band_wrong++;
		return;
	}
	memcpy(bytes, band_raster->rgba + (size_t)top * width * 4U, (size_t)width * height * 4U);

	/* The band's draws: the page, and three for each shape of a stroke that reaches the rows. */
	expected = 1U;
	if (band_background) {
		expected++;
		if (painted_top != (uint32_t)top || painted_height != height)
			band_wrong++;
	}
	top_points = (double)top / band_raster->scale - 1.0;
	bottom_points = (double)(top + (long)height) / band_raster->scale + 1.0;
	for (index = 0; index < band_page->stroke_count; index++) {
		stroke = band_page->strokes[index];
		if (stroke->bounds[3] < top_points || stroke->bounds[1] > bottom_points)
			continue;
		expected += 3U * stroke->shape_count;
	}
	if (mock.draw_count != expected || mock.passes != 1U)
		band_wrong++;
}

/* U4: draws a band's background into the renderer's background picture (white), or fails when told to. */
static int
paint_band(
	void *context,
	uint32_t top,
	uint32_t width,
	uint32_t height,
	double scale)
{
	unsigned char *target;
	size_t pitch;
	uint32_t row;
	VkResult error;

	(void)scale;
	if (paint_fails)
		return EIO;
	error = notes_renderer_background(context, width, height, &target, &pitch);
	if (error != VK_SUCCESS || target == NULL)
		return EIO;
	for (row = 0; row < height; row++)
		memset(target + (size_t)row * pitch, 0xff, (size_t)width * 4U);
	painted++;
	painted_top = top;
	painted_height = height;
	return 0;
}

/* F7, F9: notes_fill_page() through the renderer's model, undo and redo, and the journal. */
static void
test_page(
	const char *dir)
{
	struct notes_renderer renderer;
	struct notes_document document;
	struct notes_document recovered;
	struct raster raster;
	struct notes_stroke *fill;
	char path[4096];
	char document_path[4096];
	char journal_path[4096];
	size_t records;
	size_t place;
	size_t page;
	size_t index;
	size_t images;
	int same;
	int error;

	/* A device of i915's allocations, and a page with a pen ring, a line under it and a stroke far below. */
	mock_reset();
	mock.buffer_limit = 16U * 1024U * 1024U;
	mock.image_limit = 16U * 1024U * 1024U;
	mock.copy_hook = copy_band;
	error = notes_renderer_open(&renderer, &window);
	check(error == VK_SUCCESS, "page: the renderer opens");
	images = mock_live(MOCK_IMAGE);

	/* The journal under the run's folder (absolute: else Notes uses the user's own). */
	(void)snprintf(path, sizeof(path), "%s/data", dir);
	(void)mkdir(path, 0700);
	check(path[0] == '/', "page: the data folder is absolute");
	if (path[0] != '/')
		return;
	setenv("XDG_DATA_HOME", path, 1);
	(void)snprintf(document_path, sizeof(document_path), "%s/fill.pdf", dir);
	check(notes_document_init(&document, 1700000000000ULL) == 0, "page: the document");
	document.journal = notes_journal_create(document_path);
	check(document.journal != NULL && notes_journal_path(document_path, journal_path, sizeof(journal_path)) == 0, "page: its journal");
	(void)make_line(&document, NOTES_TOOL_PEN, 0x111111ffU, 2.0f, 50.0f, 700.0f, 500.0f, 700.0f, 20U);
	(void)make_line(&document, NOTES_TOOL_BRUSH, 0x7c3aedffU, 8.0f, 100.0f, 250.0f, 300.0f, 250.0f, 40U);
	(void)make_line(&document, NOTES_TOOL_PENCIL, 0x111111ffU, 2.4f, 100.0f, 350.0f, 300.0f, 350.0f, 60U);

	/* The page as the model shows it: a ring round (200, 300). */
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, 200.0, 300.0, 120.0, 2.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	band_raster = &raster;
	band_page = document.pages[0];
	band_wrong = 0;

	/* Filled inside the ring, on the paper between the strokes: at the bottom. */
	error = notes_fill_page(&renderer, &document, 0, 200.0f, 300.0f, 0x2a9d8f00U, NULL, NULL, &place);
	check(error == 0 && place == 0U, "page: filled at the bottom (%d, %lu)", error, (unsigned long)place);
	fill = document.pages[0]->strokes[0];
	check(fill->tool == NOTES_TOOL_FILL && fill->color == 0x2a9d8fffU && fill->contour_count == 1U, "page: the fill's stroke, opaque");
	check(mock.copies >= 1U && band_wrong == 0U, "page: each band copied once, its draws the strokes of its rows (%u copies, %u wrong)", mock.copies,
	      band_wrong);
	check(mock.largest <= 4U * 1024U * 1024U, "page: no allocation over 4 MiB (%lu)", (unsigned long)mock.largest);
	check(mock.bad_final_layouts == 0U, "page: every pass ends in a layout the Raspberry Pi 4 takes (S-1)");
	if (renderer.groups[NOTES_GROUP_GRAIN] == NOTES_GROUP_MADE)
		images += NOTES_GRAIN_LEVELS;
	check(renderer.band == VK_NULL_HANDLE && renderer.band_buffer == VK_NULL_HANDLE && mock_live(MOCK_IMAGE) == images,
	      "page: the band's objects are gone after the fill");
	check(log_count("NOTES FILL made place=0") == 1U, "page: the fill is logged");

	/* Filled on the brush: just above it. */
	error = notes_fill_page(&renderer, &document, 0, 200.0f, 250.0f, 0xe9c46a00U, NULL, NULL, &place);
	check(error == 0 && place == 3U && document.pages[0]->strokes[3]->tool == NOTES_TOOL_FILL, "page: filled above the brush (%lu)", (unsigned long)place);
	check(band_wrong == 0U, "page: the second fill's bands");

	/* Undo takes the second fill off its place, redo puts it back there. */
	fill = document.pages[0]->strokes[3];
	check(notes_document_undo(&document, &page) == 0 && document.pages[0]->stroke_count == 4U, "page: undo");
	check(notes_document_redo(&document, &page) == 0 && document.pages[0]->strokes[3] == fill, "page: redo at its place");

	/* The journal gives the same strokes in the same order. */
	error = notes_journal_recover(journal_path, &recovered, path, sizeof(path), &records);
	check(error == 0, "page: the journal recovered (%d)", error);
	if (error == 0) {
		same = recovered.pages[0]->stroke_count == document.pages[0]->stroke_count;
		for (index = 0; same && index < document.pages[0]->stroke_count; index++) {
			if (recovered.pages[0]->strokes[index]->id != document.pages[0]->strokes[index]->id ||
			    recovered.pages[0]->strokes[index]->tool != document.pages[0]->strokes[index]->tool ||
			    recovered.pages[0]->strokes[index]->point_count != document.pages[0]->strokes[index]->point_count)
				same = 0;
		}
		check(same, "page: the journal's strokes are the page's, in order");
		notes_document_free(&recovered);
	}

	/* A second tap on the same fill: told, not filled again (p006 review-1 C-5). */
	index = document.pages[0]->stroke_count;
	error = notes_fill_page(&renderer, &document, 0, 200.0f, 250.0f, 0xe9c46a00U, NULL, NULL, &place);
	check(error == EEXIST && document.pages[0]->stroke_count == index, "page: the same fill again is EEXIST (%d)", error);
	check(strcmp(notes_fill_notice(EEXIST), "Already filled") == 0, "page: its notice");

	/* A point off the page, and a page of dots too complex: nothing changes. */
	check(notes_fill_page(&renderer, &document, 0, 700.0f, 300.0f, 0U, NULL, NULL, &place) == EINVAL, "page: off the page");
	raster_close(&raster);
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_dots(&raster, 6.0, 0.75);
	band_raster = &raster;
	check(notes_fill_page(&renderer, &document, 0, 0.5f, 0.5f, 0U, NULL, NULL, &place) == E2BIG && document.pages[0]->stroke_count == 5U, "page: too complex");
	check(log_count("NOTES FILL refused reason=complex") == 1U, "page: the refusal is logged");
	check(strcmp(notes_fill_notice(E2BIG), "The area is too complex to fill") == 0 && notes_fill_notice(0) == NULL, "page: the notices");

	/* A device that refuses every band. */
	mock.image_limit = 64U * 1024U;
	check(notes_fill_page(&renderer, &document, 0, 200.0f, 300.0f, 0U, NULL, NULL, &place) == ENOMEM, "page: a device that refuses the bands");
	check(renderer.band == VK_NULL_HANDLE, "page: nothing of the band is left");

	/* U4 (ws216-p006): a background drawn for each band, at the band's top and size, shown in the band; its failure fails the fill. */
	mock.image_limit = 16U * 1024U * 1024U;
	raster_close(&raster);
	raster_open(&raster, TEST_A4_WIDTH, TEST_A4_HEIGHT);
	raster_ring(&raster, 300.0, 600.0, 50.0, 2.0, 0.0, 0.0, 0x00000000U, 1.0, PAINT_OVER);
	band_raster = &raster;
	band_background = 1;
	band_wrong = 0;
	painted = 0;
	error = notes_fill_page(&renderer, &document, 0, 300.0f, 600.0f, 0x2a9d8f00U, paint_band, &renderer, &place);
	check(error == 0 && painted >= 1U && band_wrong == 0U, "background: each band painted at its top and size and shown (%d, %u painted, %u wrong)", error,
	      painted, band_wrong);
	paint_fails = 1;
	index = document.pages[0]->stroke_count;
	error = notes_fill_page(&renderer, &document, 0, 300.0f, 600.0f, 0x1d4ed800U, paint_band, &renderer, &place);
	check(error == EIO && document.pages[0]->stroke_count == index, "background: a painter that fails, EIO and no change (%d)", error);
	check(log_count("NOTES FILL background failed") == 1U, "background: the failure is logged");
	paint_fails = 0;
	band_background = 0;
	raster_close(&raster);
	band_raster = NULL;
	notes_journal_destroy(document.journal);
	document.journal = NULL;
	notes_document_free(&document);
	notes_renderer_close(&renderer);
	check(mock_live(MOCK_IMAGE) == 0U && mock_live(MOCK_BUFFER) == 0U && mock_live(MOCK_MEMORY) == 0U, "page: the renderer leaves nothing");
}

/* The monotonic clock in milliseconds. */
static double
now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}
