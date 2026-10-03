/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws073-p053 (BUG-163): host test of the buffer cache's record of a line's
 * unjournaled blocks (include/kern/buf-unjournaled.h): an ordinary write
 * adds its blocks, a journal's pinned write over them takes them out, and
 * the runs the sync writes beneath the cache are exactly the unjournaled
 * blocks, never a pinned one.
 *
 *   cc -I include -o unjournaled-test plan/ws073/tests/host/unjournaled-test.c && ./unjournaled-test
 */

#include <stdio.h>

#include "kern/buf-unjournaled.h"

static unsigned failures;

static void expect(int condition, const char *what);
static uint32_t runs_cover(uint32_t record);

/* Reports one check. */
static void
expect(
	int condition,
	const char *what)
{
	/* Counts and names a failure. */
	if (!condition) {
		failures++;
		printf("FAILED: %s\n", what);
	}
}

/* The blocks the runs of a record cover, found run by run as the sync finds them. */
static uint32_t
runs_cover(
	uint32_t record)
{
	uint32_t cover;
	uint32_t first;
	uint32_t run;

	/* Walks the runs from the line's first block. */
	cover = 0;
	first = 0;
	for (;;) {
		run = buf_unjournaled_next_run(record, &first);
		if (run == 0)
			break;
		cover |= buf_unjournaled_bits(first, run);
		first += run;
	}

	/* The blocks covered. */
	return cover;
}

/*
 * Runs the checks.
 */
int
main(void)
{
	uint32_t record;
	uint32_t first;
	uint32_t run;
	uint32_t pinned;
	uint32_t block;
	uint32_t last_ordinary;
	uint32_t offset;
	uint32_t count;
	uint32_t bits;
	unsigned step;
	int is_pinned;

	/* The bits of a range. */
	expect(buf_unjournaled_bits(0, 8) == 0xffU, "a whole 4 KiB line of 512-byte blocks");
	expect(buf_unjournaled_bits(1, 7) == 0xfeU, "blocks 1 to 7");
	expect(buf_unjournaled_bits(7, 1) == 0x80U, "block 7");
	expect(buf_unjournaled_bits(3, 0) == 0U, "no block");
	expect(buf_unjournaled_bits(30, 5) == 0xc0000000U, "a range past the record is cut at its end");
	expect(buf_unjournaled_bits(32, 1) == 0U, "a range wholly past the record");
	expect(buf_unjournaled_bits(0, 32) == 0xffffffffU, "the whole record");

	/*
	 * A line shared by two blocks on a volume at sector 63: block 7 is the
	 * start of a file's block, blocks 0 to 6 the end of a directory's.
	 */
	record = 0;
	record = buf_unjournaled_after_write(record, 7, 1, 0);
	expect(record == 0x80U, "the file's write is unjournaled");
	record = buf_unjournaled_after_write(record, 0, 7, 1);
	expect(record == 0x80U, "the directory's pinned write leaves the file's block alone and adds nothing");
	expect(runs_cover(record) == 0x80U, "the sync writes the file's block only");

	/* A pinned write over an unjournaled block takes it out. */
	record = buf_unjournaled_after_write(0, 0, 8, 0);
	record = buf_unjournaled_after_write(record, 2, 3, 1);
	expect(record == 0xe3U, "a pinned write takes its blocks out");
	expect(runs_cover(record) == 0xe3U, "the runs are the blocks left");

	/* An ordinary write over a pinned block makes it unjournaled again (its newest data is outside the journal). */
	record = buf_unjournaled_after_write(record, 3, 1, 0);
	expect(record == 0xebU, "an ordinary write after the pinned one");

	/* The runs, one by one. */
	first = 0;
	run = buf_unjournaled_next_run(0xebU, &first);
	expect(first == 0 && run == 2, "first run: blocks 0 and 1");
	first += run;
	run = buf_unjournaled_next_run(0xebU, &first);
	expect(first == 3 && run == 1, "second run: block 3");
	first += run;
	run = buf_unjournaled_next_run(0xebU, &first);
	expect(first == 5 && run == 3, "third run: blocks 5 to 7");
	first += run;
	run = buf_unjournaled_next_run(0xebU, &first);
	expect(run == 0, "no fourth run");
	first = 0;
	run = buf_unjournaled_next_run(0U, &first);
	expect(run == 0, "an empty record has no run");
	first = 0;
	run = buf_unjournaled_next_run(0x80000000U, &first);
	expect(first == 31 && run == 1, "the record's last block");

	/*
	 * Every sequence of writes over an 8-block line: the runs never cover a
	 * block whose last write was pinned, and always cover one whose last
	 * write was ordinary.
	 */
	for (block = 0; block < 4096U; block++) {
		/* A line no write has touched. */
		record = 0;
		last_ordinary = 0;
		pinned = 0;
		for (step = 0; step < 6U; step++) {
			/* A write chosen by the bits of the sequence's number. */
			offset = (block * 7U + step * 3U) % 8U;
			count = 1U + (block >> step) % (8U - offset);
			is_pinned = (int)((block >> step) & 1U);
			bits = buf_unjournaled_bits(offset, count);
			record = buf_unjournaled_after_write(record, offset, count, is_pinned);

			/* The model: the last writer of each block. */
			if (is_pinned) {
				pinned |= bits;
				last_ordinary &= ~bits;
			} else {
				last_ordinary |= bits;
				pinned &= ~bits;
			}
		}

		/* The record against the model. */
		expect(runs_cover(record) == last_ordinary, "the runs are the blocks last written ordinarily");
		expect((runs_cover(record) & pinned) == 0U, "the runs never cover a block last written pinned");
	}

	/* Reports a failure. */
	if (failures != 0) {
		printf("unjournaled-test: FAIL (%u failed)\n", failures);
		return 1;
	}

	/* Reports the pass. */
	printf("unjournaled-test: PASS\n");

	/* Succeeded. */
	return 0;
}
