/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws299-p010: host test of the O_DIRECT arithmetic (include/kern/buf-direct.h):
 * which offsets and lengths a direct transfer accepts, and how a request is
 * cut into pieces that go to the caller's pages in place and pieces that go
 * through staging, so that every piece is whole blocks, the pieces cover the
 * request exactly, and a page-aligned buffer goes in place in 4 MiB pieces
 * (staging in 1 MiB pieces).
 *
 *   cc -O2 -Wall -Wextra -I include -o build/tmp/buf-direct-test plan/ws299/tests/host/buf-direct-test.c && build/tmp/buf-direct-test
 */

#include <stdio.h>
#include <stdlib.h>

#include "kern/buf-direct.h"

#define PAGE	4096U

static int failures;

static void
check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/*
 * Cuts a request as block_pdirect() does, with every page private, and
 * checks the pieces; reports the number of in-place bytes.
 */
static size_t
cut(size_t user, size_t length, uint32_t block, unsigned *pieces)
{
	size_t done;
	size_t piece;
	size_t placed;
	size_t at;

	done = 0;
	placed = 0;
	*pieces = 0;
	while (done < length) {
		at = (user + done) % PAGE;
		piece = buf_direct_page_piece(at, length - done, PAGE, BUF_DIRECT_PIECE_BYTES);
		if (piece != 0) {
			placed += piece;
		} else {
			piece = buf_direct_staged_piece(at, length - done, PAGE, block,
			    BUF_DIRECT_STAGED_BYTES);
			if (piece == 0)
				piece = block;
		}
		check(piece % block == 0, "piece is whole blocks");
		check(piece <= BUF_DIRECT_PIECE_BYTES, "piece within bound");
		check(piece <= length - done, "piece within request");
		if (piece == 0 || piece % block != 0 || piece > length - done)
			return placed;
		done += piece;
		(*pieces)++;
	}
	return placed;
}

int
main(void)
{
	unsigned pieces;
	size_t placed;

	/* Accepted ranges: whole blocks only, a negative offset never. */
	check(!buf_direct_misaligned(0, 0, 512), "empty at 0");
	check(!buf_direct_misaligned(512, 4096, 512), "512 B blocks");
	check(buf_direct_misaligned(256, 512, 512), "offset inside a block");
	check(buf_direct_misaligned(512, 100, 512), "length inside a block");
	check(buf_direct_misaligned(512, 4096, 4096), "512 offset on a 4 KiB disk");
	check(!buf_direct_misaligned(8192, 1U << 20, 4096), "4 KiB blocks");
	check(buf_direct_misaligned(-4096, 4096, 512), "negative offset");
	check(buf_direct_misaligned(0, 4096, 0), "no block size");

	/* An aligned buffer goes in place in whole pieces. */
	placed = cut(0, 4U << 20, 512, &pieces);
	check(placed == 4U << 20 && pieces == 1, "aligned 4 MiB: one 4 MiB piece in place");
	placed = cut(0, 4096, 512, &pieces);
	check(placed == 4096 && pieces == 1, "aligned 4 KiB in place");

	/* A buffer 512 bytes into a page stages up to the page, then goes in place. */
	placed = cut(512, 1U << 20, 512, &pieces);
	check(placed == (1U << 20) - 4096, "512 B in: one page staged, the rest in place");
	check(pieces == 3, "512 B in: staged head, in-place body, staged tail");

	/* A buffer at an odd address stages everything. */
	placed = cut(3, 3U << 20, 512, &pieces);
	check(placed == 0 && pieces == 3, "odd address: three staged 1 MiB pieces");

	/* A tail shorter than a page is staged; 512 B sectors keep their blocks. */
	placed = cut(0, 4096 + 1536, 512, &pieces);
	check(placed == 4096 && pieces == 2, "page then 1536 B staged");

	/* A 4 KiB disk with a buffer 512 bytes into a page cannot reach a block boundary at the page. */
	placed = cut(512, 8192, 4096, &pieces);
	check(placed == 0 && pieces == 1, "4 KiB disk, 512 B in: one staged piece");

	if (failures != 0) {
		printf("buf-direct-test: %d FAIL\n", failures);
		return 1;
	}
	printf("buf-direct-test: PASS\n");
	return 0;
}
