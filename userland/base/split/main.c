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
 * The input (standard input for - or no file) is written to files named
 * name (x by default) followed by a suffix of suffix_length lowercase
 * letters (2 by default): aa, ab, ... zz.  Each file holds line_count
 * lines (1000 by default), or with -b n bytes, n kilobytes or n
 * megabytes.  Empty input makes no file.  When the suffixes run out, split
 * stops with an error and keeps the files already written.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The lines of a piece without -l or -b. */
#define SPLIT_DEFAULT_LINES 1000

/* The suffix length without -a. */
#define SPLIT_DEFAULT_SUFFIX 2

/* The size of the buffer the input is read through. */
#define SPLIT_BUFFER_SIZE 65536

/*
 * The pieces being written.
 *
 * One instance lives for the run.  name holds the prefix and the current
 * suffix; descriptor is the open piece, or -1 between pieces.  used counts
 * the lines or bytes already in the open piece.
 */
struct split_output {
	char name[PATH_MAX + 1];
	size_t prefix_length;
	size_t suffix_length;
	unsigned long long limit;
	unsigned long long used;
	int bytes;
	int started;
	int descriptor;
};

static int read_options(int argc, char **argv, struct split_output *output);
static int parse_size(const char *text, int bytes, unsigned long long *value);
static int split_input(int input, struct split_output *output);
static int open_piece(struct split_output *output);
static int next_suffix(struct split_output *output);
static int close_piece(struct split_output *output);
static int write_all(int descriptor, const char *data, size_t length);
static void usage(void);

/*
 * Runs split.
 */
int
main(
	int argc,
	char **argv)
{
	struct split_output output;
	const char *prefix;
	int first;
	int input;
	int status;
	int compare;
	size_t length;

	/* Reads the options; the file and the name follow them. */
	first = read_options(argc, argv, &output);
	if (argc - first > 2)
		usage();

	/* The prefix of the pieces, x unless a name is given. */
	prefix = "x";
	if (argc - first == 2)
		prefix = argv[first + 1];
	length = strlen(prefix);
	if (length + output.suffix_length > PATH_MAX) {
		fprintf(stderr, "split: %s: %s\n", prefix, strerror(ENAMETOOLONG));
		return 1;
	}

	/* Starts the name with the prefix. */
	memcpy(output.name, prefix, length);
	output.prefix_length = length;

	/* Opens the input; - or none is standard input. */
	input = STDIN_FILENO;
	if (first < argc) {
		compare = strcmp(argv[first], "-");
		if (compare != 0) {
			input = open(argv[first], O_RDONLY);
			if (input < 0) {
				fprintf(stderr, "split: %s: %s\n", argv[first], strerror(errno));
				return 1;
			}
		}
	}

	/* Splits it. */
	status = split_input(input, &output);
	if (status != 0)
		return 1;

	/* Succeeded: every piece was written. */
	return 0;
}

/*
 * Reads the options and returns the index of the file operand.  An
 * invalid option ends split with a diagnostic.
 */
static int
read_options(
	int argc,
	char **argv,
	struct split_output *output)
{
	unsigned long long value;
	int option;
	int status;

	/* The defaults: 1000 lines, two letters. */
	memset(output, 0, sizeof(*output));
	output->limit = SPLIT_DEFAULT_LINES;
	output->suffix_length = SPLIT_DEFAULT_SUFFIX;
	output->descriptor = -1;

	/* Reads each option. */
	for (;;) {
		option = getopt(argc, argv, "a:b:l:");
		if (option == -1)
			break;

		/* Records what the option asks for. */
		switch (option) {
		case 'a':
			status = parse_size(optarg, 0, &value);
			if (status != 0 || value > PATH_MAX) {
				fprintf(stderr, "split: invalid suffix length: '%s'\n", optarg);
				exit(1);
			}

			/* Keeps the suffix length. */
			output->suffix_length = (size_t)value;
			break;
		case 'b':
			status = parse_size(optarg, 1, &value);
			if (status != 0) {
				fprintf(stderr, "split: invalid number of bytes: '%s'\n", optarg);
				exit(1);
			}

			/* Pieces are counted in bytes. */
			output->limit = value;
			output->bytes = 1;
			break;
		case 'l':
			status = parse_size(optarg, 0, &value);
			if (status != 0) {
				fprintf(stderr, "split: invalid number of lines: '%s'\n", optarg);
				exit(1);
			}

			/* Pieces are counted in lines. */
			output->limit = value;
			output->bytes = 0;
			break;
		default:
			usage();
			break;
		}
	}

	/* Reports where the operands start. */
	return optind;
}

/*
 * Parses a positive decimal number.  For -b a k or m suffix multiplies it
 * by 1024 or 1048576.
 */
static int
parse_size(
	const char *text,
	int bytes,
	unsigned long long *value)
{
	unsigned long long number;
	unsigned long long multiplier;
	char *end;

	/* Converts the digits. */
	if (text[0] < '0' || text[0] > '9')
		return -1;
	errno = 0;
	number = strtoull(text, &end, 10);
	if (errno != 0)
		return -1;

	/* -b takes a unit letter after the digits. */
	multiplier = 1;
	if (bytes && *end == 'k') {
		multiplier = 1024;
		end++;
	} else if (bytes && *end == 'm') {
		multiplier = 1048576;
		end++;
	}

	/* Nothing may follow, and the size must be positive and fit. */
	if (*end != '\0')
		return -1;
	if (number == 0)
		return -1;
	if (number > ~0ULL / multiplier)
		return -1;

	/* Succeeded: the number. */
	*value = number * multiplier;
	return 0;
}

/*
 * Copies the input into pieces.  A piece is opened only when a byte for
 * it arrives, so empty input makes none.
 */
static int
split_input(
	int input,
	struct split_output *output)
{
	static char buffer[SPLIT_BUFFER_SIZE];
	unsigned long long room;
	ssize_t got;
	size_t offset;
	size_t chunk;
	size_t index;
	int status;

	/* Reads the input chunk by chunk. */
	for (;;) {
		got = read(input, buffer, sizeof(buffer));
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0) {
			fprintf(stderr, "split: read error: %s\n", strerror(errno));
			close_piece(output);
			return -1;
		}

		/* The end of the input ends the copy. */
		if (got == 0)
			break;

		/* Hands the chunk out to pieces. */
		offset = 0;
		while (offset < (size_t)got) {
			/* Opens a piece when none is open. */
			if (output->descriptor < 0) {
				status = open_piece(output);
				if (status != 0)
					return -1;
			}

			/*
			 * Takes what fits in the piece: bytes up to the limit, or
			 * lines up to the newline that completes the limit.
			 */
			room = output->limit - output->used;
			chunk = (size_t)got - offset;
			if (output->bytes) {
				if (chunk > room)
					chunk = (size_t)room;
				output->used += chunk;
			} else {
				for (index = offset; index < (size_t)got; index++) {
					if (buffer[index] != '\n')
						continue;
					output->used++;
					if (output->used == output->limit)
						break;
				}

				/* A piece completed inside the chunk ends at that newline. */
				if (index < (size_t)got)
					chunk = index + 1 - offset;
			}

			/* Writes it. */
			status = write_all(output->descriptor, buffer + offset, chunk);
			if (status != 0) {
				fprintf(stderr, "split: %s: %s\n", output->name, strerror(errno));
				close_piece(output);
				return -1;
			}

			/* Goes on after what was written. */
			offset += chunk;

			/* A full piece is closed; the next byte opens another. */
			if (output->used >= output->limit) {
				status = close_piece(output);
				if (status != 0)
					return -1;
			}
		}
	}

	/* Closes the last piece. */
	status = close_piece(output);
	if (status != 0)
		return -1;

	/* Succeeded: the input is in pieces. */
	return 0;
}

/* Opens the piece of the next suffix, failing when the suffixes run out. */
static int
open_piece(
	struct split_output *output)
{
	int status;

	/* The first piece has all a's; later ones the next suffix. */
	status = next_suffix(output);
	if (status != 0) {
		fprintf(stderr, "split: output file suffixes exhausted\n");
		return -1;
	}

	/* Creates the piece, replacing a file of that name. */
	output->descriptor = open(output->name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (output->descriptor < 0) {
		fprintf(stderr, "split: %s: %s\n", output->name, strerror(errno));
		return -1;
	}

	/* Succeeded: the piece is empty and open. */
	output->used = 0;
	return 0;
}

/*
 * Moves the name to the next suffix: aa...a for the first piece, then
 * counting in letters.  Returns -1 when every suffix has been used.
 */
static int
next_suffix(
	struct split_output *output)
{
	char *suffix;
	size_t index;

	/* The first suffix is all a's. */
	suffix = output->name + output->prefix_length;
	if (!output->started) {
		memset(suffix, 'a', output->suffix_length);
		suffix[output->suffix_length] = '\0';
		output->started = 1;
		return 0;
	}

	/* Counts up from the last letter, carrying past z. */
	index = output->suffix_length;
	while (index > 0) {
		index--;
		if (suffix[index] != 'z') {
			suffix[index]++;
			return 0;
		}

		/* A z wraps to a and carries into the letter before. */
		suffix[index] = 'a';
	}

	/* Every letter was z: no suffix is left. */
	return -1;
}

/* Closes the open piece, if any. */
static int
close_piece(
	struct split_output *output)
{
	int status;

	/* Nothing is open between pieces. */
	if (output->descriptor < 0)
		return 0;

	/* Closes the piece; a failed close can lose written data. */
	status = close(output->descriptor);
	output->descriptor = -1;
	if (status != 0) {
		fprintf(stderr, "split: %s: %s\n", output->name, strerror(errno));
		return -1;
	}

	/* Succeeded: the piece is complete. */
	return 0;
}

/* Writes a whole buffer, however many writes it takes. */
static int
write_all(
	int descriptor,
	const char *data,
	size_t length)
{
	ssize_t written;
	size_t offset;

	/* Writes until every byte is out. */
	offset = 0;
	while (offset < length) {
		written = write(descriptor, data + offset, length - offset);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		/* Goes on after what was written. */
		offset += (size_t)written;
	}

	/* Succeeded: the whole buffer was written. */
	return 0;
}

/* Writes the usage message and exits with an error status. */
static void
usage(void)
{
	/* Names the POSIX forms. */
	fprintf(stderr,
		"usage: split [-l line_count] [-a suffix_length] [file [name]]\n"
		"       split -b n[k|m] [-a suffix_length] [file [name]]\n");
	exit(1);
}
