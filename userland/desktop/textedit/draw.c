/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The frame of Text Editor, drawn on the CPU (plan/ws092/design.md
 * section 4): the card, the line numbers, the rows of text with the
 * selection, the places found and the cursor, the scroll bar, the status
 * chip and the message, and over them a dialog or the file chooser.
 *
 * Only the rows in view are drawn.  The window is glass when zdesktop has
 * glass: the frame is then clear around the card, whose white lets the
 * frosted desktop show a little.
 */

#include "textedit.h"

#include <stdio.h>
#include <string.h>

/* The colours (0xAARRGGBB, not premultiplied). */
#define DRAW_GROUND		0xffe6ecf2U
#define DRAW_CARD		0xf4fbfcfdU
#define DRAW_CARD_OPAQUE	0xfffbfcfdU
#define DRAW_TEXT		0xff1e293bU
#define DRAW_FAINT		0xff94a3b8U
#define DRAW_NUMBER_NOW		0xff334155U
#define DRAW_CONTROL		0xff94a3b8U
#define DRAW_ROW_NOW		0xfff1f5f9U
#define DRAW_SELECTION		0xffcfe3ffU
#define DRAW_SELECTION_IDLE	0xffe2e8f0U
#define DRAW_MATCH		0xfffde68aU
#define DRAW_MATCH_NOW		0xfffbbf24U
#define DRAW_CURSOR		0xff2563ebU
#define DRAW_SCROLL		0x50334155U
#define DRAW_CHIP		0xecffffffU
#define DRAW_CHIP_EDGE		0x1f334155U
#define DRAW_CHIP_TEXT		0xff475569U
#define DRAW_MESSAGE		0xe8334155U
#define DRAW_MESSAGE_TEXT	0xffffffffU
#define DRAW_SHADE		0x33101828U
#define DRAW_PANEL		0xfffdfdfeU
#define DRAW_PANEL_EDGE		0x26334155U
#define DRAW_TITLE		0xff0f172aU
#define DRAW_BODY		0xff475569U
#define DRAW_PRIMARY		0xff2563ebU
#define DRAW_PRIMARY_LIT	0xff1d4ed8U
#define DRAW_BUTTON		0xffeef2f6U
#define DRAW_BUTTON_LIT		0xffe2e8f0U
#define DRAW_BUTTON_TEXT	0xff1e293bU
#define DRAW_ROW_CHOSEN		0xffdbeafeU
#define DRAW_FIELD		0xffffffffU
#define DRAW_FIELD_EDGE		0xff93c5fdU

/* How long the cursor shows and hides, in milliseconds (as the editor's). */
#define DRAW_BLINK_MS		530U

/* The interface's small text size, and the status chip's. */
#define DRAW_SMALL_PIXELS	12U
#define DRAW_TITLE_PIXELS	15U

/* The scroll bar's width and shortest thumb. */
#define DRAW_SCROLL_WIDTH	4
#define DRAW_SCROLL_MIN		24

static void draw_rows(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text);
static void draw_row(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text, size_t row, int top, size_t start, size_t end);
static void draw_span(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text, int top, size_t row_start, size_t row_end, size_t from, size_t to, int newline, uint32_t color);
static void draw_matches(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text, int top, size_t start, size_t end);
static void draw_glyphs(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text, int top, size_t start, size_t end);
static void draw_numbers(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text);
static void draw_cursor(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text);
static void draw_scroll(struct te_app *app, struct te_canvas *canvas, const struct te_rect *text);
static void draw_status(struct te_app *app, struct te_canvas *canvas);
static void draw_message(struct te_app *app, struct te_canvas *canvas);
static void draw_dialog(struct te_app *app, struct te_canvas *canvas);
static void draw_chooser(struct te_app *app, struct te_canvas *canvas);
static void draw_button(struct te_app *app, struct te_canvas *canvas, const struct te_rect *rect, const char *label, int primary, int lit);
static void draw_chip(struct te_app *app, struct te_canvas *canvas, int x, int y, const char *text, uint32_t fill, uint32_t color);
static size_t draw_count(struct te_app *app);

/*
 * Draws the whole frame.
 */
void
te_draw(
	struct te_app *app,
	struct te_canvas *canvas)
{
	struct te_rect card;
	struct te_rect text;
	uint32_t fill;

	/* The ground: clear for glass (zdesktop draws the frosted desktop), a pale slate otherwise. */
	te_canvas_unclip(canvas);
	fill = DRAW_GROUND;
	if (app->glass)
		fill = 0x00000000U;
	te_canvas_fill(canvas, 0, 0, canvas->width, canvas->height, fill);

	/* The card. */
	te_app_card(app, &card);
	fill = DRAW_CARD_OPAQUE;
	if (app->glass)
		fill = DRAW_CARD;
	te_canvas_round(canvas, card.x, card.y, card.width, card.height, TE_CARD_RADIUS, fill);

	/* The text, its line numbers and its cursor, within the card's text area. */
	te_app_text_rect(app, &text);
	draw_rows(app, canvas, &text);
	draw_numbers(app, canvas, &text);
	draw_cursor(app, canvas, &text);
	te_canvas_unclip(canvas);

	/* The scroll bar, the status and a message. */
	draw_scroll(app, canvas, &text);
	draw_status(app, canvas);
	draw_message(app, canvas);

	/* A dialog or the chooser over everything. */
	if (app->dialog != TE_DIALOG_NONE)
		draw_dialog(app, canvas);
	if (app->choosing)
		draw_chooser(app, canvas);

	/* The frame is drawn. */
	app->dirty = 0;
}

/* Draws the rows in view: their backgrounds, the selection, the places found and the characters. */
static void
draw_rows(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text)
{
	size_t first;
	size_t row;
	size_t start;
	size_t end;
	int top;

	/* Only the text area is drawn in (the rows scroll under its edges). */
	te_canvas_clip(canvas, text->x, text->y, text->width, text->height);

	/* From the first row in view until below the area. */
	first = (size_t)(app->scroll_y / (double)app->row_height);
	for (row = first; row < app->layout.total; row++) {
		top = text->y + (int)((double)row * (double)app->row_height - app->scroll_y);
		if (top >= text->y + text->height)
			break;

		/* The row's bytes, and the row. */
		te_layout_row_range(&app->layout, &app->buffer, row, &start, &end);
		draw_row(app, canvas, text, row, top, start, end);
	}
}

/* Draws one row: the cursor's row tint, the selection, the places found, then its characters. */
static void
draw_row(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text,
	size_t row,
	int top,
	size_t start,
	size_t end)
{
	size_t selection_start;
	size_t selection_end;
	size_t cursor_row;
	size_t column;
	size_t line;
	size_t line_end;
	uint32_t color;
	int newline;

	/* The cursor's row is tinted when nothing is selected. */
	te_edit_selection(app, &selection_start, &selection_end);
	te_layout_place(&app->layout, &app->buffer, app->cursor, &cursor_row, &column);
	if (selection_start == selection_end && cursor_row == row)
		te_canvas_fill(canvas, text->x, top, text->width, app->row_height, DRAW_ROW_NOW);

	/* The selection's part in the row (with a cell for the newline it takes in). */
	if (selection_start < selection_end && selection_start <= end && selection_end > start) {
		line = te_buffer_line_of(&app->buffer, start);
		line_end = te_buffer_line_end(&app->buffer, line);
		newline = 0;
		if (end == line_end && selection_end > end)
			newline = 1;
		color = DRAW_SELECTION;
		if (!app->focused)
			color = DRAW_SELECTION_IDLE;
		draw_span(app, canvas, text, top, start, end, selection_start, selection_end, newline, color);
	}

	/* The places the find text occurs. */
	draw_matches(app, canvas, text, top, start, end);

	/* The characters. */
	draw_glyphs(app, canvas, text, top, start, end);
}

/*
 * Fills the cells of a row from one position to another (clipped to the
 * row), and a cell more for a newline taken in.
 */
static void
draw_span(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text,
	int top,
	size_t row_start,
	size_t row_end,
	size_t from,
	size_t to,
	int newline,
	uint32_t color)
{
	size_t left;
	size_t right;
	int x;

	/* The part within the row. */
	if (from < row_start)
		from = row_start;
	if (to > row_end)
		to = row_end;

	/* Its cells. */
	left = te_layout_columns_between(&app->layout, &app->buffer, row_start, from);
	right = te_layout_columns_between(&app->layout, &app->buffer, row_start, to);
	if (newline)
		right++;
	if (right <= left)
		return;

	/* The fill. */
	x = text->x + (int)((double)left * (double)app->cell - app->scroll_x);
	te_canvas_fill(canvas, x, top, (int)(right - left) * app->cell, app->row_height, color);
}

/* Marks the places in a row where the find text occurs (the one selected more strongly). */
static void
draw_matches(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text,
	int top,
	size_t start,
	size_t end)
{
	size_t position;
	size_t selection_start;
	size_t selection_end;
	uint32_t color;
	int match;

	/* Nothing to mark without a find text. */
	if (app->find_length == 0U)
		return;

	/* Each place in the row where it starts. */
	te_edit_selection(app, &selection_start, &selection_end);
	position = start;
	while (position < end) {
		match = te_find_at(&app->buffer, app->find, app->find_length, position);
		if (!match) {
			position++;
			continue;
		}

		/* The place, stronger when it is the one selected. */
		color = DRAW_MATCH;
		if (position == selection_start && position + app->find_length == selection_end)
			color = DRAW_MATCH_NOW;
		draw_span(app, canvas, text, top, start, end, position, position + app->find_length, 0, color);
		position += app->find_length;
	}
}

/* Draws the characters of a row in their cells (wide ones centred in two, controls as ^X). */
static void
draw_glyphs(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text,
	int top,
	size_t start,
	size_t end)
{
	uint32_t codepoint;
	size_t position;
	size_t next;
	size_t column;
	unsigned cells;
	int baseline;
	int x;
	int advance;

	/* The baseline in the row. */
	baseline = top + app->ascent;

	/* Each character, the pen at its cell. */
	column = 0;
	position = start;
	while (position < end) {
		codepoint = te_buffer_char(&app->buffer, position, &next);
		cells = te_layout_cells(codepoint, (unsigned)column, app->layout.tab);
		x = text->x + (int)((double)column * (double)app->cell - app->scroll_x);

		/* Past the right edge nothing more shows. */
		if (x > text->x + text->width)
			break;

		/* A control character as ^ and its letter, faint. */
		if (codepoint < 0x20U && codepoint != '\t') {
			te_text_draw_char(app->body, canvas, x, baseline, '^', app->pixels, DRAW_CONTROL);
			te_text_draw_char(app->body, canvas, x + app->cell, baseline, codepoint + 0x40U, app->pixels, DRAW_CONTROL);
		} else if (codepoint == 0x7fU) {
			te_text_draw_char(app->body, canvas, x, baseline, '^', app->pixels, DRAW_CONTROL);
			te_text_draw_char(app->body, canvas, x + app->cell, baseline, '?', app->pixels, DRAW_CONTROL);
		} else if (codepoint != '\t' && codepoint != ' ') {
			/* A character centred in its cells. */
			advance = te_text_advance(app->body, codepoint, app->pixels);
			if (cells == 2U && advance > 0)
				x += (2 * app->cell - advance) / 2;
			te_text_draw_char(app->body, canvas, x, baseline, codepoint, app->pixels, DRAW_TEXT);
		}

		/* The next cell. */
		column += cells;
		position = next;
	}
}

/* Draws the line numbers of the rows in view (each line's first row), the cursor's line darker. */
static void
draw_numbers(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text)
{
	char number[24];
	size_t first;
	size_t row;
	size_t line;
	size_t cursor_line;
	uint32_t color;
	int top;
	int width;
	int right;
	int length;

	/* Nothing without line numbers. */
	if (!app->line_numbers)
		return;

	/* The column left of the text, clipped to the text's height. */
	right = text->x - TE_GUTTER_PAD;
	te_canvas_clip(canvas, 0, text->y, text->x, text->height);
	cursor_line = te_buffer_line_of(&app->buffer, app->cursor);

	/* Each row in view that starts a line. */
	first = (size_t)(app->scroll_y / (double)app->row_height);
	for (row = first; row < app->layout.total; row++) {
		top = text->y + (int)((double)row * (double)app->row_height - app->scroll_y);
		if (top >= text->y + text->height)
			break;
		line = te_layout_line_of_row(&app->layout, row);
		if (app->layout.first_row[line] != row)
			continue;

		/* The number, right-aligned, darker on the cursor's line. */
		length = snprintf(number, sizeof(number), "%lu", (unsigned long)(line + 1U));
		width = te_text_width(app->body, number, (size_t)length, app->pixels, 0);
		color = DRAW_FAINT;
		if (line == cursor_line)
			color = DRAW_NUMBER_NOW;
		(void)te_text_draw(app->body, canvas, right - width, top + app->ascent, number, (size_t)length, app->pixels, 0, color);
	}
}

/* Draws the cursor (a bar, blinking) when the window has the keyboard; and text being composed. */
static void
draw_cursor(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text)
{
	size_t row;
	size_t column;
	uint64_t since;
	int top;
	int x;
	int width;
	int length;

	/* No cursor without the keyboard, or under a dialog or the chooser. */
	if (!app->focused || app->dialog != TE_DIALOG_NONE || app->choosing)
		return;

	/* Where the cursor is. */
	te_canvas_clip(canvas, text->x - 2, text->y, text->width + 4, text->height);
	te_layout_place(&app->layout, &app->buffer, app->cursor, &row, &column);
	top = text->y + (int)((double)row * (double)app->row_height - app->scroll_y);
	x = text->x + (int)((double)column * (double)app->cell - app->scroll_x);

	/* Text an input method is composing, underlined at the cursor. */
	length = (int)strlen(app->preedit);
	if (length > 0) {
		width = te_text_width(app->body, app->preedit, (size_t)length, app->pixels, 0);
		te_canvas_fill(canvas, x, top, width, app->row_height, DRAW_CARD_OPAQUE);
		(void)te_text_draw(app->body, canvas, x, top + app->ascent, app->preedit, (size_t)length, app->pixels, 0, DRAW_TEXT);
		te_canvas_fill(canvas, x, top + app->row_height - 2, width, 1, DRAW_TEXT);
		x += width;
	}

	/* The bar, in the shown half of the blink. */
	since = app->now - app->blink_start;
	if ((since / DRAW_BLINK_MS) % 2U != 0U)
		return;
	te_canvas_fill(canvas, x - 1, top + 1, 2, app->row_height - 2, DRAW_CURSOR);
}

/* Draws the scroll bar at the card's right edge when the text is taller than the view. */
static void
draw_scroll(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *text)
{
	struct te_rect card;
	double largest;
	double content;
	int thumb;
	int top;
	int x;

	/* A text that fits has no bar. */
	largest = te_app_max_scroll_y(app);
	if (largest <= 0.0)
		return;

	/* The thumb's length for the share in view, and its place for the offset. */
	content = (double)app->layout.total * (double)app->row_height;
	thumb = (int)((double)text->height * (double)text->height / content);
	if (thumb < DRAW_SCROLL_MIN)
		thumb = DRAW_SCROLL_MIN;
	top = text->y + (int)((double)(text->height - thumb) * app->scroll_y / largest);

	/* The thumb near the card's edge. */
	te_app_card(app, &card);
	x = card.x + card.width - 6 - DRAW_SCROLL_WIDTH;
	te_canvas_round(canvas, x, top, DRAW_SCROLL_WIDTH, thumb, DRAW_SCROLL_WIDTH / 2, DRAW_SCROLL);
}

/* Draws the status chip at the card's bottom right: the cursor's line and column, the selection, CR LF. */
static void
draw_status(
	struct te_app *app,
	struct te_canvas *canvas)
{
	struct te_rect card;
	char status[96];
	size_t line;
	size_t line_start;
	size_t column;
	size_t position;
	size_t selected;
	int width;
	int length;

	/* The cursor's line, and its column in characters. */
	line = te_buffer_line_of(&app->buffer, app->cursor);
	line_start = te_buffer_line_start(&app->buffer, line);
	column = 1;
	for (position = line_start; position < app->cursor; column++)
		position = te_buffer_next_char(&app->buffer, position);

	/* The words, with the selection's length and CR LF when there are. */
	length = snprintf(status, sizeof(status), "Ln %lu, Col %lu", (unsigned long)(line + 1U), (unsigned long)column);
	selected = draw_count(app);
	if (selected > 0U && length > 0 && (size_t)length < sizeof(status))
		length += snprintf(status + length, sizeof(status) - (size_t)length, "  (%lu selected)", (unsigned long)selected);
	if (app->file.crlf && length > 0 && (size_t)length < sizeof(status))
		(void)snprintf(status + length, sizeof(status) - (size_t)length, "  CRLF");

	/* The chip at the bottom right of the card. */
	te_app_card(app, &card);
	width = te_text_width(app->ui, status, strlen(status), DRAW_SMALL_PIXELS, 0) + 20;
	draw_chip(app, canvas, card.x + card.width - 14 - width, card.y + card.height - 12 - 24, status, DRAW_CHIP, DRAW_CHIP_TEXT);
}

/* Draws the message, when there is one, at the bottom middle of the card. */
static void
draw_message(
	struct te_app *app,
	struct te_canvas *canvas)
{
	struct te_rect card;
	int width;

	/* Nothing to say. */
	if (app->message[0] == '\0')
		return;

	/* A dark chip in the middle. */
	te_app_card(app, &card);
	width = te_text_width(app->ui, app->message, strlen(app->message), TE_UI_PIXELS, 0) + 24;
	draw_chip(app, canvas, card.x + (card.width - width) / 2, card.y + card.height - 12 - 24 - 36, app->message, DRAW_MESSAGE, DRAW_MESSAGE_TEXT);
}

/* Draws the dialog shown: a shade over the card, and a panel with its title, its words and its buttons. */
static void
draw_dialog(
	struct te_app *app,
	struct te_canvas *canvas)
{
	static const char *const unsaved[] = { "Save", "Don't Save", "Cancel" };
	static const char *const replace[] = { "Replace", "Cancel" };
	static const char *const changed[] = { "Overwrite", "Cancel" };
	static const char *const about[] = { "OK" };
	struct te_rect buttons[3];
	struct te_rect card;
	const char *const *labels;
	char title[TE_PATH_MAX + 64];
	const char *words;
	const char *name;
	int count;
	int index;

	/* The shade, and the panel. */
	te_canvas_blend(canvas, 0, 0, canvas->width, canvas->height, DRAW_SHADE);
	te_app_dialog_layout(app, &card, buttons, &count);
	te_canvas_round(canvas, card.x - 1, card.y - 1, card.width + 2, card.height + 2, 15, DRAW_PANEL_EDGE);
	te_canvas_round(canvas, card.x, card.y, card.width, card.height, 14, DRAW_PANEL);

	/* The dialog's words and buttons. */
	name = te_app_name(app);
	labels = about;
	words = "";
	switch (app->dialog) {
	case TE_DIALOG_UNSAVED:
		snprintf(title, sizeof(title), "Save changes to \"%s\"?", name);
		words = "Your changes will be lost if you don't save them.";
		labels = unsaved;
		break;
	case TE_DIALOG_REPLACE:
		name = strrchr(app->pending_path, '/');
		if (name == NULL)
			name = app->pending_path;
		else
			name++;
		snprintf(title, sizeof(title), "Replace \"%s\"?", name);
		words = "A file with this name already exists.";
		labels = replace;
		break;
	case TE_DIALOG_CHANGED:
		snprintf(title, sizeof(title), "\"%s\" changed on disk.", name);
		words = "Overwrite it with the text here?";
		labels = changed;
		break;
	default:
		snprintf(title, sizeof(title), "Text Editor");
		words = "A simple editor of plain text for Kei.";
		break;
	}

	/* The title and the words. */
	(void)te_text_draw_fit(app->ui, canvas, card.x + 22, card.y + 40, title, DRAW_TITLE_PIXELS, 1, card.width - 44, DRAW_TITLE);
	(void)te_text_draw_fit(app->ui, canvas, card.x + 22, card.y + 68, words, TE_UI_PIXELS, 0, card.width - 44, DRAW_BODY);

	/* The buttons, the first the default. */
	for (index = 0; index < count; index++)
		draw_button(app, canvas, &buttons[index], labels[index], index == 0, index == app->dialog_hover);
}

/* Draws the file chooser: its title and folder, the list, and the name field and buttons. */
static void
draw_chooser(
	struct te_app *app,
	struct te_canvas *canvas)
{
	struct te_rect buttons[2];
	struct te_rect card;
	struct te_rect list;
	struct te_rect field;
	const struct te_entry *entry;
	char label[300];
	size_t rows;
	size_t index;
	int top;
	int width;

	/* The shade, and the panel. */
	te_canvas_blend(canvas, 0, 0, canvas->width, canvas->height, DRAW_SHADE);
	te_app_chooser_layout(app, &card, &list, &rows);
	te_canvas_round(canvas, card.x - 1, card.y - 1, card.width + 2, card.height + 2, 15, DRAW_PANEL_EDGE);
	te_canvas_round(canvas, card.x, card.y, card.width, card.height, 14, DRAW_PANEL);

	/* The title, and the folder listed. */
	(void)te_text_draw(app->ui, canvas, card.x + 16, card.y + 24, app->chooser.saving ? "Save As" : "Open", app->chooser.saving ? 7U : 4U, DRAW_TITLE_PIXELS, 1, DRAW_TITLE);
	(void)te_text_draw_fit(app->ui, canvas, card.x + 16, card.y + 42, app->chooser.folder, DRAW_SMALL_PIXELS, 0, card.width - 32, DRAW_BODY);

	/* The rows in view, the selected one tinted; a folder's name ends in a slash. */
	te_canvas_clip(canvas, list.x, list.y, list.width, list.height);
	for (index = app->chooser.first; index < app->chooser.count && index < app->chooser.first + rows; index++) {
		entry = &app->chooser.entries[index];
		top = list.y + (int)(index - app->chooser.first) * TE_CHOOSER_ROW;
		if (index == app->chooser.selected)
			te_canvas_round(canvas, list.x, top + 1, list.width, TE_CHOOSER_ROW - 2, 7, DRAW_ROW_CHOSEN);
		snprintf(label, sizeof(label), "%s%s", entry->name, entry->folder ? "/" : "");
		(void)te_text_draw_fit(app->ui, canvas, list.x + 12, te_text_center(TE_UI_PIXELS, top, TE_CHOOSER_ROW), label, TE_UI_PIXELS, 0, list.width - 24, DRAW_TEXT);
	}
	te_canvas_unclip(canvas);

	/* The buttons at the bottom right: the default, and Cancel. */
	buttons[0].width = TE_BUTTON_WIDTH;
	buttons[0].height = TE_BUTTON_HEIGHT;
	buttons[0].x = card.x + card.width - 16 - TE_BUTTON_WIDTH;
	buttons[0].y = card.y + card.height - 14 - TE_BUTTON_HEIGHT;
	buttons[1] = buttons[0];
	buttons[1].x -= TE_BUTTON_WIDTH + 10;
	draw_button(app, canvas, &buttons[0], app->chooser.saving ? "Save" : "Open", 1, 0);
	draw_button(app, canvas, &buttons[1], "Cancel", 0, 0);

	/* Save As's name field, left of the buttons, with its text selected or a caret after it. */
	if (!app->chooser.saving)
		return;
	field.x = card.x + 16;
	field.y = buttons[0].y;
	field.width = buttons[1].x - 12 - field.x;
	field.height = TE_BUTTON_HEIGHT;
	te_canvas_round(canvas, field.x - 1, field.y - 1, field.width + 2, field.height + 2, 8, DRAW_FIELD_EDGE);
	te_canvas_round(canvas, field.x, field.y, field.width, field.height, 7, DRAW_FIELD);
	width = te_text_width(app->ui, app->chooser.name, app->chooser.name_length, TE_UI_PIXELS, 0);
	if (app->chooser.name_all && app->chooser.name_length > 0U)
		te_canvas_fill(canvas, field.x + 9, field.y + 7, width + 2, field.height - 14, DRAW_SELECTION);
	(void)te_text_draw(app->ui, canvas, field.x + 10, te_text_center(TE_UI_PIXELS, field.y, field.height), app->chooser.name, app->chooser.name_length, TE_UI_PIXELS, 0, DRAW_TEXT);
	if (!app->chooser.name_all)
		te_canvas_fill(canvas, field.x + 11 + width, field.y + 7, 2, field.height - 14, DRAW_CURSOR);
}

/* Draws a button: the default one blue, the others pale; lit under the pointer. */
static void
draw_button(
	struct te_app *app,
	struct te_canvas *canvas,
	const struct te_rect *rect,
	const char *label,
	int primary,
	int lit)
{
	uint32_t fill;
	uint32_t color;
	int width;

	/* The colours of its kind. */
	fill = DRAW_BUTTON;
	color = DRAW_BUTTON_TEXT;
	if (lit)
		fill = DRAW_BUTTON_LIT;
	if (primary) {
		fill = DRAW_PRIMARY;
		color = 0xffffffffU;
		if (lit)
			fill = DRAW_PRIMARY_LIT;
	}

	/* The pill and its label in the middle. */
	te_canvas_round(canvas, rect->x, rect->y, rect->width, rect->height, rect->height / 2, fill);
	width = te_text_width(app->ui, label, strlen(label), TE_UI_PIXELS, 0);
	(void)te_text_draw(app->ui, canvas, rect->x + (rect->width - width) / 2, te_text_center(TE_UI_PIXELS, rect->y, rect->height), label, strlen(label), TE_UI_PIXELS, 0, color);
}

/* Draws a chip: a rounded pill with a hairline edge and its words. */
static void
draw_chip(
	struct te_app *app,
	struct te_canvas *canvas,
	int x,
	int y,
	const char *text,
	uint32_t fill,
	uint32_t color)
{
	int width;
	int height;
	unsigned pixels;

	/* The chip's size for its words. */
	pixels = DRAW_SMALL_PIXELS;
	if (fill == DRAW_MESSAGE)
		pixels = TE_UI_PIXELS;
	width = te_text_width(app->ui, text, strlen(text), pixels, 0) + 20;
	height = 24;
	if (fill == DRAW_MESSAGE) {
		width += 4;
		height = 30;
	}

	/* The edge, the pill and the words. */
	te_canvas_round(canvas, x - 1, y - 1, width + 2, height + 2, height / 2 + 1, DRAW_CHIP_EDGE);
	te_canvas_round(canvas, x, y, width, height, height / 2, fill);
	(void)te_text_draw(app->ui, canvas, x + 10, te_text_center(pixels, y, height), text, strlen(text), pixels, 0, color);
}

/* Reports how many characters are selected, counted again only when the selection or the text changed. */
static size_t
draw_count(
	struct te_app *app)
{
	size_t start;
	size_t end;
	size_t position;
	size_t count;
	size_t length;

	/* Nothing selected. */
	te_edit_selection(app, &start, &end);
	if (end <= start)
		return 0;

	/* The same selection of the same text was counted already. */
	length = te_buffer_length(&app->buffer);
	if (app->counted_valid &&
	    app->counted_cursor == app->cursor &&
	    app->counted_anchor == app->anchor &&
	    app->counted_length == length)
		return app->counted;

	/* Each character. */
	count = 0;
	for (position = start; position < end; count++)
		position = te_buffer_next_char(&app->buffer, position);

	/* Kept for the next frames. */
	app->counted = count;
	app->counted_cursor = app->cursor;
	app->counted_anchor = app->anchor;
	app->counted_length = length;
	app->counted_valid = 1;

	/* Succeeded: the count. */
	return count;
}
