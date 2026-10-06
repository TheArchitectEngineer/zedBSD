/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws102-p003: the flick layout on the host (keyboard-layout.c): every
 * lower-case letter, digit, printable ASCII symbol and the space can be
 * typed on the alpha or the number face; the 46 plain kana and the long
 * vowel mark on the kana face; no face types a character twice; the
 * flick's direction from a movement; the voice key's and the case key's
 * forms.
 */

#include "keyboard.h"

#include <stdio.h>
#include <string.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);
static int face_has(unsigned face, const char *text);
static int face_repeats(unsigned face);
static int face_keys(unsigned face);
static int qwerty_has(const char *text);
static int qwerty_keys(void);
static int qwerty_widths(void);
static void check_hand(void);
static void check_emoji(void);

/* The 46 plain kana (UTF-8, three bytes each). */
static const char host_kana[] = "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん";

/* The printable ASCII symbols (32). */
static const char host_symbols[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

/* Runs the checks. */
int
main(void)
{
	char one[8];
	char next[8];
	unsigned code;
	int shift;
	size_t index;
	int missing;
	int found;
	int repeats;

	/* Every lower-case letter and digit on the alpha or number face. */
	missing = 0;
	for (index = 0; index < 36U; index++) {
		one[0] = (char)('a' + index);
		if (index >= 26U)
			one[0] = (char)('0' + index - 26U);
		one[1] = '\0';
		found = face_has(KWL_FLICK_ALPHA, one) || face_has(KWL_FLICK_NUMBER, one);
		if (!found) {
			printf("missing: %s\n", one);
			missing++;
		}
	}
	check(missing == 0, "26 letters and 10 digits");

	/* Every printable ASCII symbol and the space. */
	missing = 0;
	for (index = 0; index < strlen(host_symbols); index++) {
		one[0] = host_symbols[index];
		one[1] = '\0';
		found = face_has(KWL_FLICK_ALPHA, one) || face_has(KWL_FLICK_NUMBER, one);
		if (!found) {
			printf("missing: %s\n", one);
			missing++;
		}
	}
	found = face_has(KWL_FLICK_ALPHA, " ");
	check(missing == 0 && strlen(host_symbols) == 32U && found, "32 ASCII symbols and the space");

	/* The 46 kana and the long vowel mark on the kana face. */
	missing = 0;
	for (index = 0; index < strlen(host_kana); index += 3U) {
		memcpy(one, host_kana + index, 3U);
		one[3] = '\0';
		found = face_has(KWL_FLICK_KANA, one);
		if (!found) {
			printf("missing: %s\n", one);
			missing++;
		}
	}
	found = face_has(KWL_FLICK_KANA, "ー");
	check(missing == 0 && strlen(host_kana) == 46U * 3U && found, "46 kana and the long vowel mark");

	/* No face types a character twice. */
	for (index = 0; index < KWL_FLICK_FACES; index++) {
		repeats = face_repeats((unsigned)index);
		check(repeats == 0, kwl_flick_face_name((unsigned)index));
	}

	/* The flick's direction on a 72-pixel key (the distance 21) and a 40-pixel one (the minimum 16). */
	check(kwl_flick_direction(0, 0, 72) == KWL_FLICK_CENTER, "no movement: centre");
	check(kwl_flick_direction(10, 5, 72) == KWL_FLICK_CENTER, "11 px on a 72 px key: centre");
	check(kwl_flick_direction(30, 0, 72) == KWL_FLICK_RIGHT, "30 px right: right");
	check(kwl_flick_direction(-30, 5, 72) == KWL_FLICK_LEFT, "30 px left: left");
	check(kwl_flick_direction(5, -30, 72) == KWL_FLICK_UP, "30 px up: up");
	check(kwl_flick_direction(0, 30, 72) == KWL_FLICK_DOWN, "30 px down: down");
	check(kwl_flick_direction(25, 25, 72) == KWL_FLICK_RIGHT, "a tie goes across");
	check(kwl_flick_direction(0, 17, 40) == KWL_FLICK_DOWN, "17 px on a 40 px key (the minimum 16): down");
	check(kwl_flick_direction(0, 15, 40) == KWL_FLICK_CENTER, "15 px on a 40 px key: centre");

	/* The key in a direction: か's left is き, abc's right types nothing. */
	check(strcmp(kwl_flick_text(kwl_flick_key(KWL_FLICK_KANA, 0, 1), KWL_FLICK_LEFT), "き") == 0, "か left: き");
	check(kwl_flick_text(kwl_flick_key(KWL_FLICK_ALPHA, 0, 1), KWL_FLICK_RIGHT) == NULL, "abc right: nothing");

	/* The voice key: か が か, は ば ぱ は, つ っ づ つ; な has no other form. */
	found = kwl_flick_voice("か", next, sizeof(next));
	check(found && strcmp(next, "が") == 0, "か -> が");
	found = kwl_flick_voice("が", next, sizeof(next));
	check(found && strcmp(next, "か") == 0, "が -> か");
	found = kwl_flick_voice("ば", next, sizeof(next));
	check(found && strcmp(next, "ぱ") == 0, "ば -> ぱ");
	found = kwl_flick_voice("ぱ", next, sizeof(next));
	check(found && strcmp(next, "は") == 0, "ぱ -> は");
	found = kwl_flick_voice("っ", next, sizeof(next));
	check(found && strcmp(next, "づ") == 0, "っ -> づ");
	found = kwl_flick_voice("な", next, sizeof(next));
	check(!found, "な has no other form");

	/* The case key. */
	found = kwl_flick_case("a", next, sizeof(next));
	check(found && strcmp(next, "A") == 0, "a -> A");
	found = kwl_flick_case("Z", next, sizeof(next));
	check(found && strcmp(next, "z") == 0, "Z -> z");
	found = kwl_flick_case("1", next, sizeof(next));
	check(!found, "1 has no case");

	/* The US layout's keys (ws102-p004): a, A, 1, !, space, newline, backslash, bar; none for あ. */
	found = kwl_flick_us_key("a", &code, &shift);
	check(found && code == 30U && !shift, "a: key 30");
	found = kwl_flick_us_key("A", &code, &shift);
	check(found && code == 30U && shift, "A: key 30 with Shift");
	found = kwl_flick_us_key("1", &code, &shift);
	check(found && code == 2U && !shift, "1: key 2");
	found = kwl_flick_us_key("!", &code, &shift);
	check(found && code == 2U && shift, "!: key 2 with Shift");
	found = kwl_flick_us_key(" ", &code, &shift);
	check(found && code == 57U && !shift, "space: key 57");
	found = kwl_flick_us_key("\n", &code, &shift);
	check(found && code == 28U && !shift, "newline: key 28");
	found = kwl_flick_us_key("\\", &code, &shift);
	check(found && code == 43U && !shift, "backslash: key 43");
	found = kwl_flick_us_key("|", &code, &shift);
	check(found && code == 43U && shift, "bar: key 43 with Shift");
	found = kwl_flick_us_key("あ", &code, &shift);
	check(!found, "あ: no key");

	/* Every character of the alpha and number faces but the non-ASCII ones has a key. */
	missing = 0;
	for (index = 0; index < 2U; index++) {
		missing += face_keys(KWL_FLICK_ALPHA + (unsigned)index);
	}
	check(missing == 0, "every ASCII character of the alpha and number faces has a key");

	/* The QWERTY panel (ws102-p006): every letter in both cases, digit, ASCII symbol and the space, each with a key of the US layout. */
	missing = 0;
	for (index = 0; index < 26U + 26U + 10U + 32U + 1U; index++) {
		if (index < 26U) {
			one[0] = (char)('a' + index);
		} else if (index < 52U) {
			one[0] = (char)('A' + index - 26U);
		} else if (index < 62U) {
			one[0] = (char)('0' + index - 52U);
		} else if (index < 94U) {
			one[0] = host_symbols[index - 62U];
		} else {
			one[0] = ' ';
		}
		one[1] = '\0';
		found = qwerty_has(one);
		if (!found) {
			printf("qwerty missing: %s\n", one);
			missing++;
		}
	}
	check(missing == 0, "QWERTY: 52 letters, 10 digits, 32 symbols and the space");
	missing = qwerty_keys();
	check(missing == 0, "QWERTY: every character has a key of the US layout");
	missing = qwerty_widths();
	check(missing == 0, "QWERTY: no row wider than 40 quarters, at most 12 keys");

	/* The handwriting's ink and the stub recognizer (ws102-p008). */
	check_hand();

	/* The emoji face's table (ws102-p022). */
	check_emoji();

	/* The outcome. */
	if (failures != 0) {
		printf("host-keyboard: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-keyboard: PASS\n");
	return 0;
}

/* Prints a check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *text)
{
	/* A failed check is counted. */
	if (!condition) {
		printf("FAIL: %s\n", text);
		failures++;
		return;
	}

	/* A passed check is printed. */
	printf("ok: %s\n", text);
}

/* Tells whether a face types a text in some direction of some key. */
static int
face_has(
	unsigned face,
	const char *text)
{
	const struct kwl_flick_key *key;
	const char *typed;
	unsigned row;
	unsigned column;
	unsigned direction;

	/* Each key and direction. */
	for (row = 0; row < KWL_FLICK_ROWS; row++) {
		for (column = 0; column < KWL_FLICK_COLUMNS; column++) {
			key = kwl_flick_key(face, row, column);
			for (direction = 0; direction < KWL_FLICK_DIRECTIONS; direction++) {
				typed = kwl_flick_text(key, direction);
				if (typed != NULL && strcmp(typed, text) == 0)
					return 1;
			}
		}
	}

	/* Not on the face. */
	return 0;
}

/* Counts the characters a face types more than once. */
static int
face_repeats(
	unsigned face)
{
	const char *texts[KWL_FLICK_ROWS * KWL_FLICK_COLUMNS * KWL_FLICK_DIRECTIONS];
	const struct kwl_flick_key *key;
	const char *typed;
	unsigned count;
	unsigned other;
	unsigned row;
	unsigned column;
	unsigned direction;
	int repeats;

	/* Every text of the face. */
	count = 0;
	for (row = 0; row < KWL_FLICK_ROWS; row++) {
		for (column = 0; column < KWL_FLICK_COLUMNS; column++) {
			key = kwl_flick_key(face, row, column);
			for (direction = 0; direction < KWL_FLICK_DIRECTIONS; direction++) {
				typed = kwl_flick_text(key, direction);
				if (typed != NULL)
					texts[count++] = typed;
			}
		}
	}

	/* Each pair that is the same. */
	repeats = 0;
	for (row = 0; row < count; row++) {
		for (other = row + 1U; other < count; other++) {
			if (strcmp(texts[row], texts[other]) == 0) {
				printf("repeat: %s on %s\n", texts[row], kwl_flick_face_name(face));
				repeats++;
			}
		}
	}

	/* The repeats. */
	return repeats;
}

/* Counts the ASCII characters of a face that no key of the US layout types. */
static int
face_keys(
	unsigned face)
{
	const struct kwl_flick_key *key;
	const char *typed;
	unsigned row;
	unsigned column;
	unsigned direction;
	unsigned code;
	int shift;
	int found;
	int missing;

	/* Each ASCII text of the face. */
	missing = 0;
	for (row = 0; row < KWL_FLICK_ROWS; row++) {
		for (column = 0; column < KWL_FLICK_COLUMNS; column++) {
			key = kwl_flick_key(face, row, column);
			for (direction = 0; direction < KWL_FLICK_DIRECTIONS; direction++) {
				typed = kwl_flick_text(key, direction);
				if (typed == NULL || (unsigned char)typed[0] >= 0x80U)
					continue;
				found = kwl_flick_us_key(typed, &code, &shift);
				if (!found) {
					printf("no key: %s\n", typed);
					missing++;
				}
			}
		}
	}

	/* The ones without a key. */
	return missing;
}

/* Tells whether a QWERTY face types a text (with or without Shift). */
static int
qwerty_has(
	const char *text)
{
	const struct kwl_qwerty_key *keys;
	unsigned face;
	unsigned row;
	unsigned count;
	unsigned index;

	/* Each key of each row of each face, without and with Shift. */
	for (face = 0; face < KWL_QWERTY_FACES; face++) {
		for (row = 0; row < KWL_QWERTY_ROWS; row++) {
			keys = kwl_qwerty_row(face, row, &count);
			for (index = 0; index < count; index++) {
				if (keys[index].text != NULL && strcmp(keys[index].text, text) == 0)
					return 1;
				if (keys[index].shifted != NULL && strcmp(keys[index].shifted, text) == 0)
					return 1;
			}
		}
	}

	/* No key types it. */
	return 0;
}

/* Counts the QWERTY characters (with and without Shift) that no key of the US layout types. */
static int
qwerty_keys(void)
{
	const struct kwl_qwerty_key *keys;
	unsigned face;
	unsigned row;
	unsigned count;
	unsigned index;
	unsigned code;
	int shift;
	int missing;

	/* Each character of each key. */
	missing = 0;
	for (face = 0; face < KWL_QWERTY_FACES; face++) {
		for (row = 0; row < KWL_QWERTY_ROWS; row++) {
			keys = kwl_qwerty_row(face, row, &count);
			for (index = 0; index < count; index++) {
				if (keys[index].text != NULL && !kwl_flick_us_key(keys[index].text, &code, &shift))
					missing++;
				if (keys[index].shifted != NULL && !kwl_flick_us_key(keys[index].shifted, &code, &shift))
					missing++;
			}
		}
	}

	/* The ones without a key. */
	return missing;
}

/* Counts the QWERTY rows wider than the panel's quarters or with more keys than a row has. */
static int
qwerty_widths(void)
{
	const struct kwl_qwerty_key *keys;
	unsigned face;
	unsigned row;
	unsigned count;
	unsigned index;
	unsigned units;
	int wrong;

	/* Each row of each face. */
	wrong = 0;
	for (face = 0; face < KWL_QWERTY_FACES; face++) {
		for (row = 0; row < KWL_QWERTY_ROWS; row++) {
			keys = kwl_qwerty_row(face, row, &count);
			units = 0;
			for (index = 0; index < count; index++)
				units += keys[index].width;
			if (units > KWL_QWERTY_ROW_UNITS || count > KWL_QWERTY_ROW_KEYS) {
				printf("row %u of %s: %u quarters, %u keys\n", row, kwl_qwerty_face_name(face), units, count);
				wrong++;
			}
		}
	}

	/* The wrong rows. */
	return wrong;
}

/* Checks the handwriting's ink (strokes, points, limits, bounds) and the stub recognizer (ws102-p008). */
static void
check_hand(void)
{
	static struct kwl_hand_ink ink;
	struct kwl_hand_result result;
	int32_t rect[4];
	unsigned index;
	int kept;

	/* No ink: no candidates, empty bounds. */
	kwl_hand_clear(&ink);
	kwl_hand_recognize(&ink, &result);
	kwl_hand_bounds(&ink, rect);
	check(result.count == 0U && rect[2] == 0 && rect[3] == 0, "no ink: no candidates, no bounds");
	kept = kwl_hand_add(&ink, 1, 1);
	check(kept == 0, "no point without a stroke");

	/* A stroke of three points, a point that does not move is not kept. */
	kept = kwl_hand_begin(&ink, 10, 20);
	kept &= kwl_hand_add(&ink, 30, 25);
	kept &= kwl_hand_add(&ink, 50, 60);
	check(kept == 1, "a stroke's points kept");
	kept = kwl_hand_add(&ink, 50, 60);
	check(kept == 0 && kwl_hand_points(&ink) == 3U, "a point that does not move is not kept");

	/* A second stroke; the bounds cover both. */
	(void)kwl_hand_begin(&ink, 5, 70);
	(void)kwl_hand_add(&ink, 8, 90);
	kwl_hand_bounds(&ink, rect);
	check(ink.count == 2U && rect[0] == 5 && rect[1] == 20 && rect[2] == 46 && rect[3] == 71, "bounds of two strokes 5,20 46x71");

	/* The stub: three candidates (あ first) and its note. */
	kwl_hand_recognize(&ink, &result);
	check(result.count == 3U && strcmp(result.candidates[0], "あ") == 0 && strcmp(result.note, "認識はまだ") == 0, "stub: あ い う and its note");

	/* The limits: a stroke keeps KWL_HAND_POINTS points, the ink KWL_HAND_STROKES strokes. */
	kwl_hand_clear(&ink);
	(void)kwl_hand_begin(&ink, 0, 0);
	for (index = 1; index < KWL_HAND_POINTS + 10U; index++)
		(void)kwl_hand_add(&ink, (int32_t)index, 0);
	check(ink.strokes[0].count == KWL_HAND_POINTS, "a stroke keeps 512 points");
	for (index = 1; index < KWL_HAND_STROKES + 5U; index++)
		(void)kwl_hand_begin(&ink, 0, (int32_t)index);
	check(ink.count == KWL_HAND_STROKES, "the ink keeps 64 strokes");
}

/*
 * Checks the emoji face's table: every category holds its emoji, each one
 * well-formed UTF-8 of a single character of four or fewer bytes, and no
 * emoji appears twice anywhere.
 */
static void
check_emoji(void)
{
	const unsigned char *byte;
	const char *text;
	const char *other;
	unsigned category;
	unsigned index;
	unsigned other_category;
	unsigned other_index;
	unsigned bad;
	unsigned count;
	unsigned repeats;
	unsigned length;
	int same;

	/* Each category's count and names, and each emoji's bytes. */
	bad = 0U;
	for (category = 0U; category < KWL_EMOJI_CATEGORIES; category++) {
		/* The category is full. */
		count = kwl_emoji_count(category);
		if (count != KWL_EMOJI_PER_CATEGORY)
			bad++;

		/* Its emoji. */
		for (index = 0U; index < KWL_EMOJI_PER_CATEGORY; index++) {
			/* An emoji of one character: a lead byte above ASCII and its continuation bytes. */
			text = kwl_emoji(category, index);
			if (text == NULL) {
				bad++;
				continue;
			}

			/* Two to four bytes, the first above ASCII. */
			byte = (const unsigned char *)text;
			length = (unsigned)strlen(text);
			if (length < 2U || length > 4U || byte[0] < 0xc2U) {
				printf("emoji bad: category %u index %u\n", category, index);
				bad++;
			}
		}
	}

	/* The table's shape, the edges, and a name. */
	check(bad == 0U, "emoji: 4 categories of 20, each a single UTF-8 character");
	check(kwl_emoji_count(KWL_EMOJI_CATEGORIES) == 0U && kwl_emoji(0U, KWL_EMOJI_PER_CATEGORY) == NULL, "emoji: out of range is none");
	check(strcmp(kwl_emoji_category_name(KWL_EMOJI_FACES), "顔") == 0, "emoji: the first category is 顔");

	/* No emoji twice. */
	repeats = 0U;
	for (category = 0U; category < KWL_EMOJI_CATEGORIES; category++) {
		for (index = 0U; index < KWL_EMOJI_PER_CATEGORY; index++) {
			text = kwl_emoji(category, index);
			for (other_category = 0U; other_category < KWL_EMOJI_CATEGORIES; other_category++) {
				for (other_index = 0U; other_index < KWL_EMOJI_PER_CATEGORY; other_index++) {
					/* The same place is not a repeat. */
					if (other_category == category && other_index == index)
						continue;
					other = kwl_emoji(other_category, other_index);
					same = strcmp(text, other);
					if (same == 0)
						repeats++;
				}
			}
		}
	}

	/* The outcome of the comparison. */
	check(repeats == 0U, "emoji: no emoji twice");
}
