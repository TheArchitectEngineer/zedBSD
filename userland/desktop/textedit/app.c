/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The editor of Text Editor: the document's file (opening, saving, new),
 * the frame's layout, the pointer and the keys (with the dialogs and the
 * file chooser they may be for), the actions of the menus and the
 * titlebar, and time (the cursor's blinking, the wheel's glide, a message
 * fading, a selection dragged past the edge).  plan/ws092/design.md
 * sections 3, 4, 6, 9 and 11.
 */

#include "textedit.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* How long the cursor shows and hides, and how long a message stays, in milliseconds. */
#define APP_BLINK_MS		530U
#define APP_MESSAGE_MS		2500U

/* How long a click may follow the one before and still count with it, and how far it may be from it. */
#define APP_CLICK_MS		400U
#define APP_CLICK_DISTANCE	4

/* How often the view moves while it glides or a selection is dragged past the edge, in milliseconds. */
#define APP_FRAME_MS		16

/* How quickly the wheel's glide reaches its target: its time constant in milliseconds. */
#define APP_GLIDE_MS		70.0

/* The fewest columns a wrapped text is laid out in, and the fewest digits of the line numbers. */
#define APP_COLUMNS_MIN		8U
#define APP_DIGITS_MIN		3

/* What a drag selects by. */
#define APP_UNIT_CHARACTER	0
#define APP_UNIT_WORD		1
#define APP_UNIT_LINE		2

/* The buttons of the dialogs, as te_app_dialog_layout numbers them (the first is the default). */
#define APP_BUTTON_FIRST	0
#define APP_BUTTON_SECOND	1
#define APP_BUTTON_THIRD	2

/* The most buttons a dialog has. */
#define APP_BUTTONS		3

static void app_measure(struct te_app *app);
static int app_gutter(const struct te_app *app);
static unsigned app_columns(const struct te_app *app);
static void app_fit(struct te_app *app);
static void app_replace(struct te_app *app, char *text, size_t length);
static void app_pointer(struct te_app *app, const struct te_event *event);
static void app_press(struct te_app *app, const struct te_event *event);
static void app_drag(struct te_app *app);
static void app_wheel(struct te_app *app, const struct te_event *event);
static void app_key(struct te_app *app, const struct te_event *event);
static void app_find_text(struct te_app *app, const struct te_event *event);
static void app_request(struct te_app *app, enum te_after after);
static void app_after(struct te_app *app);
static void app_save(struct te_app *app);
static int app_save_to(struct te_app *app, const char *path);
static void app_size(struct te_app *app, int step);
static void app_dialog(struct te_app *app, enum te_dialog dialog);
static void app_dialog_key(struct te_app *app, const struct te_event *event);
static void app_dialog_press(struct te_app *app, const struct te_event *event);
static void app_dialog_choose(struct te_app *app, int button);
static int app_dialog_buttons(const struct te_app *app);
static void app_chooser_start(struct te_app *app, int saving);
static void app_chooser_key(struct te_app *app, const struct te_event *event);
static void app_chooser_press(struct te_app *app, const struct te_event *event);
static void app_chooser_activate(struct te_app *app);
static void app_chooser_confirm(struct te_app *app);
static void app_chooser_show(struct te_app *app);
static void app_chooser_name(struct te_app *app, const char *name);
static int app_inside(const struct te_rect *rect, int x, int y);
static void app_error_message(struct te_app *app, const char *what, const char *name, int error);

/*
 * Starts the editor with an empty Untitled document, at a size, with the
 * body's and the interface's fonts.
 */
void
te_app_init(
	struct te_app *app,
	struct te_text *body,
	struct te_text *ui,
	int width,
	int height)
{
	int error;

	/* Nothing yet but the fonts and the size; line numbers and wrapping are on (J12). */
	memset(app, 0, sizeof(*app));
	app->body = body;
	app->ui = ui;
	app->width = width;
	app->height = height;
	app->pixels = TE_PIXELS_DEFAULT;
	app->line_numbers = 1;
	app->wrap = 1;
	app->focused = 1;

	/* The empty document, its history and its rows. */
	error = te_buffer_init(&app->buffer, "", 0U);
	if (error != 0)
		te_log("FAILED operation=buffer error=%d", error);
	te_undo_init(&app->undo);
	te_layout_init(&app->layout);

	/* The text's measurements, and the rows. */
	app_measure(app);
	te_app_relayout(app);
	app->title_changed = 1;
	app->dirty = 1;
}

/*
 * Frees what the editor holds.
 */
void
te_app_release(
	struct te_app *app)
{
	/* The chooser, the rows, the history and the document. */
	te_chooser_close(&app->chooser);
	te_layout_free(&app->layout);
	te_undo_free(&app->undo);
	te_buffer_free(&app->buffer);
}

/*
 * Opens a file in place of the document (a path that does not exist is a
 * new file of that name, made by its first save).  The caller has already
 * dealt with unsaved changes.
 *
 * Returns 0, or an errno value (the document is then as it was, and a
 * message says why).
 */
int
te_app_open(
	struct te_app *app,
	const char *path)
{
	struct te_file_info info;
	const char *name;
	char *text;
	size_t length;
	int error;

	/* The file's name, for the messages. */
	name = strrchr(path, '/');
	if (name == NULL)
		name = path;
	else
		name++;

	/* The whole file. */
	error = te_file_read(path, &text, &length, &info);
	if (error == ENOENT) {
		/* A new file of that name, empty until it is saved. */
		app_replace(app, NULL, 0U);
		snprintf(app->path, sizeof(app->path), "%s", path);
		memset(&app->file, 0, sizeof(app->file));
		te_log("OPEN path=%s bytes=0 lines=1 crlf=0 bom=0 new=1", path);
		te_app_message(app, "New file");
		return 0;
	}

	/* A file that cannot be edited says why. */
	if (error != 0) {
		app_error_message(app, "Can't open", name, error);
		return error;
	}

	/* The text in place of the document. */
	app_replace(app, text, length);
	free(text);
	snprintf(app->path, sizeof(app->path), "%s", path);
	app->file = info;
	app->opened = 1;
	te_log("OPEN path=%s bytes=%lu lines=%lu crlf=%d bom=%d", path, (unsigned long)length, (unsigned long)app->buffer.line_count, info.crlf, info.bom);

	/* Bytes that are not UTF-8 are kept, and said so. */
	if (info.invalid)
		te_app_message(app, "Some bytes aren't valid UTF-8; they are kept as they are.");

	/* Succeeded: the file is the document. */
	return 0;
}

/*
 * Starts a new, empty Untitled document.  The caller has already dealt
 * with unsaved changes.
 */
void
te_app_new(
	struct te_app *app)
{
	/* An empty text with no file. */
	app_replace(app, NULL, 0U);
	app->path[0] = '\0';
	memset(&app->file, 0, sizeof(app->file));
	te_log("NEW");
}

/*
 * Saves the document to a path (NULL: its own), and goes on with what
 * waited for the save.
 *
 * Returns 0, or an errno value (a message says why).
 */
int
te_app_save(
	struct te_app *app,
	const char *path)
{
	int error;

	/* The document's own path, unless another is given. */
	if (path == NULL)
		path = app->path;

	/* The text into the file. */
	error = app_save_to(app, path);
	if (error != 0)
		return error;

	/* Succeeded: what waited for the save goes on. */
	app_after(app);
	return 0;
}

/*
 * Takes a new size of the frame.
 */
void
te_app_resize(
	struct te_app *app,
	int width,
	int height)
{
	/* The size, and the rows laid out for its width. */
	app->width = width;
	app->height = height;
	te_app_relayout(app);
	if (app->choosing)
		app_chooser_show(app);
	app->dirty = 1;
}

/*
 * Takes one input of the window.
 */
void
te_app_event(
	struct te_app *app,
	const struct te_event *event)
{
	/* The input by its kind. */
	switch (event->type) {
	case TE_EVENT_MOTION:
	case TE_EVENT_BUTTON:
	case TE_EVENT_LEAVE:
		app_pointer(app, event);
		break;
	case TE_EVENT_AXIS:
		app_wheel(app, event);
		break;
	case TE_EVENT_KEY:
		app_key(app, event);
		break;
	case TE_EVENT_ACTION:
		te_app_action(app, (enum te_action)event->action);
		break;
	case TE_EVENT_FOCUS:
		/* The keyboard came or went; the cursor starts a blink either way. */
		app->focused = event->pressed;
		app->blink_start = app->now;
		app->dirty = 1;
		break;
	case TE_EVENT_FIND_TEXT:
		app_find_text(app, event);
		break;
	case TE_EVENT_FIND_DONE:
		/* Enter in the field finds the next place; leaving it or Esc keeps the place found. */
		if (event->how == TE_FIND_SUBMITTED)
			te_edit_find(app, 1, 0);
		break;
	}

	/* The line numbers may have grown a digit, which narrows the text. */
	app_fit(app);
}

/*
 * Carries out an action of the menus, the titlebar, the context menu or
 * the keys.
 */
void
te_app_action(
	struct te_app *app,
	enum te_action action)
{
	/* A dialog or the chooser waits for its answer; only Quit goes past them. */
	if ((app->dialog != TE_DIALOG_NONE || app->choosing) && action != TE_ACTION_QUIT)
		return;
	te_log("ACTION %d", (int)action);

	/* The action. */
	switch (action) {
	case TE_ACTION_NEW:
		app_request(app, TE_AFTER_NEW);
		break;
	case TE_ACTION_OPEN:
		app_request(app, TE_AFTER_OPEN);
		break;
	case TE_ACTION_SAVE:
		app_save(app);
		break;
	case TE_ACTION_SAVE_AS:
		app_chooser_start(app, 1);
		break;
	case TE_ACTION_CLOSE:
	case TE_ACTION_QUIT:
		/* Quit closes a dialog or the chooser first, then asks like Close. */
		app->dialog = TE_DIALOG_NONE;
		app->choosing = 0;
		app_request(app, TE_AFTER_CLOSE);
		break;
	case TE_ACTION_UNDO:
		te_edit_undo(app);
		break;
	case TE_ACTION_REDO:
		te_edit_redo(app);
		break;
	case TE_ACTION_CUT:
		te_edit_cut(app);
		break;
	case TE_ACTION_COPY:
		te_edit_copy(app);
		break;
	case TE_ACTION_PASTE:
		te_edit_paste(app, 0);
		break;
	case TE_ACTION_SELECT_ALL:
		te_edit_select_all(app);
		break;
	case TE_ACTION_FIND:
		/* The titlebar's field takes the keyboard. */
		if (app->host.find_focus != NULL)
			app->host.find_focus(app->host.data);
		break;
	case TE_ACTION_FIND_NEXT:
		te_edit_find(app, 1, 0);
		break;
	case TE_ACTION_FIND_PREVIOUS:
		te_edit_find(app, 0, 0);
		break;
	case TE_ACTION_LINE_NUMBERS:
		app->line_numbers = !app->line_numbers;
		te_app_relayout(app);
		break;
	case TE_ACTION_WORD_WRAP:
		/* The rows change: the view keeps the cursor. */
		app->wrap = !app->wrap;
		app->scroll_x = 0.0;
		te_app_relayout(app);
		te_edit_reveal(app);
		break;
	case TE_ACTION_BIGGER:
		app_size(app, 1);
		break;
	case TE_ACTION_SMALLER:
		app_size(app, -1);
		break;
	case TE_ACTION_ACTUAL_SIZE:
		app_size(app, 0);
		break;
	case TE_ACTION_ABOUT:
		app_dialog(app, TE_DIALOG_ABOUT);
		break;
	case TE_ACTION_NONE:
		break;
	}

	/* The frame shows what the action did. */
	app->dirty = 1;
}

/*
 * Moves time on: the cursor's blink, the wheel's glide, a message's end,
 * and a selection dragged past the edge.  Reports in how many milliseconds
 * something is due again (-1 for nothing).
 */
int
te_app_tick(
	struct te_app *app,
	uint64_t now)
{
	double elapsed;
	double share;
	uint64_t since;
	int due;
	int wait;

	/* The time, and nothing due yet. */
	elapsed = 0.0;
	if (app->now != 0U && now > app->now)
		elapsed = (double)(now - app->now);
	app->now = now;
	due = -1;

	/* A message that ran its time goes. */
	if (app->message[0] != '\0') {
		if (now >= app->message_until) {
			app->message[0] = '\0';
			app->dirty = 1;
		} else {
			due = (int)(app->message_until - now);
		}
	}

	/* The wheel's glide: the view closes on its target, and stops within half a pixel. */
	if (app->gliding) {
		share = 1.0 - exp(-elapsed / APP_GLIDE_MS);
		app->scroll_y += (app->target_y - app->scroll_y) * share;
		if (fabs(app->target_y - app->scroll_y) < 0.5) {
			app->scroll_y = app->target_y;
			app->gliding = 0;
		}
		app->dirty = 1;
		if (app->gliding && (due < 0 || due > APP_FRAME_MS))
			due = APP_FRAME_MS;
	}

	/* A selection dragged past the edge scrolls the view and follows the pointer. */
	if (app->selecting) {
		app_drag(app);
		if (due < 0 || due > APP_FRAME_MS)
			due = APP_FRAME_MS;
	}

	/* The cursor blinks while the keyboard is the window's and no dialog or chooser covers it. */
	if (app->focused && app->dialog == TE_DIALOG_NONE && !app->choosing) {
		since = now - app->blink_start;
		wait = (int)(APP_BLINK_MS - since % APP_BLINK_MS);
		if (since % APP_BLINK_MS < APP_FRAME_MS)
			app->dirty = 1;
		if (due < 0 || wait < due)
			due = wait;
	}

	/* Succeeded: when something is due next. */
	return due;
}

/*
 * Shows a message at the bottom of the frame for a while.
 */
void
te_app_message(
	struct te_app *app,
	const char *message)
{
	/* The message and its end. */
	snprintf(app->message, sizeof(app->message), "%s", message);
	app->message_until = app->now + APP_MESSAGE_MS;
	app->dirty = 1;
	te_log("MESSAGE %s", message);
}

/*
 * Lays the whole text out again for the frame's width and the text's
 * size, keeping the view within the text.
 */
void
te_app_relayout(
	struct te_app *app)
{
	unsigned columns;
	int error;

	/* The columns the text's width holds. */
	columns = app_columns(app);

	/* The rows; without memory the text keeps the rows it had. */
	error = te_layout_reset(&app->layout, &app->buffer, app->wrap, columns);
	if (error != 0)
		te_log("LAYOUT failed error=%d", error);
	te_app_clamp(app);
	app->target_y = app->scroll_y;
	app->dirty = 1;
}

/*
 * Keeps the view's place within the text.
 */
void
te_app_clamp(
	struct te_app *app)
{
	double largest;

	/* Down no further than the last row at the bottom. */
	largest = te_app_max_scroll_y(app);
	if (app->scroll_y > largest)
		app->scroll_y = largest;
	if (app->scroll_y < 0.0)
		app->scroll_y = 0.0;

	/* Across no further than the widest line (never, with wrapping). */
	largest = te_app_max_scroll_x(app);
	if (app->scroll_x > largest)
		app->scroll_x = largest;
	if (app->scroll_x < 0.0)
		app->scroll_x = 0.0;
}

/*
 * Gives the rectangle the text is drawn in (right of the line numbers).
 */
void
te_app_text_rect(
	const struct te_app *app,
	struct te_rect *rect)
{
	struct te_rect card;
	int gutter;

	/* Inside the card, past the line numbers. */
	te_app_card(app, &card);
	gutter = app_gutter(app);
	rect->x = card.x + TE_TEXT_SIDE + gutter;
	rect->y = card.y + TE_TEXT_TOP;
	rect->width = card.x + card.width - TE_TEXT_SIDE - rect->x;
	rect->height = card.y + card.height - TE_TEXT_TOP - rect->y;

	/* Never less than a cell and a row. */
	if (rect->width < app->cell)
		rect->width = app->cell;
	if (rect->height < app->row_height)
		rect->height = app->row_height;
}

/*
 * Gives the card the window's content is drawn on.
 */
void
te_app_card(
	const struct te_app *app,
	struct te_rect *rect)
{
	/* The frame inset on every side. */
	rect->x = TE_CARD_INSET;
	rect->y = TE_CARD_INSET;
	rect->width = app->width - 2 * TE_CARD_INSET;
	rect->height = app->height - 2 * TE_CARD_INSET;
}

/*
 * Reports how far the view may scroll across (0 with wrapping).
 */
double
te_app_max_scroll_x(
	const struct te_app *app)
{
	struct te_rect text;
	double largest;

	/* Wrapped lines never reach across. */
	if (app->wrap)
		return 0.0;

	/* The widest line and a cell for the cursor, past the text's width. */
	te_app_text_rect(app, &text);
	largest = (double)(app->layout.widest + 1U) * (double)app->cell - (double)text.width;
	if (largest < 0.0)
		largest = 0.0;

	/* Succeeded: the largest offset. */
	return largest;
}

/*
 * Reports how far the view may scroll down: the last row at the bottom.
 */
double
te_app_max_scroll_y(
	const struct te_app *app)
{
	struct te_rect text;
	double largest;

	/* The rows' height past the text's. */
	te_app_text_rect(app, &text);
	largest = (double)app->layout.total * (double)app->row_height - (double)text.height;
	if (largest < 0.0)
		largest = 0.0;

	/* Succeeded: the largest offset. */
	return largest;
}

/*
 * Reports the document's name: its file's name, or Untitled.
 */
const char *
te_app_name(
	const struct te_app *app)
{
	const char *slash;

	/* No file yet. */
	if (app->path[0] == '\0')
		return "Untitled";

	/* The part after the last slash. */
	slash = strrchr(app->path, '/');
	if (slash == NULL)
		return app->path;

	/* Succeeded: the name. */
	return slash + 1;
}

/*
 * Reports whether the document has changes that are not saved.
 */
int
te_app_modified(
	const struct te_app *app)
{
	int modified;

	/* The history knows. */
	modified = te_undo_modified(&app->undo);

	/* Succeeded: whether it has. */
	return modified;
}

/*
 * Makes a new selection the primary selection (once per round of input).
 */
void
te_app_publish_primary(
	struct te_app *app)
{
	size_t start;
	size_t end;
	char *text;

	/* Only a changed, non-empty selection, and only with somewhere to publish it. */
	if (!app->primary_changed)
		return;
	app->primary_changed = 0;
	te_edit_selection(app, &start, &end);
	if (end <= start || app->host.select == NULL)
		return;
	if (end - start > TE_FILE_MAX)
		end = start + TE_FILE_MAX;

	/* The selected text, which the window keeps its own copy of. */
	text = malloc(end - start);
	if (text == NULL)
		return;
	te_buffer_copy(&app->buffer, start, end, text);
	app->host.select(app->host.data, text, end - start);
	free(text);
}

/*
 * A finger tapped (count 1) or tapped twice (count 2): the pointer's click
 * there, and for a double tap in the text, the word there selected.
 */
void
te_app_tap(
	struct te_app *app,
	int x,
	int y,
	int count)
{
	struct te_event event;
	struct te_rect text;
	size_t position;
	size_t start;
	size_t end;
	int inside;

	/* A double tap in the text selects the word there. */
	te_app_text_rect(app, &text);
	inside = app_inside(&text, x, y);
	if (count == 2 && inside && app->dialog == TE_DIALOG_NONE && !app->choosing) {
		position = te_edit_position_at(app, x, y);
		te_edit_word(app, position, &start, &end);
		te_edit_select(app, start, end);
		return;
	}

	/* Otherwise a click: the pointer comes, presses and lets go. */
	memset(&event, 0, sizeof(event));
	event.x = x;
	event.y = y;
	event.button = TE_BUTTON_LEFT;
	event.type = TE_EVENT_MOTION;
	te_app_event(app, &event);
	event.type = TE_EVENT_BUTTON;
	event.pressed = 1;
	te_app_event(app, &event);
	event.pressed = 0;
	te_app_event(app, &event);
}

/*
 * Gives the dialog's card and its buttons (the first is the default), and
 * how many buttons it has.
 */
void
te_app_dialog_layout(
	const struct te_app *app,
	struct te_rect *card,
	struct te_rect *buttons,
	int *count)
{
	int index;
	int right;

	/* The card in the middle of the frame. */
	card->width = TE_DIALOG_WIDTH;
	card->height = TE_DIALOG_HEIGHT;
	if (card->width > app->width - 2 * TE_CARD_INSET)
		card->width = app->width - 2 * TE_CARD_INSET;
	card->x = (app->width - card->width) / 2;
	card->y = (app->height - card->height) / 2;

	/* The buttons along its bottom, from the right: the default, then the others. */
	*count = app_dialog_buttons(app);
	right = card->x + card->width - 20;
	for (index = 0; index < *count; index++) {
		buttons[index].width = TE_BUTTON_WIDTH;
		buttons[index].height = TE_BUTTON_HEIGHT;
		buttons[index].x = right - TE_BUTTON_WIDTH;
		buttons[index].y = card->y + card->height - 20 - TE_BUTTON_HEIGHT;
		right -= TE_BUTTON_WIDTH + 10;
	}
}

/*
 * Gives the chooser's card, its list's rectangle, and how many rows the
 * list shows.
 */
void
te_app_chooser_layout(
	const struct te_app *app,
	struct te_rect *card,
	struct te_rect *list,
	size_t *rows)
{
	/* The card in the middle of the frame, within it. */
	card->width = TE_CHOOSER_WIDTH;
	card->height = TE_CHOOSER_HEIGHT;
	if (card->width > app->width - 2 * TE_CARD_INSET)
		card->width = app->width - 2 * TE_CARD_INSET;
	if (card->height > app->height - 2 * TE_CARD_INSET)
		card->height = app->height - 2 * TE_CARD_INSET;
	card->x = (app->width - card->width) / 2;
	card->y = (app->height - card->height) / 2;

	/* The list between the header and the footer. */
	list->x = card->x + 12;
	list->y = card->y + TE_CHOOSER_HEADER;
	list->width = card->width - 24;
	list->height = card->height - TE_CHOOSER_HEADER - TE_CHOOSER_FOOTER;
	if (list->height < TE_CHOOSER_ROW)
		list->height = TE_CHOOSER_ROW;

	/* Succeeded: the rows that fit. */
	*rows = (size_t)(list->height / TE_CHOOSER_ROW);
}

/* Measures the body's text at its size: the cell's width, the row's height and the baseline. */
static void
app_measure(
	struct te_app *app)
{
	struct te_text_line line;
	int advance;

	/* The font's line at the size, with some room between rows. */
	te_text_metrics(app->body, app->pixels, &line);
	app->ascent = line.ascent + 2;
	app->row_height = line.ascent + line.descent + 5;
	if (app->row_height < (int)app->pixels + 2)
		app->row_height = (int)app->pixels + 2;

	/* A cell is the monospaced font's advance (a guess from the size without a font). */
	advance = te_text_advance(app->body, '0', app->pixels);
	app->cell = advance;
	if (advance <= 0)
		app->cell = (int)(app->pixels * 3U / 5U);
	if (app->cell < 1)
		app->cell = 1;
}

/* Reports the width of the line numbers' column (0 without them). */
static int
app_gutter(
	const struct te_app *app)
{
	size_t lines;
	int digits;

	/* No column without line numbers. */
	if (!app->line_numbers)
		return 0;

	/* As many digits as the last line's number has, at least three. */
	digits = 1;
	for (lines = app->buffer.line_count; lines >= 10U; lines /= 10U)
		digits++;
	if (digits < APP_DIGITS_MIN)
		digits = APP_DIGITS_MIN;

	/* Succeeded: the digits and a margin. */
	return digits * app->cell + TE_GUTTER_PAD;
}

/* Reports how many columns the text's width holds. */
static unsigned
app_columns(
	const struct te_app *app)
{
	struct te_rect text;
	unsigned columns;

	/* The whole cells across the text. */
	te_app_text_rect(app, &text);
	columns = (unsigned)(text.width / app->cell);
	if (columns < APP_COLUMNS_MIN)
		columns = APP_COLUMNS_MIN;

	/* Succeeded: the columns. */
	return columns;
}

/* Lays the text out again when its width changed (the line numbers grew a digit). */
static void
app_fit(
	struct te_app *app)
{
	unsigned columns;

	/* Only wrapped text depends on the width. */
	if (!app->wrap)
		return;

	/* The same columns keep the rows. */
	columns = app_columns(app);
	if (columns == app->layout.columns)
		return;

	/* The rows for the new width, the cursor still in view. */
	te_app_relayout(app);
	te_edit_reveal(app);
}

/* Puts a text (NULL for none) in place of the document, with a new history and the view at its start. */
static void
app_replace(
	struct te_app *app,
	char *text,
	size_t length)
{
	struct te_buffer buffer;
	int error;

	/* The new document; without memory the old one stays. */
	if (text == NULL)
		text = "";
	error = te_buffer_init(&buffer, text, length);
	if (error != 0) {
		te_app_message(app, "Not enough memory to open this file.");
		return;
	}

	/* It replaces the old one, with an empty history. */
	te_buffer_free(&app->buffer);
	app->buffer = buffer;
	te_undo_free(&app->undo);
	te_undo_init(&app->undo);

	/* The cursor and the view at the start, the rows laid out. */
	app->cursor = 0;
	app->anchor = 0;
	app->goal_valid = 0;
	app->scroll_x = 0.0;
	app->scroll_y = 0.0;
	app->target_y = 0.0;
	app->gliding = 0;
	te_app_relayout(app);
	app->title_changed = 1;
	app->dirty = 1;
}

/* Takes the pointer's input: over a dialog or the chooser, or in the text. */
static void
app_pointer(
	struct te_app *app,
	const struct te_event *event)
{
	/* Where the pointer is. */
	app->pointer_x = event->x;
	app->pointer_y = event->y;

	/* A dialog takes the pointer. */
	if (app->dialog != TE_DIALOG_NONE) {
		app_dialog_press(app, event);
		return;
	}

	/* So does the chooser. */
	if (app->choosing) {
		app_chooser_press(app, event);
		return;
	}

	/* The pointer left: a drag in progress ends. */
	if (event->type == TE_EVENT_LEAVE)
		return;

	/* A drag follows the pointer. */
	if (event->type == TE_EVENT_MOTION) {
		if (app->selecting)
			app_drag(app);
		return;
	}

	/* A release ends a drag; its selection is offered as the primary one. */
	if (!event->pressed) {
		if (app->selecting && event->button == TE_BUTTON_LEFT) {
			app->selecting = 0;
			app->primary_changed = 1;
		}
		return;
	}

	/* A press. */
	app_press(app, event);
}

/*
 * A press in the text: the left button places the cursor (Shift extends
 * the selection; twice selects a word, three times a line) and starts a
 * drag; the middle one pastes the primary selection there; the right one
 * opens the context menu.
 */
static void
app_press(
	struct te_app *app,
	const struct te_event *event)
{
	struct te_rect card;
	size_t position;
	size_t start;
	size_t end;
	int near;
	int inside;

	/* Only in the card. */
	te_app_card(app, &card);
	inside = app_inside(&card, event->x, event->y);
	if (!inside)
		return;
	position = te_edit_position_at(app, event->x, event->y);

	/* The middle button pastes the primary selection where it presses. */
	if (event->button == TE_BUTTON_MIDDLE) {
		te_edit_select(app, position, position);
		te_edit_paste(app, 1);
		return;
	}

	/* The right button: the context menu (over the selection, or with the cursor moved there). */
	if (event->button == TE_BUTTON_RIGHT) {
		te_edit_selection(app, &start, &end);
		if (position < start || position > end || start == end)
			te_edit_select(app, position, position);
		if (app->host.context_menu != NULL)
			app->host.context_menu(app->host.data, event->x, event->y);
		return;
	}

	/* Only the left button selects. */
	if (event->button != TE_BUTTON_LEFT)
		return;

	/* A click soon after one near it counts with it (up to three). */
	near = 0;
	if (abs(event->x - app->click_x) <= APP_CLICK_DISTANCE && abs(event->y - app->click_y) <= APP_CLICK_DISTANCE)
		near = 1;
	if (near && app->now - app->click_time <= APP_CLICK_MS && app->click_count < 3)
		app->click_count++;
	else
		app->click_count = 1;
	app->click_time = app->now;
	app->click_x = event->x;
	app->click_y = event->y;

	/* One click places the cursor (Shift extends the selection); two a word; three a line. */
	app->select_unit = APP_UNIT_CHARACTER;
	start = position;
	end = position;
	if (app->click_count == 2) {
		app->select_unit = APP_UNIT_WORD;
		te_edit_word(app, position, &start, &end);
	} else if (app->click_count == 3) {
		app->select_unit = APP_UNIT_LINE;
		te_edit_line(app, position, &start, &end);
	}

	/* The selection, and what a drag keeps selected. */
	if (app->click_count == 1 && (event->modifiers & TE_MOD_SHIFT) != 0U) {
		te_edit_select(app, app->anchor, position);
		app->select_start = app->anchor;
		app->select_end = app->anchor;
	} else {
		te_edit_select(app, start, end);
		app->select_start = start;
		app->select_end = end;
	}
	app->selecting = 1;
}

/* Extends the selection being dragged to the pointer, scrolling when it is past the text's edge. */
static void
app_drag(
	struct te_app *app)
{
	struct te_rect text;
	size_t position;
	size_t start;
	size_t end;
	double step;

	/* Past the top or the bottom, the view scrolls by a share of the distance. */
	te_app_text_rect(app, &text);
	step = 0.0;
	if (app->pointer_y < text.y)
		step = (double)(app->pointer_y - text.y) / 3.0;
	if (app->pointer_y > text.y + text.height)
		step = (double)(app->pointer_y - text.y - text.height) / 3.0;
	if (step != 0.0) {
		app->scroll_y += step;
		te_app_clamp(app);
		app->target_y = app->scroll_y;
		app->dirty = 1;
	}

	/* The place under the pointer, grown to the word or the line the drag selects by. */
	position = te_edit_position_at(app, app->pointer_x, app->pointer_y);
	start = position;
	end = position;
	if (app->select_unit == APP_UNIT_WORD)
		te_edit_word(app, position, &start, &end);
	if (app->select_unit == APP_UNIT_LINE)
		te_edit_line(app, position, &start, &end);

	/* The selection from what the press selected to the pointer, either way. */
	if (start < app->select_start) {
		app->anchor = app->select_end;
		app->cursor = start;
	} else {
		app->anchor = app->select_start;
		app->cursor = end;
		if (end < app->select_end)
			app->cursor = app->select_end;
	}
	app->goal_valid = 0;
	app->blink_start = app->now;
	app->dirty = 1;
}

/* The wheel: the chooser's list moves, Control changes the text's size, and otherwise the view glides. */
static void
app_wheel(
	struct te_app *app,
	const struct te_event *event)
{
	/* A dialog keeps the view still. */
	if (app->dialog != TE_DIALOG_NONE)
		return;

	/* The chooser's list moves a row a notch. */
	if (app->choosing) {
		if (event->scroll > 0 && app->chooser.first + 1U < app->chooser.count)
			app->chooser.first++;
		if (event->scroll < 0 && app->chooser.first > 0U)
			app->chooser.first--;
		app->dirty = 1;
		return;
	}

	/* Control and the wheel change the text's size. */
	if ((event->modifiers & TE_MOD_CTRL) != 0U) {
		if (event->scroll < 0)
			app_size(app, 1);
		if (event->scroll > 0)
			app_size(app, -1);
		return;
	}

	/* Across: at once. */
	if (event->scroll_x != 0) {
		app->scroll_x += (double)event->scroll_x;
		te_app_clamp(app);
		app->dirty = 1;
	}

	/* Down or up: the view glides to the new place. */
	if (event->scroll != 0) {
		if (!app->gliding)
			app->target_y = app->scroll_y;
		app->target_y += (double)event->scroll;
		if (app->target_y < 0.0)
			app->target_y = 0.0;
		if (app->target_y > te_app_max_scroll_y(app))
			app->target_y = te_app_max_scroll_y(app);
		app->gliding = 1;
		app->dirty = 1;
	}
}

/* Takes a key: a dialog's, the chooser's, F3 and Esc, or the text's. */
static void
app_key(
	struct te_app *app,
	const struct te_event *event)
{
	int shift;

	/* Only presses. */
	if (!event->pressed)
		return;

	/* A dialog takes the keys. */
	if (app->dialog != TE_DIALOG_NONE) {
		app_dialog_key(app, event);
		return;
	}

	/* So does the chooser. */
	if (app->choosing) {
		app_chooser_key(app, event);
		return;
	}

	/* F3 finds again (Shift: backward). */
	if (event->key == TE_KEY_F3) {
		shift = 0;
		if ((event->modifiers & TE_MOD_SHIFT) != 0U)
			shift = 1;
		te_edit_find(app, !shift, 0);
		return;
	}

	/* Anything else is the text's. */
	(void)te_edit_key(app, event);
	app->blink_start = app->now;
	app->dirty = 1;
}

/* The find field's text changed: it is found from where the selection starts. */
static void
app_find_text(
	struct te_app *app,
	const struct te_event *event)
{
	/* The text, kept for Find Next. */
	snprintf(app->find, sizeof(app->find), "%s", event->text);
	app->find_length = strlen(app->find);

	/* Found as it is typed; an empty field only clears the marks. */
	if (app->find_length != 0U)
		te_edit_find(app, 1, 1);
	app->dirty = 1;
}

/*
 * Asks for something that replaces the document or closes the window: with
 * unsaved changes a dialog asks first; otherwise it happens now.
 */
static void
app_request(
	struct te_app *app,
	enum te_after after)
{
	int modified;

	/* What waits. */
	app->after = after;

	/* Unsaved changes are asked about first. */
	modified = te_app_modified(app);
	if (modified) {
		app_dialog(app, TE_DIALOG_UNSAVED);
		return;
	}

	/* Nothing to lose: it happens. */
	app_after(app);
}

/* Does what waited for the unsaved changes to be saved or dropped. */
static void
app_after(
	struct te_app *app)
{
	enum te_after after;

	/* What waited, once. */
	after = app->after;
	app->after = TE_AFTER_NOTHING;

	/* Carries it out. */
	switch (after) {
	case TE_AFTER_CLOSE:
		app->want_close = 1;
		break;
	case TE_AFTER_NEW:
		te_app_new(app);
		break;
	case TE_AFTER_OPEN:
		app_chooser_start(app, 0);
		break;
	case TE_AFTER_NOTHING:
		break;
	}
}

/* File > Save: to the document's file, asking first when the file changed on the disk; without a file, Save As. */
static void
app_save(
	struct te_app *app)
{
	int changed;

	/* Untitled: Save As. */
	if (app->path[0] == '\0') {
		app_chooser_start(app, 1);
		return;
	}

	/* A file changed on the disk since it was read: overwrite it? */
	changed = te_file_changed(app->path, &app->file);
	if (changed) {
		app_dialog(app, TE_DIALOG_CHANGED);
		return;
	}

	/* The save. */
	(void)te_app_save(app, NULL);
}

/* Writes the document to a path, which becomes its file; returns 0 or an errno value. */
static int
app_save_to(
	struct te_app *app,
	const char *path)
{
	char resolved[TE_PATH_MAX];
	const char *name;
	int same;
	int error;

	/* A new path is a new file (its line ends and mark stay the document's). */
	snprintf(resolved, sizeof(resolved), "%s", path);
	same = strcmp(resolved, app->path);
	if (same != 0) {
		app->file.exists = 0;
		app->file.mode = 0;
	}

	/* The text into the file. */
	error = te_file_write(resolved, &app->buffer, &app->file);
	if (error != 0) {
		name = strrchr(resolved, '/');
		if (name == NULL)
			name = resolved;
		else
			name++;
		app_error_message(app, "Can't save", name, error);
		app->after = TE_AFTER_NOTHING;
		return error;
	}

	/* The file is the document's now, and saved. */
	snprintf(app->path, sizeof(app->path), "%s", resolved);
	te_undo_mark_saved(&app->undo);
	app->opened = 1;
	app->title_changed = 1;
	te_log("SAVE path=%s bytes=%lu", app->path, (unsigned long)te_buffer_length(&app->buffer));
	te_app_message(app, "Saved");

	/* Succeeded: saved. */
	return 0;
}

/* Changes the body's text size by a step (0: back to the default), keeping the cursor in view. */
static void
app_size(
	struct te_app *app,
	int step)
{
	unsigned pixels;

	/* The new size, within its bounds. */
	pixels = TE_PIXELS_DEFAULT;
	if (step > 0)
		pixels = app->pixels + 1U;
	if (step < 0)
		pixels = app->pixels - 1U;
	if (pixels < TE_PIXELS_MIN)
		pixels = TE_PIXELS_MIN;
	if (pixels > TE_PIXELS_MAX)
		pixels = TE_PIXELS_MAX;

	/* The rows at the new size. */
	app->pixels = pixels;
	app_measure(app);
	te_app_relayout(app);
	te_edit_reveal(app);
}

/* Shows a dialog, its default button under the keyboard. */
static void
app_dialog(
	struct te_app *app,
	enum te_dialog dialog)
{
	/* The dialog, nothing under the pointer yet. */
	app->dialog = dialog;
	app->dialog_hover = -1;
	app->selecting = 0;
	app->dirty = 1;
	te_log("DIALOG %d", (int)dialog);
}

/* A key in a dialog: Enter chooses the default button, Esc the last (Cancel). */
static void
app_dialog_key(
	struct te_app *app,
	const struct te_event *event)
{
	int count;

	/* The buttons. */
	count = app_dialog_buttons(app);

	/* Enter: the default. */
	if (event->key == TE_KEY_ENTER || event->key == TE_KEY_KP_ENTER) {
		app_dialog_choose(app, APP_BUTTON_FIRST);
		return;
	}

	/* Esc: the last, which cancels. */
	if (event->key == TE_KEY_ESCAPE)
		app_dialog_choose(app, count - 1);
}

/* The pointer over a dialog: a button under it is lit, and a click on one chooses it. */
static void
app_dialog_press(
	struct te_app *app,
	const struct te_event *event)
{
	struct te_rect buttons[APP_BUTTONS];
	struct te_rect card;
	int count;
	int index;
	int inside;
	int hover;

	/* The button under the pointer (-1 for none). */
	te_app_dialog_layout(app, &card, buttons, &count);
	hover = -1;
	for (index = 0; index < count; index++) {
		inside = app_inside(&buttons[index], event->x, event->y);
		if (inside)
			hover = index;
	}

	/* Lit when it changes. */
	if (hover != app->dialog_hover) {
		app->dialog_hover = hover;
		app->dirty = 1;
	}

	/* A left release on a button chooses it. */
	if (event->type == TE_EVENT_BUTTON && !event->pressed && event->button == TE_BUTTON_LEFT && hover >= 0)
		app_dialog_choose(app, hover);
}

/* Carries out a dialog's button. */
static void
app_dialog_choose(
	struct te_app *app,
	int button)
{
	enum te_dialog dialog;

	/* The dialog closes. */
	dialog = app->dialog;
	app->dialog = TE_DIALOG_NONE;
	app->dirty = 1;
	te_log("DIALOG choose=%d", button);

	/* What the button means in that dialog. */
	switch (dialog) {
	case TE_DIALOG_UNSAVED:
		/* Save (as, for Untitled), Don't Save, or Cancel. */
		if (button == APP_BUTTON_FIRST) {
			if (app->path[0] == '\0')
				app_chooser_start(app, 1);
			else
				(void)te_app_save(app, NULL);
		} else if (button == APP_BUTTON_SECOND) {
			app_after(app);
		} else {
			app->after = TE_AFTER_NOTHING;
		}
		break;
	case TE_DIALOG_REPLACE:
		/* Replace the file chosen, or go back to the chooser. */
		if (button == APP_BUTTON_FIRST)
			(void)te_app_save(app, app->pending_path);
		else
			app_chooser_start(app, 1);
		break;
	case TE_DIALOG_CHANGED:
		/* Overwrite the file changed on the disk, or keep it. */
		if (button == APP_BUTTON_FIRST)
			(void)te_app_save(app, NULL);
		else
			app->after = TE_AFTER_NOTHING;
		break;
	case TE_DIALOG_ABOUT:
	case TE_DIALOG_NONE:
		break;
	}
}

/* Reports how many buttons the shown dialog has. */
static int
app_dialog_buttons(
	const struct te_app *app)
{
	/* Unsaved changes: Save, Don't Save, Cancel. */
	if (app->dialog == TE_DIALOG_UNSAVED)
		return 3;

	/* About: OK. */
	if (app->dialog == TE_DIALOG_ABOUT)
		return 1;

	/* The others: the action and Cancel. */
	return 2;
}

/* Opens the chooser for Open or for Save As, at the document's folder (or the home folder). */
static void
app_chooser_start(
	struct te_app *app,
	int saving)
{
	char folder[TE_PATH_MAX];
	const char *home;
	char *slash;
	int error;

	/* The document's folder, or home, or here. */
	snprintf(folder, sizeof(folder), "%s", app->path);
	slash = strrchr(folder, '/');
	if (slash != NULL && slash != folder)
		*slash = '\0';
	else if (slash == folder)
		folder[1] = '\0';
	if (app->path[0] == '\0' || slash == NULL) {
		home = getenv("HOME");
		if (home == NULL || home[0] == '\0')
			home = ".";
		snprintf(folder, sizeof(folder), "%s", home);
	}

	/* The folder's list; one that cannot be read gives the root's. */
	error = te_chooser_open(&app->chooser, folder);
	if (error != 0)
		error = te_chooser_open(&app->chooser, "/");
	if (error != 0) {
		app_error_message(app, "Can't list", folder, error);
		return;
	}

	/* Save As starts with the document's name, all of it selected. */
	app->chooser.saving = saving;
	app_chooser_name(app, te_app_name(app));
	app->chooser.name_all = 1;
	if (saving && app->path[0] == '\0')
		app_chooser_name(app, "Untitled.txt");
	app->choosing = 1;
	app->selecting = 0;
	app->dirty = 1;
	te_log("CHOOSER saving=%d folder=%s", saving, app->chooser.folder);
}

/* A key in the chooser: moves in the list, opens, goes up, types the name, or closes it. */
static void
app_chooser_key(
	struct te_app *app,
	const struct te_event *event)
{
	struct te_chooser *chooser;
	uint32_t codepoint;
	char bytes[4];

	/* The chooser. */
	chooser = &app->chooser;
	app->dirty = 1;

	/* The key. */
	switch (event->key) {
	case TE_KEY_ESCAPE:
		/* Closed: nothing waits any more. */
		app->choosing = 0;
		app->after = TE_AFTER_NOTHING;
		return;
	case TE_KEY_UP:
		if (chooser->selected > 0U)
			chooser->selected--;
		app_chooser_show(app);
		return;
	case TE_KEY_DOWN:
		if (chooser->selected + 1U < chooser->count)
			chooser->selected++;
		app_chooser_show(app);
		return;
	case TE_KEY_HOME:
		chooser->selected = 0;
		app_chooser_show(app);
		return;
	case TE_KEY_END:
		if (chooser->count > 0U)
			chooser->selected = chooser->count - 1U;
		app_chooser_show(app);
		return;
	case TE_KEY_ENTER:
	case TE_KEY_KP_ENTER:
		/* Save As saves the name typed; Open opens the entry. */
		if (chooser->saving)
			app_chooser_confirm(app);
		else
			app_chooser_activate(app);
		return;
	case TE_KEY_BACKSPACE:
		/* Save As edits the name (all of it when it is selected); Open goes up. */
		if (chooser->saving) {
			if (chooser->name_all)
				chooser->name_length = 0;
			while (chooser->name_length > 0U) {
				chooser->name_length--;
				if (((unsigned char)chooser->name[chooser->name_length] & 0xc0U) != 0x80U)
					break;
			}
			chooser->name[chooser->name_length] = '\0';
			chooser->name_all = 0;
		} else {
			chooser->selected = 0;
			app_chooser_activate(app);
		}
		return;
	default:
		break;
	}

	/* Ctrl+A selects the whole name. */
	if (event->key == TE_KEY_A && (event->modifiers & TE_MOD_CTRL) != 0U) {
		chooser->name_all = 1;
		return;
	}

	/* A character goes into the name (in place of all of it when it is selected). */
	codepoint = te_key_character(event->key, event->modifiers);
	if (!chooser->saving || codepoint == 0U || codepoint == '/')
		return;
	if (chooser->name_all)
		chooser->name_length = 0;
	chooser->name_all = 0;
	bytes[0] = (char)codepoint;
	bytes[1] = '\0';
	if (chooser->name_length + 1U < sizeof(chooser->name)) {
		memcpy(chooser->name + chooser->name_length, bytes, 2U);
		chooser->name_length++;
	}
}

/* The pointer over the chooser: a click selects a row (twice, opens it), or chooses a button. */
static void
app_chooser_press(
	struct te_app *app,
	const struct te_event *event)
{
	struct te_rect buttons[APP_BUTTONS];
	struct te_rect card;
	struct te_rect list;
	size_t rows;
	size_t index;
	int inside;
	int again;

	/* Only a left press. */
	if (event->type != TE_EVENT_BUTTON || !event->pressed || event->button != TE_BUTTON_LEFT)
		return;
	te_app_chooser_layout(app, &card, &list, &rows);
	app->dirty = 1;

	/* The buttons at the bottom right: the default (Open or Save) and Cancel. */
	buttons[0].width = TE_BUTTON_WIDTH;
	buttons[0].height = TE_BUTTON_HEIGHT;
	buttons[0].x = card.x + card.width - 16 - TE_BUTTON_WIDTH;
	buttons[0].y = card.y + card.height - 14 - TE_BUTTON_HEIGHT;
	buttons[1] = buttons[0];
	buttons[1].x -= TE_BUTTON_WIDTH + 10;

	/* The default button. */
	inside = app_inside(&buttons[0], event->x, event->y);
	if (inside) {
		if (app->chooser.saving)
			app_chooser_confirm(app);
		else
			app_chooser_activate(app);
		return;
	}

	/* Cancel. */
	inside = app_inside(&buttons[1], event->x, event->y);
	if (inside) {
		app->choosing = 0;
		app->after = TE_AFTER_NOTHING;
		return;
	}

	/* A row of the list. */
	inside = app_inside(&list, event->x, event->y);
	if (!inside)
		return;
	index = app->chooser.first + (size_t)((event->y - list.y) / TE_CHOOSER_ROW);
	if (index >= app->chooser.count)
		return;

	/* The same row clicked again soon opens it; a file's name goes into Save As's name. */
	again = 0;
	if (index == app->chooser.selected && app->now - app->click_time <= APP_CLICK_MS)
		again = 1;
	app->click_time = app->now;
	app->chooser.selected = index;
	if (app->chooser.saving && !app->chooser.entries[index].folder)
		app_chooser_name(app, app->chooser.entries[index].name);
	if (again)
		app_chooser_activate(app);
}

/* Opens the selected entry: a folder is listed, a file is opened (Open) or named (Save As). */
static void
app_chooser_activate(
	struct te_app *app)
{
	char path[TE_PATH_MAX];
	int error;

	/* The entry's path. */
	error = te_chooser_path(&app->chooser, app->chooser.selected, path, sizeof(path));
	if (error != 0)
		return;

	/* A folder: its list. */
	if (app->chooser.entries[app->chooser.selected].folder) {
		error = te_chooser_open(&app->chooser, path);
		if (error != 0)
			app_error_message(app, "Can't open", path, error);
		return;
	}

	/* Save As: the file's name is the one saved to. */
	if (app->chooser.saving) {
		app_chooser_name(app, app->chooser.entries[app->chooser.selected].name);
		app_chooser_confirm(app);
		return;
	}

	/* Open: the file becomes the document. */
	error = te_app_open(app, path);
	if (error == 0)
		app->choosing = 0;
}

/* Save As's Save: the name typed in the folder listed, asking first before replacing another file. */
static void
app_chooser_confirm(
	struct te_app *app)
{
	struct stat status;
	int error;
	int same;

	/* The path. */
	error = te_chooser_name_path(&app->chooser, app->pending_path, sizeof(app->pending_path));
	if (error != 0) {
		te_app_message(app, "Type a name for the file.");
		return;
	}
	app->choosing = 0;

	/* Another file of that name is replaced only when the answer is yes. */
	error = stat(app->pending_path, &status);
	same = strcmp(app->pending_path, app->path);
	if (error == 0 && same != 0) {
		app_dialog(app, TE_DIALOG_REPLACE);
		return;
	}

	/* The save. */
	(void)te_app_save(app, app->pending_path);
}

/* Keeps the selected row of the chooser's list in view. */
static void
app_chooser_show(
	struct te_app *app)
{
	struct te_rect card;
	struct te_rect list;
	size_t rows;

	/* The rows that fit. */
	te_app_chooser_layout(app, &card, &list, &rows);
	if (rows == 0U)
		rows = 1U;

	/* The first row shown moves until the selected one is among them. */
	if (app->chooser.selected < app->chooser.first)
		app->chooser.first = app->chooser.selected;
	if (app->chooser.selected >= app->chooser.first + rows)
		app->chooser.first = app->chooser.selected - rows + 1U;
}

/* Sets Save As's name. */
static void
app_chooser_name(
	struct te_app *app,
	const char *name)
{
	/* The name, cut to its room. */
	snprintf(app->chooser.name, sizeof(app->chooser.name), "%s", name);
	app->chooser.name_length = strlen(app->chooser.name);
	app->chooser.name_all = 0;
}

/* Tells whether a point is in a rectangle. */
static int
app_inside(
	const struct te_rect *rect,
	int x,
	int y)
{
	/* Left of it or above it. */
	if (x < rect->x || y < rect->y)
		return 0;

	/* Right of it or below it. */
	if (x >= rect->x + rect->width || y >= rect->y + rect->height)
		return 0;

	/* Inside. */
	return 1;
}

/* Shows why a file could not be opened, saved or listed. */
static void
app_error_message(
	struct te_app *app,
	const char *what,
	const char *name,
	int error)
{
	char message[160];

	/* The reasons said in words of their own, and the rest by the system's. */
	switch (error) {
	case EFBIG:
		snprintf(message, sizeof(message), "\"%s\" is too large to edit.", name);
		break;
	case EILSEQ:
		snprintf(message, sizeof(message), "\"%s\" isn't a plain text file.", name);
		break;
	case EISDIR:
		snprintf(message, sizeof(message), "\"%s\" is a folder.", name);
		break;
	default:
		snprintf(message, sizeof(message), "%s \"%s\": %s", what, name, strerror(error));
		break;
	}

	/* Shown. */
	te_app_message(app, message);
}
