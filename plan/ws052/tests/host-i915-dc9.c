/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the DC9 decisions (ws052-p009, display/dc9.c): entering
 * needs DC9, DC5 and DC6 off, power well 2 off and the interrupts off;
 * leaving needs power well 2 off and DC5 and DC6 off; the PCH bit is set
 * on the way in and cleared on the way out, the other bits kept.
 *
 *   make -C plan/ws052/tests i915-dc9 && build/ws052/host/i915-dc9
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <uapi/errno.h>

#include "drivers/gpu/i915/display/dc9.h"

/* The DC_STATE_EN bits the checks read. */
#define DC5	0x00000001U
#define DC6	0x00000002U
#define DC9	0x00000008U
#define DC3CO	0x40000000U

static void check(bool condition, const char *what);

/* How many checks failed. */
static unsigned failures;

/*
 * Runs the checks and reports how many failed.
 */
int
main(void)
{
	uint32_t value;

	/* Entering. */
	check(drv_i915_dc9_enter_check(0U, 0, 0) == 0, "enter: allowed from no DC state");
	check(drv_i915_dc9_enter_check(DC3CO, 0, 0) == 0, "enter: other bits do not matter");
	check(drv_i915_dc9_enter_check(DC9, 0, 0) == EALREADY, "enter: DC9 already set");
	check(drv_i915_dc9_enter_check(DC5, 0, 0) == EBUSY, "enter: DC5 still enabled");
	check(drv_i915_dc9_enter_check(DC6, 0, 0) == EBUSY, "enter: DC6 still enabled");
	check(drv_i915_dc9_enter_check(0U, 1, 0) == EBUSY, "enter: power well 2 on");
	check(drv_i915_dc9_enter_check(0U, 0, 1) == EINVAL, "enter: interrupts on");

	/* Leaving. */
	check(drv_i915_dc9_leave_check(DC9, 0) == 0, "leave: allowed from DC9");
	check(drv_i915_dc9_leave_check(DC9, 1) == EBUSY, "leave: power well 2 on");
	check(drv_i915_dc9_leave_check(DC9 | DC5, 0) == EBUSY, "leave: DC5 alongside DC9");

	/* The PCH bit. */
	value = drv_i915_dc9_south_chicken(0x00010001U, 1);
	check(value == (0x00010001U | I915_SOUTH_CHICKEN1_SBCLK_RUN_REFCLK_DIS), "chicken: set on the way in, others kept");
	value = drv_i915_dc9_south_chicken(value, 0);
	check(value == 0x00010001U, "chicken: cleared on the way out, others kept");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("i915-dc9: %u checks failed\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("i915-dc9: every check passed\n");
	return 0;
}

/* Counts a check that failed and names it. */
static void
check(
	bool condition,
	const char *what)
{
	/* A check that held says nothing. */
	if (condition)
		return;

	/* Names the check. */
	printf("FAIL %s\n", what);
	failures++;
}
