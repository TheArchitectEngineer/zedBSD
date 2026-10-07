/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arithmetic and the file of several displays shown at once
 * (ws113-p004b; displays.h says what and why).
 */

#include "displays.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The longest line of the file read; a longer one is not one this version writes, and is skipped. */
#define DISPLAYS_LINE	256U

static int displays_overlap(const struct kwl_display_rect *one, const struct kwl_display_rect *other);
static int displays_touch(const struct kwl_display_rect *one, const struct kwl_display_rect *other);
static int displays_ranges_meet(int64_t start, int64_t end, int64_t other_start, int64_t other_end);
static int displays_in_range(const struct kwl_display_rect *rect);
static int displays_line(char *line, struct kwl_display_config *config, unsigned *version);
static int displays_place_line(const char *value, struct kwl_display_config *config);
static int displays_number(const char *text, char **end, int32_t *number);

/*
 * Fits an image of a size whole into a rectangle of another, keeping its
 * proportions and centring it: the rest of the rectangle is the bars.  A
 * size of zero fills the rectangle.
 */
void
kwl_displays_fit(
	uint32_t source_width,
	uint32_t source_height,
	uint32_t width,
	uint32_t height,
	struct kwl_display_rect *fitted)
{
	uint64_t wide;
	uint64_t tall;

	/* Without a size there are no proportions to keep. */
	fitted->x = 0;
	fitted->y = 0;
	fitted->width = width;
	fitted->height = height;
	if (source_width == 0U || source_height == 0U || width == 0U || height == 0U)
		return;

	/* The image is as wide as the rectangle (bars above and below), or as tall (bars at the sides). */
	wide = (uint64_t)source_width * height;
	tall = (uint64_t)source_height * width;
	if (wide >= tall) {
		fitted->height = (uint32_t)(((uint64_t)source_height * width + source_width / 2U) / source_width);
		if (fitted->height == 0U)
			fitted->height = 1U;
	} else {
		fitted->width = (uint32_t)(((uint64_t)source_width * height + source_height / 2U) / source_height);
		if (fitted->width == 0U)
			fitted->width = 1U;
	}

	/* Centred in the rectangle. */
	fitted->x = (int32_t)((width - fitted->width) / 2U);
	fitted->y = (int32_t)((height - fitted->height) / 2U);
}

/*
 * Chooses the part of an image that covers a rectangle of another size
 * with its proportions kept, centred (the rest of the image is cut off), as
 * the fractions left, top, right and bottom.  A size of zero takes the
 * whole image.
 */
void
kwl_displays_cover(
	uint32_t source_width,
	uint32_t source_height,
	uint32_t width,
	uint32_t height,
	float *uv)
{
	uint64_t wide;
	uint64_t tall;
	float shown;

	/* The whole image, unless the proportions differ. */
	uv[0] = 0.0f;
	uv[1] = 0.0f;
	uv[2] = 1.0f;
	uv[3] = 1.0f;
	if (source_width == 0U || source_height == 0U || width == 0U || height == 0U)
		return;

	/* A wider image loses its sides, a taller one its top and bottom. */
	wide = (uint64_t)source_width * height;
	tall = (uint64_t)source_height * width;
	if (wide > tall) {
		shown = (float)tall / (float)wide;
		uv[0] = (1.0f - shown) / 2.0f;
		uv[2] = 1.0f - uv[0];
	} else if (tall > wide) {
		shown = (float)wide / (float)tall;
		uv[1] = (1.0f - shown) / 2.0f;
		uv[3] = 1.0f - uv[1];
	}
}

/*
 * Checks the places of the extended mode: every display of a size, within
 * KWL_DISPLAYS_LIMIT of the origin, no two overlapping, and all of them
 * joined by the edges they share.  Returns 0, ERANGE for a display out of
 * range or without a size, EINVAL for two that overlap, or ENOTCONN for a
 * display not joined to the others.
 */
int
kwl_displays_validate(
	const struct kwl_display_rect *rects,
	unsigned count)
{
	unsigned reached[KWL_DISPLAYS_PLACES];
	unsigned index;
	unsigned other;
	unsigned found;
	unsigned grew;
	int inside;
	int overlap;
	int touch;

	/* More displays than a choice keeps cannot be checked. */
	if (count > KWL_DISPLAYS_PLACES)
		return ERANGE;

	/* Each display has a size and stays in range. */
	for (index = 0U; index < count; index++) {
		inside = displays_in_range(&rects[index]);
		if (!inside)
			return ERANGE;
	}

	/* No two overlap. */
	for (index = 0U; index < count; index++) {
		for (other = index + 1U; other < count; other++) {
			overlap = displays_overlap(&rects[index], &rects[other]);
			if (overlap)
				return EINVAL;
		}
	}

	/* None (or one) is joined trivially. */
	if (count <= 1U)
		return 0;

	/* From the first display, every display reached through a shared edge, until no more are. */
	memset(reached, 0, sizeof(reached));
	reached[0] = 1U;
	found = 1U;
	grew = 1U;
	while (grew) {
		grew = 0U;
		for (index = 0U; index < count; index++) {
			/* Only a display reached already reaches others. */
			if (!reached[index])
				continue;

			/* The displays it shares an edge with. */
			for (other = 0U; other < count; other++) {
				if (reached[other])
					continue;
				touch = displays_touch(&rects[index], &rects[other]);
				if (!touch)
					continue;
				reached[other] = 1U;
				found++;
				grew = 1U;
			}
		}
	}

	/* A display no edge reaches is not joined. */
	if (found != count)
		return ENOTCONN;

	/* Succeeded: the places are a joined desk of displays. */
	return 0;
}

/*
 * Gives the place of a display connected while the compositor runs: right
 * of the rightmost display, its top at that display's top (D-HOTPLUG).
 * Without a display it is the origin.
 */
void
kwl_displays_place_right(
	const struct kwl_display_rect *rects,
	unsigned count,
	int32_t *x,
	int32_t *y)
{
	int64_t right;
	int64_t edge;
	unsigned index;

	/* The origin, unless a display stands. */
	*x = 0;
	*y = 0;
	if (count == 0U)
		return;

	/* The display whose right edge is farthest right; the first of those that tie. */
	right = (int64_t)rects[0].x + rects[0].width;
	*y = rects[0].y;
	for (index = 1U; index < count; index++) {
		edge = (int64_t)rects[index].x + rects[index].width;
		if (edge > right) {
			right = edge;
			*y = rects[index].y;
		}
	}

	/* Succeeded: the new display starts at that edge. */
	*x = (int32_t)right;
}

/* Makes the choice of no file: every display extended, no anchor, no place. */
void
kwl_displays_config_init(
	struct kwl_display_config *config)
{
	/* All zero is the extended mode with nothing kept. */
	memset(config, 0, sizeof(*config));
	config->mode = KWL_DISPLAYS_EXTENDED;
}

/*
 * Reads the file's text into a choice: its version line first, then the
 * mode, the anchor and the places; lines of other keys, and a line this
 * version does not read, are skipped.  Returns 0, ENOTSUP for a file of
 * another version (the choice is then the one of no file), or EINVAL for a
 * text without a version.
 */
int
kwl_displays_parse(
	const char *text,
	size_t length,
	struct kwl_display_config *config)
{
	char line[DISPLAYS_LINE];
	unsigned version;
	size_t start;
	size_t end;
	size_t size;
	int error;

	/* What no file chooses, until the text says otherwise. */
	kwl_displays_config_init(config);
	version = 0U;

	/* Each line in turn. */
	start = 0U;
	while (start < length) {
		/* The line's end: the newline, or the end of the text. */
		end = start;
		while (end < length && text[end] != '\n')
			end++;

		/* A line too long for this version is skipped; any other is read without its newline and a carriage return. */
		size = end - start;
		if (size > 0U && text[start + size - 1U] == '\r')
			size--;
		if (size < sizeof(line)) {
			memcpy(line, text + start, size);
			line[size] = '\0';
			error = displays_line(line, config, &version);
			if (error != 0) {
				kwl_displays_config_init(config);
				return error;
			}
		}

		/* The next line. */
		start = end + 1U;
	}

	/* A text that never said its version is not the file. */
	if (version == 0U) {
		kwl_displays_config_init(config);
		return EINVAL;
	}

	/* Succeeded: the choice the file keeps. */
	return 0;
}

/*
 * Writes a choice as the file's text.  Returns the text's length, or 0
 * when it does not fit `size` with its terminator.
 */
size_t
kwl_displays_format(
	const struct kwl_display_config *config,
	char *text,
	size_t size)
{
	const char *mode;
	size_t used;
	unsigned index;
	int written;

	/* The version and the mode. */
	mode = "extended";
	if (config->mode == KWL_DISPLAYS_MIRROR)
		mode = "mirror";
	written = snprintf(text, size, "version=%u\nmode=%s\n", KWL_DISPLAYS_VERSION, mode);
	if (written < 0 || (size_t)written >= size)
		return 0U;
	used = (size_t)written;

	/* The anchor, when there is one. */
	if (config->anchor[0] != '\0') {
		written = snprintf(text + used, size - used, "anchor=%s\n", config->anchor);
		if (written < 0 || (size_t)written >= size - used)
			return 0U;
		used += (size_t)written;
	}

	/* Each place. */
	for (index = 0U; index < config->count && index < KWL_DISPLAYS_PLACES; index++) {
		written = snprintf(text + used, size - used, "place=%s %ld %ld\n", config->places[index].key,
		    (long)config->places[index].x, (long)config->places[index].y);
		if (written < 0 || (size_t)written >= size - used)
			return 0U;
		used += (size_t)written;
	}

	/* Succeeded: the text and its length. */
	return used;
}

/*
 * Reads "KEY X Y": a key (which may hold spaces, as a virtual adapter's
 * display names do) and the last two words, signed coordinates within
 * KWL_DISPLAYS_LIMIT.  Returns 0, or EINVAL for text that is not one.
 */
int
kwl_displays_parse_place(
	const char *text,
	char *key,
	size_t size,
	int32_t *x,
	int32_t *y)
{
	const char *last;
	const char *before;
	char *end;
	size_t length;
	int error;

	/* The last space, and the one before it. */
	last = strrchr(text, ' ');
	if (last == NULL || last == text)
		return EINVAL;
	before = last - 1;
	while (before > text && *before != ' ')
		before--;
	if (*before != ' ' || before == text)
		return EINVAL;

	/* The key: everything before the two coordinates. */
	length = (size_t)(before - text);
	if (length == 0U || length >= size)
		return EINVAL;
	memcpy(key, text, length);
	key[length] = '\0';

	/* The two coordinates, and nothing after them. */
	error = displays_number(before + 1, &end, x);
	if (error != 0)
		return error;
	if (end != last)
		return EINVAL;
	error = displays_number(last + 1, &end, y);
	if (error != 0)
		return error;
	if (*end != '\0' && *end != '\n')
		return EINVAL;

	/* Succeeded: the key and its place. */
	return 0;
}

/* Finds a display's place in a choice by its key: its index, or -1. */
int
kwl_displays_find(
	const struct kwl_display_config *config,
	const char *key)
{
	unsigned index;
	int differs;

	/* Each place's key. */
	for (index = 0U; index < config->count && index < KWL_DISPLAYS_PLACES; index++) {
		differs = strcmp(config->places[index].key, key);
		if (differs == 0)
			return (int)index;
	}

	/* Not kept. */
	return -1;
}

/*
 * Keeps a display's place in a choice: the place of its key changed, or a
 * new one.  Returns 0, EINVAL for a key empty or too long, or ENOSPC when
 * the choice keeps as many places as it can.
 */
int
kwl_displays_set(
	struct kwl_display_config *config,
	const char *key,
	int32_t x,
	int32_t y)
{
	size_t length;
	int index;

	/* A key the file can keep. */
	length = strlen(key);
	if (length == 0U || length >= KWL_DISPLAYS_KEY)
		return EINVAL;

	/* The key's place, when it has one. */
	index = kwl_displays_find(config, key);
	if (index >= 0) {
		config->places[index].x = x;
		config->places[index].y = y;
		return 0;
	}

	/* A new place, while there is room. */
	if (config->count >= KWL_DISPLAYS_PLACES)
		return ENOSPC;
	memcpy(config->places[config->count].key, key, length + 1U);
	config->places[config->count].x = x;
	config->places[config->count].y = y;
	config->count++;

	/* Succeeded: the place is kept. */
	return 0;
}

/* Tells whether two rectangles share some area (the half-open edges do not). */
static int
displays_overlap(
	const struct kwl_display_rect *one,
	const struct kwl_display_rect *other)
{
	int meet_x;
	int meet_y;

	/* Both their columns and their rows must meet. */
	meet_x = displays_ranges_meet(one->x, (int64_t)one->x + one->width, other->x, (int64_t)other->x + other->width);
	if (!meet_x)
		return 0;
	meet_y = displays_ranges_meet(one->y, (int64_t)one->y + one->height, other->y, (int64_t)other->y + other->height);
	if (!meet_y)
		return 0;

	/* They overlap. */
	return 1;
}

/* Tells whether two rectangles share a length of edge (a corner alone does not join them). */
static int
displays_touch(
	const struct kwl_display_rect *one,
	const struct kwl_display_rect *other)
{
	int64_t one_right;
	int64_t one_bottom;
	int64_t other_right;
	int64_t other_bottom;
	int meet;

	/* The far edges. */
	one_right = (int64_t)one->x + one->width;
	one_bottom = (int64_t)one->y + one->height;
	other_right = (int64_t)other->x + other->width;
	other_bottom = (int64_t)other->y + other->height;

	/* Side by side: a vertical edge in common and rows that meet. */
	if (one_right == other->x || other_right == one->x) {
		meet = displays_ranges_meet(one->y, one_bottom, other->y, other_bottom);
		if (meet)
			return 1;
	}

	/* One above the other: a horizontal edge in common and columns that meet. */
	if (one_bottom == other->y || other_bottom == one->y) {
		meet = displays_ranges_meet(one->x, one_right, other->x, other_right);
		if (meet)
			return 1;
	}

	/* They do not share an edge. */
	return 0;
}

/* Tells whether two half-open ranges have a length in common. */
static int
displays_ranges_meet(
	int64_t start,
	int64_t end,
	int64_t other_start,
	int64_t other_end)
{
	/* One ends before the other starts. */
	if (end <= other_start)
		return 0;
	if (other_end <= start)
		return 0;

	/* They meet. */
	return 1;
}

/* Tells whether a display has a size and stays within KWL_DISPLAYS_LIMIT of the origin. */
static int
displays_in_range(
	const struct kwl_display_rect *rect)
{
	/* A display has a size, no larger than the limit. */
	if (rect->width == 0U || rect->height == 0U)
		return 0;
	if (rect->width > (uint32_t)KWL_DISPLAYS_LIMIT || rect->height > (uint32_t)KWL_DISPLAYS_LIMIT)
		return 0;

	/* Its origin is within the limit either way. */
	if (rect->x < -KWL_DISPLAYS_LIMIT || rect->x > KWL_DISPLAYS_LIMIT)
		return 0;
	if (rect->y < -KWL_DISPLAYS_LIMIT || rect->y > KWL_DISPLAYS_LIMIT)
		return 0;

	/* It is in range. */
	return 1;
}

/*
 * Reads one line of the file into the choice.  Returns 0 (also for a line
 * skipped), or ENOTSUP for a version this compositor does not read.
 */
static int
displays_line(
	char *line,
	struct kwl_display_config *config,
	unsigned *version)
{
	const char *value;
	char *equals;
	char *end;
	unsigned long number;
	size_t length;
	int differs;

	/* A line without a key and its value is skipped. */
	equals = strchr(line, '=');
	if (equals == NULL)
		return 0;
	*equals = '\0';
	value = equals + 1;

	/* The version: one this compositor reads, or the whole file is not read. */
	differs = strcmp(line, "version");
	if (differs == 0) {
		number = strtoul(value, &end, 10);
		if (end == value ||
		    *end != '\0' ||
		    number != KWL_DISPLAYS_VERSION)
			return ENOTSUP;
		*version = (unsigned)number;
		return 0;
	}

	/* The mode. */
	differs = strcmp(line, "mode");
	if (differs == 0) {
		differs = strcmp(value, "mirror");
		if (differs == 0) {
			config->mode = KWL_DISPLAYS_MIRROR;
		} else {
			config->mode = KWL_DISPLAYS_EXTENDED;
		}

		/* The mode is read. */
		return 0;
	}

	/* The anchor, when it fits. */
	differs = strcmp(line, "anchor");
	if (differs == 0) {
		length = strlen(value);
		if (length < KWL_DISPLAYS_KEY)
			memcpy(config->anchor, value, length + 1U);
		return 0;
	}

	/* A place. */
	differs = strcmp(line, "place");
	if (differs == 0) {
		/* A place the line does not give is skipped. */
		(void)displays_place_line(value, config);
		return 0;
	}

	/* Succeeded: a key of a later version, skipped. */
	return 0;
}

/*
 * Reads a place's value "KEY X Y" into the choice (the key may hold
 * spaces: the coordinates are the last two words).  Returns 0, or EINVAL
 * for a value that is not one (the line is skipped).
 */
static int
displays_place_line(
	const char *value,
	struct kwl_display_config *config)
{
	char key[KWL_DISPLAYS_KEY];
	int32_t x;
	int32_t y;
	int error;

	/* The key and the two coordinates. */
	error = kwl_displays_parse_place(value, key, sizeof(key), &x, &y);
	if (error != 0)
		return error;

	/* The place kept (a choice that keeps as many as it can drops the rest). */
	error = kwl_displays_set(config, key, x, y);
	if (error != 0)
		return error;

	/* Succeeded: the place is read. */
	return 0;
}

/* Reads a signed coordinate within KWL_DISPLAYS_LIMIT; returns 0 or EINVAL. */
static int
displays_number(
	const char *text,
	char **end,
	int32_t *number)
{
	long value;

	/* Digits with an optional sign. */
	errno = 0;
	value = strtol(text, end, 10);
	if (*end == text || errno != 0)
		return EINVAL;

	/* Within the limit. */
	if (value < -(long)KWL_DISPLAYS_LIMIT || value > (long)KWL_DISPLAYS_LIMIT)
		return EINVAL;

	/* Succeeded: the coordinate. */
	*number = (int32_t)value;
	return 0;
}
