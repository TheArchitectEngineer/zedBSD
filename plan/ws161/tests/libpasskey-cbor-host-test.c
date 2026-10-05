/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of libpasskey's CBOR (ws161-p004, userland/base/libpasskey/cbor.c):
 * the examples of RFC 8949 Appendix A that the subset has, written and read
 * back, and the inputs the subset refuses (floats, tags, indefinite
 * lengths, heads not of the shortest form, maps out of canonical order or
 * with a key twice, items cut short, nesting too deep).
 */

#include "userland/base/libpasskey/cbor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The checks that failed. */
static unsigned failures;

/* Counts and reports a check that does not hold. */
static void
expect(
	int condition,
	const char *what)
{
	if (condition)
		return;
	failures++;
	printf("FAIL: %s\n", what);
}

/* Turns hex text into bytes; returns the count. */
static size_t
unhex(
	const char *text,
	uint8_t *bytes)
{
	size_t count;
	unsigned value;

	count = 0U;
	while (text[0] != '\0' && text[1] != '\0') {
		(void)sscanf(text, "%2x", &value);
		bytes[count++] = (uint8_t)value;
		text += 2;
	}
	return count;
}

/* Checks that a writer produced exactly the hex. */
static void
expect_written(
	const struct pk_cbor_writer *writer,
	const char *hex,
	const char *what)
{
	uint8_t want[64];
	size_t length;

	length = unhex(hex, want);
	expect(writer->error == 0 && writer->length == length && memcmp(writer->buffer, want, length) == 0, what);
}

/* Writes one unsigned or signed integer and compares it. */
static void
expect_integer(
	int64_t value,
	const char *hex)
{
	struct pk_cbor_writer writer;
	struct pk_cbor_reader reader;
	struct pk_cbor_item item;
	uint8_t buffer[16];
	int64_t back;
	int error;

	pk_cbor_writer_init(&writer, buffer, sizeof(buffer));
	pk_cbor_put_integer(&writer, value);
	expect_written(&writer, hex, hex);
	pk_cbor_reader_init(&reader, buffer, writer.length);
	error = pk_cbor_read(&reader, &item);
	expect(error == 0, "read back an integer");
	error = pk_cbor_integer(&item, &back);
	expect(error == 0 && back == value && reader.offset == writer.length, "integer read back");
}

/* Checks an input against pk_cbor_check(). */
static void
expect_check(
	const char *hex,
	unsigned flags,
	int want,
	const char *what)
{
	uint8_t bytes[64];
	size_t length;
	size_t used;
	int error;

	length = unhex(hex, bytes);
	error = pk_cbor_check(bytes, length, flags, &used);
	expect(error == want && (want != 0 || used == length), what);
}

int
main(void)
{
	struct pk_cbor_writer writer;
	struct pk_cbor_reader reader;
	struct pk_cbor_item item;
	uint8_t buffer[64];
	uint8_t small[2];
	static const uint8_t four[4] = { 1, 2, 3, 4 };
	int error;

	/* Integers (RFC 8949 Appendix A). */
	expect_integer(0, "00");
	expect_integer(1, "01");
	expect_integer(10, "0a");
	expect_integer(23, "17");
	expect_integer(24, "1818");
	expect_integer(25, "1819");
	expect_integer(100, "1864");
	expect_integer(1000, "1903e8");
	expect_integer(1000000, "1a000f4240");
	expect_integer(1000000000000LL, "1b000000e8d4a51000");
	expect_integer(-1, "20");
	expect_integer(-10, "29");
	expect_integer(-100, "3863");
	expect_integer(-1000, "3903e7");
	pk_cbor_writer_init(&writer, buffer, sizeof(buffer));
	pk_cbor_put_unsigned(&writer, UINT64_MAX);
	expect_written(&writer, "1bffffffffffffffff", "18446744073709551615");

	/* Strings. */
	pk_cbor_writer_init(&writer, buffer, sizeof(buffer));
	pk_cbor_put_bytes(&writer, NULL, 0U);
	pk_cbor_put_bytes(&writer, four, sizeof(four));
	pk_cbor_put_text(&writer, "", 0U);
	pk_cbor_put_text(&writer, "IETF", 4U);
	expect_written(&writer, "40440102030460" "6449455446", "strings");

	/* [1, [2, 3], [4, 5]] and {"a": 1, "b": [2, 3]}, true, false, null. */
	pk_cbor_writer_init(&writer, buffer, sizeof(buffer));
	pk_cbor_put_array(&writer, 3U);
	pk_cbor_put_unsigned(&writer, 1U);
	pk_cbor_put_array(&writer, 2U);
	pk_cbor_put_unsigned(&writer, 2U);
	pk_cbor_put_unsigned(&writer, 3U);
	pk_cbor_put_array(&writer, 2U);
	pk_cbor_put_unsigned(&writer, 4U);
	pk_cbor_put_unsigned(&writer, 5U);
	expect_written(&writer, "8301820203820405", "nested arrays");
	pk_cbor_writer_init(&writer, buffer, sizeof(buffer));
	pk_cbor_put_map(&writer, 2U);
	pk_cbor_put_text(&writer, "a", 1U);
	pk_cbor_put_unsigned(&writer, 1U);
	pk_cbor_put_text(&writer, "b", 1U);
	pk_cbor_put_array(&writer, 2U);
	pk_cbor_put_unsigned(&writer, 2U);
	pk_cbor_put_unsigned(&writer, 3U);
	pk_cbor_put_bool(&writer, 1);
	pk_cbor_put_bool(&writer, 0);
	pk_cbor_put_null(&writer);
	expect_written(&writer, "a26161016162820203f5f4f6", "map and simple values");

	/* A writer that runs out of room says so and keeps nothing more. */
	pk_cbor_writer_init(&writer, small, sizeof(small));
	pk_cbor_put_bytes(&writer, four, sizeof(four));
	pk_cbor_put_unsigned(&writer, 1U);
	expect(writer.error == ENOSPC, "writer out of room");

	/* Reading a string gives its bytes in place. */
	pk_cbor_reader_init(&reader, (const uint8_t *)"\x64IETF", 5U);
	error = pk_cbor_read(&reader, &item);
	expect(error == 0 && item.kind == PK_CBOR_TEXT && item.value == 4U && memcmp(item.bytes, "IETF", 4U) == 0,
	    "read a text string");

	/* Well-formed items, and skipping one. */
	expect_check("8301820203820405", 0U, 0, "check nested arrays");
	expect_check("a26161016162820203", PK_CBOR_STRICT_MAPS, 0, "check a canonical map");
	expect_check("a201020304", PK_CBOR_STRICT_MAPS, 0, "check {1: 2, 3: 4}");
	expect_check("a2016161186401", PK_CBOR_STRICT_MAPS, 0, "check a shorter key before a longer one");
	pk_cbor_reader_init(&reader, (const uint8_t *)"\x83\x01\x82\x02\x03\x82\x04\x05\xf6", 9U);
	error = pk_cbor_skip(&reader);
	expect(error == 0 && reader.offset == 8U, "skip a whole array");

	/* What the subset refuses. */
	expect_check("f97c00", 0U, EINVAL, "a half-precision float");
	expect_check("fb3ff199999999999a", 0U, EINVAL, "a double");
	expect_check("c11a514b67b0", 0U, EINVAL, "a tag");
	expect_check("5f42010243030405ff", 0U, EINVAL, "an indefinite byte string");
	expect_check("9fff", 0U, EINVAL, "an indefinite array");
	expect_check("1800", 0U, EINVAL, "1800 is not the shortest form of 0");
	expect_check("190001", 0U, EINVAL, "190001 is not the shortest form of 1");
	expect_check("f0", 0U, EINVAL, "an unassigned simple value");
	expect_check("f818", 0U, EINVAL, "a two-byte simple value");
	expect_check("a20102", 0U, EMSGSIZE, "a map cut short");
	expect_check("a201020103", PK_CBOR_STRICT_MAPS, EINVAL, "a key twice");
	expect_check("a203040102", PK_CBOR_STRICT_MAPS, EINVAL, "keys out of order");
	expect_check("a2186401616101", PK_CBOR_STRICT_MAPS, 0, "100 before \"a\": same length, smaller bytes first");
	expect_check("a2626161010102", PK_CBOR_STRICT_MAPS, EINVAL, "a longer key before a shorter one");
	expect_check("1a0000", 0U, EMSGSIZE, "a head cut short");
	expect_check("4501020304", 0U, EMSGSIZE, "a string past the end");
	expect_check("818181818181818101", 0U, EINVAL, "nesting too deep");
	expect_check("8181818181818101", 0U, 0, "nesting just deep enough");

	/* The verdict. */
	if (failures != 0U) {
		printf("libpasskey-cbor-host-test: FAIL (%u)\n", failures);
		return 1;
	}
	printf("libpasskey-cbor-host-test: PASS\n");
	return 0;
}
