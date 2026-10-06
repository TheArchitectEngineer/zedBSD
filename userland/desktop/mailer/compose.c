/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Writing a message (WS169 p003, RFC 5322 and MIME; mail.h): the header
 * fields (From with the user's name, To, Cc, the subject as an encoded
 * word when it is not ASCII, the date in UTC, a new Message-ID, and
 * In-Reply-To and References for a reply) and the words as UTF-8 text
 * in quoted-printable, every line ended with CR LF.
 */

#include "mail.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The longest line of quoted-printable, before its soft line break. */
#define COMPOSE_LINE_MAX	75U

/* The days and months of a date (RFC 5322 section 3.3). */
static const char *const compose_days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const compose_months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* The hexadecimal digits of quoted-printable. */
static const char compose_hex[] = "0123456789ABCDEF";

/*
 * A message being written: its bytes (allocated), how many there are, the
 * room, and whether growing it failed (later appends do nothing then).
 */
struct compose_text {
	char *bytes;
	size_t length;
	size_t capacity;
	int failed;
};

/* How many Message-IDs this program made, so that two made in one second differ. */
static unsigned long compose_serial;

static void compose_append(struct compose_text *text, const char *bytes, size_t length);
static void compose_string(struct compose_text *text, const char *string);
static void compose_field(struct compose_text *text, const char *name, const char *value);
static void compose_encoded(struct compose_text *text, const char *value);
static void compose_body(struct compose_text *text, const char *body);
static int compose_is_ascii(const char *text);

/*
 * Writes a message from an account: to, cc (may be empty), its subject
 * and its words, a reply to a message ID (NULL or empty for none), dated
 * now.  Returns 0 with the bytes (the caller frees them), or ENOMEM.
 */
int
ml_compose(
	const struct ml_account_config *account,
	const char *to,
	const char *cc,
	const char *subject,
	const char *body,
	const char *reply_to_id,
	time_t now,
	char **raw,
	size_t *length)
{
	struct compose_text text;
	struct tm parts;
	const char *domain;
	char line[ML_TEXT_MAX * 2U];
	int ascii;

	/* Nothing yet. */
	memset(&text, 0, sizeof(text));
	*raw = NULL;
	*length = 0;

	/* From: the user's name (quoted, or an encoded word) and address. */
	compose_string(&text, "From: ");
	ascii = compose_is_ascii(account->name);
	if (account->name[0] != '\0' && ascii) {
		(void)snprintf(line, sizeof(line), "\"%s\" ", account->name);
		compose_string(&text, line);
	} else if (account->name[0] != '\0') {
		compose_encoded(&text, account->name);
		compose_string(&text, " ");
	}

	/* The address. */
	(void)snprintf(line, sizeof(line), "<%s>\r\n", account->address);
	compose_string(&text, line);

	/* The receivers. */
	compose_field(&text, "To", to);
	if (cc[0] != '\0')
		compose_field(&text, "Cc", cc);

	/* The subject, an encoded word when it is not ASCII. */
	compose_string(&text, "Subject: ");
	ascii = compose_is_ascii(subject);
	if (ascii)
		compose_string(&text, subject);
	else
		compose_encoded(&text, subject);
	compose_string(&text, "\r\n");

	/* The date, in UTC. */
	(void)gmtime_r(&now, &parts);
	(void)snprintf(line, sizeof(line), "Date: %s, %d %s %d %02d:%02d:%02d +0000\r\n",
	    compose_days[parts.tm_wday], parts.tm_mday, compose_months[parts.tm_mon], parts.tm_year + 1900,
	    parts.tm_hour, parts.tm_min, parts.tm_sec);
	compose_string(&text, line);

	/* A new ID at the address's domain. */
	domain = strchr(account->address, '@');
	if (domain == NULL)
		domain = "@localhost";
	compose_serial++;
	(void)snprintf(line, sizeof(line), "Message-ID: <%lld.%ld.%lu%s>\r\n", (long long)now, (long)getpid(), compose_serial, domain);
	compose_string(&text, line);

	/* The message replied to. */
	if (reply_to_id != NULL && reply_to_id[0] != '\0') {
		compose_field(&text, "In-Reply-To", reply_to_id);
		compose_field(&text, "References", reply_to_id);
	}

	/* The words: UTF-8 text in quoted-printable. */
	compose_string(&text, "MIME-Version: 1.0\r\n");
	compose_string(&text, "Content-Type: text/plain; charset=utf-8\r\n");
	compose_string(&text, "Content-Transfer-Encoding: quoted-printable\r\n");
	compose_string(&text, "\r\n");
	compose_body(&text, body);

	/* Growing failed somewhere. */
	if (text.failed) {
		free(text.bytes);
		return ENOMEM;
	}

	/* Succeeded: the message is written. */
	*raw = text.bytes;
	*length = text.length;
	return 0;
}

/* Appends bytes, growing the room; a failure is kept and later appends do nothing. */
static void
compose_append(
	struct compose_text *text,
	const char *bytes,
	size_t length)
{
	size_t capacity;
	char *grown;

	/* An earlier failure. */
	if (text->failed)
		return;

	/* Room for the bytes and a NUL. */
	if (text->length + length + 1U > text->capacity) {
		capacity = text->capacity * 2U;
		if (capacity < 1024U)
			capacity = 1024U;
		while (capacity < text->length + length + 1U)
			capacity *= 2U;
		grown = realloc(text->bytes, capacity);
		if (grown == NULL) {
			text->failed = 1;
			return;
		}

		/* The grown room. */
		text->bytes = grown;
		text->capacity = capacity;
	}

	/* The bytes and the NUL. */
	memcpy(text->bytes + text->length, bytes, length);
	text->length += length;
	text->bytes[text->length] = '\0';
}

/* Appends a string. */
static void
compose_string(
	struct compose_text *text,
	const char *string)
{
	/* Its bytes. */
	compose_append(text, string, strlen(string));
}

/* Appends a field "Name: value" and its line end (line ends in the value become spaces). */
static void
compose_field(
	struct compose_text *text,
	const char *name,
	const char *value)
{
	size_t index;

	/* The name. */
	compose_string(text, name);
	compose_string(text, ": ");

	/* The value, without line ends (they would start a new field). */
	for (index = 0; value[index] != '\0'; index++) {
		if (value[index] == '\r' || value[index] == '\n')
			compose_append(text, " ", 1U);
		else
			compose_append(text, value + index, 1U);
	}

	/* The line end. */
	compose_string(text, "\r\n");
}

/* Appends a value as one encoded word "=?UTF-8?B?...?=". */
static void
compose_encoded(
	struct compose_text *text,
	const char *value)
{
	char encoded[ML_TEXT_MAX * 2U];
	size_t length;

	/* The value in base64. */
	length = ml_base64_encode((const unsigned char *)value, strlen(value), encoded, sizeof(encoded));
	if (length == 0U) {
		compose_string(text, "=?UTF-8?B?" "?=");
		return;
	}

	/* Its word. */
	compose_string(text, "=?UTF-8?B?");
	compose_append(text, encoded, length);
	compose_string(text, "?=");
}

/* Appends the words in quoted-printable, each line ended with CR LF. */
static void
compose_body(
	struct compose_text *text,
	const char *body)
{
	unsigned char byte;
	char escape[3];
	size_t column;
	size_t index;
	int literal;

	/* Each byte. */
	column = 0;
	for (index = 0; body[index] != '\0'; index++) {
		byte = (unsigned char)body[index];

		/* A line end: CR LF (a CR of the text is dropped). */
		if (byte == '\r')
			continue;
		if (byte == '\n') {
			compose_string(text, "\r\n");
			column = 0;
			continue;
		}

		/* A byte that stands for itself: printable ASCII but '=', and a space or tab not at a line's end. */
		literal = 0;
		if (byte >= 33U && byte <= 126U && byte != '=')
			literal = 1;
		if ((byte == ' ' || byte == '\t') && body[index + 1U] != '\n' && body[index + 1U] != '\r' && body[index + 1U] != '\0')
			literal = 1;

		/* A soft line break before the line grows too long. */
		if (column + 3U > COMPOSE_LINE_MAX) {
			compose_string(text, "=\r\n");
			column = 0;
		}

		/* The byte, or its escape. */
		if (literal) {
			compose_append(text, body + index, 1U);
			column++;
		} else {
			escape[0] = '=';
			escape[1] = compose_hex[byte >> 4];
			escape[2] = compose_hex[byte & 0x0fU];
			compose_append(text, escape, 3U);
			column += 3U;
		}
	}

	/* The last line's end. */
	compose_string(text, "\r\n");
}

/* Tells whether a text is all ASCII. */
static int
compose_is_ascii(
	const char *text)
{
	size_t index;

	/* Each byte below 128. */
	for (index = 0; text[index] != '\0'; index++) {
		if ((unsigned char)text[index] >= 0x80U)
			return 0;
	}

	/* All ASCII. */
	return 1;
}
