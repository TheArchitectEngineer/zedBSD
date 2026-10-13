/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The lines of the file vi edits, and the few changes every edit is made of.
 */

#include "userland/base/vi/vi.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int line_reserve(struct vi_line *line, size_t length);
static int lines_reserve(struct vi_buffer *buffer, size_t count);
static int insert_text(struct vi_buffer *buffer, size_t index, size_t offset, const char *text, size_t length);
static int delete_text(struct vi_buffer *buffer, size_t index, size_t offset, size_t length);
static int insert_line(struct vi_buffer *buffer, size_t index, const char *text, size_t length);
static int delete_line(struct vi_buffer *buffer, size_t index);
static int split_line(struct vi_buffer *buffer, size_t index, size_t offset);
static int join_line(struct vi_buffer *buffer, size_t index, size_t offset);

/*
 * Prepares an empty buffer with no lines.
 *
 * The caller adds the first line; an editing session never shows a buffer
 * without one.
 */
int
vi_buffer_init(
	struct vi_buffer *buffer)
{
	/* Nothing is allocated until the first line arrives. */
	memset(buffer, 0, sizeof(*buffer));

	/* Succeeded. */
	return 0;
}

/*
 * Releases every line of a buffer and leaves it empty.
 */
void
vi_buffer_free(
	struct vi_buffer *buffer)
{
	size_t index;

	/* Releases the text of each line. */
	for (index = 0; index < buffer->count; index++)
		free(buffer->lines[index].text);

	/* Releases the line table itself. */
	free(buffer->lines);
	memset(buffer, 0, sizeof(*buffer));
}

/*
 * Adds a line after the last one, as the file is read.
 */
int
vi_buffer_append_line(
	struct vi_buffer *buffer,
	const char *text,
	size_t length)
{
	int error;

	/* The new line goes after every line already read. */
	error = insert_line(buffer, buffer->count, text, length);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Makes one change to the buffer, or takes it back.
 *
 * With reverse clear the change is made as it is described; with reverse
 * set its opposite is made, which restores the buffer to what it was
 * before the change.  Returns 0, or an errno value with the buffer
 * unchanged.
 */
int
vi_buffer_apply(
	struct vi_buffer *buffer,
	const struct vi_change *change,
	int reverse)
{
	enum vi_change_kind kind;
	int error;

	/* Turns the change into its opposite when it is being taken back. */
	kind = change->kind;
	if (reverse) {
		/* Each kind has exactly one opposite. */
		switch (kind) {
		case VI_CHANGE_INSERT_TEXT:
			kind = VI_CHANGE_DELETE_TEXT;
			break;
		case VI_CHANGE_DELETE_TEXT:
			kind = VI_CHANGE_INSERT_TEXT;
			break;
		case VI_CHANGE_SPLIT_LINE:
			kind = VI_CHANGE_JOIN_LINE;
			break;
		case VI_CHANGE_JOIN_LINE:
			kind = VI_CHANGE_SPLIT_LINE;
			break;
		case VI_CHANGE_INSERT_LINE:
			kind = VI_CHANGE_DELETE_LINE;
			break;
		case VI_CHANGE_DELETE_LINE:
			kind = VI_CHANGE_INSERT_LINE;
			break;
		}
	}

	/* Makes the change of the chosen kind. */
	error = EINVAL;
	switch (kind) {
	case VI_CHANGE_INSERT_TEXT:
		error = insert_text(buffer, change->line, change->offset, change->text, change->length);
		break;
	case VI_CHANGE_DELETE_TEXT:
		error = delete_text(buffer, change->line, change->offset, change->length);
		break;
	case VI_CHANGE_SPLIT_LINE:
		error = split_line(buffer, change->line, change->offset);
		break;
	case VI_CHANGE_JOIN_LINE:
		error = join_line(buffer, change->line, change->offset);
		break;
	case VI_CHANGE_INSERT_LINE:
		error = insert_line(buffer, change->line, change->text, change->length);
		break;
	case VI_CHANGE_DELETE_LINE:
		error = delete_line(buffer, change->line);
		break;
	}

	/* Reports a change the buffer could not take. */
	if (error != 0)
		return error;

	/* Succeeded: the buffer now shows the change. */
	return 0;
}

/* Makes room in one line for a text of the given length and its NUL. */
static int
line_reserve(
	struct vi_line *line,
	size_t length)
{
	size_t capacity;
	char *text;

	/* A line already large enough keeps its storage. */
	if (length < line->capacity)
		return 0;

	/* Grows by half again, so that typing a long line stays cheap. */
	capacity = line->capacity + line->capacity / 2 + 16;
	if (capacity <= length)
		capacity = length + 1;

	/* Moves the text into the larger storage. */
	text = realloc(line->text, capacity);
	if (text == NULL)
		return ENOMEM;

	line->text = text;
	line->capacity = capacity;

	/* Succeeded: the line can hold the length and its NUL. */
	return 0;
}

/* Makes room in the line table for the given number of lines. */
static int
lines_reserve(
	struct vi_buffer *buffer,
	size_t count)
{
	size_t capacity;
	struct vi_line *lines;

	/* A file larger than the limit is refused rather than grown. */
	if (count > VI_LINE_COUNT_LIMIT)
		return EFBIG;

	/* A table already large enough is kept. */
	if (count <= buffer->capacity)
		return 0;

	/* Grows by half again, so that reading a file stays linear. */
	capacity = buffer->capacity + buffer->capacity / 2 + 64;
	if (capacity < count)
		capacity = count;

	/* Moves the table into the larger storage. */
	lines = realloc(buffer->lines, capacity * sizeof(*lines));
	if (lines == NULL)
		return ENOMEM;

	buffer->lines = lines;
	buffer->capacity = capacity;

	/* Succeeded: the table can hold the count. */
	return 0;
}

/* Inserts bytes into a line at an offset. */
static int
insert_text(
	struct vi_buffer *buffer,
	size_t index,
	size_t offset,
	const char *text,
	size_t length)
{
	struct vi_line *line;
	int error;

	/* Refuses a place outside the buffer. */
	if (index >= buffer->count)
		return EINVAL;

	line = &buffer->lines[index];

	/* Refuses a place past the end of the line. */
	if (offset > line->length)
		return EINVAL;

	/* Makes room for the longer line. */
	error = line_reserve(line, line->length + length);
	if (error != 0)
		return error;

	/* Moves the rest of the line, with its NUL, and puts the bytes in the gap. */
	memmove(line->text + offset + length, line->text + offset, line->length - offset + 1);
	memcpy(line->text + offset, text, length);
	line->length += length;

	/* Succeeded. */
	return 0;
}

/* Removes bytes from a line at an offset. */
static int
delete_text(
	struct vi_buffer *buffer,
	size_t index,
	size_t offset,
	size_t length)
{
	struct vi_line *line;

	/* Refuses a place outside the buffer. */
	if (index >= buffer->count)
		return EINVAL;

	line = &buffer->lines[index];

	/* Refuses a range that runs past the end of the line. */
	if (offset > line->length)
		return EINVAL;
	if (length > line->length - offset)
		return EINVAL;

	/* Closes the gap, moving the NUL with the rest of the line. */
	memmove(line->text + offset, line->text + offset + length, line->length - offset - length + 1);
	line->length -= length;

	/* Succeeded. */
	return 0;
}

/* Inserts a new line holding the given text before the line at index. */
static int
insert_line(
	struct vi_buffer *buffer,
	size_t index,
	const char *text,
	size_t length)
{
	struct vi_line line;
	int error;

	/* Refuses a place outside the buffer; the end itself is allowed. */
	if (index > buffer->count)
		return EINVAL;

	/* Makes room for one more line in the table. */
	error = lines_reserve(buffer, buffer->count + 1);
	if (error != 0)
		return error;

	/* Makes the storage of the new line before the table changes. */
	memset(&line, 0, sizeof(line));
	error = line_reserve(&line, length);
	if (error != 0)
		return error;

	/* Fills the new line with its text and the NUL after it. */
	memcpy(line.text, text, length);
	line.text[length] = '\0';
	line.length = length;

	/* Opens a slot in the table and puts the line in it. */
	memmove(&buffer->lines[index + 1], &buffer->lines[index], (buffer->count - index) * sizeof(buffer->lines[0]));
	buffer->lines[index] = line;
	buffer->count++;

	/* Succeeded. */
	return 0;
}

/* Removes the line at index from the buffer. */
static int
delete_line(
	struct vi_buffer *buffer,
	size_t index)
{
	/* Refuses a place outside the buffer. */
	if (index >= buffer->count)
		return EINVAL;

	/* Releases the text and closes the slot in the table. */
	free(buffer->lines[index].text);
	memmove(&buffer->lines[index], &buffer->lines[index + 1], (buffer->count - index - 1) * sizeof(buffer->lines[0]));
	buffer->count--;

	/* Succeeded. */
	return 0;
}

/* Splits a line in two at an offset; the second part becomes the next line. */
static int
split_line(
	struct vi_buffer *buffer,
	size_t index,
	size_t offset)
{
	struct vi_line *line;
	int error;

	/* Refuses a place outside the buffer. */
	if (index >= buffer->count)
		return EINVAL;

	line = &buffer->lines[index];

	/* Refuses a place past the end of the line. */
	if (offset > line->length)
		return EINVAL;

	/* Puts the second part in a new line after this one. */
	error = insert_line(buffer, index + 1, line->text + offset, line->length - offset);
	if (error != 0)
		return error;

	/* Ends the first part at the split; the table may have moved. */
	line = &buffer->lines[index];
	line->length = offset;
	line->text[offset] = '\0';

	/* Succeeded. */
	return 0;
}

/* Joins a line with the next one; offset must be the first line's length. */
static int
join_line(
	struct vi_buffer *buffer,
	size_t index,
	size_t offset)
{
	struct vi_line *next;
	int error;

	/* Refuses a line that has no next line. */
	if (index + 1 >= buffer->count)
		return EINVAL;

	/* Refuses a join that would not undo the split it stands for. */
	if (offset != buffer->lines[index].length)
		return EINVAL;

	/* Copies the next line onto the end of this one. */
	next = &buffer->lines[index + 1];
	error = insert_text(buffer, index, offset, next->text, next->length);
	if (error != 0)
		return error;

	/* Removes the next line, now part of this one. */
	error = delete_line(buffer, index + 1);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}
