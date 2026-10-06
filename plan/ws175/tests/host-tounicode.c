/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p002b: the host test of libpdf's /ToUnicode CMap reader
 * (userland/base/libpdf/tounicode.c): single codes, a ligature, a
 * surrogate pair, a range counting up, a range of an array, the codes'
 * byte lengths kept apart, a later entry over an earlier one, and CMaps
 * that are not one.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static int looks(const struct pdf_tounicode *map, unsigned code, unsigned length, const uint32_t *expected, size_t count);

int
main(
	void)
{
	static const char cmap[] =
		"/CIDInit /ProcSet findresource begin 12 dict begin begincmap\n"
		"/CMapName /Test def 1 begincodespacerange <00> <FF> endcodespacerange\n"
		"3 beginbfchar\n<01> <0041>\n<02> <00660069>\n<0003> <D83DDE00>\nendbfchar\n"
		"2 beginbfrange\n<10> <12> <0061>\n<20> <21> [<0058> <00590059>]\nendbfrange\n"
		"1 beginbfchar\n<01> <0042>\nendbfchar\n"
		"endcmap CMapName currentdict /CMap defineresource pop end end\n";
	static const uint32_t a[] = { 0x42 };
	static const uint32_t fi[] = { 0x66, 0x69 };
	static const uint32_t smile[] = { 0x1f600 };
	static const uint32_t c[] = { 0x63 };
	static const uint32_t x[] = { 0x58 };
	static const uint32_t yy[] = { 0x59, 0x59 };
	struct pdf_tounicode *map;
	uint32_t characters[4];
	size_t count;
	int error;

	/* The CMap. */
	error = pdf_tounicode_parse((const unsigned char *)cmap, sizeof(cmap) - 1U, &map);
	check(error == 0, "the CMap is read");
	if (error != 0)
		return 1;

	/* Each kind of entry. */
	check(looks(map, 0x01U, 1U, a, 1), "<01> is B (the later entry over A)");
	check(looks(map, 0x02U, 1U, fi, 2), "<02> is the ligature f i");
	check(looks(map, 0x0003U, 2U, smile, 1), "<0003> is U+1F600 (a surrogate pair)");
	check(looks(map, 0x12U, 1U, c, 1), "<12> is c (the range counts up from a)");
	check(looks(map, 0x20U, 1U, x, 1), "<20> is X (the range's array)");
	check(looks(map, 0x21U, 1U, yy, 2), "<21> is Y Y (the array's second string)");

	/* Codes the map does not have. */
	error = pdf_tounicode_lookup(map, 0x01U, 2U, characters, 4, &count);
	check(error == ENOENT, "<0001> (two bytes) is not <01>");
	error = pdf_tounicode_lookup(map, 0x13U, 1U, characters, 4, &count);
	check(error == ENOENT, "<13> is past the range");
	pdf_tounicode_free(map);

	/* Bytes that are not a CMap: an empty map; a broken section keeps what came before. */
	error = pdf_tounicode_parse((const unsigned char *)"not a cmap at all", 17U, &map);
	check(error == 0, "words that are no CMap: read, empty");
	if (error == 0) {
		error = pdf_tounicode_lookup(map, 0x41U, 1U, characters, 4, &count);
		check(error == ENOENT, "the empty map has nothing");
		pdf_tounicode_free(map);
	}
	error = pdf_tounicode_parse((const unsigned char *)"2 beginbfchar <41> <0061> <42> 7 endbfchar", 43U, &map);
	check(error == 0, "a broken section: read");
	if (error == 0) {
		check(looks(map, 0x41U, 1U, c, 0) == 0, "the broken section's first entry kept");
		pdf_tounicode_free(map);
	}

	/* The summary. */
	printf("host-tounicode: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Tells whether a code stands for the characters expected (count 0: only whether it is in the map, 0 when so). */
static int
looks(
	const struct pdf_tounicode *map,
	unsigned code,
	unsigned length,
	const uint32_t *expected,
	size_t count)
{
	uint32_t characters[8];
	size_t got;
	int error;

	/* The code's characters. */
	error = pdf_tounicode_lookup(map, code, length, characters, 8, &got);
	if (count == 0)
		return error;
	if (error != 0 || got != count)
		return 0;

	/* The same. */
	return memcmp(characters, expected, count * sizeof(*characters)) == 0;
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
