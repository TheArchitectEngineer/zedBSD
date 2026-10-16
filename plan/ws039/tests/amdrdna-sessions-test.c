/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's address spaces (stage A5s): the pool
 * of hub contexts (a space keeps its context, drops its translations when
 * its tables changed or the last drop went on another ring not yet past
 * it, takes the least recently used context whose other rings are done,
 * waits on its own ring only, waits for the oldest context when none is
 * free, never hands out a held one), the address-space identifiers, and
 * the ring commands a run puts before its indirect buffer (their words
 * against the ones Linux's builders write).
 *
 * Each case prints "PASS name" or "FAIL name: why"; the exit status is 1
 * when any case failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void request_for(struct amdrdna_context_request *request, uint32_t owner, unsigned held, unsigned ring);
static void test_keep(void);
static void test_lru(void);
static void test_busy(void);
static void test_held(void);
static void test_release(void);
static void test_ids(void);
static void test_packets(void);

int
main(void)
{
	/* Runs the cases. */
	test_keep();
	test_lru();
	test_busy();
	test_held();
	test_release();
	test_ids();
	test_packets();

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-sessions-test: %d case(s)\n", failures);
		return 1;
	}

	/* Every case passed. */
	printf("PASS amdrdna-sessions-test\n");
	return 0;
}

/* Records one case. */
static void
check(
	int condition,
	const char *name,
	const char *why)
{
	/* A passed case says so. */
	if (condition) {
		printf("PASS %s\n", name);
		return;
	}

	/* A failed case says why. */
	printf("FAIL %s: %s\n", name, why);
	failures++;
}

/* Fills a request of a space whose table word is its identifier times 0x1000. */
static void
request_for(
	struct amdrdna_context_request *request,
	uint32_t owner,
	unsigned held,
	unsigned ring)
{
	/* The space's identifier, its context, its table, its first generation and the ring. */
	request->owner = owner;
	request->held = held;
	request->table = (uint64_t)owner * 0x1000U + 1U;
	request->generation = 1U;
	request->ring = ring;
}

/* A space keeps its context; only a change of its tables or another ring's pending drop drops it again. */
static void
test_keep(void)
{
	struct amdrdna_context_pool pool;
	struct amdrdna_context_request request;
	struct amdrdna_context_grant grant;
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	unsigned context;
	int error;

	/* The first run takes a context and drops it. */
	memset(passed, 0, sizeof(passed));
	amdrdna_context_pool_init(&pool);
	request_for(&request, 7U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context >= 1U && grant.context <= 15U && grant.flush && !grant.wait, "keep.first", "no context with a drop");
	context = grant.context;
	amdrdna_context_submitted(&pool, context, &request, 1U, grant.flush);

	/* The second run keeps it without a drop, its first run still on the ring. */
	request.held = context;
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context == context && !grant.flush && !grant.sync, "keep.again", "the context was not kept quietly");
	amdrdna_context_submitted(&pool, context, &request, 2U, grant.flush);

	/* Changed tables drop it. */
	request.generation = 2U;
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context == context && grant.flush, "keep.generation", "a new generation was not dropped");

	/* A drop put on compute queue 0, not yet passed, makes a graphics run drop again. */
	request.ring = AMDRDNA_TRACKED_COMPUTE;
	amdrdna_context_submitted(&pool, context, &request, 5U, true);
	request.ring = AMDRDNA_TRACKED_GRAPHICS;
	passed[AMDRDNA_TRACKED_COMPUTE] = 4U;
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.flush, "keep.other-ring", "another ring's pending drop was trusted");

	/* Once that ring passed the drop, it counts. */
	passed[AMDRDNA_TRACKED_COMPUTE] = 5U;
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && !grant.flush, "keep.other-ring-passed", "a passed drop was not trusted");
	check(pool.flushes == 2U, "keep.count", "the drops were not counted");
}

/* Sixteen spaces share fifteen contexts: the sixteenth takes the least recently used. */
static void
test_lru(void)
{
	struct amdrdna_context_pool pool;
	struct amdrdna_context_request request;
	struct amdrdna_context_grant grant;
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	unsigned held[17];
	unsigned owner;
	unsigned seen;
	int error;
	int distinct;

	/* Spaces 1 to 15 each run once; every run has ended. */
	memset(passed, 0, sizeof(passed));
	amdrdna_context_pool_init(&pool);
	distinct = 1;
	seen = 0U;
	for (owner = 1U; owner <= 15U; owner++) {
		request_for(&request, owner, 0U, AMDRDNA_TRACKED_GRAPHICS);
		error = amdrdna_context_grant(&pool, &request, passed, &grant);
		if (error != 0 || grant.wait || (seen & (1U << grant.context)) != 0U)
			distinct = 0;
		seen |= 1U << grant.context;
		held[owner] = grant.context;
		amdrdna_context_submitted(&pool, grant.context, &request, owner, true);
	}

	/* Every run has ended; the fifteen contexts are the fifteen spaces'. */
	passed[AMDRDNA_TRACKED_GRAPHICS] = 15U;
	check(distinct && seen == 0xfffeU, "lru.fifteen", "the fifteen spaces did not get the fifteen contexts");

	/* Space 16 takes space 1's context, the least recently used. */
	request_for(&request, 16U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context == held[1] && grant.flush && pool.takeovers == 1U, "lru.sixteenth", "the oldest context was not taken");
	amdrdna_context_submitted(&pool, grant.context, &request, 16U, true);
	held[16] = grant.context;
	passed[AMDRDNA_TRACKED_GRAPHICS] = 16U;

	/* Space 1 comes back: its context is gone, it takes space 2's. */
	request_for(&request, 1U, held[1], AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context == held[2] && grant.flush && pool.takeovers == 2U, "lru.back", "the returning space did not take the next oldest");
	amdrdna_context_submitted(&pool, grant.context, &request, 17U, true);

	/* And keeps it on its next run. */
	request.held = grant.context;
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context == held[2] && !grant.flush, "lru.kept", "the context was not kept");
	check(pool.flushes == 17U, "lru.flushes", "seventeen drops were not counted");
}

/* A context still running on another ring is not taken; on the run's own ring it only makes the run wait. */
static void
test_busy(void)
{
	struct amdrdna_context_pool pool;
	struct amdrdna_context_request request;
	struct amdrdna_context_grant grant;
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	unsigned owner;
	int error;

	/* Fifteen spaces run on compute queue 1; none has ended. */
	memset(passed, 0, sizeof(passed));
	amdrdna_context_pool_init(&pool);
	for (owner = 1U; owner <= 15U; owner++) {
		request_for(&request, owner, 0U, AMDRDNA_TRACKED_COMPUTE + 1U);
		(void)amdrdna_context_grant(&pool, &request, passed, &grant);
		amdrdna_context_submitted(&pool, grant.context, &request, owner, true);
	}

	/* A graphics run of a new space must wait for the oldest one's compute work. */
	request_for(&request, 20U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.wait && grant.wait_ring == AMDRDNA_TRACKED_COMPUTE + 1U && grant.wait_sequence == 1U, "busy.wait", "the oldest context's fence was not named");

	/* A run of a new space on that compute queue takes the oldest and waits on its own ring. */
	request_for(&request, 21U, 0U, AMDRDNA_TRACKED_COMPUTE + 1U);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && !grant.wait && grant.sync && grant.flush, "busy.sync", "the same ring's work did not make the run sync");

	/* Once the first run ended, the graphics run takes its context without a sync. */
	passed[AMDRDNA_TRACKED_COMPUTE + 1U] = 1U;
	request_for(&request, 20U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && !grant.wait && !grant.sync && grant.flush, "busy.ended", "an ended context was not taken");
}

/* A held context is neither granted nor waited for; with all held there is none. */
static void
test_held(void)
{
	struct amdrdna_context_pool pool;
	struct amdrdna_context_request request;
	struct amdrdna_context_grant grant;
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	unsigned context;
	int error;

	/* Every context but 9 held. */
	memset(passed, 0, sizeof(passed));
	amdrdna_context_pool_init(&pool);
	for (context = 1U; context < AMDRDNA_CONTEXTS; context++) {
		if (context != 9U)
			amdrdna_context_hold(&pool, context);
	}

	/* One context is left, and it is granted. */
	check(amdrdna_context_free_count(&pool) == 1U, "held.count", "the free contexts were not counted");
	request_for(&request, 3U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.context == 9U, "held.only", "the one free context was not granted");

	/* With 9 held too, nothing can be had. */
	amdrdna_context_hold(&pool, 9U);
	request_for(&request, 4U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == ENOSPC, "held.none", "a held context was granted");
}

/* A released context is nobody's: a new space with the same identifier does not keep it, and it waits for its work. */
static void
test_release(void)
{
	struct amdrdna_context_pool pool;
	struct amdrdna_context_request request;
	struct amdrdna_context_grant grant;
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	unsigned context;
	bool idle;
	int error;

	/* Space 5 runs on the graphics ring. */
	memset(passed, 0, sizeof(passed));
	amdrdna_context_pool_init(&pool);
	request_for(&request, 5U, 0U, AMDRDNA_TRACKED_GRAPHICS);
	(void)amdrdna_context_grant(&pool, &request, passed, &grant);
	context = grant.context;
	amdrdna_context_submitted(&pool, context, &request, 3U, true);
	idle = amdrdna_context_idle(&pool, context, passed);
	check(!idle, "release.busy", "running work was not seen");
	passed[AMDRDNA_TRACKED_GRAPHICS] = 3U;
	idle = amdrdna_context_idle(&pool, context, passed);
	check(idle, "release.idle", "ended work was seen as running");

	/* Released, a space reusing the identifier and the context gets a drop. */
	amdrdna_context_release(&pool, context, 5U);
	request_for(&request, 5U, context, AMDRDNA_TRACKED_GRAPHICS);
	error = amdrdna_context_grant(&pool, &request, passed, &grant);
	check(error == 0 && grant.flush, "release.reuse", "a released context was kept without a drop");

	/* A release by another owner changes nothing. */
	amdrdna_context_submitted(&pool, grant.context, &request, 4U, true);
	amdrdna_context_release(&pool, grant.context, 6U);
	check(pool.slots[grant.context].owner == 5U, "release.foreign", "another space released the context");

	/* The fence comparison handles the wrap. */
	check(amdrdna_fence_passed(2U, 0xfffffffeU) && !amdrdna_fence_passed(0xfffffffeU, 2U), "release.wrap", "the wrap was not handled");
}

/* The identifiers: 1 first, never 0, not reused at once, all taken gives ENOSPC. */
static void
test_ids(void)
{
	struct amdrdna_space_ids *ids;
	uint32_t id;
	uint32_t first;
	uint32_t count;
	int error;

	/* The set is large; it lives on the heap. */
	ids = malloc(sizeof(*ids));
	if (ids == NULL) {
		check(0, "ids.alloc", "no memory");
		return;
	}

	/* The first is 1, the second 2. */
	amdrdna_space_ids_init(ids);
	error = amdrdna_space_id_take(ids, &first);
	check(error == 0 && first == 1U, "ids.first", "the first identifier is not 1");
	error = amdrdna_space_id_take(ids, &id);
	check(error == 0 && id == 2U, "ids.second", "the second identifier is not 2");

	/* A given-back identifier is not taken again at once. */
	amdrdna_space_id_give(ids, first);
	error = amdrdna_space_id_take(ids, &id);
	check(error == 0 && id == 3U && ids->count == 2U, "ids.no-reuse", "a freed identifier came back at once");

	/* Every other identifier, then none. */
	for (count = ids->count; count < AMDRDNA_SPACE_IDS - 1U; count++) {
		error = amdrdna_space_id_take(ids, &id);
		if (error != 0 || id == 0U)
			break;
	}

	/* All are taken; one more is refused. */
	check(ids->count == AMDRDNA_SPACE_IDS - 1U, "ids.all", "not every identifier could be had");
	error = amdrdna_space_id_take(ids, &id);
	check(error == ENOSPC, "ids.full", "an identifier was had from a full set");

	/* Giving 0 or a free one back changes nothing. */
	amdrdna_space_id_give(ids, 0U);
	check(ids->count == AMDRDNA_SPACE_IDS - 1U, "ids.zero", "giving 0 back changed the count");
	free(ids);
}

/*
 * The commands before an indirect buffer, against the words Linux v6.19's
 * builders write (gfx_v10_0.c: gfx_v10_0_ring_emit_wreg on the graphics
 * ring, gfx_v10_0_ring_emit_reg_write_reg_wait,
 * gfx_v10_0_ring_emit_pipeline_sync, gfx_v10_0_ring_emit_vm_flush; nvd.h:
 * PACKET3, WRITE_DATA_ENGINE_SEL, WR_CONFIRM, the WAIT_REG_MEM fields),
 * worked out by hand.
 */
static void
test_packets(void)
{
	uint32_t words[8];
	unsigned count;

	/* A register write by the fetcher: header 0xc0033700, control 0x40100000. */
	count = amdrdna_pm4_fetcher_write_register(words, 0x168bU, 0x12345001U);
	check(count == 5U && words[0] == 0xc0033700U && words[1] == 0x40100000U && words[2] == 0x168bU && words[3] == 0U && words[4] == 0x12345001U,
	    "packets.fetcher-write", "the words differ");

	/* A write then wait on the fetcher: header 0xc0053c00, control 0x143, poll 0x20. */
	count = amdrdna_pm4_write_and_wait(words, 0x1643U, 0x1655U, 0x00f80008U, 0x8U, true);
	check(count == 7U && words[0] == 0xc0053c00U && words[1] == 0x00000143U && words[2] == 0x1643U && words[3] == 0x1655U &&
	    words[4] == 0x00f80008U && words[5] == 0x8U && words[6] == 0x20U,
	    "packets.write-wait", "the words differ");

	/* The same on a compute queue: the micro engine (control 0x43). */
	count = amdrdna_pm4_write_and_wait(words, 0x1647U, 0x1659U, 0x00f80004U, 0x4U, false);
	check(count == 7U && words[1] == 0x00000043U, "packets.write-wait-compute", "the control word differs");

	/* The ring's own fence waited for: control 0x113 on the fetcher, address, sequence, every bit, poll 4. */
	count = amdrdna_pm4_wait_memory(words, 0x0000800000001010ULL, 77U, true);
	check(count == 7U && words[0] == 0xc0053c00U && words[1] == 0x00000113U && words[2] == 0x00001010U && words[3] == 0x8000U &&
	    words[4] == 77U && words[5] == 0xffffffffU && words[6] == 4U,
	    "packets.pipeline-sync", "the words differ");

	/* The fetcher's sync: header 0xc0004200 and a zero word. */
	count = amdrdna_pm4_fetcher_sync(words);
	check(count == 2U && words[0] == 0xc0004200U && words[1] == 0U, "packets.fetcher-sync", "the words differ");

	/* A move through the level-2 cache: CP_SYNC, both selects 3 (control 0xe0300000), 8 bytes. */
	count = amdrdna_pm4_move_cached(words, 0x100003000ULL, 0x100008000ULL, 8U);
	check(count == 7U && words[0] == 0xc0055000U && words[1] == 0xe0300000U && words[2] == 0x3000U && words[3] == 1U &&
	    words[4] == 0x8000U && words[5] == 1U && words[6] == 8U,
	    "packets.move-cached", "the words differ");

	/* An indirect buffer of context 3 on the graphics ring: size and context, no valid bit. */
	count = amdrdna_pm4_indirect(words, 0x0000000100000000ULL, 64U, 3U, false);
	check(count == 4U && words[1] == 0U && words[2] == 1U && words[3] == 0x03000040U, "packets.indirect", "the words differ");
}
