/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The flick panel's layouts (ws102-p003, plan/ws102/design.md §2.4).
 *
 * Each face is a grid of four columns and four rows.  The three left
 * columns are the twelve keys of a telephone keypad; the last column is the
 * same on every face: delete, space, enter and the face key.  A key types
 * its centre character when it is tapped and the character of a direction
 * when it is flicked that way; a flick is a movement from the press of at
 * least max(16, a third of the key) pixels, its direction the nearer axis.
 *
 * The kana face is the Japanese twelve-key layout (あ in the middle,
 * い う え お to the left, up, right and down); its voice key turns the
 * last kana into its voiced, half-voiced or small form and back.  The alpha
 * face is the telephone's letters, with a case key; the number face has
 * the digits, with the ASCII symbols the alpha face does not have around
 * them.  Between the alpha and the number face every lower-case letter,
 * digit and printable ASCII symbol can be typed (the host's tests check
 * it).
 */

#include "keyboard.h"

#include <string.h>

/* The smallest movement that is a flick, and the share of a key's side it grows to (in tenths). */
#define LAYOUT_FLICK_MIN	16
#define LAYOUT_FLICK_TENTHS	3

/*
 * The keys of every face, row by row, column by column: [face][row][column].
 * The texts are centre, left, up, right, down.
 */
static const struct zwl_flick_key layout_keys[ZWL_FLICK_FACES][ZWL_FLICK_ROWS][ZWL_FLICK_COLUMNS] = {
	{
		{
			{ "あ", ZWL_FLICK_TYPE, { "あ", "い", "う", "え", "お" } },
			{ "か", ZWL_FLICK_TYPE, { "か", "き", "く", "け", "こ" } },
			{ "さ", ZWL_FLICK_TYPE, { "さ", "し", "す", "せ", "そ" } },
			{ "Del", ZWL_FLICK_BACKSPACE, { NULL, NULL, NULL, NULL, NULL } }
		},
		{
			{ "た", ZWL_FLICK_TYPE, { "た", "ち", "つ", "て", "と" } },
			{ "な", ZWL_FLICK_TYPE, { "な", "に", "ぬ", "ね", "の" } },
			{ "は", ZWL_FLICK_TYPE, { "は", "ひ", "ふ", "へ", "ほ" } },
			{ "空白", ZWL_FLICK_SPACE, { " ", NULL, NULL, NULL, NULL } }
		},
		{
			{ "ま", ZWL_FLICK_TYPE, { "ま", "み", "む", "め", "も" } },
			{ "や", ZWL_FLICK_TYPE, { "や", "（", "ゆ", "）", "よ" } },
			{ "ら", ZWL_FLICK_TYPE, { "ら", "り", "る", "れ", "ろ" } },
			{ "改行", ZWL_FLICK_ENTER, { "\n", NULL, NULL, NULL, NULL } }
		},
		{
			{ "゛゜小", ZWL_FLICK_VOICE, { NULL, NULL, NULL, NULL, NULL } },
			{ "わ", ZWL_FLICK_TYPE, { "わ", "を", "ん", "ー", "〜" } },
			{ "、", ZWL_FLICK_TYPE, { "、", "。", "？", "！", "…" } },
			{ "A", ZWL_FLICK_FACE, { NULL, NULL, NULL, NULL, NULL } }
		}
	},
	{
		{
			{ "@", ZWL_FLICK_TYPE, { "@", "#", "/", "&", "_" } },
			{ "abc", ZWL_FLICK_TYPE, { "a", "b", "c", NULL, NULL } },
			{ "def", ZWL_FLICK_TYPE, { "d", "e", "f", NULL, NULL } },
			{ "Del", ZWL_FLICK_BACKSPACE, { NULL, NULL, NULL, NULL, NULL } }
		},
		{
			{ "ghi", ZWL_FLICK_TYPE, { "g", "h", "i", NULL, NULL } },
			{ "jkl", ZWL_FLICK_TYPE, { "j", "k", "l", NULL, NULL } },
			{ "mno", ZWL_FLICK_TYPE, { "m", "n", "o", NULL, NULL } },
			{ "space", ZWL_FLICK_SPACE, { " ", NULL, NULL, NULL, NULL } }
		},
		{
			{ "pqrs", ZWL_FLICK_TYPE, { "p", "q", "r", "s", NULL } },
			{ "tuv", ZWL_FLICK_TYPE, { "t", "u", "v", NULL, NULL } },
			{ "wxyz", ZWL_FLICK_TYPE, { "w", "x", "y", "z", NULL } },
			{ "Enter", ZWL_FLICK_ENTER, { "\n", NULL, NULL, NULL, NULL } }
		},
		{
			{ "a/A", ZWL_FLICK_CASE, { NULL, NULL, NULL, NULL, NULL } },
			{ "'\"()", ZWL_FLICK_TYPE, { "'", "\"", "(", ")", ":" } },
			{ ".,?!", ZWL_FLICK_TYPE, { ".", ",", "?", "!", "-" } },
			{ "1", ZWL_FLICK_FACE, { NULL, NULL, NULL, NULL, NULL } }
		}
	},
	{
		{
			{ "1", ZWL_FLICK_TYPE, { "1", "+", "-", "*", "/" } },
			{ "2", ZWL_FLICK_TYPE, { "2", "=", "%", "<", ">" } },
			{ "3", ZWL_FLICK_TYPE, { "3", "[", "]", "{", "}" } },
			{ "Del", ZWL_FLICK_BACKSPACE, { NULL, NULL, NULL, NULL, NULL } }
		},
		{
			{ "4", ZWL_FLICK_TYPE, { "4", "$", "^", "~", "\\" } },
			{ "5", ZWL_FLICK_TYPE, { "5", "|", ";", "`", ":" } },
			{ "6", ZWL_FLICK_TYPE, { "6", "(", ")", "'", "\"" } },
			{ "space", ZWL_FLICK_SPACE, { " ", NULL, NULL, NULL, NULL } }
		},
		{
			{ "7", ZWL_FLICK_TYPE, { "7", "!", "?", "@", "#" } },
			{ "8", ZWL_FLICK_TYPE, { "8", "&", "_", ".", "," } },
			{ "9", ZWL_FLICK_TYPE, { "9", NULL, NULL, NULL, NULL } },
			{ "Enter", ZWL_FLICK_ENTER, { "\n", NULL, NULL, NULL, NULL } }
		},
		{
			{ "「」", ZWL_FLICK_TYPE, { "「", "」", "・", "。", "、" } },
			{ "0", ZWL_FLICK_TYPE, { "0", NULL, NULL, NULL, NULL } },
			{ "¥€°", ZWL_FLICK_TYPE, { "¥", "€", "°", "±", "×" } },
			{ "あ", ZWL_FLICK_FACE, { NULL, NULL, NULL, NULL, NULL } }
		}
	}
};

/* The faces' names, for the log and the title band. */
static const char *const layout_face_names[ZWL_FLICK_FACES] = {
	"kana",
	"alpha",
	"number"
};

/* The directions' names, for the log. */
static const char *const layout_direction_names[ZWL_FLICK_DIRECTIONS] = {
	"center",
	"left",
	"up",
	"right",
	"down"
};

/*
 * The kana that have voiced, half-voiced or small forms, each cycle a
 * string of its forms in the order the voice key goes through them (the
 * last goes back to the first).
 */
static const char *const layout_voice_cycles[] = {
	"あぁ", "いぃ", "うぅ", "えぇ", "おぉ",
	"かが", "きぎ", "くぐ", "けげ", "こご",
	"さざ", "しじ", "すず", "せぜ", "そぞ",
	"ただ", "ちぢ", "つっづ", "てで", "とど",
	"はばぱ", "ひびぴ", "ふぶぷ", "へべぺ", "ほぼぽ",
	"やゃ", "ゆゅ", "よょ", "わゎ"
};

/*
 * The keys of the US layout (zdesktop's keymap, keymap.c) by evdev code,
 * as the characters they type without and with Shift: the code of a
 * character is its place in one of the two strings, 0 where a code types
 * none.  The codes run from 0 to 57 (the space bar).
 */
static const char layout_us_plain[] =
	"\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0\0\0 ";
static const char layout_us_shifted[] =
	"\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0\0\0 ";

/* The bytes of one kana in UTF-8 (all of them are three). */
#define LAYOUT_KANA_BYTES	3U

/*
 * Returns a key of a face by its row and column; NULL outside the grid.
 */
const struct zwl_flick_key *
zwl_flick_key(
	unsigned face,
	unsigned row,
	unsigned column)
{
	/* Only the faces and the grid. */
	if (face >= ZWL_FLICK_FACES)
		return NULL;
	if (row >= ZWL_FLICK_ROWS || column >= ZWL_FLICK_COLUMNS)
		return NULL;

	/* The key. */
	return &layout_keys[face][row][column];
}

/*
 * Returns a face's name (kana, alpha, number); "?" for none.
 */
const char *
zwl_flick_face_name(
	unsigned face)
{
	/* Only the faces. */
	if (face >= ZWL_FLICK_FACES)
		return "?";

	/* The name. */
	return layout_face_names[face];
}

/*
 * Returns the face that follows one (kana, alpha, number, then kana again).
 */
unsigned
zwl_flick_face_next(
	unsigned face)
{
	/* The next, wrapping. */
	return (face + 1U) % ZWL_FLICK_FACES;
}

/*
 * Works out the direction of a movement from a key's press (dx right,
 * dy down, in pixels) on a key of a side: the centre when it is shorter
 * than the flick's distance, otherwise the nearer axis's direction.
 */
unsigned
zwl_flick_direction(
	int dx,
	int dy,
	int key_size)
{
	long distance;
	long threshold;
	int across;
	int down;

	/* The flick's distance: a share of the key, at least the minimum. */
	threshold = (long)key_size * LAYOUT_FLICK_TENTHS / 10L;
	if (threshold < LAYOUT_FLICK_MIN)
		threshold = LAYOUT_FLICK_MIN;

	/* Shorter than that: a tap. */
	distance = (long)dx * dx + (long)dy * dy;
	if (distance < threshold * threshold)
		return ZWL_FLICK_CENTER;

	/* The nearer axis (a tie goes across). */
	across = dx;
	if (across < 0)
		across = -across;
	down = dy;
	if (down < 0)
		down = -down;

	/* Across: left or right. */
	if (across >= down) {
		if (dx < 0)
			return ZWL_FLICK_LEFT;
		return ZWL_FLICK_RIGHT;
	}

	/* Up or down (the screen's y grows down). */
	if (dy < 0)
		return ZWL_FLICK_UP;
	return ZWL_FLICK_DOWN;
}

/*
 * Returns what a key types in a direction; NULL where it types nothing
 * (a direction without a character, or a key that only acts).
 */
const char *
zwl_flick_text(
	const struct zwl_flick_key *key,
	unsigned direction)
{
	/* Only a key and a direction. */
	if (key == NULL || direction >= ZWL_FLICK_DIRECTIONS)
		return NULL;

	/* The character, or none. */
	return key->text[direction];
}

/*
 * Works out the next form of a kana for the voice key: か to が and back,
 * は to ば to ぱ and back, つ to っ to づ and back.  Returns 1 with the
 * next form (UTF-8) in next, or 0 when the text is not such a kana.
 */
int
zwl_flick_voice(
	const char *previous,
	char *next,
	size_t size)
{
	const char *cycle;
	const char *found;
	size_t cycle_length;
	size_t length;
	size_t index;
	size_t offset;

	/* Only one kana (three bytes), and room for one. */
	length = strlen(previous);
	if (length != LAYOUT_KANA_BYTES || size <= LAYOUT_KANA_BYTES)
		return 0;

	/* The cycle that has it, at a kana's boundary. */
	for (index = 0; index < sizeof(layout_voice_cycles) / sizeof(layout_voice_cycles[0]); index++) {
		/* The kana in this cycle, if it is there. */
		cycle = layout_voice_cycles[index];
		found = strstr(cycle, previous);
		if (found == NULL)
			continue;

		/* Only at a kana's boundary (not across two). */
		offset = (size_t)(found - cycle);
		if (offset % LAYOUT_KANA_BYTES != 0U)
			continue;

		/* The form after it, or the first after the last. */
		offset += LAYOUT_KANA_BYTES;
		cycle_length = strlen(cycle);
		if (offset >= cycle_length)
			offset = 0;
		memcpy(next, cycle + offset, LAYOUT_KANA_BYTES);
		next[LAYOUT_KANA_BYTES] = '\0';
		return 1;
	}

	/* Not a kana with other forms. */
	return 0;
}

/*
 * Works out the other case of a letter for the case key.  Returns 1 with
 * it in next, or 0 when the text is not one ASCII letter.
 */
int
zwl_flick_case(
	const char *previous,
	char *next,
	size_t size)
{
	char letter;

	/* Only one character, and room for it. */
	if (previous[0] == '\0' || previous[1] != '\0' || size < 2U)
		return 0;
	letter = previous[0];

	/* A lower-case letter becomes upper-case. */
	if (letter >= 'a' && letter <= 'z') {
		next[0] = (char)(letter - 'a' + 'A');
		next[1] = '\0';
		return 1;
	}

	/* An upper-case letter becomes lower-case. */
	if (letter >= 'A' && letter <= 'Z') {
		next[0] = (char)(letter - 'A' + 'a');
		next[1] = '\0';
		return 1;
	}

	/* Not a letter. */
	return 0;
}

/*
 * Returns a direction's name (center, left, up, right, down); "?" for none.
 */
const char *
zwl_flick_direction_name(
	unsigned direction)
{
	/* Only the directions. */
	if (direction >= ZWL_FLICK_DIRECTIONS)
		return "?";

	/* The name. */
	return layout_direction_names[direction];
}

/*
 * Finds the key of the US layout that types a text of one ASCII character
 * (a newline is the enter key).  Returns 1 with its evdev code and whether
 * Shift must be held, or 0 when no key types it (any other text).
 */
int
zwl_flick_us_key(
	const char *text,
	unsigned *code,
	int *shift)
{
	unsigned index;
	char character;

	/* Only one character. */
	character = text[0];
	if (character == '\0' || text[1] != '\0')
		return 0;

	/* A newline is the enter key. */
	if (character == '\n') {
		*code = ZWL_FLICK_KEY_ENTER;
		*shift = 0;
		return 1;
	}

	/* The code whose key types it, without Shift or with it. */
	for (index = 0; index < sizeof(layout_us_plain) - 1U; index++) {
		/* Without Shift. */
		if (layout_us_plain[index] == character) {
			*code = index;
			*shift = 0;
			return 1;
		}

		/* With Shift (the space bar types a space either way, found above). */
		if (layout_us_shifted[index] == character) {
			*code = index;
			*shift = 1;
			return 1;
		}
	}

	/* No key types it. */
	return 0;
}
