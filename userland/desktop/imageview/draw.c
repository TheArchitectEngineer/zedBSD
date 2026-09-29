/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The canvas of Image Viewer (ws091): the viewer's own words and cards,
 * drawn over the image (which the presenter draws beneath it) only when
 * they change.  The canvas is clear wherever the image or the window's
 * glass should show.
 *
 * It holds the empty window (the Kei mark, what to do, the Open button),
 * the card of an image that cannot be shown, the chip at the bottom (the
 * image's name, its place in the folder, its size and the zoom), a
 * message, and the file chooser over everything.
 */

#include "imageview.h"

#include "../artwork/mark.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The colours (0xAARRGGBB, not premultiplied). */
#define DRAW_TITLE		0xff334155U
#define DRAW_HINT		0xff64748bU
#define DRAW_CARD		0xf2ffffffU
#define DRAW_SHADOW		0x22000000U
#define DRAW_CHIP		0xd9ffffffU
#define DRAW_CHIP_DARK		0xb3202530U
#define DRAW_CHIP_TEXT		0xff334155U
#define DRAW_CHIP_TEXT_DARK	0xffffffffU
#define DRAW_BUTTON		0xff2f7cf6U
#define DRAW_BUTTON_TEXT	0xffffffffU
#define DRAW_DIM		0x66000000U
#define DRAW_SELECTED		0x332f7cf6U
#define DRAW_FOLDER		0xff2f7cf6U
#define DRAW_FILE		0xff3fa36bU
#define DRAW_MESSAGE		0xf0ffffffU
#define DRAW_MESSAGE_TEXT	0xff334155U
#define DRAW_WARNING		0xffe0a526U

/* The text sizes, in pixels. */
#define DRAW_TEXT		15U
#define DRAW_TEXT_SMALL		13U
#define DRAW_TEXT_LARGE		22U

/* The chip's height, and how far it stands above the bottom of the area. */
#define DRAW_CHIP_HEIGHT	36
#define DRAW_CHIP_LIFT		14

/* The mark's size on the empty window, and the largest the kept layers go. */
#define DRAW_MARK		96U
#define DRAW_MARK_MAX		128U

static void draw_empty(struct iv_app *app, struct iv_canvas *canvas);
static void draw_failed(struct iv_app *app, struct iv_canvas *canvas);
static void draw_chip(struct iv_app *app, struct iv_canvas *canvas);
static void draw_message(struct iv_app *app, struct iv_canvas *canvas);
static void draw_chooser(struct iv_app *app, struct iv_canvas *canvas);
static void draw_mark(struct iv_canvas *canvas, int x, int y, unsigned pixels);
static void draw_centred(struct iv_app *app, struct iv_canvas *canvas, int baseline, const char *text, unsigned pixels, uint32_t color);
static uint32_t draw_fade(uint32_t color, double opacity);

/*
 * Draws the canvas of words and cards.
 */
void
iv_draw(
	struct iv_app *app,
	struct iv_canvas *canvas)
{
	/* Clear, so that the image and the glass show. */
	iv_canvas_fill(canvas, 0, 0, canvas->width, canvas->height, 0x00000000U);
	app->chip_width = 0;
	app->chip_height = 0;

	/* The empty window, or the card of an image that cannot be shown. */
	if (!app->has_image)
		draw_empty(app, canvas);
	else if (app->current != NULL && app->current->error != 0)
		draw_failed(app, canvas);

	/* The chip while it is shown, and the message. */
	if (app->has_image)
		draw_chip(app, canvas);
	if (app->message[0] != '\0')
		draw_message(app, canvas);

	/* The chooser over everything. */
	if (app->chooser_open)
		draw_chooser(app, canvas);
	app->ui_dirty = 0;
}

/* Draws the empty window: the Kei mark, what to do, and the Open button. */
static void
draw_empty(
	struct iv_app *app,
	struct iv_canvas *canvas)
{
	int label_width;
	int x;
	int y;
	int width;
	int height;

	/* The mark above the middle. */
	draw_mark(canvas, (canvas->width - (int)DRAW_MARK) / 2, canvas->height / 2 - (int)DRAW_MARK - 24, DRAW_MARK);

	/* What the window is for, and what to do. */
	draw_centred(app, canvas, canvas->height / 2 + 12, "Open an image", DRAW_TEXT_LARGE, DRAW_TITLE);
	draw_centred(app, canvas, canvas->height / 2 + 40, "PNG, JPEG and GIF images", DRAW_TEXT, DRAW_HINT);

	/* The Open button. */
	iv_app_open_button(app, &x, &y, &width, &height);
	iv_canvas_round(canvas, x, y, width, height, height / 2, DRAW_BUTTON);
	label_width = iv_text_width(app->text, "Open\xe2\x80\xa6", DRAW_TEXT);
	iv_text_draw(app->text, canvas, x + (width - label_width) / 2, y + height / 2 + 5, "Open\xe2\x80\xa6", DRAW_TEXT, DRAW_BUTTON_TEXT);
}

/* Draws the card of an image that cannot be shown: what went wrong, why, and which file. */
static void
draw_failed(
	struct iv_app *app,
	struct iv_canvas *canvas)
{
	const char *name;
	int width;
	int height;
	int x;
	int y;

	/* A card in the middle of the area. */
	width = 420;
	if (width > app->area_width - 32)
		width = app->area_width - 32;
	height = 132;
	x = app->area_x + (app->area_width - width) / 2;
	y = app->area_y + (app->area_height - height) / 2;
	iv_canvas_round(canvas, x + 1, y + 3, width, height, 16, DRAW_SHADOW);
	iv_canvas_round(canvas, x, y, width, height, 16, DRAW_CARD);

	/* A warning mark, what went wrong and why. */
	iv_canvas_round(canvas, x + 24, y + 26, 22, 22, 11, DRAW_WARNING);
	iv_text_draw(app->text, canvas, x + 34, y + 43, "!", DRAW_TEXT, 0xffffffffU);
	iv_text_draw(app->text, canvas, x + 60, y + 43, "Can't show this image", DRAW_TEXT_LARGE - 4U, DRAW_TITLE);
	iv_text_draw(app->text, canvas, x + 60, y + 72, app->current->reason, DRAW_TEXT, DRAW_HINT);

	/* The file's name. */
	name = strrchr(app->current->path, '/');
	if (name == NULL)
		name = app->current->path;
	else
		name++;
	iv_text_draw(app->text, canvas, x + 60, y + 100, name, DRAW_TEXT_SMALL, DRAW_HINT);
}

/* Draws the chip at the bottom of the area: the image's name, its place, its size and the zoom, fading at its end. */
static void
draw_chip(
	struct iv_app *app,
	struct iv_canvas *canvas)
{
	char line[IV_NAME_MAX + 96];
	const char *name;
	double opacity;
	uint32_t fill;
	uint32_t ink;
	int width;
	int zoom;
	int x;
	int y;

	/* Not shown, or gone. */
	opacity = iv_app_chip_opacity(app);
	if (opacity <= 0.0 || app->current == NULL)
		return;

	/* The file's name. */
	name = strrchr(app->current->path, '/');
	if (name == NULL)
		name = app->current->path;
	else
		name++;

	/* The line: the name, the place in the folder, the size and the zoom (an image that cannot be shown has only its name and place). */
	zoom = (int)floor(app->scale * 100.0 + 0.5);
	if (app->current->error != 0) {
		snprintf(line, sizeof(line), "%s  \xc2\xb7  %lu / %lu", name, (unsigned long)app->folder.index + 1UL, (unsigned long)app->folder.count);
	} else {
		snprintf(line, sizeof(line), "%s  \xc2\xb7  %lu / %lu  \xc2\xb7  %d \xc3\x97 %d  \xc2\xb7  %d%%", name, (unsigned long)app->folder.index + 1UL,
		    (unsigned long)app->folder.count, app->current->file_width, app->current->file_height, zoom);
	}

	/* A pill as wide as the line, within the area, at its bottom. */
	width = iv_text_width(app->text, line, DRAW_TEXT_SMALL) + 36;
	if (width > app->area_width - 16)
		width = app->area_width - 16;
	x = app->area_x + (app->area_width - width) / 2;
	y = app->area_y + app->area_height - DRAW_CHIP_HEIGHT - DRAW_CHIP_LIFT;

	/* Light on the glass, dark over the full screen's black. */
	fill = DRAW_CHIP;
	ink = DRAW_CHIP_TEXT;
	if (app->fullscreen) {
		fill = DRAW_CHIP_DARK;
		ink = DRAW_CHIP_TEXT_DARK;
	}

	/* The pill and its line, fading together. */
	iv_canvas_round(canvas, x, y, width, DRAW_CHIP_HEIGHT, DRAW_CHIP_HEIGHT / 2, draw_fade(fill, opacity));
	iv_text_draw(app->text, canvas, x + 18, y + 23, line, DRAW_TEXT_SMALL, draw_fade(ink, opacity));

	/* Where it is, for the glass under it. */
	app->chip_x = x;
	app->chip_y = y;
	app->chip_width = width;
	app->chip_height = DRAW_CHIP_HEIGHT;
}

/* Draws the message in a card at the top of the area. */
static void
draw_message(
	struct iv_app *app,
	struct iv_canvas *canvas)
{
	int width;
	int height;
	int x;
	int y;

	/* A card as wide as the text, within the window. */
	width = iv_text_width(app->text, app->message, DRAW_TEXT) + 32;
	if (width > canvas->width - 32)
		width = canvas->width - 32;
	height = 40;
	x = (canvas->width - width) / 2;
	y = app->area_y + 16;
	iv_canvas_round(canvas, x + 1, y + 2, width, height, 12, DRAW_SHADOW);
	iv_canvas_round(canvas, x, y, width, height, 12, DRAW_MESSAGE);
	iv_text_draw(app->text, canvas, x + 16, y + 26, app->message, DRAW_TEXT, DRAW_MESSAGE_TEXT);
}

/* Draws the file chooser: the window dimmed, a card with the folder and its entries. */
static void
draw_chooser(
	struct iv_app *app,
	struct iv_canvas *canvas)
{
	char line[IV_NAME_MAX + 8];
	const struct iv_entry *entry;
	const char *folder;
	size_t rows;
	size_t row;
	size_t index;
	int x;
	int y;
	int width;
	int height;
	int row_y;
	int folder_width;
	int differs;

	/* The window dimmed, and the card. */
	iv_canvas_blend(canvas, 0, 0, canvas->width, canvas->height, DRAW_DIM);
	iv_chooser_layout(app, &x, &y, &width, &height, &rows);
	iv_canvas_round(canvas, x, y, width, height, 16, DRAW_CARD);

	/* The header: what the card is for, and the folder (its end, when too long). */
	iv_text_draw(app->text, canvas, x + 18, y + 24, "Open an image", DRAW_TEXT, DRAW_TITLE);
	folder = app->chooser.folder;
	while (folder[0] != '\0') {
		folder_width = iv_text_width(app->text, folder, DRAW_TEXT_SMALL);
		if (folder_width <= width - 36)
			break;
		folder++;
	}

	/* The folder, and a line under the header. */
	iv_text_draw(app->text, canvas, x + 18, y + 44, folder, DRAW_TEXT_SMALL, DRAW_HINT);
	iv_canvas_fill(canvas, x + 12, y + IV_CHOOSER_HEADER - 1, width - 24, 1, 0xffd4d7dcU);

	/* The rows in view. */
	for (row = 0; row < rows; row++) {
		index = app->chooser.first + row;
		if (index >= app->chooser.count)
			break;
		entry = &app->chooser.entries[index];
		row_y = y + IV_CHOOSER_HEADER + (int)row * IV_CHOOSER_ROW;

		/* The selection's band. */
		if (index == app->chooser.selected)
			iv_canvas_round(canvas, x + 8, row_y + 2, width - 16, IV_CHOOSER_ROW - 4, 8, DRAW_SELECTED);

		/* A mark of the kind (a blue folder or a green picture) and the name. */
		if (entry->folder) {
			iv_canvas_round(canvas, x + 20, row_y + 11, 18, 13, 3, DRAW_FOLDER);
			snprintf(line, sizeof(line), "%s/", entry->name);
			differs = strcmp(entry->name, "..");
			if (differs == 0)
				snprintf(line, sizeof(line), "Parent folder");
		} else {
			iv_canvas_round(canvas, x + 20, row_y + 10, 18, 15, 3, DRAW_FILE);
			snprintf(line, sizeof(line), "%s", entry->name);
		}

		/* The entry's name. */
		iv_text_draw(app->text, canvas, x + 48, row_y + 22, line, DRAW_TEXT, DRAW_TITLE);
	}

	/* An empty folder says so. */
	if (app->chooser.count == 0)
		iv_text_draw(app->text, canvas, x + 18, y + IV_CHOOSER_HEADER + 24, "No images here", DRAW_TEXT, DRAW_HINT);
}

/*
 * Draws the Kei mark in a square of a size at (x, y): its seven layers
 * (userland/desktop/artwork/mark.c), each in its colour, as the file
 * manager draws it.  The layers are rendered once for a size and kept.
 */
static void
draw_mark(
	struct iv_canvas *canvas,
	int x,
	int y,
	unsigned pixels)
{
	/*
	 * The bar pale, its shade deeper, the leaf clearer and its shade the
	 * deep blue of the splash, the overlap deeper still, then the white
	 * light along the edges and the sheen (colour, alpha).
	 */
	static const uint32_t colours[KEILAND_MARK_LAYERS][2] = {
		{ 0xa9c3f6U, 175U },
		{ 0x7fa2f0U, 90U },
		{ 0xa3d8faU, 170U },
		{ 0x3a86f5U, 170U },
		{ 0x2f7cf3U, 200U },
		{ 0xffffffU, 170U },
		{ 0xffffffU, 60U }
	};

	/*
	 * The layers at the size last drawn (zero before the first); a new size
	 * renders them again.  They live for the program's life.
	 */
	static uint8_t layers[KEILAND_MARK_LAYERS][DRAW_MARK_MAX * DRAW_MARK_MAX];
	static unsigned layers_pixels;
	unsigned layer;

	/* A mark larger than the kept layers is drawn at their largest. */
	if (pixels > DRAW_MARK_MAX)
		pixels = DRAW_MARK_MAX;
	if (pixels == 0U)
		return;

	/* The layers at this size. */
	if (layers_pixels != pixels) {
		for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++)
			keiland_mark_raster(layer, pixels, layers[layer], pixels);
		layers_pixels = pixels;
	}

	/* Each layer in its colour. */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++)
		iv_canvas_mask(canvas, x, y, layers[layer], (int)pixels, (int)pixels, (colours[layer][1] << 24) | colours[layer][0]);
}

/* Draws a line of text centred across the canvas. */
static void
draw_centred(
	struct iv_app *app,
	struct iv_canvas *canvas,
	int baseline,
	const char *text,
	unsigned pixels,
	uint32_t color)
{
	int width;

	/* Its width decides where it starts. */
	width = iv_text_width(app->text, text, pixels);
	iv_text_draw(app->text, canvas, (canvas->width - width) / 2, baseline, text, pixels, color);
}

/* Makes a colour as much more transparent as an opacity (0 to 1) says. */
static uint32_t
draw_fade(
	uint32_t color,
	double opacity)
{
	uint32_t alpha;

	/* The colour's alpha scaled. */
	alpha = (uint32_t)((double)(color >> 24) * opacity + 0.5);

	/* Reports the colour with its new alpha. */
	return (alpha << 24) | (color & 0x00ffffffU);
}
