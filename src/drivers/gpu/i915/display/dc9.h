/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The decisions around DC9 (ws052-p009): whether the display may enter or
 * leave it, and the PCH clock-gating bit that goes with it.
 *
 * DC9 is the deepest display state, which S0 idle needs: the display core
 * is off and the DMC's program is lost.  The checks are the ones the
 * reference asserts before it writes DC_STATE_EN (assert_can_enable_dc9()
 * and assert_can_disable_dc9()); here a failed check refuses the
 * transition.  The functions only decide; power.c reads the registers and
 * writes them, so the decisions are tested on the host
 * (plan/ws052/tests/host-i915-dc9.c).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DC9_H
#define DRIVERS_GPU_I915_DISPLAY_DC9_H

#include <stdint.h>

/* SBCLK_RUN_REFCLK_DIS of SOUTH_CHICKEN1: the PCH stops the display's reference clock in S0 idle (Wa_14010685332). */
#define I915_SOUTH_CHICKEN1_SBCLK_RUN_REFCLK_DIS	0x00000080U

int
drv_i915_dc9_enter_check(
	uint32_t dc_state,
	int pw2_enabled,
	int irqs_enabled);

int
drv_i915_dc9_leave_check(
	uint32_t dc_state,
	int pw2_enabled);

uint32_t
drv_i915_dc9_south_chicken(
	uint32_t value,
	int entering);

#endif
