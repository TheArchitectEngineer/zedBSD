/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The one-line text field of the library (ws090-p005), in Settings' look
 * (settings/page-network.c: white with 8-pixel corners, the accent's edge
 * while it has the keyboard, a thin caret) and with the editing of the
 * file chooser's and Files' fields: characters of the US layout (an input
 * method arrives with WS095), Left and Right (with Shift, the selection),
 * Home and End, Backspace and Delete, Ctrl+A; Enter submits and Esc
 * cancels.  A click or a tap takes the keyboard and puts the caret at the
 * character nearest the point.  A secret field shows its characters as
 * dots.
 */

#include "internal.h"

#include <string.h>

/* The text's size and its margin inside the field. */
#define FIELD_TEXT		14U
#define FIELD_SIDE		12

/* The dot a secret field shows for each character, in UTF-8. */
#define FIELD_DOT		"\xe2\x80\xa2"

static int field_wants(uint32_t code, unsigned modifiers);
static unsigned field_key(struct kl_field *field, uint32_t code, unsigned modifiers);
static void field_erase(struct kl_field *field);
static size_t field_prev(const struct kl_field *field, size_t at);
static size_t field_next(const struct kl_field *field, size_t at);
static size_t field_shown(const struct kl_field *field, char *out, size_t size, size_t through);
static size_t field_at(const struct kl_style *style, const struct kl_field *field, int x);

/*
 * Sets a field's text, with the caret at its end and nothing selected.
 */
void
kl_field_set(
	struct kl_field *field,
	const char *text)
{
	/* The text, cut to the field. */
	strncpy(field->text, text, sizeof(field->text) - 1U);
	field->text[sizeof(field->text) - 1U] = '\0';
	field->length = strlen(field->text);

	/* The caret at the end. */
	field->caret = field->length;
	field->anchor = field->length;
	field->scroll = 0;
}

/*
 * Draws a text field in a rectangle and takes its input; reports what
 * happened (KL_FIELD_* bits).  placeholder (may be NULL) shows while it
 * is empty.
 */
unsigned
kl_field(
	struct kl_ui *ui,
	const struct kl_style *style,
	uint32_t id,
	const struct kl_rect *rect,
	struct kl_field *field,
	const char *placeholder)
{
	const struct kl_theme *theme;
	struct kl_rect inside;
	char shown[KL_FIELD_MAX * 3U];
	uint32_t code;
	unsigned modifiers;
	unsigned changes;
	unsigned state;
	size_t length;
	size_t start;
	size_t end;
	int caret_x;
	int left_x;
	int right_x;
	int baseline;
	int width;
	int taken;
	int focused;
	double pointer_x;
	double pointer_y;

	/* The record: it takes the keyboard. */
	theme = style->theme;
	state = keiui_ui_widget(ui, id, 0U, rect, KEIUI_FOCUSABLE);
	focused = 0;
	if ((state & KL_HIT_FOCUSED) != 0U)
		focused = 1;

	/* A click or a tap puts the caret at the point (twice: the whole text selected). */
	changes = 0;
	if ((state & KL_HIT_CLICKED) != 0U) {
		kl_ui_pointer(ui, &pointer_x, &pointer_y);
		field->caret = field_at(style, field, (int)pointer_x - rect->x - FIELD_SIDE + field->scroll);
		field->anchor = field->caret;
		if ((state & KL_HIT_DOUBLE) != 0U) {
			field->anchor = 0;
			field->caret = field->length;
		}
	}

	/* The keys while it has the keyboard. */
	for (;;) {
		taken = keiui_ui_take_key(ui, id, 0U, field_wants, &code, &modifiers);
		if (!taken)
			break;
		changes |= field_key(field, code, modifiers);
	}

	/* The ground: white, with the accent's edge while it has the keyboard. */
	kl_canvas_round(style->canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, theme->control_radius, theme->panel);
	if (focused)
		kl_canvas_round_border(style->canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, theme->control_radius, 1.5f, theme->accent);
	else
		kl_canvas_round_border(style->canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, theme->control_radius, 1.0f, theme->control_edge);

	/* The text as shown (dots for a secret one), and where the caret and the selection's ends are in it. */
	length = field_shown(field, shown, sizeof(shown), field->length);
	start = field->anchor;
	end = field->caret;
	if (start > end) {
		start = field->caret;
		end = field->anchor;
	}

	/* Where the caret and the selection's ends stand across. */
	caret_x = kl_text_width(style->text, shown, field_shown(field, NULL, 0U, field->caret), FIELD_TEXT, 0);
	left_x = kl_text_width(style->text, shown, field_shown(field, NULL, 0U, start), FIELD_TEXT, 0);
	right_x = kl_text_width(style->text, shown, field_shown(field, NULL, 0U, end), FIELD_TEXT, 0);

	/* The text scrolls across just enough to keep the caret inside. */
	width = rect->width - 2 * FIELD_SIDE;
	if (caret_x - field->scroll > width)
		field->scroll = caret_x - width;
	if (caret_x < field->scroll)
		field->scroll = caret_x;

	/* Inside the field: the selection, the text or the placeholder, and the caret while it has the keyboard. */
	inside.x = rect->x + FIELD_SIDE / 2;
	inside.y = rect->y;
	inside.width = rect->width - FIELD_SIDE;
	inside.height = rect->height;
	kl_canvas_clip_push(style->canvas, &inside);
	baseline = kl_text_center(FIELD_TEXT, rect->y, rect->height);
	if (start != end && focused)
		kl_canvas_round(style->canvas, (float)(rect->x + FIELD_SIDE + left_x - field->scroll), (float)rect->y + 7.0f, (float)(right_x - left_x), (float)rect->height - 14.0f, 2.0f, theme->selection);
	if (field->length == 0 && placeholder != NULL)
		(void)kl_text_draw(style->text, style->canvas, rect->x + FIELD_SIDE, baseline, placeholder, strlen(placeholder), FIELD_TEXT, 0, theme->text_faint);
	else
		(void)kl_text_draw(style->text, style->canvas, rect->x + FIELD_SIDE - field->scroll, baseline, shown, length, FIELD_TEXT, 0, theme->text);
	if (focused)
		kl_canvas_line(style->canvas, (float)(rect->x + FIELD_SIDE + caret_x - field->scroll) + 0.75f, (float)rect->y + 8.0f, (float)(rect->x + FIELD_SIDE + caret_x - field->scroll) + 0.75f, (float)(rect->y + rect->height) - 8.0f, 1.5f, theme->accent);
	kl_canvas_clip_pop(style->canvas);

	/* Reports what happened. */
	return changes;
}

/* Tells whether a field takes a key: its characters, its editing keys, Enter and Esc (with Control, only A). */
static int
field_wants(
	uint32_t code,
	unsigned modifiers)
{
	uint32_t character;

	/* Control's only key is A (select all); Alt and Super are commands. */
	if ((modifiers & (KL_MOD_ALT | KL_MOD_SUPER)) != 0U)
		return 0;
	if ((modifiers & KL_MOD_CTRL) != 0U) {
		if (code == 30U)
			return 1;
		return 0;
	}

	/* The editing keys. */
	switch (code) {
	case KL_KEY_LEFT:
	case KL_KEY_RIGHT:
	case KL_KEY_HOME:
	case KL_KEY_END:
	case KL_KEY_BACKSPACE:
	case KL_KEY_DELETE:
	case KL_KEY_ENTER:
	case KL_KEY_KPENTER:
	case KL_KEY_ESC:
		return 1;
	default:
		break;
	}

	/* A key that types a character. */
	character = kl_key_character(code, modifiers);
	if (character != 0U)
		return 1;
	return 0;
}

/* Carries out one key in a field; reports what happened (KL_FIELD_* bits). */
static unsigned
field_key(
	struct kl_field *field,
	uint32_t code,
	unsigned modifiers)
{
	uint32_t character;
	unsigned shift;
	size_t at;

	/* Shift keeps the other end of the selection where it is. */
	shift = modifiers & KL_MOD_SHIFT;

	/* The keys that move, erase, submit and cancel. */
	switch (code) {
	case KL_KEY_LEFT:
		at = field_prev(field, field->caret);
		if (field->caret != field->anchor && shift == 0U && field->anchor < field->caret)
			at = field->anchor;
		field->caret = at;
		if (shift == 0U)
			field->anchor = at;
		return 0;
	case KL_KEY_RIGHT:
		at = field_next(field, field->caret);
		if (field->caret != field->anchor && shift == 0U && field->anchor > field->caret)
			at = field->anchor;
		field->caret = at;
		if (shift == 0U)
			field->anchor = at;
		return 0;
	case KL_KEY_HOME:
		field->caret = 0;
		if (shift == 0U)
			field->anchor = 0;
		return 0;
	case KL_KEY_END:
		field->caret = field->length;
		if (shift == 0U)
			field->anchor = field->length;
		return 0;
	case KL_KEY_BACKSPACE:
		if (field->caret == field->anchor)
			field->anchor = field_prev(field, field->caret);
		field_erase(field);
		return KL_FIELD_CHANGED;
	case KL_KEY_DELETE:
		if (field->caret == field->anchor)
			field->anchor = field_next(field, field->caret);
		field_erase(field);
		return KL_FIELD_CHANGED;
	case KL_KEY_ENTER:
	case KL_KEY_KPENTER:
		return KL_FIELD_SUBMITTED;
	case KL_KEY_ESC:
		return KL_FIELD_CANCELLED;
	default:
		break;
	}

	/* Ctrl+A selects the whole text. */
	if ((modifiers & KL_MOD_CTRL) != 0U) {
		field->anchor = 0;
		field->caret = field->length;
		return 0;
	}

	/* A character replaces the selection (a full field takes no more). */
	character = kl_key_character(code, modifiers);
	if (character == 0U)
		return 0;
	field_erase(field);
	if (field->length + 1U >= sizeof(field->text))
		return KL_FIELD_CHANGED;
	memmove(field->text + field->caret + 1U, field->text + field->caret, field->length - field->caret + 1U);
	field->text[field->caret] = (char)character;
	field->length++;
	field->caret++;
	field->anchor = field->caret;

	/* Succeeded: the text changed. */
	return KL_FIELD_CHANGED;
}

/* Erases the selection (nothing when the caret and the anchor meet). */
static void
field_erase(
	struct kl_field *field)
{
	size_t start;
	size_t end;

	/* The selection's ends in order. */
	start = field->anchor;
	end = field->caret;
	if (start > end) {
		start = field->caret;
		end = field->anchor;
	}

	/* The bytes after it close up. */
	memmove(field->text + start, field->text + end, field->length - end + 1U);
	field->length -= end - start;
	field->caret = start;
	field->anchor = start;
}

/* Reports the start of the character before an offset. */
static size_t
field_prev(
	const struct kl_field *field,
	size_t at)
{
	/* Nothing before the start. */
	if (at == 0U)
		return 0U;

	/* Back over the continuation bytes. */
	at--;
	while (at > 0U && ((unsigned char)field->text[at] & 0xc0U) == 0x80U)
		at--;

	/* Reports the character's start. */
	return at;
}

/* Reports the offset after the character at an offset. */
static size_t
field_next(
	const struct kl_field *field,
	size_t at)
{
	/* Nothing after the end. */
	if (at >= field->length)
		return field->length;

	/* On over the continuation bytes. */
	at++;
	while (at < field->length && ((unsigned char)field->text[at] & 0xc0U) == 0x80U)
		at++;

	/* Reports the next character's start. */
	return at;
}

/*
 * Writes the text as shown (a dot for each character of a secret field)
 * into out (when not NULL), up to the byte offset through of the text, and
 * reports the length shown up to there.
 */
static size_t
field_shown(
	const struct kl_field *field,
	char *out,
	size_t size,
	size_t through)
{
	size_t length;
	size_t at;

	/* A plain field shows its bytes. */
	if (!field->secret) {
		if (out != NULL) {
			memcpy(out, field->text, field->length + 1U);
			return field->length;
		}

		/* Without an output: the offset itself. */
		return through;
	}

	/* A secret one a dot for each character up to the offset. */
	length = 0;
	at = 0;
	while (at < through && at < field->length) {
		at = field_next(field, at);
		if (out != NULL && length + sizeof(FIELD_DOT) < size)
			memcpy(out + length, FIELD_DOT, sizeof(FIELD_DOT));
		length += sizeof(FIELD_DOT) - 1U;
	}

	/* Reports the shown length. */
	return length;
}

/* Reports the byte offset of the character boundary nearest a place (pixels from the text's start). */
static size_t
field_at(
	const struct kl_style *style,
	const struct kl_field *field,
	int x)
{
	char shown[KL_FIELD_MAX * 3U];
	size_t best;
	size_t at;
	int width;
	int distance;
	int nearest;

	/* The text as shown. */
	(void)field_shown(field, shown, sizeof(shown), field->length);

	/* Each boundary, the nearest one kept. */
	best = 0;
	nearest = -1;
	at = 0;
	for (;;) {
		width = kl_text_width(style->text, shown, field_shown(field, NULL, 0U, at), FIELD_TEXT, 0);
		distance = width - x;
		if (distance < 0)
			distance = -distance;
		if (nearest < 0 || distance < nearest) {
			nearest = distance;
			best = at;
		}

		/* Stops at the end, else the next character. */
		if (at >= field->length)
			break;
		at = field_next(field, at);
	}

	/* Reports the nearest boundary. */
	return best;
}
