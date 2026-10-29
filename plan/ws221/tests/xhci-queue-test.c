/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the xHCI bulk endpoint request queue decisions
 * (include/drivers/pci/pci-xhci-queue.h): FIFO order, removal anywhere,
 * ring room and admission, event matching by TRB address (head, behind,
 * across the Link TRB, late events) and which completions halt.
 */

#include <stdio.h>
#include <stdlib.h>

#include <drivers/pci/pci-xhci-queue.h>

#define RING_TRBS 256U
#define RING_BASE 0x10000000ULL

static unsigned failures;

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
			       #condition);                                    \
			failures++;                                            \
		}                                                              \
	} while (0)

static uint64_t trb_address(unsigned index);
static void test_fifo(void);
static void test_remove(void);
static void test_room(void);
static void test_match(void);
static void test_halts(void);
static void test_stopped(void);
static void test_model(void);

/* Returns the device address of one ring entry. */
static uint64_t
trb_address(
	unsigned index)
{
	/* Succeeded: sixteen bytes a TRB. */
	return RING_BASE + (uint64_t)index * 16U;
}

/* Pushes and pops keep submit order, wrap the array and refuse a fifth. */
static void
test_fifo(void)
{
	struct drv_xhci_queue queue;
	int items[8];
	unsigned round;
	unsigned index;

	/* Fills and empties the queue several times so the head wraps. */
	drv_xhci_queue_init(&queue);
	for (round = 0; round < 3U; round++) {
		for (index = 0; index < DRV_XHCI_QUEUE_MAX; index++)
			CHECK(drv_xhci_queue_push(&queue, &items[index]) == 0);
		CHECK(drv_xhci_queue_push(&queue, &items[4]) == -1);
		CHECK(queue.count == DRV_XHCI_QUEUE_MAX);
		for (index = 0; index < DRV_XHCI_QUEUE_MAX; index++) {
			CHECK(drv_xhci_queue_at(&queue, 0) == &items[index]);
			CHECK(drv_xhci_queue_remove(&queue, &items[index]) == 1);
		}
		CHECK(queue.count == 0);
		CHECK(drv_xhci_queue_at(&queue, 0) == NULL);

		/* Moves the head on by one so the next round starts elsewhere. */
		CHECK(drv_xhci_queue_push(&queue, &items[5]) == 0);
		CHECK(drv_xhci_queue_remove(&queue, &items[5]) == 1);
	}

	/* A NULL entry is refused and a missing one is not found. */
	CHECK(drv_xhci_queue_push(&queue, NULL) == -1);
	CHECK(drv_xhci_queue_find(&queue, &items[0]) == -1);
	CHECK(drv_xhci_queue_remove(&queue, &items[0]) == 0);
}

/* Taking a middle or last request keeps the others in order. */
static void
test_remove(void)
{
	struct drv_xhci_queue queue;
	int items[4];
	unsigned start;
	unsigned taken;
	unsigned index;
	unsigned position;

	/* Every starting head and every request taken. */
	for (start = 0; start < DRV_XHCI_QUEUE_MAX; start++) {
		for (taken = 0; taken < 4U; taken++) {
			drv_xhci_queue_init(&queue);
			queue.head = start;
			for (index = 0; index < 4U; index++)
				CHECK(drv_xhci_queue_push(&queue, &items[index]) == 0);
			CHECK(drv_xhci_queue_remove(&queue, &items[taken]) == 1);
			CHECK(queue.count == 3U);
			position = 0;
			for (index = 0; index < 4U; index++) {
				if (index == taken)
					continue;
				CHECK(drv_xhci_queue_at(&queue, position) == &items[index]);
				CHECK(drv_xhci_queue_find(&queue, &items[index]) == (int)position);
				position++;
			}
			CHECK(drv_xhci_queue_find(&queue, &items[taken]) == -1);

			/* A push after the removal lands behind the rest. */
			CHECK(drv_xhci_queue_push(&queue, &items[taken]) == 0);
			CHECK(drv_xhci_queue_at(&queue, 3) == &items[taken]);
		}
	}
}

/* Ring room counts across the Link TRB and admission keeps a free place. */
static void
test_room(void)
{
	/* An empty queue takes nothing, wherever the producer is. */
	CHECK(drv_xhci_queue_ring_used(RING_TRBS, 10, 200, 0) == 0);

	/* Plain and wrapped distances (255 usable places). */
	CHECK(drv_xhci_queue_ring_used(RING_TRBS, 10, 30, 2) == 20U);
	CHECK(drv_xhci_queue_ring_used(RING_TRBS, 250, 5, 2) == 10U);
	CHECK(drv_xhci_queue_ring_used(RING_TRBS, 0, 254, 1) == 254U);

	/* Malformed indexes look full. */
	CHECK(drv_xhci_queue_ring_used(RING_TRBS, 255, 0, 1) == 255U);

	/* Depth decides first; zero means one and the cap is the queue's. */
	CHECK(drv_xhci_queue_admit(RING_TRBS, 0, 0, 0, 3) == DRV_XHCI_QUEUE_ADMIT);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 1, 0, 3, 3) == DRV_XHCI_QUEUE_DEPTH_FULL);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 1, 1, 3, 3) == DRV_XHCI_QUEUE_DEPTH_FULL);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 3, 4, 9, 3) == DRV_XHCI_QUEUE_ADMIT);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 4, 9, 12, 3) == DRV_XHCI_QUEUE_DEPTH_FULL);

	/* Room: 255 usable, one kept free, and 32 more behind others. */
	CHECK(drv_xhci_queue_admit(RING_TRBS, 1, 4, 218, 4) == DRV_XHCI_QUEUE_ADMIT);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 1, 4, 218, 5) == DRV_XHCI_QUEUE_RING_FULL);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 1, 4, 230, 1) == DRV_XHCI_QUEUE_RING_FULL);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 0, 4, 0, 254) == DRV_XHCI_QUEUE_ADMIT);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 0, 4, 0, 255) == DRV_XHCI_QUEUE_RING_FULL);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 1, 4, 255, 1) == DRV_XHCI_QUEUE_RING_FULL);
	CHECK(drv_xhci_queue_admit(RING_TRBS, 0, 4, 0, 0) == DRV_XHCI_QUEUE_RING_FULL);
	CHECK(drv_xhci_queue_admit(2U, 0, 4, 0, 1) == DRV_XHCI_QUEUE_RING_FULL);
}

/* Events match the head, a TD behind it, a TD across the Link TRB, or nothing. */
static void
test_match(void)
{
	struct drv_xhci_queue_td tds[4];
	enum drv_xhci_queue_match match;
	unsigned position;
	unsigned offset;

	/* Four TDs: 250..252, 253..254 + 0..1 (across the Link TRB), 2..4, 5. */
	tds[0].first_trb = 250;
	tds[0].trb_count = 3;
	tds[1].first_trb = 253;
	tds[1].trb_count = 4;
	tds[2].first_trb = 2;
	tds[2].trb_count = 3;
	tds[3].first_trb = 5;
	tds[3].trb_count = 1;

	/* The head's last TRB. */
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(252), 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_HEAD);
	CHECK(position == 0 && offset == 2U);

	/* The second TD's TRB after the Link TRB is its third. */
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(0), 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_BEHIND);
	CHECK(position == 1U && offset == 2U);

	/* The last TD. */
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(5), 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_BEHIND);
	CHECK(position == 3U && offset == 0);

	/* The Link TRB, a retired TD's place, another slot or endpoint: none. */
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(255), 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_NONE);
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(249), 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_NONE);
	CHECK(position == 0 && offset == 0);
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(250), 2, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_NONE);
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(250), 3, 5, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_NONE);

	/* A misaligned pointer and an empty queue match nothing. */
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 4,
				     trb_address(250) + 8U, 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_NONE);
	match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 3, 4, tds, 0,
				     trb_address(250), 3, 4, &position, &offset);
	CHECK(match == DRV_XHCI_QUEUE_MATCH_NONE);
}

/* Halting codes, and any code once the context says Halted. */
static void
test_halts(void)
{
	/* Codes that halt by themselves. */
	CHECK(drv_xhci_queue_completion_halts(3, DRV_XHCI_ENDPOINT_RUNNING));
	CHECK(drv_xhci_queue_completion_halts(4, DRV_XHCI_ENDPOINT_RUNNING));
	CHECK(drv_xhci_queue_completion_halts(6, DRV_XHCI_ENDPOINT_RUNNING));
	CHECK(drv_xhci_queue_completion_halts(36, DRV_XHCI_ENDPOINT_RUNNING));

	/* Success, short packet, stopped and TRB Error do not, unless Halted. */
	CHECK(!drv_xhci_queue_completion_halts(1, DRV_XHCI_ENDPOINT_RUNNING));
	CHECK(!drv_xhci_queue_completion_halts(13, DRV_XHCI_ENDPOINT_RUNNING));
	CHECK(!drv_xhci_queue_completion_halts(26, DRV_XHCI_ENDPOINT_STOPPED));
	CHECK(!drv_xhci_queue_completion_halts(5, DRV_XHCI_ENDPOINT_RUNNING));
	CHECK(drv_xhci_queue_completion_halts(5, DRV_XHCI_ENDPOINT_HALTED));
	CHECK(drv_xhci_queue_completion_halts(2, DRV_XHCI_ENDPOINT_HALTED));
}

/* The three Stopped codes a suspend leaves on the ring, Stopped - Short Packet among them. */
static void
test_stopped(void)
{
	/* Stopped, Stopped - Length Invalid and Stopped - Short Packet. */
	CHECK(drv_xhci_completion_stopped(26));
	CHECK(drv_xhci_completion_stopped(27));
	CHECK(drv_xhci_completion_stopped(28));

	/* Success, short packet, stall and transaction error end the TD. */
	CHECK(!drv_xhci_completion_stopped(1));
	CHECK(!drv_xhci_completion_stopped(13));
	CHECK(!drv_xhci_completion_stopped(6));
	CHECK(!drv_xhci_completion_stopped(4));
	CHECK(!drv_xhci_completion_stopped(29));
}

/*
 * A small model of one endpoint: random submits (admission by depth and
 * ring room), completions of the head in order, the late event of a short
 * TD, a cancel that takes all, and a STALL of the head that retires the
 * TDs behind it.  The completed order must equal the submitted order.
 */
static void
test_model(void)
{
	struct drv_xhci_queue queue;
	struct drv_xhci_queue_td tds[DRV_XHCI_QUEUE_MAX];
	struct drv_xhci_queue_td records[64];
	enum drv_xhci_queue_admission admission;
	enum drv_xhci_queue_match match;
	struct drv_xhci_queue_td *record;
	unsigned enqueue = 0;
	unsigned submitted = 0;
	unsigned completed = 0;
	unsigned step;
	unsigned used;
	unsigned trbs;
	unsigned index;
	unsigned position;
	unsigned offset;
	unsigned last_retired = 0;
	unsigned retired_valid = 0;
	unsigned depth;
	unsigned seed = 12345U;
	unsigned action;

	/* Runs many random steps. */
	drv_xhci_queue_init(&queue);
	for (step = 0; step < 200000U; step++) {
		seed = seed * 1103515245U + 12345U;
		action = (seed >> 16) % 8U;
		depth = 1U + (seed >> 8) % DRV_XHCI_QUEUE_MAX;

		/* Lists where the queued TDs lie. */
		for (index = 0; index < queue.count; index++) {
			record = drv_xhci_queue_at(&queue, index);
			tds[index] = *record;
		}

		if (action < 4U) {
			/* A submit of 1 to 70 TRBs. */
			trbs = 1U + (seed >> 4) % 70U;
			used = 0;
			if (queue.count != 0)
				used = drv_xhci_queue_ring_used(RING_TRBS, tds[0].first_trb, enqueue, queue.count);
			admission = drv_xhci_queue_admit(RING_TRBS, queue.count, depth, used, trbs);
			if (admission != DRV_XHCI_QUEUE_ADMIT)
				continue;
			record = &records[submitted % 64U];
			record->first_trb = enqueue;
			record->trb_count = trbs;
			CHECK(drv_xhci_queue_push(&queue, record) == 0);
			enqueue = (enqueue + trbs) % (RING_TRBS - 1U);
			submitted++;

			/* The new TD never overlaps the head's first TRB, and behind others keeps the slack. */
			used = drv_xhci_queue_ring_used(RING_TRBS, ((struct drv_xhci_queue_td *)drv_xhci_queue_at(&queue, 0))->first_trb, enqueue, queue.count);
			CHECK(used <= RING_TRBS - 2U);
			if (queue.count > 1U)
				CHECK(used <= RING_TRBS - 2U - DRV_XHCI_QUEUE_SLACK);
		} else if (action < 6U && queue.count != 0) {
			/* The controller finishes the head: its last TRB's event. */
			match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 1, 2, tds, queue.count,
						     trb_address((tds[0].first_trb + tds[0].trb_count - 1U) % (RING_TRBS - 1U)),
						     1, 2, &position, &offset);
			CHECK(match == DRV_XHCI_QUEUE_MATCH_HEAD);
			CHECK(offset == tds[0].trb_count - 1U);
			record = drv_xhci_queue_at(&queue, 0);
			CHECK(record == &records[completed % 64U]);
			last_retired = record->first_trb;
			retired_valid = 1;
			CHECK(drv_xhci_queue_remove(&queue, record) == 1);
			completed++;
		} else if (action == 6U && retired_valid && queue.count != 0) {
			/* A late event of the TD retired last names nothing queued, unless the ring reused its place. */
			match = drv_xhci_queue_match(RING_BASE, RING_TRBS, 1, 2, tds, queue.count,
						     trb_address(last_retired), 1, 2, &position, &offset);
			CHECK(match != DRV_XHCI_QUEUE_MATCH_HEAD || tds[0].first_trb == last_retired);
		} else if (action == 7U && queue.count != 0) {
			/* A cancel or a halt of the head retires everything, in order. */
			while (queue.count != 0) {
				record = drv_xhci_queue_at(&queue, 0);
				CHECK(record == &records[completed % 64U]);
				CHECK(drv_xhci_queue_remove(&queue, record) == 1);
				completed++;
			}

			/* Set TR Dequeue puts the controller at the producer. */
			retired_valid = 0;
		}
	}

	/* Everything submitted is accounted for. */
	CHECK(completed + queue.count == submitted);
	CHECK(submitted > 10000U);
}

/* Runs every case. */
int
main(void)
{
	/* Each part in turn. */
	test_fifo();
	test_remove();
	test_room();
	test_match();
	test_halts();
	test_stopped();
	test_model();

	/* Reports the result. */
	if (failures != 0) {
		printf("xhci-queue: %u FAILED\n", failures);
		return EXIT_FAILURE;
	}
	printf("xhci-queue: PASS\n");
	return EXIT_SUCCESS;
}
