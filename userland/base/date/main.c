/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Writes the date and time (the zedBSD date command).
 *
 *	date [-u] [-d date | -r file] [-R | -I[spec]] [+format]
 *
 * The time is written in UTC, by the format (strftime's conversions, and
 * GNU's %N for the nanoseconds and %s for the seconds since the epoch), by
 * default "%Y-%m-%d %H:%M:%S UTC".  The time is now, or with GNU's -d the
 * date given (command_parse_date reads the forms scripts use: @SECONDS,
 * ISO dates and times, YYYYMMDD, relative items), or with -r the time a
 * file was last modified.  -R writes the date as in mail, and -I in ISO
 * 8601.
 */

#include "userland/base/common/command.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* The codes of the long options that have no letter. */
#define OPTION_HELP	256
#define OPTION_VERSION	257

/* The format when none is given. */
#define DEFAULT_FORMAT	"%Y-%m-%d %H:%M:%S UTC"

/* The format of -R. */
#define MAIL_FORMAT	"%a, %d %b %Y %H:%M:%S +0000"

/*
 * The options written in full.
 *
 * The table is read by the scan of the command line only; the letter a
 * long option shares its code with makes the two forms one case.
 */
static const struct command_long_option date_long_options[] = {
	{"date", COMMAND_VALUE_REQUIRED, 'd'},
	{"help", COMMAND_VALUE_NONE, OPTION_HELP},
	{"iso-8601", COMMAND_VALUE_OPTIONAL, 'I'},
	{"reference", COMMAND_VALUE_REQUIRED, 'r'},
	{"rfc-email", COMMAND_VALUE_NONE, 'R'},
	{"universal", COMMAND_VALUE_NONE, 'u'},
	{"utc", COMMAND_VALUE_NONE, 'u'},
	{"version", COMMAND_VALUE_NONE, OPTION_VERSION},
	{NULL, 0, 0}
};

/* What the command line asks for. */
struct options {
	const char *date;
	const char *reference;
	const char *format;
};

static int read_options(int argc, char **argv, struct options *options);
static const char *iso_format(const char *spec);
static int find_moment(const struct options *options, struct timespec *moment);
static int write_date(const char *format, const struct timespec *moment);
static int expand_format(const char *format, const struct timespec *moment, char *out, size_t size);
static void usage(void);

/*
 * Runs the date command.
 */
int
main(
	int argc,
	char **argv)
{
	struct options options;
	struct timespec moment;
	int operands;
	int found;
	int written;

	/* The options, and a +format operand. */
	memset(&options, 0, sizeof(options));
	options.format = DEFAULT_FORMAT;
	operands = read_options(argc, argv, &options);
	if (operands > 1)
		usage();
	if (operands == 1) {
		if (argv[1][0] != '+')
			usage();
		options.format = argv[1] + 1;
	}

	/* The moment: now, -d's date or -r's file. */
	found = find_moment(&options, &moment);
	if (!found)
		return 1;

	/* It, written. */
	written = write_date(options.format, &moment);
	if (!written)
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
	scan.program = "date";
	scan.letters = "ud:r:RI::";
	scan.names = date_long_options;
	command_options_start(&scan);

	/* Each option in turn. */
	for (;;) {
		code = command_options_next(&scan);
		if (code == COMMAND_OPTION_END)
			break;

		/* The option of its code. */
		switch (code) {
		case 'u':
			/* The time is written in UTC anyway. */
			break;
		case 'd':
			options->date = scan.value;
			break;
		case 'r':
			options->reference = scan.value;
			break;
		case 'R':
			options->format = MAIL_FORMAT;
			break;
		case 'I':
			options->format = iso_format(scan.value);
			break;
		case OPTION_VERSION:
			printf("date (zedBSD) 1.0\n");
			exit(0);
		default:
			usage();
		}
	}

	/* Succeeded: the operands follow argv[0]. */
	return scan.operand_count;
}

/* Returns the format of -I[spec] (date, hours, minutes, seconds or ns). */
static const char *
iso_format(
	const char *spec)
{
	static const char *const specs[][2] = {
		{"date", "%Y-%m-%d"},
		{"hours", "%Y-%m-%dT%H+00:00"},
		{"minutes", "%Y-%m-%dT%H:%M+00:00"},
		{"seconds", "%Y-%m-%dT%H:%M:%S+00:00"},
		{"ns", "%Y-%m-%dT%H:%M:%S,%N+00:00"}
	};
	size_t length;
	size_t index;
	int differs;

	/* No spec is the date. */
	if (spec == NULL)
		return specs[0][1];

	/* The spec, or the beginning of one. */
	length = strlen(spec);
	for (index = 0; index < sizeof(specs) / sizeof(specs[0]); index++) {
		differs = strncmp(spec, specs[index][0], length);
		if (differs == 0)
			return specs[index][1];
	}

	/* Any other spec. */
	fprintf(stderr, "date: invalid argument '%s' for '--iso-8601'\n", spec);
	exit(1);
}

/*
 * Finds the moment to write: now, the date of -d, or the modification
 * time of -r's file.  Returns 0 after a message when there is none.
 */
static int
find_moment(
	const struct options *options,
	struct timespec *moment)
{
	struct timespec now;
	struct stat status;
	int result;
	int valid;

	/* Now. */
	result = clock_gettime(CLOCK_REALTIME, &now);
	if (result != 0) {
		command_error("date", NULL);
		return 0;
	}

	/* -r: the time the file was last modified. */
	if (options->reference != NULL) {
		result = stat(options->reference, &status);
		if (result != 0) {
			command_error("date", options->reference);
			return 0;
		}

		/* Succeeded: the file's time. */
		*moment = status.st_mtim;
		return 1;
	}

	/* -d: the date given, read in UTC like the time written. */
	if (options->date != NULL) {
		valid = command_parse_date(options->date, &now, 1, moment);
		if (!valid) {
			fprintf(stderr, "date: invalid date '%s'\n",
				options->date);
			return 0;
		}

		/* Succeeded: the date given. */
		return 1;
	}

	/* Succeeded: now. */
	*moment = now;
	return 1;
}

/* Writes a moment in UTC by a format.  Returns 0 after a message. */
static int
write_date(
	const char *format,
	const struct timespec *moment)
{
	char expanded[1024];
	char text[4096];
	struct tm fields;
	time_t seconds;
	size_t length;
	int valid;
	int failed;

	/* GNU's %N and %s, which strftime does not know. */
	valid = expand_format(format, moment, expanded, sizeof(expanded));
	if (!valid) {
		fprintf(stderr, "date: format too long\n");
		return 0;
	}

	/* The fields of the moment in UTC, and the text of the format. */
	seconds = moment->tv_sec;
	gmtime_r(&seconds, &fields);
	length = strftime(text, sizeof(text), expanded, &fields);
	if (length == 0 && expanded[0] != '\0') {
		fprintf(stderr, "date: format too long\n");
		return 0;
	}

	/* The text and a newline. */
	fwrite(text, 1, length, stdout);
	putchar('\n');
	failed = ferror(stdout);
	if (failed)
		return 0;

	/* Succeeded. */
	return 1;
}

/*
 * Copies a format with GNU's %N (nanoseconds, nine digits) and %s
 * (seconds since the epoch) written out, for strftime to do the rest.
 * Returns 0 when it does not fit.
 */
static int
expand_format(
	const char *format,
	const struct timespec *moment,
	char *out,
	size_t size)
{
	char number[32];
	const char *cursor;
	size_t length;
	size_t used;

	/* Each character, and each conversion whole. */
	used = 0;
	for (cursor = format; *cursor != '\0'; cursor++) {
		/* %N and %s become their digits. */
		number[0] = '\0';
		if (cursor[0] == '%' && cursor[1] == 'N') {
			snprintf(number, sizeof(number), "%09ld",
				 (long)moment->tv_nsec);
		} else if (cursor[0] == '%' && cursor[1] == 's') {
			snprintf(number, sizeof(number), "%lld",
				 (long long)moment->tv_sec);
		}

		/* The digits in the conversion's place. */
		if (number[0] != '\0') {
			length = strlen(number);
			if (used + length + 1U > size)
				return 0;
			memcpy(out + used, number, length);
			used += length;
			cursor++;
			continue;
		}

		/* Any other character, and the character after a %. */
		if (used + 3U > size)
			return 0;
		out[used] = *cursor;
		used++;
		if (cursor[0] == '%' && cursor[1] != '\0') {
			cursor++;
			out[used] = *cursor;
			used++;
		}
	}

	/* Succeeded: the copy, ended. */
	out[used] = '\0';
	return 1;
}

/* Reports the usage and ends date. */
static void
usage(
	void)
{
	/* The form. */
	fprintf(stderr, "usage: date [-u] [-d date | -r file] [-R | -I[spec]] "
		"[+format]\n");
	exit(2);
}
