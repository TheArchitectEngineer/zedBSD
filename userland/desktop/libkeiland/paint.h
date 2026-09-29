/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The library's own drawing, for the parts it shows in windows of its own
 * (the file chooser, ws092-p003): a CPU canvas of premultiplied
 * 0xAARRGGBB words and text from TrueType fonts through libtruetype.
 *
 * None of this leaves the library (exports.map); it is the start of the
 * shared widgets of WS090.  The canvas follows Text Editor's canvas.c and
 * the text Files' text.c, whose looks the chooser matches.
 */

#ifndef KEILAND_PAINT_H
#define KEILAND_PAINT_H

#include <stddef.h>
#include <stdint.h>

/* A font as libtruetype holds it. */
struct truetype_face;

/* A colour given as 0xRRGGBB, opaque, or with an alpha of 0 to 255. */
#define KL_RGB(rgb)		(0xff000000U | (uint32_t)(rgb))
#define KL_RGBA(rgb, alpha)	(((uint32_t)(alpha) << 24) | (uint32_t)(rgb))

/* How many faces a text has: the main font and a fallback. */
#define KL_TEXT_FACES		2

/*
 * A frame being drawn: premultiplied 0xAARRGGBB words, stride words a row,
 * and the rectangle drawing is clipped to (the whole canvas unless
 * kl_paint_clip narrows it).
 */
struct kl_canvas {
	uint32_t *pixels;
	size_t stride;
	int width;
	int height;
	int clip_x;
	int clip_y;
	int clip_width;
	int clip_height;
};

/*
 * One glyph drawn at a size: its cache key (0 for an empty slot), its
 * coverage bitmap (width bytes a row; NULL for a blank glyph), where it
 * sits from the pen and the baseline, and how far it moves the pen.
 */
struct kl_glyph {
	uint32_t key;
	int width;
	int height;
	int left;
	int top;
	int advance;
	uint8_t *bitmap;
};

/* One font file: its bytes (kept for the face), the face, and the size set last. */
struct kl_text_face {
	void *data;
	size_t size;
	struct truetype_face *face;
	unsigned pixels;
};

/*
 * The fonts of one kind of text and every glyph drawn so far.  It lives as
 * long as the window that draws with it; the cache is emptied when it
 * fills up.
 */
struct kl_text {
	struct kl_text_face faces[KL_TEXT_FACES];
	int face_count;
	struct kl_glyph *cache;
	unsigned cache_size;
	unsigned cache_used;
	uint8_t *scratch;
	size_t scratch_size;
};

/* The canvas (paint.c). */
void kl_paint_init(struct kl_canvas *canvas, uint32_t *pixels, int width, int height, size_t stride);
void kl_paint_clip(struct kl_canvas *canvas, int x, int y, int width, int height);
void kl_paint_unclip(struct kl_canvas *canvas);
void kl_paint_fill(struct kl_canvas *canvas, int x, int y, int width, int height, uint32_t color);
void kl_paint_gradient(struct kl_canvas *canvas, int x, int y, int width, int height, uint32_t top, uint32_t bottom);
void kl_paint_round(struct kl_canvas *canvas, int x, int y, int width, int height, int radius, uint32_t color);
void kl_paint_round_border(struct kl_canvas *canvas, int x, int y, int width, int height, int radius, uint32_t color);
void kl_paint_line(struct kl_canvas *canvas, double x0, double y0, double x1, double y1, double thickness, uint32_t color);
void kl_paint_circle(struct kl_canvas *canvas, double x, double y, double radius, uint32_t color);
void kl_paint_ring(struct kl_canvas *canvas, double x, double y, double radius, double thickness, uint32_t color);
void kl_paint_mask(struct kl_canvas *canvas, int x, int y, const unsigned char *mask, int width, int height, size_t stride, uint32_t color);

/* The text (paint-text.c). */
int kl_text_open(struct kl_text *text, const char *primary, const char *fallback);
void kl_text_close(struct kl_text *text);
int kl_text_center(unsigned pixels, int top, int height);
int kl_text_width(struct kl_text *text, const char *string, size_t length, unsigned pixels, int bold);
int kl_text_draw(struct kl_text *text, struct kl_canvas *canvas, int x, int baseline, const char *string, size_t length, unsigned pixels, int bold, uint32_t color);
int kl_text_draw_fit(struct kl_text *text, struct kl_canvas *canvas, int x, int baseline, const char *string, unsigned pixels, int bold, int width, uint32_t color);
size_t kl_text_fit(struct kl_text *text, const char *string, unsigned pixels, int bold, int width, char *out, size_t size);
uint32_t kl_utf8_next(const char *string, size_t length, size_t *index);

#endif
