/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The fonts of a page: opening them, picking one for a style, and the
 * glyph cache that keeps every glyph's advance and coverage bitmap once it
 * has been measured or drawn.
 */

#include "text/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <truetype.h>

/* The smallest and largest pixel size a font is drawn at. */
#define FONT_PIXELS_MIN		1U
#define FONT_PIXELS_MAX		400U

/* The weight from which a font is drawn bold. */
#define FONT_BOLD_WEIGHT	600

/* The size of the glyph cache's table when the first glyph arrives. */
#define FONT_CACHE_MIN		1024U

/*
 * One cached glyph: its key (face, bold, size and code point), what the
 * layout needs, and the bitmap once it is drawn.
 */
struct text_glyph_entry {
	uint64_t key;
	struct text_glyph glyph;
	uint8_t *pixels;
	int drawn;
};

static uint64_t font_key(const struct text_font *font, uint32_t code_point);
static struct text_glyph_entry **font_slot(struct text_system *system, uint64_t key);
static int font_grow(struct text_system *system);
static int font_measure(struct text_system *system, const struct text_font *font, uint32_t code_point, struct text_glyph_entry *entry);
static int font_draw(struct text_system *system, const struct text_font *font, struct text_glyph_entry *entry);
static int font_open_face(struct text_face *face, const char *path);

/*
 * Opens the fonts of a text system.  The sans font is required; the
 * monospace and the fallback fonts are used when they open.
 */
int
text_system_open(
	struct text_system *system,
	const struct text_font_paths *paths)
{
	int error;

	/* Starts with no faces and an empty cache. */
	memset(system, 0, sizeof(*system));

	/* The sans font must open. */
	error = font_open_face(&system->faces[TEXT_FACE_SANS], paths->sans);
	if (error != 0)
		return error;

	/* The others are optional. */
	if (paths->mono != NULL)
		font_open_face(&system->faces[TEXT_FACE_MONO], paths->mono);
	if (paths->fallback != NULL)
		font_open_face(&system->faces[TEXT_FACE_FALLBACK], paths->fallback);

	/* Succeeded: the fonts are open. */
	return 0;
}

/*
 * Closes the fonts and frees the glyph cache.
 */
void
text_system_close(
	struct text_system *system)
{
	size_t index;
	int face;

	/* Frees every cached glyph. */
	for (index = 0; index < system->cache_capacity; index++) {
		if (system->cache[index] == NULL)
			continue;

		/* The bitmap and the entry. */
		free(system->cache[index]->pixels);
		free(system->cache[index]);
	}

	/* Then the table. */
	free(system->cache);

	/* Closes the faces and frees their files. */
	for (face = 0; face < TEXT_FACES; face++) {
		if (system->faces[face].open)
			truetype_close(system->faces[face].face);
		wb_buffer_release(&system->faces[face].data);
	}

	/* Nothing is left open. */
	memset(system, 0, sizeof(*system));
}

/*
 * Picks the font for a style: the face of its family, its size rounded to
 * whole pixels and whether it is bold.
 */
void
text_select_font(
	const struct text_system *system,
	int monospace,
	float size,
	int weight,
	struct text_font *font)
{
	float rounded;

	/* Monospace text uses the monospace face when it opened. */
	font->face = TEXT_FACE_SANS;
	if (monospace && system->faces[TEXT_FACE_MONO].open)
		font->face = TEXT_FACE_MONO;

	/* The size in whole pixels, within what the rasterizer takes. */
	rounded = size + 0.5f;
	if (rounded < (float)FONT_PIXELS_MIN)
		rounded = (float)FONT_PIXELS_MIN;
	if (rounded > (float)FONT_PIXELS_MAX)
		rounded = (float)FONT_PIXELS_MAX;
	font->pixels = (unsigned)rounded;

	/* Heavy weights are drawn bold. */
	font->bold = 0;
	if (weight >= FONT_BOLD_WEIGHT)
		font->bold = 1;
}

/*
 * Reports a font's ascent, descent and line height in pixels.
 */
int
text_font_metrics(
	struct text_system *system,
	const struct text_font *font,
	struct text_metrics *metrics)
{
	struct truetype_metrics measured;
	struct text_face *face;
	int error;

	/* Measures the face at the size. */
	face = &system->faces[font->face];
	error = truetype_set_pixel_size(face->face, font->pixels);
	if (error != 0)
		return error;
	error = truetype_metrics(face->face, &measured);
	if (error != 0)
		return error;

	/* Reports the measures (the descent as a positive distance below the baseline). */
	metrics->ascent = measured.ascent;
	metrics->descent = measured.descent;
	if (metrics->descent < 0)
		metrics->descent = -metrics->descent;
	metrics->line_height = measured.line_height;
	if (metrics->line_height < metrics->ascent + metrics->descent)
		metrics->line_height = metrics->ascent + metrics->descent;

	/* Succeeded: the measures are filled. */
	return 0;
}

/*
 * Finds the glyph of a code point in a font (or, when the font lacks it,
 * in the fallback), with its advance and, when asked, its bitmap.
 *
 * The glyph stays valid until the text system is closed.
 */
int
text_glyph(
	struct text_system *system,
	const struct text_font *font,
	uint32_t code_point,
	int with_bitmap,
	struct text_glyph *glyph)
{
	struct text_glyph_entry **slot;
	struct text_glyph_entry *entry;
	uint64_t key;
	int error;

	/* Grows the cache when adding would fill more than half of it. */
	if ((system->cache_count + 1U) * 2U > system->cache_capacity) {
		error = font_grow(system);
		if (error != 0)
			return error;
	}

	/* Finds the entry, or measures the glyph into a new one. */
	key = font_key(font, code_point);
	slot = font_slot(system, key);
	entry = *slot;
	if (entry == NULL) {
		entry = calloc(1, sizeof(*entry));
		if (entry == NULL)
			return ENOMEM;
		entry->key = key;
		error = font_measure(system, font, code_point, entry);
		if (error != 0) {
			free(entry);
			return error;
		}

		/* Keeps the entry. */
		*slot = entry;
		system->cache_count++;
	}

	/* Draws the bitmap the first time it is asked for. */
	if (with_bitmap && !entry->drawn) {
		error = font_draw(system, font, entry);
		if (error != 0)
			return error;
	}

	/* Succeeded: the glyph is the cache's. */
	*glyph = entry->glyph;
	return 0;
}

/* Packs a font and a code point into a cache key. */
static uint64_t
font_key(
	const struct text_font *font,
	uint32_t code_point)
{
	uint64_t key;

	/* The code point, the size, the bold flag and the face, in separate bits. */
	key = code_point;
	key |= (uint64_t)font->pixels << 24;
	key |= (uint64_t)(font->bold & 1) << 40;
	key |= (uint64_t)font->face << 41;

	/* Reports the key. */
	return key;
}

/* Finds the cache slot of a key: the one holding it, or the empty one where it goes. */
static struct text_glyph_entry **
font_slot(
	struct text_system *system,
	uint64_t key)
{
	size_t slot;

	/* Probes linearly from the key's hash. */
	slot = (size_t)((key * 0x9e3779b97f4a7c15ULL) >> 20) & (system->cache_capacity - 1U);
	while (system->cache[slot] != NULL) {
		if (system->cache[slot]->key == key)
			return &system->cache[slot];

		/* The next slot of the probe. */
		slot = (slot + 1U) & (system->cache_capacity - 1U);
	}

	/* The empty slot where the key goes. */
	return &system->cache[slot];
}

/* Doubles the cache's table and moves every entry. */
static int
font_grow(
	struct text_system *system)
{
	struct text_glyph_entry **old;
	struct text_glyph_entry **slot;
	size_t old_capacity;
	size_t index;

	/* Takes the larger, empty table. */
	old = system->cache;
	old_capacity = system->cache_capacity;
	system->cache_capacity = old_capacity * 2U;
	if (system->cache_capacity < FONT_CACHE_MIN)
		system->cache_capacity = FONT_CACHE_MIN;
	system->cache = calloc(system->cache_capacity, sizeof(*system->cache));
	if (system->cache == NULL) {
		system->cache = old;
		system->cache_capacity = old_capacity;
		return ENOMEM;
	}

	/* Moves the entries. */
	for (index = 0; index < old_capacity; index++) {
		if (old[index] == NULL)
			continue;

		/* Into the slot its key probes to. */
		slot = font_slot(system, old[index]->key);
		*slot = old[index];
	}

	/* The old table is no longer needed. */
	free(old);

	/* Succeeded: the table has room. */
	return 0;
}

/* Finds the face and index of a code point's glyph and measures its advance and box. */
static int
font_measure(
	struct text_system *system,
	const struct text_font *font,
	uint32_t code_point,
	struct text_glyph_entry *entry)
{
	struct truetype_glyph measured;
	struct text_face *face;
	unsigned index;
	int face_index;
	int error;

	/* The font's own face, or the fallback when it lacks the glyph. */
	face_index = font->face;
	index = truetype_glyph_index(system->faces[face_index].face, code_point);
	if (index == 0 && system->faces[TEXT_FACE_FALLBACK].open) {
		index = truetype_glyph_index(system->faces[TEXT_FACE_FALLBACK].face, code_point);
		if (index != 0)
			face_index = TEXT_FACE_FALLBACK;
	}

	/* Measures the glyph at the size (the missing glyph when neither face has it). */
	face = &system->faces[face_index];
	error = truetype_set_pixel_size(face->face, font->pixels);
	if (error != 0)
		return error;
	error = truetype_glyph_metrics(face->face, index, &measured);
	if (error != 0)
		return error;

	/* Records it; a bold glyph is a pixel wider. */
	entry->glyph.face = face_index;
	entry->glyph.index = index;
	entry->glyph.advance = measured.advance + font->bold;
	entry->glyph.width = (int)measured.width;
	entry->glyph.height = (int)measured.height;
	entry->glyph.left = measured.left;
	entry->glyph.top = measured.top;
	entry->glyph.bitmap = NULL;

	/* Succeeded: the entry is measured. */
	return 0;
}

/* Draws a measured glyph's coverage bitmap, widened by a pixel when bold. */
static int
font_draw(
	struct text_system *system,
	const struct text_font *font,
	struct text_glyph_entry *entry)
{
	struct truetype_glyph measured;
	struct text_face *face;
	uint8_t *pixels;
	size_t size;
	int width;
	int height;
	int x;
	int y;
	int error;

	/* An empty glyph (a space) has no pixels. */
	entry->drawn = 1;
	width = entry->glyph.width;
	height = entry->glyph.height;
	if (width <= 0 || height <= 0)
		return 0;

	/* Draws the glyph into a buffer one pixel wider than it, for the bold widening. */
	size = (size_t)(width + 1) * (size_t)height;
	pixels = calloc(size, 1);
	if (pixels == NULL)
		return ENOMEM;
	face = &system->faces[entry->glyph.face];
	error = truetype_set_pixel_size(face->face, font->pixels);
	if (error == 0)
		error = truetype_render_glyph(face->face, entry->glyph.index, &measured, pixels, (size_t)(width + 1), size);
	if (error != 0) {
		free(pixels);
		return error;
	}

	/* Bold widens every row by a pixel: each pixel is the stronger of itself and its left neighbour. */
	if (font->bold) {
		for (y = 0; y < height; y++) {
			for (x = width; x > 0; x--) {
				if (pixels[(size_t)y * (size_t)(width + 1) + (size_t)(x - 1)] > pixels[(size_t)y * (size_t)(width + 1) + (size_t)x])
					pixels[(size_t)y * (size_t)(width + 1) + (size_t)x] = pixels[(size_t)y * (size_t)(width + 1) + (size_t)(x - 1)];
			}
		}
	}

	/* Publishes the bitmap: the stride is the width plus one, which the glyph's width now is. */
	entry->pixels = pixels;
	entry->glyph.width = width + 1;
	entry->glyph.bitmap = pixels;

	/* Succeeded: the glyph has its bitmap. */
	return 0;
}

/* Reads a font file and opens its first face. */
static int
font_open_face(
	struct text_face *face,
	const char *path)
{
	int error;

	/* Reads the file. */
	wb_buffer_init(&face->data);
	error = wb_file_read(path, &face->data);
	if (error != 0) {
		wb_buffer_release(&face->data);
		return error;
	}

	/* Opens the face; the data stays with it. */
	error = truetype_open(face->data.data, face->data.length, 0, &face->face);
	if (error != 0) {
		wb_buffer_release(&face->data);
		return error;
	}

	/* Succeeded: the face is open. */
	face->open = 1;
	return 0;
}
