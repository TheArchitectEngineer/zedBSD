/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The drawing surface of zdesktop-files: a CPU canvas of premultiplied
 * BGRA pixels, the text drawn on it and the icons made of its shapes.
 *
 * Nothing here knows about Wayland or Vulkan.  The window's frame is drawn
 * into a canvas and handed to the presenter, and the host tests draw the
 * same frames into a canvas of their own and write it out as a picture.
 */

#ifndef ZDESKTOP_FILES_CANVAS_H
#define ZDESKTOP_FILES_CANVAS_H

#include <stddef.h>
#include <stdint.h>

/* How deep the clip rectangles nest. */
#define FM_CANVAS_CLIPS		16

/* The most corners a polygon may have. */
#define FM_POLYGON_POINTS	96

/* How many fonts the text draws from: the main one and a fallback. */
#define FM_TEXT_FACES		2

/* A color as 0xAARRGGBB, not premultiplied. */
typedef uint32_t fm_color;

/* An opaque color from 0xRRGGBB. */
#define FM_RGB(value)		((fm_color)(0xff000000U | (uint32_t)(value)))

/* A color from 0xRRGGBB and an alpha from 0 to 255. */
#define FM_RGBA(value, alpha)	((fm_color)(((uint32_t)(alpha) << 24) | ((uint32_t)(value) & 0xffffffU)))

/*
 * A rectangle of whole pixels.
 *
 * It is a plain value: a layout computes one, the drawing and the hit test
 * both read it.
 */
struct fm_rect {
	int x;
	int y;
	int width;
	int height;
};

/*
 * A picture in memory, premultiplied BGRA (0xAARRGGBB words).
 *
 * The pixels belong to whoever made the image: fm_image_create allocates
 * them and fm_image_release frees them.
 */
struct fm_image {
	uint32_t *pixels;
	int width;
	int height;
	size_t stride;
};

/*
 * A surface to draw on.
 *
 * The pixels are the caller's; the canvas adds the clip rectangles and the
 * scratch row the polygon filler accumulates coverage in, which live as
 * long as the canvas.
 */
struct fm_canvas {
	/* The pixels, the words in a row and the size. */
	uint32_t *pixels;
	size_t stride;
	int width;
	int height;

	/* The clip in force, and the ones it replaced (innermost last). */
	struct fm_rect clip;
	struct fm_rect clips[FM_CANVAS_CLIPS];
	int clip_depth;

	/* One row of polygon coverage, a float per pixel and one more. */
	float *coverage;
};

/*
 * One glyph drawn at one size, kept for the next time.
 *
 * key is zero for an empty slot; the bitmap is the glyph's coverage.
 */
struct fm_glyph {
	uint32_t key;
	int width;
	int height;
	int left;
	int top;
	int advance;
	uint8_t *bitmap;
};

/*
 * One font file: its bytes (kept for the face) and the face.
 */
struct fm_text_face {
	void *data;
	size_t size;
	struct truetype_face *face;
	unsigned pixels;
};

/*
 * The text of the window: the fonts and every glyph drawn so far.
 *
 * One lives for the whole program.  The cache is emptied when it fills up,
 * which only costs drawing the glyphs again.
 */
struct fm_text {
	struct fm_text_face faces[FM_TEXT_FACES];
	int face_count;
	struct fm_glyph *cache;
	unsigned cache_size;
	unsigned cache_used;
	uint8_t *scratch;
	size_t scratch_size;
};

/*
 * The vertical measurements of text at one size, in pixels.
 */
struct fm_text_line {
	int ascent;
	int descent;
	int height;
};

/*
 * The icons drawn with lines (sidebar, toolbar) or filled shapes (items).
 */
enum fm_icon {
	FM_ICON_HOME,
	FM_ICON_DESKTOP,
	FM_ICON_DOCUMENTS,
	FM_ICON_DOWNLOADS,
	FM_ICON_PICTURES,
	FM_ICON_MUSIC,
	FM_ICON_MOVIES,
	FM_ICON_FOLDER_LINE,
	FM_ICON_RECENTS,
	FM_ICON_TRASH,
	FM_ICON_COMPUTER,
	FM_ICON_VOLUME,
	FM_ICON_BACK,
	FM_ICON_FORWARD,
	FM_ICON_SEARCH,
	FM_ICON_GRID,
	FM_ICON_LIST,
	FM_ICON_PREVIEW,
	FM_ICON_CHEVRON,
	FM_ICON_CLOSE,
	FM_ICON_PLUS,
	FM_ICON_UP,
	FM_ICON_DOWN
};

/* The canvas (canvas.c). */
int fm_canvas_init(struct fm_canvas *canvas, uint32_t *pixels, size_t stride, int width, int height);
void fm_canvas_release(struct fm_canvas *canvas);
void fm_canvas_clip_push(struct fm_canvas *canvas, const struct fm_rect *rect);
void fm_canvas_clip_pop(struct fm_canvas *canvas);
void fm_canvas_fill(struct fm_canvas *canvas, const struct fm_rect *rect, fm_color color);
void fm_canvas_gradient(struct fm_canvas *canvas, const struct fm_rect *rect, fm_color top, fm_color bottom);
void fm_canvas_round(struct fm_canvas *canvas, float x, float y, float width, float height, float radius, fm_color color);
void fm_canvas_round_gradient(struct fm_canvas *canvas, float x, float y, float width, float height, float radius, fm_color top, fm_color bottom);
void fm_canvas_round_border(struct fm_canvas *canvas, float x, float y, float width, float height, float radius, float thickness, fm_color color);
void fm_canvas_shadow(struct fm_canvas *canvas, float x, float y, float width, float height, float radius, float softness, fm_color color);
void fm_canvas_circle(struct fm_canvas *canvas, float cx, float cy, float radius, fm_color color);
void fm_canvas_ring(struct fm_canvas *canvas, float cx, float cy, float radius, float thickness, float fraction, fm_color color);
void fm_canvas_polygon(struct fm_canvas *canvas, const float *points, int count, fm_color color);
void fm_canvas_line(struct fm_canvas *canvas, float x0, float y0, float x1, float y1, float thickness, fm_color color);
void fm_canvas_mask(struct fm_canvas *canvas, int x, int y, const uint8_t *mask, int width, int height, size_t stride, fm_color color);
void fm_canvas_image(struct fm_canvas *canvas, const struct fm_image *image, float x, float y, float width, float height, float radius, float opacity);
int fm_image_create(struct fm_image *image, int width, int height);
void fm_image_release(struct fm_image *image);
void fm_image_scale(const struct fm_image *source, struct fm_image *target);
fm_color fm_color_mix(fm_color from, fm_color to, float amount);

/* The text (text.c). */
int fm_text_open(struct fm_text *text, const char *primary, const char *fallback);
void fm_text_close(struct fm_text *text);
void fm_text_metrics(struct fm_text *text, unsigned pixels, struct fm_text_line *line);
int fm_text_center(unsigned pixels, int top, int height);
int fm_text_width(struct fm_text *text, const char *string, size_t length, unsigned pixels, int bold);
int fm_text_draw(struct fm_text *text, struct fm_canvas *canvas, int x, int baseline, const char *string, size_t length, unsigned pixels, int bold, fm_color color);
int fm_text_draw_fit(struct fm_text *text, struct fm_canvas *canvas, int x, int baseline, const char *string, unsigned pixels, int bold, int width, fm_color color);
size_t fm_text_fit(struct fm_text *text, const char *string, unsigned pixels, int bold, int width, char *out, size_t size);
size_t fm_text_break(struct fm_text *text, const char *string, unsigned pixels, int bold, int width);
uint32_t fm_utf8_next(const char *string, size_t length, size_t *index);

/* The icons (icons.c). */
void fm_icon_draw(struct fm_canvas *canvas, enum fm_icon icon, float x, float y, float size, fm_color color);
void fm_icon_folder(struct fm_canvas *canvas, float x, float y, float size, fm_color tint);
void fm_icon_file(struct fm_canvas *canvas, struct fm_text *text, float x, float y, float size, fm_color band, const char *label);
void fm_icon_tag(struct fm_canvas *canvas, float cx, float cy, float radius, fm_color color);

#endif
