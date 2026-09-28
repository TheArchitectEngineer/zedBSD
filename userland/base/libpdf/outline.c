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
 * outline, and it fills the same polygon on the screen, so the page and the
 * saved PDF have the same shape (plan/ws079/design-pdf.md section 1).
 *
 * The samples are first joined by a centripetal Catmull-Rom spline, which
 * passes through every sample without the loops and cusps of the uniform
 * one, and the spline is cut into pieces short enough to stay within a
 * tolerance of the curve.  The pressure is interpolated along each piece.
 * The outline then walks the left edge forward, turns around the last point
 * with a round cap, walks the right edge back and turns around the first
 * point.  Each edge is offset from the centerline by half the width there.
 *
 * At a corner the outer edge follows an arc around the corner point (a
 * round join), and the inner edge passes through the corner point itself.
 * Filled with the nonzero rule, the polygon is then exactly the union of the
 * pieces' trapezoids, the joins' circular sectors and the two caps, all
 * turning the same way, so a stroke never narrows at a corner and never
 * shows a notch however sharply it turns.
 */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

/* The largest coordinate and width the outline accepts, the writer's own limit. */
#define PDF_OUTLINE_COORDINATE_LIMIT 1000000.0

/* The farthest a spline piece or an arc's chord may stray from the true curve, in points. */
#define PDF_OUTLINE_TOLERANCE 0.05

/* The fewest chords of a half-circle cap, and the most chords of any half turn. */
#define PDF_OUTLINE_CAP_MIN 2
#define PDF_OUTLINE_CAP_MAX 64

/* The most pieces one spline interval between two samples is cut into. */
#define PDF_OUTLINE_PIECES_MAX 256

/* The most corners one outline may have, which bounds its memory to 256 MiB. */
#define PDF_OUTLINE_CORNERS_MAX 16777216

/* Points closer than this to the previous kept point add nothing to the outline. */
#define PDF_OUTLINE_MIN_STEP 0.001

/* The share of the full width that the lightest touch still draws. */
#define PDF_OUTLINE_WIDTH_FLOOR 0.15

/* The exponent of the pressure curve, which widens light strokes faster than a straight line. */
#define PDF_OUTLINE_PRESSURE_EXPONENT 0.7

/* Pi, which C89's math.h does not name. */
#define PDF_OUTLINE_PI 3.14159265358979323846

/*
 * One kept sample of the stroke.
 *
 * The pressure is clamped to 0..1.  It lives only while one outline is
 * being built.
 */
struct pdf_outline_sample {
	double x;
	double y;
	double pressure;
};

/*
 * One point of the smoothed centerline with the half width drawn there.
 *
 * It lives only while one outline is being built.
 */
struct pdf_outline_vertex {
	double x;
	double y;
	double radius;
};

/*
 * The smoothed centerline being built.
 *
 * error holds the first failure of an append; later appends then do
 * nothing, so the builder is checked once, when the centerline is done.
 */
struct pdf_outline_path {
	struct pdf_outline_vertex *vertices;
	size_t count;
	size_t capacity;
	int error;
};

/*
 * The outline polygon being built, which becomes the caller's.
 *
 * error works as in the centerline builder.
 */
struct pdf_outline_polygon {
	struct pdf_point *corners;
	size_t count;
	size_t capacity;
	int error;
};

/*
 * The shape of the centerline at one inner point.
 *
 * before and after are the unit left normals of the segments that arrive at
 * and leave the point, and turn is the signed angle between them.  A gentle
 * turn is one whose round join would be a single chord; its two edges then
 * meet in one corner on middle, the unit normal halfway between the two.
 */
struct pdf_outline_join {
	double before_x;
	double before_y;
	double after_x;
	double after_y;
	double middle_x;
	double middle_y;
	double turn;
	int gentle;
};

/*
 * The cubic Bezier form of one spline interval, and the four pressures
 * around it that the pressure is interpolated from.
 */
struct pdf_outline_piece {
	double start_x;
	double start_y;
	double control1_x;
	double control1_y;
	double control2_x;
	double control2_y;
	double end_x;
	double end_y;
	double pressure_before;
	double pressure_start;
	double pressure_end;
	double pressure_after;
};

static int keep_samples(const struct pdf_stroke_point *points, size_t count, struct pdf_outline_sample *samples, size_t *kept);
static int check_value(double value);
static double pressure_radius(double width, double pressure);
static void build_centerline(const struct pdf_outline_sample *samples, size_t count, double width, struct pdf_outline_path *path);
static void spline_interval(const struct pdf_outline_sample *samples, size_t count, size_t index, struct pdf_outline_piece *piece);
static double knot_distance(double from_x, double from_y, double to_x, double to_y);
static size_t interval_pieces(const struct pdf_outline_piece *piece);
static double interpolate_pressure(const struct pdf_outline_piece *piece, double t);
static void path_append(struct pdf_outline_path *path, double x, double y, double radius);
static void build_outline(const struct pdf_outline_path *path, struct pdf_outline_polygon *polygon);
static void measure_join(const struct pdf_outline_vertex *vertices, size_t index, struct pdf_outline_join *join);
static double turn_angle(const struct pdf_outline_vertex *before, const struct pdf_outline_vertex *corner, const struct pdf_outline_vertex *after);
static void segment_normal(const struct pdf_outline_vertex *from, const struct pdf_outline_vertex *to, double *normal_x, double *normal_y);
static void build_dot(const struct pdf_outline_vertex *center, struct pdf_outline_polygon *polygon);
static double arc_step(double radius);
static size_t arc_chords(double radius, double angle, size_t fewest);
static void append_arc(struct pdf_outline_polygon *polygon, const struct pdf_outline_vertex *center, double start_angle, double sweep, size_t chords);
static void polygon_append(struct pdf_outline_polygon *polygon, double x, double y);

/*
 * Builds the pressure-width outline polygon of a pen stroke.
 *
 * The stroke passes smoothly through every sample.  The width at a point is
 * width * (0.15 + 0.85 * pressure^0.7), so the lightest touch still draws 15%
 * of the full width, and the pressure between samples is interpolated.  Both
 * ends and every corner are round.  A stroke that does not move is a round
 * dot.  The polygon is to be filled with the nonzero rule.  The caller frees
 * it with pdf_outline_free().  The same points always give the same polygon.
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
	struct pdf_outline_path path;
	struct pdf_outline_polygon polygon;
	size_t kept;
	int error;

	/* Refuses a stroke without points. */
	if (points == NULL)
		return EINVAL;
	if (count == 0)
		return EINVAL;

	/* Refuses a width without size or past the limit; the negation also refuses a NaN. */
	if (!(width > 0.0 && width <= PDF_OUTLINE_COORDINATE_LIMIT))
		return EINVAL;

	/* Refuses a stroke longer than one outline may be. */
	if (count > PDF_OUTLINE_CORNERS_MAX)
		return EINVAL;

	/* Allocates the kept samples. */
	samples = malloc(count * sizeof(*samples));
	if (samples == NULL)
		return ENOMEM;

	/* Keeps each point that moved away from the previous kept one. */
	error = keep_samples(points, count, samples, &kept);
	if (error != 0) {
		free(samples);
		return error;
	}

	/* Joins the samples with the spline, cut into short pieces with their half widths. */
	memset(&path, 0, sizeof(path));
	build_centerline(samples, kept, width, &path);
	free(samples);
	if (path.error != 0) {
		free(path.vertices);
		return path.error;
	}

	/* Walks around the centerline; a stroke that never moved is a dot. */
	memset(&polygon, 0, sizeof(polygon));
	if (path.count == 1) {
		build_dot(&path.vertices[0], &polygon);
	} else {
		build_outline(&path, &polygon);
	}

	/* The centerline is not needed any more; a failed outline frees its corners. */
	free(path.vertices);
	if (polygon.error != 0) {
		free(polygon.corners);
		return polygon.error;
	}

	/* Succeeded: the caller owns the closed polygon. */
	*outline = polygon.corners;
	*outline_count = polygon.count;
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

/*
 * Checks the points and keeps each one that moved away from the previous
 * kept one.
 *
 * A point that has not moved is merged into the previous one, which keeps
 * the harder of the two pressures.
 */
static int
keep_samples(
	const struct pdf_stroke_point *points,
	size_t count,
	struct pdf_outline_sample *samples,
	size_t *kept)
{
	size_t index;
	size_t used;
	double pressure;
	double step_x;
	double step_y;
	int error;

	/* Walks the points in the order the pen drew them. */
	used = 0;
	for (index = 0; index < count; index++) {
		/* Refuses an x that cannot be drawn. */
		error = check_value(points[index].x);
		if (error != 0)
			return error;

		/* Refuses a y that cannot be drawn. */
		error = check_value(points[index].y);
		if (error != 0)
			return error;

		/* Refuses a pressure that is not a number, and clamps the rest to 0..1. */
		pressure = points[index].pressure;
		if (pressure != pressure)
			return EINVAL;
		if (pressure < 0.0)
			pressure = 0.0;
		if (pressure > 1.0)
			pressure = 1.0;

		/* Merges a point that has not moved away from the previous kept one into it. */
		if (used != 0) {
			step_x = points[index].x - samples[used - 1].x;
			step_y = points[index].y - samples[used - 1].y;
			if (step_x * step_x + step_y * step_y < PDF_OUTLINE_MIN_STEP * PDF_OUTLINE_MIN_STEP) {
				if (pressure > samples[used - 1].pressure)
					samples[used - 1].pressure = pressure;
				continue;
			}
		}

		/* Keeps the point. */
		samples[used].x = points[index].x;
		samples[used].y = points[index].y;
		samples[used].pressure = pressure;
		used++;
	}

	/* Succeeded: kept counts the distinct samples. */
	*kept = used;
	return 0;
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

/*
 * Builds the smoothed centerline through the samples.
 *
 * Each interval between two samples is cut into the pieces its curve needs;
 * the first sample and the end of every interval become vertices, so the
 * centerline passes through every sample.
 */
static void
build_centerline(
	const struct pdf_outline_sample *samples,
	size_t count,
	double width,
	struct pdf_outline_path *path)
{
	struct pdf_outline_piece piece;
	size_t index;
	size_t pieces;
	size_t step;
	double t;
	double u;
	double x;
	double y;
	double pressure;
	double radius;

	/* Starts at the first sample. */
	radius = pressure_radius(width, samples[0].pressure);
	path_append(path, samples[0].x, samples[0].y, radius);

	/* Cuts each interval's curve into pieces and appends the end of each piece. */
	for (index = 0; index + 1 < count; index++) {
		/* Finds the interval's curve and how finely it must be cut. */
		spline_interval(samples, count, index, &piece);
		pieces = interval_pieces(&piece);

		/* Appends the end of each piece. */
		for (step = 1; step <= pieces; step++) {
			/* Evaluates the Bezier form at the end of the piece. */
			t = (double)step / (double)pieces;
			u = 1.0 - t;
			x = u * u * u * piece.start_x + 3.0 * u * u * t * piece.control1_x + 3.0 * u * t * t * piece.control2_x + t * t * t * piece.end_x;
			y = u * u * u * piece.start_y + 3.0 * u * u * t * piece.control1_y + 3.0 * u * t * t * piece.control2_y + t * t * t * piece.end_y;

			/* The interval's last piece ends exactly on the next sample. */
			if (step == pieces) {
				x = piece.end_x;
				y = piece.end_y;
			}

			/* Appends the point with the half width of its interpolated pressure. */
			pressure = interpolate_pressure(&piece, t);
			radius = pressure_radius(width, pressure);
			path_append(path, x, y, radius);
		}
	}
}

/*
 * Finds the cubic Bezier form of the centripetal Catmull-Rom interval that
 * joins sample index to the next one.
 *
 * The spline needs a neighbor on each side of the interval.  At the ends of
 * the stroke the missing neighbor is the far sample mirrored through the
 * end, so the curve leaves the end straight toward its neighbor.
 */
static void
spline_interval(
	const struct pdf_outline_sample *samples,
	size_t count,
	size_t index,
	struct pdf_outline_piece *piece)
{
	double before_x;
	double before_y;
	double after_x;
	double after_y;
	double knot_before;
	double knot_middle;
	double knot_after;
	double tangent_x;
	double tangent_y;

	/* Takes the sample before the interval, or mirrors the next one through the first. */
	if (index > 0) {
		before_x = samples[index - 1].x;
		before_y = samples[index - 1].y;
		piece->pressure_before = samples[index - 1].pressure;
	} else {
		before_x = 2.0 * samples[0].x - samples[1].x;
		before_y = 2.0 * samples[0].y - samples[1].y;
		piece->pressure_before = samples[0].pressure;
	}

	/* Takes the sample after the interval, or mirrors the previous one through the last. */
	if (index + 2 < count) {
		after_x = samples[index + 2].x;
		after_y = samples[index + 2].y;
		piece->pressure_after = samples[index + 2].pressure;
	} else {
		after_x = 2.0 * samples[index + 1].x - samples[index].x;
		after_y = 2.0 * samples[index + 1].y - samples[index].y;
		piece->pressure_after = samples[index + 1].pressure;
	}

	/* Records the interval's ends. */
	piece->start_x = samples[index].x;
	piece->start_y = samples[index].y;
	piece->end_x = samples[index + 1].x;
	piece->end_y = samples[index + 1].y;
	piece->pressure_start = samples[index].pressure;
	piece->pressure_end = samples[index + 1].pressure;

	/*
	 * Spaces the knots by the square roots of the distances, which is what
	 * makes the spline centripetal.  Neighboring samples are at least the
	 * merge distance apart, so no knot interval is empty.
	 */
	knot_before = knot_distance(before_x, before_y, piece->start_x, piece->start_y);
	knot_middle = knot_distance(piece->start_x, piece->start_y, piece->end_x, piece->end_y);
	knot_after = knot_distance(piece->end_x, piece->end_y, after_x, after_y);

	/* Finds the tangent at the start, scaled to the interval, and the first control point a third of it along. */
	tangent_x = (piece->start_x - before_x) / knot_before - (piece->end_x - before_x) / (knot_before + knot_middle) + (piece->end_x - piece->start_x) / knot_middle;
	tangent_y = (piece->start_y - before_y) / knot_before - (piece->end_y - before_y) / (knot_before + knot_middle) + (piece->end_y - piece->start_y) / knot_middle;
	piece->control1_x = piece->start_x + tangent_x * knot_middle / 3.0;
	piece->control1_y = piece->start_y + tangent_y * knot_middle / 3.0;

	/* Finds the tangent at the end and the second control point a third of it back. */
	tangent_x = (piece->end_x - piece->start_x) / knot_middle - (after_x - piece->start_x) / (knot_middle + knot_after) + (after_x - piece->end_x) / knot_after;
	tangent_y = (piece->end_y - piece->start_y) / knot_middle - (after_y - piece->start_y) / (knot_middle + knot_after) + (after_y - piece->end_y) / knot_after;
	piece->control2_x = piece->end_x - tangent_x * knot_middle / 3.0;
	piece->control2_y = piece->end_y - tangent_y * knot_middle / 3.0;
}

/* Computes the square root of the distance between two points, the centripetal knot spacing. */
static double
knot_distance(
	double from_x,
	double from_y,
	double to_x,
	double to_y)
{
	double step_x;
	double step_y;
	double length;
	double root;

	/* Measures the step between the points. */
	step_x = to_x - from_x;
	step_y = to_y - from_y;

	/* The square root of its length (the centripetal parameterization's knot distance). */
	length = sqrt(step_x * step_x + step_y * step_y);
	root = sqrt(length);

	/* Reports the knot distance. */
	return root;
}

/*
 * Counts the pieces an interval's curve is cut into.
 *
 * Wang's bound for a cubic: n pieces stray at most 3/4 * d / n^2 from the
 * curve, where d is the larger second difference of the control points.
 */
static size_t
interval_pieces(
	const struct pdf_outline_piece *piece)
{
	double first_x;
	double first_y;
	double second_x;
	double second_y;
	double first;
	double second;
	double largest;
	double pieces;

	/* Measures the two second differences of the control points. */
	first_x = piece->start_x - 2.0 * piece->control1_x + piece->control2_x;
	first_y = piece->start_y - 2.0 * piece->control1_y + piece->control2_y;
	second_x = piece->control1_x - 2.0 * piece->control2_x + piece->end_x;
	second_y = piece->control1_y - 2.0 * piece->control2_y + piece->end_y;
	first = sqrt(first_x * first_x + first_y * first_y);
	second = sqrt(second_x * second_x + second_y * second_y);
	largest = first;
	if (second > largest)
		largest = second;

	/* Finds the fewest pieces that stay within the tolerance, within the limits. */
	pieces = ceil(sqrt(0.75 * largest / PDF_OUTLINE_TOLERANCE));
	if (pieces < 1.0)
		pieces = 1.0;
	if (pieces > PDF_OUTLINE_PIECES_MAX)
		pieces = PDF_OUTLINE_PIECES_MAX;

	/* Reports the whole number of pieces. */
	return (size_t)pieces;
}

/*
 * Interpolates the pressure at a point of an interval.
 *
 * A Catmull-Rom curve through the four pressures around the interval gives
 * a smooth change; it is kept between the interval's two end pressures, so
 * a stroke never draws wider or thinner than the samples on either side.
 */
static double
interpolate_pressure(
	const struct pdf_outline_piece *piece,
	double t)
{
	double slope_start;
	double slope_end;
	double t2;
	double t3;
	double pressure;
	double lowest;
	double highest;

	/* Finds the slopes at the interval's ends from the neighboring pressures. */
	slope_start = (piece->pressure_end - piece->pressure_before) / 2.0;
	slope_end = (piece->pressure_after - piece->pressure_start) / 2.0;

	/* Evaluates the cubic Hermite curve between the end pressures. */
	t2 = t * t;
	t3 = t2 * t;
	pressure = (2.0 * t3 - 3.0 * t2 + 1.0) * piece->pressure_start + (t3 - 2.0 * t2 + t) * slope_start + (-2.0 * t3 + 3.0 * t2) * piece->pressure_end + (t3 - t2) * slope_end;

	/* Keeps the pressure between the two end pressures. */
	lowest = piece->pressure_start;
	highest = piece->pressure_end;
	if (highest < lowest) {
		lowest = piece->pressure_end;
		highest = piece->pressure_start;
	}

	/* The pressure within them. */
	if (pressure < lowest)
		pressure = lowest;
	if (pressure > highest)
		pressure = highest;

	/* Reports the interpolated pressure. */
	return pressure;
}

/*
 * Appends a centerline point.
 *
 * A point that has not moved away from the previous one is merged into it,
 * keeping the wider of the two, so no segment of the centerline is empty.
 */
static void
path_append(
	struct pdf_outline_path *path,
	double x,
	double y,
	double radius)
{
	struct pdf_outline_vertex *grown;
	struct pdf_outline_vertex *last;
	size_t capacity;
	double step_x;
	double step_y;

	/* A failed centerline takes no more points. */
	if (path->error != 0)
		return;

	/* Merges a point that has not moved into the previous one. */
	if (path->count != 0) {
		last = &path->vertices[path->count - 1];
		step_x = x - last->x;
		step_y = y - last->y;
		if (step_x * step_x + step_y * step_y < PDF_OUTLINE_MIN_STEP * PDF_OUTLINE_MIN_STEP) {
			if (radius > last->radius)
				last->radius = radius;
			return;
		}
	}

	/* Refuses a centerline longer than one outline may be. */
	if (path->count == PDF_OUTLINE_CORNERS_MAX) {
		path->error = EINVAL;
		return;
	}

	/* Grows the vertex array when it is full. */
	if (path->count == path->capacity) {
		capacity = path->capacity * 2;
		if (capacity == 0)
			capacity = 64;
		grown = realloc(path->vertices, capacity * sizeof(*grown));
		if (grown == NULL) {
			path->error = ENOMEM;
			return;
		}

		/* The grown array is the path's. */
		path->vertices = grown;
		path->capacity = capacity;
	}

	/* Appends the point. */
	path->vertices[path->count].x = x;
	path->vertices[path->count].y = y;
	path->vertices[path->count].radius = radius;
	path->count++;
}

/*
 * Walks around a centerline of two or more points.
 *
 * The left edge lies along each segment's left normal and the right edge
 * along its right one.  At a corner, the edge on the outside of the turn
 * follows an arc around the corner point from one segment's normal to the
 * next; the edge on the inside passes through the corner point.  A gentle
 * turn, whose arc would be one chord, takes one corner on each edge.
 */
static void
build_outline(
	const struct pdf_outline_path *path,
	struct pdf_outline_polygon *polygon)
{
	const struct pdf_outline_vertex *vertices;
	const struct pdf_outline_vertex *corner;
	const struct pdf_outline_vertex *last;
	struct pdf_outline_join join;
	size_t count;
	size_t index;
	size_t chords;
	double normal_x;
	double normal_y;
	double start_angle;

	/* Names the centerline and its last point. */
	vertices = path->vertices;
	count = path->count;
	last = &vertices[count - 1];

	/* Starts the left edge at the first point. */
	segment_normal(&vertices[0], &vertices[1], &normal_x, &normal_y);
	polygon_append(polygon, vertices[0].x + normal_x * vertices[0].radius, vertices[0].y + normal_y * vertices[0].radius);

	/* Walks the left edge forward through each inner point. */
	for (index = 1; index + 1 < count; index++) {
		/* Measures the turn at the point. */
		corner = &vertices[index];
		measure_join(vertices, index, &join);

		/* Joins the two segments' left edges. */
		if (join.gentle) {
			/* A gentle turn needs one corner, on the middle normal. */
			polygon_append(polygon, corner->x + join.middle_x * corner->radius, corner->y + join.middle_y * corner->radius);
		} else if (join.turn < 0.0) {
			/* A turn to the right puts the left edge outside: it follows a round join. */
			start_angle = atan2(join.before_y, join.before_x);
			chords = arc_chords(corner->radius, -join.turn, 1);
			polygon_append(polygon, corner->x + join.before_x * corner->radius, corner->y + join.before_y * corner->radius);
			append_arc(polygon, corner, start_angle, join.turn, chords);
			polygon_append(polygon, corner->x + join.after_x * corner->radius, corner->y + join.after_y * corner->radius);
		} else {
			/* A turn to the left puts the left edge inside: it passes through the point. */
			polygon_append(polygon, corner->x + join.before_x * corner->radius, corner->y + join.before_y * corner->radius);
			polygon_append(polygon, corner->x, corner->y);
			polygon_append(polygon, corner->x + join.after_x * corner->radius, corner->y + join.after_y * corner->radius);
		}
	}

	/* Ends the left edge at the last point. */
	segment_normal(&vertices[count - 2], last, &normal_x, &normal_y);
	polygon_append(polygon, last->x + normal_x * last->radius, last->y + normal_y * last->radius);

	/* Turns around the last point, from the left edge through the stroke's direction to the right edge. */
	start_angle = atan2(normal_y, normal_x);
	chords = arc_chords(last->radius, PDF_OUTLINE_PI, PDF_OUTLINE_CAP_MIN);
	append_arc(polygon, last, start_angle, -PDF_OUTLINE_PI, chords);

	/* Starts the right edge back at the last point. */
	polygon_append(polygon, last->x - normal_x * last->radius, last->y - normal_y * last->radius);

	/* Walks the right edge back through each inner point. */
	for (index = count - 2; index > 0; index--) {
		/* Measures the turn at the point. */
		corner = &vertices[index];
		measure_join(vertices, index, &join);

		/* Joins the two segments' right edges. */
		if (join.gentle) {
			/* A gentle turn needs one corner, on the middle normal. */
			polygon_append(polygon, corner->x - join.middle_x * corner->radius, corner->y - join.middle_y * corner->radius);
		} else if (join.turn > 0.0) {
			/* A turn to the left puts the right edge outside: it follows a round join. */
			start_angle = atan2(-join.after_y, -join.after_x);
			chords = arc_chords(corner->radius, join.turn, 1);
			polygon_append(polygon, corner->x - join.after_x * corner->radius, corner->y - join.after_y * corner->radius);
			append_arc(polygon, corner, start_angle, -join.turn, chords);
			polygon_append(polygon, corner->x - join.before_x * corner->radius, corner->y - join.before_y * corner->radius);
		} else {
			/* A turn to the right puts the right edge inside: it passes through the point. */
			polygon_append(polygon, corner->x - join.after_x * corner->radius, corner->y - join.after_y * corner->radius);
			polygon_append(polygon, corner->x, corner->y);
			polygon_append(polygon, corner->x - join.before_x * corner->radius, corner->y - join.before_y * corner->radius);
		}
	}

	/* Ends the right edge at the first point. */
	segment_normal(&vertices[0], &vertices[1], &normal_x, &normal_y);
	polygon_append(polygon, vertices[0].x - normal_x * vertices[0].radius, vertices[0].y - normal_y * vertices[0].radius);

	/* Turns around the first point, from the right edge against the stroke's direction back to the left edge. */
	start_angle = atan2(-normal_y, -normal_x);
	chords = arc_chords(vertices[0].radius, PDF_OUTLINE_PI, PDF_OUTLINE_CAP_MIN);
	append_arc(polygon, &vertices[0], start_angle, -PDF_OUTLINE_PI, chords);
}

/* Measures the normals and the turn at an inner point of the centerline. */
static void
measure_join(
	const struct pdf_outline_vertex *vertices,
	size_t index,
	struct pdf_outline_join *join)
{
	double length;
	double step;
	double magnitude;

	/* Finds the normals of the segments on either side of the point and the turn between them. */
	segment_normal(&vertices[index - 1], &vertices[index], &join->before_x, &join->before_y);
	segment_normal(&vertices[index], &vertices[index + 1], &join->after_x, &join->after_y);
	join->turn = turn_angle(&vertices[index - 1], &vertices[index], &vertices[index + 1]);

	/* A turn that one chord of the join's arc covers is gentle. */
	step = arc_step(vertices[index].radius);
	magnitude = fabs(join->turn);
	join->gentle = 0;
	if (magnitude <= step)
		join->gentle = 1;

	/*
	 * Finds the normal halfway between the two.  A gentle turn is at most a
	 * quarter turn, so the two normals never cancel.
	 */
	join->middle_x = join->before_x + join->after_x;
	join->middle_y = join->before_y + join->after_y;
	length = sqrt(join->middle_x * join->middle_x + join->middle_y * join->middle_y);
	if (length > 0.0) {
		join->middle_x /= length;
		join->middle_y /= length;
	}
}

/*
 * Measures the signed angle the centerline turns at a point.
 *
 * It is positive when the stroke turns toward its left normal and runs from
 * minus to plus pi; a stroke that doubles back turns by pi either way.
 */
static double
turn_angle(
	const struct pdf_outline_vertex *before,
	const struct pdf_outline_vertex *corner,
	const struct pdf_outline_vertex *after)
{
	double in_x;
	double in_y;
	double out_x;
	double out_y;
	double cross;
	double dot;
	double angle;

	/* Takes the segments that arrive at and leave the point. */
	in_x = corner->x - before->x;
	in_y = corner->y - before->y;
	out_x = after->x - corner->x;
	out_y = after->y - corner->y;

	/* Compares their directions. */
	cross = in_x * out_y - in_y * out_x;
	dot = in_x * out_x + in_y * out_y;

	/* The angle from the arriving direction to the leaving one. */
	angle = atan2(cross, dot);

	/* Reports the turn, in radians. */
	return angle;
}

/* Finds the unit left normal of a segment, its direction turned a quarter toward positive angles. */
static void
segment_normal(
	const struct pdf_outline_vertex *from,
	const struct pdf_outline_vertex *to,
	double *normal_x,
	double *normal_y)
{
	double step_x;
	double step_y;
	double length;

	/* Measures the segment, which the centerline builder never leaves empty. */
	step_x = to->x - from->x;
	step_y = to->y - from->y;
	length = sqrt(step_x * step_x + step_y * step_y);

	/* Turns the unit direction a quarter. */
	*normal_x = -step_y / length;
	*normal_y = step_x / length;
}

/* Draws a stroke that never moved as a whole circle around its only point. */
static void
build_dot(
	const struct pdf_outline_vertex *center,
	struct pdf_outline_polygon *polygon)
{
	size_t chords;

	/* Starts on the circle and walks it once; the polygon closes itself. */
	chords = arc_chords(center->radius, 2.0 * PDF_OUTLINE_PI, 2 * PDF_OUTLINE_CAP_MIN);
	polygon_append(polygon, center->x + center->radius, center->y);
	append_arc(polygon, center, 0.0, 2.0 * PDF_OUTLINE_PI, chords);
}

/*
 * Computes the widest angle one chord of a circle may span and stay within
 * the tolerance of the circle.
 *
 * A circle smaller than the tolerance takes a quarter turn per chord.
 */
static double
arc_step(
	double radius)
{
	/* A circle smaller than the tolerance needs no finer chords. */
	if (radius <= PDF_OUTLINE_TOLERANCE)
		return PDF_OUTLINE_PI / 2.0;

	/* Reports the angle whose chord strays from the circle by the tolerance. */
	return 2.0 * acos(1.0 - PDF_OUTLINE_TOLERANCE / radius);
}

/*
 * Counts the chords an arc needs to stay within the tolerance of its circle.
 *
 * angle is the arc's size in radians.  The count is at least fewest and at
 * most the half turn's limit in proportion to the arc.
 */
static size_t
arc_chords(
	double radius,
	double angle,
	size_t fewest)
{
	double step;
	double chords;
	double most;

	/* Finds the widest angle one chord may span. */
	step = arc_step(radius);

	/* Divides the arc into chords no wider than the step, within the limits. */
	chords = ceil(angle / step);
	most = ceil(PDF_OUTLINE_CAP_MAX * angle / PDF_OUTLINE_PI);
	if (chords > most)
		chords = most;
	if (chords < (double)fewest)
		chords = (double)fewest;

	/* Reports the whole number of chords. */
	return (size_t)chords;
}

/*
 * Appends the inner corners of an arc around a point.
 *
 * The arc starts at start_angle and turns by sweep radians in chords steps.
 * Its two ends are the edges' corners, which the caller writes itself, so
 * only the corners between them are appended.
 */
static void
append_arc(
	struct pdf_outline_polygon *polygon,
	const struct pdf_outline_vertex *center,
	double start_angle,
	double sweep,
	size_t chords)
{
	size_t step;
	double angle;

	/* Writes each corner strictly between the arc's ends. */
	for (step = 1; step < chords; step++) {
		angle = start_angle + sweep * (double)step / (double)chords;
		polygon_append(polygon, center->x + center->radius * cos(angle), center->y + center->radius * sin(angle));
	}
}

/* Appends one corner to the outline polygon. */
static void
polygon_append(
	struct pdf_outline_polygon *polygon,
	double x,
	double y)
{
	struct pdf_point *grown;
	size_t capacity;

	/* A failed polygon takes no more corners. */
	if (polygon->error != 0)
		return;

	/* Refuses a polygon larger than one outline may be. */
	if (polygon->count == PDF_OUTLINE_CORNERS_MAX) {
		polygon->error = EINVAL;
		return;
	}

	/* Grows the corner array when it is full. */
	if (polygon->count == polygon->capacity) {
		capacity = polygon->capacity * 2;
		if (capacity == 0)
			capacity = 64;
		grown = realloc(polygon->corners, capacity * sizeof(*grown));
		if (grown == NULL) {
			polygon->error = ENOMEM;
			return;
		}

		/* The grown array is the polygon's. */
		polygon->corners = grown;
		polygon->capacity = capacity;
	}

	/* Appends the corner. */
	polygon->corners[polygon->count].x = x;
	polygon->corners[polygon->count].y = y;
	polygon->count++;
}
