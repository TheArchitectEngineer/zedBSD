/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The handwriting recognizer's point clouds and their matching
 * (hand-cloud.h, ws165-p002).
 *
 * A cloud is made as $P makes one: the strokes' points resampled to
 * HAND_CLOUD_POINTS points an equal distance apart along the strokes (no
 * point is put on the jump from one stroke to the next), scaled by the
 * larger of the width and the height so that the shape keeps its
 * proportions, and moved so that the mean of the points is the origin.
 * Two clouds are compared from every HAND_CLOUD_STEP-th point of one, both
 * ways: going round from that point, each point takes the nearest point
 * of the other cloud not taken yet, and the distance counts the more the
 * earlier the point comes.  The smallest of these sums is the distance.
 */

#include "hand-cloud.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* How far apart the starting points of the matching are (about the square root of the points). */
#define HAND_CLOUD_STEP		5U

/* The longest template line's part kept while it is read. */
#define HAND_LINE_MAX		16384U

/* The Hershey glyphs' area (the templates' units): its top and height. */
#define HAND_TEMPLATE_TOP	(-16.0f)
#define HAND_TEMPLATE_HEIGHT	32.0f

/*
 * The weight of the size (a factor of e between the ink's and the
 * template's adds this much) and of the place (the middle a whole area's
 * height away adds this much), and the least size counted (a dot's).
 */
#define HAND_SIZE_WEIGHT	1.0f
#define HAND_PLACE_WEIGHT	3.0f
#define HAND_SIZE_LEAST		0.04f

static float cloud_match(const struct hand_cloud *from, const struct hand_cloud *to, unsigned start, float bound);
static float cloud_distance(const struct hand_cloud *written, const struct hand_cloud *template_cloud, float bound);
static float cloud_length(const struct hand_cloud_input *input);
static int templates_line(struct hand_templates *templates, const char *line, size_t length);
static void cloud_extent(const struct hand_cloud_input *input, struct hand_extent *extent);
static float framed_penalty(const struct hand_extent *ink, const struct hand_frame *frame, const struct hand_extent *glyph);
static size_t recognize_cloud(const struct hand_templates *templates, const struct hand_cloud *written, const struct hand_extent *ink, const struct hand_frame *frame, uint32_t *codes, float *distances, size_t capacity);
static size_t recognize_strokes(const struct hand_templates *templates, const struct hand_cloud_input *input, const struct hand_frame *frame, uint32_t *codes, float *distances, size_t capacity);
static unsigned strokes_mark(const struct hand_cloud_input *input, unsigned char *mark);
static uint32_t strokes_voiced(uint32_t code, unsigned mark);
static uint32_t strokes_unvoiced(uint32_t code);

/* The marks a written kana may carry. */
#define HAND_MARK_NONE		0U
#define HAND_MARK_DAKUTEN	1U
#define HAND_MARK_HANDAKUTEN	2U

/* The most strokes looked at for a mark. */
#define HAND_MARK_STROKES	64U

/*
 * Makes a cloud of a stroke list.  Returns 0, or EINVAL for a list without
 * points.
 */
int
hand_cloud_make(
	const struct hand_cloud_input *input,
	struct hand_cloud *cloud)
{
	struct hand_cloud_point last;
	struct hand_cloud_point next;
	float interval;
	float walked;
	float step;
	float min_x;
	float min_y;
	float max_x;
	float max_y;
	float size;
	float mean_x;
	float mean_y;
	size_t made;
	size_t index;

	/* Points to make it of. */
	if (input->count == 0U)
		return EINVAL;

	/* The resampling: a point every interval along the strokes, the first point first. */
	interval = cloud_length(input) / (float)(HAND_CLOUD_POINTS - 1U);
	cloud->points[0].x = input->x[0];
	cloud->points[0].y = input->y[0];
	made = 1U;
	walked = 0.0f;
	last = cloud->points[0];
	for (index = 1U; index < input->count && made < HAND_CLOUD_POINTS; index++) {
		next.x = input->x[index];
		next.y = input->y[index];

		/* A new stroke starts from its own first point: nothing on the jump. */
		if (input->starts[index]) {
			last = next;
			continue;
		}

		/* As many points as fit on the segment from the last point to this one. */
		step = hypotf(next.x - last.x, next.y - last.y);
		while (interval > 0.0f && walked + step >= interval && made < HAND_CLOUD_POINTS) {
			last.x += (interval - walked) / step * (next.x - last.x);
			last.y += (interval - walked) / step * (next.y - last.y);
			cloud->points[made++] = last;
			step = hypotf(next.x - last.x, next.y - last.y);
			walked = 0.0f;
		}

		/* The rest of the segment is walked. */
		walked += step;
		last = next;
	}

	/* A shortfall (rounding, or a dot) is filled with the last point. */
	while (made < HAND_CLOUD_POINTS) {
		cloud->points[made].x = input->x[input->count - 1U];
		cloud->points[made].y = input->y[input->count - 1U];
		made++;
	}

	/* The bounds and the mean. */
	min_x = FLT_MAX;
	min_y = FLT_MAX;
	max_x = -FLT_MAX;
	max_y = -FLT_MAX;
	for (index = 0U; index < HAND_CLOUD_POINTS; index++) {
		min_x = fminf(min_x, cloud->points[index].x);
		min_y = fminf(min_y, cloud->points[index].y);
		max_x = fmaxf(max_x, cloud->points[index].x);
		max_y = fmaxf(max_y, cloud->points[index].y);
	}

	/* The larger side (a dot has none: 1). */
	size = fmaxf(max_x - min_x, max_y - min_y);
	if (size <= 0.0f)
		size = 1.0f;

	/* Scaled into a unit square, the proportions kept. */
	mean_x = 0.0f;
	mean_y = 0.0f;
	for (index = 0U; index < HAND_CLOUD_POINTS; index++) {
		cloud->points[index].x = (cloud->points[index].x - min_x) / size;
		cloud->points[index].y = (cloud->points[index].y - min_y) / size;
		mean_x += cloud->points[index].x;
		mean_y += cloud->points[index].y;
	}

	/* Moved so that the mean is the origin. */
	mean_x /= (float)HAND_CLOUD_POINTS;
	mean_y /= (float)HAND_CLOUD_POINTS;
	for (index = 0U; index < HAND_CLOUD_POINTS; index++) {
		cloud->points[index].x -= mean_x;
		cloud->points[index].y -= mean_y;
	}

	/* Succeeded: the cloud. */
	return 0;
}

/*
 * Gives the distance of a written cloud from a template's: the smallest
 * weighted sum of the greedy matchings, from every HAND_CLOUD_STEP-th point
 * and both ways.
 */
float
hand_cloud_distance(
	const struct hand_cloud *written,
	const struct hand_cloud *template_cloud)
{
	float distance;

	/* Without a bound. */
	distance = cloud_distance(written, template_cloud, FLT_MAX);
	return distance;
}

/*
 * Reads templates from text (hand-cloud.h).  A line that is not a
 * template's (a comment, an empty one) is skipped.  Returns 0, EINVAL for
 * a template line that does not read, or ENOMEM.
 */
int
hand_templates_parse(
	struct hand_templates *templates,
	const char *text,
	size_t length)
{
	const char *start;
	const char *end;
	int error;

	/* None yet. */
	templates->items = NULL;
	templates->count = 0U;

	/* Each line. */
	start = text;
	while (start < text + length) {
		end = memchr(start, '\n', (size_t)(text + length - start));
		if (end == NULL)
			end = text + length;

		/* A template's. */
		if (end - start > 2 && start[0] == 'U' && start[1] == '+') {
			error = templates_line(templates, start, (size_t)(end - start));
			if (error != 0) {
				hand_templates_free(templates);
				return error;
			}
		}

		/* The next line. */
		start = end + 1;
	}

	/* Succeeded: the templates. */
	return 0;
}

/* Frees the templates read. */
void
hand_templates_free(
	struct hand_templates *templates)
{
	/* The table. */
	free(templates->items);
	templates->items = NULL;
	templates->count = 0U;
}

/*
 * Gives the characters nearest to a written cloud, the nearest first: at
 * most capacity code points (a character with several templates once, at
 * its best) and their distances.  Returns how many.
 */
size_t
hand_recognize(
	const struct hand_templates *templates,
	const struct hand_cloud *written,
	uint32_t *codes,
	float *distances,
	size_t capacity)
{
	/* Without an area. */
	return recognize_cloud(templates, written, NULL, NULL, codes, distances, capacity);
}

/*
 * Gives the characters nearest to a written cloud as hand_recognize does;
 * with an area (frame not NULL), each distance has how far the ink's box
 * on the area is from the template's added (framed_penalty).
 */
static size_t
recognize_cloud(
	const struct hand_templates *templates,
	const struct hand_cloud *written,
	const struct hand_extent *ink,
	const struct hand_frame *frame,
	uint32_t *codes,
	float *distances,
	size_t capacity)
{
	size_t count;
	size_t index;
	size_t place;
	size_t seen;
	float distance;
	float bound;
	float penalty;
	int duplicate;

	/* Each template against the cloud, given up once it is farther than the farthest kept. */
	count = 0U;
	for (index = 0U; index < templates->count; index++) {
		bound = FLT_MAX;
		if (count == capacity)
			bound = distances[count - 1U];

		/* The size and the place first: a template already too far is not matched. */
		penalty = 0.0f;
		if (frame != NULL)
			penalty = framed_penalty(ink, frame, &templates->items[index].extent);
		if (penalty >= bound)
			continue;
		distance = cloud_distance(written, &templates->items[index].cloud, bound - penalty);
		distance += penalty;
		if (distance >= bound)
			continue;

		/* A character kept already keeps its better distance. */
		duplicate = 0;
		for (seen = 0U; seen < count; seen++) {
			if (codes[seen] != templates->items[index].code)
				continue;
			duplicate = 1;
			if (distance < distances[seen]) {
				memmove(&codes[seen], &codes[seen + 1U], (count - seen - 1U) * sizeof(codes[0]));
				memmove(&distances[seen], &distances[seen + 1U], (count - seen - 1U) * sizeof(distances[0]));
				count--;
				duplicate = 0;
			}

			/* One of a character is enough. */
			break;
		}

		/* A worse duplicate is not kept. */
		if (duplicate)
			continue;

		/* Its place among the nearest, when it is one of them. */
		place = count;
		while (place > 0U && distances[place - 1U] > distance)
			place--;
		if (place >= capacity)
			continue;
		if (count < capacity)
			count++;
		memmove(&codes[place + 1U], &codes[place], (count - place - 1U) * sizeof(codes[0]));
		memmove(&distances[place + 1U], &distances[place], (count - place - 1U) * sizeof(distances[0]));
		codes[place] = templates->items[index].code;
		distances[place] = distance;
	}

	/* The nearest. */
	return count;
}

/*
 * Recognizes a stroke list: its voicing mark found apart and put on the
 * candidates of the rest that take it (hand-cloud.h), then the
 * candidates of the whole ink after them.  Returns how many, at most
 * capacity, the nearest first.
 */
size_t
hand_recognize_strokes(
	const struct hand_templates *templates,
	const struct hand_cloud_input *input,
	uint32_t *codes,
	float *distances,
	size_t capacity)
{
	/* Without an area. */
	return recognize_strokes(templates, input, NULL, codes, distances, capacity);
}

/*
 * Recognizes a stroke list as hand_recognize_strokes does; with an area
 * (frame not NULL), the size and the place of the ink (or of the rest
 * without its mark) count too (recognize_cloud).
 */
static size_t
recognize_strokes(
	const struct hand_templates *templates,
	const struct hand_cloud_input *input,
	const struct hand_frame *frame,
	uint32_t *codes,
	float *distances,
	size_t capacity)
{
	static float xs[HAND_CLOUD_INPUT_MAX];
	static float ys[HAND_CLOUD_INPUT_MAX];
	static unsigned char starts[HAND_CLOUD_INPUT_MAX];
	unsigned char marked[HAND_CLOUD_INPUT_MAX];
	struct hand_cloud_input body;
	struct hand_cloud cloud;
	uint32_t base[8];
	float base_distances[8];
	uint32_t plain[8];
	float plain_distances[8];
	struct hand_extent extent;
	uint32_t voiced;
	size_t base_count;
	size_t plain_count;
	size_t count;
	size_t index;
	size_t seen;
	unsigned mark;
	int fresh;
	int error;
	int duplicate;

	/* The whole ink's candidates. */
	if (capacity > 8U)
		capacity = 8U;
	plain_count = 0U;
	cloud_extent(input, &extent);
	error = hand_cloud_make(input, &cloud);
	if (error == 0)
		plain_count = recognize_cloud(templates, &cloud, &extent, frame, plain, plain_distances, capacity);

	/* Without a mark, those: the characters without a mark first (a voiced one needs its mark). */
	mark = HAND_MARK_NONE;
	if (input->count <= HAND_CLOUD_INPUT_MAX)
		mark = strokes_mark(input, marked);
	if (mark == HAND_MARK_NONE) {
		count = 0U;
		for (index = 0U; index < plain_count; index++) {
			voiced = strokes_unvoiced(plain[index]);
			if (voiced != 0U)
				continue;
			codes[count] = plain[index];
			distances[count] = plain_distances[index];
			count++;
		}

		/* Then the voiced ones. */
		for (index = 0U; index < plain_count; index++) {
			voiced = strokes_unvoiced(plain[index]);
			if (voiced == 0U)
				continue;
			codes[count] = plain[index];
			distances[count] = plain_distances[index];
			count++;
		}

		/* The candidates. */
		return count;
	}

	/* The rest without the mark's strokes. */
	body.count = 0U;
	fresh = 1;
	for (index = 0U; index < input->count; index++) {
		if (input->starts[index])
			fresh = 1;
		if (marked[index])
			continue;
		xs[body.count] = input->x[index];
		ys[body.count] = input->y[index];
		starts[body.count] = (unsigned char)fresh;
		fresh = 0;
		body.count++;
	}

	/* Its candidates. */
	body.x = xs;
	body.y = ys;
	body.starts = starts;
	base_count = 0U;
	cloud_extent(&body, &extent);
	error = hand_cloud_make(&body, &cloud);
	if (error == 0)
		base_count = recognize_cloud(templates, &cloud, &extent, frame, base, base_distances, 8U);

	/* The rest's candidates that take the mark, voiced, first. */
	count = 0U;
	for (index = 0U; index < base_count && count < capacity; index++) {
		voiced = strokes_voiced(base[index], mark);
		if (voiced == 0U)
			continue;
		codes[count] = voiced;
		distances[count] = base_distances[index];
		count++;
	}

	/* Then the whole ink's, each once. */
	for (index = 0U; index < plain_count && count < capacity; index++) {
		duplicate = 0;
		for (seen = 0U; seen < count; seen++) {
			if (codes[seen] == plain[index])
				duplicate = 1;
		}

		/* Each once. */
		if (duplicate)
			continue;
		codes[count] = plain[index];
		distances[count] = plain_distances[index];
		count++;
	}

	/* The candidates. */
	return count;
}

/*
 * Recognizes a stroke list written on an area as hand_recognize_strokes
 * does, each template's distance with how far the ink's size and place on
 * the area are from the template's on the Hershey glyphs' area added: a
 * small c before C, a dot low on the area before the middle dot.  An area
 * of no height is recognized without it.  Returns how many candidates, at
 * most capacity, the likeliest first.
 */
size_t
hand_recognize_framed(
	const struct hand_templates *templates,
	const struct hand_cloud_input *input,
	const struct hand_frame *frame,
	uint32_t *codes,
	float *distances,
	size_t capacity)
{
	/* An area of no height is none. */
	if (frame == NULL || frame->height <= 0.0f)
		return recognize_strokes(templates, input, NULL, codes, distances, capacity);
	return recognize_strokes(templates, input, frame, codes, distances, capacity);
}

/*
 * Gives the distance of two clouds (hand_cloud_distance's), or bound
 * once every way has passed it.
 */
static float
cloud_distance(
	const struct hand_cloud *written,
	const struct hand_cloud *template_cloud,
	float bound)
{
	float best;
	float one;
	float other;
	unsigned start;

	/* Each starting point, each way; a sum past the best so far is given up. */
	best = bound;
	for (start = 0U; start < HAND_CLOUD_POINTS; start += HAND_CLOUD_STEP) {
		one = cloud_match(written, template_cloud, start, best);
		best = fminf(best, one);
		other = cloud_match(template_cloud, written, start, best);
		best = fminf(best, other);
	}

	/* The smallest, or the bound. */
	return best;
}

/*
 * Matches one cloud's points with the other's, greedily from start round
 * to it again: the weighted sum of the distances, or bound as soon as the
 * sum passes it.
 */
static float
cloud_match(
	const struct hand_cloud *from,
	const struct hand_cloud *to,
	unsigned start,
	float bound)
{
	unsigned char taken[HAND_CLOUD_POINTS];
	unsigned index;
	unsigned other;
	unsigned nearest;
	float sum;
	float weight;
	float distance;
	float smallest;
	float dx;
	float dy;

	/* Nothing taken yet. */
	memset(taken, 0, sizeof(taken));
	sum = 0.0f;
	index = start;

	/* Each point in turn, the earlier weighing more. */
	do {
		smallest = FLT_MAX;
		nearest = 0U;
		for (other = 0U; other < HAND_CLOUD_POINTS; other++) {
			if (taken[other])
				continue;
			dx = from->points[index].x - to->points[other].x;
			dy = from->points[index].y - to->points[other].y;
			distance = dx * dx + dy * dy;
			if (distance < smallest) {
				smallest = distance;
				nearest = other;
			}
		}

		/* Taken, its distance weighed by how early the point came. */
		taken[nearest] = 1U;
		weight = 1.0f - (float)((index + HAND_CLOUD_POINTS - start) % HAND_CLOUD_POINTS) / (float)HAND_CLOUD_POINTS;
		sum += weight * sqrtf(smallest);
		if (sum >= bound)
			return bound;
		index = (index + 1U) % HAND_CLOUD_POINTS;
	} while (index != start);

	/* The sum. */
	return sum;
}

/* Gives the length of the strokes (the jumps between them not counted). */
static float
cloud_length(
	const struct hand_cloud_input *input)
{
	float length;
	size_t index;

	/* Each segment within a stroke. */
	length = 0.0f;
	for (index = 1U; index < input->count; index++) {
		if (input->starts[index])
			continue;
		length += hypotf(input->x[index] - input->x[index - 1U], input->y[index] - input->y[index - 1U]);
	}

	/* The sum. */
	return length;
}

/*
 * Reads one template's line ("U+XXXX NUMBER x,y x,y / x,y ...") into a
 * new template.  Returns 0, EINVAL or ENOMEM.
 */
static int
templates_line(
	struct hand_templates *templates,
	const char *line,
	size_t length)
{
	static float xs[HAND_CLOUD_INPUT_MAX];
	static float ys[HAND_CLOUD_INPUT_MAX];
	static unsigned char starts[HAND_CLOUD_INPUT_MAX];
	struct hand_cloud_input input;
	struct hand_template *grown;
	char copy[HAND_LINE_MAX];
	char *word;
	char *rest;
	char *end;
	unsigned long code;
	long x;
	long y;
	size_t count;
	int fresh;
	int error;

	/* A copy to cut into words. */
	if (length >= sizeof(copy))
		return EINVAL;
	memcpy(copy, line, length);
	copy[length] = '\0';

	/* The code point, then the number (skipped). */
	code = strtoul(copy + 2, &end, 16);
	if (end == copy + 2 || *end != ' ' || code == 0UL || code > 0x10ffffUL)
		return EINVAL;
	rest = strchr(end + 1, ' ');
	if (rest == NULL)
		return EINVAL;

	/* The points: "/" starts a new stroke. */
	count = 0U;
	fresh = 1;
	word = strtok(rest, " ");
	while (word != NULL) {
		if (word[0] == '/' && word[1] == '\0') {
			fresh = 1;
			word = strtok(NULL, " ");
			continue;
		}

		/* A point "x,y". */
		x = strtol(word, &end, 10);
		if (*end != ',' || count >= HAND_CLOUD_INPUT_MAX)
			return EINVAL;
		y = strtol(end + 1, &end, 10);
		if (*end != '\0')
			return EINVAL;
		xs[count] = (float)x;
		ys[count] = (float)y;
		starts[count] = (unsigned char)fresh;
		fresh = 0;
		count++;
		word = strtok(NULL, " ");
	}

	/* A place for it. */
	grown = realloc(templates->items, (templates->count + 1U) * sizeof(templates->items[0]));
	if (grown == NULL)
		return ENOMEM;
	templates->items = grown;

	/* Its cloud. */
	input.x = xs;
	input.y = ys;
	input.starts = starts;
	input.count = count;
	error = hand_cloud_make(&input, &templates->items[templates->count].cloud);
	if (error != 0)
		return error;
	templates->items[templates->count].code = (uint32_t)code;
	cloud_extent(&input, &templates->items[templates->count].extent);
	templates->count++;

	/* Succeeded: one more template. */
	return 0;
}

/*
 * Finds a voicing mark among the strokes: small strokes at the top right
 * of the ink, two or more open ones a dakuten, a ring a handakuten.
 * Marks their points in mark and returns HAND_MARK_*.
 */
static unsigned
strokes_mark(
	const struct hand_cloud_input *input,
	unsigned char *mark)
{
	size_t begins[HAND_MARK_STROKES + 1U];
	size_t strokes;
	size_t index;
	size_t point;
	size_t small;
	size_t rings;
	float min_x;
	float min_y;
	float max_x;
	float max_y;
	float width;
	float height;
	float size;
	float s_min_x;
	float s_min_y;
	float s_max_x;
	float s_max_y;
	float s_size;
	float centre_x;
	float centre_y;
	float gap;
	int corner;
	unsigned char is_small[HAND_MARK_STROKES];
	unsigned char is_ring[HAND_MARK_STROKES];

	/* The strokes, and the whole ink's bounds. */
	memset(mark, 0, input->count);
	strokes = 0U;
	min_x = FLT_MAX;
	min_y = FLT_MAX;
	max_x = -FLT_MAX;
	max_y = -FLT_MAX;
	for (index = 0U; index < input->count; index++) {
		if (input->starts[index]) {
			if (strokes == HAND_MARK_STROKES)
				return HAND_MARK_NONE;
			begins[strokes++] = index;
		}

		/* The bounds. */
		min_x = fminf(min_x, input->x[index]);
		min_y = fminf(min_y, input->y[index]);
		max_x = fmaxf(max_x, input->x[index]);
		max_y = fmaxf(max_y, input->y[index]);
	}

	/* The last stroke's end, and the ink's size. */
	begins[strokes] = input->count;
	width = max_x - min_x;
	height = max_y - min_y;
	size = fmaxf(width, height);
	if (strokes < 2U || size <= 0.0f)
		return HAND_MARK_NONE;

	/* Each stroke: small, at the top right, and whether it closes on itself. */
	small = 0U;
	rings = 0U;
	for (index = 0U; index < strokes; index++) {
		s_min_x = FLT_MAX;
		s_min_y = FLT_MAX;
		s_max_x = -FLT_MAX;
		s_max_y = -FLT_MAX;
		for (point = begins[index]; point < begins[index + 1U]; point++) {
			s_min_x = fminf(s_min_x, input->x[point]);
			s_min_y = fminf(s_min_y, input->y[point]);
			s_max_x = fmaxf(s_max_x, input->x[point]);
			s_max_y = fmaxf(s_max_y, input->y[point]);
		}

		/* Its size, where it is, and how far its ends are apart. */
		s_size = fmaxf(s_max_x - s_min_x, s_max_y - s_min_y);
		centre_x = 0.5f * (s_min_x + s_max_x);
		centre_y = 0.5f * (s_min_y + s_max_y);
		corner = centre_x >= min_x + 0.55f * width && centre_y <= min_y + 0.40f * height;
		gap = hypotf(input->x[begins[index + 1U] - 1U] - input->x[begins[index]], input->y[begins[index + 1U] - 1U] - input->y[begins[index]]);
		is_ring[index] = (unsigned char)(corner && s_size <= 0.25f * size && s_size >= 0.05f * size && gap <= 0.35f * s_size &&
		    begins[index + 1U] - begins[index] >= 5U);
		is_small[index] = (unsigned char)(is_ring[index] || (corner && s_size <= 0.18f * size));
		if (is_small[index])
			small++;
		if (is_ring[index])
			rings++;
	}

	/* Too few small strokes, or every stroke small: no mark. */
	if (small == 0U || small == strokes || (rings == 0U && small < 2U))
		return HAND_MARK_NONE;

	/* The mark's points. */
	for (index = 0U; index < strokes; index++) {
		if (!is_small[index])
			continue;
		for (point = begins[index]; point < begins[index + 1U]; point++)
			mark[point] = 1U;
	}

	/* A ring is a handakuten, open strokes a dakuten. */
	if (rings != 0U)
		return HAND_MARK_HANDAKUTEN;
	return HAND_MARK_DAKUTEN;
}

/* Gives a kana with a mark (が for か and the dakuten), or 0 for one that does not take it. */
static uint32_t
strokes_voiced(
	uint32_t code,
	unsigned mark)
{
	static const uint16_t dakuten[] = {
		0x304b, 0x304d, 0x304f, 0x3051, 0x3053, 0x3055, 0x3057, 0x3059, 0x305b, 0x305d, 0x305f, 0x3061, 0x3064,
		0x3066, 0x3068, 0x306f, 0x3072, 0x3075, 0x3078, 0x307b
	};
	static const uint16_t handakuten[] = { 0x306f, 0x3072, 0x3075, 0x3078, 0x307b };
	uint32_t base;
	uint32_t shift;
	size_t index;

	/* Katakana as hiragana (the same distance apart). */
	shift = 0U;
	base = code;
	if (code >= 0x30a1U && code <= 0x30f6U) {
		shift = 0x60U;
		base = code - shift;
	}

	/* The dakuten: the next code point. */
	if (mark == HAND_MARK_DAKUTEN) {
		for (index = 0U; index < sizeof(dakuten) / sizeof(dakuten[0]); index++) {
			if (dakuten[index] == base)
				return base + 1U + shift;
		}

		/* One that does not take it. */
		return 0U;
	}

	/* The handakuten: two on. */
	for (index = 0U; index < sizeof(handakuten) / sizeof(handakuten[0]); index++) {
		if (handakuten[index] == base)
			return base + 2U + shift;
	}

	/* None. */
	return 0U;
}

/* Gives the kana without its mark for a voiced one (か for が), or 0 for a character without a mark. */
static uint32_t
strokes_unvoiced(
	uint32_t code)
{
	uint32_t base;
	uint32_t dakuten;
	uint32_t handakuten;

	/* Each kana that takes a mark, with each mark it takes. */
	for (base = 0x304bU; base <= 0x307bU; base++) {
		dakuten = strokes_voiced(base, HAND_MARK_DAKUTEN);
		handakuten = strokes_voiced(base, HAND_MARK_HANDAKUTEN);
		if (dakuten == code || handakuten == code)
			return base;
		dakuten = strokes_voiced(base + 0x60U, HAND_MARK_DAKUTEN);
		handakuten = strokes_voiced(base + 0x60U, HAND_MARK_HANDAKUTEN);
		if (dakuten == code || handakuten == code)
			return base + 0x60U;
	}

	/* Not a voiced kana. */
	return 0U;
}

/* Finds the box all the points of a stroke list take (an empty list: all zero). */
static void
cloud_extent(
	const struct hand_cloud_input *input,
	struct hand_extent *extent)
{
	size_t index;

	/* None. */
	memset(extent, 0, sizeof(*extent));
	if (input->count == 0U)
		return;

	/* Each point widens it. */
	extent->left = input->x[0];
	extent->right = input->x[0];
	extent->top = input->y[0];
	extent->bottom = input->y[0];
	for (index = 1U; index < input->count; index++) {
		extent->left = fminf(extent->left, input->x[index]);
		extent->right = fmaxf(extent->right, input->x[index]);
		extent->top = fminf(extent->top, input->y[index]);
		extent->bottom = fmaxf(extent->bottom, input->y[index]);
	}
}

/*
 * Gives how far a written character's size and place on its area are from
 * a template's on the Hershey glyphs' area: the size (the longer side, a
 * share of the area's height) as the logarithm of their ratio, and the
 * height of the middle, a share of the area's, each weighed.
 */
static float
framed_penalty(
	const struct hand_extent *ink,
	const struct hand_frame *frame,
	const struct hand_extent *glyph)
{
	float ink_size;
	float glyph_size;
	float ink_middle;
	float glyph_middle;

	/* The sizes, a dot's at least. */
	ink_size = fmaxf(ink->right - ink->left, ink->bottom - ink->top) / frame->height;
	glyph_size = fmaxf(glyph->right - glyph->left, glyph->bottom - glyph->top) / HAND_TEMPLATE_HEIGHT;
	ink_size = fmaxf(ink_size, HAND_SIZE_LEAST);
	glyph_size = fmaxf(glyph_size, HAND_SIZE_LEAST);

	/* The middles' heights on their areas. */
	ink_middle = ((ink->top + ink->bottom) * 0.5f - frame->top) / frame->height;
	glyph_middle = ((glyph->top + glyph->bottom) * 0.5f - HAND_TEMPLATE_TOP) / HAND_TEMPLATE_HEIGHT;

	/* Weighed. */
	return HAND_SIZE_WEIGHT * fabsf(logf(ink_size / glyph_size)) + HAND_PLACE_WEIGHT * fabsf(ink_middle - glyph_middle);
}
