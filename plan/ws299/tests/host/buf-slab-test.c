/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws299-p002: host test of the buffer cache's index of header slabs with a
 * free slot (include/kern/buf-slab.h): every slot is handed out once, a
 * slot given back is handed out again, an emptied slab is let go of beyond
 * the one kept spare, a wrong slot is refused, and taking and giving back
 * stay a constant number of steps with twenty thousand slabs (the 5330's
 * cache of about a gigabyte has some sixteen thousand).
 *
 *   cc -O2 -I include -o buf-slab-test plan/ws299/tests/host/buf-slab-test.c && ./buf-slab-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kern/buf-slab.h"

/* The slots a slab has in the 5330's kernel (a page of headers). */
#define TEST_CAPACITY 15U

/* The slabs of the large test. */
#define TEST_SLABS 20000U

/* The give-back-and-take cycles the large test times. */
#define TEST_CYCLES 2000000U

/* The most the large test may take: the walk it replaces needed minutes. */
#define TEST_SECONDS_MAX 1.0

/*
 * One test slab: the index's record and which of its slots the test holds.
 *
 * The array of them lives for the whole test.
 */
struct test_slab {
	struct buf_slab_record record;
	uint64_t held;
};

/* The checks that failed; zero is a passing run. */
static unsigned failures;

static void expect(int condition, const char *what);
static struct test_slab *test_slab_of(struct buf_slab_record *record);
static void test_small(void);
static void test_refusals(void);
static void test_large(void);

/* Runs every test and reports PASS or FAIL. */
int
main(
	void)
{
	/* Runs the tests in turn. */
	test_small();
	test_refusals();
	test_large();

	/* Reports the run. */
	if (failures != 0) {
		printf("buf-slab-test: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("buf-slab-test: PASS\n");
	return 0;
}

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

/* Finds the test slab a record is the first member of. */
static struct test_slab *
test_slab_of(
	struct buf_slab_record *record)
{
	/* The record is the first member, so the addresses are the same. */
	return (struct test_slab *)record;
}

/* Takes every slot of a few slabs, gives them back, and takes them again. */
static void
test_small(
	void)
{
	struct buf_slab_index index;
	struct test_slab slabs[3];
	struct buf_slab_record *record;
	struct test_slab *slab;
	unsigned slot;
	unsigned count;
	unsigned i;
	int taken;
	int put;
	int duplicate;
	int retired;

	memset(&index, 0, sizeof(index));
	memset(slabs, 0, sizeof(slabs));

	/* An empty index has nothing to take. */
	taken = buf_slab_take(&index, &record, &slot);
	expect(taken == -1, "an empty index refuses a take");

	/* Adds three slabs; each counts as empty. */
	for (i = 0; i < 3U; i++)
		expect(buf_slab_insert(&index, &slabs[i].record, TEST_CAPACITY) == 0, "insert");
	expect(index.empty_count == 3U, "three empty slabs");

	/* Takes every slot once, and no slot twice. */
	duplicate = 0;
	count = 0;
	for (;;) {
		taken = buf_slab_take(&index, &record, &slot);
		if (taken != 0)
			break;
		slab = test_slab_of(record);
		if ((slab->held & (UINT64_C(1) << slot)) != 0)
			duplicate = 1;
		slab->held |= UINT64_C(1) << slot;
		count++;
	}

	expect(count == 3U * TEST_CAPACITY, "every slot is handed out");
	expect(!duplicate, "no slot is handed out twice");
	expect(index.partial == NULL, "full slabs are on no list");
	expect(index.empty_count == 0U, "no slab is empty");

	/* Gives back every slot; one emptied slab is kept, the other two are let go of. */
	retired = 0;
	for (i = 0; i < 3U; i++) {
		for (slot = 0; slot < TEST_CAPACITY; slot++) {
			put = buf_slab_put(&index, &slabs[i].record, slot);
			expect(put == 0 || put == 1, "a held slot is taken back");
			if (put == 1)
				retired++;
			slabs[i].held &= ~(UINT64_C(1) << slot);
		}
	}

	expect(retired == 2, "two emptied slabs are let go of");
	expect(index.empty_count == 1U, "one empty slab is kept");
	expect(index.partial != NULL && index.partial->partial_next == NULL, "only the kept slab is listed");

	/* The kept slab's slots are handed out again. */
	count = 0;
	for (;;) {
		taken = buf_slab_take(&index, &record, &slot);
		if (taken != 0)
			break;
		count++;
	}

	expect(count == TEST_CAPACITY, "the kept slab's slots are handed out again");
}

/* Refuses capacities the mask cannot name and slots a slab does not hold out. */
static void
test_refusals(
	void)
{
	struct buf_slab_index index;
	struct test_slab slab;
	struct buf_slab_record *record;
	unsigned slot;
	int taken;

	memset(&index, 0, sizeof(index));
	memset(&slab, 0, sizeof(slab));

	/* A slab with no slot, or more slots than the mask has bits, is refused. */
	expect(buf_slab_insert(&index, &slab.record, 0U) == -1, "capacity 0 is refused");
	expect(buf_slab_insert(&index, &slab.record, BUF_SLAB_SLOTS_MAX + 1U) == -1, "capacity 65 is refused");
	expect(index.partial == NULL, "a refused slab is not listed");

	/* A 64-slot slab has a full mask. */
	expect(buf_slab_insert(&index, &slab.record, BUF_SLAB_SLOTS_MAX) == 0, "capacity 64 is taken");
	expect(slab.record.free_mask == UINT64_MAX, "a 64-slot slab's mask is full");

	/* A free slot and a slot outside the slab are refused. */
	expect(buf_slab_put(&index, &slab.record, 3U) == -1, "a free slot is refused");
	expect(buf_slab_put(&index, &slab.record, BUF_SLAB_SLOTS_MAX) == -1, "a slot outside is refused");

	/* The lowest slot comes first, and comes back to be handed out again. */
	taken = buf_slab_take(&index, &record, &slot);
	expect(taken == 0 && slot == 0U, "slot 0 comes first");
	taken = buf_slab_take(&index, &record, &slot);
	expect(taken == 0 && slot == 1U, "slot 1 comes next");
	expect(buf_slab_put(&index, &slab.record, 0U) == 0, "slot 0 is taken back");
	expect(buf_slab_put(&index, &slab.record, 0U) == -1, "slot 0 is not taken back twice");
	taken = buf_slab_take(&index, &record, &slot);
	expect(taken == 0 && slot == 0U, "slot 0 is handed out again");
}

/*
 * Fills twenty thousand slabs, then gives back the oldest held slot and
 * takes one, as a cache that evicts its least recently used line does, and
 * times the cycles.
 */
static void
test_large(
	void)
{
	struct buf_slab_index index;
	struct test_slab *slabs;
	struct buf_slab_record *record;
	struct test_slab *slab;
	unsigned *ring_slab;
	unsigned *ring_slot;
	unsigned ring_size;
	unsigned head;
	unsigned tail;
	unsigned slot;
	unsigned cycle;
	unsigned i;
	clock_t start;
	double seconds;
	int taken;
	int put;
	int broken;

	memset(&index, 0, sizeof(index));
	broken = 0;

	/* Allocates the slabs and the ring of held slots, oldest first. */
	slabs = calloc(TEST_SLABS, sizeof(*slabs));
	if (slabs == NULL) {
		expect(0, "memory for the slabs");
		return;
	}

	ring_size = TEST_SLABS * TEST_CAPACITY;
	ring_slab = calloc(ring_size, sizeof(*ring_slab));
	if (ring_slab == NULL) {
		free(slabs);
		expect(0, "memory for the ring");
		return;
	}

	ring_slot = calloc(ring_size, sizeof(*ring_slot));
	if (ring_slot == NULL) {
		free(ring_slab);
		free(slabs);
		expect(0, "memory for the ring");
		return;
	}

	/* Fills the slabs as the cache grows: a new slab only when none has a free slot. */
	tail = 0;
	for (i = 0; i < TEST_SLABS; i++) {
		expect(buf_slab_insert(&index, &slabs[i].record, TEST_CAPACITY) == 0, "insert");
		for (slot = 0; slot < TEST_CAPACITY; slot++) {
			taken = buf_slab_take(&index, &record, &ring_slot[tail]);
			if (taken != 0) {
				broken = 1;
				break;
			}
			ring_slab[tail] = (unsigned)(test_slab_of(record) - slabs);
			tail++;
		}
	}

	expect(!broken, "every slot of a growing cache is handed out");
	expect(tail == ring_size, "the ring holds every slot");

	/* Gives back the oldest slot and takes one, the full cache's steady state. */
	head = 0;
	start = clock();
	for (cycle = 0; cycle < TEST_CYCLES; cycle++) {
		slab = &slabs[ring_slab[head]];
		put = buf_slab_put(&index, &slab->record, ring_slot[head]);
		if (put != 0)
			broken = 1;
		head = (head + 1U) % ring_size;

		taken = buf_slab_take(&index, &record, &slot);
		if (taken != 0) {
			broken = 1;
			break;
		}
		ring_slab[tail % ring_size] = (unsigned)(test_slab_of(record) - slabs);
		ring_slot[tail % ring_size] = slot;
		tail++;
	}

	seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
	printf("buf-slab-test: %u cycles with %u slabs in %.3f s\n", TEST_CYCLES, TEST_SLABS, seconds);
	expect(!broken, "the steady state never runs out of slots");
	expect(seconds < TEST_SECONDS_MAX, "the steady state stays a constant number of steps");

	free(ring_slot);
	free(ring_slab);
	free(slabs);
}
