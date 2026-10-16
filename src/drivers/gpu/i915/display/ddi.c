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
#include "hdmi-mode.h"
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
static struct drm_i915_private *i915_ddi_cur_i915(const struct intel_encoder *encoder);
static struct i915_lcd_modeset *i915_encoders_bound(struct intel_atomic_state *state);
static void i915_encoders_call(struct i915_lcd_modeset *ms, i915_ddi_encoder_hook_t hook, struct intel_atomic_state *state, const struct intel_crtc_state *crtc_state);
static void i915_encoders_walk_disable(struct intel_atomic_state *state, struct intel_crtc *crtc, enum i915_ddi_disable_hook which);
static void i915_ddi_init_dp_buf_reg(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_ddi_xd_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, struct i915_xd_trans_func *func);
static void i915_ddi_enable_transcoder_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_config_transcoder_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_tgl_ddi_pre_enable_dp(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_ddi_pre_enable_dp(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_ddi_pre_enable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_enable_ddi_dp(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_enable_ddi(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_ddi_pre_pll_enable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static u8 i915_ddi_dp_voltage_max(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
static u8 i915_ddi_dp_preemph_max(struct intel_dp *intel_dp);
static void i915_ddi_enable_clock(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_disable_clock(struct intel_encoder *encoder);
static void i915_icl_ddi_combo_enable_clock(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_icl_ddi_combo_disable_clock(struct intel_encoder *encoder);
static enum intel_display_power_domain i915_ddi_main_link_aux_domain(struct intel_digital_port *dig_port, const struct intel_crtc_state *crtc_state);
static void i915_main_link_aux_power_domain_get(struct intel_digital_port *dig_port, const struct intel_crtc_state *crtc_state);
static void i915_main_link_aux_power_domain_put(struct intel_digital_port *dig_port, const struct intel_crtc_state *crtc_state);
static void i915_ddi_enable_transcoder_clock(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_disable_transcoder_clock(const struct intel_crtc_state *crtc_state);
static int i915_ddi_dp_level(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, int lane);
static int i915_ddi_level(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int lane);
static void i915_icl_combo_phy_set_signal_levels(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static int i915_translate_signal_level(struct intel_dp *intel_dp, u8 signal_levels);
static void i915_ddi_power_up_lanes(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_mso_configure(const struct intel_crtc_state *crtc_state);
static enum transcoder i915_tgl_dp_tp_transcoder(const struct intel_crtc_state *crtc_state);
static void i915_ddi_prepare_link_retrain(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
static void i915_ddi_set_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, u8 dp_train_pat);
static void i915_ddi_set_idle_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
static void i915_disable_ddi_buf_ctl(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_disable_ddi_buf(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_ddi_disable_transcoder_func(struct drm_i915_private *cur_i915, const struct intel_crtc_state *crtc_state);
static void i915_dp_sink_set_msa_timing_par_ignore_state(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, bool enable);
static void i915_disable_ddi_dp(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static void i915_disable_ddi(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static void i915_ddi_post_disable_dp(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static void i915_ddi_post_disable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static void i915_ddi_post_pll_disable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static int i915_icl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_jsl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_tgl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int i915_ddi_hdmi_level(struct intel_encoder *encoder, const struct intel_ddi_buf_trans *trans);
static unsigned i915_combo_output_kind(const struct intel_crtc_state *crtc_state);
static const struct i915_xd_combo_swing_table *i915_combo_swing_table(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static const struct intel_ddi_buf_trans *i915_combo_get_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_dkl_get_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static void i915_ddi_bind_buf_trans(struct intel_encoder *encoder);
static void i915_ddi_xd_link(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, struct i915_xd_ddi_link *link);
static void i915_ddi_tbt_to_dp_alt_hook(void *context);
static void i915_ddi_report_late(struct drm_i915_private *i915, enum port port, unsigned late);
static void i915_ddi_pre_enable_hdmi(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_enable_ddi_hdmi(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void i915_disable_ddi_hdmi(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static void i915_ddi_post_disable_hdmi(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *old_crtc_state, const struct drm_connector_state *old_conn_state);
static bool i915_icl_ddi_combo_is_clock_enabled(struct intel_encoder *encoder);
static void i915_icl_program_mg_dp_mode(struct intel_digital_port *dig_port, const struct intel_crtc_state *crtc_state);
static void i915_ddi_update_active_dpll(struct intel_atomic_state *state, struct intel_encoder *encoder, struct intel_crtc *crtc);
static void i915_adlp_tbt_to_dp_alt_switch_wa(struct intel_encoder *encoder);
static u32 i915_icl_pll_to_ddi_clk_sel(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_icl_ddi_tc_enable_clock(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
static void i915_icl_ddi_tc_disable_clock(struct intel_encoder *encoder);
static bool i915_icl_ddi_tc_is_clock_enabled(struct intel_encoder *encoder);
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

	/* Binds the enable and disable hooks of the crtc sequences. */
	encoder->enable = i915_enable_ddi;
	encoder->pre_pll_enable = i915_ddi_pre_pll_enable;
	encoder->pre_enable = i915_ddi_pre_enable;
	encoder->disable = i915_disable_ddi;
	encoder->post_disable = i915_ddi_post_disable;
	encoder->post_pll_disable = i915_ddi_post_pll_disable;

	/*
	 * Binds the clock and signal-level hooks of the port's PHY: a Type-C
	 * PHY's DDI clock select and DKL transmitters, or a combo PHY's.
	 */
	if (is_tc) {
		encoder->enable_clock = i915_icl_ddi_tc_enable_clock;
		encoder->is_clock_enabled = i915_icl_ddi_tc_is_clock_enabled;
		encoder->disable_clock = i915_icl_ddi_tc_disable_clock;
		encoder->set_signal_levels = i915_tgl_dkl_phy_set_signal_levels;
	} else {
		encoder->enable_clock = i915_icl_ddi_combo_enable_clock;
		encoder->is_clock_enabled = i915_icl_ddi_combo_is_clock_enabled;
		encoder->disable_clock = i915_icl_ddi_combo_disable_clock;
		encoder->set_signal_levels = i915_icl_combo_phy_set_signal_levels;
	}

	/* Binds the buffer-translation table of the port. */
	i915_ddi_bind_buf_trans(encoder);

	/* Binds the link-training hooks of the DP half. */
	intel_dp->prepare_link_retrain = i915_ddi_prepare_link_retrain;
	intel_dp->set_link_train = i915_ddi_set_link_train;
	intel_dp->set_idle_link_train = i915_ddi_set_idle_link_train;
	intel_dp->voltage_max = i915_ddi_dp_voltage_max;
	intel_dp->preemph_max = i915_ddi_dp_preemph_max;
}

/*
 * Runs the pre-PLL-enable hook of the encoder on the crtc.
 *
 * The Linux intel_encoders_pre_pll_enable() walks the atomic state's
 * connectors; here there is exactly one encoder, the bound object's, and
 * it sees the new crtc state.  The world is the one the atomic state
 * names; the crtc is not read.
 */
void
drv_i915_encoders_pre_pll_enable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct i915_lcd_modeset *ms;

	UNUSED_PARAMETER(crtc);

	/* Finds the object the commit's world has bound; none is reported. */
	ms = i915_encoders_bound(state);
	if (ms == NULL)
		return;

	/* Runs the bound encoder's hook on the new state. */
	i915_encoders_call(ms, ms->dig_port.base.pre_pll_enable, state, state->crtc_state);
}

/*
 * Runs the pre-enable hook of the encoder on the crtc.
 *
 * The Linux intel_encoders_pre_enable(), on the one encoder with the new
 * crtc state, in the world the atomic state names.  The crtc is not read.
 */
void
drv_i915_encoders_pre_enable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct i915_lcd_modeset *ms;

	UNUSED_PARAMETER(crtc);

	/* Finds the object the commit's world has bound; none is reported. */
	ms = i915_encoders_bound(state);
	if (ms == NULL)
		return;

	/* Runs the bound encoder's hook on the new state. */
	i915_encoders_call(ms, ms->dig_port.base.pre_enable, state, state->crtc_state);
}

/*
 * Runs the enable hook of the encoder on the crtc.
 *
 * The Linux intel_encoders_enable(), on the one encoder with the new crtc
 * state, in the world the atomic state names.  The crtc is not read.
 */
void
drv_i915_encoders_enable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct i915_lcd_modeset *ms;

	UNUSED_PARAMETER(crtc);

	/* Finds the object the commit's world has bound; none is reported. */
	ms = i915_encoders_bound(state);
	if (ms == NULL)
		return;

	/* Runs the bound encoder's hook on the new state. */
	i915_encoders_call(ms, ms->dig_port.base.enable, state, state->crtc_state);
}

/*
 * Runs the disable hook of the encoder on the crtc.
 *
 * The Linux intel_encoders_disable(), on the one encoder with the old crtc
 * state, in the world the atomic state names.  The crtc is not read.
 */
void
drv_i915_encoders_disable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct i915_lcd_modeset *ms;

	/* A state that walks encoders of its own runs the hook of each encoder on the crtc. */
	if (state->encoder_at != NULL) {
		i915_encoders_walk_disable(state, crtc, I915_DDI_HOOK_DISABLE);
		return;
	}

	/* Finds the object the commit's world has bound; none is reported. */
	ms = i915_encoders_bound(state);
	if (ms == NULL)
		return;

	/* Runs the bound encoder's hook on the old state. */
	i915_encoders_call(ms, ms->dig_port.base.disable, state, state->old_crtc_state);
}

/*
 * Runs the post-disable hook of the encoder on the crtc.
 *
 * The Linux intel_encoders_post_disable(), on the one encoder with the old
 * crtc state, in the world the atomic state names.  The crtc is not read.
 */
void
drv_i915_encoders_post_disable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct i915_lcd_modeset *ms;

	/* A state that walks encoders of its own runs the hook of each encoder on the crtc. */
	if (state->encoder_at != NULL) {
		i915_encoders_walk_disable(state, crtc, I915_DDI_HOOK_POST_DISABLE);
		return;
	}

	/* Finds the object the commit's world has bound; none is reported. */
	ms = i915_encoders_bound(state);
	if (ms == NULL)
		return;

	/* Runs the bound encoder's hook on the old state. */
	i915_encoders_call(ms, ms->dig_port.base.post_disable, state, state->old_crtc_state);
}

/*
 * Runs the post-PLL-disable hook of the encoder on the crtc.
 *
 * The Linux intel_encoders_post_pll_disable(), on the one encoder with the
 * old crtc state.  The crtc is not read.
 */
void
drv_i915_encoders_post_pll_disable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc)
{
	struct i915_lcd_modeset *ms;

	/* A state that walks encoders of its own runs the hook of each encoder on the crtc. */
	if (state->encoder_at != NULL) {
		i915_encoders_walk_disable(state, crtc, I915_DDI_HOOK_POST_PLL_DISABLE);
		return;
	}

	/* Finds the object the commit's world has bound; none is reported. */
	ms = i915_encoders_bound(state);
	if (ms == NULL)
		return;

	/* Runs the bound encoder's hook on the old state. */
	i915_encoders_call(ms, ms->dig_port.base.post_pll_disable, state, state->old_crtc_state);
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
 * Finds the device of the current entry point: the device the Linux text
 * reached through its file-scope pointer, as the encoder's world records it.
 */
static struct drm_i915_private *
i915_ddi_cur_i915(
	const struct intel_encoder *encoder)
{
	struct i915_lcd_modeset *ms;

	/* The encoder's modeset object leads to its world. */
	ms = i915_ddi_modeset(encoder);

	/* Succeeded: reports the device the entry point recorded. */
	return ms->world->i915_lcd_cur_i915;
}

/*
 * Finds the modeset object the DDI hooks are bound to in the world a
 * commit's atomic state names.  A state that names no world is reported
 * as an error (NULL): the Linux text reached the bound object through a
 * file-scope pointer, which a state without its world cannot replace.
 */
static struct i915_lcd_modeset *
i915_encoders_bound(
	struct intel_atomic_state *state)
{
	/* A state its builder did not tie to a world cannot be walked. */
	if (state->world == NULL) {
		drv_i915_lcd_error("encoder walk: the atomic state names no modeset world\n");
		return NULL;
	}

	/* Succeeded: reports the world's bound object. */
	return state->world->ddi_ms;
}

/* Runs one hook of the bound encoder, if it is bound, on a crtc state. */
static void
i915_encoders_call(
	struct i915_lcd_modeset *ms,
	i915_ddi_encoder_hook_t hook,
	struct intel_atomic_state *state,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder;

	/* The one encoder of the modeset. */
	encoder = &ms->dig_port.base;

	/* An unbound hook does nothing. */
	if (!hook)
		return;

	/* Runs the hook with the connector state of the object. */
	hook(state, encoder, crtc_state, &ms->conn_state);
}

/*
 * Runs one disable hook of every encoder of a state's own walk that is on
 * the crtc (the Linux intel_encoders_disable(), _post_disable() and
 * _post_pll_disable() over the old state's connectors), with the old crtc
 * state and the connector state of the encoder's modeset object.
 */
static void
i915_encoders_walk_disable(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc,
	enum i915_ddi_disable_hook which)
{
	struct i915_lcd_modeset *ms;
	struct intel_encoder *encoder;
	i915_ddi_encoder_hook_t hook;
	unsigned index;

	/* Walks the state's encoders until the walk ends. */
	for (index = 0u; ; index++) {
		encoder = state->encoder_at(state->walk, index);
		if (encoder == NULL)
			break;

		/* An encoder on another crtc is not part of this disable. */
		if (encoder->base.crtc != &crtc->base)
			continue;

		/* The hook the walk runs. */
		switch (which) {
		case I915_DDI_HOOK_DISABLE:
			hook = encoder->disable;
			break;
		case I915_DDI_HOOK_POST_DISABLE:
			hook = encoder->post_disable;
			break;
		default:
			hook = encoder->post_pll_disable;
			break;
		}

		/* Runs it with the connector state of the encoder's object. */
		ms = i915_ddi_modeset(encoder);
		i915_encoders_call(ms, hook, state, state->old_crtc_state);
	}
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

/* Writes the transcoder function of a crtc state without enabling it, through the xd pipe. */
static void
i915_ddi_config_transcoder_func(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	struct i915_xd_trans_func func;

	/* Finds the crtc and its device. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);

	/* The function without its enable. */
	i915_ddi_xd_func(encoder, crtc_state, &func);
	drv_i915_xd_pipe_func_config(drv_i915_lcd_xd_io(dev_priv), (unsigned)crtc_state->cpu_transcoder, &func);
}

/*
 * Enables and trains a DP port on display version 12 and 13 (the Linux
 * tgl_ddi_pre_enable_dp(), which follows the bspec's numbered steps).
 */
static void
i915_tgl_ddi_pre_enable_dp(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp;
	struct drm_i915_private *dev_priv;
	struct drm_i915_private *cur_i915;
	struct intel_digital_port *dig_port;
	bool is_mst;
	bool in_tbt_alt_mode;

	UNUSED_PARAMETER(state);
	UNUSED_PARAMETER(conn_state);

	/* Finds the DP half, the devices, the port and whether the stream is MST. */
	intel_dp = enc_to_intel_dp(encoder);
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	is_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);

	/* Records the link rate and lane count of the link to train. */
	drv_i915_dp_set_link_params(intel_dp,
				    crtc_state->port_clock,
				    crtc_state->lane_count);

	/*
	 * We only configure what the register value will be here.  Actual
	 * enabling happens during link training farther down.
	 */
	i915_ddi_init_dp_buf_reg(encoder, crtc_state);

	/*
	 * 1. Enable Power Wells
	 *
	 * This was handled at the beginning of intel_atomic_commit_tail(),
	 * before we called down into this function.
	 */

	/* 2. Enable Panel Power if PPS is required */
	i915_lcd_intel_pps_on(cur_i915, intel_dp);

	/*
	 * 3. For non-TBT Type-C ports, set FIA lane count
	 * (DFLEXDPSP.DPX4TXLATC)
	 *
	 * This was done before tgl_ddi_pre_enable_dp by
	 * hsw_crtc_enable()->intel_encoders_pre_pll_enable().
	 */

	/*
	 * 4. Enable the port PLL.
	 *
	 * The PLL enabling itself was already done before this function by
	 * hsw_crtc_enable()->intel_enable_shared_dpll().  We need only
	 * configure the PLL to port mapping here.
	 */
	i915_ddi_enable_clock(encoder, crtc_state);

	/* 5. If IO power is controlled through PWR_WELL_CTL, Enable IO Power */
	in_tbt_alt_mode = i915_lcd_intel_tc_port_in_tbt_alt_mode(dig_port);
	if (!in_tbt_alt_mode) {
		(void)I915_LCD_DRM_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
		dig_port->ddi_io_wakeref = i915_lcd_intel_display_power_get(dev_priv,
									    dig_port->ddi_io_power_domain);
	}

	/* 6. Program DP_MODE */
	i915_icl_program_mg_dp_mode(dig_port, crtc_state);

	/*
	 * 7. The rest of the below are substeps under the bspec's "Enable and
	 * Train Display Port" step.  Note that steps that are specific to
	 * MST will be handled by intel_mst_pre_enable_dp() before/after it
	 * calls into this function.  Also intel_mst_pre_enable_dp() only calls
	 * us when active_mst_links==0, so any steps designated for "single
	 * stream or multi-stream master transcoder" can just be performed
	 * unconditionally here.
	 */

	/*
	 * 7.a Configure Transcoder Clock Select to direct the Port clock to the
	 * Transcoder.
	 */
	i915_ddi_enable_transcoder_clock(encoder, crtc_state);

	/* A DP 2.0 transcoder has its own DP2 control word. */
	if (HAS_DP20(dev_priv))
		intel_ddi_config_transcoder_dp2(encoder, crtc_state);

	/*
	 * 7.b Configure TRANS_DDI_FUNC_CTL DDI Select, DDI Mode Select & MST
	 * Transport Select
	 */
	i915_ddi_config_transcoder_func(encoder, crtc_state);

	/*
	 * 7.c Configure & enable DP_TP_CTL with link training pattern 1
	 * selected
	 *
	 * This will be handled by the intel_dp_start_link_train() farther
	 * down this function.
	 */

	/* 7.e Configure voltage swing and related IO settings */
	encoder->set_signal_levels(encoder, crtc_state);

	/*
	 * 7.f Combo PHY: Configure PORT_CL_DW10 Static Power Down to power up
	 * the used lanes of the DDI.
	 */
	i915_ddi_power_up_lanes(encoder, crtc_state);

	/*
	 * 7.g Program CoG/MSO configuration bits in DSS_CTL1 if selected.
	 */
	i915_ddi_mso_configure(crtc_state);

	/* Wakes the sink of a single stream. */
	if (!is_mst)
		drv_i915_dp_set_power(intel_dp, DP_SET_POWER_D0);

	/* Configures a protocol converter behind the port. */
	intel_dp_configure_protocol_converter(intel_dp, crtc_state);

	/* Enables the sink's decompression for a single stream. */
	if (!is_mst) {
		intel_dp_sink_enable_decompression(state,
						   I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector),
						   crtc_state);
	}

	/*
	 * DDI FEC: "anticipates enabling FEC encoding sets the FEC_READY bit
	 * in the FEC_CONFIGURATION register to 1 before initiating link
	 * training
	 */
	intel_dp_sink_set_fec_ready(intel_dp, crtc_state, true);

	/* Checks the FRL training of a PCON and configures its DSC. */
	intel_dp_check_frl_training(intel_dp);
	intel_dp_pcon_dsc_configure(intel_dp, crtc_state);

	/*
	 * 7.i Follow DisplayPort specification training sequence (see notes for
	 *     failure handling)
	 * 7.j If DisplayPort multi-stream - Set DP_TP_CTL link training to Idle
	 *     Pattern, wait for 5 idle patterns (DP_TP_STATUS Min_Idles_Sent)
	 *     (timeout after 800 us)
	 */
	drv_i915_dp_start_link_train(intel_dp, crtc_state);

	/* 7.k Set DP_TP_CTL link training to Normal */
	if (!is_trans_port_sync_mode(crtc_state))
		drv_i915_dp_stop_link_train(intel_dp, crtc_state);

	/* 7.l Configure and enable FEC if needed */
	intel_ddi_enable_fec(encoder, crtc_state);

	/* Writes the sink's DSC picture parameters for a single stream. */
	if (!is_mst)
		intel_dsc_dp_pps_write(encoder, crtc_state);
}

/* Enables a DP port by the display version's sequence, then its MSA (the Linux intel_ddi_pre_enable_dp()). */
static void
i915_ddi_pre_enable_dp(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv __maybe_unused;
	struct drm_i915_private *cur_i915;
	struct intel_dp *intel_dp __maybe_unused;
	int display_ver;
	bool has_dp_mst;

	/* Finds the devices and the DP half of the encoder. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	intel_dp = enc_to_intel_dp(encoder);
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);

	/* A DP 2.0 source sends the SDP CRC and enables the sink's panel replay. */
	if (HAS_DP20(dev_priv)) {
		intel_dp_128b132b_sdp_crc16(enc_to_intel_dp(encoder),
					    crtc_state);
		if (crtc_state->has_panel_replay) {
			(void)I915_LCD_DRM_DP_DPCD_WRITEB(cur_i915,
							  &intel_dp->aux,
							  I915_LCD_PANEL_REPLAY_CONFIG,
							  I915_LCD_DP_PANEL_REPLAY_ENABLE);
		}
	}

	/* Runs the enable sequence of the display version. */
	if (display_ver >= 14) {
		mtl_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);
	} else if (display_ver >= 12) {
		i915_tgl_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);
	} else {
		hsw_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);
	}

	/* MST will call a setting of MSA after an allocating of Virtual Channel
	 * from MST encoder pre_enable callback.
	 */
	has_dp_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);
	if (!has_dp_mst)
		i915_ddi_set_dp_msa(crtc_state, conn_state);
}

/* Runs the pre-enable of an HDMI or a DP port (the Linux intel_ddi_pre_enable(), the pre_enable hook). */
static void
i915_ddi_pre_enable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	enum pipe pipe __maybe_unused;
	bool has_hdmi;

	/* Finds the crtc, its device and its pipe. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	pipe = crtc->pipe;

	/*
	 * When called from DP MST code:
	 * - conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - crtc_state will be the state of the first stream to
	 *   be activated on this port, and it may not be the same
	 *   stream that will be deactivated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	/* A PCH encoder here is a warning. */
	(void)I915_LCD_DRM_WARN_ON(&dev_priv->drm, crtc_state->has_pch_encoder);

	/* Where Linux arms the pipe's underrun reporting. */
	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	/* Runs the HDMI or the DP enable of the port. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (has_hdmi) {
		i915_ddi_pre_enable_hdmi(state, encoder, crtc_state,
					 conn_state);
	} else {
		dig_port = i915_lcd_enc_to_dig_port(encoder);

		i915_ddi_pre_enable_dp(state, encoder, crtc_state,
				       conn_state);

		/* FIXME precompute everything properly */
		/* FIXME how do we turn infoframes off again? */
		if (dig_port->lspcon.active && intel_dp_has_hdmi_sink(&dig_port->dp)) {
			dig_port->set_infoframes(encoder,
						 crtc_state->has_infoframe,
						 crtc_state, conn_state);
		}
	}
}

/* Runs the DP half of the port enable after the transcoder (the Linux intel_enable_ddi_dp()). */
static void
i915_enable_ddi_dp(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv __maybe_unused;
	struct intel_dp *intel_dp;
	struct intel_digital_port *dig_port;
	enum port port;
	int display_ver;

	UNUSED_PARAMETER(state);

	/* Finds the device, the DP half, the port object and its number. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	intel_dp = enc_to_intel_dp(encoder);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	port = encoder->port;

	/* Port A before display version 9 stops the link training only here. */
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	if (port == PORT_A && display_ver < 9)
		drv_i915_dp_stop_link_train(intel_dp, crtc_state);

	/* Turns the privacy screen and the eDP backlight on. */
	drm_connector_update_privacy_screen(conn_state);
	drv_i915_edp_backlight_on(crtc_state, conn_state);

	/* Enables the DP infoframes unless an LSPCON drives an HDMI sink without them. */
	if (!dig_port->lspcon.active || intel_dp_has_hdmi_sink(&dig_port->dp))
		drv_i915_dp_set_infoframes(encoder, true, crtc_state, conn_state);

	/* Stops the link training of the port-sync slaves. */
	trans_port_sync_stop_link_train(state, encoder, crtc_state);
}

/* Enables the transcoder and the port output (the Linux intel_enable_ddi(), the enable hook). */
static void
i915_enable_ddi(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	bool has_hdmi;

	/* A PCH encoder here is a warning. */
	(void)I915_LCD_DRM_WARN_ON(state->base.dev, crtc_state->has_pch_encoder);

	/* The master pipe enables the transcoder function. */
	if (!intel_crtc_is_bigjoiner_slave(crtc_state))
		i915_ddi_enable_transcoder_func(encoder, crtc_state);

	/* Enable/Disable DP2.0 SDP split config before transcoder */
	intel_audio_sdp_split_update(crtc_state);

	/* Enables the transcoder. */
	drv_i915_enable_transcoder(crtc_state);

	/* Waits for the FEC status and turns the crtc's vblank on. */
	intel_ddi_wait_for_fec_status(encoder, crtc_state, true);
	intel_crtc_vblank_on(crtc_state);

	/* Runs the HDMI or the DP half of the enable. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (has_hdmi) {
		i915_enable_ddi_hdmi(state, encoder, crtc_state, conn_state);
	} else {
		i915_enable_ddi_dp(state, encoder, crtc_state, conn_state);
	}

	/* Enables HDCP when content protection asks for it. */
	intel_hdcp_enable(state, encoder, crtc_state, conn_state);
}

/*
 * Takes the port's AUX power before the PLL is enabled (the Linux
 * intel_ddi_pre_pll_enable(), the pre_pll_enable hook).
 */
static void
i915_ddi_pre_pll_enable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct intel_crtc *master_crtc __maybe_unused;
	enum phy phy;
	bool is_tc_port;
	bool in_tbt_alt_mode;

	UNUSED_PARAMETER(state);
	UNUSED_PARAMETER(conn_state);

	/* Finds the device, the port object and whether its PHY is Type-C. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	phy = drv_i915_lcd_intel_port_to_phy(dev_priv, encoder->port);
	is_tc_port = drv_i915_lcd_intel_phy_is_tc(dev_priv, phy);

	/* A Type-C port takes its link and selects its active PLL. */
	if (is_tc_port) {
		master_crtc = to_intel_crtc(crtc_state->uapi.crtc);

		intel_tc_port_get_link(dig_port, crtc_state->lane_count);
		i915_ddi_update_active_dpll(state, encoder, master_crtc);
	}

	/* Takes the main link's AUX power. */
	i915_main_link_aux_power_domain_get(dig_port, crtc_state);

	/* Whether the port is in Thunderbolt-alt mode (only asked for a Type-C port). */
	in_tbt_alt_mode = true;
	if (is_tc_port)
		in_tbt_alt_mode = i915_lcd_intel_tc_port_in_tbt_alt_mode(dig_port);

	/*
	 * Program the lane count for static/dynamic connections on
	 * Type-C ports.  Skip this step for TBT.
	 */
	if (is_tc_port && !in_tbt_alt_mode) {
		intel_tc_port_set_fia_lane_count(dig_port, crtc_state->lane_count);
	} else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
		bxt_ddi_phy_set_lane_optim_mask(encoder,
						crtc_state->lane_lat_optim_mask);
	}
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

/*
 * Reports the highest pre-emphasis of the port (the Linux
 * intel_ddi_dp_preemph_max()).  We assume that the full set of
 * pre-emphasis values can be used on all DDI platforms.
 */
static u8
i915_ddi_dp_preemph_max(
	struct intel_dp *intel_dp)
{
	UNUSED_PARAMETER(intel_dp);

	/* Succeeded: every DDI platform reaches level 3. */
	return DP_TRAIN_PRE_EMPH_LEVEL_3;
}

/* Maps the port to its PLL and ungates its clock, if the port can (the Linux intel_ddi_enable_clock()). */
static void
i915_ddi_enable_clock(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	/* A port without the hook has nothing to enable. */
	if (!encoder->enable_clock)
		return;

	/* Enables the clock through the port's hook. */
	encoder->enable_clock(encoder, crtc_state);
}

/* Gates the port's clock, if the port can (the Linux intel_ddi_disable_clock()). */
static void
i915_ddi_disable_clock(
	struct intel_encoder *encoder)
{
	/* A port without the hook has nothing to disable. */
	if (!encoder->disable_clock)
		return;

	/* Disables the clock through the port's hook. */
	encoder->disable_clock(encoder);
}

/* Maps a combo PHY port to the crtc's PLL and ungates its clock (the Linux icl_ddi_combo_enable_clock(), the enable_clock hook). */
static void
i915_icl_ddi_combo_enable_clock(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	struct drm_i915_private *cur_i915;
	const struct intel_shared_dpll *pll;
	enum phy phy;
	bool warned;

	/* Finds the devices, the crtc's PLL and the port's PHY. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	pll = crtc_state->shared_dpll;
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);

	/* A crtc state without a PLL is a warning, and nothing is mapped. */
	warned = I915_LCD_DRM_WARN_ON(&i915->drm, !pll);
	if (warned)
		return;

	/*
	 * Selects the PLL for the PHY and ungates the PHY's DDI clock through
	 * the xd clock, under the DPLL lock (taken on the entry point's
	 * device), as the Type-C clock-off bits of the same word are written.
	 */
	I915_LCD_MUTEX_LOCK(cur_i915, &i915->display.dpll.lock);
	drv_i915_dpll_map_ddi(i915, (unsigned)phy, pll);
	I915_LCD_MUTEX_UNLOCK(cur_i915, &i915->display.dpll.lock);
}

/* Gates the DDI clock of a combo PHY port (the Linux icl_ddi_combo_disable_clock(), the disable_clock hook). */
static void
i915_icl_ddi_combo_disable_clock(
	struct intel_encoder *encoder)
{
	struct drm_i915_private *i915;
	struct drm_i915_private *cur_i915;
	enum phy phy;

	/* Finds the devices and the port's PHY. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);

	/* Gates the PHY's DDI clock through the xd clock, under the DPLL lock. */
	I915_LCD_MUTEX_LOCK(cur_i915, &i915->display.dpll.lock);
	drv_i915_dpll_unmap_ddi(i915, (unsigned)phy);
	I915_LCD_MUTEX_UNLOCK(cur_i915, &i915->display.dpll.lock);
}

/* Selects the AUX power domain the port's main link needs (the Linux intel_ddi_main_link_aux_domain()). */
static enum intel_display_power_domain
i915_ddi_main_link_aux_domain(
	struct intel_digital_port *dig_port,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	enum phy phy;
	bool has_dp_encoder;
	bool is_tc;
	enum intel_display_power_domain domain;
	int display_ver;

	/* Finds the device and the port's PHY. */
	i915 = i915_lcd_to_i915(dig_port->base.base.dev);
	phy = drv_i915_lcd_intel_port_to_phy(i915, dig_port->base.port);

	/*
	 * ICL+ HW requires corresponding AUX IOs to be powered up for PSR with
	 * DC states enabled at the same time, while for driver initiated AUX
	 * transfers we need the same AUX IOs to be powered but with DC states
	 * disabled. Accordingly use the AUX_IO_<port> power domain here which
	 * leaves DC states enabled.
	 *
	 * Before MTL TypeC PHYs (in all TypeC modes and both DP/HDMI) also require
	 * AUX IO to be enabled, but all these require DC_OFF to be enabled as
	 * well, so we can acquire a wider AUX_<port> power domain reference
	 * instead of a specific AUX_IO_<port> reference without powering up any
	 * extra wells.
	 */
	if (intel_psr_needs_aux_io_power(&dig_port->base, crtc_state))
		return intel_display_power_aux_io_domain(i915, dig_port->aux_ch);

	/* Before display version 14 a DP stream or a Type-C PHY takes the AUX domain. */
	display_ver = I915_LCD_DISPLAY_VER(i915);
	if (display_ver < 14) {
		has_dp_encoder = intel_crtc_has_dp_encoder(crtc_state);
		is_tc = false;
		if (!has_dp_encoder)
			is_tc = drv_i915_lcd_intel_phy_is_tc(i915, phy);

		if (has_dp_encoder || is_tc) {
			domain = drv_i915_aux_power_domain(dig_port);
			return domain;
		}
	}

	/* Succeeded: the main link needs no AUX power. */
	return POWER_DOMAIN_INVALID;
}

/* Takes the AUX power reference of the port's main link (the Linux main_link_aux_power_domain_get()). */
static void
i915_main_link_aux_power_domain_get(
	struct intel_digital_port *dig_port,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	enum intel_display_power_domain domain;

	/* Finds the device and the domain the main link needs. */
	i915 = i915_lcd_to_i915(dig_port->base.base.dev);
	domain = i915_ddi_main_link_aux_domain(dig_port, crtc_state);

	/* A reference still held here is a warning. */
	(void)I915_LCD_DRM_WARN_ON(&i915->drm, dig_port->aux_wakeref);

	/* A main link that needs no AUX power takes nothing. */
	if (domain == POWER_DOMAIN_INVALID)
		return;

	/* Takes the reference the post-PLL disable gives back. */
	dig_port->aux_wakeref = i915_lcd_intel_display_power_get(i915, domain);
}

/* Gives the AUX power reference of the port's main link back (the Linux main_link_aux_power_domain_put()). */
static void
i915_main_link_aux_power_domain_put(
	struct intel_digital_port *dig_port,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	enum intel_display_power_domain domain;
	intel_wakeref_t wf;

	/* Finds the device and the domain the main link needed. */
	i915 = i915_lcd_to_i915(dig_port->base.base.dev);
	domain = i915_ddi_main_link_aux_domain(dig_port, crtc_state);

	/* Takes the reference out of the port; none was held. */
	wf = fetch_and_zero(&dig_port->aux_wakeref);
	if (!wf)
		return;

	/* Gives the reference back. */
	i915_lcd_intel_display_power_put(i915, domain, wf);
}

/* Directs the port clock to the crtc's transcoder (the Linux intel_ddi_enable_transcoder_clock()). */
static void
i915_ddi_enable_transcoder_clock(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	enum phy phy;
	int display_ver;
	u32 val;

	/* Finds the crtc, its device, its transcoder and the port's PHY. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	cpu_transcoder = crtc_state->cpu_transcoder;
	phy = drv_i915_lcd_intel_port_to_phy(dev_priv, encoder->port);

	/* The eDP transcoder has no clock select. */
	if (cpu_transcoder == TRANSCODER_EDP)
		return;

	/* The select value names the PHY from display version 13, the port before. */
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	if (display_ver >= 13) {
		val = TGL_TRANS_CLK_SEL_PORT(phy);
	} else if (display_ver >= 12) {
		val = TGL_TRANS_CLK_SEL_PORT(encoder->port);
	} else {
		val = TRANS_CLK_SEL_PORT(encoder->port);
	}

	/* Writes the clock select. */
	i915_lcd_intel_de_write(dev_priv, TRANS_CLK_SEL(cpu_transcoder), val);
}

/* Directs no clock to the crtc's transcoder (the Linux intel_ddi_disable_transcoder_clock()). */
static void
i915_ddi_disable_transcoder_clock(
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	u32 val;
	int display_ver;

	/* Finds the crtc's device and its transcoder. */
	dev_priv = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);
	cpu_transcoder = crtc_state->cpu_transcoder;

	/* The eDP transcoder has no clock select. */
	if (cpu_transcoder == TRANSCODER_EDP)
		return;

	/* The disabled value of the display version. */
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	if (display_ver >= 12) {
		val = TGL_TRANS_CLK_SEL_DISABLED;
	} else {
		val = TRANS_CLK_SEL_DISABLED;
	}

	/* Writes the clock select. */
	i915_lcd_intel_de_write(dev_priv, TRANS_CLK_SEL(cpu_transcoder), val);
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

/*
 * Programs the signal levels of a combo PHY port through the xd DDI: the
 * swing table of the port's kind, rate and VBT, and the row of each lane
 * (the set_signal_levels hook).  An eDP port notes whether its table is
 * the HOBL one.
 */
static void
i915_icl_combo_phy_set_signal_levels(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_combo_levels levels;
	struct intel_dp *intel_dp;
	int ln;

	/* The port's PHY, kind, rate, lanes and table. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	levels.phy = (unsigned)drv_i915_lcd_intel_port_to_phy(dev_priv, encoder->port);
	levels.kind = i915_combo_output_kind(crtc_state);
	levels.rate_khz = (uint32_t)crtc_state->port_clock;
	levels.lanes = (unsigned)crtc_state->lane_count;
	levels.table = i915_combo_swing_table(encoder, crtc_state);

	/* The row of each lane. */
	for (ln = 0; ln < 4; ln++)
		levels.level[ln] = (uint8_t)i915_ddi_level(encoder, crtc_state, ln);

	/* An eDP port remembers whether the HOBL table drives it. */
	if (levels.kind == I915_XD_OUTPUT_EDP) {
		intel_dp = enc_to_intel_dp(encoder);
		intel_dp->hobl_active = drv_i915_xd_combo_swing_is_hobl(drv_i915_lcd_xd_platform(), levels.table) != 0;
	}

	/* Programs them. */
	drv_i915_xd_combo_signal_levels(drv_i915_lcd_xd_io(dev_priv), drv_i915_lcd_xd_platform(), &levels);
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

/* Powers up the lanes the crtc state uses on a combo PHY (the Linux intel_ddi_power_up_lanes()). */
static void
i915_ddi_power_up_lanes(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	struct intel_digital_port *dig_port;
	enum phy phy;
	bool is_combo;
	bool lane_reversal;

	/* Finds the device, the port object and its PHY. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);

	/* Only a combo PHY has the static power-down field. */
	is_combo = drv_i915_phy_is_combo(i915, phy);
	if (!is_combo)
		return;

	/* Powers up the used lanes through the xd DDI, reversed when the port's lanes are. */
	lane_reversal = dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;
	drv_i915_xd_combo_lanes(drv_i915_lcd_xd_io(i915), (unsigned)phy, (unsigned)crtc_state->lane_count, lane_reversal ? 1 : 0);
}

/* Programs the eDP multi-SST splitter of the pipe (the Linux intel_ddi_mso_configure()). */
static void
i915_ddi_mso_configure(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *i915;
	enum pipe pipe;
	u32 dss1;
	int display_ver;

	/* Finds the crtc, its device and its pipe. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915 = i915_lcd_to_i915(crtc->base.dev);
	pipe = crtc->pipe;
	dss1 = 0;

	/* The splitter exists from display version 12 (the Linux HAS_MSO()). */
	display_ver = I915_LCD_DISPLAY_VER(i915);
	if (!(display_ver >= 12))
		return;

	/* An enabled splitter selects its segments and overlap. */
	if (crtc_state->splitter.enable) {
		dss1 |= SPLITTER_ENABLE;
		dss1 |= OVERLAP_PIXELS(crtc_state->splitter.pixel_overlap);
		if (crtc_state->splitter.link_count == 2) {
			dss1 |= SPLITTER_CONFIGURATION_2_SEGMENT;
		} else {
			dss1 |= SPLITTER_CONFIGURATION_4_SEGMENT;
		}
	}

	/* Writes the splitter fields of the pipe. */
	(void)i915_lcd_intel_de_rmw(i915, ICL_PIPE_DSS_CTL1(pipe),
				    SPLITTER_ENABLE | SPLITTER_CONFIGURATION_MASK |
				    OVERLAP_PIXELS_MASK, dss1);
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

/*
 * Restarts the link in training pattern 1 and enables the DDI buffer
 * through the xd DDI (the prepare_link_retrain hook).  An Alder Lake-P
 * Type-C port in DP-alt or legacy mode takes the TBT-to-DP workaround
 * between the transport and the buffer.
 */
static void
i915_ddi_prepare_link_retrain(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port;
	struct intel_encoder *encoder;
	struct drm_i915_private *dev_priv;
	struct i915_xd_ddi_link link;
	void (*before_buffer)(void *context);
	unsigned late;

	/* Finds the port object, its encoder and the device. */
	dig_port = i915_lcd_dp_to_dig_port(intel_dp);
	encoder = &dig_port->base;
	dev_priv = i915_lcd_to_i915(encoder->base.dev);

	/* The workaround of an Alder Lake-P Type-C port in DP-alt or legacy mode. */
	before_buffer = NULL;
	if (I915_LCD_IS_ALDERLAKE_P(dev_priv) &&
	    (intel_tc_port_in_dp_alt_mode(dig_port) || intel_tc_port_in_legacy_mode(dig_port)))
		before_buffer = i915_ddi_tbt_to_dp_alt_hook;

	/* Starts the link with the word computed at the pre-enable. */
	i915_ddi_xd_link(encoder, crtc_state, &link);
	intel_dp->DP |= DDI_BUF_CTL_ENABLE;
	late = drv_i915_xd_ddi_dp_start(drv_i915_lcd_xd_io(dev_priv), &link, intel_dp->DP & ~DDI_BUF_CTL_ENABLE, before_buffer, encoder);
	i915_ddi_report_late(dev_priv, encoder->port, late);
}

/* Selects a training pattern in DP_TP_CTL through the xd DDI (the set_link_train hook). */
static void
i915_ddi_set_link_train(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state,
	u8 dp_train_pat)
{
	struct intel_encoder *encoder;
	struct drm_i915_private *dev_priv;
	unsigned pattern;

	/* Finds the port's encoder and its device. */
	encoder = &i915_lcd_dp_to_dig_port(intel_dp)->base;
	dev_priv = i915_lcd_to_i915(encoder->base.dev);

	/* The field of the requested pattern (without its scrambling bit); an unknown one reads as pattern 1. */
	switch (intel_dp_training_pattern_symbol(dp_train_pat)) {
	case DP_TRAINING_PATTERN_DISABLE:
		pattern = I915_XD_DP_TP_PATTERN_NORMAL;
		break;
	case DP_TRAINING_PATTERN_2:
		pattern = I915_XD_DP_TP_PATTERN_2;
		break;
	case DP_TRAINING_PATTERN_3:
		pattern = I915_XD_DP_TP_PATTERN_3;
		break;
	case DP_TRAINING_PATTERN_4:
		pattern = I915_XD_DP_TP_PATTERN_4;
		break;
	default:
		pattern = I915_XD_DP_TP_PATTERN_1;
		break;
	}

	/* Writes it. */
	drv_i915_xd_ddi_pattern(drv_i915_lcd_xd_io(dev_priv), (unsigned)i915_tgl_dp_tp_transcoder(crtc_state), pattern);
}

/*
 * Selects the idle pattern and waits for the idle patterns to be sent
 * through the xd DDI (the set_idle_link_train hook).
 */
static void
i915_ddi_set_idle_link_train(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder;
	struct drm_i915_private *dev_priv;
	unsigned late;

	/* Finds the port's encoder and its device. */
	encoder = &i915_lcd_dp_to_dig_port(intel_dp)->base;
	dev_priv = i915_lcd_to_i915(encoder->base.dev);

	/* The idle pattern and its wait. */
	late = drv_i915_xd_ddi_idle_patterns(drv_i915_lcd_xd_io(dev_priv), (unsigned)i915_tgl_dp_tp_transcoder(crtc_state));
	i915_ddi_report_late(dev_priv, encoder->port, late);
}

/*
 * Disables the DDI buffer, the link's DP_TP_CTL and FEC through the xd
 * DDI, and waits for the buffer to go idle.
 */
static void
i915_disable_ddi_buf_ctl(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_ddi_link link;
	unsigned late;

	/* Stops the buffer (and for DP the transport). */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	i915_ddi_xd_link(encoder, crtc_state, &link);
	late = drv_i915_xd_ddi_stop(drv_i915_lcd_xd_io(dev_priv), &link, intel_crtc_has_dp_encoder(crtc_state) ? 1 : 0);
	i915_ddi_report_late(dev_priv, encoder->port, late);
}

/* Disables the DDI buffer of the display version (the Linux intel_disable_ddi_buf()). */
static void
i915_disable_ddi_buf(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv __maybe_unused;
	struct drm_i915_private *cur_i915;
	int display_ver;

	/* Finds the devices. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);

	/* Runs the disable of the display version. */
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	if (display_ver >= 14)
		I915_LCD_MTL_DISABLE_DDI_BUF(cur_i915, encoder, crtc_state);
	else
		i915_disable_ddi_buf_ctl(encoder, crtc_state);

	/* Waits for FEC to report disabled. */
	intel_ddi_wait_for_fec_status(encoder, crtc_state, false);
}

/*
 * Disables the transcoder function (the Linux
 * intel_ddi_disable_transcoder_func(); the quirk's sleep runs on the entry
 * point's device).
 */
static void
i915_ddi_disable_transcoder_func(
	struct drm_i915_private *cur_i915,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *dev_priv;
	enum transcoder cpu_transcoder;
	u32 ctl;
	bool mst_master;
	bool has_hdmi;

	/* Finds the crtc, its device and its transcoder. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	dev_priv = i915_lcd_to_i915(crtc->base.dev);
	cpu_transcoder = crtc_state->cpu_transcoder;

	/* Clears the port-sync word and the function's enable (and its port and mode, but on an MST master). */
	mst_master = intel_dp_mst_is_master_trans(crtc_state);
	ctl = drv_i915_xd_pipe_func_disable(drv_i915_lcd_xd_io(dev_priv), (unsigned)cpu_transcoder, mst_master ? 1 : 0);

	/* HDCP signalling still on is a warning. */
	(void)I915_LCD_DRM_WARN_ON(crtc->base.dev, ctl & TRANS_DDI_HDCP_SIGNALLING);

	/* A quirked HDMI sink is given 100 ms with the DDI disabled. */
	if (!I915_LCD_INTEL_HAS_QUIRK(dev_priv, QUIRK_INCREASE_DDI_DISABLED_TIME))
		return;

	/* The quirk applies to an HDMI stream only. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (!has_hdmi)
		return;

	/* Reports the quirk. */
	I915_LCD_DRM_DBG_KMS(&dev_priv->drm,
			     "Quirk Increase DDI disabled time\n");

	/* Quirk time at 100ms for reliable operation */
	i915_lcd_msleep(cur_i915, 100);
}

/*
 * Sets or clears the sink's MSA_TIMING_PAR_IGNORE for a VRR stream (the
 * Linux intel_dp_sink_set_msa_timing_par_ignore_state(); the DPCD write
 * runs on the entry point's device).
 */
static void
i915_dp_sink_set_msa_timing_par_ignore_state(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state,
	bool enable)
{
	struct drm_i915_private *i915 __maybe_unused;
	struct drm_i915_private *cur_i915;
	long written;
	u8 value;

	/* Finds the port's device and the entry point's device. */
	i915 = i915_lcd_dp_to_i915(intel_dp);
	cur_i915 = i915_ddi_cur_i915(&i915_lcd_dp_to_dig_port(intel_dp)->base);

	/* Only a VRR stream asks the sink to ignore the MSA timing. */
	if (!crtc_state->vrr.enable)
		return;

	/* Writes the sink's control byte; a failed write is only logged. */
	value = 0;
	if (enable)
		value = DP_MSA_TIMING_PAR_IGNORE_EN;
	written = I915_LCD_DRM_DP_DPCD_WRITEB(cur_i915, &intel_dp->aux, DP_DOWNSPREAD_CTRL, value);
	if (written <= 0) {
		I915_LCD_DRM_DBG_KMS(&i915->drm,
				     "Failed to %s MSA_TIMING_PAR_IGNORE in the sink\n",
				     i915_str_enable_disable(enable));
	}
}

/* Runs the DP half of the port disable before the transcoder stops (the Linux intel_disable_ddi_dp()). */
static void
i915_disable_ddi_dp(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp;
	struct intel_connector *connector __maybe_unused;

	UNUSED_PARAMETER(state);

	/* Finds the DP half and the connector. */
	intel_dp = enc_to_intel_dp(encoder);
	connector = I915_LCD_TO_INTEL_CONNECTOR(old_conn_state->connector);

	/* The link is no longer trained. */
	intel_dp->link_trained = false;

	/* Disables PSR and the eDP backlight. */
	intel_psr_disable(intel_dp, old_crtc_state);
	drv_i915_edp_backlight_off(old_conn_state);

	/* Disable the decompression in DP Sink */
	intel_dp_sink_disable_decompression(state,
					    connector, old_crtc_state);

	/* Disable Ignore_MSA bit in DP Sink */
	i915_dp_sink_set_msa_timing_par_ignore_state(intel_dp, old_crtc_state,
						     false);
}

/* Runs the port disable before the transcoder stops (the Linux intel_disable_ddi(), the disable hook). */
static void
i915_disable_ddi(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	bool has_hdmi;

	/* Cancels the Type-C link reset work and disables HDCP. */
	intel_tc_port_link_cancel_reset_work(i915_lcd_enc_to_dig_port(encoder));
	intel_hdcp_disable(I915_LCD_TO_INTEL_CONNECTOR(old_conn_state->connector));

	/* Runs the HDMI or the DP half. */
	has_hdmi = intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI);
	if (has_hdmi) {
		i915_disable_ddi_hdmi(state, encoder, old_crtc_state,
				      old_conn_state);
	} else {
		i915_disable_ddi_dp(state, encoder, old_crtc_state,
				    old_conn_state);
	}
}

/* Runs the DP half of the port disable after the transcoder stopped (the Linux intel_ddi_post_disable_dp()). */
static void
i915_ddi_post_disable_dp(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv;
	struct drm_i915_private *cur_i915;
	struct intel_digital_port *dig_port;
	struct intel_dp *intel_dp;
	enum transcoder cpu_transcoder;
	intel_wakeref_t wakeref;
	int display_ver;
	bool is_mst;

	UNUSED_PARAMETER(state);

	/* Finds the devices, the port object, its DP half and whether the stream is MST. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	intel_dp = &dig_port->dp;
	is_mst = intel_crtc_has_type(old_crtc_state,
				     INTEL_OUTPUT_DP_MST);
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);

	/* A single stream disables its DP infoframes. */
	if (!is_mst) {
		drv_i915_dp_set_infoframes(encoder, false,
					   old_crtc_state, old_conn_state);
	}

	/*
	 * Power down sink before disabling the port, otherwise we end
	 * up getting interrupts from the sink on detecting link loss.
	 */
	drv_i915_dp_set_power(intel_dp, DP_SET_POWER_D3);

	/* Deselects the port of an MST transcoder, or the clock of an older SST one. */
	if (display_ver >= 12) {
		if (is_mst) {
			cpu_transcoder = old_crtc_state->cpu_transcoder;

			(void)i915_lcd_intel_de_rmw(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder),
						    TGL_TRANS_DDI_PORT_MASK | TRANS_DDI_MODE_SELECT_MASK,
						    0);
		}
	} else {
		if (!is_mst)
			i915_ddi_disable_transcoder_clock(old_crtc_state);
	}

	/* Disables the DDI buffer and the sink's FEC readiness. */
	i915_disable_ddi_buf(encoder, old_crtc_state);

	intel_dp_sink_set_fec_ready(intel_dp, old_crtc_state, false);

	/*
	 * From TGL spec: "If single stream or multi-stream master transcoder:
	 * Configure Transcoder Clock select to direct no clock to the
	 * transcoder"
	 */
	if (display_ver >= 12)
		i915_ddi_disable_transcoder_clock(old_crtc_state);

	/* Forces the panel's VDD and turns the panel's power off. */
	i915_lcd_intel_pps_vdd_on(cur_i915, intel_dp);
	i915_lcd_intel_pps_off(cur_i915, intel_dp);

	/* Gives the port's I/O power reference back. */
	wakeref = fetch_and_zero(&dig_port->ddi_io_wakeref);

	if (wakeref) {
		i915_lcd_intel_display_power_put(dev_priv,
						 dig_port->ddi_io_power_domain,
						 wakeref);
	}

	/* Gates the port clock. */
	i915_ddi_disable_clock(encoder);

	/* De-select Thunderbolt */
	if (display_ver >= 14) {
		(void)i915_lcd_intel_de_rmw(dev_priv, XELPDP_PORT_BUF_CTL1(encoder->port),
					    XELPDP_PORT_BUF_IO_SELECT_TBT, 0);
	}
}

/*
 * Stops the transcoder and runs the port disable after it (the Linux
 * intel_ddi_post_disable(), the post_disable hook).
 */
static void
i915_ddi_post_disable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv __maybe_unused;
	struct drm_i915_private *cur_i915;
	struct i915_lcd_world *world;
	bool has_dp_mst;
	bool has_hdmi;
	int display_ver;

	/* Finds the devices and the world of the encoder's modeset object. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	world = i915_ddi_modeset(encoder)->world;

	/* A single stream turns the vblank off and stops the transcoder and its function. */
	has_dp_mst = intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DP_MST);
	if (!has_dp_mst) {
		I915_LCD_INTEL_CRTC_VBLANK_OFF(world, old_crtc_state);

		drv_i915_disable_transcoder(old_crtc_state);

		i915_ddi_disable_transcoder_func(cur_i915, old_crtc_state);

		intel_dsc_disable(old_crtc_state);

		/* Display version 9 and later disable the scaler, earlier ones the panel fitter. */
		display_ver = I915_LCD_DISPLAY_VER(dev_priv);
		if (display_ver >= 9) {
			skl_scaler_disable(old_crtc_state);
		} else {
			I915_LCD_ILK_PFIT_DISABLE(cur_i915, old_crtc_state);
		}
	}

	/*
	 * XXX: Linux walks the crtcs of the big-joiner slave pipes here
	 * (for_each_intel_crtc_in_pipe_mask over the takeover registry) and
	 * turns off each slave's vblank, DSC and scaler.  The mask is
	 * intel_crtc_bigjoiner_slave_pipes(), which is the literal 0 in this
	 * environment, so the walk never ran its body; the registry is not
	 * reachable from this hook, so the walk is not written out.  The guard
	 * reports a nonzero mask should the answer ever change.
	 */
	I915_LCD_GUARD(intel_crtc_bigjoiner_slave_pipes(old_crtc_state) == 0,
		       "the big-joiner slave pipes of intel_ddi_post_disable");

	/*
	 * When called from DP MST code:
	 * - old_conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - old_crtc_state will be the state of the last stream to
	 *   be deactivated on this port, and it may not be the same
	 *   stream that was activated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	/* Runs the HDMI or the DP half. */
	has_hdmi = intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI);
	if (has_hdmi) {
		i915_ddi_post_disable_hdmi(state, encoder, old_crtc_state,
					   old_conn_state);
	} else {
		i915_ddi_post_disable_dp(state, encoder, old_crtc_state,
					 old_conn_state);
	}
}

/*
 * Gives the main link's AUX power back after the PLL is disabled (the
 * Linux intel_ddi_post_pll_disable(), the post_pll_disable hook).
 */
static void
i915_ddi_post_pll_disable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915;
	struct drm_i915_private *cur_i915;
	struct intel_digital_port *dig_port;
	enum phy phy;
	bool is_tc_port;

	UNUSED_PARAMETER(state);
	UNUSED_PARAMETER(old_conn_state);

	/* Finds the devices, the port object and whether its PHY is Type-C. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);
	is_tc_port = drv_i915_lcd_intel_phy_is_tc(i915, phy);

	/* Gives the AUX power reference back. */
	i915_main_link_aux_power_domain_put(dig_port, old_crtc_state);

	/* A Type-C port gives its link reference back. */
	if (is_tc_port)
		I915_LCD_INTEL_TC_PORT_PUT_LINK(cur_i915, dig_port);
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

/* Enables the clock, I/O power and infoframes of an HDMI port (the Linux intel_ddi_pre_enable_hdmi()). */
static void
i915_ddi_pre_enable_hdmi(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port;
	struct intel_hdmi *intel_hdmi;
	struct drm_i915_private *dev_priv;

	UNUSED_PARAMETER(state);

	/* Finds the port object, its HDMI half and its device. */
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	intel_hdmi = &dig_port->hdmi;
	dev_priv = i915_lcd_to_i915(encoder->base.dev);

	/* Switches a DP dual-mode adaptor to TMDS and maps the port to its PLL. */
	drv_i915_dp_dual_mode_set_tmds_output(intel_hdmi, true);
	i915_ddi_enable_clock(encoder, crtc_state);

	/* Takes the port's I/O power reference. */
	(void)I915_LCD_DRM_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
	dig_port->ddi_io_wakeref = i915_lcd_intel_display_power_get(dev_priv,
								    dig_port->ddi_io_power_domain);

	/* Programs the DP mode of a Type-C PHY and the transcoder's clock. */
	i915_icl_program_mg_dp_mode(dig_port, crtc_state);

	i915_ddi_enable_transcoder_clock(encoder, crtc_state);

	/* Writes the infoframes the stream carries. */
	dig_port->set_infoframes(encoder,
				 crtc_state->has_infoframe,
				 crtc_state, conn_state);
}

/*
 * Sets up the sink's scrambling, the signal levels and the lanes of an
 * HDMI port, then enables its DDI buffer (the Linux intel_enable_ddi_hdmi()).
 */
static void
i915_enable_ddi_hdmi(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv;
	struct drm_i915_private *cur_i915;
	struct intel_digital_port *dig_port;
	struct drm_connector *connector;
	enum port port;
	enum phy phy;
	struct i915_xd_ddi_link link;
	unsigned late;
	i915_reg_t reg;
	u32 val;
	int display_ver;
	bool scrambling_set;
	bool is_tc;
	bool is_alderlake_p;

	UNUSED_PARAMETER(state);

	/*
	 * Finds the devices, the port object, its connector, port and PHY.
	 *
	 * XXX: the connector state's connector is the modeset object's struct
	 * intel_connector, read here through struct drm_connector as the Linux
	 * text does; the two layouts differ in this environment, so the scdc
	 * capabilities and the name read by the scrambling are not those of
	 * the intel_connector's display_info.  Kept as it was.
	 */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	connector = conn_state->connector;
	port = encoder->port;
	phy = drv_i915_lcd_intel_port_to_phy(dev_priv, port);
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	is_alderlake_p = I915_LCD_IS_ALDERLAKE_P(dev_priv);

	/* Sets the sink's scrambling and TMDS clock ratio; a failure is only logged. */
	scrambling_set = drv_i915_hdmi_handle_sink_scrambling(encoder, connector,
							       crtc_state->hdmi_high_tmds_clock_ratio,
							       crtc_state->hdmi_scrambling);
	if (!scrambling_set) {
		I915_LCD_DRM_DBG_KMS(&dev_priv->drm,
				     "[CONNECTOR:%d:%s] Failed to configure sink scrambling/TMDS bit clock ratio\n",
				     connector->base.id, connector->name);
	}

	/* A platform with the buffer-translation select prepares the HDMI buffers. */
	if (has_buf_trans_select(dev_priv))
		hsw_prepare_hdmi_ddi_buffers(encoder, crtc_state);

	/* e. Enable D2D Link for C10/C20 Phy */
	if (display_ver >= 14)
		mtl_ddi_enable_d2d(encoder);

	/* Programs the signal levels. */
	encoder->set_signal_levels(encoder, crtc_state);

	/* Display WA #1143: skl,kbl,cfl */
	if (display_ver == 9 && !IS_BROXTON(dev_priv)) {
		/*
		 * For some reason these chicken bits have been
		 * stuffed into a transcoder register, event though
		 * the bits affect a specific DDI port rather than
		 * a specific transcoder.
		 */
		reg = gen9_chicken_trans_reg_by_port(dev_priv, port);

		val = i915_lcd_intel_de_read(dev_priv, reg);

		if (port == PORT_E) {
			val |= DDIE_TRAINING_OVERRIDE_ENABLE |
				DDIE_TRAINING_OVERRIDE_VALUE;
		} else {
			val |= DDI_TRAINING_OVERRIDE_ENABLE |
				DDI_TRAINING_OVERRIDE_VALUE;
		}

		i915_lcd_intel_de_write(dev_priv, reg, val);
		i915_lcd_intel_de_posting_read(dev_priv, reg);

		i915_lcd_udelay(cur_i915, 1);

		if (port == PORT_E) {
			val &= ~(DDIE_TRAINING_OVERRIDE_ENABLE |
				 DDIE_TRAINING_OVERRIDE_VALUE);
		} else {
			val &= ~(DDI_TRAINING_OVERRIDE_ENABLE |
				 DDI_TRAINING_OVERRIDE_VALUE);
		}

		i915_lcd_intel_de_write(dev_priv, reg, val);
	}

	/* Powers up the used lanes. */
	i915_ddi_power_up_lanes(encoder, crtc_state);

	/* In HDMI/DVI mode, the port width, and swing/emphasis values
	 * are ignored so nothing special needs to be done besides
	 * enabling the port.
	 *
	 * On ADL_P the PHY link rate and lane count must be programmed but
	 * these are both 0 for HDMI.
	 *
	 * But MTL onwards HDMI2.1 is supported and in TMDS mode this
	 * is filled with lane count, already set in the crtc_state.
	 * The same is required to be filled in PORT_BUF_CTL for C10/20 Phy.
	 */
	if (display_ver >= 14) {
		I915_LCD_STEP(cur_i915, "mtl_enable_ddi_buf_hdmi");
	} else {
		/* The buffer through the xd DDI (a Type-C port only in legacy mode). */
		is_tc = drv_i915_lcd_intel_phy_is_tc(dev_priv, phy);
		if (is_tc && is_alderlake_p)
			(void)I915_LCD_DRM_WARN_ON(&dev_priv->drm, !intel_tc_port_in_legacy_mode(dig_port));
		i915_ddi_xd_link(encoder, crtc_state, &link);
		late = drv_i915_xd_ddi_hdmi_start(drv_i915_lcd_xd_io(dev_priv), drv_i915_lcd_xd_platform(), &link);
		i915_ddi_report_late(dev_priv, port, late);
	}
}

/* Resets the sink's scrambling and TMDS clock ratio (the Linux intel_disable_ddi_hdmi()). */
static void
i915_disable_ddi_hdmi(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 __maybe_unused;
	struct drm_connector *connector;
	bool scrambling_reset;

	UNUSED_PARAMETER(state);
	UNUSED_PARAMETER(old_crtc_state);

	/* Finds the device and the connector (read as in the enable, see there). */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	connector = old_conn_state->connector;

	/* Resets the sink; a failure is only logged. */
	scrambling_reset = drv_i915_hdmi_handle_sink_scrambling(encoder, connector,
								 false, false);
	if (!scrambling_reset) {
		I915_LCD_DRM_DBG_KMS(&i915->drm,
				     "[CONNECTOR:%d:%s] Failed to reset sink scrambling/TMDS bit clock ratio\n",
				     connector->base.id, connector->name);
	}
}

/*
 * Disables the infoframes, the DDI buffer, the clock and the I/O power of
 * an HDMI port after the transcoder stopped (the Linux
 * intel_ddi_post_disable_hdmi()).
 */
static void
i915_ddi_post_disable_hdmi(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *old_crtc_state,
	const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct intel_hdmi *intel_hdmi;
	intel_wakeref_t wakeref;
	int display_ver;

	UNUSED_PARAMETER(state);

	/* Finds the device, the port object and its HDMI half. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	dig_port = i915_lcd_enc_to_dig_port(encoder);
	intel_hdmi = &dig_port->hdmi;
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);

	/* Disables the infoframes. */
	dig_port->set_infoframes(encoder, false,
				 old_crtc_state, old_conn_state);

	/* Before display version 12 the transcoder clock goes first. */
	if (display_ver < 12)
		i915_ddi_disable_transcoder_clock(old_crtc_state);

	/* Disables the DDI buffer. */
	i915_disable_ddi_buf(encoder, old_crtc_state);

	/* From display version 12 the transcoder clock goes after the buffer. */
	if (display_ver >= 12)
		i915_ddi_disable_transcoder_clock(old_crtc_state);

	/* Gives the port's I/O power reference back. */
	wakeref = fetch_and_zero(&dig_port->ddi_io_wakeref);
	if (wakeref) {
		i915_lcd_intel_display_power_put(dev_priv,
						 dig_port->ddi_io_power_domain,
						 wakeref);
	}

	/* Gates the port clock and switches a DP dual-mode adaptor off TMDS. */
	i915_ddi_disable_clock(encoder);

	drv_i915_dp_dual_mode_set_tmds_output(intel_hdmi, false);
}

/*
 * Tells whether a combo PHY port's DDI clock is ungated (the Linux
 * icl_ddi_combo_is_clock_enabled(), the is_clock_enabled hook).
 */
static bool
i915_icl_ddi_combo_is_clock_enabled(
	struct intel_encoder *encoder)
{
	struct drm_i915_private *i915;
	unsigned id;
	enum phy phy;
	int enabled;

	/* Finds the device and the port's PHY. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);

	/* Reads the PHY's clock-off bit (xd clock). */
	enabled = drv_i915_dpll_ddi_mapped(i915, (unsigned)phy, &id);

	/* Succeeded: reports whether the clock is ungated. */
	return enabled != 0;
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
	u32 pin_assignment;
	u8 width;
	bool is_tc;
	bool in_tbt_alt_mode;
	bool in_legacy_mode;
	int display_ver;
	int mode_result;

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

	/* The pin assignment (DPPATC) and the link's width. */
	pin_assignment = i915_lcd_tc_port_get_pin_assignment_mask(dig_port);
	width = (u8)crtc_state->lane_count;

	/* A fixed connection: only a legacy port has one. */
	if (pin_assignment == 0x0) {
		in_legacy_mode = intel_tc_port_in_legacy_mode(dig_port);
		if (!in_legacy_mode)
			drv_i915_lcd_error("WARN_ON(!intel_tc_port_in_legacy_mode(dig_port))\n");
	}

	/* Programs both lanes' DP mode through the xd Type-C PHY. */
	mode_result = drv_i915_xd_tc_dp_mode(drv_i915_lcd_xd_dkl(dev_priv), (unsigned)tc_port, pin_assignment, width);
	if (mode_result != 0)
		I915_LCD_MISSING_CASE(pin_assignment);
}

/*
 * Selects a Type-C port's crtc's active port PLL by the port's mode (the
 * Linux intel_ddi_update_active_dpll() with the Alder Lake-P and Tiger
 * Lake PLL manager's icl_update_active_dpll()).
 *
 * A port that is not Type-C, and display version 14, are left alone.  No
 * big joiner slave pipes exist in this path.
 */
static void
i915_ddi_update_active_dpll(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	struct intel_crtc *crtc)
{
	struct drm_i915_private *i915;
	enum phy phy;
	bool is_tc;
	int display_ver;

	/* Finds the device, its display version and the port's PHY. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	display_ver = I915_LCD_DISPLAY_VER(i915);
	phy = drv_i915_lcd_intel_port_to_phy(i915, encoder->port);

	/* Display version 14 has no such PLL manager yet (a FIXME of the reference). */
	if (display_ver >= 14)
		return;

	/* Only a Type-C port has two PLLs to choose from. */
	is_tc = drv_i915_lcd_intel_phy_is_tc(i915, phy);
	if (!is_tc)
		return;

	/* Selects the crtc's active port PLL. */
	drv_i915_icl_update_active_dpll(state, crtc, encoder);
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

	/* Finds the device and the Type-C port. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	tc_port = i915_lcd_intel_port_to_tc(i915, encoder->port);

	/* Releases the core soft reset of both lanes through the xd Type-C PHY. */
	drv_i915_xd_tc_release_soft_reset(drv_i915_lcd_xd_dkl(i915), (unsigned)tc_port);
}

/*
 * The DDI clock select of a Type-C port's crtc PLL through the xd clock:
 * the port's own PLL, or the Thunderbolt PLL's output of the link rate.
 * A combo PLL, or a rate the Thunderbolt PLL does not put out, is
 * reported and selects nothing.
 */
static u32
i915_icl_pll_to_ddi_clk_sel(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	const struct i915_xd_pll_desc *desc;
	u32 select;

	UNUSED_PARAMETER(encoder);

	/* The platform's PLL of the crtc's PLL id. */
	desc = drv_i915_xd_dpll_desc(drv_i915_lcd_xd_platform(), (unsigned)crtc_state->shared_dpll->info->id);
	if (desc == NULL) {
		I915_LCD_MISSING_CASE(crtc_state->shared_dpll->info->id);
		return I915_XD_DDI_CLK_SEL_NONE;
	}

	/* Its select for the link rate. */
	select = drv_i915_xd_tc_clock_select(desc, (uint32_t)crtc_state->port_clock);
	if (select == I915_XD_DDI_CLK_SEL_NONE)
		I915_LCD_MISSING_CASE(crtc_state->port_clock);

	/* Succeeded: the select (none when it was reported). */
	return select;
}

/*
 * Selects a Type-C port's clock and ungates it (the Linux
 * icl_ddi_tc_enable_clock(), the enable_clock hook).
 *
 * The port's DDI clock select names the crtc's PLL; the gate is in the
 * clock register the combo PHYs share, changed under the DPLL lock.
 */
static void
i915_icl_ddi_tc_enable_clock(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	struct drm_i915_private *cur_i915;
	const struct intel_shared_dpll *pll;
	enum tc_port tc_port;
	enum port port;
	u32 clk_sel;
	bool warned;

	/* Finds the devices, the crtc's PLL and the port. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	pll = crtc_state->shared_dpll;
	tc_port = i915_lcd_intel_port_to_tc(i915, encoder->port);
	port = encoder->port;

	/* A crtc state without a PLL is a warning, and nothing is selected. */
	warned = I915_LCD_DRM_WARN_ON(&i915->drm, !pll);
	if (warned)
		return;

	/* Selects the PLL for the port and ungates its clock through the xd clock, under the DPLL lock. */
	clk_sel = i915_icl_pll_to_ddi_clk_sel(encoder, crtc_state);
	I915_LCD_MUTEX_LOCK(cur_i915, &i915->display.dpll.lock);

	drv_i915_xd_tc_clock_map(drv_i915_lcd_xd_io(i915), (unsigned)port, (unsigned)tc_port, clk_sel);

	I915_LCD_MUTEX_UNLOCK(cur_i915, &i915->display.dpll.lock);
}

/*
 * Gates a Type-C port's clock and deselects it (the Linux
 * icl_ddi_tc_disable_clock(), the disable_clock hook).
 */
static void
i915_icl_ddi_tc_disable_clock(
	struct intel_encoder *encoder)
{
	struct drm_i915_private *i915;
	struct drm_i915_private *cur_i915;
	enum tc_port tc_port;
	enum port port;

	/* Finds the devices and the port. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_ddi_cur_i915(encoder);
	tc_port = i915_lcd_intel_port_to_tc(i915, encoder->port);
	port = encoder->port;

	/* Gates the port's clock and deselects its PLL through the xd clock, under the DPLL lock. */
	I915_LCD_MUTEX_LOCK(cur_i915, &i915->display.dpll.lock);

	drv_i915_xd_tc_clock_unmap(drv_i915_lcd_xd_io(i915), (unsigned)port, (unsigned)tc_port);

	I915_LCD_MUTEX_UNLOCK(cur_i915, &i915->display.dpll.lock);
}

/*
 * Tells whether a Type-C port's clock runs (the Linux
 * icl_ddi_tc_is_clock_enabled(), the is_clock_enabled hook): a PLL is
 * selected and the port's gate is open.
 */
static bool
i915_icl_ddi_tc_is_clock_enabled(
	struct intel_encoder *encoder)
{
	struct drm_i915_private *i915;
	enum tc_port tc_port;
	enum port port;
	int mapped;

	/* Finds the device and the port. */
	i915 = i915_lcd_to_i915(encoder->base.dev);
	tc_port = i915_lcd_intel_port_to_tc(i915, encoder->port);
	port = encoder->port;

	/* A port that selects no PLL, or whose clock is gated, has no clock. */
	mapped = drv_i915_xd_tc_clock_mapped(drv_i915_lcd_xd_io(i915), (unsigned)port, (unsigned)tc_port);
	if (!mapped)
		return false;

	/* Succeeded: the port's clock runs. */
	return true;
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
	const struct i915_xd_dkl_swing_table *table;
	enum tc_port tc_port;
	uint8_t levels[4];
	int ln;
	bool in_tbt_alt_mode;
	bool has_hdmi;

	/* Finds the device and the Type-C port. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	tc_port = i915_lcd_intel_port_to_tc(dev_priv, encoder->port);

	/* A port in Thunderbolt-alt mode is the Thunderbolt controller's. */
	in_tbt_alt_mode = i915_lcd_intel_tc_port_in_tbt_alt_mode(i915_lcd_enc_to_dig_port(encoder));
	if (in_tbt_alt_mode)
		return;

	/* The port's xd swing table; a platform without Dekel tables is a warning. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	table = drv_i915_xd_tc_swing_table(drv_i915_lcd_xd_platform(), has_hdmi, (uint32_t)crtc_state->port_clock);
	if (table == NULL) {
		drv_i915_lcd_error("WARN_ON_ONCE(!trans)\n");
		return;
	}

	/* The level of each link lane. */
	for (ln = 0; ln < 4; ln++)
		levels[ln] = (uint8_t)i915_ddi_level(encoder, crtc_state, ln);

	/* Programs the transmitters through the xd Type-C PHY. */
	drv_i915_xd_tc_signal_levels(drv_i915_lcd_xd_dkl(dev_priv), drv_i915_lcd_xd_platform(), (unsigned)tc_port, table, levels,
		has_hdmi);
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

/*
 * The buffer-translation view of a Type-C port's xd Dekel swing table:
 * its length and HDMI default row, which the DP and HDMI level choices
 * read (the rows themselves are written by the xd Type-C PHY).
 * Rewritten by every call; read at once by the modeset that asked, under
 * its locks.
 */
static struct intel_ddi_buf_trans i915_dkl_trans_view;

/* The get_buf_trans hook of a Type-C port: the view of its xd Dekel swing table. */
static const struct intel_ddi_buf_trans *
i915_dkl_get_buf_trans(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct i915_xd_dkl_swing_table *table;
	bool has_hdmi;

	UNUSED_PARAMETER(encoder);

	/* The table of the output and rate; a platform without Dekel tables has none. */
	has_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	table = drv_i915_xd_tc_swing_table(drv_i915_lcd_xd_platform(), has_hdmi, (uint32_t)crtc_state->port_clock);
	if (table == NULL) {
		*n_entries = 0;
		return NULL;
	}

	/* The table's length and default. */
	i915_dkl_trans_view.entries = NULL;
	i915_dkl_trans_view.num_entries = (u8)table->count;
	i915_dkl_trans_view.hdmi_default_entry = (u8)table->hdmi_default;
	*n_entries = (int)table->count;

	/* Succeeded: the view. */
	return &i915_dkl_trans_view;
}

/*
 * Binds an encoder's buffer-translation hook as intel_ddi_buf_trans_init()
 * does: a Type-C (DKL) PHY takes the view of its xd Dekel swing table, a
 * combo PHY the view of its xd swing table.
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

	/* A Type-C PHY takes its xd Dekel swing table. */
	if (is_tc) {
		encoder->get_buf_trans = i915_dkl_get_buf_trans;
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

/* Runs the TBT-to-DP workaround of an Alder Lake-P Type-C port for the xd DDI's start. */
static void
i915_ddi_tbt_to_dp_alt_hook(
	void *context)
{
	/* The context is the port's encoder. */
	i915_adlp_tbt_to_dp_alt_switch_wa(context);
}

/* Reports the DDI buffer and transport waits that ran out, as the modeset's errors. */
static void
i915_ddi_report_late(
	struct drm_i915_private *i915,
	enum port port,
	unsigned late)
{
	UNUSED_PARAMETER(i915);

	/* One error per wait. */
	if ((late & I915_XD_DDI_LATE_IDLE) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "Timeout waiting for DDI BUF %c to get idle\n", port_name(port));
	if ((late & I915_XD_DDI_LATE_ACTIVE) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "Timeout waiting for DDI BUF %c to get active\n", port_name(port));
	if ((late & I915_XD_DDI_LATE_IDLE_PATTERNS) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "Timed out waiting for DP idle patterns\n");
}
