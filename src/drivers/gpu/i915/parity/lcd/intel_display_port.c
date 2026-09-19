/*
 * Copyright © 2006-2007 Intel Corporation
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
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_display.c
 * (sha256 f64a3b7c302986237b0c6c936be6f31a8736c04ac2dc5a2f5cab6c5307f9dee9) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_phy_is_tc, intel_port_to_phy, intel_reduce_m_n_ratio, compute_m_n, intel_link_compute_m_n,
 *    intel_set_m_n, intel_cpu_transcoder_has_m2_n2, intel_cpu_transcoder_set_m1_n1,
 *    intel_cpu_transcoder_set_m2_n2, intel_set_transcoder_timings, intel_set_pipe_src_size,
 *    hsw_set_frame_start_delay, hsw_set_transconf, their caller hsw_configure_cpu_transcoder, and ITS caller
 *    hsw_crtc_enable (so the order between the writers and the steps around them is the reference's);
 *  - callees of hsw_crtc_enable that are not ported are named steps (lcd_seq_compat.h), never dropped;
 *  - the includes are replaced by lcd_compat.h (register writes go through its emit hook, so the
 *    same text fills a word list in the tests and, later, the hardware);
 *  - parity_display_emit_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/, drm/ and i915 includes */
#include "lcd_trans_regs.h"
#include "lcd_ddi_regs.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static void
intel_reduce_m_n_ratio(u32 *num, u32 *den);
static void compute_m_n(u32 *ret_m, u32 *ret_n,
			u32 m, u32 n, u32 constant_n);
static void intel_set_transcoder_timings(const struct intel_crtc_state *crtc_state);
static void intel_set_pipe_src_size(const struct intel_crtc_state *crtc_state);
static void hsw_set_frame_start_delay(const struct intel_crtc_state *crtc_state);
static void hsw_set_transconf(const struct intel_crtc_state *crtc_state);
static void hsw_configure_cpu_transcoder(const struct intel_crtc_state *crtc_state);
static void hsw_crtc_enable(struct intel_atomic_state *state,
			    struct intel_crtc *crtc);
static bool is_hdr_mode(const struct intel_crtc_state *crtc_state);
static void
intel_wait_for_pipe_off(const struct intel_crtc_state *old_crtc_state);
static void bdw_set_pipe_misc(const struct intel_crtc_state *crtc_state);
static void icl_set_pipe_chicken(const struct intel_crtc_state *crtc_state);
static void hsw_set_linetime_wm(const struct intel_crtc_state *crtc_state);
static void hsw_crtc_disable(struct intel_atomic_state *state,
			     struct intel_crtc *crtc);
static void get_crtc_power_domains(struct intel_crtc_state *crtc_state,
				   struct intel_power_domain_mask *mask);

/* Prefer intel_encoder_is_tc() */
bool intel_phy_is_tc(struct drm_i915_private *dev_priv, enum phy phy)
{
	/*
	 * DG2's "TC1", although TC-capable output, doesn't share the same flow
	 * as other platforms on the display engine side and rather rely on the
	 * SNPS PHY, that is programmed separately
	 */
	if (IS_DG2(dev_priv))
		return false;

	if (DISPLAY_VER(dev_priv) >= 13)
		return phy >= PHY_F && phy <= PHY_I;
	else if (IS_TIGERLAKE(dev_priv))
		return phy >= PHY_D && phy <= PHY_I;
	else if (IS_ICELAKE(dev_priv))
		return phy >= PHY_C && phy <= PHY_F;

	return false;
}

/* Prefer intel_encoder_to_phy() */
enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port)
{
	if (DISPLAY_VER(i915) >= 13 && port >= PORT_D_XELPD)
		return PHY_D + port - PORT_D_XELPD;
	else if (DISPLAY_VER(i915) >= 13 && port >= PORT_TC1)
		return PHY_F + port - PORT_TC1;
	else if (IS_ALDERLAKE_S(i915) && port >= PORT_TC1)
		return PHY_B + port - PORT_TC1;
	else if ((IS_DG1(i915) || IS_ROCKETLAKE(i915)) && port >= PORT_TC1)
		return PHY_C + port - PORT_TC1;
	else if ((IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) &&
		 port == PORT_D)
		return PHY_A;

	return PHY_A + port - PORT_A;
}

static void
intel_reduce_m_n_ratio(u32 *num, u32 *den)
{
	while (*num > DATA_LINK_M_N_MASK ||
	       *den > DATA_LINK_M_N_MASK) {
		*num >>= 1;
		*den >>= 1;
	}
}

static void compute_m_n(u32 *ret_m, u32 *ret_n,
			u32 m, u32 n, u32 constant_n)
{
	if (constant_n)
		*ret_n = constant_n;
	else
		*ret_n = min_t(unsigned int, roundup_pow_of_two(n), DATA_LINK_N_MAX);

	*ret_m = div_u64(mul_u32_u32(m, *ret_n), n);
	intel_reduce_m_n_ratio(ret_m, ret_n);
}

void
intel_link_compute_m_n(u16 bits_per_pixel_x16, int nlanes,
		       int pixel_clock, int link_clock,
		       int bw_overhead,
		       struct intel_link_m_n *m_n)
{
	u32 link_symbol_clock = intel_dp_link_symbol_clock(link_clock);
	u32 data_m = intel_dp_effective_data_rate(pixel_clock, bits_per_pixel_x16,
						  bw_overhead);
	u32 data_n = intel_dp_max_data_rate(link_clock, nlanes);

	/*
	 * Windows/BIOS uses fixed M/N values always. Follow suit.
	 *
	 * Also several DP dongles in particular seem to be fussy
	 * about too large link M/N values. Presumably the 20bit
	 * value used by Windows/BIOS is acceptable to everyone.
	 */
	m_n->tu = 64;
	compute_m_n(&m_n->data_m, &m_n->data_n,
		    data_m, data_n,
		    0x8000000);

	compute_m_n(&m_n->link_m, &m_n->link_n,
		    pixel_clock, link_symbol_clock,
		    0x80000);
}

void intel_set_m_n(struct drm_i915_private *i915,
		   const struct intel_link_m_n *m_n,
		   i915_reg_t data_m_reg, i915_reg_t data_n_reg,
		   i915_reg_t link_m_reg, i915_reg_t link_n_reg)
{
	intel_de_write(i915, data_m_reg, TU_SIZE(m_n->tu) | m_n->data_m);
	intel_de_write(i915, data_n_reg, m_n->data_n);
	intel_de_write(i915, link_m_reg, m_n->link_m);
	/*
	 * On BDW+ writing LINK_N arms the double buffered update
	 * of all the M/N registers, so it must be written last.
	 */
	intel_de_write(i915, link_n_reg, m_n->link_n);
}

bool intel_cpu_transcoder_has_m2_n2(struct drm_i915_private *dev_priv,
				    enum transcoder transcoder)
{
	if (IS_HASWELL(dev_priv))
		return transcoder == TRANSCODER_EDP;

	return IS_DISPLAY_VER(dev_priv, 5, 7) || IS_CHERRYVIEW(dev_priv);
}

void intel_cpu_transcoder_set_m1_n1(struct intel_crtc *crtc,
				    enum transcoder transcoder,
				    const struct intel_link_m_n *m_n)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (DISPLAY_VER(dev_priv) >= 5)
		intel_set_m_n(dev_priv, m_n,
			      PIPE_DATA_M1(transcoder), PIPE_DATA_N1(transcoder),
			      PIPE_LINK_M1(transcoder), PIPE_LINK_N1(transcoder));
	else
		intel_set_m_n(dev_priv, m_n,
			      PIPE_DATA_M_G4X(pipe), PIPE_DATA_N_G4X(pipe),
			      PIPE_LINK_M_G4X(pipe), PIPE_LINK_N_G4X(pipe));
}

void intel_cpu_transcoder_set_m2_n2(struct intel_crtc *crtc,
				    enum transcoder transcoder,
				    const struct intel_link_m_n *m_n)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (!intel_cpu_transcoder_has_m2_n2(dev_priv, transcoder))
		return;

	intel_set_m_n(dev_priv, m_n,
		      PIPE_DATA_M2(transcoder), PIPE_DATA_N2(transcoder),
		      PIPE_LINK_M2(transcoder), PIPE_LINK_N2(transcoder));
}

static void intel_set_transcoder_timings(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	u32 crtc_vdisplay, crtc_vtotal, crtc_vblank_start, crtc_vblank_end;
	int vsyncshift = 0;

	/* We need to be careful not to changed the adjusted mode, for otherwise
	 * the hw state checker will get angry at the mismatch. */
	crtc_vdisplay = adjusted_mode->crtc_vdisplay;
	crtc_vtotal = adjusted_mode->crtc_vtotal;
	crtc_vblank_start = adjusted_mode->crtc_vblank_start;
	crtc_vblank_end = adjusted_mode->crtc_vblank_end;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		/* the chip adds 2 halflines automatically */
		crtc_vtotal -= 1;
		crtc_vblank_end -= 1;

		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO))
			vsyncshift = (adjusted_mode->crtc_htotal - 1) / 2;
		else
			vsyncshift = adjusted_mode->crtc_hsync_start -
				adjusted_mode->crtc_htotal / 2;
		if (vsyncshift < 0)
			vsyncshift += adjusted_mode->crtc_htotal;
	}

	/*
	 * VBLANK_START no longer works on ADL+, instead we must use
	 * TRANS_SET_CONTEXT_LATENCY to configure the pipe vblank start.
	 */
	if (DISPLAY_VER(dev_priv) >= 13) {
		intel_de_write(dev_priv, TRANS_SET_CONTEXT_LATENCY(cpu_transcoder),
			       crtc_vblank_start - crtc_vdisplay);

		/*
		 * VBLANK_START not used by hw, just clear it
		 * to make it stand out in register dumps.
		 */
		crtc_vblank_start = 1;
	}

	if (DISPLAY_VER(dev_priv) >= 4)
		intel_de_write(dev_priv, TRANS_VSYNCSHIFT(cpu_transcoder),
			       vsyncshift);

	intel_de_write(dev_priv, TRANS_HTOTAL(cpu_transcoder),
		       HACTIVE(adjusted_mode->crtc_hdisplay - 1) |
		       HTOTAL(adjusted_mode->crtc_htotal - 1));
	intel_de_write(dev_priv, TRANS_HBLANK(cpu_transcoder),
		       HBLANK_START(adjusted_mode->crtc_hblank_start - 1) |
		       HBLANK_END(adjusted_mode->crtc_hblank_end - 1));
	intel_de_write(dev_priv, TRANS_HSYNC(cpu_transcoder),
		       HSYNC_START(adjusted_mode->crtc_hsync_start - 1) |
		       HSYNC_END(adjusted_mode->crtc_hsync_end - 1));

	intel_de_write(dev_priv, TRANS_VTOTAL(cpu_transcoder),
		       VACTIVE(crtc_vdisplay - 1) |
		       VTOTAL(crtc_vtotal - 1));
	intel_de_write(dev_priv, TRANS_VBLANK(cpu_transcoder),
		       VBLANK_START(crtc_vblank_start - 1) |
		       VBLANK_END(crtc_vblank_end - 1));
	intel_de_write(dev_priv, TRANS_VSYNC(cpu_transcoder),
		       VSYNC_START(adjusted_mode->crtc_vsync_start - 1) |
		       VSYNC_END(adjusted_mode->crtc_vsync_end - 1));

	/* Workaround: when the EDP input selection is B, the VTOTAL_B must be
	 * programmed with the VTOTAL_EDP value. Same for VTOTAL_C. This is
	 * documented on the DDI_FUNC_CTL register description, EDP Input Select
	 * bits. */
	if (IS_HASWELL(dev_priv) && cpu_transcoder == TRANSCODER_EDP &&
	    (pipe == PIPE_B || pipe == PIPE_C))
		intel_de_write(dev_priv, TRANS_VTOTAL(pipe),
			       VACTIVE(crtc_vdisplay - 1) |
			       VTOTAL(crtc_vtotal - 1));
}

static void intel_set_pipe_src_size(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	int width = drm_rect_width(&crtc_state->pipe_src);
	int height = drm_rect_height(&crtc_state->pipe_src);
	enum pipe pipe = crtc->pipe;

	/* pipesrc controls the size that is scaled from, which should
	 * always be the user's requested size.
	 */
	intel_de_write(dev_priv, PIPESRC(pipe),
		       PIPESRC_WIDTH(width - 1) | PIPESRC_HEIGHT(height - 1));
}

static void hsw_set_frame_start_delay(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	intel_de_rmw(i915, hsw_chicken_trans_reg(i915, crtc_state->cpu_transcoder),
		     HSW_FRAME_START_DELAY_MASK,
		     HSW_FRAME_START_DELAY(crtc_state->framestart_delay - 1));
}

static void hsw_set_transconf(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val = 0;

	/*
	 * - During modeset the pipe is still disabled and must remain so
	 * - During fastset the pipe is already enabled and must remain so
	 */
	if (!intel_crtc_needs_modeset(crtc_state))
		val |= TRANSCONF_ENABLE;

	if (IS_HASWELL(dev_priv) && crtc_state->dither)
		val |= TRANSCONF_DITHER_EN | TRANSCONF_DITHER_TYPE_SP;

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		val |= TRANSCONF_INTERLACE_IF_ID_ILK;
	else
		val |= TRANSCONF_INTERLACE_PF_PD_ILK;

	if (IS_HASWELL(dev_priv) &&
	    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB)
		val |= TRANSCONF_OUTPUT_COLORSPACE_YUV_HSW;

	intel_de_write(dev_priv, TRANSCONF(cpu_transcoder), val);
	intel_de_posting_read(dev_priv, TRANSCONF(cpu_transcoder));
}

static void hsw_configure_cpu_transcoder(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (crtc_state->has_pch_encoder) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->fdi_m_n);
	} else if (intel_crtc_has_dp_encoder(crtc_state)) {
		intel_cpu_transcoder_set_m1_n1(crtc, cpu_transcoder,
					       &crtc_state->dp_m_n);
		intel_cpu_transcoder_set_m2_n2(crtc, cpu_transcoder,
					       &crtc_state->dp_m2_n2);
	}

	intel_set_transcoder_timings(crtc_state);
	if (HAS_VRR(dev_priv))
		intel_vrr_set_transcoder_timings(crtc_state);

	if (cpu_transcoder != TRANSCODER_EDP)
		intel_de_write(dev_priv, TRANS_MULT(cpu_transcoder),
			       crtc_state->pixel_multiplier - 1);

	hsw_set_frame_start_delay(crtc_state);

	hsw_set_transconf(crtc_state);
}

static void hsw_crtc_enable(struct intel_atomic_state *state,
			    struct intel_crtc *crtc)
{
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe, hsw_workaround_pipe;
	enum transcoder cpu_transcoder = new_crtc_state->cpu_transcoder;
	bool psl_clkgate_wa;

	if (drm_WARN_ON(&dev_priv->drm, crtc->active))
		return;

	intel_dmc_enable_pipe(dev_priv, crtc->pipe);

	if (!new_crtc_state->bigjoiner_pipes) {
		intel_encoders_pre_pll_enable(state, crtc);

		if (new_crtc_state->shared_dpll)
			intel_enable_shared_dpll(new_crtc_state);

		intel_encoders_pre_enable(state, crtc);
	} else {
		icl_ddi_bigjoiner_pre_enable(state, new_crtc_state);
	}

	intel_dsc_enable(new_crtc_state);

	if (DISPLAY_VER(dev_priv) >= 13)
		intel_uncompressed_joiner_enable(new_crtc_state);

	intel_set_pipe_src_size(new_crtc_state);
	if (DISPLAY_VER(dev_priv) >= 9 || IS_BROADWELL(dev_priv))
		bdw_set_pipe_misc(new_crtc_state);

	if (!intel_crtc_is_bigjoiner_slave(new_crtc_state) &&
	    !transcoder_is_dsi(cpu_transcoder))
		hsw_configure_cpu_transcoder(new_crtc_state);

	crtc->active = true;

	/* Display WA #1180: WaDisableScalarClockGating: glk */
	psl_clkgate_wa = DISPLAY_VER(dev_priv) == 10 &&
		new_crtc_state->pch_pfit.enabled;
	if (psl_clkgate_wa)
		glk_pipe_scaler_clock_gating_wa(dev_priv, pipe, true);

	if (DISPLAY_VER(dev_priv) >= 9)
		skl_pfit_enable(new_crtc_state);
	else
		ilk_pfit_enable(new_crtc_state);

	/*
	 * On ILK+ LUT must be loaded before the pipe is running but with
	 * clocks enabled
	 */
	intel_color_load_luts(new_crtc_state);
	intel_color_commit_noarm(new_crtc_state);
	intel_color_commit_arm(new_crtc_state);
	/* update DSPCNTR to configure gamma/csc for pipe bottom color */
	if (DISPLAY_VER(dev_priv) < 9)
		intel_disable_primary_plane(new_crtc_state);

	hsw_set_linetime_wm(new_crtc_state);

	if (DISPLAY_VER(dev_priv) >= 11)
		icl_set_pipe_chicken(new_crtc_state);

	intel_initial_watermarks(state, crtc);

	if (intel_crtc_is_bigjoiner_slave(new_crtc_state))
		intel_crtc_vblank_on(new_crtc_state);

	intel_encoders_enable(state, crtc);

	if (psl_clkgate_wa) {
		intel_crtc_wait_for_next_vblank(crtc);
		glk_pipe_scaler_clock_gating_wa(dev_priv, pipe, false);
	}

	/* If we change the relative order between pipe/planes enabling, we need
	 * to change the workaround. */
	hsw_workaround_pipe = new_crtc_state->hsw_workaround_pipe;
	if (IS_HASWELL(dev_priv) && hsw_workaround_pipe != INVALID_PIPE) {
		struct intel_crtc *wa_crtc;

		wa_crtc = intel_crtc_for_pipe(dev_priv, hsw_workaround_pipe);

		intel_crtc_wait_for_next_vblank(wa_crtc);
		intel_crtc_wait_for_next_vblank(wa_crtc);
	}
}

static bool is_hdr_mode(const struct intel_crtc_state *crtc_state)
{
	return (crtc_state->active_planes &
		~(icl_hdr_plane_mask() | BIT(PLANE_CURSOR))) == 0;
}

/* Prefer intel_encoder_is_combo() */
bool intel_phy_is_combo(struct drm_i915_private *dev_priv, enum phy phy)
{
	if (phy == PHY_NONE)
		return false;
	else if (IS_ALDERLAKE_S(dev_priv))
		return phy <= PHY_E;
	else if (IS_DG1(dev_priv) || IS_ROCKETLAKE(dev_priv))
		return phy <= PHY_D;
	else if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv))
		return phy <= PHY_C;
	else if (IS_ALDERLAKE_P(dev_priv) || IS_DISPLAY_VER(dev_priv, 11, 12))
		return phy <= PHY_B;
	else
		/*
		 * DG2 outputs labelled as "combo PHY" in the bspec use
		 * SNPS PHYs with completely different programming,
		 * hence we always return false here.
		 */
		return false;
}

enum intel_display_power_domain
intel_aux_power_domain(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		return intel_display_power_tbt_aux_domain(i915, dig_port->aux_ch);

	return intel_display_power_legacy_aux_domain(i915, dig_port->aux_ch);
}

static void
intel_wait_for_pipe_off(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (DISPLAY_VER(dev_priv) >= 4) {
		enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

		/* Wait for the Pipe State to go off */
		if (intel_de_wait_for_clear(dev_priv, TRANSCONF(cpu_transcoder),
					    TRANSCONF_STATE_ENABLE, 100))
			drm_WARN(&dev_priv->drm, 1, "pipe_off wait timed out\n");
	} else {
		intel_wait_for_pipe_scanline_stopped(crtc);
	}
}

void intel_enable_transcoder(const struct intel_crtc_state *new_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = new_crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	u32 val;

	drm_dbg_kms(&dev_priv->drm, "enabling pipe %c\n", pipe_name(pipe));

	assert_planes_disabled(crtc);

	/*
	 * A pipe without a PLL won't actually be able to drive bits from
	 * a plane.  On ILK+ the pipe PLLs are integrated, so we don't
	 * need the check.
	 */
	if (HAS_GMCH(dev_priv)) {
		if (intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI))
			assert_dsi_pll_enabled(dev_priv);
		else
			assert_pll_enabled(dev_priv, pipe);
	} else {
		if (new_crtc_state->has_pch_encoder) {
			/* if driving the PCH, we need FDI enabled */
			assert_fdi_rx_pll_enabled(dev_priv,
						  intel_crtc_pch_transcoder(crtc));
			assert_fdi_tx_pll_enabled(dev_priv,
						  (enum pipe) cpu_transcoder);
		}
		/* FIXME: assert CPU port conditions for SNB+ */
	}

	/* Wa_22012358565:adl-p */
	if (DISPLAY_VER(dev_priv) == 13)
		intel_de_rmw(dev_priv, PIPE_ARB_CTL(pipe),
			     0, PIPE_ARB_USE_PROG_SLOTS);

	val = intel_de_read(dev_priv, TRANSCONF(cpu_transcoder));
	if (val & TRANSCONF_ENABLE) {
		/* we keep both pipes enabled on 830 */
		drm_WARN_ON(&dev_priv->drm, !IS_I830(dev_priv));
		return;
	}

	intel_de_write(dev_priv, TRANSCONF(cpu_transcoder),
		       val | TRANSCONF_ENABLE);
	intel_de_posting_read(dev_priv, TRANSCONF(cpu_transcoder));

	/*
	 * Until the pipe starts PIPEDSL reads will return a stale value,
	 * which causes an apparent vblank timestamp jump when PIPEDSL
	 * resets to its proper value. That also messes up the frame count
	 * when it's derived from the timestamps. So let's wait for the
	 * pipe to start properly before we call drm_crtc_vblank_on()
	 */
	if (intel_crtc_max_vblank_count(new_crtc_state) == 0)
		intel_wait_for_pipe_scanline_moving(crtc);
}

void intel_disable_transcoder(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;
	enum pipe pipe = crtc->pipe;
	u32 val;

	drm_dbg_kms(&dev_priv->drm, "disabling pipe %c\n", pipe_name(pipe));

	/*
	 * Make sure planes won't keep trying to pump pixels to us,
	 * or we might hang the display.
	 */
	assert_planes_disabled(crtc);

	val = intel_de_read(dev_priv, TRANSCONF(cpu_transcoder));
	if ((val & TRANSCONF_ENABLE) == 0)
		return;

	/*
	 * Double wide has implications for planes
	 * so best keep it disabled when not needed.
	 */
	if (old_crtc_state->double_wide)
		val &= ~TRANSCONF_DOUBLE_WIDE;

	/* Don't disable pipe or pipe PLLs if needed */
	if (!IS_I830(dev_priv))
		val &= ~TRANSCONF_ENABLE;

	intel_de_write(dev_priv, TRANSCONF(cpu_transcoder), val);

	if (DISPLAY_VER(dev_priv) >= 12)
		intel_de_rmw(dev_priv, hsw_chicken_trans_reg(dev_priv, cpu_transcoder),
			     FECSTALL_DIS_DPTSTREAM_DPTTG, 0);

	if ((val & TRANSCONF_ENABLE) == 0)
		intel_wait_for_pipe_off(old_crtc_state);
}

static void bdw_set_pipe_misc(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 val = 0;

	switch (crtc_state->pipe_bpp) {
	case 18:
		val |= PIPE_MISC_BPC_6;
		break;
	case 24:
		val |= PIPE_MISC_BPC_8;
		break;
	case 30:
		val |= PIPE_MISC_BPC_10;
		break;
	case 36:
		/* Port output 12BPC defined for ADLP+ */
		if (DISPLAY_VER(dev_priv) >= 13)
			val |= PIPE_MISC_BPC_12_ADLP;
		break;
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		break;
	}

	if (crtc_state->dither)
		val |= PIPE_MISC_DITHER_ENABLE | PIPE_MISC_DITHER_TYPE_SP;

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 ||
	    crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		val |= PIPE_MISC_OUTPUT_COLORSPACE_YUV;

	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		val |= PIPE_MISC_YUV420_ENABLE |
			PIPE_MISC_YUV420_MODE_FULL_BLEND;

	if (DISPLAY_VER(dev_priv) >= 11 && is_hdr_mode(crtc_state))
		val |= PIPE_MISC_HDR_MODE_PRECISION;

	if (DISPLAY_VER(dev_priv) >= 12)
		val |= PIPE_MISC_PIXEL_ROUNDING_TRUNC;

	/* allow PSR with sprite enabled */
	if (IS_BROADWELL(dev_priv))
		val |= PIPE_MISC_PSR_MASK_SPRITE_ENABLE;

	intel_de_write(dev_priv, PIPE_MISC(crtc->pipe), val);
}

static void icl_set_pipe_chicken(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tmp;

	tmp = intel_de_read(dev_priv, PIPE_CHICKEN(pipe));

	/*
	 * Display WA #1153: icl
	 * enable hardware to bypass the alpha math
	 * and rounding for per-pixel values 00 and 0xff
	 */
	tmp |= PER_PIXEL_ALPHA_BYPASS_EN;
	/*
	 * Display WA # 1605353570: icl
	 * Set the pixel rounding bit to 1 for allowing
	 * passthrough of Frame buffer pixels unmodified
	 * across pipe
	 */
	tmp |= PIXEL_ROUNDING_TRUNC_FB_PASSTHRU;

	/*
	 * Underrun recovery must always be disabled on display 13+.
	 * DG2 chicken bit meaning is inverted compared to other platforms.
	 */
	if (IS_DG2(dev_priv))
		tmp &= ~UNDERRUN_RECOVERY_ENABLE_DG2;
	else if (DISPLAY_VER(dev_priv) >= 13)
		tmp |= UNDERRUN_RECOVERY_DISABLE_ADLP;

	/* Wa_14010547955:dg2 */
	if (IS_DG2(dev_priv))
		tmp |= DG2_RENDER_CCSTAG_4_3_EN;

	intel_de_write(dev_priv, PIPE_CHICKEN(pipe), tmp);
}

static void hsw_set_linetime_wm(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	intel_de_write(dev_priv, WM_LINETIME(crtc->pipe),
		       HSW_LINETIME(crtc_state->linetime) |
		       HSW_IPS_LINETIME(crtc_state->ips_linetime));
}

static void hsw_crtc_disable(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/*
	 * FIXME collapse everything to one hook.
	 * Need care with mst->ddi interactions.
	 */
	if (!intel_crtc_is_bigjoiner_slave(old_crtc_state)) {
		intel_encoders_disable(state, crtc);
		intel_encoders_post_disable(state, crtc);
	}

	intel_disable_shared_dpll(old_crtc_state);

	if (!intel_crtc_is_bigjoiner_slave(old_crtc_state)) {
		struct intel_crtc *slave_crtc;

		intel_encoders_post_pll_disable(state, crtc);

		intel_dmc_disable_pipe(i915, crtc->pipe);

		for_each_intel_crtc_in_pipe_mask(&i915->drm, slave_crtc,
						 intel_crtc_bigjoiner_slave_pipes(old_crtc_state))
			intel_dmc_disable_pipe(i915, slave_crtc->pipe);
	}
}

static void get_crtc_power_domains(struct intel_crtc_state *crtc_state,
				   struct intel_power_domain_mask *mask)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	struct drm_encoder *encoder;
	enum pipe pipe = crtc->pipe;

	bitmap_zero(mask->bits, POWER_DOMAIN_NUM);

	if (!crtc_state->hw.active)
		return;

	set_bit(POWER_DOMAIN_PIPE(pipe), mask->bits);
	set_bit(POWER_DOMAIN_TRANSCODER(cpu_transcoder), mask->bits);
	if (crtc_state->pch_pfit.enabled ||
	    crtc_state->pch_pfit.force_thru)
		set_bit(POWER_DOMAIN_PIPE_PANEL_FITTER(pipe), mask->bits);

	drm_for_each_encoder_mask(encoder, &dev_priv->drm,
				  crtc_state->uapi.encoder_mask) {
		struct intel_encoder *intel_encoder = to_intel_encoder(encoder);

		set_bit(intel_encoder->power_domain, mask->bits);
	}

	if (HAS_DDI(dev_priv) && crtc_state->has_audio)
		set_bit(POWER_DOMAIN_AUDIO_MMIO, mask->bits);

	if (crtc_state->shared_dpll)
		set_bit(POWER_DOMAIN_DISPLAY_CORE, mask->bits);

	if (crtc_state->dsc.compression_enable)
		set_bit(intel_dsc_power_domain(crtc, cpu_transcoder), mask->bits);
}

void intel_modeset_get_crtc_power_domains(struct intel_crtc_state *crtc_state,
					  struct intel_power_domain_mask *old_domains)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum intel_display_power_domain domain;
	struct intel_power_domain_mask domains, new_domains;

	get_crtc_power_domains(crtc_state, &domains);

	bitmap_andnot(new_domains.bits,
		      domains.bits,
		      crtc->enabled_power_domains.mask.bits,
		      POWER_DOMAIN_NUM);
	bitmap_andnot(old_domains->bits,
		      crtc->enabled_power_domains.mask.bits,
		      domains.bits,
		      POWER_DOMAIN_NUM);

	for_each_power_domain(domain, &new_domains)
		intel_display_power_get_in_set(dev_priv,
					       &crtc->enabled_power_domains,
					       domain);
}

void intel_modeset_put_crtc_power_domains(struct intel_crtc *crtc,
					  struct intel_power_domain_mask *domains)
{
	intel_display_power_put_mask_in_set(to_i915(crtc->base.dev),
					    &crtc->enabled_power_domains,
					    domains);
}


#include "parity_display_emit_glue.inc"	/* zedBSD: builds the crtc state and records the words */
