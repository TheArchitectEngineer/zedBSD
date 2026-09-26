/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Moves names (the mv command), by renaming them.
 *
 *	mv [-fintuvT] [-t directory] source... destination
 *
 * With several sources, or a destination that is a directory, each source
 * goes into the directory under its own last name; -T takes the destination
 * as the name itself, and -t names the directory first.  -n never replaces
 * a name that exists (in one kernel operation, RENAME_NOREPLACE), -i asks
 * before replacing, -f replaces without asking; the last of them wins.  -u
 * replaces only an older file.  -v writes each move.  The long options are
 * GNU's (--no-clobber, --update[=WHEN], --target-directory ...), and options
 * may follow operands unless POSIXLY_CORRECT is set.
 */

#include "userland/base/common/command.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* What a move does when the new name exists. */
#define REPLACE_ALWAYS		0	/* -f: replace it */
#define REPLACE_NEVER		1	/* -n: keep it, quietly */
#define REPLACE_NEVER_FAIL	2	/* --update=none-fail: keep it, and fail */
#define REPLACE_ASK		3	/* -i: ask */
#define REPLACE_OLDER		4	/* -u: replace it when it is older */

/* The codes of the long options that have no letter. */
#define OPTION_UPDATE		256
#define OPTION_IGNORED		257
#define OPTION_HELP		258
#define OPTION_VERSION		259

/*
 * The options written in full.
 *
 * The table is read by the scan of the command line only; the letter a
 * long option shares its code with makes the two forms one case.
 */
static const struct command_long_option mv_long_options[] = {
	{"force", COMMAND_VALUE_NONE, 'f'},
	{"help", COMMAND_VALUE_NONE, OPTION_HELP},
	{"interactive", COMMAND_VALUE_NONE, 'i'},
	{"no-clobber", COMMAND_VALUE_NONE, 'n'},
	{"no-target-directory", COMMAND_VALUE_NONE, 'T'},
	{"strip-trailing-slashes", COMMAND_VALUE_NONE, OPTION_IGNORED},
	{"target-directory", COMMAND_VALUE_REQUIRED, 't'},
	{"update", COMMAND_VALUE_OPTIONAL, OPTION_UPDATE},
	{"verbose", COMMAND_VALUE_NONE, 'v'},
	{"version", COMMAND_VALUE_NONE, OPTION_VERSION},
	{NULL, 0, 0}
};

/* What the command line asks for. */
struct options {
	int replace;
	int literal;
	int verbose;
	const char *target_directory;
};

static int read_options(int argc, char **argv, struct options *options);
static void apply_update(struct options *options, const char *word);
static int move_one(const struct options *options, const char *source, const char *destination);
static int keep_newer(const char *source, const char *destination);
static int ask(const char *destination);
static const char *leaf(const char *path);
static void usage(void);

/*
 * Runs mv.
 */
int
main(
	int argc,
	char **argv)
{
	struct options options;
	struct stat status;
	char target[4096];
	const char *destination;
	const char *name;
	int count;
	int sources;
	int index;
	int directory;
	int result;
	int length;
	int failed;

	/* The options; the operands follow argv[0]. */
	memset(&options, 0, sizeof(options));
	count = read_options(argc, argv, &options);

	/* The destination: -t's directory, or the last operand. */
	destination = options.target_directory;
	sources = count;
	if (destination == NULL) {
		if (count < 2)
			usage();
		destination = argv[count];
		sources = count - 1;
	}

	/* At least one source. */
	if (sources < 1)
		usage();

	/* Whether the destination is a directory the sources go into. */
	directory = 0;
	if (!options.literal) {
		result = stat(destination, &status);
		if (result == 0 && (status.st_mode & S_IFMT) == S_IFDIR)
			directory = 1;
	}

	/* -t names a directory. */
	if (options.target_directory != NULL)
		directory = 1;

	/* Several sources need a directory. */
	if (sources > 1 && !directory) {
		fprintf(stderr, "mv: target '%s' is not a directory\n",
			destination);
		return 1;
	}

	/* Each source. */
	failed = 0;
	for (index = 1; index <= sources; index++) {
		/* Its new name, in the directory under its last name. */
		if (directory) {
			name = leaf(argv[index]);
			length = snprintf(target, sizeof(target), "%s/%s",
					  destination, name);
			if (length < 0 || (size_t)length >= sizeof(target)) {
				errno = ENAMETOOLONG;
				command_error("mv", argv[index]);
				failed = 1;
				continue;
			}

			/* The move into the directory. */
			result = move_one(&options, argv[index], target);
		} else {
			result = move_one(&options, argv[index], destination);
		}

		/* A source that did not move. */
		if (result != 0)
			failed = 1;
	}

	/* Some source did not move. */
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
	int code;

	/* The scan of the command line. */
	memset(&scan, 0, sizeof(scan));
	scan.argc = argc;
	scan.argv = argv;
	scan.program = "mv";
	scan.letters = "finuvTt:";
	scan.names = mv_long_options;
	command_options_start(&scan);

	/* Each option; -f, -i and -n override each other. */
	for (;;) {
		code = command_options_next(&scan);
		if (code == COMMAND_OPTION_END)
			break;
		switch (code) {
		case 'f':
			options->replace = REPLACE_ALWAYS;
			break;
		case 'i':
			options->replace = REPLACE_ASK;
			break;
		case 'n':
			options->replace = REPLACE_NEVER;
			break;
		case 'u':
			options->replace = REPLACE_OLDER;
			break;
		case OPTION_UPDATE:
			apply_update(options, scan.value);
			break;
		case 'v':
			options->verbose = 1;
			break;
		case 'T':
			options->literal = 1;
			break;
		case 't':
			options->target_directory = scan.value;
			break;
		case OPTION_IGNORED:
			/* Trailing slashes are the kernel's to take. */
			break;
		case OPTION_VERSION:
			printf("mv (zedBSD) 1.0\n");
			exit(0);
		default:
			usage();
		}
	}

	/* Succeeded: the operands follow argv[0]. */
	return scan.operand_count;
}

/* Applies --update[=WHEN]: all, none, none-fail or older (the default). */
static void
apply_update(
	struct options *options,
	const char *word)
{
	int differs;

	/* No word, or older: -u. */
	options->replace = REPLACE_OLDER;
	if (word == NULL)
		return;

	/* all: replace always. */
	differs = strcmp(word, "all");
	if (differs == 0) {
		options->replace = REPLACE_ALWAYS;
		return;
	}

	/* none: never, quietly. */
	differs = strcmp(word, "none");
	if (differs == 0) {
		options->replace = REPLACE_NEVER;
		return;
	}

	/* none-fail: never, and fail. */
	differs = strcmp(word, "none-fail");
	if (differs == 0)
		options->replace = REPLACE_NEVER_FAIL;
}

/* Moves one source to its new name.  Returns 0 when it moved or was meant to stay. */
static int
move_one(
	const struct options *options,
	const char *source,
	const char *destination)
{
	int result;
	int keep;
	int yes;

	/* -u: a newer file at the destination stays. */
	if (options->replace == REPLACE_OLDER) {
		keep = keep_newer(source, destination);
		if (keep)
			return 0;
	}

	/* -i: asked when the destination exists. */
	if (options->replace == REPLACE_ASK) {
		result = access(destination, F_OK);
		if (result == 0) {
			yes = ask(destination);
			if (!yes)
				return 0;
		}
	}

	/* The rename; -n never replaces, in one kernel operation. */
	if (options->replace == REPLACE_NEVER ||
	    options->replace == REPLACE_NEVER_FAIL)
		result = renameat2(AT_FDCWD, source, AT_FDCWD, destination, RENAME_NOREPLACE);
	else
		result = rename(source, destination);
	if (result != 0) {
		if (errno == EEXIST && options->replace == REPLACE_NEVER)
			return 0;
		command_error("mv", destination);
		return -1;
	}

	/* Succeeded: said so with -v. */
	if (options->verbose)
		printf("renamed '%s' -> '%s'\n", source, destination);
	return 0;
}

/* Reports whether the destination exists and is not older than the source (-u). */
static int
keep_newer(
	const char *source,
	const char *destination)
{
	struct stat source_status;
	struct stat destination_status;
	int result;

	/* No destination: the move goes ahead. */
	result = stat(destination, &destination_status);
	if (result != 0)
		return 0;

	/* A source that cannot be read goes ahead, for rename to report. */
	result = stat(source, &source_status);
	if (result != 0)
		return 0;

	/* An older destination is replaced. */
	if (destination_status.st_mtime < source_status.st_mtime)
		return 0;

	/* Succeeded: the destination is as new, and stays. */
	return 1;
}

/* Asks whether to replace a name, on standard error; returns whether yes. */
static int
ask(
	const char *destination)
{
	int first;
	int character;

	/* The question. */
	fprintf(stderr, "mv: overwrite '%s'? ", destination);
	fflush(stderr);

	/* The first character of the answer, and the rest of the line. */
	first = getchar();
	character = first;
	while (character != '\n' && character != EOF)
		character = getchar();

	/* Succeeded: y or Y is yes. */
	if (first == 'y' || first == 'Y')
		return 1;
	return 0;
}

/* Returns the last name of a path (after its last slash). */
static const char *
leaf(
	const char *path)
{
	const char *slash;

	/* The part after the last slash. */
	slash = strrchr(path, '/');
	if (slash == NULL)
		return path;

	/* Succeeded. */
	return slash + 1;
}

/* Reports the usage and ends mv. */
static void
usage(
	void)
{
	/* The forms. */
	fprintf(stderr, "usage: mv [-finuvT] source destination\n"
		"       mv [-finuv] source... directory\n"
		"       mv [-finuv] -t directory source...\n");
	exit(1);
}
