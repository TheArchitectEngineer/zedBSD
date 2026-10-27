/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws299-p004: host test of the raw block device read-ahead stream
 * (include/kern/buf-readahead.h): the first read proposes nothing, the
 * second sequential read proposes the window, later reads keep it full in
 * whole units, a jump or a reverse read restarts the stream with a new
 * generation, the device's end clips the window (and a short tail is still
 * proposed there), a rewind proposes the dropped blocks again, and a
 * reader of one block at a time never gets less than a unit at once.
 *
 *   cc -O2 -Wall -Wextra -I include -o build/tmp/buf-readahead-test plan/ws299/tests/host/buf-readahead-test.c && build/tmp/buf-readahead-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kern/buf-readahead.h"

/* The 5330's shape: 512-byte blocks, 256 KiB units, a 2 MiB window, a 1 MiB read. */
#define UNIT	512U
#define WINDOW	4096U
#define READ	2048U
#define LIMIT	(UINT64_C(1) << 30)

static unsigned failures;

/* Checks one proposal. */
static void
expect(
	const char *what,
	uint64_t start,
	uint64_t length,
	uint64_t want_start,
	uint64_t want_length)
{
	if (start == want_start && length == want_length)
		return;
	printf("FAIL %s: got [%llu,+%llu) want [%llu,+%llu)\n", what,
	    (unsigned long long)start, (unsigned long long)length,
	    (unsigned long long)want_start, (unsigned long long)want_length);
	failures++;
}

/* Reads one range and takes what is proposed. */
static void
step(
	struct buf_readahead_stream *stream,
	uint64_t block,
	uint64_t count,
	uint64_t limit,
	uint64_t *start,
	uint64_t *length)
{
	buf_readahead_plan(stream, block, count, limit, WINDOW, UNIT, start, length);
	if (*length != 0)
		buf_readahead_taken(stream, *start + *length);
}

int
main(
	void)
{
	struct buf_readahead_stream stream;
	uint64_t start;
	uint64_t length;
	uint64_t generation;
	uint64_t block;
	uint64_t total;
	unsigned index;

	/* Sequential reads: nothing, then the window, then one read's worth each. */
	memset(&stream, 0, sizeof(stream));
	step(&stream, 1000, READ, LIMIT, &start, &length);
	expect("first read", start, length, 0, 0);
	generation = stream.generation;
	step(&stream, 1000 + READ, READ, LIMIT, &start, &length);
	expect("second read", start, length, 1000 + 2 * READ, WINDOW);
	step(&stream, 1000 + 2 * READ, READ, LIMIT, &start, &length);
	expect("third read", start, length, 1000 + 3 * READ + WINDOW - READ, READ);
	if (stream.generation != generation) {
		printf("FAIL sequential reads changed the generation\n");
		failures++;
	}

	/* A jump restarts with a new generation and proposes nothing. */
	step(&stream, 500000, READ, LIMIT, &start, &length);
	expect("jump", start, length, 0, 0);
	if (stream.generation == generation) {
		printf("FAIL a jump kept the generation\n");
		failures++;
	}

	/* A reverse read restarts as well. */
	generation = stream.generation;
	step(&stream, 500000 - READ, READ, LIMIT, &start, &length);
	expect("reverse", start, length, 0, 0);
	if (stream.generation == generation) {
		printf("FAIL a reverse read kept the generation\n");
		failures++;
	}

	/* The device's end clips the window, and the short tail is still proposed. */
	memset(&stream, 0, sizeof(stream));
	step(&stream, 10000 - 3 * READ, READ, 10000, &start, &length);
	step(&stream, 10000 - 2 * READ, READ, 10000, &start, &length);
	expect("near the end", start, length, 10000 - READ, READ);
	step(&stream, 10000 - READ, READ, 10000, &start, &length);
	expect("at the end", start, length, 0, 0);

	/* A tail shorter than a unit is proposed when it reaches the end. */
	memset(&stream, 0, sizeof(stream));
	step(&stream, 0, 100, 400, &start, &length);
	step(&stream, 100, 100, 400, &start, &length);
	expect("short tail", start, length, 200, 200);

	/* A rewind proposes the dropped blocks again on the next read. */
	memset(&stream, 0, sizeof(stream));
	step(&stream, 0, READ, LIMIT, &start, &length);
	step(&stream, READ, READ, LIMIT, &start, &length);
	buf_readahead_rewind(&stream);
	step(&stream, 2 * READ, READ, LIMIT, &start, &length);
	expect("after a rewind", start, length, 3 * READ, WINDOW);

	/* A partial rewind proposes only the blocks after the point again. */
	memset(&stream, 0, sizeof(stream));
	step(&stream, 0, READ, LIMIT, &start, &length);
	step(&stream, READ, READ, LIMIT, &start, &length);
	buf_readahead_rewind_to(&stream, 3 * READ + 3 * UNIT);
	step(&stream, 2 * READ, READ, LIMIT, &start, &length);
	expect("after a partial rewind", start, length, 3 * READ + 3 * UNIT, WINDOW - 3 * UNIT);
	buf_readahead_rewind_to(&stream, 0);
	if (stream.issued != stream.next) {
		printf("FAIL a rewind went behind the reader\n");
		failures++;
	}

	/* An impossible read restarts the stream. */
	generation = stream.generation;
	step(&stream, LIMIT - 1, 2, LIMIT, &start, &length);
	expect("past the end", start, length, 0, 0);
	if (stream.generation == generation || stream.valid) {
		printf("FAIL a read past the end did not restart\n");
		failures++;
	}

	/* One block at a time: every proposal is a whole unit, and the window stays covered. */
	memset(&stream, 0, sizeof(stream));
	total = 0;
	for (block = 0; block < 100000; block++) {
		step(&stream, block, 1, LIMIT, &start, &length);
		if (length != 0 && length < UNIT) {
			printf("FAIL block %llu proposed %llu blocks\n",
			    (unsigned long long)block, (unsigned long long)length);
			failures++;
			break;
		}
		total += length;
		if (block > 1 && stream.issued + UNIT <= block + 1 + WINDOW) {
			printf("FAIL block %llu: issued %llu lags the window\n",
			    (unsigned long long)block, (unsigned long long)stream.issued);
			failures++;
			break;
		}
	}
	if (total < 100000 - 2) {
		printf("FAIL one-block reads proposed only %llu blocks\n", (unsigned long long)total);
		failures++;
	}

	/* Restart forgets the stream. */
	for (index = 0; index < 3; index++)
		buf_readahead_restart(&stream);
	if (stream.valid || stream.issued != 0) {
		printf("FAIL restart left the stream\n");
		failures++;
	}

	if (failures != 0) {
		printf("buf-readahead-test: FAIL (%u)\n", failures);
		return 1;
	}
	printf("buf-readahead-test: PASS\n");
	return 0;
}
