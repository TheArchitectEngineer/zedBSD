/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The outline of a pressure-sensitive pen stroke.
 *
 * A stroke whose width follows the pen's pressure cannot be drawn with a PDF
 * line width, which is constant along a path.  Notes therefore fills an
 * outline: the left edge offset from each sampled point by half the width
 * there, a round cap around the last point, the right edge back, and a round
 * cap around the first point.  Notes draws the same polygon on the screen, so
 * the page and the saved PDF have the same shape (plan/ws079/design-pdf.md
 * section 1, plan/ws079/design-input-notes.md section 5).
 */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

/* The largest coordinate and width the outline accepts, the writer's own limit. */
#define PDF_OUTLINE_COORDINATE_LIMIT 1000000.0

/* The farthest a cap's chord may stray from the true circle, in points. */
#define PDF_OUTLINE_TOLERANCE 0.05

/* The fewest and the most chords of one half-circle cap. */
#define PDF_OUTLINE_CAP_MIN 2
#define PDF_OUTLINE_CAP_MAX 64

/* Points closer than this to the previous kept point add nothing to the outline. */
#define PDF_OUTLINE_MIN_STEP 0.001

/* The share of the full width that the lightest touch still draws. */
#define PDF_OUTLINE_WIDTH_FLOOR 0.15

/* The exponent of the pressure curve, which widens light strokes faster than a straight line. */
#define PDF_OUTLINE_PRESSURE_EXPONENT 0.7

/* Pi, which C89's math.h does not name. */
#define PDF_OUTLINE_PI 3.14159265358979323846

/*
 * One kept sample of the stroke with its derived geometry.
 *
 * It lives only while one outline is being built.
 */
struct pdf_outline_sample {
	double x;
	double y;
	double radius;
	double normal_x;
	double normal_y;
};

static int check_value(double value);
static double pressure_radius(double width, double pressure);
static size_t cap_chords(double radius, double angle);
static void compute_normals(struct pdf_outline_sample *samples, size_t count);
static void append_arc(struct pdf_point *outline, size_t *used, const struct pdf_outline_sample *center, double start_angle, double sweep, size_t chords, int whole_circle);

/*
 * Builds the pressure-width outline polygon of a pen stroke.
 *
 * The width at a point is width * (0.15 + 0.85 * pressure^0.7), so the
 * lightest touch still draws 15% of the full width.  Both ends are round.
 * A stroke that does not move is a round dot.  The caller frees the polygon
 * with pdf_outline_free().
 */
int
pdf_outline_stroke(
	const struct pdf_stroke_point *points,
	size_t count,
	double width,
	struct pdf_point **outline,
	size_t *outline_count)
{
	struct pdf_outline_sample *samples;
	struct pdf_outline_sample *last;
	struct pdf_point *polygon;
	size_t kept;
	size_t index;
	size_t end_chords;
	size_t start_chords;
	size_t capacity;
	size_t used;
	double pressure;
	double radius;
	double distance;
	double step_x;
	double step_y;
	double angle;
	int error;

	/* Refuses a stroke without points. */
	if (points == NULL)
		return EINVAL;
	if (count == 0)
		return EINVAL;

	/* Refuses a width without size or past the limit; the negation also refuses a NaN. */
	if (!(width > 0.0 && width <= PDF_OUTLINE_COORDINATE_LIMIT))
		return EINVAL;

	/* Refuses a count whose samples or polygon would overflow the allocation's size. */
	if (count > ((size_t)-1 / sizeof(*samples)) / 2 - 2 * PDF_OUTLINE_CAP_MAX)
		return EINVAL;

	/* Allocates the kept samples. */
	samples = malloc(count * sizeof(*samples));
	if (samples == NULL)
		return ENOMEM;

	/* Keeps each point that moved away from the previous kept one, with its radius. */
	kept = 0;
	for (index = 0; index < count; index++) {
		/* Refuses an x that cannot be drawn. */
		error = check_value(points[index].x);
		if (error != 0) {
			free(samples);
			return error;
		}

		/* Refuses a y that cannot be drawn. */
		error = check_value(points[index].y);
		if (error != 0) {
			free(samples);
			return error;
		}

		/* Refuses a pressure that is not a number, and clamps the rest to 0..1. */
		pressure = points[index].pressure;
		if (pressure != pressure) {
			free(samples);
			return EINVAL;
		}
		if (pressure < 0.0)
			pressure = 0.0;
		if (pressure > 1.0)
			pressure = 1.0;

		/* Finds the half width the pressure draws. */
		radius = pressure_radius(width, pressure);

		/* Measures how far the point moved from the previous kept one. */
		distance = PDF_OUTLINE_MIN_STEP;
		if (kept != 0) {
			step_x = points[index].x - samples[kept - 1].x;
			step_y = points[index].y - samples[kept - 1].y;
			distance = sqrt(step_x * step_x + step_y * step_y);
		}

		/* Merges a point that has not moved into the previous one, keeping the wider of the two. */
		if (distance < PDF_OUTLINE_MIN_STEP) {
			if (radius > samples[kept - 1].radius)
				samples[kept - 1].radius = radius;
			continue;
		}

		/* Keeps the point. */
		samples[kept].x = points[index].x;
		samples[kept].y = points[index].y;
		samples[kept].radius = radius;
		kept++;
	}

	/* Finds each kept point's normal, which the edges are offset along. */
	compute_normals(samples, kept);

	/* Counts the chords of the two caps; a dot is one whole circle. */
	last = &samples[kept - 1];
	if (kept == 1) {
		end_chords = cap_chords(last->radius, 2.0 * PDF_OUTLINE_PI);
		start_chords = 0;
		capacity = end_chords;
	} else {
		end_chords = cap_chords(last->radius, PDF_OUTLINE_PI);
		start_chords = cap_chords(samples[0].radius, PDF_OUTLINE_PI);
		capacity = 2 * kept + end_chords + start_chords;
	}

	/* Allocates the polygon. */
	polygon = malloc(capacity * sizeof(*polygon));
	if (polygon == NULL) {
		free(samples);
		return ENOMEM;
	}

	/* Draws a dot as a whole circle around the only point. */
	used = 0;
	if (kept == 1) {
		append_arc(polygon, &used, last, 0.0, 2.0 * PDF_OUTLINE_PI, end_chords, 1);
		free(samples);
		*outline = polygon;
		*outline_count = used;
		return 0;
	}

	/* Walks the left edge forward. */
	for (index = 0; index < kept; index++) {
		polygon[used].x = samples[index].x + samples[index].normal_x * samples[index].radius;
		polygon[used].y = samples[index].y + samples[index].normal_y * samples[index].radius;
		used++;
	}

	/* Turns around the last point, from the left edge through the stroke's direction to the right edge. */
	angle = atan2(last->normal_y, last->normal_x);
	append_arc(polygon, &used, last, angle, -PDF_OUTLINE_PI, end_chords, 0);

	/* Walks the right edge back. */
	for (index = kept; index > 0; index--) {
		polygon[used].x = samples[index - 1].x - samples[index - 1].normal_x * samples[index - 1].radius;
		polygon[used].y = samples[index - 1].y - samples[index - 1].normal_y * samples[index - 1].radius;
		used++;
	}

	/* Turns around the first point, from the right edge against the stroke's direction back to the left edge. */
	angle = atan2(-samples[0].normal_y, -samples[0].normal_x);
	append_arc(polygon, &used, &samples[0], angle, -PDF_OUTLINE_PI, start_chords, 0);
	free(samples);

	/* Succeeded: the caller owns the closed polygon. */
	*outline = polygon;
	*outline_count = used;
	return 0;
}

/*
 * Frees an outline polygon that pdf_outline_stroke() made.
 */
void
pdf_outline_free(
	struct pdf_point *outline)
{
	/* Releases the polygon's allocation. */
	free(outline);
}

/* Reports whether a coordinate is finite and inside the limit. */
static int
check_value(
	double value)
{
	/* Refuses a NaN and anything past the limit on either side. */
	if (!(value >= -PDF_OUTLINE_COORDINATE_LIMIT && value <= PDF_OUTLINE_COORDINATE_LIMIT))
		return EINVAL;

	/* Succeeded: the coordinate can be drawn. */
	return 0;
}

/* Computes the half width the pressure curve gives a point. */
static double
pressure_radius(
	double width,
	double pressure)
{
	double share;

	/* Maps the pressure onto the share of the full width that it draws. */
	share = PDF_OUTLINE_WIDTH_FLOOR + (1.0 - PDF_OUTLINE_WIDTH_FLOOR) * pow(pressure, PDF_OUTLINE_PRESSURE_EXPONENT);

	/* Reports half of that width, the distance of each edge from the point. */
	return width * share / 2.0;
}

/* Counts the chords an arc needs to stay within the tolerance of its circle. */
static size_t
cap_chords(
	double radius,
	double angle)
{
	double step;
	double chords;

	/* A circle smaller than the tolerance needs only the fewest chords per half turn. */
	step = PDF_OUTLINE_PI / 2.0;
	if (radius > PDF_OUTLINE_TOLERANCE)
		step = 2.0 * acos(1.0 - PDF_OUTLINE_TOLERANCE / radius);

	/* Divides the arc into chords no wider than the step, within the half turn's limits. */
	chords = ceil(angle / step);
	if (chords < PDF_OUTLINE_CAP_MIN * angle / PDF_OUTLINE_PI)
		chords = PDF_OUTLINE_CAP_MIN * angle / PDF_OUTLINE_PI;
	if (chords > PDF_OUTLINE_CAP_MAX * angle / PDF_OUTLINE_PI)
		chords = PDF_OUTLINE_CAP_MAX * angle / PDF_OUTLINE_PI;

	/* Reports the whole number of chords. */
	return (size_t)chords;
}

/*
 * Computes the unit normal of each kept point.
 *
 * An inner point's direction is the sum of its two neighboring segments'
 * directions, which bisects the corner; an end point uses its one segment.
 * The normal is the direction turned a quarter to the left.
 */
static void
compute_normals(
	struct pdf_outline_sample *samples,
	size_t count)
{
	double direction_x;
	double direction_y;
	double before_x;
	double before_y;
	double after_x;
	double after_y;
	double length;
	size_t index;

	/* A single point has no direction; its dot needs none. */
	if (count < 2) {
		samples[0].normal_x = 0.0;
		samples[0].normal_y = 1.0;
		return;
	}

	/* Finds each point's direction from its neighboring segments. */
	for (index = 0; index < count; index++) {
		/* The segment that arrives at the point, or none at the first point. */
		before_x = 0.0;
		before_y = 0.0;
		if (index > 0) {
			before_x = samples[index].x - samples[index - 1].x;
			before_y = samples[index].y - samples[index - 1].y;
			length = sqrt(before_x * before_x + before_y * before_y);
			before_x /= length;
			before_y /= length;
		}

		/* The segment that leaves the point, or none at the last point. */
		after_x = 0.0;
		after_y = 0.0;
		if (index + 1 < count) {
			after_x = samples[index + 1].x - samples[index].x;
			after_y = samples[index + 1].y - samples[index].y;
			length = sqrt(after_x * after_x + after_y * after_y);
			after_x /= length;
			after_y /= length;
		}

		/* Bisects the corner; a stroke that doubles back uses the leaving segment alone. */
		direction_x = before_x + after_x;
		direction_y = before_y + after_y;
		length = sqrt(direction_x * direction_x + direction_y * direction_y);
		if (length < 1e-9) {
			direction_x = after_x;
			direction_y = after_y;
			length = 1.0;
		}

		/* Turns the unit direction a quarter to the left. */
		samples[index].normal_x = -direction_y / length;
		samples[index].normal_y = direction_x / length;
	}
}

/*
 * Appends the corners of an arc around a point.
 *
 * The arc starts at start_angle and turns by sweep radians in chords steps.
 * A cap's two ends are the edges' corners, which the caller writes itself,
 * so only the corners between them are appended; a whole circle appends
 * every corner once.
 */
static void
append_arc(
	struct pdf_point *outline,
	size_t *used,
	const struct pdf_outline_sample *center,
	double start_angle,
	double sweep,
	size_t chords,
	int whole_circle)
{
	size_t step;
	size_t first;
	size_t last;
	double angle;

	/* Chooses the corners to write: every one of a whole circle, or the inner ones of a cap. */
	first = 1;
	last = chords;
	if (whole_circle)
		first = 0;

	/* Writes each corner on the circle. */
	for (step = first; step < last; step++) {
		angle = start_angle + sweep * (double)step / (double)chords;
		outline[*used].x = center->x + center->radius * cos(angle);
		outline[*used].y = center->y + center->radius * sin(angle);
		(*used)++;
	}
}
