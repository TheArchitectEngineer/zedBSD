/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_color.c),
 * which carries the following notice.
 *
 * Copyright © 2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * The pipe colour management of the one-screen path (see color.h).
 *
 * The functions follow the Linux 6.8.12 intel_color.c text for display
 * version 12 and later (the tgl_color_funcs hooks: icl_color_commit_noarm,
 * icl_color_commit_arm and icl_load_luts), and the part of its colour check
 * (icl_color_check) that computes the two mode words.  No LUT and no CTM
 * exist in this path, so the LUT and CSC loaders are reached only through
 * the guards of modeset-internal.h, which report a reached body as an
 * error instead of passing over it.
 */

#include "modeset-internal.h"
#include "takeover-internal.h"
#include "../intel/mreg.h"
#include "color.h"

static bool i915_lut_is_legacy(const struct drm_property_blob *lut);
static u32 i915_icl_gamma_mode(const struct intel_crtc_state *crtc_state);
static u32 i915_icl_csc_mode(const struct intel_crtc_state *crtc_state);
static void i915_icl_load_csc_matrix(const struct intel_crtc_state *crtc_state);
static void i915_icl_load_luts(const struct intel_crtc_state *crtc_state);
static void i915_icl_color_commit_noarm(const struct intel_crtc_state *crtc_state);
static void i915_icl_color_commit_arm(const struct intel_crtc_state *crtc_state);

/*
 * Loads the pipe's LUTs through the device's colour hook.
 *
 * The Linux intel_color_load_luts(): a state carried by a DSB loads its
 * LUTs there instead.
 */
void
drv_i915_color_load_luts(
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;

	/* Finds the device the crtc belongs to. */
	i915 = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);

	/* A DSB loads the LUTs itself. */
	if (crtc_state->dsb)
		return;

	/* Loads the LUTs of the state. */
	i915->display.funcs.color->load_luts(crtc_state);
}

/*
 * Writes the colour registers that do not arm the update.
 *
 * The Linux intel_color_commit_noarm(): the hook is optional.
 */
void
drv_i915_color_commit_noarm(
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;

	/* Finds the device the crtc belongs to. */
	i915 = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);

	/* Writes the unarmed colour registers when the device has the hook. */
	if (i915->display.funcs.color->color_commit_noarm) {
		i915->display.funcs.color->color_commit_noarm(crtc_state);
	}
}

/*
 * Writes the colour registers that arm with the pipe update.
 *
 * The Linux intel_color_commit_arm(): a state carried by a DSB commits it
 * after the hook.
 */
void
drv_i915_color_commit_arm(
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;

	/* Finds the device the crtc belongs to. */
	i915 = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);

	/* Writes the armed colour registers. */
	i915->display.funcs.color->color_commit_arm(crtc_state);

	/* A DSB is committed after the registers it carries. */
	if (crtc_state->dsb)
		intel_dsb_commit(crtc_state->dsb, true);
}

/*
 * Returns the colour hooks of display version 12 and later.
 *
 * A device view that is not a modeset object (the takeover registry) binds
 * them: the sanitize of an active crtc commits the colour registers through
 * them.
 */
const struct intel_color_funcs *
drv_i915_lcd_ms_color_funcs(void)
{
	/*
	 * The colour hooks of display version 12 and later (the Linux
	 * tgl_color_funcs), as far as this path uses them.  The table is
	 * constant and shared by every device view.
	 */
	static const struct intel_color_funcs tgl_color_funcs = {
		i915_icl_color_commit_noarm,
		i915_icl_color_commit_arm,
		i915_icl_load_luts,
	};

	/* Succeeded: the table is constant and shared. */
	return &tgl_color_funcs;
}

/*
 * Binds the colour hooks of a modeset object and computes its mode words.
 *
 * The part of the Linux icl_color_check() this path needs: no LUT and no
 * CTM exist, so only GAMMA_MODE and CSC_MODE are computed.
 */
void
drv_i915_lcd_ms_color_check(
	struct i915_lcd_modeset *ms)
{
	/* The device reaches the colour registers through the display version 12 hooks. */
	ms->i915.display.funcs.color = drv_i915_lcd_ms_color_funcs();

	/* Computes the two mode words the arming write programs. */
	ms->crtc_state.gamma_mode = i915_icl_gamma_mode(&ms->crtc_state);
	ms->crtc_state.csc_mode = i915_icl_csc_mode(&ms->crtc_state);
}

/* Tells whether a LUT blob is the 256-entry legacy LUT. */
static bool
i915_lut_is_legacy(
	const struct drm_property_blob *lut)
{
	/* No LUT is not a legacy LUT. */
	if (!lut)
		return false;

	/* Only the 256-entry size is legacy. */
	if (drm_color_lut_size(lut) != LEGACY_LUT_LENGTH)
		return false;

	/* Succeeded: the LUT has the legacy size. */
	return true;
}

/* Computes the GAMMA_MODE word of a crtc state (the Linux icl_gamma_mode()). */
static u32
i915_icl_gamma_mode(
	const struct intel_crtc_state *crtc_state)
{
	u32 gamma_mode;
	bool legacy;
	int display_ver;

	/* The display version of the device the probe found (the Linux DISPLAY_VER() of the crtc's device). */
	display_ver = drv_i915_lcd_display_ver();
	gamma_mode = 0;

	/* A degamma LUT is applied before the CSC. */
	if (crtc_state->hw.degamma_lut)
		gamma_mode |= PRE_CSC_GAMMA_ENABLE;

	/* A gamma LUT is applied after the CSC unless C8 planes use the palette. */
	if (crtc_state->hw.gamma_lut &&
	    !crtc_state->c8_planes)
		gamma_mode |= POST_CSC_GAMMA_ENABLE;

	/* Asks whether the gamma LUT is the legacy one (only when there is one). */
	legacy = false;
	if (crtc_state->hw.gamma_lut)
		legacy = i915_lut_is_legacy(crtc_state->hw.gamma_lut);

	/*
	 * No LUT or a legacy LUT is 8-bit gamma.  Display version 13 uses
	 * 10-bit gamma ("ToDo: Extend to Logarithmic Gamma once the new UAPI
	 * is accepted"); earlier versions use the 12-bit multi-segment mode.
	 */
	if (!crtc_state->hw.gamma_lut) {
		gamma_mode |= GAMMA_MODE_MODE_8BIT;
	} else if (legacy) {
		gamma_mode |= GAMMA_MODE_MODE_8BIT;
	} else if (display_ver >= 13) {
		gamma_mode |= GAMMA_MODE_MODE_10BIT;
	} else {
		gamma_mode |= GAMMA_MODE_MODE_12BIT_MULTI_SEG;
	}

	/* Succeeded: reports the word. */
	return gamma_mode;
}

/* Computes the CSC_MODE word of a crtc state (the Linux icl_csc_mode()). */
static u32
i915_icl_csc_mode(
	const struct intel_crtc_state *crtc_state)
{
	u32 csc_mode;

	/* Starts from no CSC. */
	csc_mode = 0;

	/* A CTM enables the pipe CSC. */
	if (crtc_state->hw.ctm)
		csc_mode |= ICL_CSC_ENABLE;

	/* YCbCr output or limited range enables the output CSC. */
	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB) {
		csc_mode |= ICL_OUTPUT_CSC_ENABLE;
	} else if (crtc_state->limited_color_range) {
		csc_mode |= ICL_OUTPUT_CSC_ENABLE;
	}

	/* Succeeded: reports the word. */
	return csc_mode;
}

/* Loads the CSC matrices the mode word enables (the Linux icl_load_csc_matrix()). */
static void
i915_icl_load_csc_matrix(
	const struct intel_crtc_state *crtc_state)
{
	/*
	 * Both loaders are guards of modeset-internal.h: this path has no CTM
	 * and no YCbCr output, so reaching either one is reported as an error.
	 * They do not evaluate the crtc they are handed.
	 */

	/* Loads the pipe CSC when the CTM enabled it. */
	if (crtc_state->csc_mode & ICL_CSC_ENABLE)
		ilk_update_pipe_csc(to_intel_crtc(crtc_state->uapi.crtc), &crtc_state->csc);

	/* Loads the output CSC when the output format or range enabled it. */
	if (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE)
		icl_update_output_csc(to_intel_crtc(crtc_state->uapi.crtc), &crtc_state->output_csc);
}

/* Loads the degamma and gamma LUTs of a crtc state (the Linux icl_load_luts()). */
static void
i915_icl_load_luts(
	const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *pre_csc_lut;
	const struct drm_property_blob *post_csc_lut;

	/* Takes the two LUTs the check assigned. */
	pre_csc_lut = crtc_state->pre_csc_lut;
	post_csc_lut = crtc_state->post_csc_lut;

	/* Loads the degamma LUT when there is one. */
	if (pre_csc_lut)
		glk_load_degamma_lut(crtc_state, pre_csc_lut);

	/* Loads the gamma LUT in the format the gamma mode selects. */
	switch (crtc_state->gamma_mode & GAMMA_MODE_MODE_MASK) {
	case GAMMA_MODE_MODE_8BIT:
		ilk_load_lut_8(crtc_state, post_csc_lut);
		break;
	case GAMMA_MODE_MODE_12BIT_MULTI_SEG:
		icl_program_gamma_superfine_segment(crtc_state);
		icl_program_gamma_multi_segment(crtc_state);
		ivb_load_lut_ext_max(crtc_state);
		glk_load_lut_ext2_max(crtc_state);
		break;
	case GAMMA_MODE_MODE_10BIT:
		bdw_load_lut_10(crtc_state, post_csc_lut, PAL_PREC_INDEX_VALUE(0));
		ivb_load_lut_ext_max(crtc_state);
		glk_load_lut_ext2_max(crtc_state);
		break;
	default:
		I915_LCD_MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

/*
 * Writes the unarmed colour registers (the Linux icl_color_commit_noarm()).
 *
 * Despite Wa_1406463849, ICL no longer suffers from the SKL DC5/PSR CSC
 * black screen issue, and on TGL+ all CSC arming issues are fixed: only the
 * CSC matrices are loaded here.
 */
static void
i915_icl_color_commit_noarm(
	const struct intel_crtc_state *crtc_state)
{
	/* Loads the CSC matrices. */
	i915_icl_load_csc_matrix(crtc_state);
}

/* Writes the armed colour registers (the Linux icl_color_commit_arm()). */
static void
i915_icl_color_commit_arm(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *i915;
	enum pipe pipe;

	/* Finds the crtc, its device and its pipe. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915 = i915_lcd_to_i915(crtc->base.dev);
	pipe = crtc->pipe;

	/* Userspace does not control the pipe background colour: it is forced to black. */
	i915_lcd_intel_de_write(i915, SKL_BOTTOM_COLOR(pipe), 0);

	/* Programs the gamma mode. */
	i915_lcd_intel_de_write(i915, GAMMA_MODE(crtc->pipe), crtc_state->gamma_mode);

	/* Programs the CSC mode. */
	i915_lcd_intel_de_write_fw(i915, PIPE_CSC_MODE(crtc->pipe), crtc_state->csc_mode);
}
