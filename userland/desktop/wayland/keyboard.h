/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The on-screen keyboard's layouts (keyboard-layout.c, ws102-p003): the
 * flick panel's faces and keys, which character a flick in a direction
 * types, and the voiced and small forms of the kana; the QWERTY panel's
 * rows (p006); and the handwriting face's ink and recognizer
 * (keyboard-hand.c, p008).  It knows nothing of Wayland or drawing, so the
 * host's tests read it directly.
 */

#ifndef ZWL_KEYBOARD_H
#define ZWL_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

/* The flick panel's grid: four columns (the last is the fixed keys) and four rows. */
#define ZWL_FLICK_COLUMNS	4U
#define ZWL_FLICK_ROWS		4U

/* The flick panel's faces, in the order the face key goes through them. */
#define ZWL_FLICK_KANA		0U
#define ZWL_FLICK_ALPHA		1U
#define ZWL_FLICK_NUMBER	2U
#define ZWL_FLICK_FACES		3U

/* The directions of a flick: none (a tap), then left, up, right and down. */
#define ZWL_FLICK_CENTER	0U
#define ZWL_FLICK_LEFT		1U
#define ZWL_FLICK_UP		2U
#define ZWL_FLICK_RIGHT		3U
#define ZWL_FLICK_DOWN		4U
#define ZWL_FLICK_DIRECTIONS	5U

/* The evdev codes of the keys the keyboard sends besides the characters'. */
#define ZWL_FLICK_KEY_BACKSPACE	14U
#define ZWL_FLICK_KEY_ENTER	28U
#define ZWL_FLICK_KEY_SPACE	57U

/*
 * What a key does besides typing its characters: nothing more (a
 * character key), delete the character before the cursor, a space, a new
 * line, go to the next face, cycle the last kana through its voiced and
 * small forms, or change the last letter's case.
 */
#define ZWL_FLICK_TYPE		0U
#define ZWL_FLICK_BACKSPACE	1U
#define ZWL_FLICK_SPACE		2U
#define ZWL_FLICK_ENTER		3U
#define ZWL_FLICK_FACE		4U
#define ZWL_FLICK_VOICE		5U
#define ZWL_FLICK_CASE		6U
#define ZWL_FLICK_SHIFT		7U
#define ZWL_FLICK_ARROW		8U
#define ZWL_FLICK_CTRL		9U
#define ZWL_FLICK_ALT		10U

/*
 * The QWERTY panel's faces (ws102-p006): the letters (with Shift, the
 * capitals and the digits' symbols) and the symbols; its rows, the most
 * keys a row has, and a row's width in quarter keys.  The rows count from
 * the top: the extra keys (Esc, Tab, Ctrl, Alt, a few symbols, Home, End,
 * PgUp, PgDn; ws102-p020), the digits, three rows of letters (or symbols)
 * and the space row.
 */
#define ZWL_QWERTY_LETTERS	0U
#define ZWL_QWERTY_SYMBOLS	1U
#define ZWL_QWERTY_FACES	2U
#define ZWL_QWERTY_ROWS		6U
#define ZWL_QWERTY_EXTRA_ROW	0U
#define ZWL_QWERTY_ROW_KEYS	12U
#define ZWL_QWERTY_ROW_UNITS	40U

/* The evdev codes of the keys sent by their code (ZWL_FLICK_ARROW): the arrows, Esc, Tab, Home, End, PgUp, PgDn. */
#define ZWL_KEY_ESC		1U
#define ZWL_KEY_TAB		15U
#define ZWL_KEY_HOME		102U
#define ZWL_KEY_UP		103U
#define ZWL_KEY_PAGE_UP		104U
#define ZWL_KEY_LEFT		105U
#define ZWL_KEY_RIGHT		106U
#define ZWL_KEY_END		107U
#define ZWL_KEY_DOWN		108U
#define ZWL_KEY_PAGE_DOWN	109U

/*
 * One key of a face: its label, what it does, and the characters (UTF-8)
 * it types in each direction, NULL where a direction types nothing.
 */
struct zwl_flick_key {
	const char *label;
	unsigned action;
	const char *text[ZWL_FLICK_DIRECTIONS];
};

/*
 * One key of the QWERTY panel: its label and its label with Shift, what it
 * types without and with Shift (NULL for a key that only acts), what it
 * does (ZWL_FLICK_*), the evdev code of a key sent by its code (an arrow,
 * Esc, Tab, ...), and its width in quarter keys.
 */
struct zwl_qwerty_key {
	const char *label;
	const char *shifted_label;
	const char *text;
	const char *shifted;
	unsigned action;
	unsigned code;
	unsigned width;
};

/*
 * Handwriting (keyboard-hand.c, ws102-p008): the most strokes and points
 * a written character keeps, the most candidates a recognizer returns, and
 * the longest candidate (UTF-8, with its end).
 */
#define ZWL_HAND_STROKES	64U
#define ZWL_HAND_POINTS		512U
#define ZWL_HAND_CANDIDATES	4U
#define ZWL_HAND_TEXT		32U

/* One point of a stroke, in output pixels. */
struct zwl_hand_point {
	int16_t x;
	int16_t y;
};

/*
 * One stroke of the pen or finger, from its press to its release: its
 * points in order (the last ones dropped past ZWL_HAND_POINTS).
 */
struct zwl_hand_stroke {
	unsigned count;
	struct zwl_hand_point points[ZWL_HAND_POINTS];
};

/*
 * The ink written on the handwriting face since it was last cleared: its
 * strokes in order (a stroke past ZWL_HAND_STROKES is not kept).
 */
struct zwl_hand_ink {
	unsigned count;
	struct zwl_hand_stroke strokes[ZWL_HAND_STROKES];
};

/*
 * What a recognizer answers: its candidates (UTF-8, the likeliest first)
 * and a note to show over them (empty for none).
 */
struct zwl_hand_result {
	unsigned count;
	char candidates[ZWL_HAND_CANDIDATES][ZWL_HAND_TEXT];
	char note[ZWL_HAND_TEXT];
};

const struct zwl_flick_key *zwl_flick_key(unsigned face, unsigned row, unsigned column);
const char *zwl_flick_face_name(unsigned face);
unsigned zwl_flick_face_next(unsigned face);
unsigned zwl_flick_direction(int dx, int dy, int key_size);
const char *zwl_flick_text(const struct zwl_flick_key *key, unsigned direction);
int zwl_flick_voice(const char *previous, char *next, size_t size);
int zwl_flick_case(const char *previous, char *next, size_t size);
const char *zwl_flick_direction_name(unsigned direction);
int zwl_flick_us_key(const char *text, unsigned *code, int *shift);
const struct zwl_qwerty_key *zwl_qwerty_row(unsigned face, unsigned row, unsigned *count);
const char *zwl_qwerty_face_name(unsigned face);
void zwl_hand_clear(struct zwl_hand_ink *ink);
int zwl_hand_begin(struct zwl_hand_ink *ink, int32_t x, int32_t y);
int zwl_hand_add(struct zwl_hand_ink *ink, int32_t x, int32_t y);
unsigned zwl_hand_points(const struct zwl_hand_ink *ink);
void zwl_hand_bounds(const struct zwl_hand_ink *ink, int32_t *rect);
void zwl_hand_recognize(const struct zwl_hand_ink *ink, struct zwl_hand_result *result);


#endif
