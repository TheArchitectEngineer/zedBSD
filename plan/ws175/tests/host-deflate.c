/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p006: the host test of libz-compat's deflate (userland/base/
 * libz-compat/deflate.c):
 *
 *   host-deflate FOLDER
 *
 * inputs (empty, short, a content stream's text, runs, random bytes, a
 * megabyte of zeros, an image's rows) at the levels 0, 1, 6 and 9 go
 * through compress2 and come back through libz-compat's uncompress the
 * same; the compressed ones are written to FOLDER (input-N.bin and
 * level-L-N.z) for the host's zlib to read back (run-host-deflate.sh).  A
 * stream fed in pieces with little room for output gives the same bytes;
 * a raw stream (windowBits -15) inflates raw.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compat/zlib/zlib.h>

#define INPUTS	7

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static unsigned char *make_input(int which, size_t *size);
static int write_file(const char *path, const unsigned char *data, size_t size);

int
main(
	int argc,
	char **argv)
{
	static const int levels[4] = { 0, 1, 6, 9 };
	unsigned char *input;
	unsigned char *packed;
	unsigned char *unpacked;
	unsigned char *pieces;
	char path[512];
	char what[128];
	z_stream stream;
	uLongf packed_size;
	uLongf unpacked_size;
	size_t size;
	size_t given;
	size_t taken;
	int which;
	int level;
	int result;
	int flush;
	int same;

	/* The folder the files go in. */
	if (argc != 2)
		return 2;

	/* Each input at each level, there and back. */
	for (which = 0; which < INPUTS; which++) {
		input = make_input(which, &size);
		if (input == NULL)
			return 1;
		(void)snprintf(path, sizeof(path), "%s/input-%d.bin", argv[1], which);
		(void)write_file(path, input, size);
		for (level = 0; level < 4; level++) {
			/* Compressed into the bound's room. */
			packed_size = compressBound((uLong)size);
			packed = malloc(packed_size);
			unpacked = malloc(size + 1U);
			if (packed == NULL || unpacked == NULL)
				return 1;
			result = compress2(packed, &packed_size, input, (uLong)size, levels[level]);
			unpacked_size = (uLongf)size + 1U;
			if (result == Z_OK)
				result = uncompress(unpacked, &unpacked_size, packed, packed_size);
			same = result == Z_OK && unpacked_size == size && memcmp(unpacked, input, size) == 0;
			(void)snprintf(what, sizeof(what), "input %d (%lu bytes) at level %d: there and back (%lu bytes packed)", which, (unsigned long)size, levels[level], (unsigned long)packed_size);
			check(same, what);
			(void)snprintf(path, sizeof(path), "%s/level-%d-%d.z", argv[1], levels[level], which);
			(void)write_file(path, packed, packed_size);
			free(packed);
			free(unpacked);
		}

		/* The next input. */
		free(input);
	}

	/* A stream fed in pieces of 1000 bytes, the output taken 7 bytes at a time, raw: inflated raw, the same. */
	input = make_input(2, &size);
	pieces = malloc(compressBound((uLong)size));
	unpacked = malloc(size + 1U);
	if (input == NULL || pieces == NULL || unpacked == NULL)
		return 1;
	memset(&stream, 0, sizeof(stream));
	result = deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
	given = 0;
	taken = 0;
	while (result == Z_OK || result == Z_BUF_ERROR) {
		/* The next piece of input (what deflate took counts), or the finish. */
		given = stream.total_in;
		stream.next_in = input + given;
		stream.avail_in = 0;
		flush = Z_FINISH;
		if (given < size) {
			stream.avail_in = 1000U;
			if (size - given < 1000U)
				stream.avail_in = (uInt)(size - given);
			flush = Z_NO_FLUSH;
		}

		/* Room for 7 bytes of output. */
		stream.next_out = pieces + taken;
		stream.avail_out = 7;
		result = deflate(&stream, flush);
		taken = stream.total_out;
		if (result == Z_STREAM_END)
			break;
	}

	/* The stream ends; inflated raw, the same. */
	(void)deflateEnd(&stream);
	check(result == Z_STREAM_END, "pieces: the stream ends");
	memset(&stream, 0, sizeof(stream));
	result = inflateInit2(&stream, -15);
	stream.next_in = pieces;
	stream.avail_in = (uInt)taken;
	stream.next_out = unpacked;
	stream.avail_out = (uInt)size + 1U;
	if (result == Z_OK)
		result = inflate(&stream, Z_FINISH);
	same = result == Z_STREAM_END && stream.total_out == size && memcmp(unpacked, input, size) == 0;
	(void)inflateEnd(&stream);
	check(same, "pieces: raw, inflated raw the same");
	free(input);
	free(pieces);
	free(unpacked);

	/* A level past 9 is refused. */
	packed_size = 64;
	check(compress2((Bytef *)path, &packed_size, (const Bytef *)"x", 1, 10) == Z_STREAM_ERROR, "level 10: Z_STREAM_ERROR");

	/* The summary. */
	printf("host-deflate: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Makes one of the inputs; NULL when memory runs out. */
static unsigned char *
make_input(
	int which,
	size_t *size)
{
	static const char content[] = "q 1 0 0 1 72 720 cm BT /F1 12 Tf 0 0 Td (Hello, the world of PDF content) Tj ET Q\n";
	unsigned char *data;
	unsigned long seed;
	size_t at;

	/* The sizes. */
	switch (which) {
	case 0:
		*size = 0;
		break;
	case 1:
		*size = 5;
		break;
	case 2:
		*size = 200000;
		break;
	case 3:
		*size = 100000;
		break;
	case 4:
		*size = 70000;
		break;
	case 5:
		*size = 1048576;
		break;
	default:
		*size = 640U * 480U * 3U;
		break;
	}

	/* The bytes. */
	data = malloc(*size + 1U);
	if (data == NULL)
		return NULL;
	seed = 12345UL;
	for (at = 0; at < *size; at++) {
		seed = seed * 1103515245UL + 12345UL;
		switch (which) {
		case 1:
			data[at] = (unsigned char)"hello"[at];
			break;
		case 2:
			data[at] = (unsigned char)content[at % (sizeof(content) - 1U)];
			break;
		case 3:
			data[at] = (unsigned char)('a' + (at / 300U) % 3U);
			break;
		case 4:
			data[at] = (unsigned char)(seed >> 16);
			break;
		case 5:
			data[at] = 0;
			break;
		default:
			data[at] = (unsigned char)(((at / 3U) % 640U) * 255U / 639U + (at % 3U) * 40U + (seed >> 28));
			break;
		}
	}

	/* The bytes. */
	return data;
}

/* Writes a file.  Returns 0 or -1. */
static int
write_file(
	const char *path,
	const unsigned char *data,
	size_t size)
{
	FILE *file;
	size_t written;

	/* The bytes. */
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;
	written = fwrite(data, 1, size, file);
	fclose(file);
	if (written != size)
		return -1;
	return 0;
}

/* Counts and prints one check. */
static void
check(
	int ok,
	const char *what)
{
	/* A check that holds. */
	if (ok) {
		test_passed++;
		printf("ok   %s\n", what);
		return;
	}

	/* One that does not. */
	test_failed++;
	printf("FAIL %s\n", what);
}
