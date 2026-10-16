/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the xd register headers against the tree's older
 * register headers (intel/mreg.h, trans.h, plane.h, dkl.h, hotplug.h):
 * every pair of xd-regs-map.h whose Linux name an older header still
 * defines must have the same value.
 *
 * The older headers go when the display 12 and 13 code they serve is
 * removed; this test goes with them, and the pair list then stays
 * checked against the Linux tree itself
 * (plan/ws245/tests/check-xd-regs-linux.sh).
 */

#include "host-test.h"
#include "../../display/internal.h"

#include <stdint.h>

/*
 * The block helpers the older headers expect from the display code that
 * includes them (the headers define the pipe, port and pin enumerations
 * themselves): on display 12 and 13 the pipe and transcoder blocks are
 * 4 KiB apart from 0x70000 and 0x60000.
 */
#define _MMIO_TRANS2(trans, offset) _MMIO(0x1000U * (u32)(trans) + (offset))
#define _MMIO_PIPE2(pipe, offset) _MMIO(0x1000U * (u32)(pipe) + (offset))
#define _PICK(index, ...) (((const u32 []){ __VA_ARGS__ })[index])
#define _PLANE(plane, a, b) _PICK_EVEN(plane, a, b)
#define _PORT(port, a, b) _PICK_EVEN(port, a, b)
#define _MMIO_PLANE(plane, a, b) _MMIO(_PLANE(plane, a, b))
#define _MMIO_TRANS(trans, a, b) _MMIO(_PICK_EVEN(trans, a, b))
typedef uint32_t __u32;

/*
 * The pipes, which the transcoder enumeration of trans.h is built from;
 * the display code defines them in its own environment.
 */
enum i915_xd_regs_test_pipe {
	PIPE_A = 0,
	PIPE_B,
	PIPE_C,
	PIPE_D
};

#include "../../intel/mreg.h"
#include "../../intel/trans.h"
#include "../../intel/plane.h"
#include "../../intel/dkl.h"
#include "../../intel/hotplug.h"
/* The DPCD header marks its packet structures packed, a name the display environment defines. */
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#include "../../intel/dp.h"

#include "../../intel/display-regs-xd.h"
#include "../../intel/display-regs-xd-south.h"
#include "../../intel/display-dpcd-xd.h"

/* The pairs compared, for the report. */
static unsigned i915_xd_regs_compared;

static void i915_xd_regs_compare(uint32_t xd, uint32_t older, const char *xd_text);

/*
 * Compares every pair the older headers can answer and reports.
 */
int
main(void)
{
	int failures;

#define I915_XD_MAP_REG(xd, linux) i915_xd_regs_compare((uint32_t)(xd), i915_mmio_reg_offset(linux), #xd);
#define I915_XD_MAP_VAL(xd, linux) i915_xd_regs_compare((uint32_t)(xd), (uint32_t)(linux), #xd);
#include "xd-regs-map.h"

	/* At least the pipe, transcoder and plane registers must have been compared. */
	i915_host_check(i915_xd_regs_compared >= 100U, "regs: the older headers answered at least 100 pairs");

	/* Reports the result. */
	failures = i915_host_report("host_xd_regs_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every pair the older headers know is equal. */
	return 0;
}

/* Compares one pair. */
static void
i915_xd_regs_compare(
	uint32_t xd,
	uint32_t older,
	const char *xd_text)
{
	/* Counts the pair and checks it. */
	i915_xd_regs_compared++;
	i915_host_check(xd == older, xd_text);
}
