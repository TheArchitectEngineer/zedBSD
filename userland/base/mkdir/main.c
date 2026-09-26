/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Makes directories (POSIX XCU mkdir).
 *
 *	mkdir [-p] [-m mode] dir...
 *
 * Each directory is made with rwx for all, less the file mode creation
 * mask; -m gives it exactly the mode instead, a symbolic mode counting
 * from a=rwx.  -p also makes every missing parent, each with the mask's
 * bits and at least owner write and search so that the next one can be
 * made in it, and accepts a directory that already exists.
 */

#include "userland/base/chmod/mode.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * What one run of mkdir makes.
 *
 * One instance lives for the run.  mode is the mode of each operand's
 * directory and exact says whether it must be set despite the mask.
 */
struct mkdir_request {
	int parents;
	int exact;
	mode_t mode;
	mode_t mask;
};

static int read_options(int argc, char **argv, struct mkdir_request *request);
static int make_operand(const struct mkdir_request *request, const char *path);
static int make_parents(const struct mkdir_request *request, const char *path);
static int make_directory(const char *path, mode_t mode, int exact, int existing_ok);
static int is_directory(const char *path);
static void usage(void);

/*
 * Runs mkdir.
 */
int
main(
	int argc,
	char **argv)
{
	struct mkdir_request request;
	int first;
	int index;
	int failed;
	int status;

	/* Reads the options; the directories follow them. */
	first = read_options(argc, argv, &request);
	if (first >= argc)
		usage();

	/* Makes each directory; a failure is remembered and the rest go on. */
	failed = 0;
	for (index = first; index < argc; index++) {
		status = make_operand(&request, argv[index]);
		if (status != 0)
			failed = 1;
	}

	/* Reports whether any directory could not be made. */
	if (failed)
		return 1;

	/* Succeeded: every directory exists. */
	return 0;
}

/*
 * Reads the options and returns the index of the first operand.  The mode
 * of -m is computed here, from a=rwx.
 */
static int
read_options(
	int argc,
	char **argv,
	struct mkdir_request *request)
{
	const char *mode_text;
	int option;
	int status;

	/* Without -m the mode is rwx for all less the mask. */
	request->parents = 0;
	request->exact = 0;
	request->mask = umask(0);
	umask(request->mask);
	request->mode = 0777 & ~request->mask;
	mode_text = NULL;

	/* Reads -p and -m. */
	for (;;) {
		option = getopt(argc, argv, "pm:");
		if (option == -1)
			break;

		/* Records what the option asks for. */
		switch (option) {
		case 'p':
			request->parents = 1;
			break;
		case 'm':
			mode_text = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	/* Computes the -m mode from a=rwx; it is set exactly. */
	if (mode_text != NULL) {
		status = mode_apply(mode_text, 0777, request->mask, 1, &request->mode);
		if (status != 0) {
			fprintf(stderr, "mkdir: invalid mode: '%s'\n", mode_text);
			exit(1);
		}

		/* The mode is set whatever the mask. */
		request->exact = 1;
	}

	/* Reports where the operands start. */
	return optind;
}

/* Makes one directory operand, and with -p its missing parents. */
static int
make_operand(
	const struct mkdir_request *request,
	const char *path)
{
	int status;

	/* Makes the parents first with -p. */
	if (request->parents) {
		status = make_parents(request, path);
		if (status != 0)
			return -1;
	}

	/* Makes the directory; with -p an existing one is accepted. */
	status = make_directory(path, request->mode, request->exact, request->parents);
	if (status != 0)
		return -1;

	/* Succeeded: the directory exists. */
	return 0;
}

/*
 * Makes every missing parent of a pathname.  A parent gets the mask's
 * bits with owner write and search added.
 */
static int
make_parents(
	const struct mkdir_request *request,
	const char *path)
{
	char prefix[PATH_MAX + 1];
	mode_t mode;
	size_t length;
	size_t end;
	int status;
	int exact;

	/* Copies the pathname so that it can be cut at each slash. */
	length = strlen(path);
	if (length > PATH_MAX) {
		fprintf(stderr, "mkdir: %s: %s\n", path, strerror(ENAMETOOLONG));
		return -1;
	}

	/* Copies it with its terminator. */
	memcpy(prefix, path, length + 1);

	/* Drops trailing slashes; the last component is the operand's own. */
	while (length > 1 && prefix[length - 1] == '/')
		length--;
	prefix[length] = '\0';

	/*
	 * The parent mode: the mask's bits with owner write and search, which
	 * must be set explicitly when the mask removes them.
	 */
	mode = (0777 & ~request->mask) | S_IWUSR | S_IXUSR;
	exact = 0;
	if ((request->mask & (S_IWUSR | S_IXUSR)) != 0)
		exact = 1;

	/* Makes each prefix that ends before a slash. */
	end = 0;
	while (end < length) {
		/* Finds the end of the next component. */
		while (end < length && prefix[end] == '/')
			end++;
		while (end < length && prefix[end] != '/')
			end++;

		/* The last component is made by the caller. */
		if (end >= length)
			break;

		/* Makes the parent, accepting an existing directory. */
		prefix[end] = '\0';
		status = make_directory(prefix, mode, exact, 1);
		prefix[end] = '/';
		if (status != 0)
			return -1;
	}

	/* Succeeded: every parent exists. */
	return 0;
}

/*
 * Makes one directory with a mode, setting the mode exactly when asked.
 * An existing directory is accepted when existing_ok is set.
 */
static int
make_directory(
	const char *path,
	mode_t mode,
	int exact,
	int existing_ok)
{
	int status;
	int error;
	int directory;

	/* Makes the directory. */
	status = mkdir(path, mode);
	if (status != 0) {
		error = errno;

		/* With -p an existing directory is what was asked for. */
		if (error == EEXIST && existing_ok) {
			directory = is_directory(path);
			if (directory)
				return 0;
			error = ENOTDIR;
		}

		/* Anything else fails the operand. */
		fprintf(stderr, "mkdir: %s: %s\n", path, strerror(error));
		return -1;
	}

	/* Sets the mode the mask would have narrowed, and special bits. */
	if (exact) {
		status = chmod(path, mode);
		if (status != 0) {
			fprintf(stderr, "mkdir: %s: %s\n", path, strerror(errno));
			return -1;
		}
	}

	/* Succeeded: the directory was made. */
	return 0;
}

/* Tells whether a pathname names a directory, following links. */
static int
is_directory(
	const char *path)
{
	struct stat status_of_path;
	int status;
	int directory;

	/* Reads the file. */
	status = stat(path, &status_of_path);
	if (status != 0)
		return 0;

	/* Reports whether it is a directory. */
	directory = S_ISDIR(status_of_path.st_mode);
	return directory;
}

/* Writes the usage message and exits with an error status. */
static void
usage(void)
{
	/* Names the POSIX form. */
	fprintf(stderr, "usage: mkdir [-p] [-m mode] dir...\n");
	exit(1);
}
