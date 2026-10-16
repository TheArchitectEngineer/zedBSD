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
 * ==== File-scope constants ====
 */

/*
 * The DP voltage swing and pre-emphasis pairs, in the order of the entries
 * of a buffer-translation table (the Linux index_to_dp_signal_levels[]).
 *
 * A signal level the sink requests is translated to the table entry of the
 * same index; the number of entries of the port's table bounds the swing
 * the port reports as its maximum.  Constant for the life of the kernel.
 */
static const u8 index_to_dp_signal_levels[] = {
	[0] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[1] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[2] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[3] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_3,
	[4] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[5] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[6] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[7] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[8] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[9] = DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0,
};

/*
 * ==== Forward declarations ====
 */

static struct i915_lcd_modeset *i915_ddi_modeset(const struct intel_encoder *encoder);
static void i915_ddi_init_dp_buf_reg(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_ddi_xd_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, struct i915_xd_trans_func *func);
static void i915_ddi_enable_transcoder_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static u8 i915_ddi_dp_voltage_max(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
static int i915_ddi_dp_level(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, int lane);
static int i915_ddi_level(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int lane);
static int i915_translate_signal_level(struct intel_dp *intel_dp, u8 signal_levels);
static enum transcoder i915_tgl_dp_tp_transcoder(const struct intel_crtc_state *crtc_state);
static int i915_icl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_jsl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_tgl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_ddi_hdmi_level(struct intel_encoder *encoder, const struct intel_ddi_buf_trans *trans);
static unsigned i915_combo_output_kind(const struct intel_crtc_state *crtc_state);
static const struct i915_xd_combo_swing_table *i915_combo_swing_table(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static const struct intel_ddi_buf_trans *i915_combo_get_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_intel_get_buf_trans(const struct intel_ddi_buf_trans *trans, int *num_entries);
static const struct intel_ddi_buf_trans *i915_tgl_get_dkl_buf_trans_dp(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_tgl_get_dkl_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_adlp_get_dkl_buf_trans_dp(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_adlp_get_dkl_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static void i915_ddi_bind_buf_trans(struct intel_encoder *encoder);
static void i915_ddi_xd_link(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, struct i915_xd_ddi_link *link);
static void i915_icl_program_mg_dp_mode(struct intel_digital_port *dig_port, const struct intel_crtc_state *crtc_state);
static void i915_adlp_tbt_to_dp_alt_switch_wa(struct intel_encoder *encoder);
static void i915_tgl_dkl_phy_set_signal_levels(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);

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
 * Binds the encoder hooks of a modeset object's DDI port, as
 * intel_ddi_init() and intel_ddi_init_dp_connector() bind them on display
 * version 12 and later (the `encoder->... =` and `dig_port->dp.... =`
 * assignments of intel_ddi.c; the buffer translations as
 * intel_ddi_buf_trans_init() binds them), without making the object its
 * world's bound one.
 *
 * The takeover binds the Type-C ports' readout encoders this way: a crtc the
 * firmware left on such a port is stopped through that port's own hooks.
 */
void
drv_i915_lcd_ms_bind_port_hooks(
	struct i915_lcd_modeset *ms)
{
	struct intel_encoder *encoder;
	struct intel_dp *intel_dp;
	struct drm_i915_private *i915;
	enum phy phy;
	bool is_tc;

	/* Finds the object's encoder and its DP half. */
	encoder = &ms->dig_port.base;
	intel_dp = &ms->dig_port.dp;

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

	/* A Type-C PHY's DKL transmitters, which the xd output reaches through its Type-C hooks. */
	if (is_tc)
		encoder->set_signal_levels = i915_tgl_dkl_phy_set_signal_levels;

	/* Binds the buffer-translation table of the port, and the highest swing it reaches. */
	i915_ddi_bind_buf_trans(encoder);
	intel_dp->voltage_max = i915_ddi_dp_voltage_max;
}

/*
 * Programs the DisplayPort mode of a modeset object's Type-C PHY for its
 * link's lanes (the xd output's Type-C hook).
 */
void
drv_i915_lcd_ms_tc_dp_mode(
	struct i915_lcd_modeset *ms)
{
	/* The PHY's two lanes. */
	i915_icl_program_mg_dp_mode(&ms->dig_port, &ms->crtc_state);
}

/*
 * Releases the core reset of a modeset object's Type-C PHY before its DDI
 * buffer starts (the xd output's Type-C hook).
 */
void
drv_i915_lcd_ms_tc_phy_wa(
	struct i915_lcd_modeset *ms)
{
	/* The PHY's two lanes. */
	i915_adlp_tbt_to_dp_alt_switch_wa(&ms->dig_port.base);
}

/*
 * ==== Static functions ====
 */

/* Finds the modeset object whose encoder this is (every hooked encoder is one). */
static struct i915_lcd_modeset *
i915_ddi_modeset(
	const struct intel_encoder *encoder)
{
	struct i915_lcd_modeset *ms;

	/* The encoder is the base of the object's digital port. */
	ms = container_of(encoder, struct i915_lcd_modeset, dig_port.base);

	/* Succeeded: reports the object. */
	return ms;
}

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

/* Reports the highest voltage swing the port's buffer table reaches (the Linux intel_ddi_dp_voltage_max()). */
static u8
i915_ddi_dp_voltage_max(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder;
	struct drm_i915_private *dev_priv __maybe_unused;
	int n_entries;
	bool warned;

	/* Finds the port's encoder and its device. */
	encoder = &i915_lcd_dp_to_dig_port(intel_dp)->base;
	dev_priv = i915_lcd_to_i915(encoder->base.dev);

	/* Asks the port for the number of entries of its buffer table. */
	encoder->get_buf_trans(encoder, crtc_state, &n_entries);

	/* A table without an entry is taken as one entry. */
	warned = I915_LCD_DRM_WARN_ON(&dev_priv->drm, n_entries < 1);
	if (warned)
		n_entries = 1;

	/*
	 * A table longer than the signal-level list is taken as the list's
	 * length.  The warning text is the Linux drm_WARN_ON()'s; the comparison
	 * casts the list length so that it is not an unsigned comparison.
	 */
	warned = I915_LCD_DRM_WARN(&dev_priv->drm,
				   n_entries > (int)ARRAY_SIZE(index_to_dp_signal_levels),
				   "WARN_ON(n_entries > ARRAY_SIZE(index_to_dp_signal_levels))\n");
	if (warned)
		n_entries = ARRAY_SIZE(index_to_dp_signal_levels);

	/* Succeeded: reports the swing of the last entry. */
	return index_to_dp_signal_levels[n_entries - 1] &
		DP_TRAIN_VOLTAGE_SWING_MASK;
}

/* Converts a lane's DP train set into a buffer-translation index (the Linux intel_ddi_dp_level()). */
static int
i915_ddi_dp_level(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state,
	int lane)
{
	u8 train_set;
	u8 signal_levels;
	bool is_uhbr;
	int level;

	/* The lane's train set. */
	train_set = intel_dp->train_set[lane];

	/* A UHBR link trains with a TX FFE preset, which is the index itself. */
	is_uhbr = drv_i915_dp_is_uhbr(crtc_state);
	if (is_uhbr)
		return train_set & DP_TX_FFE_PRESET_VALUE_MASK;

	/* An 8b/10b link's swing and pre-emphasis pair is looked up. */
	signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
				     DP_TRAIN_PRE_EMPHASIS_MASK);
	level = i915_translate_signal_level(intel_dp, signal_levels);

	/* Succeeded: reports the index of the pair. */
	return level;
}

/* Selects the buffer-translation entry of one lane (the Linux intel_ddi_level()). */
static int
i915_ddi_level(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int lane)
{
	struct drm_i915_private *i915 __maybe_unused;
	const struct intel_ddi_buf_trans *trans;
	int level;
	int n_entries;
	bool warned;
	bool has_hdmi;

	/* Finds the device and the port's buffer table. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);

	/* A port without a table is a warning, and the first entry is used. */
	warned = drm_WARN_ON_ONCE(&i915->drm, !trans);
	if (warned)
		return 0;

	/* HDMI uses the VBT's level; DP the level the sink asked for. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (has_hdmi) {
		level = i915_ddi_hdmi_level(encoder, trans);
	} else {
		level = i915_ddi_dp_level(enc_to_intel_dp(encoder), crtc_state,
					  lane);
	}

	/* A level past the table is a warning, and the last entry is used. */
	warned = drm_WARN_ON_ONCE(&i915->drm, level >= n_entries);
	if (warned)
		level = n_entries - 1;

	/* Succeeded: reports the entry. */
	return level;
}

/* Finds the index of a DP swing and pre-emphasis pair (the Linux translate_signal_level()). */
static int
i915_translate_signal_level(
	struct intel_dp *intel_dp,
	u8 signal_levels)
{
	struct drm_i915_private *i915 __maybe_unused;
	int i;

	/* Finds the device of the port. */
	i915 = i915_lcd_dp_to_i915(intel_dp);

	/* Looks the pair up in the signal-level list. */
	for (i = 0; i < (int)ARRAY_SIZE(index_to_dp_signal_levels); i++) {
		if (index_to_dp_signal_levels[i] == signal_levels)
			return i;
	}

	/* A pair the list does not have is a warning, and index 0 is used. */
	(void)I915_LCD_DRM_WARN(&i915->drm, 1,
				"Unsupported voltage swing/pre-emphasis level: 0x%x\n",
				signal_levels);

	/* Succeeded: reports the fallback index. */
	return 0;
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
 * Selects the buffer-translation entry of an HDMI port (the Linux
 * intel_ddi_hdmi_level()): the VBT's level shift, or the table's default.
 */
static int
i915_ddi_hdmi_level(
	struct intel_encoder *encoder,
	const struct intel_ddi_buf_trans *trans)
{
	struct i915_lcd_world *world;
	int level;

	/* Finds the world of the encoder's modeset object. */
	world = i915_ddi_modeset(encoder)->world;

	/* The VBT's value for the port the DDI hooks are bound to. */
	level = I915_LCD_INTEL_BIOS_HDMI_LEVEL_SHIFT(world, encoder->devdata);
	if (level < 0)
		level = trans->hdmi_default_entry;

	/* Succeeded: reports the entry. */
	return level;
}

/*
 * Programs the DP mode of a Type-C port's two PHY lanes from its pin
 * assignment and the link's lane count (the Linux
 * icl_program_mg_dp_mode(), with the DKL PHY of display version 12 and
 * later; Ice Lake's MG PHY is not ported and is reported).
 *
 * A port that is not Type-C, or one in Thunderbolt-alt mode, is left
 * alone.
 */
static void
i915_icl_program_mg_dp_mode(
	struct intel_digital_port *dig_port,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	enum tc_port tc_port;
	enum phy phy;
	u32 ln0;
	u32 ln1;
	u32 pin_assignment;
	u8 width;
	bool is_tc;
	bool in_tbt_alt_mode;
	bool in_legacy_mode;
	int display_ver;

	/* Finds the device, the Type-C port and the PHY. */
	dev_priv = i915_lcd_to_i915(dig_port->base.base.dev);
	tc_port = i915_lcd_intel_port_to_tc(dev_priv, dig_port->base.port);
	phy = drv_i915_lcd_intel_port_to_phy(dev_priv, dig_port->base.port);

	/* Only a Type-C PHY has a DP mode. */
	is_tc = drv_i915_lcd_intel_phy_is_tc(dev_priv, phy);
	if (!is_tc)
		return;

	/* A port in Thunderbolt-alt mode is the Thunderbolt controller's. */
	in_tbt_alt_mode = i915_lcd_intel_tc_port_in_tbt_alt_mode(dig_port);
	if (in_tbt_alt_mode)
		return;

	/* Ice Lake's MG PHY is not ported. */
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	if (display_ver < 12) {
		drv_i915_lcd_error("UNPORTED: the MG PHY DP mode of display version 11 (icl_program_mg_dp_mode)\n");
		return;
	}

	/* Reads both lanes' DP mode and clears their width. */
	ln0 = i915_lcd_dkl_phy_read(dev_priv, tc_port, DKL_DP_MODE(0));
	ln1 = i915_lcd_dkl_phy_read(dev_priv, tc_port, DKL_DP_MODE(1));
	ln0 &= ~(MG_DP_MODE_CFG_DP_X1_MODE | MG_DP_MODE_CFG_DP_X2_MODE);
	ln1 &= ~(MG_DP_MODE_CFG_DP_X1_MODE | MG_DP_MODE_CFG_DP_X2_MODE);

	/* The pin assignment (DPPATC) and the link's width. */
	pin_assignment = i915_lcd_tc_port_get_pin_assignment_mask(dig_port);
	width = (u8)crtc_state->lane_count;

	/* Picks the lanes' width from the pin assignment and the link's width. */
	switch (pin_assignment) {
	case 0x0:
		/* A fixed connection: only a legacy port has one. */
		in_legacy_mode = intel_tc_port_in_legacy_mode(dig_port);
		if (!in_legacy_mode)
			drv_i915_lcd_error("WARN_ON(!intel_tc_port_in_legacy_mode(dig_port))\n");
		if (width == 1) {
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}

		break;
	case 0x1:
		/* Pin A: x2 only for four lanes. */
		if (width == 4) {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}

		break;
	case 0x2:
		/* Pin B: x2 only for two lanes. */
		if (width == 2) {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}

		break;
	case 0x3:
	case 0x5:
		/* Pins C and E: x1 for one lane, x2 otherwise. */
		if (width == 1) {
			ln0 |= MG_DP_MODE_CFG_DP_X1_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}

		break;
	case 0x4:
	case 0x6:
		/* Pins D and F: x1 for one lane, x2 otherwise. */
		if (width == 1) {
			ln0 |= MG_DP_MODE_CFG_DP_X1_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}

		break;
	default:
		I915_LCD_MISSING_CASE(pin_assignment);
		break;
	}

	/* Writes both lanes' DP mode. */
	i915_lcd_dkl_phy_write(dev_priv, tc_port, DKL_DP_MODE(0), ln0);
	i915_lcd_dkl_phy_write(dev_priv, tc_port, DKL_DP_MODE(1), ln1);
}

/*
 * Releases the core soft reset of a Type-C port's two PHY lanes before its
 * DDI buffer is enabled in DP-alt or legacy mode (the Linux
 * adlp_tbt_to_dp_alt_switch_wa(), an Alder Lake-P workaround for a port
 * that was in Thunderbolt-alt mode).
 */
static void
i915_adlp_tbt_to_dp_alt_switch_wa(
	struct intel_encoder *encoder)
{
	struct drm_i915_private *i915;
	enum tc_port tc_port;
	int ln;

	/* Finds the device and the Type-C port. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	tc_port = i915_lcd_intel_port_to_tc(i915, encoder->port);

	/* Clears the core soft reset of both lanes. */
	for (ln = 0; ln < 2; ln++)
		i915_lcd_dkl_phy_rmw(i915, tc_port, DKL_PCS_DW5(ln), DKL_PCS_DW5_CORE_SOFTRESET, 0);
}

/*
 * Programs a Type-C port's DKL PHY transmitters with the levels of its
 * buffer translation table (the Linux tgl_dkl_phy_set_signal_levels(), the
 * set_signal_levels hook).
 *
 * Each of the two PHY lanes drives two of the link's lanes (TX1 and TX2):
 * DPCNTL0 takes the even link lane's level, DPCNTL1 the odd one's.  Alder
 * Lake-P also selects the load generators (HDMI needs its own).  A port in
 * Thunderbolt-alt mode is left alone.
 */
static void
i915_tgl_dkl_phy_set_signal_levels(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	enum tc_port tc_port;
	const struct intel_ddi_buf_trans *trans;
	int n_entries;
	int ln;
	int level;
	u32 val;
	bool in_tbt_alt_mode;
	bool warned;
	bool is_alderlake_p;
	bool has_hdmi;

	/* Finds the device and the Type-C port. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	tc_port = i915_lcd_intel_port_to_tc(dev_priv, encoder->port);

	/* A port in Thunderbolt-alt mode is the Thunderbolt controller's. */
	in_tbt_alt_mode = i915_lcd_intel_tc_port_in_tbt_alt_mode(i915_lcd_enc_to_dig_port(encoder));
	if (in_tbt_alt_mode)
		return;

	/* Finds the port's buffer table; a port without one is a warning. */
	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	warned = drm_WARN_ON_ONCE(&dev_priv->drm, !trans);
	if (warned)
		return;

	/* Programs both PHY lanes. */
	is_alderlake_p = I915_LCD_IS_ALDERLAKE_P(dev_priv);
	for (ln = 0; ln < 2; ln++) {
		/* Takes the lane out of its PMD suspend. */
		i915_lcd_dkl_phy_write(dev_priv, tc_port, DKL_TX_PMD_LANE_SUS(ln), 0);

		/* The even link lane's level in DPCNTL0. */
		level = i915_ddi_level(encoder, crtc_state, 2 * ln + 0);
		i915_lcd_dkl_phy_rmw(dev_priv, tc_port, DKL_TX_DPCNTL0(ln),
				     DKL_TX_PRESHOOT_COEFF_MASK |
				     DKL_TX_DE_EMPAHSIS_COEFF_MASK |
				     DKL_TX_VSWING_CONTROL_MASK,
				     DKL_TX_PRESHOOT_COEFF(trans->entries[level].dkl.preshoot) |
				     DKL_TX_DE_EMPHASIS_COEFF(trans->entries[level].dkl.de_emphasis) |
				     DKL_TX_VSWING_CONTROL(trans->entries[level].dkl.vswing));

		/* The odd link lane's level in DPCNTL1. */
		level = i915_ddi_level(encoder, crtc_state, 2 * ln + 1);
		i915_lcd_dkl_phy_rmw(dev_priv, tc_port, DKL_TX_DPCNTL1(ln),
				     DKL_TX_PRESHOOT_COEFF_MASK |
				     DKL_TX_DE_EMPAHSIS_COEFF_MASK |
				     DKL_TX_VSWING_CONTROL_MASK,
				     DKL_TX_PRESHOOT_COEFF(trans->entries[level].dkl.preshoot) |
				     DKL_TX_DE_EMPHASIS_COEFF(trans->entries[level].dkl.de_emphasis) |
				     DKL_TX_VSWING_CONTROL(trans->entries[level].dkl.vswing));

		/* The 8b/10b links of this path are not in 20-bit mode. */
		i915_lcd_dkl_phy_rmw(dev_priv, tc_port, DKL_TX_DPCNTL2(ln),
				     DKL_TX_DP20BITMODE, 0);

		/* Only Alder Lake-P selects the load generators. */
		if (!is_alderlake_p)
			continue;

		/* HDMI's lanes take their own load generators; DP's take 0. */
		has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
		if (has_hdmi) {
			if (ln == 0) {
				val = DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(0);
				val |= DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(2);
			} else {
				val = DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(3);
				val |= DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(3);
			}
		} else {
			val = DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(0);
			val |= DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(0);
		}

		/* Selects them. */
		i915_lcd_dkl_phy_rmw(dev_priv, tc_port, DKL_TX_DPCNTL2(ln),
				     DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK |
				     DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK,
				     val);
	}
}

/*
 * Reports the xd kind of a crtc state's output: HDMI, eDP or DisplayPort.
 */
static unsigned
i915_combo_output_kind(
	const struct intel_crtc_state *crtc_state)
{
	/* HDMI, then eDP; any other is DisplayPort. */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return I915_XD_OUTPUT_HDMI;
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return I915_XD_OUTPUT_EDP;

	/* Succeeded: DisplayPort. */
	return I915_XD_OUTPUT_DP;
}

/*
 * Chooses the xd swing table of a combo port: its kind and rate, and for
 * eDP the panel's VBT (HOBL unless a link failed with it, the low swing).
 */
static const struct i915_xd_combo_swing_table *
i915_combo_swing_table(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_connector *connector;
	struct intel_dp *intel_dp;
	unsigned kind;
	unsigned vbt;

	/* The VBT's settings of an eDP panel. */
	kind = i915_combo_output_kind(crtc_state);
	vbt = 0U;
	if (kind == I915_XD_OUTPUT_EDP) {
		intel_dp = enc_to_intel_dp(encoder);
		connector = intel_dp->attached_connector;
		if (connector->panel.vbt.edp.hobl && !intel_dp->hobl_failed)
			vbt |= I915_XD_SWING_HOBL;
		if (connector->panel.vbt.edp.low_vswing)
			vbt |= I915_XD_SWING_LOW;
	}

	/* Succeeded: the platform's table. */
	return drv_i915_xd_combo_swing_table(drv_i915_lcd_xd_platform(), kind, (uint32_t)crtc_state->port_clock, vbt);
}

/*
 * The buffer-translation view of a combo port's xd swing table: its
 * length and HDMI default row, which the DP and HDMI level choices read
 * (the rows themselves are written by the xd DDI).  Rewritten by every
 * call; read at once by the modeset that asked, under its locks.
 */
static struct intel_ddi_buf_trans i915_combo_trans_view;

/* The get_buf_trans hook of a combo port: the view of its xd swing table. */
static const struct intel_ddi_buf_trans *
i915_combo_get_buf_trans(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct i915_xd_combo_swing_table *table;

	/* The table's length and default. */
	table = i915_combo_swing_table(encoder, crtc_state);
	i915_combo_trans_view.entries = NULL;
	i915_combo_trans_view.num_entries = (u8)table->count;
	i915_combo_trans_view.hdmi_default_entry = (u8)table->hdmi_default;
	*n_entries = (int)table->count;

	/* Succeeded: the view. */
	return &i915_combo_trans_view;
}

/* Hands a table out with its length (intel_get_buf_trans()). */
static const struct intel_ddi_buf_trans *
i915_intel_get_buf_trans(
	const struct intel_ddi_buf_trans *trans,
	int *num_entries)
{
	/* Reports the length alongside the table. */
	*num_entries = trans->num_entries;

	/* Succeeded: the table is constant and shared. */
	return trans;
}

/* The Tiger Lake DKL DP table of a link rate (tgl_get_dkl_buf_trans_dp()). */
static const struct intel_ddi_buf_trans *
i915_tgl_get_dkl_buf_trans_dp(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;

	UNUSED_PARAMETER(encoder);

	/* Above HBR the HBR2 table, HBR and below the HBR table. */
	if (crtc_state->port_clock > 270000)
		table = i915_intel_get_buf_trans(&tgl_dkl_phy_trans_dp_hbr2, n_entries);
	else
		table = i915_intel_get_buf_trans(&tgl_dkl_phy_trans_dp_hbr, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/* The Tiger Lake DKL PHY table of an output (tgl_get_dkl_buf_trans()). */
static const struct intel_ddi_buf_trans *
i915_tgl_get_dkl_buf_trans(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;
	bool is_hdmi;

	/* HDMI has its own table, DP picks by rate. */
	is_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (is_hdmi)
		table = i915_intel_get_buf_trans(&tgl_dkl_phy_trans_hdmi, n_entries);
	else
		table = i915_tgl_get_dkl_buf_trans_dp(encoder, crtc_state, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/* The Alder Lake-P DKL DP table of a link rate (adlp_get_dkl_buf_trans_dp()). */
static const struct intel_ddi_buf_trans *
i915_adlp_get_dkl_buf_trans_dp(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;

	UNUSED_PARAMETER(encoder);

	/* Above HBR the HBR2 / HBR3 table, HBR and below the HBR table. */
	if (crtc_state->port_clock > 270000)
		table = i915_intel_get_buf_trans(&adlp_dkl_phy_trans_dp_hbr2_hbr3, n_entries);
	else
		table = i915_intel_get_buf_trans(&adlp_dkl_phy_trans_dp_hbr, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/*
 * The Alder Lake-P DKL PHY table of an output (adlp_get_dkl_buf_trans()):
 * HDMI takes Tiger Lake's table, DP Alder Lake-P's own.
 */
static const struct intel_ddi_buf_trans *
i915_adlp_get_dkl_buf_trans(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;
	bool is_hdmi;

	/* HDMI has Tiger Lake's table, DP picks by rate. */
	is_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (is_hdmi)
		table = i915_intel_get_buf_trans(&tgl_dkl_phy_trans_hdmi, n_entries);
	else
		table = i915_adlp_get_dkl_buf_trans_dp(encoder, crtc_state, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/*
 * Binds an encoder's buffer-translation hook as intel_ddi_buf_trans_init()
 * does: a Type-C (DKL) PHY takes its platform's DKL tables, a combo PHY
 * the view of its xd swing table.
 */
static void
i915_ddi_bind_buf_trans(
	struct intel_encoder *encoder)
{
	struct drm_i915_private *i915;
	enum phy phy;
	bool is_tc;

	/* Finds whether the port's PHY is Type-C. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);
	is_tc = drv_i915_lcd_intel_phy_is_tc(i915, phy);

	/* A Type-C PHY takes the platform's DKL tables. */
	if (is_tc) {
		if (drv_i915_lcd_display_ver() >= 13)
			encoder->get_buf_trans = i915_adlp_get_dkl_buf_trans;
		else
			encoder->get_buf_trans = i915_tgl_get_dkl_buf_trans;
		return;
	}

	/* A combo PHY takes its xd swing table. */
	encoder->get_buf_trans = i915_combo_get_buf_trans;
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
