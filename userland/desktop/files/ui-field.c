/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The one-line text fields of files: the location (Ctrl+L), the
 * search field and the name being changed (rename).
 *
 * A field keeps UTF-8 text, a cursor and the other end of a selection,
 * both byte offsets on character boundaries.  Keys arrive as evdev codes
 * and are read with the US layout, as zdesktop sends no keymap; there is
 * no input method, so only what the keyboard types directly can be typed.
 */

#include "files.h"

#include <stdio.h>
#include <string.h>

/* The evdev codes of the keys a field handles. */
#define FIELD_KEY_ESC		1U
#define FIELD_KEY_BACKSPACE	14U
#define FIELD_KEY_TAB		15U
#define FIELD_KEY_ENTER		28U
#define FIELD_KEY_A		30U
#define FIELD_KEY_KPENTER	96U
#define FIELD_KEY_HOME		102U
#define FIELD_KEY_LEFT		105U
#define FIELD_KEY_RIGHT		106U
#define FIELD_KEY_END		107U
#define FIELD_KEY_DELETE	111U

/* How many codes the character tables cover (up to the space bar). */
#define FIELD_TABLE_SIZE	58U

/*
 * The character each key types without shift, by evdev code; 0 for a key
 * that types none.  The US layout.
 */
static const char field_plain[FIELD_TABLE_SIZE] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
	'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

/* The character each key types with shift, by evdev code. */
static const char field_shifted[FIELD_TABLE_SIZE] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
	'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static size_t field_previous(const struct fm_field *field, size_t offset);
static size_t field_next(const struct fm_field *field, size_t offset);
static void field_delete_selection(struct fm_field *field);
static void field_move(struct fm_field *field, size_t offset, int extend);

/*
 * Reports the ASCII character a key types with the modifiers held, or 0
 * for a key that types none (or when Ctrl or Alt makes it a command).
 */
char
fm_key_character(
	uint32_t key,
	uint32_t modifiers)
{
	/* A command key types nothing. */
	if ((modifiers & (FM_MOD_CTRL | FM_MOD_ALT | FM_MOD_SUPER)) != 0U)
		return 0;

	/* Keys past the tables type nothing. */
	if (key >= FIELD_TABLE_SIZE)
		return 0;

	/* The shifted character. */
	if ((modifiers & FM_MOD_SHIFT) != 0U)
		return field_shifted[key];

	/* The plain character. */
	return field_plain[key];
}

/*
 * Replaces a field's text, selecting all of it (the cursor at its end).
 */
void
fm_field_set(
	struct fm_field *field,
	const char *text)
{
	size_t length;

	/* The text, cut to the field's size. */
	length = strlen(text);
	if (length >= sizeof(field->text))
		length = sizeof(field->text) - 1U;
	memcpy(field->text, text, length);
	field->text[length] = '\0';
	field->length = length;

	/* All of it selected. */
	field->anchor = 0;
	field->cursor = length;
}

/*
 * Selects part of a field's text (byte offsets; the cursor at the end).
 */
void
fm_field_select(
	struct fm_field *field,
	size_t start,
	size_t end)
{
	/* Offsets past the text are its end. */
	if (start > field->length)
		start = field->length;
	if (end > field->length)
		end = field->length;

	/* The selection runs from the anchor to the cursor. */
	field->anchor = start;
	field->cursor = end;
}

/*
 * Handles a key pressed in a field and reports what it meant: a change of
 * the text, the end of editing (Enter, Esc) or nothing the field knew.
 */
unsigned
fm_field_key(
	struct fm_field *field,
	uint32_t key,
	uint32_t modifiers)
{
	char character;
	int extend;

	/* Shift extends the selection with the cursor keys. */
	extend = 0;
	if ((modifiers & FM_MOD_SHIFT) != 0U)
		extend = 1;

	/* The keys that edit or move. */
	switch (key) {
	case FIELD_KEY_ENTER:
	case FIELD_KEY_KPENTER:
		return FM_FIELD_ENTER;
	case FIELD_KEY_ESC:
		return FM_FIELD_CANCEL;
	case FIELD_KEY_TAB:
		return FM_FIELD_NONE;
	case FIELD_KEY_LEFT:
		if (field->anchor != field->cursor && extend == 0) {
			if (field->anchor < field->cursor)
				field_move(field, field->anchor, 0);
			else
				field_move(field, field->cursor, 0);
			return FM_FIELD_MOVED;
		}

		/* Without a selection, one character to the left. */
		field_move(field, field_previous(field, field->cursor), extend);
		return FM_FIELD_MOVED;
	case FIELD_KEY_RIGHT:
		if (field->anchor != field->cursor && extend == 0) {
			if (field->anchor > field->cursor)
				field_move(field, field->anchor, 0);
			else
				field_move(field, field->cursor, 0);
			return FM_FIELD_MOVED;
		}

		/* Without a selection, one character to the right. */
		field_move(field, field_next(field, field->cursor), extend);
		return FM_FIELD_MOVED;
	case FIELD_KEY_HOME:
		field_move(field, 0, extend);
		return FM_FIELD_MOVED;
	case FIELD_KEY_END:
		field_move(field, field->length, extend);
		return FM_FIELD_MOVED;
	case FIELD_KEY_BACKSPACE:
		if (field->anchor == field->cursor)
			field->anchor = field_previous(field, field->cursor);
		field_delete_selection(field);
		return FM_FIELD_CHANGED;
	case FIELD_KEY_DELETE:
		if (field->anchor == field->cursor)
			field->anchor = field_next(field, field->cursor);
		field_delete_selection(field);
		return FM_FIELD_CHANGED;
	default:
		break;
	}

	/* Ctrl+A selects everything. */
	if (key == FIELD_KEY_A && modifiers == FM_MOD_CTRL) {
		field->anchor = 0;
		field->cursor = field->length;
		return FM_FIELD_MOVED;
	}

	/* A character replaces the selection. */
	character = fm_key_character(key, modifiers);
	if (character == 0)
		return FM_FIELD_NONE;
	fm_field_insert(field, &character, 1U);

	/* The text changed. */
	return FM_FIELD_CHANGED;
}

/*
 * Inserts text at the cursor, replacing the selection; what does not fit
 * is dropped at a character boundary.
 */
void
fm_field_insert(
	struct fm_field *field,
	const char *text,
	size_t length)
{
	/* The selection goes first. */
	field_delete_selection(field);

	/* Only what fits; a cut text is cut before a continuation byte. */
	if (length > sizeof(field->text) - 1U - field->length) {
		length = sizeof(field->text) - 1U - field->length;
		while (length > 0 && ((unsigned char)text[length] & 0xc0U) == 0x80U)
			length--;
	}

	/* The text after the cursor moves over, the new text goes in. */
	memmove(field->text + field->cursor + length, field->text + field->cursor, field->length - field->cursor + 1U);
	memcpy(field->text + field->cursor, text, length);
	field->length += length;
	field->cursor += length;
	field->anchor = field->cursor;
}

/*
 * Deletes bytes before the cursor, back to a character's start, as an
 * input method asks; a selection goes first.
 */
void
fm_field_delete_before(
	struct fm_field *field,
	size_t bytes)
{
	size_t start;

	/* A selection goes as the deletion. */
	if (field->anchor != field->cursor) {
		field_delete_selection(field);
		return;
	}

	/* The first byte deleted, back to a character's start. */
	start = 0;
	if (bytes < field->cursor)
		start = field->cursor - bytes;
	while (start > 0 && ((unsigned char)field->text[start] & 0xc0U) == 0x80U)
		start--;

	/* The bytes after the cursor close up. */
	memmove(field->text + start, field->text + field->cursor, field->length - field->cursor + 1U);
	field->length -= field->cursor - start;
	field->cursor = start;
	field->anchor = start;
}

/*
 * Gives an input method's text to the name being changed (ws090-p022): a
 * commit in place of the selection (without control characters), bytes
 * deleted before the cursor, or the text being composed, drawn at the
 * cursor.  Nothing happens while no name is being changed.
 */
void
fm_field_text_input(
	struct fm_app *app,
	const struct fm_event *event)
{
	char clean[FM_TEXT_INPUT_MAX];
	size_t length;
	size_t at;
	unsigned char byte;

	/* Only while a name is being changed. */
	if (app->focus != FM_FOCUS_RENAME) {
		app->preedit[0] = '\0';
		return;
	}
	app->dirty = 1;

	/* The text being composed replaces the one before. */
	if (event->type == FM_EVENT_PREEDIT) {
		(void)snprintf(app->preedit, sizeof(app->preedit), "%s", event->text);
		return;
	}

	/* Bytes before the cursor. */
	if (event->type == FM_EVENT_TEXT_DELETE) {
		fm_field_delete_before(&app->rename, (size_t)event->before);
		return;
	}

	/* A commit, without control characters, which a name does not hold. */
	length = 0;
	for (at = 0; event->text[at] != '\0' && length + 1U < sizeof(clean); at++) {
		byte = (unsigned char)event->text[at];
		if (byte < 0x20U || byte == 0x7fU)
			continue;
		if (byte == '/')
			continue;
		clean[length] = (char)byte;
		length++;
	}
	clean[length] = '\0';
	fm_field_insert(&app->rename, clean, length);
}

/*
 * Draws a field's text in a rectangle at a size: the selection lit, the
 * cursor a thin bar, the text scrolled so that the cursor is in sight; a
 * faint placeholder when empty.
 */
void
fm_field_draw(
	struct fm_app *app,
	struct kl_canvas *canvas,
	const struct fm_field *field,
	const struct kl_rect *rect,
	unsigned pixels,
	const char *placeholder)
{
	size_t first;
	size_t last;
	int baseline;
	int cursor_x;
	int start_x;
	int end_x;
	int shift;
	int composed;
	int composing;

	/* The baseline, the text's left end, and the cursor's place in it. */
	baseline = kl_text_center(pixels, rect->y, rect->height);
	cursor_x = kl_text_width(app->text, field->text, field->cursor, pixels, 0);

	/* The text an input method is composing for the name being changed, shown at the cursor (ws090-p022). */
	composing = 0;
	if (field == &app->rename &&
	    app->focus == FM_FOCUS_RENAME &&
	    app->preedit[0] != '\0')
		composing = 1;
	composed = 0;
	if (composing)
		composed = kl_text_width(app->text, app->preedit, strlen(app->preedit), pixels, 0);

	/* The text moves left when the cursor (after the composed text) would be past the right end. */
	shift = 0;
	if (cursor_x + composed > rect->width - 4)
		shift = cursor_x + composed - (rect->width - 4);
	kl_canvas_clip_push(canvas, rect);

	/* An empty field shows its placeholder. */
	if (field->length == 0 && placeholder != NULL)
		(void)kl_text_draw(app->text, canvas, rect->x, baseline, placeholder, strlen(placeholder), pixels, 0, FM_COLOR_TEXT_FAINT);

	/* The selection's ground. */
	if (field->anchor != field->cursor) {
		first = field->anchor;
		last = field->cursor;
		if (first > last) {
			first = field->cursor;
			last = field->anchor;
		}

		/* Where the selection starts and ends on the screen. */
		start_x = kl_text_width(app->text, field->text, first, pixels, 0);
		end_x = kl_text_width(app->text, field->text, last, pixels, 0);
		kl_canvas_round(canvas, (float)(rect->x + start_x - shift), (float)(rect->y + 3), (float)(end_x - start_x), (float)(rect->height - 6), 3.0f, KL_RGBA(0x2f7cf6, 70));
	}

	/* The text, and the text an input method is composing at the cursor, underlined (the name being changed, ws090-p022). */
	(void)kl_text_draw(app->text, canvas, rect->x - shift, baseline, field->text, field->cursor, pixels, 0, FM_COLOR_TEXT);
	if (composing) {
		(void)kl_text_draw(app->text, canvas, rect->x + cursor_x - shift, baseline, app->preedit, strlen(app->preedit), pixels, 0, FM_COLOR_TEXT);
		kl_canvas_round(canvas, (float)(rect->x + cursor_x - shift), (float)(baseline + 3), (float)composed, 1.0f, 0.0f, FM_COLOR_TEXT);
	}
	(void)kl_text_draw(app->text, canvas, rect->x + cursor_x + composed - shift, baseline, field->text + field->cursor, field->length - field->cursor, pixels, 0, FM_COLOR_TEXT);
	kl_canvas_round(canvas, (float)(rect->x + cursor_x + composed - shift), (float)(rect->y + 4), 1.5f, (float)(rect->height - 8), 0.5f, FM_COLOR_ACCENT);
	kl_canvas_clip_pop(canvas);

	/* Where the cursor is, for the window's text input. */
	if (field == &app->rename) {
		app->caret.x = rect->x + cursor_x + composed - shift;
		app->caret.y = rect->y + 4;
		app->caret.width = 2;
		app->caret.height = rect->height - 8;
		app->caret_known = 1;
	}
}

/* Returns the offset of the character before one (0 at the start). */
static size_t
field_previous(
	const struct fm_field *field,
	size_t offset)
{
	/* Back over continuation bytes to the character's first byte. */
	if (offset == 0)
		return 0;
	offset--;
	while (offset > 0 && ((unsigned char)field->text[offset] & 0xc0U) == 0x80U)
		offset--;

	/* Reports that byte's offset. */
	return offset;
}

/* Returns the offset of the character after one (the end at the end). */
static size_t
field_next(
	const struct fm_field *field,
	size_t offset)
{
	/* Past the first byte and its continuation bytes. */
	if (offset >= field->length)
		return field->length;
	offset++;
	while (offset < field->length && ((unsigned char)field->text[offset] & 0xc0U) == 0x80U)
		offset++;

	/* Reports the next character's offset. */
	return offset;
}

/* Deletes the selected text; the cursor goes where it began. */
static void
field_delete_selection(
	struct fm_field *field)
{
	size_t first;
	size_t last;

	/* The selection's two ends in order. */
	first = field->anchor;
	last = field->cursor;
	if (first > last) {
		first = field->cursor;
		last = field->anchor;
	}

	/* The text after the selection moves over it. */
	memmove(field->text + first, field->text + last, field->length - last + 1U);
	field->length -= last - first;
	field->cursor = first;
	field->anchor = first;
}

/* Moves the cursor, the selection's anchor staying when extending and following otherwise. */
static void
field_move(
	struct fm_field *field,
	size_t offset,
	int extend)
{
	/* The cursor's new place. */
	field->cursor = offset;

	/* Without extending, nothing stays selected. */
	if (extend == 0)
		field->anchor = offset;
}
