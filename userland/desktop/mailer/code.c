/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sign-in code of a message (WS169 p003, plan/ws169/phase001/phase.md
 * section 4): when the subject or the words speak of a code (code,
 * passcode, verification, one-time, OTP, PIN, コード, 認証, 確認), the
 * first run of 6 to 8 digits in the words (or else the subject) that no
 * other digit or letter touches, or else the first run of 4 or 5 that is
 * not a year (1900 to 2099).
 */

#include "mail.h"

#include <string.h>

/* The fewest and the most digits of a code, and the fewest of a long one (looked for first). */
#define CODE_DIGITS_MIN		4U
#define CODE_DIGITS_MAX		8U
#define CODE_DIGITS_LONG	6U

/* The words that tell a message carries a code, ASCII ones matched in any case. */
static const char *const code_words[] = {
	"code",
	"passcode",
	"verification",
	"verify",
	"one-time",
	"otp",
	"pin",
	"\xe3\x82\xb3\xe3\x83\xbc\xe3\x83\x89",	/* コード */
	"\xe8\xaa\x8d\xe8\xa8\xbc",		/* 認証 */
	"\xe7\xa2\xba\xe8\xaa\x8d"		/* 確認 */
};

static int code_speaks_of(const char *text);
static int code_contains(const char *text, const char *word);
static int code_take(const char *text, size_t fewest, size_t most, char *code, size_t size);
static int code_is_year(const char *digits, size_t length);
static int code_is_digit(int c);
static int code_is_letter(int c);

/*
 * Finds the sign-in code of a message.  Returns 1 with it in code, 0 when
 * the message has none (code is then empty).
 */
int
ml_code_find(
	const char *subject,
	const char *body,
	char *code,
	size_t size)
{
	int speaks;
	int taken;

	/* None yet. */
	code[0] = '\0';

	/* The subject or the words must speak of a code. */
	speaks = code_speaks_of(subject);
	if (!speaks)
		speaks = code_speaks_of(body);
	if (!speaks)
		return 0;

	/* The first long code of the words. */
	taken = code_take(body, CODE_DIGITS_LONG, CODE_DIGITS_MAX, code, size);
	if (taken)
		return 1;

	/* Else of the subject. */
	taken = code_take(subject, CODE_DIGITS_LONG, CODE_DIGITS_MAX, code, size);
	if (taken)
		return 1;

	/* Else a short one of the words. */
	taken = code_take(body, CODE_DIGITS_MIN, CODE_DIGITS_LONG - 1U, code, size);
	if (taken)
		return 1;

	/* Else of the subject. */
	taken = code_take(subject, CODE_DIGITS_MIN, CODE_DIGITS_LONG - 1U, code, size);
	if (taken)
		return 1;

	/* No code. */
	return 0;
}

/* Tells whether a text has one of the words of a code. */
static int
code_speaks_of(
	const char *text)
{
	size_t index;
	int found;

	/* Each word. */
	for (index = 0; index < sizeof(code_words) / sizeof(code_words[0]); index++) {
		found = code_contains(text, code_words[index]);
		if (found)
			return 1;
	}

	/* None. */
	return 0;
}

/* Tells whether a text holds a word, ASCII letters in any case. */
static int
code_contains(
	const char *text,
	const char *word)
{
	size_t length;
	size_t index;
	size_t at;
	int a;
	int b;

	/* Each place the word could start. */
	length = strlen(word);
	for (at = 0; text[at] != '\0'; at++) {
		/* The bytes from there, in lower case. */
		for (index = 0; index < length; index++) {
			a = (unsigned char)text[at + index];
			b = (unsigned char)word[index];
			if (a >= 'A' && a <= 'Z')
				a = a - 'A' + 'a';
			if (a != b)
				break;
		}

		/* The whole word. */
		if (index == length)
			return 1;
	}

	/* Not held. */
	return 0;
}

/* Takes the first run of fewest to most digits that no digit or letter touches and is not a year. */
static int
code_take(
	const char *text,
	size_t fewest,
	size_t most,
	char *code,
	size_t size)
{
	size_t start;
	size_t end;
	int before;
	int after;
	int digit;
	int year;

	/* Each run of digits. */
	start = 0;
	while (text[start] != '\0') {
		/* Not a digit: on. */
		digit = code_is_digit((unsigned char)text[start]);
		if (!digit) {
			start++;
			continue;
		}

		/* The run's end. */
		end = start;
		for (;;) {
			digit = code_is_digit((unsigned char)text[end]);
			if (!digit)
				break;
			end++;
		}

		/* A letter just before or after makes it a part of a word (an order number, a street). */
		before = 0;
		if (start > 0U)
			before = code_is_letter((unsigned char)text[start - 1U]);
		after = code_is_letter((unsigned char)text[end]);
		year = code_is_year(text + start, end - start);

		/* A code: of the right length, alone, not a year, and fitting the room. */
		if (end - start >= fewest &&
		    end - start <= most &&
		    !before &&
		    !after &&
		    !year &&
		    end - start < size) {
			memcpy(code, text + start, end - start);
			code[end - start] = '\0';
			return 1;
		}

		/* The next run. */
		start = end;
	}

	/* None. */
	return 0;
}

/* Tells whether a run of digits is a year of the dates mail carries (1900 to 2099). */
static int
code_is_year(
	const char *digits,
	size_t length)
{
	/* Four digits only. */
	if (length != 4U)
		return 0;

	/* 19xx. */
	if (digits[0] == '1' && digits[1] == '9')
		return 1;

	/* 20xx. */
	if (digits[0] == '2' && digits[1] == '0')
		return 1;

	/* Not a year. */
	return 0;
}

/* Tells whether a byte is an ASCII digit. */
static int
code_is_digit(
	int c)
{
	/* 0 to 9. */
	if (c >= '0' && c <= '9')
		return 1;

	/* Anything else. */
	return 0;
}

/* Tells whether a byte is an ASCII letter. */
static int
code_is_letter(
	int c)
{
	/* A small letter. */
	if (c >= 'a' && c <= 'z')
		return 1;

	/* A capital. */
	if (c >= 'A' && c <= 'Z')
		return 1;

	/* Anything else. */
	return 0;
}
