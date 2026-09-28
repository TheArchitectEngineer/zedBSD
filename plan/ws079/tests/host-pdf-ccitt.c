/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libpdf's CCITT fax decoder (ws079-p015).
 *
 *   host-pdf-ccitt exact DIR       decodes every case of DIR/cases.txt (make-ccitt-data.py: ghostscript's
 *                                  CCITTFaxEncode of known bitmaps) and compares the rows bit for bit with the
 *                                  bitmap; rows the decode gives past the bitmap's height are not compared
 *   host-pdf-ccitt fuzz DIR N      decodes N corruptions of each case's data (changed, inserted and removed bytes,
 *                                  a cut end) and of its parameters, and checks that each output is whole rows
 *                                  within the limit; under ASan and UBSan any fault ends the test
 *
 * It calls pdf_ccitt_decode() of userland/base/libpdf/ccitt.c directly.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

/* The longest name of a case. */
#define TEST_NAME_MAX 128

/*
 * One case of cases.txt: its name, the parameters the decoder gets, and
 * the rows of its bitmap.
 */
struct test_case {
	char name[TEST_NAME_MAX];
	struct pdf_ccitt_parameters parameters;
	long height;
};

static int read_case(FILE *list, struct test_case *test);
static unsigned char *read_file(const char *dir, const char *name, const char *suffix, size_t *size);
static int run_exact(const char *dir);
static int run_fuzz(const char *dir, long rounds);
static int compare_rows(const struct test_case *test, const unsigned char *output, size_t output_size, const unsigned char *expected, size_t expected_size);
static unsigned long next_random(unsigned long *state);

/* Runs the mode the command line names. */
int
main(
	int argc,
	char **argv)
{
	int failed;

	/* The mode and its arguments. */
	if (argc == 3 && strcmp(argv[1], "exact") == 0) {
		failed = run_exact(argv[2]);
		return failed != 0;
	}
	if (argc == 4 && strcmp(argv[1], "fuzz") == 0) {
		failed = run_fuzz(argv[2], atol(argv[3]));
		return failed != 0;
	}

	/* Anything else is a usage error. */
	fprintf(stderr, "usage: host-pdf-ccitt exact DIR | fuzz DIR ROUNDS\n");
	return 2;
}

/* Reads the next line of cases.txt; 0 at its end. */
static int
read_case(
	FILE *list,
	struct test_case *test)
{
	long end_of_line;
	long byte_align;
	long end_of_block;
	long black_is_1;
	int fields;

	/* NAME K COLUMNS ROWS ENDOFLINE BYTEALIGN ENDOFBLOCK BLACKIS1 HEIGHT. */
	memset(test, 0, sizeof(*test));
	fields = fscanf(list, "%127s %ld %ld %ld %ld %ld %ld %ld %ld", test->name, &test->parameters.k, &test->parameters.columns,
	    &test->parameters.rows, &end_of_line, &byte_align, &end_of_block, &black_is_1, &test->height);
	if (fields != 9)
		return 0;

	/* The flags. */
	test->parameters.end_of_line = end_of_line != 0;
	test->parameters.byte_align = byte_align != 0;
	test->parameters.end_of_block = end_of_block != 0;
	test->parameters.black_is_1 = black_is_1 != 0;
	test->parameters.damaged_rows = 0;
	return 1;
}

/* Reads DIR/cases/NAME.SUFFIX into a new buffer. */
static unsigned char *
read_file(
	const char *dir,
	const char *name,
	const char *suffix,
	size_t *size)
{
	char path[1024];
	unsigned char *data;
	FILE *file;
	long length;

	/* Opens the file and measures it. */
	snprintf(path, sizeof(path), "%s/cases/%s.%s", dir, name, suffix);
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Reads it whole (one byte more keeps an empty file's buffer valid). */
	data = malloc((size_t)length + 1);
	if (data == NULL) {
		fclose(file);
		return NULL;
	}
	*size = fread(data, 1, (size_t)length, file);
	fclose(file);
	return data;
}

/* Decodes every case and compares it with its bitmap; nonzero when one differs. */
static int
run_exact(
	const char *dir)
{
	struct test_case test;
	unsigned char *coded;
	unsigned char *expected;
	unsigned char *output;
	size_t coded_size;
	size_t expected_size;
	size_t output_size;
	char path[1024];
	FILE *list;
	int cases;
	int failures;
	int error;
	int differs;

	/* The list of cases. */
	snprintf(path, sizeof(path), "%s/cases.txt", dir);
	list = fopen(path, "r");
	if (list == NULL) {
		fprintf(stderr, "host-pdf-ccitt: cannot open %s\n", path);
		return 1;
	}

	/* Each case. */
	cases = 0;
	failures = 0;
	while (read_case(list, &test)) {
		coded = read_file(dir, test.name, "bin", &coded_size);
		expected = read_file(dir, test.name, "raw", &expected_size);
		if (coded == NULL || expected == NULL) {
			printf("FAILED %s: missing files\n", test.name);
			failures++;
			free(coded);
			free(expected);
			continue;
		}

		/* Decodes and compares. */
		error = pdf_ccitt_decode(&test.parameters, coded, coded_size, &output, &output_size);
		if (error != 0) {
			printf("FAILED %s: decode error %d\n", test.name, error);
			failures++;
		} else {
			differs = compare_rows(&test, output, output_size, expected, expected_size);
			if (differs) {
				failures++;
			} else {
				printf("ok %s (%lu bytes coded, %lu rows)\n", test.name, (unsigned long)coded_size,
				    (unsigned long)(output_size / (((size_t)test.parameters.columns + 7) / 8)));
			}
			free(output);
		}
		free(coded);
		free(expected);
		cases++;
	}
	fclose(list);

	/* The summary the script reads. */
	printf("host-pdf-ccitt exact: %d cases, %d failed\n", cases, failures);
	if (cases == 0)
		return 1;
	return failures != 0;
}

/*
 * Compares decoded rows with the bitmap: every pixel of every row the
 * bitmap has (the padding bits of a row are not pixels).
 */
static int
compare_rows(
	const struct test_case *test,
	const unsigned char *output,
	size_t output_size,
	const unsigned char *expected,
	size_t expected_size)
{
	size_t stride;
	size_t rows;
	size_t row;
	long column;
	int got;
	int wanted;

	/* The decode must hold every row of the bitmap. */
	stride = ((size_t)test->parameters.columns + 7) / 8;
	rows = expected_size / stride;
	if (output_size % stride != 0) {
		printf("FAILED %s: %lu bytes is not whole rows\n", test->name, (unsigned long)output_size);
		return 1;
	}
	if (output_size / stride < rows) {
		printf("FAILED %s: %lu rows decoded, %lu in the bitmap\n", test->name, (unsigned long)(output_size / stride),
		    (unsigned long)rows);
		return 1;
	}

	/* Each pixel. */
	for (row = 0; row < rows; row++) {
		for (column = 0; column < test->parameters.columns; column++) {
			got = (output[row * stride + (size_t)column / 8] >> (7 - column % 8)) & 1;
			wanted = (expected[row * stride + (size_t)column / 8] >> (7 - column % 8)) & 1;
			if (got != wanted) {
				printf("FAILED %s: row %lu column %ld is %d, not %d\n", test->name, (unsigned long)row, column, got,
				    wanted);
				return 1;
			}
		}
	}

	/* Every pixel is the bitmap's. */
	return 0;
}

/* Decodes corrupted copies of every case; nonzero when an output is not whole rows. */
static int
run_fuzz(
	const char *dir,
	long rounds)
{
	struct test_case test;
	struct pdf_ccitt_parameters parameters;
	unsigned long state;
	unsigned char *coded;
	unsigned char *copy;
	unsigned char *output;
	size_t coded_size;
	size_t copy_size;
	size_t output_size;
	size_t stride;
	size_t at;
	char path[1024];
	FILE *list;
	long round;
	long decoded;
	long changes;
	long change;
	int failures;
	int error;

	/* The list of cases. */
	snprintf(path, sizeof(path), "%s/cases.txt", dir);
	list = fopen(path, "r");
	if (list == NULL)
		return 1;

	/* Each case, corrupted many times. */
	state = 15;
	failures = 0;
	while (read_case(list, &test)) {
		coded = read_file(dir, test.name, "bin", &coded_size);
		if (coded == NULL)
			continue;
		copy = malloc(coded_size + 64);
		if (copy == NULL) {
			free(coded);
			break;
		}
		decoded = 0;
		for (round = 0; round < rounds; round++) {
			/* A copy with a few bytes changed, one inserted or removed, or its end cut. */
			memcpy(copy, coded, coded_size);
			copy_size = coded_size;
			changes = 1 + (long)(next_random(&state) % 4);
			for (change = 0; change < changes && copy_size > 0; change++) {
				at = next_random(&state) % copy_size;
				switch (next_random(&state) % 4) {
				case 0:
					copy[at] = (unsigned char)next_random(&state);
					break;
				case 1:
					copy[at] ^= (unsigned char)(1U << (next_random(&state) % 8));
					break;
				case 2:
					memmove(copy + at, copy + at + 1, copy_size - at - 1);
					copy_size--;
					break;
				default:
					copy_size = at;
					break;
				}
			}

			/* The parameters, now and then changed too. */
			parameters = test.parameters;
			if (next_random(&state) % 8 == 0)
				parameters.k = (long)(next_random(&state) % 5) - 2;
			if (next_random(&state) % 8 == 0)
				parameters.end_of_line = !parameters.end_of_line;
			if (next_random(&state) % 8 == 0)
				parameters.byte_align = !parameters.byte_align;
			if (next_random(&state) % 8 == 0)
				parameters.end_of_block = !parameters.end_of_block;
			if (next_random(&state) % 16 == 0)
				parameters.columns = 1 + (long)(next_random(&state) % 4000);
			if (next_random(&state) % 16 == 0)
				parameters.rows = (long)(next_random(&state) % 3000);
			if (next_random(&state) % 16 == 0)
				parameters.damaged_rows = (long)(next_random(&state) % 50);

			/* Decodes; an output is whole rows within the limit. */
			error = pdf_ccitt_decode(&parameters, copy, copy_size, &output, &output_size);
			if (error == 0) {
				decoded++;
				stride = ((size_t)parameters.columns + 7) / 8;
				if (output_size % stride != 0 || output_size > PDF_FILTER_OUTPUT_MAX) {
					printf("FAILED %s round %ld: %lu bytes\n", test.name, round, (unsigned long)output_size);
					failures++;
				}
				free(output);
			}
		}
		printf("fuzz %s: %ld rounds, %ld decoded\n", test.name, rounds, decoded);
		free(copy);
		free(coded);
	}
	fclose(list);

	/* The summary the script reads. */
	printf("host-pdf-ccitt fuzz: %d failed\n", failures);
	return failures != 0;
}

/* A small deterministic generator (xorshift). */
static unsigned long
next_random(
	unsigned long *state)
{
	unsigned long value;

	/* The next state, kept to 32 bits. */
	value = *state & 0xffffffffUL;
	value ^= (value << 13) & 0xffffffffUL;
	value ^= value >> 17;
	value ^= (value << 5) & 0xffffffffUL;
	*state = value;
	return value;
}
