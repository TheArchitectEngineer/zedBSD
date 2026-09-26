/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Sets the environment for a utility (POSIX XCU env).
 *
 *	env [-i] [name=value]... [utility [argument...]]
 *
 * -i (or - alone) starts from an empty environment.  Without a utility the
 * resulting environment is written, one name=value to a line.  It is
 * installed as /usr/bin/env, the path that #! lines name; the shell has an
 * env builtin of its own.
 *
 * The new environment is an array of env's own.  It becomes the process
 * environment just before the utility is run, so the utility is looked up
 * with the PATH of the new environment, and a file without #! is run by
 * the shell as execvp does.  env exits with 126 when the utility was found
 * but could not be run, 127 when it could not be found, and 125 when env
 * itself failed.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The status when env itself fails. */
#define ENV_STATUS_FAILED 125

/* The status when the utility was found but could not be run. */
#define ENV_STATUS_NOT_RUNNABLE 126

/* The status when the utility could not be found. */
#define ENV_STATUS_NOT_FOUND 127

/* The environment of the process. */
extern char **environ;

static int read_options(int argc, char **argv, int *empty);
static void set_entry(char **entries, size_t *count, char *assignment);
static int write_environment(char **entries);
static int run_utility(char **arguments, char **entries);

/*
 * Runs env.
 */
int
main(
	int argc,
	char **argv)
{
	char **entries;
	char **entry;
	char *equals;
	size_t count;
	size_t size;
	int index;
	int empty;
	int status;

	/* Reads -i and --; the assignments follow them. */
	index = read_options(argc, argv, &empty);

	/* Room for the inherited entries and every assignment. */
	size = (size_t)argc + 1U;
	if (!empty) {
		for (entry = environ; entry != NULL && *entry != NULL; entry++)
			size++;
	}

	/* Allocates the new environment. */
	entries = calloc(size, sizeof(*entries));
	if (entries == NULL) {
		fprintf(stderr, "env: out of memory\n");
		return ENV_STATUS_FAILED;
	}

	/* The inherited environment, unless -i. */
	count = 0;
	if (!empty) {
		for (entry = environ; entry != NULL && *entry != NULL; entry++) {
			entries[count] = *entry;
			count++;
		}
	}

	/* Each name=value before the utility, replacing an inherited one. */
	for (; index < argc; index++) {
		equals = strchr(argv[index], '=');
		if (equals == NULL)
			break;
		set_entry(entries, &count, argv[index]);
	}

	/* No utility: writes the environment. */
	if (index >= argc) {
		status = write_environment(entries);
		return status;
	}

	/* The utility, in env's place; this returns only on failure. */
	status = run_utility(argv + index, entries);
	return status;
}

/*
 * Reads the options: -i, or - alone, starts from nothing and -- ends them.
 * Returns the index of the first operand.
 */
static int
read_options(
	int argc,
	char **argv,
	int *empty)
{
	int index;
	int compare;

	/* Reads options until the first operand. */
	*empty = 0;
	for (index = 1; index < argc; index++) {
		/* - alone is the historical spelling of -i. */
		if (argv[index][0] == '-' && argv[index][1] == '\0') {
			*empty = 1;
			continue;
		}

		/* -i starts from an empty environment. */
		compare = strcmp(argv[index], "-i");
		if (compare == 0) {
			*empty = 1;
			continue;
		}

		/* -- ends the options; anything else is the first operand. */
		compare = strcmp(argv[index], "--");
		if (compare == 0)
			index++;
		break;
	}

	/* Reports where the operands start. */
	return index;
}

/* Sets name=value in the entries, replacing an entry of the same name. */
static void
set_entry(
	char **entries,
	size_t *count,
	char *assignment)
{
	size_t name_length;
	size_t index;
	int compare;

	/* The name is everything before the first =. */
	name_length = (size_t)(strchr(assignment, '=') - assignment);

	/* An entry of the same name is replaced. */
	for (index = 0; index < *count; index++) {
		compare = strncmp(entries[index], assignment, name_length + 1U);
		if (compare == 0) {
			entries[index] = assignment;
			return;
		}
	}

	/* A new entry goes at the end. */
	entries[*count] = assignment;
	(*count)++;
	entries[*count] = NULL;
}

/*
 * Writes the environment, one entry to a line.  Returns 0, or 1 when
 * standard output could not be written.
 */
static int
write_environment(
	char **entries)
{
	size_t index;
	int failed;

	/* Writes each entry. */
	for (index = 0; entries[index] != NULL; index++)
		printf("%s\n", entries[index]);

	/* A write that failed, now or when flushed, is an error. */
	failed = fflush(stdout);
	if (failed == 0)
		failed = ferror(stdout);
	if (failed != 0) {
		fprintf(stderr, "env: write error: %s\n", strerror(errno));
		return 1;
	}

	/* Succeeded: the environment was written. */
	return 0;
}

/*
 * Runs a utility with the entries as its environment.  Returns only on
 * failure: 127 when it cannot be found, 126 when it cannot be run.
 */
static int
run_utility(
	char **arguments,
	char **entries)
{
	int error;

	/* Installs the new environment and runs the utility through PATH. */
	environ = entries;
	execvp(arguments[0], arguments);

	/* It could not be run. */
	error = errno;
	fprintf(stderr, "env: %s: %s\n", arguments[0], strerror(error));
	if (error == ENOENT)
		return ENV_STATUS_NOT_FOUND;

	/* Found but not runnable. */
	return ENV_STATUS_NOT_RUNNABLE;
}
