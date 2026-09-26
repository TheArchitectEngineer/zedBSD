/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Writes or sets the date and time (POSIX XCU date).
 *
 *	date [-u] [+format]
 *	date [-u] mmddhhmm[[cc]yy]
 *
 * The current time is written in the format given after +, whose
 * conversions are those of strftime(), or in the default format
 * "%a %b %e %H:%M:%S %Z %Y".  The E and O modifiers are accepted and, in
 * the POSIX locale, change nothing.  -u uses Coordinated Universal Time
 * instead of the TZ time zone.  A digits operand sets the system clock to
 * that local time (month, day, hour, minute, and optionally the century
 * and year; a two-digit year of 69 to 99 is in the 1900s), which only a
 * privileged user may do.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The format without a + operand. */
#define DATE_DEFAULT_FORMAT "%a %b %e %H:%M:%S %Z %Y"

/* The largest output of one format. */
#define DATE_OUTPUT_SIZE 8192

static int write_date(const char *format);
static int set_date(const char *operand);
static int read_digits(const char *text, int count);
static void strip_modifiers(const char *format, char *out, size_t size);
static void usage(void);

/*
 * Runs date.
 */
int
main(
	int argc,
	char **argv)
{
	const char *format;
	int option;
	int status;

	/* Reads -u, which puts the time zone at UTC. */
	for (;;) {
		option = getopt(argc, argv, "u");
		if (option == -1)
			break;

		/* -u is the only option. */
		if (option != 'u')
			usage();
		setenv("TZ", "UTC0", 1);
	}

	/* Reads the time zone, now that -u may have set it. */
	tzset();

	/* At most one operand. */
	if (argc - optind > 1)
		usage();

	/* No operand: the default format. */
	format = DATE_DEFAULT_FORMAT;
	if (optind == argc) {
		status = write_date(format);
		return status;
	}

	/* + and a format: that format. */
	if (argv[optind][0] == '+') {
		status = write_date(argv[optind] + 1);
		return status;
	}

	/* Anything else sets the clock. */
	status = set_date(argv[optind]);
	return status;
}

/* Writes the current time in a format. */
static int
write_date(
	const char *format)
{
	char stripped[DATE_OUTPUT_SIZE];
	char output[DATE_OUTPUT_SIZE];
	struct timespec now;
	struct tm *broken;
	size_t length;
	int status;

	/* Reads the clock. */
	status = clock_gettime(CLOCK_REALTIME, &now);
	if (status != 0) {
		fprintf(stderr, "date: cannot read the clock: %s\n", strerror(errno));
		return 1;
	}

	/* Breaks it down in the time zone. */
	broken = localtime(&now.tv_sec);
	if (broken == NULL) {
		fprintf(stderr, "date: cannot convert the time: %s\n", strerror(errno));
		return 1;
	}

	/*
	 * Formats it; the modifiers are dropped first.  strftime() gives 0
	 * both for an empty result and for one too long, so an empty format
	 * is told apart first.
	 */
	strip_modifiers(format, stripped, sizeof(stripped));
	length = 0;
	if (stripped[0] != '\0') {
		length = strftime(output, sizeof(output), stripped, broken);
		if (length == 0)
			output[0] = '\0';
	}

	/* Terminates the text. */
	output[length] = '\0';

	/* Writes it and its newline. */
	printf("%s\n", output);
	fflush(stdout);
	status = ferror(stdout);
	if (status != 0) {
		fprintf(stderr, "date: write error\n");
		return 1;
	}

	/* Succeeded: the date was written. */
	return 0;
}

/*
 * Copies a format without the E and O modifiers, which the POSIX locale
 * does not use: %Ey is %y and %Od is %d.
 */
static void
strip_modifiers(
	const char *format,
	char *out,
	size_t size)
{
	size_t used;
	const char *cursor;

	/* Copies byte by byte, dropping a modifier after a %. */
	used = 0;
	for (cursor = format; *cursor != '\0' && used + 2 < size; cursor++) {
		out[used] = *cursor;
		used++;
		if (*cursor != '%')
			continue;

		/* The byte after % is copied, after any modifier. */
		cursor++;
		if (*cursor == 'E' || *cursor == 'O')
			cursor++;
		if (*cursor == '\0')
			break;
		out[used] = *cursor;
		used++;
	}

	/* Terminates the copy. */
	out[used] = '\0';
}

/*
 * Sets the system clock from mmddhhmm[[cc]yy] in local time.  The current
 * year is kept when none is given.
 */
static int
set_date(
	const char *operand)
{
	struct timespec now;
	struct tm *current;
	struct tm wanted;
	time_t when;
	size_t length;
	size_t index;
	int year;
	int day;
	int valid;
	int status;

	/* The operand is 8, 10 or 12 digits. */
	length = strlen(operand);
	if (length != 8 && length != 10 && length != 12) {
		fprintf(stderr, "date: invalid date: '%s'\n", operand);
		return 1;
	}

	/* The operand must be all digits. */
	for (index = 0; index < length; index++) {
		if (operand[index] < '0' || operand[index] > '9') {
			fprintf(stderr, "date: invalid date: '%s'\n", operand);
			return 1;
		}
	}

	/* The current year, for an operand without one. */
	status = clock_gettime(CLOCK_REALTIME, &now);
	if (status != 0) {
		fprintf(stderr, "date: cannot read the clock: %s\n", strerror(errno));
		return 1;
	}

	/* Breaks the current time down for its year. */
	current = localtime(&now.tv_sec);
	if (current == NULL) {
		fprintf(stderr, "date: cannot convert the time: %s\n", strerror(errno));
		return 1;
	}

	/* The fields of the new time. */
	memset(&wanted, 0, sizeof(wanted));
	wanted.tm_mon = read_digits(operand, 2) - 1;
	wanted.tm_mday = read_digits(operand + 2, 2);
	wanted.tm_hour = read_digits(operand + 4, 2);
	wanted.tm_min = read_digits(operand + 6, 2);
	year = current->tm_year + 1900;
	if (length == 12) {
		year = read_digits(operand + 8, 4);
	} else if (length == 10) {
		year = read_digits(operand + 8, 2);
		if (year >= 69)
			year += 1900;
		else
			year += 2000;
	}

	/* Keeps the year and lets mktime() find out about daylight saving. */
	wanted.tm_year = year - 1900;
	wanted.tm_isdst = -1;

	/* The fields must be in range; mktime() would carry them over. */
	day = wanted.tm_mday;
	valid = 1;
	if (wanted.tm_mon < 0 || wanted.tm_mon > 11)
		valid = 0;
	else if (day < 1 || day > 31)
		valid = 0;
	else if (wanted.tm_hour > 23 || wanted.tm_min > 59)
		valid = 0;
	if (!valid) {
		fprintf(stderr, "date: invalid date: '%s'\n", operand);
		return 1;
	}

	/* Converts it; a day past the end of the month moves and is refused. */
	when = mktime(&wanted);
	if (when == (time_t)-1 || wanted.tm_mday != day) {
		fprintf(stderr, "date: invalid date: '%s'\n", operand);
		return 1;
	}

	/* Sets the clock. */
	now.tv_sec = when;
	now.tv_nsec = 0;
	status = clock_settime(CLOCK_REALTIME, &now);
	if (status != 0) {
		fprintf(stderr, "date: cannot set the date: %s\n", strerror(errno));
		return 1;
	}

	/* Writes the new date. */
	status = write_date(DATE_DEFAULT_FORMAT);
	if (status != 0)
		return 1;

	/* Succeeded: the clock is set. */
	return 0;
}

/* Reads a number of decimal digits. */
static int
read_digits(
	const char *text,
	int count)
{
	int value;
	int index;

	/* Accumulates the digits. */
	value = 0;
	for (index = 0; index < count; index++)
		value = value * 10 + (text[index] - '0');

	/* Reports the number. */
	return value;
}

/* Writes the usage message and exits with an error status. */
static void
usage(void)
{
	/* Names the POSIX forms. */
	fprintf(stderr,
		"usage: date [-u] [+format]\n"
		"       date [-u] mmddhhmm[[cc]yy]\n");
	exit(1);
}
