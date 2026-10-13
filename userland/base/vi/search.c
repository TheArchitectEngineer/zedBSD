/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The / and ? searches: a POSIX basic regular expression matched within
 * each line, wrapping around the ends of the buffer.
 */

#include "userland/base/vi/vi.h"

#include <stdio.h>
#include <string.h>

static int match_after(regex_t *pattern, const struct vi_line *line, size_t start, size_t *found);
static int match_before(regex_t *pattern, const struct vi_line *line, size_t limit, size_t *found);

/*
 * Makes a pattern the one / ? n and N search for.
 *
 * An empty pattern keeps the last one and only takes the new direction.
 * Returns 0, or -1 with a message when the pattern is not valid.
 */
int
vi_search_set(
	struct vi_editor *editor,
	const char *pattern,
	int backward)
{
	regex_t compiled;
	char reason[128];
	int status;

	/* An empty pattern searches again for the last one. */
	if (pattern[0] == '\0') {
		if (!editor->search_compiled) {
			vi_message(editor, "No previous pattern");
			return -1;
		}
		editor->search_backward = backward;
		return 0;
	}

	/* Compiles the pattern as a basic regular expression. */
	status = regcomp(&compiled, pattern, 0);
	if (status != 0) {
		(void)regerror(status, &compiled, reason, sizeof(reason));
		(void)snprintf(editor->message, sizeof(editor->message), "Bad pattern: %s", reason);
		return -1;
	}

	/* Replaces the last pattern. */
	if (editor->search_compiled)
		regfree(&editor->search_regex);
	editor->search_regex = compiled;
	editor->search_compiled = 1;
	editor->search_backward = backward;

	/* Succeeded. */
	return 0;
}

/*
 * Finds the next match of the pattern after a position, or the previous
 * one before it, going round the end of the buffer if need be.
 *
 * Returns 0 with the start of the match, or -1 with a message when the
 * pattern matches nowhere.
 */
int
vi_search_find(
	struct vi_editor *editor,
	struct vi_position from,
	int backward,
	struct vi_position *found)
{
	const struct vi_line *line;
	size_t index;
	size_t visited;
	size_t start;
	size_t offset;
	int status;

	/* Tries every line once, starting with the one the search starts on. */
	index = from.line;
	for (visited = 0; visited <= editor->buffer.count; visited++) {
		line = &editor->buffer.lines[index];

		/* The first line is searched only beyond the start; on coming back, only before it. */
		if (!backward) {
			start = 0;
			if (visited == 0) {
				start = vi_text_next(line->text, line->length, from.offset);
				if (line->length == 0)
					start = 1;
			}
			status = -1;
			if (start <= line->length)
				status = match_after(&editor->search_regex, line, start, &offset);
			if (status == 0 && visited == editor->buffer.count && offset > from.offset)
				status = -1;
		} else {
			start = line->length + 1;
			if (visited == 0)
				start = from.offset;
			status = match_before(&editor->search_regex, line, start, &offset);
			if (status == 0 && visited == editor->buffer.count && offset < from.offset)
				status = -1;
		}

		/* A match ends the search, with a note when it went round the end. */
		if (status == 0) {
			found->line = index;
			found->offset = offset;
			if ((!backward && index < from.line) || (backward && index > from.line))
				vi_message(editor, "Search wrapped");
			else if (visited == editor->buffer.count)
				vi_message(editor, "Search wrapped");
			return 0;
		}

		/* Goes to the next line in the direction of the search, round the ends. */
		if (!backward) {
			index++;
			if (index == editor->buffer.count)
				index = 0;
		} else {
			if (index == 0)
				index = editor->buffer.count;
			index--;
		}
	}

	/* Nothing matched anywhere. */
	vi_message(editor, "Pattern not found");
	return -1;
}

/* Finds the first match in a line that starts at or after an offset. */
static int
match_after(
	regex_t *pattern,
	const struct vi_line *line,
	size_t start,
	size_t *found)
{
	regmatch_t match[1];
	int flags;
	int status;

	/* A match that does not start the line cannot match ^. */
	flags = 0;
	if (start > 0)
		flags = REG_NOTBOL;

	/* Matches against the rest of the line. */
	status = regexec(pattern, line->text + start, 1, match, flags);
	if (status != 0)
		return -1;

	*found = start + (size_t)match[0].rm_so;

	/* Succeeded: the start of the match. */
	return 0;
}

/* Finds the last match in a line that starts before a limit. */
static int
match_before(
	regex_t *pattern,
	const struct vi_line *line,
	size_t limit,
	size_t *found)
{
	size_t position;
	size_t offset;
	int matched;
	int status;

	/* Walks the matches of the line from its start, keeping the last before the limit. */
	matched = 0;
	position = 0;
	while (position <= line->length) {
		status = match_after(pattern, line, position, &offset);
		if (status != 0)
			break;
		if (offset >= limit)
			break;

		*found = offset;
		matched = 1;

		/* The next match must start at least one character later. */
		position = vi_text_next(line->text, line->length, offset);
		if (position == offset)
			position = offset + 1;
	}

	/* No match before the limit. */
	if (!matched)
		return -1;

	/* Succeeded: the start of the last match. */
	return 0;
}
