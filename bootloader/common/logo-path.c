/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The boot logo's tokens among the boot parameters, for the UEFI and the
 * BIOS loaders (ws035-p096): logo=PATH, a path relative to the boot volume
 * like kernel=, and whole tokens such as kmsg=quiet.
 */

#include "logo-path.h"

static size_t logo_token_end(const char *text, size_t length, size_t start);
static int logo_path_character(char character);

/*
 * Finds logo=PATH among the boot parameters and copies PATH.
 *
 * Returns 1 when a usable path was found, 0 when there is none, and -1 when
 * the token is malformed (an unsafe path, or one too long).
 */
int
zbl_logo_path(
	const char *text,
	size_t length,
	char *path,
	size_t capacity)
{
	size_t start;
	size_t end;
	size_t index;
	size_t used;
	int allowed;

	/* Each space-separated token. */
	start = 0;
	while (start < length) {
		end = logo_token_end(text, length, start);

		/* Anything that is not logo= is someone else's. */
		if (end - start < 6U ||
		    text[start] != 'l' || text[start + 1U] != 'o' || text[start + 2U] != 'g' ||
		    text[start + 3U] != 'o' || text[start + 4U] != '=') {
			start = end + 1U;
			continue;
		}

		/* The path: bounded, relative, of safe characters, with no parent step. */
		used = end - start - 5U;
		if (used == 0U || used > ZBL_LOGO_PATH_MAX || used + 1U > capacity)
			return -1;
		if (text[start + 5U] == '/')
			return -1;
		for (index = 0; index < used; index++) {
			allowed = logo_path_character(text[start + 5U + index]);
			if (!allowed)
				return -1;
			path[index] = text[start + 5U + index];
		}

		/* The path ends there. */
		path[used] = '\0';

		/* A path with ".." could leave the volume's root. */
		for (index = 0; index + 1U < used; index++) {
			if (path[index] == '.' && path[index + 1U] == '.')
				return -1;
		}

		/* Succeeded: the logo's path. */
		return 1;
	}

	/* No logo. */
	return 0;
}

/*
 * Reports whether a whole token (for example kmsg=quiet) is among the boot
 * parameters.
 */
int
zbl_parameter_present(
	const char *text,
	size_t length,
	const char *token)
{
	size_t start;
	size_t end;
	size_t index;
	int same;

	/* Each space-separated token, compared whole. */
	start = 0;
	while (start < length) {
		end = logo_token_end(text, length, start);
		same = 1;
		for (index = 0; start + index < end; index++) {
			if (token[index] != text[start + index]) {
				same = 0;
				break;
			}
		}

		/* The same token only when the given one ends there too. */
		if (same && token[end - start] == '\0')
			return 1;
		start = end + 1U;
	}

	/* Not there. */
	return 0;
}

/* Finds where the token that starts at start ends (at a space or the end). */
static size_t
logo_token_end(
	const char *text,
	size_t length,
	size_t start)
{
	size_t end;

	/* Up to the next space. */
	end = start;
	while (end < length && text[end] != ' ')
		end++;

	/* Reports the end. */
	return end;
}

/* Reports whether a character may be in a logo path. */
static int
logo_path_character(
	char character)
{
	/* Letters and digits. */
	if (character >= 'a' && character <= 'z')
		return 1;
	if (character >= 'A' && character <= 'Z')
		return 1;
	if (character >= '0' && character <= '9')
		return 1;

	/* The separators of names and directories. */
	if (character == '.' || character == '-' || character == '_' || character == '/')
		return 1;

	/* Anything else. */
	return 0;
}
