/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The on-screen keyboard's layouts (keyboard-layout.c, ws102-p003): the
 * flick panel's faces and keys, which character a flick in a direction
 * types, and the voiced and small forms of the kana.  It knows nothing of
 * Wayland or drawing, so the host's tests read it directly.
 */

#ifndef ZWL_KEYBOARD_H
#define ZWL_KEYBOARD_H

#include <stddef.h>

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

/*
 * One key of a face: its label, what it does, and the characters (UTF-8)
 * it types in each direction, NULL where a direction types nothing.
 */
struct zwl_flick_key {
	const char *label;
	unsigned action;
	const char *text[ZWL_FLICK_DIRECTIONS];
};

const struct zwl_flick_key *zwl_flick_key(unsigned face, unsigned row, unsigned column);
const char *zwl_flick_face_name(unsigned face);
unsigned zwl_flick_face_next(unsigned face);
unsigned zwl_flick_direction(int dx, int dy, int key_size);
const char *zwl_flick_text(const struct zwl_flick_key *key, unsigned direction);
int zwl_flick_voice(const char *previous, char *next, size_t size);
int zwl_flick_case(const char *previous, char *next, size_t size);
const char *zwl_flick_direction_name(unsigned direction);

#endif
