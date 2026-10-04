/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The decisions around DC9 (see dc9.h).
 */

#include <uapi/errno.h>

#include "dc9.h"
#include "internal.h"

/*
 * Decides whether the display may enter DC9.
 *
 * dc_state is DC_STATE_EN as read; pw2_enabled says power well 2 is on;
 * irqs_enabled says the device's interrupts are still enabled.  It reports
 * EALREADY when DC9 is already set, EBUSY when DC5 or DC6 is still
 * enabled or power well 2 is on, and EINVAL while the interrupts are
 * enabled.
 */
int
drv_i915_dc9_enter_check(
	uint32_t dc_state,
	int pw2_enabled,
	int irqs_enabled)
{
	/* Refuses a DC9 that is set already. */
	if ((dc_state & I915_DC_STATE_EN_DC9) != 0U)
		return EALREADY;

	/* Refuses while the DMC may still take DC5 or DC6. */
	if ((dc_state & (I915_DC_STATE_EN_UPTO_DC5 | I915_DC_STATE_EN_UPTO_DC6)) != 0U)
		return EBUSY;

	/* Refuses while power well 2 is on. */
	if (pw2_enabled)
		return EBUSY;

	/* Refuses while the interrupts are enabled. */
	if (irqs_enabled)
		return EINVAL;

	/* Succeeded: the display may enter DC9. */
	return 0;
}

/*
 * Decides whether the display may leave DC9.
 *
 * It reports EBUSY when power well 2 is on or DC5 or DC6 is enabled,
 * which DC9 cannot have left behind.
 */
int
drv_i915_dc9_leave_check(
	uint32_t dc_state,
	int pw2_enabled)
{
	/* Refuses while power well 2 is on. */
	if (pw2_enabled)
		return EBUSY;

	/* Refuses DC5 or DC6 enabled alongside DC9. */
	if ((dc_state & (I915_DC_STATE_EN_UPTO_DC5 | I915_DC_STATE_EN_UPTO_DC6)) != 0U)
		return EBUSY;

	/* Succeeded: the display may leave DC9. */
	return 0;
}

/*
 * Reports SOUTH_CHICKEN1 with the reference-clock bit for DC9: set when
 * entering, cleared when leaving; the other bits are kept.
 */
uint32_t
drv_i915_dc9_south_chicken(
	uint32_t value,
	int entering)
{
	/* Clears the bit, and sets it again when entering. */
	value &= ~I915_SOUTH_CHICKEN1_SBCLK_RUN_REFCLK_DIS;
	if (entering)
		value |= I915_SOUTH_CHICKEN1_SBCLK_RUN_REFCLK_DIS;

	/* Succeeded: reports the register's new value. */
	return value;
}
