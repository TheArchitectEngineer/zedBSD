/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The toolbar of Notes: a band across the top of the window, drawn on the
 * CPU into the picture the renderer shows (design-input-notes.md section
 * 5.2).
 *
 * From the left: the tools (Pen, Marker, Eraser), five colours, three
 * widths, Undo and Redo, the page's number between the previous and next
 * page buttons, a new page, Save, and a status line at the right.  The
 * labels are drawn with libtruetype from the desktop's font; without the
 * font the buttons are drawn without them and still work.  Only ASCII is
 * drawn (the labels are ASCII; other characters of a status show as '?').
 */

#include "app.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <truetype.h>
#include <unistd.h>

/* The labels' size in pixels. */
#define UI_FONT_PIXELS		15U

/* The largest font file read, in bytes. */
#define UI_FONT_MAX		(32U * 1024U * 1024U)

/* The largest glyph drawn, in pixels a side. */
#define UI_GLYPH_MAX		64U

/* The buttons' height, their top, and the gap between groups, in pixels. */
#define UI_BUTTON_HEIGHT	36
#define UI_BUTTON_TOP		8
#define UI_GAP			14
#define UI_PADDING		12

/* The toolbar's colours, as 0xRRGGBB. */
#define UI_BACKGROUND		0xf3f4f6U
#define UI_BORDER		0xd1d5dbU
#define UI_TEXT			0x1f2937U
#define UI_TEXT_DISABLED	0x9ca3afU
#define UI_SELECTED		0xdbeafeU
#define UI_SELECTED_TEXT	0x1d4ed8U
#define UI_STATUS_TEXT		0x4b5563U

static void ui_fill(struct notes_ui *ui, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t rgb);
static void ui_frame(struct notes_ui *ui, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t rgb);
static void ui_blend(struct notes_ui *ui, int32_t x, int32_t y, uint32_t rgb, unsigned coverage);
static void ui_dot(struct notes_ui *ui, float cx, float cy, float radius, uint32_t rgb);
static int32_t ui_text_width(struct notes_ui *ui, const char *text);
static void ui_text(struct notes_ui *ui, int32_t x, int32_t y, const char *text, uint32_t rgb);
static int32_t ui_label_button(struct notes_ui *ui, int32_t x, const char *label, uint32_t action, uint32_t chosen, int enabled);
static void ui_add_button(struct notes_ui *ui, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t action);
static uint32_t ui_codepoint(char byte);

/* The pen's colours as 0xRRGGBBAA: black, blue, red, green, orange. */
static const uint32_t ui_pen_colors[NOTES_COLORS] = {
	0x1f1f1fffU, 0x1d4ed8ffU, 0xdc2626ffU, 0x15803dffU, 0xea580cffU
};

/* The highlighter's colours, translucent: yellow, blue, pink, green, orange. */
static const uint32_t ui_marker_colors[NOTES_COLORS] = {
	0xfacc1566U, 0x60a5fa66U, 0xf472b666U, 0x4ade8066U, 0xfb923c66U
};

/* The pen's and the highlighter's widths in points: fine, medium, bold. */
static const float ui_pen_widths[NOTES_WIDTHS] = { 1.5f, 3.0f, 6.0f };
static const float ui_marker_widths[NOTES_WIDTHS] = { 10.0f, 16.0f, 24.0f };

/*
 * Opens the font the labels are drawn with.
 *
 * Returns 0, or an errno value; the toolbar works without its font, only
 * without labels.
 */
int
notes_ui_open(
	struct notes_ui *ui,
	const char *font_path)
{
	struct truetype_metrics metrics;
	struct stat status;
	ssize_t got;
	size_t done;
	int descriptor;
	int error;

	/* Nothing is open yet. */
	memset(ui, 0, sizeof(*ui));

	/* The font file, which must be of a sane size. */
	descriptor = open(font_path, O_RDONLY);
	if (descriptor < 0)
		return errno;
	error = fstat(descriptor, &status);
	if (error != 0 ||
	    status.st_size <= 0 ||
	    (unsigned long)status.st_size > UI_FONT_MAX) {
		(void)close(descriptor);
		return EINVAL;
	}

	/* Reads it whole; the face uses the bytes for as long as it is open. */
	ui->font_data = malloc((size_t)status.st_size);
	if (ui->font_data == NULL) {
		(void)close(descriptor);
		return ENOMEM;
	}

	/* Reads it all. */
	done = 0;
	while (done < (size_t)status.st_size) {
		got = read(descriptor, (unsigned char *)ui->font_data + done, (size_t)status.st_size - done);
		if (got <= 0)
			break;
		done += (size_t)got;
	}

	/* The file is not needed any more. */
	(void)close(descriptor);
	ui->font_size = done;

	/* The face at the labels' size. */
	error = truetype_open(ui->font_data, ui->font_size, 0U, &ui->face);
	if (error != 0) {
		notes_ui_close(ui);
		return error;
	}

	/* The labels' size. */
	error = truetype_set_pixel_size(ui->face, UI_FONT_PIXELS);
	if (error != 0) {
		notes_ui_close(ui);
		return error;
	}

	/* The ascent, which places the baseline in a button. */
	error = truetype_metrics(ui->face, &metrics);
	if (error != 0) {
		notes_ui_close(ui);
		return error;
	}

	/* Succeeded: labels can be drawn. */
	ui->font_pixels = UI_FONT_PIXELS;
	ui->ascent = metrics.ascent;
	return 0;
}

/*
 * Closes the font.
 */
void
notes_ui_close(
	struct notes_ui *ui)
{
	/* The face before the bytes it reads. */
	if (ui->face != NULL)
		truetype_close(ui->face);
	free(ui->font_data);
	memset(ui, 0, sizeof(*ui));
}

/*
 * Draws the toolbar into a picture (B8G8R8A8 rows) and lays out its buttons.
 */
void
notes_ui_draw(
	struct notes_ui *ui,
	unsigned char *pixels,
	size_t pitch,
	uint32_t width,
	uint32_t height,
	const struct notes_ui_state *state)
{
	char page_text[48];
	const uint32_t *colors;
	const float *widths;
	int32_t x;
	int32_t text_width;
	unsigned index;
	float radius;
	int selected;
	int earlier;
	int later;

	/* The picture being drawn, and no buttons yet. */
	ui->pixels = pixels;
	ui->pitch = pitch;
	ui->width = width;
	ui->height = height;
	ui->button_count = 0;
	if (pixels == NULL)
		return;

	/* The band and the line under it. */
	ui_fill(ui, 0, 0, (int32_t)width, (int32_t)height, UI_BACKGROUND);
	ui_fill(ui, 0, (int32_t)height - 1, (int32_t)width, 1, UI_BORDER);

	/* The tools, the chosen one marked. */
	x = UI_PADDING;
	x = ui_label_button(ui, x, "Pen", NOTES_ACTION_PEN, state->tool, 1);
	x = ui_label_button(ui, x, "Marker", NOTES_ACTION_HIGHLIGHTER, state->tool, 1);
	x = ui_label_button(ui, x, "Eraser", NOTES_ACTION_ERASER, state->tool, 1);
	x += UI_GAP;

	/* The colours of the pen, or of the highlighter while it is chosen. */
	colors = ui_pen_colors;
	widths = ui_pen_widths;
	if (state->tool == NOTES_ACTION_HIGHLIGHTER) {
		colors = ui_marker_colors;
		widths = ui_marker_widths;
	}

	/* Each colour, a dot. */
	for (index = 0; index < NOTES_COLORS; index++) {
		/* The chosen colour has a frame. */
		selected = 0;
		if (index == state->color)
			selected = 1;
		if (selected)
			ui_frame(ui, x, UI_BUTTON_TOP, UI_BUTTON_HEIGHT, UI_BUTTON_HEIGHT, UI_SELECTED_TEXT);
		ui_dot(ui, (float)x + (float)UI_BUTTON_HEIGHT / 2.0f, (float)UI_BUTTON_TOP + (float)UI_BUTTON_HEIGHT / 2.0f, 11.0f, colors[index] >> 8);
		ui_add_button(ui, x, UI_BUTTON_TOP, UI_BUTTON_HEIGHT, UI_BUTTON_HEIGHT, NOTES_ACTION_COLOR + index);
		x += UI_BUTTON_HEIGHT + 2;
	}

	/* A gap before the next group. */
	x += UI_GAP;

	/* The widths, each a dot of its size (the chosen one framed). */
	for (index = 0; index < NOTES_WIDTHS; index++) {
		selected = 0;
		if (index == state->width)
			selected = 1;
		if (selected)
			ui_fill(ui, x, UI_BUTTON_TOP, UI_BUTTON_HEIGHT, UI_BUTTON_HEIGHT, UI_SELECTED);
		radius = 2.0f + (float)index * 2.5f;
		if (widths[index] > 8.0f)
			radius = 3.0f + (float)index * 3.0f;
		ui_dot(ui, (float)x + (float)UI_BUTTON_HEIGHT / 2.0f, (float)UI_BUTTON_TOP + (float)UI_BUTTON_HEIGHT / 2.0f, radius, UI_TEXT);
		ui_add_button(ui, x, UI_BUTTON_TOP, UI_BUTTON_HEIGHT, UI_BUTTON_HEIGHT, NOTES_ACTION_WIDTH + index);
		x += UI_BUTTON_HEIGHT + 2;
	}

	/* A gap before the next group. */
	x += UI_GAP;

	/* Undo and Redo, pale when there is nothing to take back or make again. */
	x = ui_label_button(ui, x, "Undo", NOTES_ACTION_UNDO, NOTES_ACTION_NONE, state->can_undo);
	x = ui_label_button(ui, x, "Redo", NOTES_ACTION_REDO, NOTES_ACTION_NONE, state->can_redo);
	x += UI_GAP;

	/* Whether there is a page before the current one, and one after. */
	earlier = 0;
	if (state->page > 0U)
		earlier = 1;
	later = 0;
	if (state->page + 1U < state->page_count)
		later = 1;

	/* The pages: previous, "3 / 12", next, and a new page. */
	x = ui_label_button(ui, x, "<", NOTES_ACTION_PREVIOUS_PAGE, NOTES_ACTION_NONE, earlier);
	(void)snprintf(page_text, sizeof(page_text), "%lu / %lu", (unsigned long)(state->page + 1U), (unsigned long)state->page_count);
	text_width = ui_text_width(ui, page_text);
	ui_text(ui, x + 4, UI_BUTTON_TOP, page_text, UI_TEXT);
	x += text_width + 8;
	x = ui_label_button(ui, x, ">", NOTES_ACTION_NEXT_PAGE, NOTES_ACTION_NONE, later);
	x = ui_label_button(ui, x, "+ Page", NOTES_ACTION_NEW_PAGE, NOTES_ACTION_NONE, 1);
	x += UI_GAP;

	/* Save. */
	x = ui_label_button(ui, x, "Save", NOTES_ACTION_SAVE, NOTES_ACTION_NONE, 1);

	/* The status at the right, when there is room for it. */
	if (state->status != NULL && state->status[0] != '\0') {
		text_width = ui_text_width(ui, state->status);
		if (x + UI_GAP + text_width + UI_PADDING <= (int32_t)width)
			ui_text(ui, (int32_t)width - UI_PADDING - text_width, UI_BUTTON_TOP, state->status, UI_STATUS_TEXT);
	}
}

/*
 * Tells the action of the button at a point of the toolbar (NOTES_ACTION_NONE for none).
 */
uint32_t
notes_ui_hit(
	const struct notes_ui *ui,
	float x,
	float y)
{
	const struct notes_button *button;
	unsigned index;

	/* The first button whose rectangle holds the point. */
	for (index = 0; index < ui->button_count; index++) {
		button = &ui->buttons[index];
		if (x < (float)button->x || x >= (float)(button->x + button->width))
			continue;
		if (y < (float)button->y || y >= (float)(button->y + button->height))
			continue;
		return button->action;
	}

	/* No button there. */
	return NOTES_ACTION_NONE;
}

/*
 * Gives a tool's colour of an index, as 0xRRGGBBAA.
 */
uint32_t
notes_ui_color(
	unsigned tool,
	unsigned index)
{
	/* An index past the palette is its first colour. */
	if (index >= NOTES_COLORS)
		index = 0;

	/* The highlighter's translucent colours. */
	if (tool == NOTES_TOOL_HIGHLIGHTER)
		return ui_marker_colors[index];

	/* The pen's. */
	return ui_pen_colors[index];
}

/*
 * Gives a tool's width of an index, in points.
 */
float
notes_ui_width(
	unsigned tool,
	unsigned index)
{
	/* An index past the widths is the middle one. */
	if (index >= NOTES_WIDTHS)
		index = 1;

	/* The highlighter's wide ones. */
	if (tool == NOTES_TOOL_HIGHLIGHTER)
		return ui_marker_widths[index];

	/* The pen's. */
	return ui_pen_widths[index];
}

/* Fills a rectangle of the picture with an opaque colour, clipped to the picture. */
static void
ui_fill(
	struct notes_ui *ui,
	int32_t x,
	int32_t y,
	int32_t width,
	int32_t height,
	uint32_t rgb)
{
	unsigned char *row;
	int32_t left;
	int32_t top;
	int32_t right;
	int32_t bottom;
	int32_t column;

	/* The rectangle inside the picture. */
	left = x;
	top = y;
	right = x + width;
	bottom = y + height;
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > (int32_t)ui->width)
		right = (int32_t)ui->width;
	if (bottom > (int32_t)ui->height)
		bottom = (int32_t)ui->height;

	/* Each pixel, blue, green, red, alpha. */
	for (; top < bottom; top++) {
		row = ui->pixels + (size_t)top * ui->pitch;
		for (column = left; column < right; column++) {
			row[column * 4 + 0] = (unsigned char)(rgb & 0xffU);
			row[column * 4 + 1] = (unsigned char)((rgb >> 8) & 0xffU);
			row[column * 4 + 2] = (unsigned char)((rgb >> 16) & 0xffU);
			row[column * 4 + 3] = 0xffU;
		}
	}
}

/* Draws a rectangle's outline, two pixels thick. */
static void
ui_frame(
	struct notes_ui *ui,
	int32_t x,
	int32_t y,
	int32_t width,
	int32_t height,
	uint32_t rgb)
{
	/* The four sides. */
	ui_fill(ui, x, y, width, 2, rgb);
	ui_fill(ui, x, y + height - 2, width, 2, rgb);
	ui_fill(ui, x, y, 2, height, rgb);
	ui_fill(ui, x + width - 2, y, 2, height, rgb);
}

/* Blends a colour over one pixel by a coverage from 0 to 255. */
static void
ui_blend(
	struct notes_ui *ui,
	int32_t x,
	int32_t y,
	uint32_t rgb,
	unsigned coverage)
{
	unsigned char *pixel;
	unsigned channel;
	unsigned value;

	/* A pixel outside the picture is not drawn. */
	if (x < 0 ||
	    y < 0 ||
	    x >= (int32_t)ui->width ||
	    y >= (int32_t)ui->height)
		return;

	/* Each of blue, green and red moves toward the colour by the coverage. */
	pixel = ui->pixels + (size_t)y * ui->pitch + (size_t)x * 4U;
	for (channel = 0; channel < 3U; channel++) {
		value = (rgb >> (channel * 8U)) & 0xffU;
		pixel[channel] = (unsigned char)((value * coverage + pixel[channel] * (255U - coverage) + 127U) / 255U);
	}
}

/* Draws an anti-aliased disc. */
static void
ui_dot(
	struct notes_ui *ui,
	float cx,
	float cy,
	float radius,
	uint32_t rgb)
{
	int32_t x;
	int32_t y;
	float dx;
	float dy;
	float distance;
	float coverage;

	/* Each pixel of the disc's box, covered by how far inside the edge its centre is. */
	for (y = (int32_t)(cy - radius - 1.0f); y <= (int32_t)(cy + radius + 1.0f); y++) {
		for (x = (int32_t)(cx - radius - 1.0f); x <= (int32_t)(cx + radius + 1.0f); x++) {
			dx = (float)x + 0.5f - cx;
			dy = (float)y + 0.5f - cy;
			distance = (float)sqrt((double)(dx * dx + dy * dy));
			coverage = radius + 0.5f - distance;
			if (coverage <= 0.0f)
				continue;
			if (coverage > 1.0f)
				coverage = 1.0f;
			ui_blend(ui, x, y, rgb & 0xffffffU, (unsigned)(coverage * 255.0f));
		}
	}
}

/* Measures a text's width in pixels (0 without the font). */
static int32_t
ui_text_width(
	struct notes_ui *ui,
	const char *text)
{
	struct truetype_glyph glyph;
	uint32_t codepoint;
	unsigned index;
	int32_t width;
	int error;

	/* Nothing to measure without the font. */
	if (ui->face == NULL)
		return 0;

	/* The sum of the characters' advances. */
	width = 0;
	for (; *text != '\0'; text++) {
		codepoint = ui_codepoint(*text);
		index = truetype_glyph_index(ui->face, codepoint);
		error = truetype_glyph_metrics(ui->face, index, &glyph);
		if (error == 0)
			width += glyph.advance;
	}

	/* Reports the width. */
	return width;
}

/* Draws a text with its top at a point, in a colour. */
static void
ui_text(
	struct notes_ui *ui,
	int32_t x,
	int32_t y,
	const char *text,
	uint32_t rgb)
{
	unsigned char bitmap[UI_GLYPH_MAX * UI_GLYPH_MAX];
	struct truetype_glyph glyph;
	uint32_t codepoint;
	unsigned index;
	unsigned row;
	unsigned column;
	int32_t baseline;
	int error;

	/* Nothing to draw without the font. */
	if (ui->face == NULL)
		return;

	/* The baseline, centring the text in a button's height. */
	baseline = y + (UI_BUTTON_HEIGHT - (int32_t)ui->font_pixels) / 2 + ui->ascent - 2;

	/* Each character, one after another. */
	for (; *text != '\0'; text++) {
		/* The glyph's coverage; one that does not fit is only advanced over. */
		codepoint = ui_codepoint(*text);
		index = truetype_glyph_index(ui->face, codepoint);
		error = truetype_render_glyph(ui->face, index, &glyph, bitmap, UI_GLYPH_MAX, sizeof(bitmap));
		if (error != 0) {
			error = truetype_glyph_metrics(ui->face, index, &glyph);
			if (error == 0)
				x += glyph.advance;
			continue;
		}

		/* Its pixels blended over the toolbar. */
		for (row = 0; row < glyph.height; row++) {
			for (column = 0; column < glyph.width; column++) {
				if (bitmap[row * UI_GLYPH_MAX + column] != 0U)
					ui_blend(ui, x + glyph.left + (int32_t)column, baseline - glyph.top + (int32_t)row, rgb, bitmap[row * UI_GLYPH_MAX + column]);
			}
		}

		/* The next character's place. */
		x += glyph.advance;
	}
}

/*
 * Draws a button with a label, marked when its action is the chosen one
 * (a tool's) or pale when it cannot be used, adds it, and returns where the
 * next one goes.
 */
static int32_t
ui_label_button(
	struct notes_ui *ui,
	int32_t x,
	const char *label,
	uint32_t action,
	uint32_t chosen,
	int enabled)
{
	int32_t width;
	uint32_t rgb;

	/* The label's width with room on both sides (a fixed width without the font). */
	width = ui_text_width(ui, label) + 20;
	if (ui->face == NULL)
		width = 44;

	/* The chosen tool's background, and the label's colour. */
	rgb = UI_TEXT;
	if (action == chosen) {
		ui_fill(ui, x, UI_BUTTON_TOP, width, UI_BUTTON_HEIGHT, UI_SELECTED);
		rgb = UI_SELECTED_TEXT;
	}

	/* A button that cannot be used is pale. */
	if (!enabled)
		rgb = UI_TEXT_DISABLED;
	ui_text(ui, x + 10, UI_BUTTON_TOP, label, rgb);

	/* A pale button does nothing, so it is not added. */
	if (enabled)
		ui_add_button(ui, x, UI_BUTTON_TOP, width, UI_BUTTON_HEIGHT, action);

	/* Reports where the next button goes. */
	return x + width + 2;
}

/* Adds a button's rectangle and action, when there is room. */
static void
ui_add_button(
	struct notes_ui *ui,
	int32_t x,
	int32_t y,
	int32_t width,
	int32_t height,
	uint32_t action)
{
	struct notes_button *button;

	/* A full table takes no more. */
	if (ui->button_count >= NOTES_BUTTONS)
		return;

	/* Succeeded: the button is the last. */
	button = &ui->buttons[ui->button_count];
	button->x = x;
	button->y = y;
	button->width = width;
	button->height = height;
	button->action = action;
	ui->button_count++;
}

/* Gives the character a byte of a label draws: itself when it is ASCII, '?' otherwise. */
static uint32_t
ui_codepoint(
	char byte)
{
	/* A byte past ASCII is part of a character the toolbar does not draw. */
	if ((unsigned char)byte >= 0x80U)
		return (uint32_t)'?';

	/* Reports the ASCII character. */
	return (uint32_t)(unsigned char)byte;
}
