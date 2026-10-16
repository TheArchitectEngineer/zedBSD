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
 * the readout of a running pipe), intel_crtc.c (the synchronous update of
 * a running pipe: intel_pipe_update_start() and intel_pipe_update_end())
 * and intel_vrr.c (the VRR words of the transcoder).  Every register
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
static void i915_hsw_crtc_enable_part(struct intel_atomic_state *state, struct intel_crtc *crtc, unsigned part);
static bool i915_is_hdr_mode(const struct intel_crtc_state *crtc_state);
static void i915_bdw_set_pipe_misc(const struct intel_crtc_state *crtc_state);
static void i915_icl_set_pipe_chicken(const struct intel_crtc_state *crtc_state);
static void i915_hsw_set_linetime_wm(const struct intel_crtc_state *crtc_state);
static void i915_hsw_crtc_disable_part(struct intel_atomic_state *state, struct intel_crtc *crtc, unsigned part);
static void i915_get_crtc_power_domains(struct i915_lcd_world *world, struct intel_crtc_state *crtc_state, struct intel_power_domain_mask *mask);
static u32 i915_crtc_get_vblank_counter(struct intel_crtc *crtc);
static bool i915_crtc_needs_vblank_work(const struct intel_crtc_state *crtc_state);
static int i915_mode_vblank_start(const struct drm_display_mode *mode);

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
 * Tells whether a PHY is a combo PHY (the Linux intel_phy_is_combo()).
 *
 * DG2's outputs labelled "combo PHY" use SNPS PHYs with completely
 * different programming, hence false.
 */
bool
drv_i915_phy_is_combo(
	struct drm_i915_private *dev_priv,
	enum phy phy)
{
	UNUSED_PARAMETER(dev_priv);

	/* No PHY is not a combo PHY. */
	if (phy == PHY_NONE)
		return false;

	/* Picks the combo range of the platform. */
	if (IS_ALDERLAKE_S(dev_priv)) {
		return phy <= PHY_E;
	} else if (IS_DG1(dev_priv) || IS_ROCKETLAKE(dev_priv)) {
		return phy <= PHY_D;
	} else if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv)) {
		return phy <= PHY_C;
	} else if (I915_LCD_IS_ALDERLAKE_P(dev_priv) || I915_LCD_IS_DISPLAY_VER(dev_priv, 11, 12)) {
		return phy <= PHY_B;
	}

	/* Succeeded: DG2 and the rest have no combo PHY driven this way. */
	return false;
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
 * Enables the transcoder of a crtc state through the xd pipe: on display
 * 13 the pipe arbiter takes the programmed slots first, and a transcoder
 * that already runs is left as it is.  The pipe must have no plane
 * enabled.
 */
void
drv_i915_enable_transcoder(
	const struct intel_crtc_state *new_crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	enum pipe pipe;
	int ran;

	/* Finds the crtc, its device, its transcoder and its pipe. */
	crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	cpu_transcoder = new_crtc_state->cpu_transcoder;
	pipe = crtc->pipe;

	/* Notes the enable. */
	I915_LCD_DRM_DBG_KMS(&dev_priv->drm, "enabling pipe %c\n", pipe_name(pipe));

	/* The planes of the pipe must be off (a state check). */
	assert_planes_disabled(crtc);

	/* Starts the transcoder; one that already ran is a warning. */
	ran = drv_i915_xd_pipe_start(drv_i915_lcd_xd_io(dev_priv), drv_i915_lcd_xd_platform(), (unsigned)cpu_transcoder);
	if (ran) {
		I915_LCD_DRM_WARN_ON(&dev_priv->drm, !IS_I830(dev_priv));
		return;
	}

	/*
	 * Until the pipe starts, PIPEDSL reads return a stale value; without
	 * a hardware frame counter the vblank layer would see a jump, so the
	 * scanline is waited for to move.
	 */
	if (intel_crtc_max_vblank_count(new_crtc_state) == 0)
		drv_i915_wait_for_pipe_scanline_moving(crtc);
}

/*
 * Disables the transcoder of a crtc state through the xd pipe: its enable
 * cleared, the FEC stall released, and the transcoder waited for to go
 * off.  The planes must be off already, or the pipe may hang.
 */
void
drv_i915_disable_transcoder(
	const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	enum pipe pipe;
	unsigned late;
	int was_running;

	/* Finds the crtc, its device, its transcoder and its pipe. */
	crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	cpu_transcoder = old_crtc_state->cpu_transcoder;
	pipe = crtc->pipe;

	/* Notes the disable. */
	I915_LCD_DRM_DBG_KMS(&dev_priv->drm, "disabling pipe %c\n", pipe_name(pipe));

	/* Planes must not keep pumping pixels into the pipe (a state check). */
	assert_planes_disabled(crtc);

	/* Stops the transcoder; a transcoder that did not go off is reported. */
	late = drv_i915_xd_pipe_stop(drv_i915_lcd_xd_io(dev_priv), (unsigned)cpu_transcoder, &was_running);
	if ((late & I915_XD_PIPE_LATE_OFF) != 0U)
		I915_LCD_DRM_WARN(&dev_priv->drm, 1, "pipe_off wait timed out\n");
}

/*
 * Takes the power domains a crtc state needs and reports those it no
 * longer needs (the Linux intel_modeset_get_crtc_power_domains()).
 *
 * The domains the state needs and the crtc does not hold yet are taken
 * into the crtc's set; the domains the crtc holds and the state does not
 * need are reported in old_domains, for the caller to put after the commit.
 * The encoder of the state is the named world's only encoder.
 */
void
drv_i915_modeset_get_crtc_power_domains(
	struct i915_lcd_world *world,
	struct intel_crtc_state *crtc_state,
	struct intel_power_domain_mask *old_domains)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	enum intel_display_power_domain domain;
	struct intel_power_domain_mask domains;
	struct intel_power_domain_mask new_domains;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* The domains the state needs. */
	i915_get_crtc_power_domains(world, crtc_state, &domains);

	/* Splits them into the new ones and the ones no longer needed. */
	i915_bitmap_andnot(new_domains.bits, domains.bits, crtc->enabled_power_domains.mask.bits, POWER_DOMAIN_NUM);
	i915_bitmap_andnot(old_domains->bits, crtc->enabled_power_domains.mask.bits, domains.bits, POWER_DOMAIN_NUM);

	/* Takes the new domains into the crtc's set. */
	for_each_power_domain(domain, &new_domains) {
		drv_i915_display_power_get_in_set(dev_priv, &crtc->enabled_power_domains, domain);
	}
}

/*
 * Puts power domains of a crtc's set (the Linux
 * intel_modeset_put_crtc_power_domains()).
 */
void
drv_i915_modeset_put_crtc_power_domains(
	struct intel_crtc *crtc,
	struct intel_power_domain_mask *domains)
{
	struct drm_i915_private *i915;

	/* Finds the device of the crtc. */
	i915 = i915_lcd_to_i915(crtc->base.dev);

	/* Puts the domains and drops them from the set. */
	drv_i915_display_power_put_mask_in_set(i915, &crtc->enabled_power_domains, domains);
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

/*
 * Runs one part of the enable of a modeset object's crtc (the Linux
 * hsw_crtc_enable(), cut where a screen's light steps meet it): the
 * pipe's DMC, the encoders before the PLL, the shared PLL, the encoders'
 * pre-enable, the pipe (from here the crtc runs), the encoders' enable.
 * The atomic state is reduced to the object's one crtc state, the new
 * state of the enable.
 */
void
drv_i915_lcd_ms_crtc_enable_part(
	struct i915_lcd_modeset *ms,
	unsigned part)
{
	/* The object's device is the one the Linux text works on from here. */
	ms->world->i915_lcd_cur_i915 = &ms->i915;

	/* The atomic state: the object's crtc state is the new one, there is no old one. */
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;
	ms->state.old_crtc_state = 0;

	/* The part. */
	i915_hsw_crtc_enable_part(&ms->state, &ms->crtc, part);
}

/*
 * Runs one part of the disable of a modeset object's crtc (the Linux
 * hsw_crtc_disable(), cut where a screen's dark steps meet it): the
 * encoders' disable, their post-disable (the transcoder stops), the
 * shared PLL, the encoders after the PLL, the pipe's DMC (after which the
 * crtc no longer runs).  The atomic state is the object's one crtc
 * state, the old state of the disable.
 */
void
drv_i915_lcd_ms_crtc_disable_part(
	struct i915_lcd_modeset *ms,
	unsigned part)
{
	/* The object's device is the one the Linux text works on from here. */
	ms->world->i915_lcd_cur_i915 = &ms->i915;

	/* The atomic state: the object's crtc state is the old one, there is no new one. */
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = 0;
	ms->state.old_crtc_state = &ms->crtc_state;

	/* The part. */
	i915_hsw_crtc_disable_part(&ms->state, &ms->crtc, part);

	/* intel_old_crtc_state_disables() marks the crtc inactive after the hook. */
	if (part == I915_LCD_CRTC_PART_DMC)
		ms->crtc.active = false;
}

/*
 * Converts a time in microseconds to scanlines of a mode, rounded up (the
 * Linux intel_usecs_to_scanlines()).
 *
 * A mode without a line length counts as one line.
 */
int
drv_i915_usecs_to_scanlines(
	const struct drm_display_mode *adjusted_mode,
	int usecs)
{
	/* paranoia: a mode without a line length */
	if (!adjusted_mode->crtc_htotal)
		return 1;

	/* Succeeded: reports the lines the time spans. */
	return DIV_ROUND_UP(usecs * adjusted_mode->crtc_clock, 1000 * adjusted_mode->crtc_htotal);
}

/*
 * Computes the scanline window an update of the pipe must stay out of
 * (the Linux intel_crtc_vblank_evade_scanlines()).
 *
 * During a fastset the transcoder still runs with the old timings, so the
 * old state's mode is used unless the new state is a full modeset.  M/N
 * and TRANS_VTOTAL are double buffered on the undelayed vblank, so a
 * seamless M/N or LRR update (and a DSB) evades both vblanks.
 */
void
drv_i915_crtc_vblank_evade_scanlines(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc,
	int *min,
	int *max,
	int *vblank_start)
{
	const struct intel_crtc_state *old_crtc_state;
	const struct intel_crtc_state *new_crtc_state;
	const struct intel_crtc_state *crtc_state;
	const struct drm_display_mode *adjusted_mode;
	bool needs_modeset;
	bool push_sent;

	/* Takes the two states of the update. */
	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

	/* The transcoder runs the new timings only after a full modeset. */
	needs_modeset = intel_crtc_needs_modeset(new_crtc_state);
	if (needs_modeset) {
		crtc_state = new_crtc_state;
	} else {
		crtc_state = old_crtc_state;
	}

	/* The mode the transcoder runs. */
	adjusted_mode = &crtc_state->hw.adjusted_mode;

	/*
	 * With VRR the vblank starts at vmin once the push is sent and at vmax
	 * before; timing changes must happen with VRR disabled.
	 */
	if (crtc->mode_flags & I915_MODE_FLAG_VRR) {
		if (needs_modeset || new_crtc_state->update_m_n || new_crtc_state->update_lrr) {
			/* drm_WARN_ON(), with the Linux condition as its text. */
			drv_i915_lcd_error("WARN_ON(intel_crtc_needs_modeset(new_crtc_state) || new_crtc_state->update_m_n || new_crtc_state->update_lrr)\n");
		}

		/* Whether the push was sent decides where the vblank starts. */
		push_sent = intel_vrr_is_push_sent(crtc_state);
		if (push_sent) {
			*vblank_start = intel_vrr_vmin_vblank_start(crtc_state);
		} else {
			*vblank_start = intel_vrr_vmax_vblank_start(crtc_state);
		}
	} else {
		*vblank_start = i915_mode_vblank_start(adjusted_mode);
	}

	/* The window ends one line before the vblank and starts the evasion time before it. */
	*min = *vblank_start - drv_i915_usecs_to_scanlines(adjusted_mode, VBLANK_EVASION_TIME_US);
	*max = *vblank_start - 1;

	/* A DSB, a seamless M/N or an LRR update also evades the undelayed vblank. */
	if (new_crtc_state->dsb || new_crtc_state->update_m_n || new_crtc_state->update_lrr)
		*min -= adjusted_mode->crtc_vblank_start - adjusted_mode->crtc_vdisplay;
}

/*
 * Starts an update of pipe registers that must land in one frame (the
 * Linux intel_pipe_update_start()).
 *
 * If the next vblank comes within the evasion time, the update sleeps
 * until it has passed, one vblank interrupt at a time for at most one
 * jiffy-rounded millisecond.  Interrupts are off from the return of this
 * function until drv_i915_pipe_update_end(); the interrupt section and the
 * vblank sleep are the named world's, on the crtc's device.
 */
void
drv_i915_pipe_update_start(
	struct i915_lcd_world *world,
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv;
	struct intel_crtc_state *new_crtc_state;
	long timeout;
	int scanline;
	int min;
	int max;
	int vblank_start;
	wait_queue_head_t *wq;
	bool need_vlv_dsi_wa;
	bool needs_vblank_work;
	int vblank_error;
	DEFINE_WAIT(wait);

	/* Finds the device, the new state and the pipe's vblank wait queue. */
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	timeout = msecs_to_jiffies_timeout(1);
	wq = drm_crtc_vblank_waitqueue(&crtc->base);

	/* VLV/CHV DSI needs the scanline workaround below. */
	need_vlv_dsi_wa = false;
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		need_vlv_dsi_wa = intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI);

	/* Holds PSR off for the update. */
	intel_psr_lock(new_crtc_state);

	/* An async flip only arms the event for the flip-done interrupt handler. */
	if (new_crtc_state->do_async_flip) {
		I915_LCD_SPIN_LOCK_IRQ(&crtc->base.dev->event_lock);

		crtc->flip_done_event = new_crtc_state->uapi.event;

		I915_LCD_SPIN_UNLOCK_IRQ(&crtc->base.dev->event_lock);

		/* The event now belongs to the flip-done handler. */
		new_crtc_state->uapi.event = NULL;
		return;
	}

	/* A colour update prepares its vblank work. */
	needs_vblank_work = i915_crtc_needs_vblank_work(new_crtc_state);
	if (needs_vblank_work)
		intel_crtc_vblank_work_init(new_crtc_state);

	/* Computes the window; without one the update just turns interrupts off. */
	drv_i915_crtc_vblank_evade_scanlines(state, crtc, &min, &max, &vblank_start);
	if (min <= 0 || max <= 0) {
		I915_LCD_LOCAL_IRQ_DISABLE(world, dev_priv);
		return;
	}

	/* Holds the pipe's vblank interrupt for the sleeps; without it the update does not sleep. */
	vblank_error = drm_crtc_vblank_get(&crtc->base);
	if (vblank_error != 0) {
		/* drm_WARN_ON(), with the Linux condition as its text. */
		drv_i915_lcd_error("WARN_ON(drm_crtc_vblank_get(&crtc->base))\n");
		I915_LCD_LOCAL_IRQ_DISABLE(world, dev_priv);
		return;
	}

	/*
	 * PSR idles out after the vblank interrupt is enabled: the interrupts
	 * start the PSR exit and prevent a re-entry.
	 */
	intel_psr_wait_for_idle_locked(new_crtc_state);

	/* The critical section starts: interrupts off. */
	I915_LCD_LOCAL_IRQ_DISABLE(world, dev_priv);

	/* Records the window for the diagnostics. */
	crtc->debug.min_vbl = min;
	crtc->debug.max_vbl = max;
	trace_intel_pipe_update_start(crtc);

	/*
	 * Sleeps out of the window: while the scanline is inside it, sleep
	 * until the next vblank interrupt with interrupts on, then look again.
	 */
	for (;;) {
		/* Waits on the pipe's vblank queue (only its address is passed here). */
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);

		/* Leaves the loop once the scanline is outside the window. */
		scanline = drv_i915_get_crtc_scanline(world, crtc);
		if (scanline < min || scanline > max)
			break;

		/* Gives up once the time is spent. */
		if (!timeout) {
			I915_LCD_DRM_ERR(&dev_priv->drm,
					 "Potential atomic update failure on pipe %c\n",
					 pipe_name(crtc->pipe));
			break;
		}

		/* Sleeps until the next vblank interrupt with interrupts on. */
		I915_LCD_LOCAL_IRQ_ENABLE(world, dev_priv);
		timeout = I915_LCD_SCHEDULE_TIMEOUT(dev_priv, crtc->pipe, timeout);
		I915_LCD_LOCAL_IRQ_DISABLE(world, dev_priv);
	}

	/* Stops waiting on the queue. */
	finish_wait(wq, &wait);

	/* The sleeps are over: the vblank reference goes back. */
	drm_crtc_vblank_put(&crtc->base);

	/*
	 * On VLV/CHV DSI the scanline counter increments about a third of a
	 * line before the vblank starts, while the registers latch at its
	 * start: the first vblank line is polled out.
	 */
	while (need_vlv_dsi_wa && scanline == vblank_start)
		scanline = drv_i915_get_crtc_scanline(world, crtc);

	/* Records where the update starts. */
	crtc->debug.scanline_start = scanline;
	crtc->debug.start_vbl_time = ktime_get();
	crtc->debug.start_vbl_count = i915_crtc_get_vblank_counter(crtc);
	trace_intel_pipe_update_vblank_evaded(crtc);
}

/*
 * Ends an update started with drv_i915_pipe_update_start() (the Linux
 * intel_pipe_update_end()).
 *
 * The update's event is armed for the next vblank (with a vblank
 * reference the event holds), interrupts go back on, and an update that
 * crossed a vblank is reported.
 */
void
drv_i915_pipe_update_end(
	struct i915_lcd_world *world,
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state;
	enum pipe pipe;
	int scanline_end;
	u32 end_vbl_count;
	ktime_t end_vbl_time;
	struct drm_i915_private *dev_priv;
	bool needs_vblank_work;
	bool dsi;
	int vblank_error;

	/* Samples where the update ends, then finds the device. */
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	pipe = crtc->pipe;
	scanline_end = drv_i915_get_crtc_scanline(world, crtc);
	end_vbl_count = i915_crtc_get_vblank_counter(crtc);
	end_vbl_time = ktime_get();
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* An async flip has nothing more to do. */
	if (new_crtc_state->do_async_flip) {
		intel_psr_unlock(new_crtc_state);
		return;
	}

	/* Traces the end of the update (no tracepoints here). */
	trace_intel_pipe_update_end(crtc, end_vbl_count, scanline_end);

	/* MIPI DSI command mode needs a frame update request for every commit. */
	if (I915_LCD_DISPLAY_VER(dev_priv) >= 11) {
		dsi = intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI);
		if (dsi)
			icl_dsi_frame_update(new_crtc_state);
	}

	/*
	 * Still inside the vblank-evade critical section, so this cannot race:
	 * a colour update schedules its vblank work, anything else arms its
	 * event for the next vblank with a vblank reference of its own.
	 */
	needs_vblank_work = i915_crtc_needs_vblank_work(new_crtc_state);
	if (needs_vblank_work) {
		drm_vblank_work_schedule(new_crtc_state, crtc, 1);
	} else if (new_crtc_state->uapi.event) {
		vblank_error = drm_crtc_vblank_get(&crtc->base);
		if (vblank_error != 0) {
			/* drm_WARN_ON(), with the Linux condition as its text. */
			drv_i915_lcd_error("WARN_ON(drm_crtc_vblank_get(&crtc->base) != 0)\n");
		}

		/* Arms the event for the next vblank. */
		I915_LCD_SPIN_LOCK(&crtc->base.dev->event_lock);

		drm_crtc_arm_vblank_event(&crtc->base, new_crtc_state->uapi.event);

		I915_LCD_SPIN_UNLOCK(&crtc->base.dev->event_lock);

		/* The event now belongs to the vblank. */
		new_crtc_state->uapi.event = NULL;
	}

	/*
	 * The VRR push terminates the vblank; it is sent after the frame
	 * counter was sampled, so the sample is the current frame's.
	 */
	intel_vrr_send_push(new_crtc_state);

	/* The critical section ends: interrupts on. */
	I915_LCD_LOCAL_IRQ_ENABLE(world, dev_priv);

	/* Reports an update that crossed a vblank. */
	if (!intel_vgpu_active(dev_priv)) {
		if (crtc->debug.start_vbl_count &&
		    crtc->debug.start_vbl_count != end_vbl_count) {
			I915_LCD_DRM_ERR(&dev_priv->drm,
					 "Atomic update failure on pipe %c (start=%u end=%u) time %lld us, min %d, max %d, scanline start %d, end %d\n",
					 pipe_name(pipe),
					 crtc->debug.start_vbl_count,
					 end_vbl_count,
					 ktime_us_delta(end_vbl_time, crtc->debug.start_vbl_time),
					 crtc->debug.min_vbl,
					 crtc->debug.max_vbl,
					 crtc->debug.scanline_start,
					 scanline_end);
		}

		/* The evasion statistics (empty in the non-debug build). */
		dbg_vblank_evade(crtc, end_vbl_time);
	}

	/* Lets PSR back in. */
	intel_psr_unlock(new_crtc_state);
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

/*
 * Runs one part of the crtc enable (the Linux hsw_crtc_enable(), cut into
 * the parts a screen's light runs as steps; see
 * drv_i915_lcd_ms_crtc_enable_part()).
 */
static void
i915_hsw_crtc_enable_part(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc,
	unsigned part)
{
	const struct intel_crtc_state *new_crtc_state;
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	bool warned;
	bool dsi;

	/* Finds the new state, the device and the transcoder. */
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	cpu_transcoder = new_crtc_state->cpu_transcoder;

	/* The parts in the order of the Linux text. */
	switch (part) {
	case I915_LCD_CRTC_PART_DMC:
		/* A crtc that runs already is not enabled again; the pipe's DMC event handlers. */
		warned = I915_LCD_DRM_WARN_ON(&dev_priv->drm, crtc->active);
		if (warned)
			return;
		drv_i915_dmc_enable_pipe(dev_priv, crtc->pipe);
		break;
	case I915_LCD_CRTC_PART_LINK:
		/* The encoders before the PLL (no big joiner on this path). */
		drv_i915_encoders_pre_pll_enable(state, crtc);
		break;
	case I915_LCD_CRTC_PART_PLL:
		/* The shared PLL, when the crtc uses one. */
		if (new_crtc_state->shared_dpll)
			drv_i915_enable_shared_dpll(new_crtc_state);
		break;
	case I915_LCD_CRTC_PART_PORT:
		/* The encoders' pre-enable; DSC and the uncompressed joiner (display version 13). */
		drv_i915_encoders_pre_enable(state, crtc);
		intel_dsc_enable(new_crtc_state);
		if (I915_LCD_DISPLAY_VER(dev_priv) >= 13)
			intel_uncompressed_joiner_enable(new_crtc_state);
		break;
	case I915_LCD_CRTC_PART_PIPE:
		/* The pipe source size, the pipe misc word and the CPU transcoder (not a DSI one). */
		i915_set_pipe_src_size(new_crtc_state);
		i915_bdw_set_pipe_misc(new_crtc_state);
		dsi = transcoder_is_dsi(cpu_transcoder);
		if (!dsi)
			i915_hsw_configure_cpu_transcoder(new_crtc_state);

		/* From here the crtc counts as running. */
		crtc->active = true;

		/* The panel fitter, the colour (loaded while the pipe's clocks run), the line time and the chicken bits. */
		skl_pfit_enable(new_crtc_state);
		drv_i915_color_load_luts(new_crtc_state);
		drv_i915_color_commit_noarm(new_crtc_state);
		drv_i915_color_commit_arm(new_crtc_state);
		i915_hsw_set_linetime_wm(new_crtc_state);
		i915_icl_set_pipe_chicken(new_crtc_state);

		/* The initial watermarks (skl_wm_funcs sets none: nothing happens). */
		intel_initial_watermarks(state, crtc);
		break;
	case I915_LCD_CRTC_PART_OUTPUT:
		/* The encoders' enable. */
		drv_i915_encoders_enable(state, crtc);
		break;
	default:
		break;
	}
}

/* Tells whether only HDR planes are active on the pipe (the Linux is_hdr_mode()). */
static bool
i915_is_hdr_mode(
	const struct intel_crtc_state *crtc_state)
{
	u8 hdr_planes;

	/* The planes that have the HDR pipeline. */
	hdr_planes = drv_i915_icl_hdr_plane_mask();

	/* An active plane that is neither HDR nor the cursor rules HDR mode out. */
	if ((crtc_state->active_planes & ~(hdr_planes | BIT(PLANE_CURSOR))) != 0)
		return false;

	/* Succeeded: every active plane is an HDR plane or the cursor. */
	return true;
}

/* Writes the pipe misc word of a crtc state through the xd pipe. */
static void
i915_bdw_set_pipe_misc(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	struct i915_xd_pipe_misc misc;
	bool hdr;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* The depth, the dither and the pixel format. */
	kern_memset(&misc, 0, sizeof(misc));
	misc.bpp = (unsigned)crtc_state->pipe_bpp;
	misc.dither = crtc_state->dither ? 1 : 0;
	misc.output = I915_XD_OUTPUT_RGB;
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		misc.output = I915_XD_OUTPUT_YCBCR444;
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		misc.output = I915_XD_OUTPUT_YCBCR420;

	/* HDR precision when only HDR planes are active. */
	hdr = i915_is_hdr_mode(crtc_state);
	misc.hdr = hdr ? 1 : 0;

	/* Writes the word. */
	drv_i915_xd_pipe_misc(drv_i915_lcd_xd_io(dev_priv), drv_i915_lcd_xd_platform(), (unsigned)crtc->pipe, &misc);
}

/* Sets the pipe chicken bits through the xd pipe. */
static void
i915_icl_set_pipe_chicken(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* Writes the bits. */
	drv_i915_xd_pipe_chicken(drv_i915_lcd_xd_io(dev_priv), drv_i915_lcd_xd_platform(), (unsigned)crtc->pipe);
}

/* Writes the line time watermark of the pipe (the Linux hsw_set_linetime_wm()). */
static void
i915_hsw_set_linetime_wm(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* Writes the line time and the IPS line time. */
	i915_lcd_intel_de_write(dev_priv,
				WM_LINETIME(crtc->pipe),
				HSW_LINETIME(crtc_state->linetime) |
				HSW_IPS_LINETIME(crtc_state->ips_linetime));
}

/*
 * Runs one part of the crtc disable (the Linux hsw_crtc_disable(), cut
 * into the parts a screen's dark runs as steps; see
 * drv_i915_lcd_ms_crtc_disable_part()).  No big joiner on this path.
 */
static void
i915_hsw_crtc_disable_part(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc,
	unsigned part)
{
	const struct intel_crtc_state *old_crtc_state;
	struct drm_i915_private *i915;

	/* Finds the old state and the device. */
	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	i915 = i915_lcd_to_i915(crtc->base.dev);

	/* The parts in the order of the Linux text. */
	switch (part) {
	case I915_LCD_CRTC_PART_OUTPUT:
		/* The encoders' disable. */
		drv_i915_encoders_disable(state, crtc);
		break;
	case I915_LCD_CRTC_PART_PORT:
		/* The encoders' post-disable: the transcoder stops, then the port. */
		drv_i915_encoders_post_disable(state, crtc);
		break;
	case I915_LCD_CRTC_PART_PLL:
		/* The shared PLL. */
		drv_i915_disable_shared_dpll(old_crtc_state);
		break;
	case I915_LCD_CRTC_PART_LINK:
		/* The encoders after the PLL. */
		drv_i915_encoders_post_pll_disable(state, crtc);
		break;
	case I915_LCD_CRTC_PART_DMC:
		/* The pipe's DMC event handlers. */
		drv_i915_dmc_disable_pipe(i915, crtc->pipe);
		break;
	default:
		break;
	}
}

/*
 * Computes the power domains a crtc state needs (the Linux
 * get_crtc_power_domains()).
 *
 * The pipe, its transcoder, the panel fitter, the encoders of the state
 * (the named world's only encoder), audio, the display core for a shared
 * PLL, and DSC.  Nothing for an inactive state.
 */
static void
i915_get_crtc_power_domains(
	struct i915_lcd_world *world,
	struct intel_crtc_state *crtc_state,
	struct intel_power_domain_mask *mask)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	struct drm_encoder *encoder;
	struct intel_encoder *intel_encoder;
	enum pipe pipe;

	/* Finds the crtc, its device, its transcoder and its pipe. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	cpu_transcoder = crtc_state->cpu_transcoder;
	pipe = crtc->pipe;

	/* Starts from no domain. */
	i915_bitmap_zero(mask->bits, POWER_DOMAIN_NUM);

	/* An inactive state needs nothing. */
	if (!crtc_state->hw.active)
		return;

	/* The pipe and its transcoder. */
	i915_set_bit(POWER_DOMAIN_PIPE(pipe), mask->bits);
	i915_set_bit(POWER_DOMAIN_TRANSCODER(cpu_transcoder), mask->bits);

	/* The panel fitter, when it scales or is forced through. */
	if (crtc_state->pch_pfit.enabled ||
	    crtc_state->pch_pfit.force_thru)
		i915_set_bit(POWER_DOMAIN_PIPE_PANEL_FITTER(pipe), mask->bits);

	/* The power domain of every encoder of the state. */
	I915_LCD_DRM_FOR_EACH_ENCODER_MASK(world->i915_lcd_only_encoder, encoder, crtc_state->uapi.encoder_mask) {
		intel_encoder = to_intel_encoder(encoder);
		i915_set_bit(intel_encoder->power_domain, mask->bits);
	}

	/* Audio. */
	if (HAS_DDI(dev_priv) && crtc_state->has_audio)
		i915_set_bit(POWER_DOMAIN_AUDIO_MMIO, mask->bits);

	/* A shared PLL needs the display core. */
	if (crtc_state->shared_dpll)
		i915_set_bit(POWER_DOMAIN_DISPLAY_CORE, mask->bits);

	/* DSC. */
	if (crtc_state->dsc.compression_enable)
		i915_set_bit(intel_dsc_power_domain(crtc, cpu_transcoder), mask->bits);
}

/*
 * Reads the frame counter of a running crtc (the Linux
 * intel_crtc_get_vblank_counter()).
 *
 * 0 while the crtc is off.  Without a counter range the software count
 * would be used (not reached: the range is set by the active timings).
 */
static u32
i915_crtc_get_vblank_counter(
	struct intel_crtc *crtc)
{
	struct drm_device *dev;
	struct drm_vblank_crtc *vblank;
	u32 frame;

	/* Finds the pipe's vblank object. */
	dev = crtc->base.dev;
	vblank = &dev->vblank[drm_crtc_index(&crtc->base)];

	/* A crtc that is off counts no frames. */
	if (!crtc->active)
		return 0;

	/* Without a counter range the software count answers. */
	if (!vblank->max_vblank_count) {
		frame = (u32)drm_crtc_accurate_vblank_count(&crtc->base);
		return frame;
	}

	/* Reads the counter through the crtc's hook. */
	frame = crtc->base.funcs->get_vblank_counter(&crtc->base);

	/* Succeeded: reports the frame number. */
	return frame;
}

/*
 * Tells whether an update needs vblank work (the Linux
 * intel_crtc_needs_vblank_work()): an active pipe without a modeset, whose
 * colour update is neither preloaded nor carried by a DSB.
 */
static bool
i915_crtc_needs_vblank_work(
	const struct intel_crtc_state *crtc_state)
{
	bool needs_modeset;

	/* An inactive pipe has no vblank. */
	if (!crtc_state->hw.active)
		return false;

	/* A modeset loads the colour state itself. */
	needs_modeset = intel_crtc_needs_modeset(crtc_state);
	if (needs_modeset)
		return false;

	/* Preloaded LUTs need no work. */
	if (crtc_state->preload_luts)
		return false;

	/* Only a colour update needs work. */
	if (!intel_crtc_needs_color_update(crtc_state))
		return false;

	/* A DSB carries the update itself. */
	if (intel_color_uses_dsb(crtc_state))
		return false;

	/* Succeeded: the colour update is done by vblank work. */
	return true;
}

/* Returns the vblank start line of a mode, per field when interlaced (the Linux intel_mode_vblank_start()). */
static int
i915_mode_vblank_start(
	const struct drm_display_mode *mode)
{
	int vblank_start;

	/* The vblank start line of the frame. */
	vblank_start = mode->crtc_vblank_start;

	/* An interlaced mode counts lines per field. */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	/* Succeeded: reports the line. */
	return vblank_start;
}
