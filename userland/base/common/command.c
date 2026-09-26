/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Implements shared userland command support.
 */

#include "userland/base/common/command.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static int options_letter(struct command_options *scan);
static int options_long(struct command_options *scan, const char *text);
static int options_find_long(const struct command_options *scan, const char *name, size_t length);
static void options_operand(struct command_options *scan, char *argument);
static void options_rest(struct command_options *scan);

/*
 * Starts a scan of the command line for command_options_next.
 *
 * The caller has filled in the arguments, the letters, the long options and
 * whether -NUM is an option.
 */
void
command_options_start(
	struct command_options *scan)
{
	const char *posix;

	/* Nothing read yet; the operands gather from argv[1] on. */
	scan->index = 1;
	scan->cursor = NULL;
	scan->value = NULL;
	scan->number[0] = '\0';
	scan->operands = scan->argv + 1;
	scan->operand_count = 0;

	/* POSIXLY_CORRECT keeps the POSIX order: options come first. */
	posix = getenv("POSIXLY_CORRECT");
	scan->permute = 1;
	if (posix != NULL)
		scan->permute = 0;
}

/*
 * Reports the next option of a scan.
 *
 * Returns the option's letter or code, with its value in scan->value;
 * COMMAND_OPTION_NUMBER for -NUM; COMMAND_OPTION_ERROR after a message for
 * an option that is unknown, ambiguous or missing its value; and
 * COMMAND_OPTION_END when the options are over, when scan->operands holds
 * the operands.
 */
int
command_options_next(
	struct command_options *scan)
{
	char *argument;
	int differs;
	int code;

	/* The rest of a run of letters, such as the b of -ab. */
	scan->value = NULL;
	if (scan->cursor != NULL && *scan->cursor != '\0') {
		code = options_letter(scan);
		return code;
	}

	/* The run is over. */
	scan->cursor = NULL;

	/* Each argument until one is an option. */
	for (;;) {
		/* The end of the arguments ends the options. */
		if (scan->index >= scan->argc)
			return COMMAND_OPTION_END;
		argument = scan->argv[scan->index];

		/* -- ends the options; everything after it is an operand. */
		differs = strcmp(argument, "--");
		if (differs == 0) {
			scan->index++;
			options_rest(scan);
			return COMMAND_OPTION_END;
		}

		/* An option: -letters or --name. */
		if (argument[0] == '-' && argument[1] != '\0')
			break;

		/* An operand ends the options in the POSIX order. */
		if (!scan->permute) {
			options_rest(scan);
			return COMMAND_OPTION_END;
		}

		/* In the GNU order it is set aside and the scan goes on. */
		options_operand(scan, argument);
		scan->index++;
	}

	/* A long option. */
	scan->index++;
	if (argument[1] == '-') {
		code = options_long(scan, argument + 2);
		return code;
	}

	/* Succeeded: the first of a run of letters. */
	scan->cursor = argument + 1;
	code = options_letter(scan);
	return code;
}

/*
 * Writes an entire buffer to a descriptor.
 */
int
command_write_all(
	int descriptor,
	const void *data,
	size_t length)
{
	const unsigned char *bytes;
	ssize_t count;

	bytes = data;

	/* Write every byte, retrying interrupted system calls. */
	while (length != 0) {
		count = write(descriptor, bytes, length);

		/* Checks the remaining item count. */
		if (count < 0) {
			/* Handles the reported system error. */
			if (errno == EINTR)
				continue;

			/* Reports operation failure. */
			return -1;
		}

		/* Checks the remaining item count. */
		if (count == 0) {
			errno = EIO;

			/* Reports operation failure. */
			return -1;
		}

		bytes += count;
		length -= (size_t)count;
	}

	/* Reports successful completion. */
	return 0;
}

/*
 * Copies all available bytes between two descriptors.
 */
int
command_copy_fd(
	int input,
	int output)
{
	unsigned char buffer[4096];
	ssize_t count;
	int result;

	/* Copy chunks until the input reaches end of file. */
	for (;;) {
		count = read(input, buffer, sizeof(buffer));

		/* Checks the remaining item count. */
		if (count == 0)
			return 0;

		/* Checks the remaining item count. */
		if (count < 0) {
			/* Handles the reported system error. */
			if (errno == EINTR)
				continue;

			/* Reports operation failure. */
			return -1;
		}

		result = command_write_all(output, buffer, (size_t)count);

		/* Checks the operation result. */
		if (result != 0)
			return -1;
	}
}

/*
 * Parses an unsigned decimal integer.
 */
int
command_parse_ull(
	const char *text,
	unsigned long long *result)
{
	char *end;
	unsigned long long value;

	/* Handles the text availability. */
	if (text == NULL || *text == '\0' || *text == '-') {
		errno = EINVAL;

		/* Reports operation failure. */
		return -1;
	}

	errno = 0;
	value = strtoull(text, &end, 10);

	/* Handles the reported system error. */
	if (errno != 0 || *end != '\0') {
		/* Handles the reported system error. */
		if (errno == 0)
			errno = EINVAL;

		/* Reports operation failure. */
		return -1;
	}

	*result = value;

	/* Reports successful completion. */
	return 0;
}

/*
 * Parses an unsigned octal file mode.
 */
int
command_parse_mode(
	const char *text,
	unsigned *result)
{
	char *end;
	unsigned long value;

	/* Handles the text availability. */
	if (text == NULL || *text == '\0' || *text == '-') {
		errno = EINVAL;

		/* Reports operation failure. */
		return -1;
	}

	errno = 0;
	value = strtoul(text, &end, 8);

	/* Handles the reported system error. */
	if (errno != 0 || *end != '\0' || value > 07777UL) {
		/* Handles the reported system error. */
		if (errno == 0)
			errno = EINVAL;

		/* Reports operation failure. */
		return -1;
	}

	*result = (unsigned)value;

	/* Reports successful completion. */
	return 0;
}

/*
 * Reports a command error using the saved errno value.
 */
void
command_error(
	const char *command,
	const char *operand)
{
	int saved;

	saved = errno;

	/* Handles the operand availability. */
	if (operand != NULL) {
		fprintf(
			stderr,
			"%s: %s: %s\n",
			command,
			operand,
			strerror(saved));
	} else {
		fprintf(stderr, "%s: %s\n", command, strerror(saved));
	}
}

/*
 * Reads one arbitrarily sized line from a stream.
 */
long
command_read_line(
	FILE *stream,
	char **line,
	size_t *capacity)
{
	size_t next;
	char *grown;
	size_t length;
	int c;

	length = 0;

	/* Handles the line availability. */
	if (*line == NULL || *capacity < 2) {
		*capacity = 128;
		*line = malloc(*capacity);
		/* Handles the line availability. */
		if (*line == NULL)
			return -1;
	}

	/* Collect characters through a newline or end of file. */
	for (;;) {
		c = fgetc(stream);

		/* Handles the end-of-file condition. */
		if (c == EOF)
			break;

		/* Checks the current data length. */
		if (length + 1 >= *capacity) {
			/* Handles the capacity condition. */
			if (*capacity > SIZE_MAX / 2)
				next = SIZE_MAX;
			else
				next = *capacity * 2;

			/* Handles the next condition. */
			if (next <= *capacity) {
				errno = EOVERFLOW;

				/* Reports operation failure. */
				return -1;
			}

			grown = realloc(*line, next);

			/* Handles the grown availability. */
			if (grown == NULL)
				return -1;

			*line = grown;
			*capacity = next;
		}

		(*line)[length++] = (char)c;

		/* Classifies the current input character. */
		if (c == '\n')
			break;
	}

	/* Handles the end-of-file condition. */
	if (length == 0 && c == EOF) {
		/* Handles an operation failure. */
		if (ferror(stream))
			return -1;

		/* Reports successful completion. */
		return 0;
	}

	(*line)[length] = '\0';

	/* Returns the computed result. */
	return (long)length;
}

/*
 * Executes a command using the current search path.
 */
int
command_exec(
	const char *name,
	char *const argv[])
{
	int function_result;
	char path[512];
	const char *search;
	const char *position;
	const char *end;
	size_t name_length;
	size_t directory_length;

	/* Handles a failed strchr operation. */
	if (strchr(name, '/') != NULL) {
		/* Obtains the execve result. */
		function_result = execve(name, argv, environ);

		/* Returns the computed result. */
		return function_result;
	}

	search = getenv("PATH");

	/* Handles the search availability. */
	if (search == NULL || *search == '\0')
		search = "/bin:/usr/bin";

	name_length = strlen(name);
	position = search;

	/* Try every search-path component in order. */
	for (;;) {
		end = strchr(position, ':');

		/* Handles the end availability. */
		if (end == NULL)
			directory_length = strlen(position);
		else
			directory_length = (size_t)(end - position);

		/* Handles the directory length condition. */
		if (directory_length + name_length + 2 < sizeof(path)) {
			/* Handles the directory length condition. */
			if (directory_length != 0) {
				memcpy(path, position, directory_length);
			} else {
				path[0] = '.';
				directory_length = 1;
			}

			path[directory_length] = '/';
			strcpy(path + directory_length + 1, name);
			execve(path, argv, environ);

			/* Handles the reported system error. */
			if (errno != ENOENT && errno != ENOTDIR)
				return -1;
		}

		/* Handles the end availability. */
		if (end == NULL)
			break;

		position = end + 1;
	}

	errno = ENOENT;

	/* Reports operation failure. */
	return -1;
}

/* Reads one letter of a run, and its value when it takes one. */
static int
options_letter(
	struct command_options *scan)
{
	const char *found;
	size_t length;
	int letter;

	/* The letter, and the run after it. */
	letter = (unsigned char)*scan->cursor;
	scan->cursor++;

	/* -NUM: the digits that follow make up the number. */
	if (scan->numbers && letter >= '0' && letter <= '9') {
		scan->number[0] = (char)letter;
		length = 1;
		while (*scan->cursor >= '0' && *scan->cursor <= '9' &&
		       length + 1U < sizeof(scan->number)) {
			scan->number[length] = *scan->cursor;
			length++;
			scan->cursor++;
		}

		/* Succeeded: the number as the value. */
		scan->number[length] = '\0';
		scan->value = scan->number;
		return COMMAND_OPTION_NUMBER;
	}

	/* A letter the program does not take. */
	found = NULL;
	if (letter != ':')
		found = strchr(scan->letters, letter);
	if (found == NULL) {
		fprintf(stderr, "%s: invalid option -- '%c'\n", scan->program,
			letter);
		scan->cursor = NULL;
		return COMMAND_OPTION_ERROR;
	}

	/* A letter without a value. */
	if (found[1] != ':')
		return letter;

	/* A value attached to the letter, as in -escript or -i.bak. */
	if (*scan->cursor != '\0') {
		scan->value = scan->cursor;
		scan->cursor = NULL;
		return letter;
	}

	/* An optional value is only ever attached. */
	scan->cursor = NULL;
	if (found[2] == ':')
		return letter;

	/* A required value is the next argument, which must exist. */
	if (scan->index >= scan->argc) {
		fprintf(stderr, "%s: option requires an argument -- '%c'\n",
			scan->program, letter);
		return COMMAND_OPTION_ERROR;
	}

	/* Succeeded: the next argument is the value. */
	scan->value = scan->argv[scan->index];
	scan->index++;
	return letter;
}

/* Reads a long option (the text after --) and its value. */
static int
options_long(
	struct command_options *scan,
	const char *text)
{
	const struct command_long_option *option;
	const char *equals;
	size_t length;
	int found;

	/* The name, up to an = that starts the value. */
	equals = strchr(text, '=');
	length = strlen(text);
	if (equals != NULL)
		length = (size_t)(equals - text);

	/* The option the name, or a unique beginning of it, stands for. */
	found = options_find_long(scan, text, length);
	if (found < 0)
		return COMMAND_OPTION_ERROR;
	option = &scan->names[found];

	/* An option without a value refuses one. */
	if (option->value == COMMAND_VALUE_NONE) {
		if (equals != NULL) {
			fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
				scan->program, option->name);
			return COMMAND_OPTION_ERROR;
		}

		/* Succeeded: the option alone. */
		return option->code;
	}

	/* A value after the =. */
	if (equals != NULL) {
		scan->value = equals + 1;
		return option->code;
	}

	/* An optional value is only ever written after an =. */
	if (option->value == COMMAND_VALUE_OPTIONAL)
		return option->code;

	/* A required value is the next argument, which must exist. */
	if (scan->index >= scan->argc) {
		fprintf(stderr, "%s: option '--%s' requires an argument\n",
			scan->program, option->name);
		return COMMAND_OPTION_ERROR;
	}

	/* Succeeded: the next argument is the value. */
	scan->value = scan->argv[scan->index];
	scan->index++;
	return option->code;
}

/*
 * Finds the long option a name selects: the one of that name, or else the
 * only one it begins (options that differ only in name, such as --color
 * and --colour, count as one).  Returns -1 after a message.
 */
static int
options_find_long(
	const struct command_options *scan,
	const char *name,
	size_t length)
{
	const struct command_long_option *option;
	const struct command_long_option *first;
	int candidate;
	int ambiguous;
	int differs;
	int index;

	/* Each option the name begins. */
	candidate = -1;
	ambiguous = 0;
	for (index = 0; scan->names != NULL && scan->names[index].name != NULL;
	     index++) {
		/* An option the name does not begin. */
		option = &scan->names[index];
		differs = strncmp(option->name, name, length);
		if (differs != 0)
			continue;

		/* The name in full is the option whatever else it begins. */
		if (option->name[length] == '\0')
			return index;

		/* The first option it begins. */
		if (candidate < 0) {
			candidate = index;
			continue;
		}

		/* A second one that is really another option. */
		first = &scan->names[candidate];
		if (first->code != option->code || first->value != option->value)
			ambiguous = 1;
	}

	/* A beginning of two different options chooses neither. */
	if (ambiguous) {
		fprintf(stderr, "%s: option '--%.*s' is ambiguous\n",
			scan->program, (int)length, name);
		return -1;
	}

	/* A name that begins no option. */
	if (candidate < 0) {
		fprintf(stderr, "%s: unrecognized option '--%.*s'\n",
			scan->program, (int)length, name);
		return -1;
	}

	/* Succeeded: the only option the name begins. */
	return candidate;
}

/*
 * Sets an operand aside.  Every argument before scan->index has been read,
 * so the slot it goes into is free.
 */
static void
options_operand(
	struct command_options *scan,
	char *argument)
{
	/* The next operand slot. */
	scan->argv[1 + scan->operand_count] = argument;
	scan->operand_count++;
}

/* Sets every argument that is left aside as an operand. */
static void
options_rest(
	struct command_options *scan)
{
	/* Each argument in order. */
	while (scan->index < scan->argc) {
		options_operand(scan, scan->argv[scan->index]);
		scan->index++;
	}
}
