/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rules of the second output, the head (ws113-p011, the design is
 * plan/ws113/phase011/design.md).
 *
 * A claim of a connected connector that is not the resident output moves
 * the resident output to it while no lease is held (ws113-p011a).  While
 * the resident output's lease is held, the connector becomes the head: the
 * second output, lit on the head's first frame.  This driver lights one
 * head, an HDMI or an external DisplayPort display on pipe B beside the
 * built-in panel on pipe A; any other claim is refused as the limit of the
 * outputs shown at once (D-LIMIT, the 2026-10-05 user decision: told as a
 * limit, not as a failure).
 */

#include "head-rules.h"

#include <stddef.h>

/*
 * Decides what a claim of a connector that is not the resident output
 * comes to, with the reason of a refusal for the log (an empty string
 * otherwise).
 */
enum i915_head_claim_way
drv_i915_head_claim_way(
	const struct i915_head_claim_facts *facts,
	const char **reason)
{
	/* Nothing to say yet. */
	*reason = "";

	/* No lease held: the resident output moves to the connector (ws113-p011a). */
	if (!facts->resident_leased)
		return I915_HEAD_CLAIM_MOVE;

	/* One head at a time. */
	if (facts->head_claimed) {
		*reason = "a second output is shown already";
		return I915_HEAD_CLAIM_LIMIT;
	}

	/* A head whose stop was not confirmed may still be read by the display: none is lit again. */
	if (facts->head_broken) {
		*reason = "an earlier second output was not stopped cleanly";
		return I915_HEAD_CLAIM_LIMIT;
	}

	/* A connector that could not be lit stays limited until the topology changes. */
	if (facts->limited) {
		*reason = "the connector could not be lit beside the resident output (until the next hotplug)";
		return I915_HEAD_CLAIM_LIMIT;
	}

	/* The head is lit beside the built-in panel only. */
	if (facts->resident_kind != I915_HEAD_KIND_PANEL) {
		*reason = "a second output is lit beside the built-in panel only";
		return I915_HEAD_CLAIM_LIMIT;
	}

	/* The head is an HDMI or an external DisplayPort display. */
	if (facts->kind != I915_HEAD_KIND_HDMI && facts->kind != I915_HEAD_KIND_DP_EXT) {
		*reason = "a second output of this kind is not lit";
		return I915_HEAD_CLAIM_LIMIT;
	}

	/* Succeeded: the connector becomes the head. */
	return I915_HEAD_CLAIM_HEAD;
}

/*
 * Tells whether a head's pipe is the resident output's, which the two
 * cannot share.
 */
int
drv_i915_head_pipes_conflict(
	unsigned resident_pipe,
	unsigned head_pipe)
{
	/* One pipe drives one output. */
	if (resident_pipe == head_pipe)
		return 1;

	/* Succeeded: the pipes differ. */
	return 0;
}
