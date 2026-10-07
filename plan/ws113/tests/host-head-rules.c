/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws113-p011: the host test of the second output's claim rules
 * (src/drivers/gpu/i915/display/head-rules.c, compiled unchanged): a claim
 * with no lease held moves the resident output, unless a head is still
 * claimed; with the resident panel's
 * lease held an HDMI or an external DP connector becomes the head; a
 * second head, a broken earlier head, a connector latched limited, a
 * resident output that is not the panel and a head of another kind are
 * the limit; the head's pipe may not be the resident output's.
 *
 *   sh plan/ws113/tests/host-head-rules.sh
 */

#include "head-rules.h"

#include <stdio.h>
#include <string.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);
static void facts_lit(struct i915_head_claim_facts *facts);
static enum i915_head_claim_way decide(const struct i915_head_claim_facts *facts, int *said);

/* Runs every case. */
int
main(void)
{
	struct i915_head_claim_facts facts;
	enum i915_head_claim_way way;
	int said;

	/* 1. No lease held: the resident output moves (ws113-p011a), whatever its kind. */
	facts_lit(&facts);
	facts.resident_leased = 0;
	facts.resident_kind = I915_HEAD_KIND_HDMI;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_MOVE, "no lease: a move");
	check(!said, "no lease: no reason");

	/* 1b. No resident lease but a head still claimed: no move (review F4), the limit. */
	facts_lit(&facts);
	facts.resident_leased = 0;
	facts.head_claimed = 1;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "no resident lease, head claimed: the limit");
	check(said, "no resident lease, head claimed: a reason");

	/* 2. The panel's lease held, an HDMI connector: the head. */
	facts_lit(&facts);
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_HEAD, "panel + HDMI: the head");
	check(!said, "panel + HDMI: no reason");

	/* 3. An external DP connector: the head too. */
	facts_lit(&facts);
	facts.kind = I915_HEAD_KIND_DP_EXT;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_HEAD, "panel + DP: the head");

	/* 4. A head claimed already: the limit. */
	facts_lit(&facts);
	facts.head_claimed = 1;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "second head: the limit");
	check(said, "second head: a reason");

	/* 5. A broken earlier head: the limit. */
	facts_lit(&facts);
	facts.head_broken = 1;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "broken head: the limit");

	/* 6. The connector latched limited: the limit. */
	facts_lit(&facts);
	facts.limited = 1;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "latched: the limit");

	/* 7. A resident output that is not the panel: the limit. */
	facts_lit(&facts);
	facts.resident_kind = I915_HEAD_KIND_HDMI;
	facts.kind = I915_HEAD_KIND_PANEL;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "HDMI resident + panel: the limit");

	/* 8. A head of another kind beside the panel: the limit. */
	facts_lit(&facts);
	facts.kind = I915_HEAD_KIND_OTHER;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "other kind: the limit");
	facts_lit(&facts);
	facts.kind = I915_HEAD_KIND_PANEL;
	way = decide(&facts, &said);
	check(way == I915_HEAD_CLAIM_LIMIT, "a second panel: the limit");

	/* 9. The pipes: the same one conflicts, another does not. */
	check(drv_i915_head_pipes_conflict(1U, 1U) == 1, "pipe B and pipe B conflict");
	check(drv_i915_head_pipes_conflict(0U, 1U) == 0, "pipe A and pipe B do not conflict");

	/* The result. */
	if (failures != 0) {
		printf("host-head-rules: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-head-rules: ok (%d checks)\n", checks);
	return 0;
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Fills the facts of the usual case: the panel's lease held, an HDMI connector, nothing in the way. */
static void
facts_lit(
	struct i915_head_claim_facts *facts)
{
	/* Nothing else holds. */
	memset(facts, 0, sizeof(*facts));
	facts->resident_leased = 1;
	facts->resident_kind = I915_HEAD_KIND_PANEL;
	facts->kind = I915_HEAD_KIND_HDMI;
}

/* Decides a claim and tells whether a reason was given. */
static enum i915_head_claim_way
decide(
	const struct i915_head_claim_facts *facts,
	int *said)
{
	enum i915_head_claim_way way;
	const char *reason;

	/* The rule. */
	reason = NULL;
	way = drv_i915_head_claim_way(facts, &reason);

	/* A refusal says why; a reason is never missing. */
	*said = 0;
	if (reason != NULL && reason[0] != '\0')
		*said = 1;

	/* Succeeded: the way. */
	return way;
}
