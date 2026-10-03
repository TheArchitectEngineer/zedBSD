/*
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
 *
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_color.c
 * (sha256 92993f5274bde50a12af4c1d01f06ebaf5887f93beca7e250822efdd167f3bd7) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: lut_is_legacy, icl_gamma_mode, icl_csc_mode, icl_load_csc_matrix,
 *    icl_load_luts, icl_color_commit_noarm, icl_color_commit_arm, intel_color_load_luts,
 *    intel_color_commit_noarm, intel_color_commit_arm;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_mreg_color.h;
 *  - parity_color_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_mreg_color.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool lut_is_legacy(const struct drm_property_blob *lut);
static u32 icl_gamma_mode(const struct intel_crtc_state *crtc_state);
static u32 icl_csc_mode(const struct intel_crtc_state *crtc_state);
static void icl_load_csc_matrix(const struct intel_crtc_state *crtc_state);
static void icl_load_luts(const struct intel_crtc_state *crtc_state);
static void icl_color_commit_noarm(const struct intel_crtc_state *crtc_state);
static void icl_color_commit_arm(const struct intel_crtc_state *crtc_state);

static bool lut_is_legacy(const struct drm_property_blob *lut)
{
	return lut && drm_color_lut_size(lut) == LEGACY_LUT_LENGTH;
}

static u32 icl_gamma_mode(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	u32 gamma_mode = 0;

	if (crtc_state->hw.degamma_lut)
		gamma_mode |= PRE_CSC_GAMMA_ENABLE;

	if (crtc_state->hw.gamma_lut &&
	    !crtc_state->c8_planes)
		gamma_mode |= POST_CSC_GAMMA_ENABLE;

	if (!crtc_state->hw.gamma_lut ||
	    lut_is_legacy(crtc_state->hw.gamma_lut))
		gamma_mode |= GAMMA_MODE_MODE_8BIT;
	/*
	 * Enable 10bit gamma for D13
	 * ToDo: Extend to Logarithmic Gamma once the new UAPI
	 * is accepted and implemented by a userspace consumer
	 */
	else if (DISPLAY_VER(i915) >= 13)
		gamma_mode |= GAMMA_MODE_MODE_10BIT;
	else
		gamma_mode |= GAMMA_MODE_MODE_12BIT_MULTI_SEG;

	return gamma_mode;
}

static u32 icl_csc_mode(const struct intel_crtc_state *crtc_state)
{
	u32 csc_mode = 0;

	if (crtc_state->hw.ctm)
		csc_mode |= ICL_CSC_ENABLE;

	if (crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB ||
	    crtc_state->limited_color_range)
		csc_mode |= ICL_OUTPUT_CSC_ENABLE;

	return csc_mode;
}

static void icl_load_csc_matrix(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->csc_mode & ICL_CSC_ENABLE)
		ilk_update_pipe_csc(crtc, &crtc_state->csc);

	if (crtc_state->csc_mode & ICL_OUTPUT_CSC_ENABLE)
		icl_update_output_csc(crtc, &crtc_state->output_csc);
}

static void icl_load_luts(const struct intel_crtc_state *crtc_state)
{
	const struct drm_property_blob *pre_csc_lut = crtc_state->pre_csc_lut;
	const struct drm_property_blob *post_csc_lut = crtc_state->post_csc_lut;

	if (pre_csc_lut)
		glk_load_degamma_lut(crtc_state, pre_csc_lut);

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
		MISSING_CASE(crtc_state->gamma_mode);
		break;
	}
}

static void icl_color_commit_noarm(const struct intel_crtc_state *crtc_state)
{
	/*
	 * Despite Wa_1406463849, ICL no longer suffers from the SKL
	 * DC5/PSR CSC black screen issue (see skl_color_commit_noarm()).
	 * Possibly due to the extra sticky CSC arming
	 * (see icl_color_post_update()).
	 *
	 * On TGL+ all CSC arming issues have been properly fixed.
	 */
	icl_load_csc_matrix(crtc_state);
}

static void icl_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/*
	 * We don't (yet) allow userspace to control the pipe background color,
	 * so force it to black.
	 */
	intel_de_write(i915, SKL_BOTTOM_COLOR(pipe), 0);

	intel_de_write(i915, GAMMA_MODE(crtc->pipe),
		       crtc_state->gamma_mode);

	intel_de_write_fw(i915, PIPE_CSC_MODE(crtc->pipe),
			  crtc_state->csc_mode);
}

void intel_color_load_luts(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (crtc_state->dsb)
		return;

	i915->display.funcs.color->load_luts(crtc_state);
}

void intel_color_commit_noarm(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (i915->display.funcs.color->color_commit_noarm)
		i915->display.funcs.color->color_commit_noarm(crtc_state);
}

void intel_color_commit_arm(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	i915->display.funcs.color->color_commit_arm(crtc_state);

	if (crtc_state->dsb)
		intel_dsb_commit(crtc_state->dsb, true);
}


#include "parity_color_glue.inc"
