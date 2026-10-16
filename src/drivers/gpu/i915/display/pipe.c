/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_crtc.c, drivers/gpu/drm/i915/display/intel_vrr.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_display.c),
 * which carries the following notice.
 *
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
 * 	Eric Anholt <eric@anholt.net>
 */

/*
 * The pipe and transcoder of the one-screen path (see pipe.h).
 *
 * The functions follow the Linux 6.8.12 text of intel_display.c (the crtc
 * enable and disable of display version 9 and later, the transcoder and
 * pipe writers they call, the link M/N values, the crtc power domains and
 * the readout of a running pipe) and intel_vrr.c (the VRR words of the
 * transcoder).  Every register
 * access goes through the device's backend (modeset-internal.h), so the
 * same text records register words for the calculation, drives the model
 * in the tests and programs the real display.  A callee of that text that
 * is not ported is a named step at the position the text calls it.
 *
 * The register writes, their order, the posting reads and the waits are
 * the Linux ones; they were compared word for word with the Linux register
 * dumps of the same machine.
 */

#include "modeset-internal.h"
#include "takeover-internal.h"
#include "../intel/trans.h"
#include "pipe.h"
#include "plane.h"
#include "color.h"
#include "vblank.h"
#include "ddi.h"
#include "dp.h"
#include "edid.h"
#include "xd-dp.h"
#include "xd-pipe.h"
#include <kern/kcrt.h>

/*
 * The pipe scalers (the Linux i915_reg.h SKL_PS_*): scaler 1 of pipe A at
 * 0x68180, scaler 2 0x100 above it, and the next pipe 0x800 above.
 */
#define I915_PS_1A_CTRL			0x68180
#define I915_PS_WIN_POS_1A		0x68170
#define I915_PS_WIN_SZ_1A		0x68174
#define I915_PS_SCALER_STRIDE		0x100
#define I915_PS_PIPE_STRIDE		0x800
#define I915_SKL_PS_REG(pipe, id, reg_1a) \
	_MMIO((reg_1a) + (pipe) * I915_PS_PIPE_STRIDE + (id) * I915_PS_SCALER_STRIDE)
#define I915_SKL_PS_CTRL(pipe, id)	I915_SKL_PS_REG(pipe, id, I915_PS_1A_CTRL)
#define I915_SKL_PS_WIN_POS(pipe, id)	I915_SKL_PS_REG(pipe, id, I915_PS_WIN_POS_1A)
#define I915_SKL_PS_WIN_SZ(pipe, id)	I915_SKL_PS_REG(pipe, id, I915_PS_WIN_SZ_1A)
#define I915_PS_SCALER_EN		REG_BIT(31)
#define I915_PS_PLANE_SEL_MASK		REG_GENMASK(27, 25)
#define I915_PS_WIN_XPOS_MASK		REG_GENMASK(31, 16)
#define I915_PS_WIN_YPOS_MASK		REG_GENMASK(15, 0)
#define I915_PS_WIN_XSIZE_MASK		REG_GENMASK(31, 16)
#define I915_PS_WIN_YSIZE_MASK		REG_GENMASK(15, 0)

/* The bit of the crtc itself in a scaler users mask (the Linux SKL_CRTC_INDEX). */
#define I915_SKL_CRTC_INDEX		31

static void i915_xd_timing_of(const struct drm_display_mode *mode, struct i915_xd_timing *timing);
static void i915_set_pipe_src_size(const struct intel_crtc_state *crtc_state);
static void i915_hsw_configure_cpu_transcoder(const struct intel_crtc_state *crtc_state);

/*
 * Tells whether a PHY is a Type-C PHY (the Linux intel_phy_is_tc()).
 *
 * Display version 13 has Type-C PHYs F to I, Tiger Lake D to I, Ice Lake C
 * to F; DG2's TC1 goes through the SNPS PHY and is not one.
 */
bool
drv_i915_lcd_intel_phy_is_tc(
	struct drm_i915_private *dev_priv,
	enum phy phy)
{
	UNUSED_PARAMETER(dev_priv);

	/* DG2's TC-capable output is programmed through the SNPS PHY. */
	if (IS_DG2(dev_priv))
		return false;

	/* Picks the Type-C range of the platform. */
	if (I915_LCD_DISPLAY_VER(dev_priv) >= 13) {
		if (phy < PHY_F)
			return false;
		if (phy > PHY_I)
			return false;

		return true;
	} else if (I915_LCD_IS_TIGERLAKE(dev_priv)) {
		if (phy < PHY_D)
			return false;
		if (phy > PHY_I)
			return false;

		return true;
	} else if (IS_ICELAKE(dev_priv)) {
		if (phy < PHY_C)
			return false;
		if (phy > PHY_F)
			return false;

		return true;
	}

	/* Succeeded: no other platform has a Type-C PHY. */
	return false;
}

/*
 * Returns the PHY of a port (the Linux intel_port_to_phy()).
 */
enum phy
drv_i915_lcd_intel_port_to_phy(
	struct drm_i915_private *i915,
	enum port port)
{
	UNUSED_PARAMETER(i915);

	/* The platforms whose ports and PHYs are not numbered alike. */
	if (I915_LCD_DISPLAY_VER(i915) >= 13 && port >= PORT_D_XELPD) {
		return PHY_D + port - PORT_D_XELPD;
	} else if (I915_LCD_DISPLAY_VER(i915) >= 13 && port >= PORT_TC1) {
		return PHY_F + port - PORT_TC1;
	} else if (IS_ALDERLAKE_S(i915) && port >= PORT_TC1) {
		return PHY_B + port - PORT_TC1;
	} else if ((IS_DG1(i915) || IS_ROCKETLAKE(i915)) && port >= PORT_TC1) {
		return PHY_C + port - PORT_TC1;
	} else if ((IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) &&
		   port == PORT_D) {
		return PHY_A;
	}

	/* Succeeded: the PHY has the port's number. */
	return PHY_A + port - PORT_A;
}

/*
 * Computes the data and link M/N values of a DP stream through the xd
 * DisplayPort code: fixed N values, the stream's share of the link's
 * payload and the pixel clock against the link's symbol clock.
 */
void
drv_i915_link_compute_m_n(
	u16 bits_per_pixel_x16,
	int nlanes,
	int pixel_clock,
	int link_clock,
	int bw_overhead,
	struct intel_link_m_n *m_n)
{
	struct i915_xd_dp_m_n values;

	/* The xd link's values, into the Linux structure. */
	drv_i915_xd_dp_m_n(bits_per_pixel_x16, (unsigned)nlanes, (uint32_t)pixel_clock, (uint32_t)link_clock, (uint32_t)bw_overhead, &values);
	m_n->tu = values.tu;
	m_n->data_m = values.data_m;
	m_n->data_n = values.data_n;
	m_n->link_m = values.link_m;
	m_n->link_n = values.link_n;
}

/*
 * Returns the power domain of a port's AUX channel (the Linux
 * intel_aux_power_domain()).
 */
enum intel_display_power_domain
drv_i915_aux_power_domain(
	struct intel_digital_port *dig_port)
{
	bool tbt;

	/*
	 * The domain helpers take the AUX domains from the display version's
	 * port-domain table (power.c): a Type-C channel's domain is its
	 * AUX_USBC well, which also blocks TC cold.
	 */

	/* A Type-C port in Thunderbolt-alt mode uses the Thunderbolt AUX domain. */
	tbt = i915_lcd_intel_tc_port_in_tbt_alt_mode(dig_port);
	if (tbt)
		return intel_display_power_tbt_aux_domain(i915_lcd_to_i915(dig_port->base.base.dev), dig_port->aux_ch);

	/* Succeeded: the legacy AUX domain of the channel. */
	return intel_display_power_legacy_aux_domain(i915_lcd_to_i915(dig_port->base.base.dev), dig_port->aux_ch);
}

/*
 * Detaches every scaler of a pipe that is being disabled (the Linux
 * skl_scaler_disable() and skl_detach_scaler()).
 *
 * Every scaler of the pipe is cleared, used or not, as Linux does: a
 * scaler the firmware left fitting the pipe stops with it, and the next
 * mode of the pipe starts unscaled.
 */
void
drv_i915_skl_scaler_disable(
	const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	int i;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* Turns each scaler off and clears its window. */
	for (i = 0; i < I915_LCD_SKL_NUM_SCALERS; i++) {
		i915_lcd_intel_de_write(dev_priv, I915_SKL_PS_CTRL(crtc->pipe, i), 0);
		i915_lcd_intel_de_write(dev_priv, I915_SKL_PS_WIN_POS(crtc->pipe, i), 0);
		i915_lcd_intel_de_write(dev_priv, I915_SKL_PS_WIN_SZ(crtc->pipe, i), 0);
	}
}

/*
 * Records the transcoder M/N, timing and pipe source words of a mode.
 *
 * The words go to `emit` (a recorder or the device).  The crtc state is
 * the one the Linux writers read for a progressive, non-doubled panel
 * mode, whose crtc timings drm_mode_set_crtcinfo() fills with no adjust
 * flags, as intel_crtc_compute_config() leaves them.  The device view is
 * the world's (a former function static).  Always 0.
 */
int
drv_i915_display_emit_transcoder(
	struct i915_lcd_world *world,
	struct i915_lcd_emit *emit,
	const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n,
	int pipe,
	int cpu_transcoder,
	int src_w,
	int src_h)
{
	struct drm_i915_private *i915;
	struct intel_crtc crtc;
	struct intel_crtc_state crtc_state;
	struct i915_xd_dp_m_n values;
	struct i915_xd_timing timing;

	/* The recorder's device view is the world's. */
	i915 = &world->i915_display_emit_transcoder_i915;

	/* Starts every object from zero. */
	kern_memset(i915, 0, sizeof(*i915));
	kern_memset(&crtc, 0, sizeof(crtc));
	kern_memset(&crtc_state, 0, sizeof(crtc_state));

	/* The device writes through the caller's backend. */
	i915->emit = emit;

	/* The crtc of the pipe. */
	crtc.base.dev = &i915->drm;
	crtc.pipe = (enum pipe)pipe;

	/* The crtc state: the transcoder, the mode's crtc timings and the pipe source. */
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.hw.adjusted_mode = *mode;
	drv_i915_drm_mode_set_crtcinfo(&crtc_state.hw.adjusted_mode, 0);
	crtc_state.pipe_src.x1 = 0;
	crtc_state.pipe_src.y1 = 0;
	crtc_state.pipe_src.x2 = src_w;
	crtc_state.pipe_src.y2 = src_h;

	/* Runs the xd writers in the order the transcoder's setup calls them. */
	values.tu = m_n->tu;
	values.data_m = m_n->data_m;
	values.data_n = m_n->data_n;
	values.link_m = m_n->link_m;
	values.link_n = m_n->link_n;
	i915_xd_timing_of(&crtc_state.hw.adjusted_mode, &timing);
	drv_i915_xd_pipe_m_n(drv_i915_lcd_xd_io(i915), (unsigned)cpu_transcoder, &values);
	drv_i915_xd_pipe_timing(drv_i915_lcd_xd_io(i915), drv_i915_lcd_xd_platform(), (unsigned)cpu_transcoder, &timing);
	i915_set_pipe_src_size(&crtc_state);

	/* Succeeded: the words are emitted. */
	return 0;
}

/*
 * Records the words of the Linux hsw_configure_cpu_transcoder() for a mode.
 *
 * M/N, timings, VRR words, TRANS_MULT, frame start delay and TRANSCONF, in
 * the order of the Linux caller.  The state is the one
 * intel_crtc_compute_config() and the DP encoder's compute_config() leave
 * for an SST panel: a full modeset, pixel multiplier 1, frame start delay
 * 1, VRR not in use, RGB output, no PCH encoder.  The device view and the
 * crtc state are the world's (former function statics).  Always 0.
 */
int
drv_i915_display_emit_cpu_transcoder(
	struct i915_lcd_world *world,
	struct i915_lcd_emit *emit,
	const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n,
	int pipe,
	int cpu_transcoder,
	int port_clock,
	int lanes,
	int pipe_bpp)
{
	struct drm_i915_private *i915;
	struct intel_crtc crtc;
	struct intel_crtc_state *crtc_state;

	/* The recorder's device view and crtc state are the world's. */
	i915 = &world->i915_display_emit_cpu_transcoder_i915;
	crtc_state = &world->i915_display_emit_cpu_transcoder_crtc_state;

	/* Starts every object from zero. */
	kern_memset(i915, 0, sizeof(*i915));
	kern_memset(&crtc, 0, sizeof(crtc));
	kern_memset(crtc_state, 0, sizeof(*crtc_state));

	/* The device writes through the caller's backend. */
	i915->emit = emit;

	/* The crtc of the pipe. */
	crtc.base.dev = &i915->drm;
	crtc.pipe = (enum pipe)pipe;

	/* The crtc state of a full modeset of an eDP SST stream. */
	crtc_state->uapi.crtc = &crtc.base;
	crtc_state->uapi.mode_changed = true;
	crtc_state->cpu_transcoder = cpu_transcoder;
	crtc_state->master_transcoder = INVALID_TRANSCODER;
	crtc_state->mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state->hw.adjusted_mode = *mode;
	drv_i915_drm_mode_set_crtcinfo(&crtc_state->hw.adjusted_mode, 0);
	crtc_state->output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state->port_clock = port_clock;
	crtc_state->lane_count = lanes;
	crtc_state->pipe_bpp = pipe_bpp;
	crtc_state->pixel_multiplier = 1;
	crtc_state->framestart_delay = 1;
	crtc_state->dp_m_n = *m_n;

	/* Runs the Linux caller. */
	i915_hsw_configure_cpu_transcoder(crtc_state);

	/* Succeeded: the words are emitted. */
	return 0;
}

/*
 * Describes the crtc timings of a mode to the xd transcoder (for a caller
 * that sets the transcoder up through the xd output).
 */
void
drv_i915_lcd_xd_timing(
	const struct drm_display_mode *mode,
	struct i915_xd_timing *timing)
{
	/* The mode's crtc timings. */
	i915_xd_timing_of(mode, timing);
}

/* Converts the crtc timings of a mode into the xd transcoder's timings. */
static void
i915_xd_timing_of(
	const struct drm_display_mode *mode,
	struct i915_xd_timing *timing)
{
	/* The horizontal and vertical crtc timings and the interlace flag. */
	timing->hdisplay = (uint32_t)mode->crtc_hdisplay;
	timing->hblank_start = (uint32_t)mode->crtc_hblank_start;
	timing->hblank_end = (uint32_t)mode->crtc_hblank_end;
	timing->hsync_start = (uint32_t)mode->crtc_hsync_start;
	timing->hsync_end = (uint32_t)mode->crtc_hsync_end;
	timing->htotal = (uint32_t)mode->crtc_htotal;
	timing->vdisplay = (uint32_t)mode->crtc_vdisplay;
	timing->vblank_start = (uint32_t)mode->crtc_vblank_start;
	timing->vblank_end = (uint32_t)mode->crtc_vblank_end;
	timing->vsync_start = (uint32_t)mode->crtc_vsync_start;
	timing->vsync_end = (uint32_t)mode->crtc_vsync_end;
	timing->vtotal = (uint32_t)mode->crtc_vtotal;
	timing->interlaced = (mode->flags & DRM_MODE_FLAG_INTERLACE) != 0 ? 1 : 0;
}

/* Writes the pipe source size through the xd pipe: always the user's requested size. */
static void
i915_set_pipe_src_size(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	int width;
	int height;

	/* Finds the crtc, its device and the source size. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	width = i915_drm_rect_width(&crtc_state->pipe_src);
	height = i915_drm_rect_height(&crtc_state->pipe_src);

	/* Writes the size. */
	drv_i915_xd_pipe_source(drv_i915_lcd_xd_io(dev_priv), (unsigned)crtc->pipe, (uint32_t)width, (uint32_t)height);
}

/*
 * Programs the CPU transcoder of a crtc state through the xd pipe: the
 * M/N values (FDI for a PCH encoder, the link's for DP), the timing, the
 * variable refresh words, the pixel multiplier, the frame start delay and
 * TRANSCONF.
 */
static void
i915_hsw_configure_cpu_transcoder(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	const struct intel_link_m_n *m_n;
	struct i915_xd_trans_setup setup;
	bool dp;
	bool needs_modeset;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* The transcoder and the mode's timing. */
	kern_memset(&setup, 0, sizeof(setup));
	setup.trans = (unsigned)crtc_state->cpu_transcoder;
	i915_xd_timing_of(&crtc_state->hw.adjusted_mode, &setup.timing);

	/* The M/N values: FDI for a PCH encoder, the link's for DP. */
	m_n = NULL;
	if (crtc_state->has_pch_encoder) {
		m_n = &crtc_state->fdi_m_n;
	} else {
		dp = intel_crtc_has_dp_encoder(crtc_state);
		if (dp)
			m_n = &crtc_state->dp_m_n;
	}
	if (m_n != NULL) {
		setup.dp = 1;
		setup.m_n.tu = m_n->tu;
		setup.m_n.data_m = m_n->data_m;
		setup.m_n.data_n = m_n->data_n;
		setup.m_n.link_m = m_n->link_m;
		setup.m_n.link_n = m_n->link_n;
	}

	/* The variable refresh, the multiplier and the frame start delay. */
	setup.vrr.vmin = (uint32_t)crtc_state->vrr.vmin;
	setup.vrr.vmax = (uint32_t)crtc_state->vrr.vmax;
	setup.vrr.flipline = (uint32_t)crtc_state->vrr.flipline;
	setup.vrr.guardband = (uint32_t)crtc_state->vrr.guardband;
	setup.vrr.pipeline_full = (uint32_t)crtc_state->vrr.pipeline_full;
	setup.pixel_multiplier = (unsigned)crtc_state->pixel_multiplier;
	setup.frame_start_delay = (unsigned)crtc_state->framestart_delay;

	/* A fastset keeps the pipe enabled. */
	needs_modeset = intel_crtc_needs_modeset(crtc_state);
	setup.running = needs_modeset ? 0 : 1;

	/* Programs the transcoder. */
	drv_i915_xd_pipe_configure(drv_i915_lcd_xd_io(dev_priv), drv_i915_lcd_xd_platform(), &setup);
}
