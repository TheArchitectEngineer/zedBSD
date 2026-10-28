/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amd64 memcpy of src/libc/string.c (renamed zb_memcpy by
 * memcpy-test.sh): every length from 0 to 1100 and lengths around the
 * string copy threshold and the page, each at every source and destination
 * offset 0 to 15 inside separate buffers.  For each copy the destination
 * must hold the source's bytes, no byte outside it may change (guard bytes
 * on both sides), the source must not change, and the return value must be
 * the destination.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *zb_memcpy(void *destination, const void *source, size_t count);

/* The one outside function string.c names (its strdup), never called here. */
char *heap_strdup_active(const char *text);
char *
heap_strdup_active(const char *text)
{
	(void)text;
	abort();
}

#define GUARD 64U
#define LIMIT (70000U + 2U * GUARD + 32U)

static unsigned char source_area[LIMIT];
static unsigned char destination_area[LIMIT];
static unsigned char expected_source[LIMIT];
static unsigned long failures;
static unsigned long cases;

/* Fills an area with a pattern that differs between calls. */
static void
pattern(unsigned char *area, size_t length, unsigned seed)
{
	size_t i;
	uint32_t state;

	state = seed * 2654435761U + 1U;
	for (i = 0; i < length; i++) {
		state = state * 1103515245U + 12345U;
		area[i] = (unsigned char)(state >> 16);
	}
}

/* Copies count bytes from source offset so to destination offset dof and checks everything. */
static void
one(size_t count, size_t so, size_t dof, unsigned seed)
{
	size_t area;
	size_t i;
	unsigned char *from;
	unsigned char *to;
	void *returned;

	area = count + 2U * GUARD + 32U;
	pattern(source_area, area, seed);
	pattern(destination_area, area, seed + 7777U);
	memcpy(expected_source, source_area, area);
	from = source_area + GUARD + so;
	to = destination_area + GUARD + dof;
	returned = zb_memcpy(to, from, count);
	cases++;

	/* The return value, the copied bytes, and the source untouched. */
	if (returned != to || memcmp(to, from, count) != 0 || memcmp(source_area, expected_source, area) != 0) {
		failures++;
		if (failures < 10)
			printf("FAIL count %zu src+%zu dst+%zu: copy or source\n", count, so, dof);
		return;
	}

	/* Nothing outside the destination changed: compare with the pattern before the copy. */
	pattern(expected_source, area, seed + 7777U);
	for (i = 0; i < area; i++) {
		if (destination_area + i >= to && destination_area + i < to + count)
			continue;
		if (destination_area[i] != expected_source[i]) {
			failures++;
			if (failures < 10)
				printf("FAIL count %zu src+%zu dst+%zu: byte %zd outside written\n", count, so, dof,
				    (ssize_t)(destination_area + i - to));
			return;
		}
	}
}

int
main(void)
{
	static const size_t extra[] = { 2040, 2046, 2047, 2048, 2049, 2050, 2079, 2080, 2081, 4095, 4096, 4097,
		8191, 8192, 8193, 65535, 65536, 65537, 69999 };
	size_t count;
	size_t so;
	size_t dof;
	size_t i;
	unsigned seed;

	seed = 1;
	for (count = 0; count <= 1100U; count++)
		for (so = 0; so < 16U; so++)
			for (dof = 0; dof < 16U; dof++)
				one(count, so, dof, seed++);
	for (i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
		for (so = 0; so < 16U; so++)
			for (dof = 0; dof < 16U; dof++)
				one(extra[i], so, dof, seed++);
	printf("memcpy-test: %lu cases, %lu failures\n", cases, failures);
	printf("memcpy-test: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
