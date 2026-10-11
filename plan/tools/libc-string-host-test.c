/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks the libc memcpy, memmove and memset on the host.
 *
 * src/libc/string.c is compiled with its functions renamed to zedbsd_*
 * (libc-string-host-test.sh) and linked with this harness, which is built
 * against the host's headers.  Every length up to a few hundred bytes is
 * tried at every source and destination offset within two words, both
 * directions of overlap for memmove, and a few large blocks; each result is
 * compared with a byte-by-byte reference, and the bytes around the
 * destination must be untouched.  Built with AddressSanitizer, a read or a
 * write outside the block is reported as well.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The offsets tried within two words of the widest host word. */
#define TEST_OFFSET_COUNT 16

/* The longest block tried at every offset pair. */
#define TEST_SHORT_MAXIMUM 300

/* The longest block tried at every memmove placement. */
#define TEST_MOVE_MAXIMUM 160

/* The bytes checked on each side of a destination. */
#define TEST_GUARD 32

/* The byte the guard areas hold. */
#define TEST_GUARD_BYTE 0xa5

/* A 1180x800 canvas of 32-bit pixels, the copy that found the fault. */
#define TEST_CANVAS_SIZE (1180U * 800U * 4U)

/*
 * The functions under test, from src/libc/string.c.
 */
void *zedbsd_memcpy(void *destination, const void *source, size_t count);
void *zedbsd_memmove(void *destination, const void *source, size_t count);
void *zedbsd_memset(void *destination, int value, size_t count);

/*
 * The number of checks that failed.
 *
 * Every failure is printed when found; main reports the total and the exit
 * status from it.
 */
static unsigned long test_failures;

/*
 * The number of checks made, reported with the result.
 */
static unsigned long test_checks;

/*
 * The large block lengths, around page and power-of-two sizes.
 */
static const size_t test_large_lengths[] = {
	1000, 4095, 4096, 4097, 8191, 65536 + 13, 1U << 20, TEST_CANVAS_SIZE + 7
};

static void test_fill_pattern(unsigned char *bytes, size_t count, unsigned int seed);
static void test_fail(const char *operation, size_t length, size_t first, size_t second, const char *what);
static void test_copy(size_t length, size_t source_offset, size_t destination_offset);
static void test_move(size_t length, size_t source_offset, size_t destination_offset);
static void test_set(size_t length, size_t offset, int value);

/*
 * Runs every check and reports the result.
 */
int
main(void)
{
	static const int values[] = { 0, 0xff, 0x5a, -1, 0x1234 };
	size_t length;
	size_t first;
	size_t second;
	size_t index;

	/* Copies every short length at every offset pair. */
	for (length = 0; length <= TEST_SHORT_MAXIMUM; length++) {
		for (first = 0; first < TEST_OFFSET_COUNT; first++) {
			for (second = 0; second < TEST_OFFSET_COUNT; second++)
				test_copy(length, first, second);
		}
	}

	/* Copies the large lengths at a few offset pairs. */
	for (index = 0; index < sizeof(test_large_lengths) / sizeof(test_large_lengths[0]); index++) {
		test_copy(test_large_lengths[index], 0, 0);
		test_copy(test_large_lengths[index], 3, 0);
		test_copy(test_large_lengths[index], 0, 5);
		test_copy(test_large_lengths[index], 7, 9);
	}

	/* Moves every short length at every placement within one buffer, both ways. */
	for (length = 0; length <= TEST_MOVE_MAXIMUM; length++) {
		for (first = 0; first < 2 * TEST_OFFSET_COUNT + 8; first++) {
			for (second = 0; second < 2 * TEST_OFFSET_COUNT + 8; second++)
				test_move(length, first, second);
		}
	}

	/* Moves the large lengths with overlap in both directions. */
	for (index = 0; index < sizeof(test_large_lengths) / sizeof(test_large_lengths[0]); index++) {
		test_move(test_large_lengths[index], 0, 8);
		test_move(test_large_lengths[index], 8, 0);
		test_move(test_large_lengths[index], 1, 30);
		test_move(test_large_lengths[index], 30, 1);
		test_move(test_large_lengths[index], 0, 0);
	}

	/* Fills every short length at every offset with every value. */
	for (index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		for (length = 0; length <= TEST_SHORT_MAXIMUM; length++) {
			for (first = 0; first < TEST_OFFSET_COUNT; first++)
				test_set(length, first, values[index]);
		}
		test_set(TEST_CANVAS_SIZE + 3, 1, values[index]);
	}

	/* Reports the result. */
	if (test_failures != 0) {
		printf("libc string host test: FAIL (%lu of %lu checks)\n", test_failures, test_checks);
		return 1;
	}

	printf("libc string host test: PASS (%lu checks)\n", test_checks);
	return 0;
}

/* Fills bytes with a pattern that differs between neighbouring bytes and runs. */
static void
test_fill_pattern(
	unsigned char *bytes,
	size_t count,
	unsigned int seed)
{
	size_t index;

	/* Writes a value no byte-shifted copy of the pattern matches. */
	for (index = 0; index < count; index++)
		bytes[index] = (unsigned char)((index * 131U + (index >> 8) * 7U + seed) & 0xffU);
}

/* Counts and prints one failed check. */
static void
test_fail(
	const char *operation,
	size_t length,
	size_t first,
	size_t second,
	const char *what)
{
	test_failures++;

	/* Prints only the first failures, which are enough to locate a fault. */
	if (test_failures <= 20)
		printf("FAIL %s length %zu offsets %zu %zu: %s\n", operation, length, first, second, what);
}

/* Copies one block between separate buffers and checks it. */
static void
test_copy(
	size_t length,
	size_t source_offset,
	size_t destination_offset)
{
	unsigned char *source;
	unsigned char *destination;
	unsigned char *expected;
	size_t size;
	void *returned;

	/* Allocates a source that ends exactly at the block's end. */
	source = malloc(source_offset + length + 1);
	size = destination_offset + TEST_GUARD + length + TEST_GUARD;
	destination = malloc(size);
	expected = malloc(size);
	if (source == NULL || destination == NULL || expected == NULL) {
		printf("out of memory\n");
		exit(2);
	}

	/* Fills the source and guards the destination. */
	test_fill_pattern(source, source_offset + length, (unsigned int)(length + source_offset));
	memset(destination, TEST_GUARD_BYTE, size);
	memset(expected, TEST_GUARD_BYTE, size);
	memcpy(expected + destination_offset + TEST_GUARD, source + source_offset, length);

	/* Copies the block and compares the whole destination buffer. */
	returned = zedbsd_memcpy(destination + destination_offset + TEST_GUARD, source + source_offset, length);
	test_checks++;
	if (returned != destination + destination_offset + TEST_GUARD)
		test_fail("memcpy", length, source_offset, destination_offset, "wrong return value");
	if (memcmp(destination, expected, size) != 0)
		test_fail("memcpy", length, source_offset, destination_offset, "wrong bytes");

	free(expected);
	free(destination);
	free(source);
}

/* Moves one block within a buffer and checks it against a separate copy. */
static void
test_move(
	size_t length,
	size_t source_offset,
	size_t destination_offset)
{
	unsigned char *buffer;
	unsigned char *expected;
	unsigned char *saved;
	size_t size;
	void *returned;

	/* Allocates a buffer that holds both placements and the guards. */
	size = TEST_GUARD + length + 2 * TEST_OFFSET_COUNT + 8 + TEST_GUARD + 32;
	buffer = malloc(size);
	expected = malloc(size);
	saved = malloc(length + 1);
	if (buffer == NULL || expected == NULL || saved == NULL) {
		printf("out of memory\n");
		exit(2);
	}

	/* Computes the result through a separate copy of the source. */
	test_fill_pattern(buffer, size, (unsigned int)(length * 3 + source_offset));
	memcpy(expected, buffer, size);
	memcpy(saved, buffer + TEST_GUARD + source_offset, length);
	memcpy(expected + TEST_GUARD + destination_offset, saved, length);

	/* Moves the block and compares the whole buffer. */
	returned = zedbsd_memmove(buffer + TEST_GUARD + destination_offset, buffer + TEST_GUARD + source_offset, length);
	test_checks++;
	if (returned != buffer + TEST_GUARD + destination_offset)
		test_fail("memmove", length, source_offset, destination_offset, "wrong return value");
	if (memcmp(buffer, expected, size) != 0)
		test_fail("memmove", length, source_offset, destination_offset, "wrong bytes");

	free(saved);
	free(expected);
	free(buffer);
}

/* Fills one block and checks it. */
static void
test_set(
	size_t length,
	size_t offset,
	int value)
{
	unsigned char *buffer;
	unsigned char *expected;
	size_t size;
	size_t index;
	void *returned;

	/* Allocates a guarded buffer. */
	size = offset + TEST_GUARD + length + TEST_GUARD;
	buffer = malloc(size);
	expected = malloc(size);
	if (buffer == NULL || expected == NULL) {
		printf("out of memory\n");
		exit(2);
	}

	/* Builds the expected bytes: only the low byte of the value is stored. */
	memset(buffer, TEST_GUARD_BYTE, size);
	memset(expected, TEST_GUARD_BYTE, size);
	for (index = 0; index < length; index++)
		expected[offset + TEST_GUARD + index] = (unsigned char)value;

	/* Fills the block and compares the whole buffer. */
	returned = zedbsd_memset(buffer + offset + TEST_GUARD, value, length);
	test_checks++;
	if (returned != buffer + offset + TEST_GUARD)
		test_fail("memset", length, offset, (size_t)(unsigned char)value, "wrong return value");
	if (memcmp(buffer, expected, size) != 0)
		test_fail("memset", length, offset, (size_t)(unsigned char)value, "wrong bytes");

	free(expected);
	free(buffer);
}
