/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The frame of PDF Viewer: the pages where the view lays them out, the
 * page indicator, the notice that a page drawn has content libpdf could
 * not show, the message, and the file chooser over everything.
 *
 * The pages come from the document's cache of rasters (document.c), made
 * when a page is first drawn at the scale in force.
 */

#include "viewer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The colours of the frame (0xAARRGGBB, not premultiplied). */
#define DRAW_BACKGROUND		0xffdfe2e7U
#define DRAW_SHADOW		0x22000000U
#define DRAW_EDGE		0x33000000U
#define DRAW_PILL		0xcc1f2430U
#define DRAW_PILL_TEXT		0xffffffffU
#define DRAW_HINT		0xff5a6070U
#define DRAW_TITLE		0xff2a2f3aU
#define DRAW_DIM		0x66000000U
#define DRAW_CARD		0xfff7f8faU
#define DRAW_SELECTED		0x332f7cf6U
#define DRAW_FOLDER		0xff2f7cf6U
#define DRAW_FILE		0xffd9534fU
#define DRAW_MESSAGE		0xf0ffffffU
#define DRAW_MESSAGE_TEXT	0xff8a1f1fU
#define DRAW_NOTICE		0xf2f7f8faU
#define DRAW_NOTICE_TEXT	0xff3a4150U
#define DRAW_NOTICE_MARK	0xffe0a526U

/* What the notice says, and the display-list flags that call for it. */
#define DRAW_NOTICE_WORDS	"Some content could not be shown"
#define DRAW_NOTICE_FLAGS	(PDF_DISPLAY_SKIPPED | PDF_DISPLAY_DAMAGED | PDF_DISPLAY_LIMITED)

/* The text sizes, in pixels. */
#define DRAW_TEXT		15U
#define DRAW_TEXT_LARGE		20U

static void draw_page(struct pv_app *app, struct pv_canvas *canvas, size_t index, int x, int y);
static void draw_scroll(struct pv_app *app, struct pv_canvas *canvas);
static void draw_single(struct pv_app *app, struct pv_canvas *canvas);
static void draw_empty(struct pv_app *app, struct pv_canvas *canvas);
static void draw_indicator(struct pv_app *app, struct pv_canvas *canvas);
static void draw_notice(struct pv_app *app, struct pv_canvas *canvas);
static void draw_message(struct pv_app *app, struct pv_canvas *canvas);
static void draw_chooser(struct pv_app *app, struct pv_canvas *canvas);
static void draw_centred(struct pv_app *app, struct pv_canvas *canvas, int baseline, const char *text, unsigned pixels, uint32_t color);

/*
 * Draws the frame.
 */
void
pv_draw(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	/* The ground; the pages drawn gather their flags anew. */
	pv_canvas_fill(canvas, 0, 0, canvas->width, canvas->height, DRAW_BACKGROUND);
	app->shown_flags = 0;

	/* The pages, by the mode, or the empty window's hint. */
	if (!app->has_document) {
		draw_empty(app, canvas);
	} else if (app->mode == PV_MODE_SCROLL) {
		draw_scroll(app, canvas);
	} else {
		draw_single(app, canvas);
	}

	/* The page indicator, the notice, the message and the chooser over the pages. */
	if (app->has_document && app->indicator_until != 0)
		draw_indicator(app, canvas);
	draw_notice(app, canvas);
	if (app->message[0] != '\0')
		draw_message(app, canvas);
	if (app->choosing)
		draw_chooser(app, canvas);
	app->dirty = 0;
}

/* Draws a page with its top left at a place: a shadow, an edge, and its raster. */
static void
draw_page(
	struct pv_app *app,
	struct pv_canvas *canvas,
	size_t index,
	int x,
	int y)
{
	const struct pv_page *page;
	double scale;
	int width;
	int height;
	int error;

	/* The page's size at its scale. */
	scale = pv_app_scale(app, index);
	width = (int)ceil(app->document.pages[index].width * scale - 1e-6);
	height = (int)ceil(app->document.pages[index].height * scale - 1e-6);

	/* Nothing to draw for a page outside the frame. */
	if (x >= canvas->width || y >= canvas->height || x + width <= 0 || y + height <= 0)
		return;

	/* A soft shadow and a thin edge around it. */
	pv_canvas_blend(canvas, x - 1, y + 2, width + 2, height + 2, DRAW_SHADOW);
	pv_canvas_blend(canvas, x - 1, y - 1, width + 2, height + 2, DRAW_EDGE);

	/* The raster at the scale; a page without one is white. */
	error = pv_document_raster(&app->document, index, scale, &page);
	if (error == 0 && page->list != NULL)
		app->shown_flags |= page->list->flags;
	if (error != 0 || page->raster == NULL) {
		pv_canvas_fill(canvas, x, y, width, height, 0xffffffffU);
		return;
	}
	pv_canvas_copy(canvas, x, y, page->raster, page->raster_width, page->raster_height);
}

/* Draws the scroll mode's column: every page that meets the frame. */
static void
draw_scroll(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	double scale;
	double top;
	double content_width;
	size_t index;
	size_t first;
	size_t last;
	int x;
	int y;
	int width;
	int height;

	/* Every page from the top, drawn when it meets the frame. */
	scale = pv_app_scale(app, 0);
	content_width = pv_app_content_width(app);
	top = PV_MARGIN;
	first = app->document.count;
	last = 0;
	for (index = 0; index < app->document.count; index++) {
		width = (int)ceil(app->document.pages[index].width * scale - 1e-6);
		height = (int)ceil(app->document.pages[index].height * scale - 1e-6);
		y = (int)floor(top - app->scroll_y);
		top += (double)height + PV_GAP;
		if (y + height <= 0)
			continue;
		if (y >= canvas->height)
			break;

		/* Centred across, or from the left of the view when the pages are wider. */
		x = (canvas->width - width) / 2;
		if (content_width > (double)canvas->width)
			x = (int)floor(PV_MARGIN + (content_width - 2.0 * PV_MARGIN - (double)width) / 2.0 - app->scroll_x);
		draw_page(app, canvas, index, x, y);
		if (index < first)
			first = index;
		last = index;
	}

	/* The rasters of the pages far from the view may go. */
	if (first < app->document.count) {
		if (first > 2)
			first -= 2;
		else
			first = 0;
		pv_document_trim(&app->document, first, last + 2);
	}
}

/* Draws the page mode's page, and its neighbour while it is swiped or turned. */
static void
draw_single(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	double scale;
	double top;
	double offset;
	size_t neighbour;
	int width;
	int height;
	int x;
	int y;

	/* The page, centred or panned, moved by the swipe. */
	scale = pv_app_scale(app, app->page);
	width = (int)ceil(app->document.pages[app->page].width * scale - 1e-6);
	height = (int)ceil(app->document.pages[app->page].height * scale - 1e-6);
	top = pv_app_page_top(app, app->page);
	x = (canvas->width - width) / 2;
	if (width + 2 * PV_MARGIN > canvas->width)
		x = (int)floor(PV_MARGIN - app->scroll_x);
	y = (int)floor(top - app->scroll_y);
	offset = app->swipe;
	draw_page(app, canvas, app->page, x + (int)floor(offset), y);

	/* The previous page comes from the left while the page moves right. */
	if (offset > 0.5 && app->page > 0) {
		neighbour = app->page - 1;
		scale = pv_app_scale(app, neighbour);
		width = (int)ceil(app->document.pages[neighbour].width * scale - 1e-6);
		height = (int)ceil(app->document.pages[neighbour].height * scale - 1e-6);
		x = (canvas->width - width) / 2 - (int)floor(pv_app_neighbour_distance(app, neighbour)) + (int)floor(offset);
		y = (canvas->height - height) / 2;
		if (y < PV_MARGIN)
			y = PV_MARGIN;
		draw_page(app, canvas, neighbour, x, y);
	}

	/* The next page comes from the right while the page moves left. */
	if (offset < -0.5 && app->page + 1 < app->document.count) {
		neighbour = app->page + 1;
		scale = pv_app_scale(app, neighbour);
		width = (int)ceil(app->document.pages[neighbour].width * scale - 1e-6);
		height = (int)ceil(app->document.pages[neighbour].height * scale - 1e-6);
		x = (canvas->width - width) / 2 + (int)floor(pv_app_neighbour_distance(app, neighbour)) + (int)floor(offset);
		y = (canvas->height - height) / 2;
		if (y < PV_MARGIN)
			y = PV_MARGIN;
		draw_page(app, canvas, neighbour, x, y);
	}

	/* The rasters of pages beyond the neighbours may go. */
	neighbour = 0;
	if (app->page > 1)
		neighbour = app->page - 1;
	pv_document_trim(&app->document, neighbour, app->page + 1);
}

/* Draws the window without a document: how to open one. */
static void
draw_empty(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	int middle;

	/* Two lines in the middle. */
	middle = canvas->height / 2;
	draw_centred(app, canvas, middle - 8, "No document is open", DRAW_TEXT_LARGE, DRAW_TITLE);
	draw_centred(app, canvas, middle + 22, "Open a PDF with Ctrl+O, or from Files", DRAW_TEXT, DRAW_HINT);
}

/* Draws the page indicator at the bottom: "Page 3 of 10". */
static void
draw_indicator(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	char text[64];
	size_t page;
	double middle;
	double bottom;
	double scale;
	int width;
	int height;
	int x;
	int y;

	/* The page the view is on: the page mode's, or the scroll mode's across the middle. */
	page = app->page;
	if (app->mode == PV_MODE_SCROLL) {
		middle = app->scroll_y + (double)app->height / 2.0;
		scale = pv_app_scale(app, 0);
		bottom = PV_MARGIN;
		for (page = 0; page + 1 < app->document.count; page++) {
			bottom += app->document.pages[page].height * scale + PV_GAP;
			if (bottom > middle)
				break;
		}
	}

	/* A dark pill with the text. */
	snprintf(text, sizeof(text), "Page %lu of %lu", (unsigned long)(page + 1), (unsigned long)app->document.count);
	width = pv_text_width(app->text, text, DRAW_TEXT) + 28;
	height = 30;
	x = (canvas->width - width) / 2;
	y = canvas->height - height - 20;
	pv_canvas_round(canvas, x, y, width, height, height / 2, DRAW_PILL);
	pv_text_draw(app->text, canvas, x + 14, y + 20, text, DRAW_TEXT, DRAW_PILL_TEXT);
}

/*
 * Draws, in the bottom left corner, a small notice that a page in view has
 * content libpdf left out (a font, filter or shading it does not read) or
 * could not finish, and logs when the notice comes and goes.
 */
static void
draw_notice(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	int wanted;
	int width;
	int height;
	int x;
	int y;

	/* Only a document whose pages in view left something out has the notice. */
	wanted = 0;
	if (app->has_document) {
		if ((app->shown_flags & DRAW_NOTICE_FLAGS) != 0)
			wanted = 1;
	}

	/* Logs a change, for the tests. */
	if (wanted != app->notice_shown) {
		app->notice_shown = wanted;
		if (wanted) {
			pv_log("NOTICE shown flags=%u", app->shown_flags);
		} else {
			pv_log("NOTICE hidden");
		}
	}

	/* Nothing to draw without the notice. */
	if (!wanted)
		return;

	/* A light pill with an amber mark and the words, clear of the page indicator in the middle. */
	width = pv_text_width(app->text, DRAW_NOTICE_WORDS, 13U) + 40;
	height = 28;
	x = 16;
	y = canvas->height - height - 21;
	pv_canvas_round(canvas, x + 1, y + 2, width, height, height / 2, DRAW_SHADOW);
	pv_canvas_round(canvas, x - 1, y - 1, width + 2, height + 2, height / 2 + 1, DRAW_EDGE);
	pv_canvas_round(canvas, x, y, width, height, height / 2, DRAW_NOTICE);
	pv_canvas_round(canvas, x + 12, y + 10, 8, 8, 4, DRAW_NOTICE_MARK);
	pv_text_draw(app->text, canvas, x + 28, y + 19, DRAW_NOTICE_WORDS, 13U, DRAW_NOTICE_TEXT);
}

/* Draws the message in a card at the top (in the middle for an empty window). */
static void
draw_message(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	int width;
	int height;
	int x;
	int y;

	/* A card as wide as the text, within the window. */
	width = pv_text_width(app->text, app->message, DRAW_TEXT) + 32;
	if (width > canvas->width - 32)
		width = canvas->width - 32;
	height = 40;
	x = (canvas->width - width) / 2;
	y = 20;
	if (!app->has_document)
		y = canvas->height / 2 + 48;
	pv_canvas_round(canvas, x + 1, y + 2, width, height, 10, DRAW_SHADOW);
	pv_canvas_round(canvas, x, y, width, height, 10, DRAW_MESSAGE);
	pv_text_draw(app->text, canvas, x + 16, y + 26, app->message, DRAW_TEXT, DRAW_MESSAGE_TEXT);
}

/* Draws the file chooser: the frame dimmed, a card with the folder and its entries. */
static void
draw_chooser(
	struct pv_app *app,
	struct pv_canvas *canvas)
{
	char line[300];
	const struct pv_entry *entry;
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

	/* The frame dimmed, and the card. */
	pv_canvas_blend(canvas, 0, 0, canvas->width, canvas->height, DRAW_DIM);
	pv_chooser_layout(app, &x, &y, &width, &height, &rows);
	pv_canvas_round(canvas, x, y, width, height, 14, DRAW_CARD);

	/* The header: what the card is for, and the folder (its end, when too long). */
	pv_text_draw(app->text, canvas, x + 18, y + 24, "Open a PDF", DRAW_TEXT, DRAW_TITLE);
	folder = app->chooser.folder;
	while (folder[0] != '\0') {
		folder_width = pv_text_width(app->text, folder, 13U);
		if (folder_width <= width - 36)
			break;
		folder++;
	}
	pv_text_draw(app->text, canvas, x + 18, y + 44, folder, 13U, DRAW_HINT);
	pv_canvas_fill(canvas, x + 12, y + PV_CHOOSER_HEADER - 1, width - 24, 1, 0xffd4d7dcU);

	/* The rows in view. */
	for (row = 0; row < rows; row++) {
		index = app->chooser.first + row;
		if (index >= app->chooser.count)
			break;
		entry = &app->chooser.entries[index];
		row_y = y + PV_CHOOSER_HEADER + (int)row * PV_CHOOSER_ROW;

		/* The selection's band. */
		if (index == app->chooser.selected)
			pv_canvas_round(canvas, x + 8, row_y + 2, width - 16, PV_CHOOSER_ROW - 4, 8, DRAW_SELECTED);

		/* A mark of the kind (a blue folder or a red page) and the name. */
		if (entry->folder) {
			pv_canvas_round(canvas, x + 20, row_y + 11, 18, 13, 3, DRAW_FOLDER);
			snprintf(line, sizeof(line), "%s/", entry->name);
			differs = strcmp(entry->name, "..");
			if (differs == 0)
				snprintf(line, sizeof(line), "Parent folder");
		} else {
			pv_canvas_round(canvas, x + 22, row_y + 8, 14, 18, 2, DRAW_FILE);
			snprintf(line, sizeof(line), "%s", entry->name);
		}
		pv_text_draw(app->text, canvas, x + 48, row_y + 22, line, DRAW_TEXT, DRAW_TITLE);
	}

	/* An empty folder says so. */
	if (app->chooser.count == 0)
		pv_text_draw(app->text, canvas, x + 18, y + PV_CHOOSER_HEADER + 24, "No PDF files here", DRAW_TEXT, DRAW_HINT);
}

/* Draws a line of text centred across the frame. */
static void
draw_centred(
	struct pv_app *app,
	struct pv_canvas *canvas,
	int baseline,
	const char *text,
	unsigned pixels,
	uint32_t color)
{
	int width;

	/* Its width decides where it starts. */
	width = pv_text_width(app->text, text, pixels);
	pv_text_draw(app->text, canvas, (canvas->width - width) / 2, baseline, text, pixels, color);
}
