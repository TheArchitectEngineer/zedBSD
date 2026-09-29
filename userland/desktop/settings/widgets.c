/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts Settings' pages are built of: the page's header, a card with
 * its title, a row of a label and its value, and the Kei mark.
 *
 * Each part is drawn from a top edge within a column and returns the edge
 * below it, so that a page lays its parts out from the top down.
 */

#include "settings.h"

#include "../artwork/mark.h"

#include <string.h>

/* The header's text sizes: the page's name and its summary. */
#define WIDGETS_TEXT_TITLE	30U
#define WIDGETS_TEXT_SUMMARY	15U

/* A card's corners, its inner margin, its title's and subtitle's sizes, and the space its title takes. */
#define WIDGETS_CARD_RADIUS	14.0f
#define WIDGETS_CARD_PAD	18
#define WIDGETS_TEXT_CARD	16U
#define WIDGETS_TEXT_CARD_SUB	13U
#define WIDGETS_CARD_TITLE	46

/* A row's height and text size, and the share of the row its label takes. */
#define WIDGETS_ROW_HEIGHT	40
#define WIDGETS_TEXT_ROW	14U
#define WIDGETS_LABEL_SHARE	0.34f

/* The largest Kei mark drawn, in pixels a side (its layers are kept rendered at the last size). */
#define WIDGETS_MARK_MAX	160U

/*
 * Draws a page's header: its name large and its summary under it.
 * Returns the edge below the header.
 */
int
se_page_header(
	struct se_app *app,
	struct fm_canvas *canvas,
	const struct se_page *page,
	int x,
	int top,
	int width)
{
	struct fm_text_line title;
	struct fm_text_line summary;
	int baseline;

	/* The two lines' measurements. */
	fm_text_metrics(app->text, WIDGETS_TEXT_TITLE, &title);
	fm_text_metrics(app->text, WIDGETS_TEXT_SUMMARY, &summary);

	/* The name, bold. */
	baseline = top + title.ascent;
	(void)fm_text_draw_fit(app->text, canvas, x, baseline, page->name, WIDGETS_TEXT_TITLE, 1, width, SE_COLOR_TEXT);

	/* The summary under it. */
	baseline = top + title.height + 2 + summary.ascent;
	(void)fm_text_draw_fit(app->text, canvas, x, baseline, page->summary, WIDGETS_TEXT_SUMMARY, 0, width, SE_COLOR_TEXT_SECONDARY);

	/* The edge below the summary. */
	return top + title.height + 2 + summary.height;
}

/*
 * Draws a card's ground and its title (and subtitle, when one is given)
 * in a rectangle.  Returns the edge where the card's content starts.
 */
int
se_card_begin(
	struct se_app *app,
	struct fm_canvas *canvas,
	int x,
	int top,
	int width,
	int height,
	const char *title,
	const char *subtitle)
{
	struct fm_text_line line;
	int baseline;

	/* The card: a whiter veil with a bright edge. */
	fm_canvas_round(canvas, (float)x, (float)top, (float)width, (float)height, WIDGETS_CARD_RADIUS, SE_COLOR_CARD);
	fm_canvas_round_border(canvas, (float)x, (float)top, (float)width, (float)height, WIDGETS_CARD_RADIUS, 1.0f, SE_COLOR_CARD_EDGE);

	/* A card without a title starts at its margin. */
	if (title == NULL)
		return top + WIDGETS_CARD_PAD;

	/* The title, bold. */
	fm_text_metrics(app->text, WIDGETS_TEXT_CARD, &line);
	baseline = top + WIDGETS_CARD_PAD + line.ascent;
	(void)fm_text_draw_fit(app->text, canvas, x + WIDGETS_CARD_PAD + 2, baseline, title, WIDGETS_TEXT_CARD, 1, width - 2 * WIDGETS_CARD_PAD, SE_COLOR_TEXT);

	/* The subtitle under it, when there is one. */
	if (subtitle != NULL) {
		baseline += line.descent + 4;
		fm_text_metrics(app->text, WIDGETS_TEXT_CARD_SUB, &line);
		baseline += line.ascent;
		(void)fm_text_draw_fit(app->text, canvas, x + WIDGETS_CARD_PAD + 2, baseline, subtitle, WIDGETS_TEXT_CARD_SUB, 0, width - 2 * WIDGETS_CARD_PAD, SE_COLOR_TEXT_SECONDARY);
		return baseline + line.descent + 10;
	}

	/* The content starts under the title. */
	return top + WIDGETS_CARD_TITLE;
}

/*
 * Reports how tall a card is that holds some rows of values, with or
 * without a title (no subtitle).
 */
int
se_card_height(
	int rows,
	int titled)
{
	int height;

	/* The margins and the rows. */
	height = 2 * WIDGETS_CARD_PAD + rows * WIDGETS_ROW_HEIGHT;

	/* The title's room. */
	if (titled != 0)
		height += WIDGETS_CARD_TITLE - WIDGETS_CARD_PAD;

	/* The card's height. */
	return height;
}

/*
 * Draws one row of a card: a label and its value beside it, with a thin
 * line under the row unless it is the card's last.  x and width are the
 * card's.  Returns the edge below the row.
 */
int
se_row_value(
	struct se_app *app,
	struct fm_canvas *canvas,
	int x,
	int top,
	int width,
	const char *label,
	const char *value,
	int last)
{
	int baseline;
	int label_width;
	int left;
	int right;

	/* The row's text line, and the columns of the label and the value. */
	baseline = fm_text_center(WIDGETS_TEXT_ROW, top, WIDGETS_ROW_HEIGHT);
	left = x + WIDGETS_CARD_PAD + 2;
	right = x + width - WIDGETS_CARD_PAD;
	label_width = (int)((float)(right - left) * WIDGETS_LABEL_SHARE);

	/* The label, quiet, and the value, plain. */
	(void)fm_text_draw_fit(app->text, canvas, left, baseline, label, WIDGETS_TEXT_ROW, 0, label_width - 12, SE_COLOR_TEXT_SECONDARY);
	(void)fm_text_draw_fit(app->text, canvas, left + label_width, baseline, value, WIDGETS_TEXT_ROW, 0, right - left - label_width, SE_COLOR_TEXT);

	/* The line under the row, unless it is the last. */
	if (last == 0)
		fm_canvas_line(canvas, (float)left, (float)(top + WIDGETS_ROW_HEIGHT) - 0.5f, (float)right, (float)(top + WIDGETS_ROW_HEIGHT) - 0.5f, 1.0f, SE_COLOR_SEPARATOR);

	/* The edge below the row. */
	return top + WIDGETS_ROW_HEIGHT;
}

/*
 * Draws the Kei mark in a square of a size in pixels at (x, y), as opaque
 * as asked (0..1): its seven layers (userland/desktop/artwork/mark.c), each
 * in its colour, as the file manager draws it (ws035-p109).
 */
void
se_mark_draw(
	struct fm_canvas *canvas,
	int x,
	int y,
	unsigned pixels,
	float opacity)
{
	/*
	 * The bar pale, its shade deeper, the leaf clearer and its shade the
	 * deep blue of the splash, the overlap deeper still, then the white
	 * light along the edges and the sheen (colour, alpha).  The panes are
	 * translucent, so what is behind shows through.
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
	static uint8_t layers[KEILAND_MARK_LAYERS][WIDGETS_MARK_MAX * WIDGETS_MARK_MAX];
	static unsigned layers_pixels;
	uint32_t alpha;
	unsigned layer;

	/* A mark larger than the kept layers is drawn at their largest; an empty one not at all. */
	if (pixels > WIDGETS_MARK_MAX)
		pixels = WIDGETS_MARK_MAX;
	if (pixels == 0U)
		return;

	/* The layers at this size. */
	if (layers_pixels != pixels) {
		for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++)
			keiland_mark_raster(layer, pixels, layers[layer], pixels);
		layers_pixels = pixels;
	}

	/* Each layer in its colour, as opaque as asked. */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
		alpha = (uint32_t)((float)colours[layer][1] * opacity + 0.5f);
		fm_canvas_mask(canvas, x, y, layers[layer], (int)pixels, (int)pixels, pixels, FM_RGBA(colours[layer][0], alpha));
	}
}
