/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dmc.c
 * (sha256 58f778803f7b9dbff6a494183cc8c744e94096c31cd6a5aaa0790c301c42641d) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: is_valid_dmc_id, intel_dmc_enable_pipe, intel_dmc_disable_pipe;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_mreg_dmc.h, lcd_mreg_dmc_c.h;
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_mreg_dmc.h"
#include "lcd_mreg_dmc_c.h"

enum intel_dmc_id {
	DMC_FW_MAIN = 0,
	DMC_FW_PIPEA,
	DMC_FW_PIPEB,
	DMC_FW_PIPEC,
	DMC_FW_PIPED,
	DMC_FW_MAX
};

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool is_valid_dmc_id(enum intel_dmc_id dmc_id);

static bool is_valid_dmc_id(enum intel_dmc_id dmc_id)
{
	return dmc_id >= DMC_FW_MAIN && dmc_id < DMC_FW_MAX;
}

void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe)
{
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(i915, dmc_id))
		return;

	if (DISPLAY_VER(i915) >= 14)
		intel_de_rmw(i915, MTL_PIPEDMC_CONTROL, 0, PIPEDMC_ENABLE_MTL(pipe));
	else
		intel_de_rmw(i915, PIPEDMC_CONTROL(pipe), 0, PIPEDMC_ENABLE);
}

void intel_dmc_disable_pipe(struct drm_i915_private *i915, enum pipe pipe)
{
	enum intel_dmc_id dmc_id = PIPE_TO_DMC_ID(pipe);

	if (!is_valid_dmc_id(dmc_id) || !has_dmc_id_fw(i915, dmc_id))
		return;

	if (DISPLAY_VER(i915) >= 14)
		intel_de_rmw(i915, MTL_PIPEDMC_CONTROL, PIPEDMC_ENABLE_MTL(pipe), 0);
	else
		intel_de_rmw(i915, PIPEDMC_CONTROL(pipe), PIPEDMC_ENABLE, 0);
}

