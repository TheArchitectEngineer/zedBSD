/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Makes directories (the mkdir command).
 *
 *	mkdir [-pv] [-m mode] directory...
 *
 * -p makes the missing directories above each one too, and is content when
 * the directory is already there.  -m gives the last directory the mode (in
 * octal) whatever the umask; the ones -p makes above it have the usual
 * mode.  -v (GNU) writes each directory made.  The long options are GNU's
 * (--parents, --mode, --verbose), and options may follow operands unless
 * POSIXLY_CORRECT is set.
 */

#include "userland/base/common/command.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The codes of the long options that have no letter. */
#define OPTION_HELP	256
#define OPTION_VERSION	257

/*
 * The options written in full.
 *
 * The table is read by the scan of the command line only; the letter a
 * long option shares its code with makes the two forms one case.
 */
static const struct command_long_option mkdir_long_options[] = {
	{"help", COMMAND_VALUE_NONE, OPTION_HELP},
	{"mode", COMMAND_VALUE_REQUIRED, 'm'},
	{"parents", COMMAND_VALUE_NONE, 'p'},
	{"verbose", COMMAND_VALUE_NONE, 'v'},
	{"version", COMMAND_VALUE_NONE, OPTION_VERSION},
	{NULL, 0, 0}
};

/* What the command line asks for. */
struct options {
	int parents;
	int verbose;
	int have_mode;
	mode_t mode;
};

static int read_options(int argc, char **argv, struct options *options);
static int make_directory(const struct options *options, const char *path, int last);
static int make_parents(const struct options *options, const char *name);
static int make_existing(const struct options *options, const char *path, int last);
static void usage(void);

/*
 * Runs the mkdir command.
 */
int
main(
	int argc,
	char **argv)
{
	struct options options;
	int count;
	int index;
	int result;
	int failed;

	/* The options; the directories follow argv[0]. */
	memset(&options, 0, sizeof(options));
	count = read_options(argc, argv, &options);
	if (count == 0)
		usage();

	/* Each directory. */
	failed = 0;
	for (index = 1; index <= count; index++) {
		if (options.parents)
			result = make_parents(&options, argv[index]);
		else
			result = make_directory(&options, argv[index], 1);
		if (result != 0) {
			command_error("mkdir", argv[index]);
			failed = 1;
		}
	}

	/* Some directory could not be made. */
	if (failed)
		return 1;

	/* Succeeded. */
	return 0;
}

/*
 * Reads the options; returns the number of operands, which are left in
 * argv from argv[1] on.
 */
static int
read_options(
	int argc,
	char **argv,
	struct options *options)
{
	struct command_options scan;
	unsigned mode;
	int code;
	int result;

	/* The scan of the command line. */
	memset(&scan, 0, sizeof(scan));
	scan.argc = argc;
	scan.argv = argv;
	scan.program = "mkdir";
	scan.letters = "pvm:";
	scan.names = mkdir_long_options;
	command_options_start(&scan);

	/* Each option in turn. */
	for (;;) {
		code = command_options_next(&scan);
		if (code == COMMAND_OPTION_END)
			break;
		switch (code) {
		case 'p':
			options->parents = 1;
			break;
		case 'v':
			options->verbose = 1;
			break;
		case 'm':
			/* The mode, in octal. */
			result = command_parse_mode(scan.value, &mode);
			if (result != 0) {
				fprintf(stderr, "mkdir: invalid mode '%s'\n",
					scan.value);
				exit(1);
			}

			/* The mode for the last directory. */
			options->mode = (mode_t)mode;
			options->have_mode = 1;
			break;
		case OPTION_VERSION:
			printf("mkdir (zedBSD) 1.0\n");
			exit(0);
		default:
			usage();
		}
	}

	/* Succeeded: the operands follow argv[0]. */
	return scan.operand_count;
}

/*
 * Makes one directory: the last one gets -m's mode, exactly.  Returns 0,
 * or -1 with errno set.
 */
static int
make_directory(
	const struct options *options,
	const char *path,
	int last)
{
	int result;

	/* The directory, with the usual mode that the umask narrows. */
	result = mkdir(path, 0777);
	if (result != 0)
		return -1;

	/* -m sets the last one's mode whatever the umask. */
	if (last && options->have_mode) {
		result = chmod(path, options->mode);
		if (result != 0)
			return -1;
	}

	/* Succeeded: said so with -v. */
	if (options->verbose)
		printf("mkdir: created directory '%s'\n", path);
	return 0;
}

/*
 * -p: makes each missing directory of a path, from the top down; one that
 * is there already is passed.  Returns 0, or -1 with errno set.
 */
static int
make_parents(
	const struct options *options,
	const char *name)
{
	char *path;
	size_t length;
	size_t index;
	int last;
	int result;

	/* A name to make. */
	length = strlen(name);
	if (length == 0) {
		errno = ENOENT;
		return -1;
	}

	/* A copy of the name, cut at each slash in turn. */
	path = malloc(length + 1U);
	if (path == NULL)
		return -1;
	memcpy(path, name, length + 1U);

	/* Each part's end: before a slash (not in a run of them), and the end. */
	for (index = 1; index <= length; index++) {
		if (index < length) {
			if (path[index] != '/')
				continue;
			if (path[index - 1U] == '/')
				continue;
		}

		/* The directory up to here; the last is the whole name. */
		last = 0;
		if (index == length)
			last = 1;
		path[index] = '\0';
		result = make_existing(options, path, last);
		if (result != 0) {
			free(path);
			return -1;
		}

		/* The slash back, for the next part. */
		if (index < length)
			path[index] = '/';
	}

	/* Succeeded. */
	free(path);
	return 0;
}

/*
 * Makes a directory for -p, content when a directory is there already.
 * Returns 0, or -1 with errno set.
 */
static int
make_existing(
	const struct options *options,
	const char *path,
	int last)
{
	struct stat status;
	int result;

	/* The directory, made. */
	result = make_directory(options, path, last);
	if (result == 0)
		return 0;

	/* Anything but a name that is there is an error. */
	if (errno != EEXIST)
		return -1;

	/* What is there must be a directory. */
	result = stat(path, &status);
	if (result != 0)
		return -1;
	if ((status.st_mode & S_IFMT) != S_IFDIR) {
		errno = ENOTDIR;
		return -1;
	}

	/* Succeeded: the directory was there. */
	return 0;
}

/* Reports the usage and ends mkdir. */
static void
usage(
	void)
{
	/* The form. */
	fprintf(stderr, "usage: mkdir [-pv] [-m mode] directory...\n");
	exit(1);
}
