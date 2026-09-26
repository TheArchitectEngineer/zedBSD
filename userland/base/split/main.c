/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Splits a file into pieces (POSIX XCU split).
 *
 *	split [-l line_count] [-a suffix_length] [file [name]]
 *	split -b n[k|m] [-a suffix_length] [file [name]]
 *
 * Each piece holds line_count lines (1000 by default) or n bytes, and is
 * named by the name (x by default) and a suffix of suffix_length letters
 * (2 by default): aa, ab, ... zz.  GNU's -d (--numeric-suffixes[=FROM])
 * makes the suffixes digits, --additional-suffix puts a string after them,
 * --verbose writes each piece's name, a count may have GNU's units (K, M,
 * G, KB ...), the options have their long forms, and options may follow
 * operands unless POSIXLY_CORRECT is set.
 */

#include "userland/base/common/command.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The codes of the long options that have no letter. */
#define OPTION_ADDITIONAL	256
#define OPTION_VERBOSE		257
#define OPTION_NUMERIC		258
#define OPTION_HELP		259
#define OPTION_VERSION		260

/*
 * The options written in full.
 *
 * The table is read by the scan of the command line only; the letter a
 * long option shares its code with makes the two forms one case.
 */
static const struct command_long_option split_long_options[] = {
	{"additional-suffix", COMMAND_VALUE_REQUIRED, OPTION_ADDITIONAL},
	{"bytes", COMMAND_VALUE_REQUIRED, 'b'},
	{"help", COMMAND_VALUE_NONE, OPTION_HELP},
	{"lines", COMMAND_VALUE_REQUIRED, 'l'},
	{"numeric-suffixes", COMMAND_VALUE_OPTIONAL, OPTION_NUMERIC},
	{"suffix-length", COMMAND_VALUE_REQUIRED, 'a'},
	{"verbose", COMMAND_VALUE_NONE, OPTION_VERBOSE},
	{"version", COMMAND_VALUE_NONE, OPTION_VERSION},
	{NULL, 0, 0}
};

/* What the command line asks for, and the piece being written. */
struct split {
	unsigned long long limit;
	int bytes;
	size_t suffix_length;
	int numeric;
	unsigned long long first_number;
	const char *additional;
	int verbose;
	const char *prefix;

	/* The piece: its number, its name, its stream and what it holds so far. */
	unsigned long long piece;
	char *name;
	FILE *output;
	unsigned long long used;
};

static int read_options(int argc, char **argv, struct split *split);
static unsigned long long parse_count(const char *text);
static int parse_unit(const char *text, unsigned long long *multiplier);
static int open_piece(struct split *split);
static int close_piece(struct split *split);
static void usage(void);

/*
 * Runs the split command.
 */
int
main(
	int argc,
	char **argv)
{
	struct split split;
	FILE *input;
	int count;
	int value;
	int compare;
	int result;

	/* The options; the file and the name follow argv[0]. */
	memset(&split, 0, sizeof(split));
	split.limit = 1000;
	split.suffix_length = 2;
	split.prefix = "x";
	count = read_options(argc, argv, &split);
	if (count > 2)
		usage();

	/* The input: a file, - or standard input. */
	input = stdin;
	if (count >= 1) {
		compare = strcmp(argv[1], "-");
		if (compare != 0)
			input = fopen(argv[1], "r");
		if (input == NULL) {
			command_error("split", argv[1]);
			return 1;
		}
	}

	/* The name the pieces start with. */
	if (count == 2)
		split.prefix = argv[2];

	/* Each byte, into the piece that is open or a new one. */
	for (;;) {
		value = getc(input);
		if (value == EOF)
			break;

		/* A new piece when none is open. */
		if (split.output == NULL) {
			result = open_piece(&split);
			if (result != 0)
				return 1;
		}

		/* The byte; a full piece is closed. */
		putc(value, split.output);
		if (split.bytes || value == '\n')
			split.used++;
		if (split.used >= split.limit) {
			result = close_piece(&split);
			if (result != 0)
				return 1;
		}
	}

	/* The last piece. */
	result = close_piece(&split);
	if (result != 0)
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
	struct split *split)
{
	struct command_options scan;
	int code;

	/* The scan of the command line. */
	memset(&scan, 0, sizeof(scan));
	scan.argc = argc;
	scan.argv = argv;
	scan.program = "split";
	scan.letters = "dl:b:a:";
	scan.names = split_long_options;
	command_options_start(&scan);

	/* Each option in turn. */
	for (;;) {
		code = command_options_next(&scan);
		if (code == COMMAND_OPTION_END)
			break;
		switch (code) {
		case 'l':
			split->limit = parse_count(scan.value);
			split->bytes = 0;
			break;
		case 'b':
			split->limit = parse_count(scan.value);
			split->bytes = 1;
			break;
		case 'a':
			split->suffix_length = (size_t)parse_count(scan.value);
			break;
		case 'd':
			split->numeric = 1;
			break;
		case OPTION_NUMERIC:
			split->numeric = 1;
			if (scan.value != NULL)
				split->first_number = strtoull(scan.value, NULL, 10);
			break;
		case OPTION_ADDITIONAL:
			split->additional = scan.value;
			break;
		case OPTION_VERBOSE:
			split->verbose = 1;
			break;
		case OPTION_VERSION:
			printf("split (zedBSD) 1.0\n");
			exit(0);
		default:
			usage();
		}
	}

	/* Succeeded: the operands follow argv[0]. */
	return scan.operand_count;
}

/*
 * Reads a count that is not 0: digits, then POSIX's k and m or one of GNU's
 * units.  A count that cannot be read ends split.
 */
static unsigned long long
parse_count(
	const char *text)
{
	unsigned long long value;
	unsigned long long multiplier;
	const char *cursor;
	int valid;

	/* The digits. */
	if (*text < '0' || *text > '9')
		usage();
	value = 0;
	for (cursor = text; *cursor >= '0' && *cursor <= '9'; cursor++)
		value = value * 10U + (unsigned long long)(*cursor - '0');

	/* The unit after them. */
	valid = parse_unit(cursor, &multiplier);
	if (!valid)
		usage();

	/* The count, which is not 0. */
	value *= multiplier;
	if (value == 0)
		usage();

	/* Succeeded. */
	return value;
}

/*
 * Reads the unit of a count: none, b (512), POSIX's k and m (1024 and its
 * square), or GNU's K, M, G, T, P, E (powers of 1024, also written KiB)
 * and KB, MB ... (powers of 1000).  Returns 0 for anything else.
 */
static int
parse_unit(
	const char *text,
	unsigned long long *multiplier)
{
	static const char letters[] = "KMGTPE";
	unsigned long long base;
	const char *found;
	char letter;
	int power;

	/* No unit, and b. */
	*multiplier = 1;
	if (text[0] == '\0')
		return 1;
	if (text[0] == 'b' && text[1] == '\0') {
		*multiplier = 512;
		return 1;
	}

	/* The letter of the power; k and m are K and M. */
	letter = text[0];
	if (letter == 'k' || letter == 'm')
		letter = (char)(letter - 'a' + 'A');
	found = strchr(letters, letter);
	if (found == NULL || letter == '\0')
		return 0;
	power = (int)(found - letters) + 1;

	/* K alone or KiB is 1024; KB is 1000. */
	base = 1024;
	if (text[1] == 'B' && text[2] == '\0') {
		base = 1000;
	} else if (text[1] == 'i' && text[2] == 'B' && text[3] == '\0') {
		base = 1024;
	} else if (text[1] != '\0') {
		return 0;
	}

	/* Succeeded: the power of the base. */
	for (; power > 0; power--)
		*multiplier *= base;
	return 1;
}

/* Opens the next piece under its name.  Returns 0, or -1 after a message. */
static int
open_piece(
	struct split *split)
{
	unsigned long long number;
	unsigned long long radix;
	size_t prefix_length;
	size_t additional_length;
	size_t index;
	char digit;

	/* Room for the name, the suffix and the additional suffix. */
	prefix_length = strlen(split->prefix);
	additional_length = 0;
	if (split->additional != NULL)
		additional_length = strlen(split->additional);
	free(split->name);
	split->name = malloc(prefix_length + split->suffix_length + additional_length + 1U);
	if (split->name == NULL) {
		fprintf(stderr, "split: out of memory\n");
		return -1;
	}

	/* The suffix: the piece's number in letters (base 26) or digits. */
	radix = 26;
	if (split->numeric)
		radix = 10;
	number = split->piece + split->first_number;
	memcpy(split->name, split->prefix, prefix_length);
	for (index = split->suffix_length; index > 0; index--) {
		if (split->numeric)
			digit = (char)('0' + number % radix);
		else
			digit = (char)('a' + number % radix);
		split->name[prefix_length + index - 1U] = digit;
		number /= radix;
	}

	/* A number too big for the suffix is the end. */
	if (number != 0) {
		fprintf(stderr, "split: output file suffixes exhausted\n");
		return -1;
	}

	/* The additional suffix, and the end of the name. */
	if (additional_length > 0) {
		memcpy(split->name + prefix_length + split->suffix_length,
		       split->additional, additional_length);
	}

	/* The end of the name. */
	split->name[prefix_length + split->suffix_length + additional_length] = '\0';

	/* The piece. */
	split->output = fopen(split->name, "w");
	if (split->output == NULL) {
		command_error("split", split->name);
		return -1;
	}

	/* Succeeded: said so with --verbose. */
	if (split->verbose)
		printf("creating file '%s'\n", split->name);
	split->piece++;
	split->used = 0;
	return 0;
}

/* Closes the piece that is open, if any.  Returns 0, or -1 after a message. */
static int
close_piece(
	struct split *split)
{
	int result;

	/* No piece is open. */
	if (split->output == NULL)
		return 0;

	/* The piece, written out. */
	result = fclose(split->output);
	split->output = NULL;
	if (result != 0) {
		command_error("split", split->name);
		return -1;
	}

	/* Succeeded. */
	return 0;
}

/* Reports the usage and ends split. */
static void
usage(
	void)
{
	/* The forms. */
	fprintf(stderr, "usage: split [-l line_count] [-a suffix_length] [-d] "
		"[file [name]]\n"
		"       split -b n[k|m] [-a suffix_length] [-d] [file [name]]\n");
	exit(2);
}
