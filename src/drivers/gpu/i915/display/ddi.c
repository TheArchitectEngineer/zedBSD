/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_ddi.c),
 * which carries the following notice.
 *
 * Copyright © 2012 Intel Corporation
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
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 */

/*
 * The DDI port of the one-screen modeset path (see ddi.h).
 *
 * The functions follow the Linux 6.8.12 intel_ddi.c text for a combo-PHY
 * DDI on display version 12 and later: the transcoder function words, the
 * DP and HDMI enable and disable of the port (the encoder hooks the crtc
 * enable and disable reach through the drv_i915_encoders_*() walks), the
 * port clock and the power references the port holds, the voltage swing
 * programming of the combo PHY, the DP link-training hooks of the port,
 * and the readout of a port the firmware left running.
 *
 * The encoder hooks have the Linux signatures, which carry no world.  The
 * Linux text reached two things through file-scope pointers: the device
 * of the current entry point (for its sleeps, waits, locks, panel
 * operations, DPCD accesses and named steps) and the modeset object the
 * DDI hooks are bound to.  Every encoder these hooks are bound to is the
 * encoder of a modeset object, so both are found from the encoder: its
 * modeset object's world records the entry point's device
 * (i915_lcd_cur_i915) and the bound object (ddi_ms).  Where the Linux
 * text used the device of the object itself (the register accesses and
 * the power references), that device is used.
 *
 * Many macros of the environment answer for the supported configuration
 * without evaluating the device or object the Linux text hands them (the
 * platform predicates, the warnings, the guards of unported callees).  A
 * local that only such macros name is declared __maybe_unused; the Linux
 * text is kept as it was.
 */

#include "internal.h"
#include "modeset-internal.h"
#include "takeover-internal.h"
#include "../intel/trans.h"
#include "../intel/dp.h"
#include "ddi.h"
#include "dp.h"
#include "xd-ddi.h"
#include "xd-pipe.h"
#include "xd-tc.h"

#include "../intel/display-regs-xd.h"
#include "pipe.h"
#include <kern/kcrt.h>

/*
 * ==== Types ====
 */

/*
 * One encoder hook of the enable and disable sequences, as struct
 * intel_encoder carries it (pre_pll_enable, pre_enable, enable, disable,
 * post_disable, post_pll_disable).
 */
typedef void (*i915_ddi_encoder_hook_t)(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);

/* Which disable hook a walk over the encoders on a crtc runs. */
enum i915_ddi_disable_hook {
	I915_DDI_HOOK_DISABLE,
	I915_DDI_HOOK_POST_DISABLE,
	I915_DDI_HOOK_POST_PLL_DISABLE
};

/*
 * ==== Forward declarations ====
 */

static void i915_ddi_init_dp_buf_reg(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_ddi_xd_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, struct i915_xd_trans_func *func);
static void i915_ddi_enable_transcoder_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static enum transcoder i915_tgl_dp_tp_transcoder(const struct intel_crtc_state *crtc_state);
static int i915_icl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_jsl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_tgl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static void i915_ddi_xd_link(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, struct i915_xd_ddi_link *link);

/*
 * ==== Public functions ====
 */

/*
 * Computes the lowest voltage level the port clock of a crtc state needs.
 *
 * The Linux intel_ddi_compute_min_voltage_level(): the result is stored in
 * the state (min_voltage_level); a version without a rule leaves it alone.
 */
void
drv_i915_ddi_compute_min_voltage_level(
	struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv __maybe_unused;
	int display_ver;

	/* Finds the device of the crtc and its display version. */
	dev_priv = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);

	/* Takes the rule of the platform's display version. */
	if (display_ver >= 14) {
		crtc_state->min_voltage_level = i915_icl_ddi_min_voltage_level(crtc_state);
	} else if (display_ver >= 12) {
		crtc_state->min_voltage_level = i915_tgl_ddi_min_voltage_level(crtc_state);
	} else if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv)) {
		crtc_state->min_voltage_level = i915_jsl_ddi_min_voltage_level(crtc_state);
	} else if (display_ver >= 11) {
		crtc_state->min_voltage_level = i915_icl_ddi_min_voltage_level(crtc_state);
	}
}

/*
 * Records the DDI words of a DP output without a modeset object.
 *
 * Builds the device, crtc state, encoder and digital port the Linux DDI
 * text reads and runs it on the given backend (normally a word recorder):
 * intel_ddi_init_dp_buf_reg() yields the DDI_BUF_CTL value (reported in
 * *ddi_buf_ctl_value, not written), intel_ddi_set_dp_msa() writes
 * TRANS_MSA_MISC and intel_ddi_enable_transcoder_func() writes
 * TRANS_DDI_FUNC_CTL2 and TRANS_DDI_FUNC_CTL.  saved_port_bits is the
 * caller's: intel_ddi_init() takes it from the DDI_BUF_CTL readout masked
 * with DDI_BUF_PORT_REVERSAL plus the VBT's lane-reversal flag.  The
 * device and the crtc state are the world's recorder objects, cleared at
 * each call.  Always succeeds (0).
 */
int
drv_i915_ddi_emit(
	struct i915_lcd_world *world,
	struct i915_lcd_emit *emit,
	const struct drm_display_mode *mode,
	int port,
	int pipe,
	int cpu_transcoder,
	int port_clock,
	int lanes,
	int pipe_bpp,
	u32 saved_port_bits,
	u32 *ddi_buf_ctl_value)
{
	struct drm_i915_private *i915;
	struct intel_crtc_state *crtc_state;
	struct intel_crtc crtc;
	struct intel_digital_port dig_port;
	struct drm_connector_state conn_state;

	/* Clears the recorder's device and crtc state and the local objects. */
	i915 = &world->i915_ddi_emit_i915;
	crtc_state = &world->i915_ddi_emit_crtc_state;
	kern_memset(i915, 0, sizeof(*i915));
	kern_memset(&crtc, 0, sizeof(crtc));
	kern_memset(crtc_state, 0, sizeof(*crtc_state));
	kern_memset(&dig_port, 0, sizeof(dig_port));
	kern_memset(&conn_state, 0, sizeof(conn_state));

	/* Binds the device to the backend and the crtc to the device. */
	i915->emit = emit;
	crtc.base.dev = &i915->drm;
	crtc.pipe = (enum pipe)pipe;

	/* Fills the crtc state of an eDP modeset of the given mode and link. */
	crtc_state->uapi.crtc = &crtc.base;
	crtc_state->uapi.mode_changed = true;
	crtc_state->cpu_transcoder = cpu_transcoder;
	crtc_state->master_transcoder = INVALID_TRANSCODER;
	crtc_state->mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state->hw.adjusted_mode = *mode;
	crtc_state->output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state->output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state->port_clock = port_clock;
	crtc_state->lane_count = lanes;
	crtc_state->pipe_bpp = pipe_bpp;

	/* Fills the port the words describe. */
	dig_port.base.base.dev = &i915->drm;
	dig_port.base.port = (enum port)port;
	dig_port.saved_port_bits = saved_port_bits;
	conn_state.colorspace = MODE_COLORIMETRY_DEFAULT;

	/* Computes the DDI_BUF_CTL value and hands it to the caller. */
	i915_ddi_init_dp_buf_reg(&dig_port.base, crtc_state);
	*ddi_buf_ctl_value = dig_port.dp.DP;

	/* Writes TRANS_MSA_MISC. */
	i915_ddi_set_dp_msa(crtc_state, &conn_state);

	/* Writes TRANS_DDI_FUNC_CTL2 and TRANS_DDI_FUNC_CTL. */
	i915_ddi_enable_transcoder_func(&dig_port.base, crtc_state);

	/* Succeeded: the words are recorded. */
	return 0;
}

/*
 * Reports the VBT's HDMI level shift of the port the DDI hooks are bound to.
 *
 * The Linux intel_bios_hdmi_level_shift(): the VBT child's value as the
 * parser read it.  0 is a valid index; only a VBT without the field, or no
 * bound modeset object, gives -1, and intel_ddi_hdmi_level() then falls
 * back to the buffer-translation table's own default entry.
 */
int
drv_i915_lcd_hdmi_level_shift(
	struct i915_lcd_world *world)
{
	/* No modeset object is bound. */
	if (world->ddi_ms == NULL)
		return -1;

	/* Succeeded: reports the bound object's value. */
	return world->ddi_ms->hdmi_level_shift;
}

/*
 * Reports the port the DDI hooks are bound to, for diagnostics.
 *
 * -1 when no modeset object is bound.
 */
int
drv_i915_lcd_ms_bound_port(
	struct i915_lcd_world *world)
{
	/* No modeset object is bound. */
	if (world->ddi_ms == NULL)
		return -1;

	/* Succeeded: reports the bound object's port. */
	return (int)world->ddi_ms->dig_port.base.port;
}

/*
 * Binds the one encoder of a modeset object to the DDI hooks.
 *
 * The hooks are bound as intel_ddi_init() and intel_ddi_init_dp_connector()
 * bind them (drv_i915_lcd_ms_bind_port_hooks()).  The object becomes the
 * bound object of its world, and its encoder the only encoder the modeset
 * text walks.  The object's world is the one prepare recorded in it.
 */
void
drv_i915_lcd_ms_bind_encoder(
	struct i915_lcd_modeset *ms)
{
	struct i915_lcd_world *world;
	struct intel_encoder *encoder;

	/* Finds the world the object belongs to and its encoder. */
	world = ms->world;
	encoder = &ms->dig_port.base;

	/*
	 * The object is the one the DDI hooks and the level shift answer for
	 * from now on, and its commit state names the world, so that the
	 * encoder walks of its crtc enable and disable find the object.
	 */
	world->ddi_ms = ms;
	ms->state.world = world;

	/* The encoder is the first and only one of the device, and the walk over the encoders of a mask visits it. */
	encoder->base.index = 0;
	world->i915_lcd_only_encoder = &encoder->base;

	/* Binds the port's hooks. */
	drv_i915_lcd_ms_bind_port_hooks(ms);
}

/*
 * Binds what a modeset object's DDI port keeps of intel_ddi_init() on
 * display version 12 and later: the power domain of the port's lanes,
 * without making the object its world's bound one.
 *
 * The takeover binds the Type-C ports' readout encoders this way: a crtc the
 * firmware left on such a port is stopped through that port's own hooks.
 */
void
drv_i915_lcd_ms_bind_port_hooks(
	struct i915_lcd_modeset *ms)
{
	struct intel_encoder *encoder;
	struct drm_i915_private *i915;
	enum phy phy;
	bool is_tc;

	/* Finds the object's encoder. */
	encoder = &ms->dig_port.base;

	/*
	 * intel_ddi_init(): encoder->power_domain =
	 * intel_display_power_ddi_lanes_domain(): the port's DDI lanes, A to
	 * C on the combo PHYs and TC1 to TC4 on the Type-C PHYs
	 * (d13_port_domains[]).
	 */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);
	is_tc = drv_i915_lcd_intel_phy_is_tc(i915, phy);
	if (is_tc) {
		encoder->power_domain = POWER_DOMAIN_PORT_DDI_LANES_TC1 + ((int)encoder->port - (int)PORT_TC1);
	} else {
		encoder->power_domain = POWER_DOMAIN_PORT_DDI_LANES_A + (int)encoder->port;
	}
}

/*
 * ==== Static functions ====
 */

/*
 * Computes the DDI_BUF_CTL value of a DP port into intel_dp->DP through
 * the xd DDI (the port's kept bits, the width, a display 13 Type-C port's
 * PHY link rate); a Type-C port outside Thunderbolt-alt mode shows the
 * PHY ownership in it, which the buffer keeps from the Type-C code.
 */
static void
i915_ddi_init_dp_buf_reg(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 __maybe_unused;
	struct intel_dp *intel_dp;
	struct intel_digital_port *dig_port;
	struct i915_xd_ddi_link link;

	/* Finds the device, the DP half and the port of the encoder. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	intel_dp = enc_to_intel_dp(encoder);
	dig_port = i915_lcd_enc_to_dig_port(encoder);

	/* The word; the enable is set by the link's start. */
	i915_ddi_xd_link(encoder, crtc_state, &link);
	intel_dp->DP = drv_i915_xd_ddi_dp_word(drv_i915_lcd_xd_platform(), &link);

	/* A display 13 Type-C port outside Thunderbolt-alt mode shows the PHY ownership. */
	if (link.tc && I915_LCD_IS_ALDERLAKE_P(i915) && !i915_lcd_intel_tc_port_in_tbt_alt_mode(dig_port))
		intel_dp->DP |= DDI_BUF_CTL_TC_PHY_OWNERSHIP;
}

/* Writes the MSA MISC word of a DP transcoder through the xd pipe. */
static void
i915_ddi_set_dp_msa(
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	struct i915_xd_msa msa;
	bool has_dp_encoder;
	bool needs_vsc_sdp;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* Only a DP transcoder has an MSA. */
	has_dp_encoder = intel_crtc_has_dp_encoder(crtc_state);
	if (!has_dp_encoder)
		return;

	/* The depth and the colour format of the stream. */
	kern_memset(&msa, 0, sizeof(msa));
	msa.bpp = (unsigned)crtc_state->pipe_bpp;
	msa.cea_rgb = crtc_state->limited_color_range ? 1 : 0;
	msa.ycbcr444 = crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444 ? 1 : 0;
	needs_vsc_sdp = drv_i915_dp_needs_vsc_sdp(crtc_state, conn_state);
	msa.vsc_sdp = needs_vsc_sdp ? 1 : 0;

	/* Writes the word. */
	drv_i915_xd_pipe_msa(drv_i915_lcd_xd_io(dev_priv), (unsigned)crtc_state->cpu_transcoder, &msa);
}

/* Describes the transcoder function of a crtc state to the xd pipe. */
static void
i915_ddi_xd_func(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	struct i915_xd_trans_func *func)
{
	bool has_hdmi;
	bool has_dp_mst;

	/* The DDI, the depth, the sync polarities and the lanes. */
	kern_memset(func, 0, sizeof(*func));
	func->ddi = (unsigned)encoder->port;
	func->bpp = (unsigned)crtc_state->pipe_bpp;
	func->vsync_high = (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PVSYNC) != 0 ? 1 : 0;
	func->hsync_high = (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PHSYNC) != 0 ? 1 : 0;
	func->lanes = (unsigned)crtc_state->lane_count;
	func->sync_master = I915_XD_NO_TRANSCODER;
	if (crtc_state->master_transcoder != INVALID_TRANSCODER)
		func->sync_master = (unsigned)crtc_state->master_transcoder;

	/* HDMI or DVI with its scrambling and clock ratio. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (has_hdmi) {
		func->mode = crtc_state->has_hdmi_sink ? I915_XD_FUNC_HDMI : I915_XD_FUNC_DVI;
		func->scrambling = crtc_state->hdmi_scrambling ? 1 : 0;
		func->high_tmds = crtc_state->hdmi_high_tmds_clock_ratio ? 1 : 0;
		return;
	}

	/* DP multi-stream with its master, or single stream. */
	has_dp_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);
	if (has_dp_mst) {
		func->mode = I915_XD_FUNC_DP_MST;
		(void)I915_LCD_DRM_WARN_ON(NULL, crtc_state->mst_master_transcoder == INVALID_TRANSCODER);
		func->mst_master = (unsigned)crtc_state->mst_master_transcoder;
		return;
	}

	/* Succeeded: single stream. */
	func->mode = I915_XD_FUNC_DP_SST;
}

/* Writes and enables the transcoder function of a crtc state through the xd pipe. */
static void
i915_ddi_enable_transcoder_func(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	struct i915_xd_trans_func func;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* The port sync word, then the function with its enable. */
	i915_ddi_xd_func(encoder, crtc_state, &func);
	drv_i915_xd_pipe_func_enable(drv_i915_lcd_xd_io(dev_priv), (unsigned)crtc_state->cpu_transcoder, &func);
}

/* Selects the transcoder whose DP_TP registers carry the link (the Linux tgl_dp_tp_transcoder()). */
static enum transcoder
i915_tgl_dp_tp_transcoder(
	const struct intel_crtc_state *crtc_state)
{
	bool has_dp_mst;

	/* An MST stream uses the master transcoder's. */
	has_dp_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);
	if (has_dp_mst)
		return crtc_state->mst_master_transcoder;

	/* Succeeded: an SST stream uses its own transcoder's. */
	return crtc_state->cpu_transcoder;
}

/* The minimum voltage level of Ice Lake and display 14 (the Linux icl_ddi_min_voltage_level()). */
static int
i915_icl_ddi_min_voltage_level(
	const struct intel_crtc_state *crtc_state)
{
	/* A port clock above 594 MHz needs level 1. */
	if (crtc_state->port_clock > 594000)
		return 1;

	/* Succeeded: lower clocks need no minimum. */
	return 0;
}

/* The minimum voltage level of Jasper Lake and Elkhart Lake (the Linux jsl_ddi_min_voltage_level()). */
static int
i915_jsl_ddi_min_voltage_level(
	const struct intel_crtc_state *crtc_state)
{
	/* A port clock above 594 MHz needs level 3. */
	if (crtc_state->port_clock > 594000)
		return 3;

	/* Succeeded: lower clocks need no minimum. */
	return 0;
}

/* The minimum voltage level of display 12 and 13 (the Linux tgl_ddi_min_voltage_level()). */
static int
i915_tgl_ddi_min_voltage_level(
	const struct intel_crtc_state *crtc_state)
{
	/* A port clock above 594 MHz needs level 2. */
	if (crtc_state->port_clock > 594000)
		return 2;

	/* Succeeded: lower clocks need no minimum. */
	return 0;
}

/*
 * Describes a port's link to the xd DDI: its DDI, the transcoder whose
 * transport it uses, whether its PHY is Type-C, the lane count and rate,
 * the bits the port keeps, and the stream's framing and FEC.
 */
static void
i915_ddi_xd_link(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	struct i915_xd_ddi_link *link)
{
	struct drm_i915_private *i915;
	struct intel_digital_port *dig_port;
	enum phy phy;

	/* The port and its PHY. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);

	/* The link. */
	link->ddi = (unsigned)encoder->port;
	link->trans = (unsigned)i915_tgl_dp_tp_transcoder(crtc_state);
	link->tc = drv_i915_lcd_intel_phy_is_tc(i915, phy) ? 1 : 0;
	link->lanes = (unsigned)crtc_state->lane_count;
	link->rate_khz = (uint32_t)crtc_state->port_clock;
	link->kept = dig_port->saved_port_bits;
	link->mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST) ? 1 : 0;
	link->enhanced = crtc_state->enhanced_framing ? 1 : 0;
	link->fec = crtc_state->fec_enable ? 1 : 0;
}
