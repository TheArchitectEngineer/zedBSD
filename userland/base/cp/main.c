/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Copies files (POSIX XCU cp).
 *
 *	cp [-Pfip] source_file target_file
 *	cp [-Pfip] source_file... target
 *	cp -R [-H|-L|-P] [-fip] source_file... target
 *
 * With one source and a target that is not a directory, the source is
 * copied to the target pathname; otherwise each source is copied into the
 * target directory under its own last component.  -R copies directories
 * and everything below them; -H, -L and -P choose which symbolic links are
 * followed (the last one given wins; -R alone copies links as links, and
 * without -R links named as operands are followed).  -f replaces a
 * destination that cannot be opened, -i asks before overwriting, and -p
 * keeps the owner, the permission bits and the times.
 *
 * zedBSD also accepts -r (as -R), -a (-R -P -p and shared hard links), -n
 * and --no-clobber (keep existing files), --update=none-fail (refuse
 * them), -T and --no-target-directory (the target is always a pathname),
 * --attributes-only, --preserve=mode, and --report-file=PATH, which the
 * installer uses to account for a tree copy.
 */

#include "userland/base/cp/copy.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The follow policy is the default of the -R choice. */
#define CP_FOLLOW_DEFAULT (-1)

/*
 * What the command line asks for beyond the copy policy.
 *
 * One instance lives for the run.  follow stays CP_FOLLOW_DEFAULT until
 * -H, -L or -P is given.
 */
struct cp_request {
	int follow;
	int literal;
	const char *report_path;
};

static int read_options(int argc, char **argv, struct copy_options *options, struct cp_request *request);
static int read_long_option(const char *option, struct copy_options *options, struct cp_request *request);
static int read_short_options(const char *cluster, struct copy_options *options, struct cp_request *request);
static int copy_into(struct copy_options *options, const char *source, const char *directory);
static void usage(void);

/*
 * Runs cp.
 */
int
main(
	int argc,
	char **argv)
{
	struct copy_options options;
	struct cp_request request;
	struct stat status_of_target;
	const char *target;
	int first;
	int count;
	int index;
	int directory;
	int failed;
	int status;

	/* Reads the options; the operands follow them. */
	copy_options_init(&options, "cp");
	first = read_options(argc, argv, &options, &request);
	count = argc - first - 1;
	if (count < 1)
		usage();

	/*
	 * Settles which symbolic links are followed: the option given, or
	 * none for -R and the operands without it.
	 */
	if (request.follow != CP_FOLLOW_DEFAULT)
		options.follow = request.follow;
	else if (options.recursive)
		options.follow = COPY_FOLLOW_NONE;
	else
		options.follow = COPY_FOLLOW_OPERANDS;

	/* Learns whether the target is an existing directory. */
	target = argv[argc - 1];
	directory = 0;
	if (!request.literal) {
		status = stat(target, &status_of_target);
		if (status == 0)
			directory = S_ISDIR(status_of_target.st_mode);
	}

	/* Several sources need a directory to go into. */
	if (count > 1 && !directory) {
		fprintf(stderr, "cp: target '%s' is not a directory\n", target);
		return 1;
	}

	/* Opens the report before anything is copied. */
	if (request.report_path != NULL) {
		status = copy_report_open(&options, request.report_path, argc - first, argv + first);
		if (status != 0)
			return 1;
	}

	/* Copies each source; a failure is remembered and the rest go on. */
	failed = 0;
	for (index = first; index < argc - 1; index++) {
		if (directory)
			status = copy_into(&options, argv[index], target);
		else
			status = copy_operand(&options, argv[index], target);
		if (status != 0)
			failed = 1;

		/* A failed report stops the copies that could not be recorded. */
		if (options.report_failed) {
			failed = 1;
			break;
		}
	}

	/* Completes the report and releases the run. */
	status = copy_report_close(&options, request.report_path, failed);
	if (status != 0)
		failed = 1;
	copy_finish(&options);

	/* Reports whether any source could not be copied. */
	if (failed)
		return 1;

	/* Succeeded: every source was copied. */
	return 0;
}

/*
 * Reads the options and returns the index of the first operand.  An
 * invalid option ends cp with a usage message.
 */
static int
read_options(
	int argc,
	char **argv,
	struct copy_options *options,
	struct cp_request *request)
{
	int index;
	int status;
	int compare;

	/* Nothing asked for yet. */
	request->follow = CP_FOLLOW_DEFAULT;
	request->literal = 0;
	request->report_path = NULL;

	/* Reads options until the first operand; - alone is an operand. */
	for (index = 1; index < argc; index++) {
		if (argv[index][0] != '-' || argv[index][1] == '\0')
			break;

		/* -- ends the options. */
		compare = strcmp(argv[index], "--");
		if (compare == 0) {
			index++;
			break;
		}

		/* A long option, or a cluster of letters. */
		if (argv[index][1] == '-')
			status = read_long_option(argv[index], options, request);
		else
			status = read_short_options(argv[index] + 1, options, request);
		if (status != 0)
			usage();
	}

	/* Reports where the operands start. */
	return index;
}

/* Reads one long option; returns -1 for an unknown one. */
static int
read_long_option(
	const char *option,
	struct copy_options *options,
	struct cp_request *request)
{
	int compare;

	/* The target is a pathname, never a directory to copy into. */
	compare = strcmp(option, "--no-target-directory");
	if (compare == 0) {
		request->literal = 1;
		return 0;
	}

	/* Existing destinations are kept. */
	compare = strcmp(option, "--no-clobber");
	if (compare == 0) {
		options->exclusive = 1;
		options->conflict_fails = 0;
		return 0;
	}

	/* Existing destinations are refused. */
	compare = strcmp(option, "--update=none-fail");
	if (compare == 0) {
		options->exclusive = 1;
		options->conflict_fails = 1;
		return 0;
	}

	/* Only the attributes are copied, not the contents. */
	compare = strcmp(option, "--attributes-only");
	if (compare == 0) {
		options->attributes_only = 1;
		return 0;
	}

	/* The permission bits are kept. */
	compare = strcmp(option, "--preserve=mode");
	if (compare == 0) {
		options->preserve_mode = 1;
		return 0;
	}

	/* Everything is kept, as -a. */
	compare = strcmp(option, "--archive");
	if (compare == 0) {
		read_short_options("a", options, request);
		return 0;
	}

	/* A destination that cannot be opened is replaced, as -f. */
	compare = strcmp(option, "--force");
	if (compare == 0) {
		options->force = 1;
		return 0;
	}

	/* Completed entries are recorded in a report file. */
	compare = strncmp(option, "--report-file=", 14);
	if (compare == 0) {
		request->report_path = option + 14;
		return 0;
	}

	/* Anything else is unknown. */
	fprintf(stderr, "cp: unknown option '%s'\n", option);
	return -1;
}

/* Reads a cluster of one-letter options; returns -1 for an unknown one. */
static int
read_short_options(
	const char *cluster,
	struct copy_options *options,
	struct cp_request *request)
{
	const char *letter;

	/* Takes each letter in turn. */
	for (letter = cluster; *letter != '\0'; letter++) {
		/* Records what the letter asks for. */
		switch (*letter) {
		case 'H':
			request->follow = COPY_FOLLOW_OPERANDS;
			break;
		case 'L':
			request->follow = COPY_FOLLOW_ALL;
			break;
		case 'P':
			request->follow = COPY_FOLLOW_NONE;
			break;
		case 'R':
		case 'r':
			options->recursive = 1;
			break;
		case 'f':
			options->force = 1;
			break;
		case 'i':
			options->interactive = 1;
			break;
		case 'p':
			options->preserve_mode = 1;
			options->preserve_owner = 1;
			options->preserve_times = 1;
			break;
		case 'a':
			/* -a is -R -P -p, with names of one file sharing one copy. */
			options->recursive = 1;
			request->follow = COPY_FOLLOW_NONE;
			options->preserve_mode = 1;
			options->preserve_owner = 1;
			options->preserve_times = 1;
			options->preserve_links = 1;
			break;
		case 'n':
			options->exclusive = 1;
			options->conflict_fails = 0;
			break;
		case 'T':
			request->literal = 1;
			break;
		default:
			fprintf(stderr, "cp: unknown option -%c\n", *letter);
			return -1;
		}
	}

	/* Succeeded: every letter was known. */
	return 0;
}

/* Copies one source into a target directory under its last component. */
static int
copy_into(
	struct copy_options *options,
	const char *source,
	const char *directory)
{
	char name[PATH_MAX + 1];
	char destination[PATH_MAX + 1];
	const char *leaf;
	int count;
	int status;

	/* The last component of the source, trailing slashes ignored. */
	leaf = copy_leaf(source, name, sizeof(name));
	if (leaf == NULL) {
		fprintf(stderr, "cp: %s: %s\n", source, strerror(ENAMETOOLONG));
		return -1;
	}

	/* The destination inside the directory. */
	count = snprintf(destination, sizeof(destination), "%s/%s", directory, leaf);
	if (count < 0 || (size_t)count >= sizeof(destination)) {
		fprintf(stderr, "cp: %s: %s\n", source, strerror(ENAMETOOLONG));
		return -1;
	}

	/* Copies the source there. */
	status = copy_operand(options, source, destination);
	if (status != 0)
		return -1;

	/* Succeeded: the source was copied. */
	return 0;
}

/* Writes the usage message and exits with an error status. */
static void
usage(void)
{
	/* Names the POSIX forms. */
	fprintf(stderr,
		"usage: cp [-Pfip] source_file target_file\n"
		"       cp [-Pfip] source_file... target\n"
		"       cp -R [-H|-L|-P] [-fip] source_file... target\n");
	exit(1);
}
