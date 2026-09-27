/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p002: the host test of zdesktop-browser's base/ helpers (arena,
 * buffers, array, UTF-8 and UTF-16, hash, files).
 *
 *   build/ws074-host/<variant>/host-base [SCRATCH-DIRECTORY]
 *
 * Prints one line per check that fails and a summary; exits 1 on any
 * failure.
 */

#include "base/base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One WHATWG UTF-8 decoding case: the bytes and the code points they must give. */
struct utf8_case {
	const char *name;
	const char *bytes;
	size_t length;
	uint32_t expected[8];
	size_t count;
};

static int failures;
static int checks;

static void check(int condition, const char *what);
static void test_arena(void);
static void test_buffer(void);
static void test_vector(void);
static void test_utf8(void);
static void test_utf16(void);
static void test_hash(void);
static void test_file(const char *directory);

/* The UTF-8 cases, taken from the Encoding Standard's decoder rules. */
static const struct utf8_case utf8_cases[] = {
	{ "ascii", "Ab", 2, { 0x41, 0x62 }, 2 },
	{ "two bytes", "\xc3\xa9", 2, { 0xe9 }, 1 },
	{ "three bytes", "\xe3\x81\x82", 3, { 0x3042 }, 1 },
	{ "four bytes", "\xf0\x9f\x98\x80", 4, { 0x1f600 }, 1 },
	{ "lone continuation", "\x80" "a", 2, { 0xfffd, 0x61 }, 2 },
	{ "overlong two", "\xc0\xaf", 2, { 0xfffd, 0xfffd }, 2 },
	{ "overlong three", "\xe0\x80\xaf", 3, { 0xfffd, 0xfffd, 0xfffd }, 3 },
	{ "surrogate", "\xed\xa0\x80", 3, { 0xfffd, 0xfffd, 0xfffd }, 3 },
	{ "past the last", "\xf4\x90\x80\x80", 4, { 0xfffd, 0xfffd, 0xfffd, 0xfffd }, 4 },
	{ "truncated three", "\xe3\x81", 2, { 0xfffd }, 1 },
	{ "broken three", "\xe3\x81" "a", 3, { 0xfffd, 0x61 }, 2 },
	{ "impossible lead", "\xff\xfe", 2, { 0xfffd, 0xfffd }, 2 },
	{ "truncated four then ascii", "\xf0\x9f\x98" "z", 4, { 0xfffd, 0x7a }, 2 }
};

int
main(
	int argc,
	char **argv)
{
	const char *directory;

	directory = "build/ws074-host";
	if (argc > 1)
		directory = argv[1];

	test_arena();
	test_buffer();
	test_vector();
	test_utf8();
	test_utf16();
	test_hash();
	test_file(directory);

	printf("host-base: %d checks, %d failed\n", checks, failures);
	if (failures != 0)
		return 1;

	return 0;
}

/* Counts one check and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (!condition) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* Allocates small and large pieces, checks their alignment and contents, and releases. */
static void
test_arena(void)
{
	struct wb_arena arena;
	unsigned char *pieces[1000];
	unsigned char *large;
	char *copy;
	int index;
	int intact;

	wb_arena_init(&arena, 256);
	intact = 1;
	for (index = 0; index < 1000; index++) {
		pieces[index] = wb_arena_alloc(&arena, (size_t)(index % 40) + 1U);
		check(pieces[index] != NULL, "arena: small piece");
		check(((uintptr_t)pieces[index] & 15U) == 0, "arena: alignment");
		memset(pieces[index], index & 0xff, (size_t)(index % 40) + 1U);
	}
	for (index = 0; index < 1000; index++) {
		if (pieces[index][0] != (unsigned char)(index & 0xff))
			intact = 0;
	}
	check(intact, "arena: pieces keep their bytes");

	large = wb_arena_zalloc(&arena, 10000);
	check(large != NULL && large[0] == 0 && large[9999] == 0, "arena: large zeroed piece");
	copy = wb_arena_strndup(&arena, "hello world", 5);
	check(copy != NULL && strcmp(copy, "hello") == 0, "arena: strndup");
	wb_arena_release(&arena);
	check(arena.head == NULL, "arena: released");
}

/* Appends bytes, strings, code points and formatted text. */
static void
test_buffer(void)
{
	struct wb_buffer buffer;
	struct wb_units units;
	int index;

	wb_buffer_init(&buffer);
	check(strcmp(wb_buffer_string(&buffer), "") == 0, "buffer: empty string");
	wb_buffer_append_string(&buffer, "abc");
	wb_buffer_append_byte(&buffer, '-');
	wb_buffer_append_utf8(&buffer, 0x3042);
	wb_buffer_printf(&buffer, "%d/%s", 42, "x");
	check(strcmp(wb_buffer_string(&buffer), "abc-\xe3\x81\x82" "42/x") == 0, "buffer: contents");
	for (index = 0; index < 10000; index++)
		wb_buffer_append_byte(&buffer, 'z');
	check(buffer.length == 11 + 10000 && buffer.data[buffer.length] == '\0', "buffer: growth keeps the NUL");
	wb_buffer_clear(&buffer);
	check(buffer.length == 0 && strcmp(wb_buffer_string(&buffer), "") == 0, "buffer: clear");
	wb_buffer_release(&buffer);

	wb_units_init(&units);
	wb_units_append_code_point(&units, 0x41);
	wb_units_append_code_point(&units, 0x1f600);
	check(units.length == 3 && units.data[0] == 0x41 && units.data[1] == 0xd83d && units.data[2] == 0xde00,
	    "units: surrogate pair");
	wb_units_release(&units);
}

/* Pushes many items, reads them back and pops. */
static void
test_vector(void)
{
	struct wb_vector vector;
	int value;
	int index;
	int ordered;

	wb_vector_init(&vector, sizeof(int));
	for (index = 0; index < 5000; index++)
		wb_vector_push(&vector, &index);
	ordered = 1;
	for (index = 0; index < 5000; index++) {
		value = *(int *)wb_vector_at(&vector, (size_t)index);
		if (value != index)
			ordered = 0;
	}
	check(vector.count == 5000 && ordered, "vector: push and read");
	wb_vector_pop(&vector);
	check(vector.count == 4999, "vector: pop");
	wb_vector_release(&vector);
	check(vector.count == 0 && vector.items == NULL, "vector: release");
}

/* Decodes each UTF-8 case and compares the code points; round-trips through UTF-16. */
static void
test_utf8(void)
{
	struct wb_units units;
	struct wb_buffer buffer;
	char message[160];
	uint32_t got[16];
	uint32_t code_point;
	size_t offset;
	size_t used;
	size_t count;
	size_t index;
	size_t item;
	int same;

	for (item = 0; item < sizeof(utf8_cases) / sizeof(utf8_cases[0]); item++) {
		count = 0;
		offset = 0;
		while (offset < utf8_cases[item].length && count < 16) {
			used = wb_utf8_decode((const unsigned char *)utf8_cases[item].bytes + offset,
			    utf8_cases[item].length - offset, &code_point);
			offset += used;
			got[count++] = code_point;
		}
		same = count == utf8_cases[item].count;
		for (index = 0; same && index < count; index++) {
			if (got[index] != utf8_cases[item].expected[index])
				same = 0;
		}
		snprintf(message, sizeof(message), "utf8: %s", utf8_cases[item].name);
		check(same, message);
	}

	wb_units_init(&units);
	wb_buffer_init(&buffer);
	wb_utf8_to_units((const unsigned char *)"a\xc3\xa9\xe3\x81\x82\xf0\x9f\x98\x80", 10, &units);
	check(units.length == 5, "utf8: to units length");
	wb_units_to_utf8(units.data, units.length, &buffer);
	check(buffer.length == 10 && memcmp(buffer.data, "a\xc3\xa9\xe3\x81\x82\xf0\x9f\x98\x80", 10) == 0,
	    "utf8: round trip");
	wb_units_release(&units);
	wb_buffer_release(&buffer);
}

/* Decodes pairs and lone surrogates, and encodes a lone surrogate to UTF-8 as U+FFFD. */
static void
test_utf16(void)
{
	static const uint16_t pair[] = { 0xd83d, 0xde00 };
	static const uint16_t lone_high[] = { 0xd83d, 0x0041 };
	static const uint16_t lone_low[] = { 0xde00 };
	struct wb_buffer buffer;
	uint32_t code_point;
	size_t used;

	used = wb_utf16_decode(pair, 2, &code_point);
	check(used == 2 && code_point == 0x1f600, "utf16: pair");
	used = wb_utf16_decode(lone_high, 2, &code_point);
	check(used == 1 && code_point == 0xd83d, "utf16: lone high surrogate");
	used = wb_utf16_decode(lone_low, 1, &code_point);
	check(used == 1 && code_point == 0xde00, "utf16: lone low surrogate");
	used = wb_utf16_decode(pair, 1, &code_point);
	check(used == 1 && code_point == 0xd83d, "utf16: pair cut short");

	wb_buffer_init(&buffer);
	wb_units_to_utf8(lone_low, 1, &buffer);
	check(buffer.length == 3 && memcmp(buffer.data, "\xef\xbf\xbd", 3) == 0, "utf16: lone surrogate to UTF-8");
	wb_buffer_release(&buffer);
}

/* Checks that Latin-1 text hashes alike as bytes and as UTF-16. */
static void
test_hash(void)
{
	static const uint16_t units[] = { 'd', 'i', 'v', 0xe9 };
	static const unsigned char bytes[] = { 'd', 'i', 'v', 0xe9 };

	check(wb_hash_bytes(bytes, 4) == wb_hash_units(units, 4), "hash: Latin-1 bytes and units agree");
	check(wb_hash_bytes("a", 1) != wb_hash_bytes("b", 1), "hash: differs");
	check(wb_hash_bytes("", 0) == 2166136261U, "hash: empty is the basis");
}

/* Writes a file and reads it back. */
static void
test_file(
	const char *directory)
{
	struct wb_buffer buffer;
	char path[512];
	unsigned char data[200000];
	size_t index;
	int error;

	for (index = 0; index < sizeof(data); index++)
		data[index] = (unsigned char)(index * 7U);
	snprintf(path, sizeof(path), "%s/host-base-file.bin", directory);
	error = wb_file_write(path, data, sizeof(data));
	check(error == 0, "file: write");
	wb_buffer_init(&buffer);
	error = wb_file_read(path, &buffer);
	check(error == 0 && buffer.length == sizeof(data) && memcmp(buffer.data, data, sizeof(data)) == 0,
	    "file: read back");
	wb_buffer_release(&buffer);
	remove(path);
	error = wb_file_read("/nonexistent/zdesktop-browser", &buffer);
	check(error != 0, "file: a missing file is an error");
	wb_buffer_release(&buffer);
}
