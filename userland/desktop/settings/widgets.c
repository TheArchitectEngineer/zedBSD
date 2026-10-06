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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
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

/* A switch's size. */
#define WIDGETS_TOGGLE_WIDTH	44
#define WIDGETS_TOGGLE_HEIGHT	24

/* A slider's height (the knob's room). */
#define WIDGETS_SLIDER_HEIGHT	28

/* A button's height, its side margins and its text size. */
#define WIDGETS_BUTTON_HEIGHT	32
#define WIDGETS_BUTTON_SIDE	16
#define WIDGETS_TEXT_BUTTON	14U

/* The A key (Ctrl+A selects a field's whole text). */
#define WIDGETS_KEY_A		30U

/*
 * The widgets' input every text field lives in (se_fields_open,
 * ws090-p007), the text the fields are drawn with, and a canvas of one
 * pixel a field's keys are carried out on at once (se_field_key).
 */
static struct kl_ui *widgets_fields;
static struct kl_text *widgets_text;
static uint32_t widgets_pixel;
static struct kl_canvas widgets_tiny;

static uint32_t widgets_field_id(const struct kl_field *field);

/*
 * Draws a page's header: its name large and its summary under it.
 * Returns the edge below the header.
 */
int
se_page_header(
	struct se_app *app,
	struct kl_canvas *canvas,
	const struct se_page *page,
	int x,
	int top,
	int width)
{
	struct kl_text_line title;
	struct kl_text_line summary;
	int baseline;

	/* The two lines' measurements. */
	kl_text_metrics(app->text, WIDGETS_TEXT_TITLE, &title);
	kl_text_metrics(app->text, WIDGETS_TEXT_SUMMARY, &summary);

	/* The name, bold. */
	baseline = top + title.ascent;
	(void)kl_text_draw_fit(app->text, canvas, x, baseline, kl_tr(page->name), WIDGETS_TEXT_TITLE, 1, width, SE_COLOR_TEXT);

	/* The summary under it. */
	baseline = top + title.height + 2 + summary.ascent;
	(void)kl_text_draw_fit(app->text, canvas, x, baseline, kl_tr(page->summary), WIDGETS_TEXT_SUMMARY, 0, width, SE_COLOR_TEXT_SECONDARY);

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
	struct kl_canvas *canvas,
	int x,
	int top,
	int width,
	int height,
	const char *title,
	const char *subtitle)
{
	struct kl_text_line line;
	int baseline;

	/* The card: a whiter veil with a bright edge. */
	kl_canvas_round(canvas, (float)x, (float)top, (float)width, (float)height, WIDGETS_CARD_RADIUS, SE_COLOR_CARD);
	kl_canvas_round_border(canvas, (float)x, (float)top, (float)width, (float)height, WIDGETS_CARD_RADIUS, 1.0f, SE_COLOR_CARD_EDGE);

	/* A card without a title starts at its margin. */
	if (title == NULL)
		return top + WIDGETS_CARD_PAD;

	/* The title, bold. */
	kl_text_metrics(app->text, WIDGETS_TEXT_CARD, &line);
	baseline = top + WIDGETS_CARD_PAD + line.ascent;
	(void)kl_text_draw_fit(app->text, canvas, x + WIDGETS_CARD_PAD + 2, baseline, title, WIDGETS_TEXT_CARD, 1, width - 2 * WIDGETS_CARD_PAD, SE_COLOR_TEXT);

	/* The subtitle under it, when there is one. */
	if (subtitle != NULL) {
		baseline += line.descent + 4;
		kl_text_metrics(app->text, WIDGETS_TEXT_CARD_SUB, &line);
		baseline += line.ascent;
		(void)kl_text_draw_fit(app->text, canvas, x + WIDGETS_CARD_PAD + 2, baseline, subtitle, WIDGETS_TEXT_CARD_SUB, 0, width - 2 * WIDGETS_CARD_PAD, SE_COLOR_TEXT_SECONDARY);
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
	struct kl_canvas *canvas,
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
	baseline = kl_text_center(WIDGETS_TEXT_ROW, top, WIDGETS_ROW_HEIGHT);
	left = x + WIDGETS_CARD_PAD + 2;
	right = x + width - WIDGETS_CARD_PAD;
	label_width = (int)((float)(right - left) * WIDGETS_LABEL_SHARE);

	/* The label, quiet, and the value, plain. */
	(void)kl_text_draw_fit(app->text, canvas, left, baseline, label, WIDGETS_TEXT_ROW, 0, label_width - 12, SE_COLOR_TEXT_SECONDARY);
	(void)kl_text_draw_fit(app->text, canvas, left + label_width, baseline, value, WIDGETS_TEXT_ROW, 0, right - left - label_width, SE_COLOR_TEXT);

	/* The line under the row, unless it is the last. */
	if (last == 0)
		kl_canvas_line(canvas, (float)left, (float)(top + WIDGETS_ROW_HEIGHT) - 0.5f, (float)right, (float)(top + WIDGETS_ROW_HEIGHT) - 0.5f, 1.0f, SE_COLOR_SEPARATOR);

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
	struct kl_canvas *canvas,
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
			kl_mark_raster(layer, pixels, layers[layer], pixels);
		layers_pixels = pixels;
	}

	/* Each layer in its colour, as opaque as asked. */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
		alpha = (uint32_t)((float)colours[layer][1] * opacity + 0.5f);
		kl_canvas_mask(canvas, x, y, layers[layer], (int)pixels, (int)pixels, pixels, KL_RGBA(colours[layer][0], alpha));
	}
}

/*
 * Draws a switch with its top left at (x, y): the accent with the knob on
 * the right when on, grey with the knob on the left when off, faded when
 * it does nothing.  An enabled switch is a page's control (index).
 */
void
se_toggle_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int y,
	int on,
	int enabled,
	int index)
{
	struct kl_rect rect;
	kl_color track;
	kl_color knob;
	float knob_x;

	/* The track's colour and the knob's place. */
	track = SE_COLOR_TRACK;
	knob_x = (float)x + 12.0f;
	if (on != 0) {
		track = SE_COLOR_ACCENT;
		knob_x = (float)(x + WIDGETS_TOGGLE_WIDTH) - 12.0f;
	}

	/* The knob is white. */
	knob = KL_RGB(0xffffff);

	/* A switch that does nothing is faded. */
	if (enabled == 0) {
		track = kl_color_mix(track, SE_COLOR_FADED, 0.6f);
		knob = KL_RGB(0xf6f7f9);
	}

	/* The track and the knob. */
	kl_canvas_round(canvas, (float)x, (float)y, (float)WIDGETS_TOGGLE_WIDTH, (float)WIDGETS_TOGGLE_HEIGHT, (float)WIDGETS_TOGGLE_HEIGHT * 0.5f, track);
	kl_canvas_circle(canvas, knob_x, (float)y + (float)WIDGETS_TOGGLE_HEIGHT * 0.5f, 9.5f, knob);

	/* An enabled switch is clickable. */
	if (enabled != 0) {
		rect.x = x - 4;
		rect.y = y - 4;
		rect.width = WIDGETS_TOGGLE_WIDTH + 8;
		rect.height = WIDGETS_TOGGLE_HEIGHT + 8;
		se_ui_hit(app, &rect, SE_HIT_CONTROL, index);
	}
}

/*
 * Draws a slider from (x, y), width long: a track filled in the accent up
 * to a fraction (0..1) and a white knob there, faded when it does nothing.
 * An enabled slider is a page's control (index) whose rectangle is given
 * back for a drag (se_slider_fraction).
 */
void
se_slider_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int y,
	int width,
	float fraction,
	int enabled,
	int index,
	struct kl_rect *rect)
{
	kl_color fill;
	kl_color knob;
	kl_color ring;
	float knob_x;
	float middle;

	/* Within the track. */
	if (fraction < 0.0f)
		fraction = 0.0f;
	if (fraction > 1.0f)
		fraction = 1.0f;

	/* The track, then the part up to the knob in the accent (faded when it does nothing). */
	middle = (float)y + (float)WIDGETS_SLIDER_HEIGHT * 0.5f;
	knob_x = (float)x + fraction * (float)width;
	fill = SE_COLOR_ACCENT;
	if (enabled == 0)
		fill = kl_color_mix(SE_COLOR_ACCENT, SE_COLOR_FADED, 0.6f);
	kl_canvas_round(canvas, (float)x, middle - 3.0f, (float)width, 6.0f, 3.0f, SE_COLOR_RAIL);
	kl_canvas_round(canvas, (float)x, middle - 3.0f, knob_x - (float)x, 6.0f, 3.0f, fill);

	/* The knob, with a quiet ring round it; greyed when the slider does nothing (ws089-p012 C4), so it does not look as if it could be dragged. */
	knob = KL_RGB(0xffffff);
	ring = KL_RGBA(0x5a6b85, 50);
	if (enabled == 0) {
		knob = KL_RGB(0xeef1f5);
		ring = KL_RGBA(0x5a6b85, 24);
	}

	/* The ring, then the knob in it. */
	kl_canvas_circle(canvas, knob_x, middle, 11.0f, ring);
	kl_canvas_circle(canvas, knob_x, middle, 10.0f, knob);

	/* The whole track takes presses and drags (a little taller than it looks). */
	rect->x = x - 12;
	rect->y = y;
	rect->width = width + 24;
	rect->height = WIDGETS_SLIDER_HEIGHT;
	if (enabled != 0)
		se_ui_hit(app, rect, SE_HIT_CONTROL, index);
}

/*
 * Reports where along a slider a pointer at x is, from 0 (its left end)
 * to 1 (its right end).
 */
float
se_slider_fraction(
	const struct kl_rect *rect,
	int x)
{
	float fraction;

	/* The track lies 12 pixels inside the rectangle at each end. */
	fraction = (float)(x - rect->x - 12) / (float)(rect->width - 24);

	/* Before the left end. */
	if (fraction < 0.0f)
		return 0.0f;

	/* After the right end. */
	if (fraction > 1.0f)
		return 1.0f;

	/* On the track. */
	return fraction;
}

/*
 * Reports how wide a button with a label is.
 */
int
se_button_width(
	struct se_app *app,
	const char *label)
{
	int text;

	/* The label and the margins. */
	text = kl_text_width(app->text, label, strlen(label), WIDGETS_TEXT_BUTTON, 1);

	/* The button's width. */
	return text + 2 * WIDGETS_BUTTON_SIDE;
}

/*
 * Draws a button with its top left at (x, y): the accent with white text
 * when primary, else a white one with an edge; darker under the pointer,
 * faded when it does nothing.  An enabled button is a page's control
 * (index).  Returns its width.
 */
int
se_button_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int y,
	const char *label,
	int primary,
	int enabled,
	int index)
{
	struct kl_rect rect;
	kl_color ground;
	kl_color edge;
	kl_color ink;
	int width;
	int lit;

	/* The button's size. */
	width = se_button_width(app, label);
	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = WIDGETS_BUTTON_HEIGHT;

	/* Its colours: the accent for the primary one, the control's ground for the others. */
	ground = SE_COLOR_CONTROL;
	edge = SE_COLOR_CONTROL_EDGE;
	ink = SE_COLOR_TEXT;
	if (primary != 0) {
		ground = SE_COLOR_ACCENT;
		edge = SE_COLOR_ACCENT;
		ink = KL_RGB(0xffffff);
	}

	/* Darker under the pointer, faded when it does nothing. */
	lit = se_ui_lit(app, SE_HIT_CONTROL, index);
	if (enabled != 0 && lit != 0)
		ground = kl_color_mix(ground, SE_COLOR_PRESSED, 0.08f);
	if (enabled == 0) {
		ground = kl_color_mix(ground, SE_COLOR_FADED, 0.6f);
		ink = SE_COLOR_TEXT_FAINT;
	}

	/* The button and its label, centred. */
	kl_canvas_round(canvas, (float)x, (float)y, (float)width, (float)WIDGETS_BUTTON_HEIGHT, 8.0f, ground);
	kl_canvas_round_border(canvas, (float)x, (float)y, (float)width, (float)WIDGETS_BUTTON_HEIGHT, 8.0f, 1.0f, edge);
	(void)kl_text_draw(app->text, canvas, x + WIDGETS_BUTTON_SIDE, kl_text_center(WIDGETS_TEXT_BUTTON, y, WIDGETS_BUTTON_HEIGHT), label, strlen(label), WIDGETS_TEXT_BUTTON, 1, ink);

	/* An enabled button is clickable. */
	if (enabled != 0)
		se_ui_hit(app, &rect, SE_HIT_CONTROL, index);

	/* The button's width. */
	return width;
}

/*
 * Draws a button that is a picture alone, WIDGETS_BUTTON_HEIGHT square with
 * its top left at (x, y): white within a thin edge, darker under the
 * pointer, the glyph in the middle.  It is a page's control (index).
 */
void
se_icon_button_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	int x,
	int y,
	unsigned glyph,
	int index)
{
	struct kl_rect rect;
	kl_color ground;
	int lit;

	/* The button's square. */
	rect.x = x;
	rect.y = y;
	rect.width = WIDGETS_BUTTON_HEIGHT;
	rect.height = WIDGETS_BUTTON_HEIGHT;

	/* The control's ground, darker (lighter in the dark appearance) under the pointer or while pressed. */
	ground = SE_COLOR_CONTROL;
	lit = se_ui_lit(app, SE_HIT_CONTROL, index);
	if (lit != 0)
		ground = kl_color_mix(ground, SE_COLOR_PRESSED, 0.08f);

	/* The square, its edge and the picture in the middle. */
	kl_canvas_round(canvas, (float)x, (float)y, (float)WIDGETS_BUTTON_HEIGHT, (float)WIDGETS_BUTTON_HEIGHT, 8.0f, ground);
	kl_canvas_round_border(canvas, (float)x, (float)y, (float)WIDGETS_BUTTON_HEIGHT, (float)WIDGETS_BUTTON_HEIGHT, 8.0f, 1.0f, SE_COLOR_CONTROL_EDGE);
	se_glyph_draw(canvas, glyph, (float)x + 6.0f, (float)y + 6.0f, (float)(WIDGETS_BUTTON_HEIGHT - 12), SE_COLOR_TEXT);

	/* It is clickable. */
	se_ui_hit(app, &rect, SE_HIT_CONTROL, index);
}

/*
 * Draws a status dot (green for connected, grey for not) centred at (cx, cy).
 */
void
se_dot_draw(
	struct kl_canvas *canvas,
	float cx,
	float cy,
	kl_color color)
{
	/* A small filled circle. */
	kl_canvas_circle(canvas, cx, cy, 4.5f, color);
}

/*
 * Draws a signal's four bars with their bottom left at (x, y): as many
 * full as the strength (dBm) earns, the rest faint.
 */
void
se_signal_draw(
	struct kl_canvas *canvas,
	float x,
	float y,
	int rssi,
	kl_color color)
{
	kl_color bar;
	float height;
	int bars;
	int index;

	/* The bars the strength earns: four above -55 dBm, one below -75. */
	bars = 1;
	if (rssi > -75)
		bars = 2;
	if (rssi > -65)
		bars = 3;
	if (rssi > -55)
		bars = 4;

	/* Each bar, taller to the right. */
	for (index = 0; index < 4; index++) {
		bar = color;
		if (index >= bars)
			bar = KL_RGBA(0x8a96aa, 90);
		height = 4.0f + 3.5f * (float)index;
		kl_canvas_round(canvas, x + 5.0f * (float)index, y - height, 3.0f, height, 1.0f, bar);
	}
}

/*
 * Writes a number of bytes as text in the largest unit under a thousand
 * of it (B, KB, MB, GB).
 */
void
se_bytes_text(
	uint64_t bytes,
	char *text,
	size_t size)
{
	static const char *const units[] = { "KB", "MB", "GB", "TB" };
	double value;
	unsigned unit;

	/* Under a kilobyte, whole bytes. */
	if (bytes < 1000U) {
		(void)snprintf(text, size, "%u B", (unsigned)bytes);
		return;
	}

	/* The largest unit the value is at least one of. */
	value = (double)bytes / 1000.0;
	unit = 0;
	while (value >= 1000.0 && unit + 1U < sizeof(units) / sizeof(units[0])) {
		value /= 1000.0;
		unit++;
	}

	/* One decimal under ten, none above. */
	if (value < 10.0) {
		(void)snprintf(text, size, "%.1f %s", value, units[unit]);
	} else {
		(void)snprintf(text, size, "%.0f %s", value, units[unit]);
	}
}

/*
 * Opens the widgets' input the text fields live in (ws090-p007): every
 * field of every page is libkeiland's kl_field under this one kl_ui.
 * Returns 0, or ENOMEM.
 */
int
se_fields_open(
	struct kl_text *text)
{
	int error;

	/* Made once. */
	if (widgets_fields != NULL)
		return 0;
	widgets_fields = kl_ui_create();
	if (widgets_fields == NULL)
		return ENOMEM;

	/* The text, and the one pixel the keys are carried out on. */
	widgets_text = text;
	error = kl_canvas_init(&widgets_tiny, &widgets_pixel, 1U, 1, 1);
	if (error != 0) {
		se_fields_close();
		return error;
	}

	/* Succeeded: the fields can be drawn. */
	return 0;
}

/*
 * Closes the fields' input.
 */
void
se_fields_close(void)
{
	/* The input and the pixel's canvas, and they are forgotten. */
	kl_ui_destroy(widgets_fields);
	widgets_fields = NULL;
	kl_canvas_release(&widgets_tiny);
}

/*
 * Begins a frame of the fields' input (before the pages are drawn).
 */
void
se_fields_begin(
	uint64_t now_us)
{
	/* Nothing without the input. */
	if (widgets_fields == NULL)
		return;
	kl_ui_begin(widgets_fields, now_us);
}

/*
 * Ends a frame of the fields' input (after the pages are drawn); the keys
 * no field took go.  Returns 1 while a field wants another frame.
 */
int
se_fields_end(
	uint64_t now_us)
{
	struct kl_event event;
	int moving;
	int taken;

	/* Nothing without the input. */
	if (widgets_fields == NULL)
		return 0;

	/* The frame, and what no field took (the pages took their own keys before). */
	moving = kl_ui_end(widgets_fields, now_us);
	for (;;) {
		taken = kl_ui_take(widgets_fields, &event);
		if (!taken)
			break;
	}

	/* Reports whether a field moves. */
	return moving;
}

/*
 * Gives the fields the pointer and an input method's text: the pointer's
 * moves and the main button (a click puts a field's caret), and the
 * text an input method sends for the field with the keyboard.
 */
void
se_fields_input(
	const struct se_event *event)
{
	struct kl_window_event text;

	/* Nothing without the input. */
	if (widgets_fields == NULL)
		return;

	/* Each kind of input the fields take. */
	switch (event->type) {
	case SE_EVENT_MOTION:
		(void)kl_ui_pointer_motion(widgets_fields, (double)event->x, (double)event->y);
		return;
	case SE_EVENT_BUTTON:
		(void)kl_ui_pointer_motion(widgets_fields, (double)event->x, (double)event->y);
		if (event->button == SE_BUTTON_LEFT)
			(void)kl_ui_pointer_button(widgets_fields, event->pressed, event->time * 1000U);
		return;
	case SE_EVENT_TEXT:
	case SE_EVENT_TEXT_DELETE:
	case SE_EVENT_PREEDIT:
		break;
	default:
		return;
	}

	/* An input method's text, as the window gave it. */
	memset(&text, 0, sizeof(text));
	text.kind = KL_WINDOW_TEXT_COMMIT;
	if (event->type == SE_EVENT_TEXT_DELETE)
		text.kind = KL_WINDOW_TEXT_DELETE;
	else if (event->type == SE_EVENT_PREEDIT)
		text.kind = KL_WINDOW_TEXT_PREEDIT;
	memcpy(text.text, event->text, sizeof(text.text));
	text.text[sizeof(text.text) - 1U] = '\0';
	text.before = event->before;
	text.begin = -1;
	text.end = -1;
	(void)kl_ui_text(widgets_fields, &text);
}

/*
 * Reports the fields' input, for the window's text input (main.c,
 * kl_ui_window_text); NULL before se_fields_open.
 */
struct kl_ui *
se_fields_ui(void)
{
	/* Succeeded: the input, or none. */
	return widgets_fields;
}

/*
 * Types a key press into a text field (it gets the keyboard): a character,
 * Left, Right, Home, End, Backspace, Delete, Ctrl+A.  Enter, Esc and Tab
 * are the pages' own.  The field takes it in the next frame.  Returns 1
 * when the key is the field's.
 */
int
se_field_key(
	struct kl_field *field,
	const struct se_event *event)
{
	struct kl_style style;
	struct kl_rect rect;
	uint32_t character;
	uint32_t id;
	int editing;

	/* Nothing without the input; Alt and Super are commands, Control's only key is A. */
	if (widgets_fields == NULL)
		return 0;
	if ((event->modifiers & (SE_MOD_ALT | SE_MOD_SUPER)) != 0U)
		return 0;
	if ((event->modifiers & SE_MOD_CTRL) != 0U && event->key != WIDGETS_KEY_A)
		return 0;

	/* The editing keys, or a key that types a character. */
	editing = 0;
	switch (event->key) {
	case KL_KEY_LEFT:
	case KL_KEY_RIGHT:
	case KL_KEY_HOME:
	case KL_KEY_END:
	case KL_KEY_BACKSPACE:
	case KL_KEY_DELETE:
	case WIDGETS_KEY_A:
		editing = 1;
		break;
	default:
		break;
	}
	character = kl_key_character(event->key, event->modifiers);
	if (!editing && character == 0U)
		return 0;

	/*
	 * The field has the keyboard, and the key is carried out at once in a
	 * frame of the field alone on one pixel, so that the page reads the
	 * new text before its next key (Enter right after the typing).
	 */
	id = widgets_field_id(field);
	kl_ui_set_focus(widgets_fields, id, 0U);
	(void)kl_ui_key(widgets_fields, event->key, 1, event->modifiers);
	memset(&style, 0, sizeof(style));
	style.canvas = &widgets_tiny;
	style.text = widgets_text;
	style.theme = kl_theme_default();
	rect.x = 0;
	rect.y = 0;
	rect.width = 1;
	rect.height = 1;
	kl_ui_begin(widgets_fields, event->time * 1000U);
	(void)kl_field(widgets_fields, &style, id, &rect, field, NULL);
	(void)kl_ui_end(widgets_fields, event->time * 1000U);

	/* Succeeded: the field's key. */
	return 1;
}

/*
 * Draws a text field in a rectangle (libkeiland's: white, the accent's
 * edge with the keyboard, the caret, the selection, an input method's
 * composed text) with the keyboard when focused says the page gave it, of
 * a kind (SE_FIELD_*: a secret one as dots and, like a plain one, without
 * an input method), a placeholder while it is empty.  Returns what happened
 * (KL_FIELD_*).
 */
unsigned
se_field_draw(
	struct se_app *app,
	struct kl_canvas *canvas,
	struct kl_field *field,
	const struct kl_rect *rect,
	const char *placeholder,
	unsigned kind,
	int focused)
{
	struct kl_style style;
	uint32_t id;
	int has;
	unsigned changes;

	/* Nothing without the input. */
	if (widgets_fields == NULL)
		return 0U;

	/* Its kind, and the page's keyboard decides the field's. */
	field->secret = 0;
	if (kind == SE_FIELD_SECRET)
		field->secret = 1;
	field->plain = 0;
	if (kind == SE_FIELD_PLAIN)
		field->plain = 1;
	id = widgets_field_id(field);
	has = kl_ui_has_focus(widgets_fields, id, 0U);
	if (focused && !has)
		kl_ui_set_focus(widgets_fields, id, 0U);
	if (!focused && has)
		kl_ui_clear_focus(widgets_fields);

	/* libkeiland's field in Settings' canvas and text. */
	memset(&style, 0, sizeof(style));
	style.canvas = canvas;
	style.text = app->text;
	style.theme = kl_theme_default();
	changes = kl_field(widgets_fields, &style, id, rect, field, placeholder);
	return changes;
}

/* Reports a field's widget ID: where it lives in the program (each field is a part of the application's state, kept for its life). */
static uint32_t
widgets_field_id(
	const struct kl_field *field)
{
	uintptr_t place;

	/* Its address's low bits, never 0. */
	place = (uintptr_t)field;
	return (uint32_t)(place & 0xffffffffU) | 1U;
}

/*
 * Empties a text field, wiping what was typed (a key's text).
 */
void
se_field_clear(
	struct kl_field *field)
{
	volatile char *byte;
	size_t index;

	/* Every byte, through a volatile pointer so that the wipe stays. */
	byte = field->text;
	for (index = 0; index < sizeof(field->text); index++)
		byte[index] = '\0';

	/* Nothing typed, the caret at the start. */
	field->length = 0;
	field->caret = 0;
	field->anchor = 0;
	field->scroll = 0;
}
