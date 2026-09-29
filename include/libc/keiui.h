/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's shared widgets and controls (WS090, plan/ws090/design.md):
 * one library the desktop's applications draw their parts with, so that a
 * button, a list or a scroll looks and feels the same in every one of them.
 *
 * The first layer (KUI_VERSION 1) is the drawing: a CPU canvas of
 * premultiplied BGRA pixels, the text drawn on it from TrueType fonts, the
 * icons made of its shapes, and the theme -- the colours and sizes of the
 * Kei look.  It began as the file manager's drawing surface (Files'
 * canvas.c, text.c and icons.c) and Settings' line pictures, moved here
 * unchanged so that an application moved onto the library draws the same
 * pixels as before.  Later versions add the input, the scroll view, the
 * widgets and the window.
 *
 * Nothing in this layer knows about Wayland or Vulkan.  A window's frame
 * is drawn into a canvas and handed to its presenter, and host tests draw
 * into a canvas of their own and write it out as a picture.  Every call
 * is made from one thread.  The library's name is internal and never
 * appears in what a user reads.
 */

#ifndef KEIUI_H
#define KEIUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes (1: the drawing -- canvas, text, icons and the theme). */
#define KUI_VERSION	1U

/*
 * Reports the interface version of the library that was loaded.
 *
 * A program built against this header may compare the result with
 * KUI_VERSION to learn whether the library it runs with is older.
 */
unsigned kui_version(void);

/* How deep the clip rectangles nest. */
#define KUI_CANVAS_CLIPS		16

/* The most corners a polygon may have. */
#define KUI_POLYGON_POINTS	96

/* How many fonts the text draws from: the main one and a fallback. */
#define KUI_TEXT_FACES		2

/* A color as 0xAARRGGBB, not premultiplied. */
typedef uint32_t kui_color;

/* An opaque color from 0xRRGGBB. */
#define KUI_RGB(value)		((kui_color)(0xff000000U | (uint32_t)(value)))

/* A color from 0xRRGGBB and an alpha from 0 to 255. */
#define KUI_RGBA(value, alpha)	((kui_color)(((uint32_t)(alpha) << 24) | ((uint32_t)(value) & 0xffffffU)))

/*
 * A rectangle of whole pixels.
 *
 * It is a plain value: a layout computes one, the drawing and the hit test
 * both read it.
 */
struct kui_rect {
	int x;
	int y;
	int width;
	int height;
};

/*
 * A picture in memory, premultiplied BGRA (0xAARRGGBB words).
 *
 * The pixels belong to whoever made the image: kui_image_create allocates
 * them and kui_image_release frees them.
 */
struct kui_image {
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
struct kui_canvas {
	/* The pixels, the words in a row and the size. */
	uint32_t *pixels;
	size_t stride;
	int width;
	int height;

	/* The clip in force, and the ones it replaced (innermost last). */
	struct kui_rect clip;
	struct kui_rect clips[KUI_CANVAS_CLIPS];
	int clip_depth;

	/* One row of polygon coverage, a float per pixel and one more. */
	float *coverage;
};

/*
 * One glyph drawn at one size, kept for the next time.
 *
 * key is zero for an empty slot; the bitmap is the glyph's coverage.
 */
struct kui_glyph {
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
struct kui_text_face {
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
struct kui_text {
	struct kui_text_face faces[KUI_TEXT_FACES];
	int face_count;
	struct kui_glyph *cache;
	unsigned cache_size;
	unsigned cache_used;
	uint8_t *scratch;
	size_t scratch_size;
};

/*
 * The vertical measurements of text at one size, in pixels.
 */
struct kui_text_line {
	int ascent;
	int descent;
	int height;
};

/*
 * The icons drawn with lines (sidebar, toolbar) or filled shapes (items).
 */
enum kui_icon {
	KUI_ICON_HOME,
	KUI_ICON_DESKTOP,
	KUI_ICON_DOCUMENTS,
	KUI_ICON_DOWNLOADS,
	KUI_ICON_PICTURES,
	KUI_ICON_MUSIC,
	KUI_ICON_MOVIES,
	KUI_ICON_FOLDER_LINE,
	KUI_ICON_RECENTS,
	KUI_ICON_TRASH,
	KUI_ICON_COMPUTER,
	KUI_ICON_VOLUME,
	KUI_ICON_BACK,
	KUI_ICON_FORWARD,
	KUI_ICON_SEARCH,
	KUI_ICON_GRID,
	KUI_ICON_LIST,
	KUI_ICON_PREVIEW,
	KUI_ICON_CHEVRON,
	KUI_ICON_CLOSE,
	KUI_ICON_PLUS,
	KUI_ICON_UP,
	KUI_ICON_DOWN,

	/* The line pictures (Settings' pages), from KUI_ICON_TILES on, in the order Settings numbered them. */
	KUI_ICON_TILES,
	KUI_ICON_WIFI,
	KUI_ICON_ETHERNET,
	KUI_ICON_BLUETOOTH,
	KUI_ICON_SHIELD,
	KUI_ICON_GLOBE,
	KUI_ICON_PALETTE,
	KUI_ICON_PICTURE,
	KUI_ICON_BELL,
	KUI_ICON_SPEAKER,
	KUI_ICON_MONITOR,
	KUI_ICON_DISK,
	KUI_ICON_BATTERY,
	KUI_ICON_KEYBOARD,
	KUI_ICON_MOUSE,
	KUI_ICON_TOUCHPAD,
	KUI_ICON_PRINTER,
	KUI_ICON_SHARE,
	KUI_ICON_PEOPLE,
	KUI_ICON_EYE,
	KUI_ICON_LOCK,
	KUI_ICON_PERSON,
	KUI_ICON_REFRESH,
	KUI_ICON_INFO,
	KUI_ICON_DISCLOSURE
};

/* The canvas (canvas.c). */
int kui_canvas_init(struct kui_canvas *canvas, uint32_t *pixels, size_t stride, int width, int height);
void kui_canvas_release(struct kui_canvas *canvas);
void kui_canvas_clip_push(struct kui_canvas *canvas, const struct kui_rect *rect);
void kui_canvas_clip_pop(struct kui_canvas *canvas);
void kui_canvas_clear(struct kui_canvas *canvas);
void kui_canvas_fill(struct kui_canvas *canvas, const struct kui_rect *rect, kui_color color);
void kui_canvas_gradient(struct kui_canvas *canvas, const struct kui_rect *rect, kui_color top, kui_color bottom);
void kui_canvas_round(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, kui_color color);
void kui_canvas_round_gradient(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, kui_color top, kui_color bottom);
void kui_canvas_round_border(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, float thickness, kui_color color);
void kui_canvas_shadow(struct kui_canvas *canvas, float x, float y, float width, float height, float radius, float softness, kui_color color);
void kui_canvas_circle(struct kui_canvas *canvas, float cx, float cy, float radius, kui_color color);
void kui_canvas_ring(struct kui_canvas *canvas, float cx, float cy, float radius, float thickness, float fraction, kui_color color);
void kui_canvas_polygon(struct kui_canvas *canvas, const float *points, int count, kui_color color);
void kui_canvas_line(struct kui_canvas *canvas, float x0, float y0, float x1, float y1, float thickness, kui_color color);
void kui_canvas_mask(struct kui_canvas *canvas, int x, int y, const uint8_t *mask, int width, int height, size_t stride, kui_color color);
void kui_canvas_image(struct kui_canvas *canvas, const struct kui_image *image, float x, float y, float width, float height, float radius, float opacity);
int kui_image_create(struct kui_image *image, int width, int height);
void kui_image_release(struct kui_image *image);
void kui_image_scale(const struct kui_image *source, struct kui_image *target);
kui_color kui_color_mix(kui_color from, kui_color to, float amount);

/* The text (text.c). */
int kui_text_open(struct kui_text *text, const char *primary, const char *fallback);
void kui_text_close(struct kui_text *text);
void kui_text_metrics(struct kui_text *text, unsigned pixels, struct kui_text_line *line);
int kui_text_center(unsigned pixels, int top, int height);
int kui_text_width(struct kui_text *text, const char *string, size_t length, unsigned pixels, int bold);
int kui_text_draw(struct kui_text *text, struct kui_canvas *canvas, int x, int baseline, const char *string, size_t length, unsigned pixels, int bold, kui_color color);
int kui_text_draw_fit(struct kui_text *text, struct kui_canvas *canvas, int x, int baseline, const char *string, unsigned pixels, int bold, int width, kui_color color);
size_t kui_text_fit(struct kui_text *text, const char *string, unsigned pixels, int bold, int width, char *out, size_t size);
size_t kui_text_break(struct kui_text *text, const char *string, unsigned pixels, int bold, int width);
uint32_t kui_utf8_next(const char *string, size_t length, size_t *index);

/*
 * The theme: the colours and sizes of the Kei look, which every widget
 * draws with (the file manager's values, plan/ws071/spec.md).  One theme
 * exists so far, the light one; an application reads it and does not
 * change it.
 */
struct kui_theme {
	/* The window's ground (a vertical gradient) and the cards on it. */
	kui_color ground_top;
	kui_color ground_bottom;
	kui_color panel;
	kui_color panel_edge;
	kui_color shadow;

	/* A sidebar and a content card standing on glass, and on the plain ground. */
	kui_color glass_sidebar;
	kui_color glass_content;
	kui_color sidebar;

	/* Text: the main ink, the secondary and the faint, and an icon's ink. */
	kui_color text;
	kui_color text_secondary;
	kui_color text_faint;
	kui_color icon;

	/* The accent, a selection with and without the keyboard, the pointer's hover, a separator, a folder and danger. */
	kui_color accent;
	kui_color selection;
	kui_color selection_inactive;
	kui_color hover;
	kui_color separator;
	kui_color folder;
	kui_color danger;

	/* A card's and a control's corner radius, a list row's height, and the text sizes of body, secondary and title text. */
	float card_radius;
	float control_radius;
	int row_height;
	unsigned text_body;
	unsigned text_small;
	unsigned text_title;
};

/* The theme (theme.c). */
const struct kui_theme *kui_theme_default(void);

/* The icons (icons.c and icons-line.c). */
void kui_icon_draw(struct kui_canvas *canvas, enum kui_icon icon, float x, float y, float size, kui_color color);
void kui_icon_folder(struct kui_canvas *canvas, float x, float y, float size, kui_color tint);
void kui_icon_file(struct kui_canvas *canvas, struct kui_text *text, float x, float y, float size, kui_color band, const char *label);
void kui_icon_tag(struct kui_canvas *canvas, float cx, float cy, float radius, kui_color color);

#ifdef __cplusplus
}
#endif

#endif
