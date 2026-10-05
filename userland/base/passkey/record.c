/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The records of /etc/passkey (passkey.h; ws172-p002):
 *
 *   # zedBSD passkey 1
 *   <name>:<uid>:pin:<SHA-512 crypt hash>
 *   <name>:<uid>:fido2:<id>:<COSE key>:<count>:<relying party>:<label>:<date>
 *
 * A line counts for an account only while both its name and its user ID
 * are the account's.  Lines of other kinds, comments and malformed lines
 * are kept as they are when the file is rewritten.
 */

#include "passkey.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int record_line_is(const char *line, size_t length, const char *name, const char *uid, const char *kind);
static int record_append(char *output, size_t capacity, size_t *used, const char *text, size_t length);

/* Gives the version the file's first line names (1 for a file without it, or an empty one). */
int
passkey_record_version(
	const char *text,
	size_t length)
{
	size_t header;

	/* The header and its number. */
	header = strlen(PASSKEY_HEADER);
	if (length < header || strncmp(text, PASSKEY_HEADER, header) != 0)
		return PASSKEY_VERSION;
	return atoi(text + header);
}

/*
 * Copies the index-th line (from 0) of an account's lines of a kind,
 * without its line end.  Returns 0, ENOENT, or ENAMETOOLONG.
 */
int
passkey_record_find(
	const char *text,
	size_t length,
	const char *name,
	uid_t uid,
	const char *kind,
	unsigned index,
	char *line,
	size_t size)
{
	char number[24];
	const char *start;
	const char *end;
	size_t line_length;
	unsigned found;

	/* Each line. */
	snprintf(number, sizeof(number), "%u", (unsigned)uid);
	found = 0U;
	start = text;
	while (start < text + length) {
		end = memchr(start, '\n', (size_t)(text + length - start));
		if (end == NULL)
			end = text + length;
		line_length = (size_t)(end - start);

		/* The account's line of the kind. */
		if (record_line_is(start, line_length, name, number, kind)) {
			if (found == index) {
				if (line_length >= size)
					return ENAMETOOLONG;
				memcpy(line, start, line_length);
				line[line_length] = '\0';
				return 0;
			}
			found++;
		}
		start = end + 1;
	}

	/* None. */
	return ENOENT;
}

/* Counts an account's lines of a kind. */
int
passkey_record_count(
	const char *text,
	size_t length,
	const char *name,
	uid_t uid,
	const char *kind)
{
	char line[PASSKEY_REQUEST_MAX];
	int count;

	/* Each one found. */
	for (count = 0; passkey_record_find(text, length, name, uid, kind, (unsigned)count, line, sizeof(line)) == 0; count++)
		continue;
	return count;
}

/*
 * Writes the file again: the header first, every line kept but the lines
 * of the name (whatever their user ID) of the kind (every kind when kind
 * is NULL), and added (a whole line without its end) at the end when it is
 * not NULL.  Returns 0 or ENOSPC.
 */
int
passkey_record_replace(
	const char *text,
	size_t length,
	const char *name,
	const char *kind,
	const char *added,
	char *output,
	size_t capacity,
	size_t *written)
{
	char header[64];
	const char *start;
	const char *end;
	size_t line_length;
	size_t used;
	size_t header_length;
	int error;

	/* The header. */
	used = 0U;
	snprintf(header, sizeof(header), "%s%d\n", PASSKEY_HEADER, PASSKEY_VERSION);
	error = record_append(output, capacity, &used, header, strlen(header));
	if (error != 0)
		return error;

	/* The lines kept (the old header is written again above). */
	header_length = strlen(PASSKEY_HEADER);
	start = text;
	while (start < text + length) {
		end = memchr(start, '\n', (size_t)(text + length - start));
		if (end == NULL)
			end = text + length;
		line_length = (size_t)(end - start);
		if (line_length != 0U &&
		    !(line_length >= header_length && strncmp(start, PASSKEY_HEADER, header_length) == 0) &&
		    !record_line_is(start, line_length, name, NULL, kind)) {
			error = record_append(output, capacity, &used, start, line_length);
			if (error == 0)
				error = record_append(output, capacity, &used, "\n", 1U);
			if (error != 0)
				return error;
		}
		start = end + 1;
	}

	/* The new line. */
	if (added != NULL) {
		error = record_append(output, capacity, &used, added, strlen(added));
		if (error == 0)
			error = record_append(output, capacity, &used, "\n", 1U);
		if (error != 0)
			return error;
	}

	/* Succeeded: the new text. */
	*written = used;
	return 0;
}

/*
 * Tells whether a line is name's (and uid's, unless uid is NULL) of a kind
 * (any kind when kind is NULL).
 */
static int
record_line_is(
	const char *line,
	size_t length,
	const char *name,
	const char *uid,
	const char *kind)
{
	const char *colon;
	const char *second;
	const char *third;
	size_t name_length;

	/* name: */
	name_length = strlen(name);
	if (length <= name_length || memcmp(line, name, name_length) != 0 || line[name_length] != ':')
		return 0;

	/* uid: */
	colon = line + name_length;
	second = memchr(colon + 1, ':', (size_t)(line + length - colon - 1));
	if (second == NULL)
		return 0;
	if (uid != NULL && ((size_t)(second - colon - 1) != strlen(uid) || memcmp(colon + 1, uid, strlen(uid)) != 0))
		return 0;

	/* kind: (or the end) */
	if (kind == NULL)
		return 1;
	third = memchr(second + 1, ':', (size_t)(line + length - second - 1));
	if (third == NULL)
		third = line + length;
	if ((size_t)(third - second - 1) != strlen(kind) || memcmp(second + 1, kind, strlen(kind)) != 0)
		return 0;

	/* The account's line of the kind. */
	return 1;
}

/* Appends bytes; returns 0 or ENOSPC. */
static int
record_append(
	char *output,
	size_t capacity,
	size_t *used,
	const char *text,
	size_t length)
{
	if (length > capacity - *used)
		return ENOSPC;
	memcpy(output + *used, text, length);
	*used += length;
	return 0;
}
