/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rules of the second output, the head (head-rules.c, ws113-p011):
 * whether a claim of a connector that is not the resident output may make
 * it the head.  Pure: it reads only the facts the caller gathered, so the
 * host test compiles it unchanged.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_HEAD_RULES_H
#define DRIVERS_GPU_I915_DISPLAY_HEAD_RULES_H

/* What a head's claim is decided on, as the caller found it. */
enum i915_head_kind {
	I915_HEAD_KIND_PANEL = 0,
	I915_HEAD_KIND_HDMI,
	I915_HEAD_KIND_DP_EXT,
	I915_HEAD_KIND_OTHER
};

/*
 * The facts of one claim of a connector that is not the resident output,
 * gathered by the caller under the locks that keep them still.
 *
 * It lives on the caller's stack for the one decision.
 */
struct i915_head_claim_facts {
	/* Nonzero while a session holds the resident output's lease. */
	int resident_leased;

	/* The resident output's kind. */
	enum i915_head_kind resident_kind;

	/* Nonzero while a head is claimed (by any lease). */
	int head_claimed;

	/* Nonzero once a head's stop could not be confirmed: no head is lit again. */
	int head_broken;

	/* Nonzero while this connector is latched limited for the current topology. */
	int limited;

	/* The claimed connector's kind. */
	enum i915_head_kind kind;
};

/*
 * What a head's claim comes to: a move of the resident output (no lease
 * held, ws113-p011a), the head, or a refusal for the limit of the outputs
 * shown at once.
 */
enum i915_head_claim_way {
	I915_HEAD_CLAIM_MOVE = 0,
	I915_HEAD_CLAIM_HEAD,
	I915_HEAD_CLAIM_LIMIT
};

enum i915_head_claim_way drv_i915_head_claim_way(const struct i915_head_claim_facts *facts, const char **reason);
int drv_i915_head_pipes_conflict(unsigned resident_pipe, unsigned head_pipe);

#endif
