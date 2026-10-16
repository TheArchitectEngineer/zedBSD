/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The xd output (xd-output.c) of a modeset object's screen.
 *
 * The modeset object still owns the screen's commit: its backend, its
 * locks, its error count and the state the status, the flips and the
 * plane read.  The output's steps reach the backend through the
 * environment filled here, the output is described from the object's
 * crtc state before a light, and after each step the object's state is
 * brought in line with what the output holds (the crtc's running flag,
 * the link, the PLL, the power references, the backlight), so the
 * readers of the object see what they saw before.
 */

#include "modeset-internal.h"
#include "dp.h"
#include "pipe.h"
#include "xd-ddi.h"
#include "xd-output.h"

#include "../intel/mreg.h"

/*
 * The backlight controllers of the south displays this path drives (two
 * from the Ice Lake PCH on, one on DG1, two again from Meteor Lake), and
 * the range of PCHs whose second controller shares its pins.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_backlight.c
 * (cnp_num_backlight_controllers, cnp_backlight_controller_is_valid).
 */
#define I915_OUTPUT_SHELL_CONTROLLERS	2U

static int i915_output_shell_power_get(void *context, unsigned use, unsigned index);
static void i915_output_shell_power_put(void *context, unsigned use, unsigned index, int cookie);
static void i915_output_shell_power_put_later(void *context, unsigned use, unsigned index, int cookie, int delay_ms);
static int i915_output_shell_panel(void *context, unsigned step);
static void i915_output_shell_dbuf_slices(void *context, uint8_t slices);
static void i915_output_shell_lock(void *context, unsigned lock, int taken);
static void i915_output_shell_vblank_off(void *context);
static void i915_output_shell_link_failed(void *context);
static void i915_output_shell_observe(void *context, unsigned point);
static void i915_output_shell_error(void *context, const char *text);
static void i915_output_shell_debug(void *context, const char *text);
static void i915_output_shell_converter(void *context);
static void i915_output_shell_tc_get_link(void *context, unsigned lanes);
static void i915_output_shell_tc_put_link(void *context);
static unsigned i915_output_shell_tc_mode(void *context);
static void i915_output_shell_tc_lanes(void *context, unsigned lanes);
static void i915_output_shell_tc_dp_mode(void *context);
static void i915_output_shell_tc_levels(void *context, const uint8_t set[4]);
static int i915_output_shell_tc_pll(void *context, int on);
static void i915_output_shell_tc_phy_wa(void *context);
static enum intel_display_power_domain i915_output_shell_domain(struct i915_lcd_modeset *ms, unsigned use);
static void i915_output_shell_domain_held(struct i915_lcd_modeset *ms, unsigned use, enum intel_display_power_domain domain);

/*
 * The Type-C hooks of an external DisplayPort sink's output: the
 * object's Type-C port code (its link, its mode, its lanes), its PHY
 * code (the DisplayPort mode, the signal levels, the reset of a port
 * that was in Thunderbolt mode) and its PLL pool.  Constant.
 */
static const struct i915_xd_output_tc i915_output_shell_tc = {
	i915_output_shell_tc_get_link,
	i915_output_shell_tc_put_link,
	i915_output_shell_tc_mode,
	i915_output_shell_tc_lanes,
	i915_output_shell_tc_dp_mode,
	i915_output_shell_tc_levels,
	i915_output_shell_tc_pll,
	i915_output_shell_tc_phy_wa,
};

/*
 * Fills the xd output's environment of a modeset object: the object's
 * register window and platform, its backend's power, panel, DPCD, DBUF
 * and lock hooks, the modeset's notes, and for an external DisplayPort
 * sink its converter and the Type-C port's hooks (the object's Type-C
 * and PHY code).
 */
void
drv_i915_lcd_ms_xo_env(
	struct i915_lcd_modeset *ms,
	struct i915_xd_output_env *env)
{
	/* The registers and the platform. */
	kern_memset(env, 0, sizeof(*env));
	env->io = drv_i915_lcd_xd_io(&ms->i915);
	env->platform = drv_i915_lcd_xd_platform();

	/* The backend's hooks, on the object. */
	env->power_get = i915_output_shell_power_get;
	env->power_put = i915_output_shell_power_put;
	env->power_put_later = i915_output_shell_power_put_later;
	env->panel = i915_output_shell_panel;
	drv_i915_dp_sink_bind(&ms->dig_port.dp, &env->sink);
	env->dbuf_slices = i915_output_shell_dbuf_slices;
	env->lock = i915_output_shell_lock;
	env->vblank_off = i915_output_shell_vblank_off;
	env->link_failed = i915_output_shell_link_failed;
	env->observe = i915_output_shell_observe;

	/* An external DisplayPort sink's converter and its Type-C port. */
	if (ms->output_dp_ext) {
		env->converter = i915_output_shell_converter;
		env->tc = &i915_output_shell_tc;
	}
	env->error = i915_output_shell_error;
	env->debug = i915_output_shell_debug;
	env->context = ms;
}

/*
 * Describes the panel's output from a modeset object's crtc state before
 * a light: the port, the pipe and transcoder, the mode, the link, the
 * PLL, the colours, the DMC programs, the sink's capabilities and the
 * VBT's backlight.  What the output holds is left as it is.
 */
void
drv_i915_lcd_ms_xo_describe(
	struct i915_lcd_modeset *ms)
{
	const struct intel_shared_dpll *pll;
	const struct intel_crtc_state *crtc_state;
	const struct drm_display_mode *mode;
	const struct intel_panel *panel;
	struct i915_xd_output *out;
	struct intel_dp *intel_dp;
	int index;

	crtc_state = &ms->crtc_state;
	mode = &crtc_state->hw.adjusted_mode;
	panel = &ms->connector.panel;
	intel_dp = &ms->dig_port.dp;
	out = &ms->xo;

	/* The output, its port and its pipe. */
	out->kind = I915_XD_SCREEN_PANEL;
	if (ms->output_hdmi)
		out->kind = I915_XD_SCREEN_HDMI;
	if (ms->output_dp_ext)
		out->kind = I915_XD_SCREEN_DP;
	out->ddi = (unsigned)ms->dig_port.base.port;
	out->phy = (unsigned)drv_i915_lcd_intel_port_to_phy(&ms->i915, ms->dig_port.base.port);
	out->aux = (unsigned)ms->dig_port.aux_ch;
	out->pipe = (unsigned)ms->crtc.pipe;
	out->trans = (unsigned)crtc_state->cpu_transcoder;

	/* The mode: timing, sync polarities, source, depth and colour. */
	drv_i915_lcd_xd_timing(mode, &out->timing);
	out->hsync_high = (mode->flags & DRM_MODE_FLAG_PHSYNC) != 0 ? 1 : 0;
	out->vsync_high = (mode->flags & DRM_MODE_FLAG_PVSYNC) != 0 ? 1 : 0;
	out->src_width = (uint32_t)(crtc_state->pipe_src.x2 - crtc_state->pipe_src.x1);
	out->src_height = (uint32_t)(crtc_state->pipe_src.y2 - crtc_state->pipe_src.y1);
	out->bpp = (unsigned)crtc_state->pipe_bpp;
	out->dither = crtc_state->dither ? 1 : 0;
	out->limited_range = crtc_state->limited_color_range ? 1 : 0;
	out->frame_start_delay = (unsigned)crtc_state->framestart_delay;
	out->pixel_multiplier = (unsigned)crtc_state->pixel_multiplier;

	/* The link and its M/N. */
	out->rate_khz = (uint32_t)crtc_state->port_clock;
	out->lanes = (unsigned)crtc_state->lane_count;
	out->enhanced = crtc_state->enhanced_framing ? 1 : 0;
	out->m_n.tu = crtc_state->dp_m_n.tu;
	out->m_n.data_m = crtc_state->dp_m_n.data_m;
	out->m_n.data_n = crtc_state->dp_m_n.data_n;
	out->m_n.link_m = crtc_state->dp_m_n.link_m;
	out->m_n.link_n = crtc_state->dp_m_n.link_n;
	out->kept = ms->dig_port.saved_port_bits;

	/* An eDP 1.4 panel set by its rate list takes the rate's index in it (0 when it is not listed). */
	out->rate_index = -1;
	if (intel_dp->use_rate_select) {
		out->rate_index = 0;
		for (index = 0; index < intel_dp->num_sink_rates; index++) {
			if (intel_dp->sink_rates[index] == crtc_state->port_clock)
				out->rate_index = index;
		}
	}

	/* A Type-C port, and the highest swing its PHY reaches. */
	out->tc = ms->output_dp_ext ? 1 : 0;
	out->tc_port = 0U;
	out->tc_swing_max = 0U;
	if (out->tc) {
		out->tc_port = (unsigned)(ms->dig_port.base.port - PORT_TC1);
		if (intel_dp->voltage_max != NULL)
			out->tc_swing_max = (uint8_t)(intel_dp->voltage_max(intel_dp, crtc_state) & DP_TRAIN_VOLTAGE_SWING_MASK);
	}

	/* The VBT's HDMI level and swing table settings of the panel. */
	out->hdmi_level = ms->hdmi_level_shift;
	out->swing_vbt = 0U;
	if (panel->vbt.edp.hobl)
		out->swing_vbt |= I915_XD_SWING_HOBL;
	if (panel->vbt.edp.low_vswing)
		out->swing_vbt |= I915_XD_SWING_LOW;
	out->hobl_failed = intel_dp->hobl_failed ? 1 : 0;

	/* The port's PLL the check gave the crtc, and its words; the AFC startup only with the VBT's override. */
	pll = crtc_state->shared_dpll;
	if (pll != NULL) {
		out->pll = (unsigned)pll->info->id;
		out->pll_words.cfgcr0 = pll->state.hw_state.cfgcr0;
		out->pll_words.cfgcr1 = pll->state.hw_state.cfgcr1;
		out->pll_words.div0 = pll->state.hw_state.div0;
	}
	out->pll_words.div0_set = ms->i915.display.vbt.override_afc_startup ? 1 : 0;

	/* The colours, the line time and the DMC programs. */
	out->gamma_mode = crtc_state->gamma_mode;
	out->csc_mode = crtc_state->csc_mode;
	out->linetime = HSW_LINETIME(crtc_state->linetime) | HSW_IPS_LINETIME(crtc_state->ips_linetime);
	out->dmc_programs = ms->i915.display.dmc.fw_mask;

	/* The sink's capabilities as the object read them. */
	kern_memcpy(out->caps, intel_dp->dpcd, sizeof(out->caps));
	kern_memcpy(out->repeater_caps, intel_dp->lttpr_common_caps, sizeof(out->repeater_caps));
	kern_memcpy(out->repeater_phy_caps, intel_dp->lttpr_phy_caps, sizeof(out->repeater_phy_caps));

	/* The VBT's backlight, the raw clock and the south display's controllers. */
	out->backlight.vbt_present = panel->vbt.backlight.present ? 1 : 0;
	out->backlight.vbt_controller = (unsigned)(u8)panel->vbt.backlight.controller;
	out->backlight.vbt_hz = panel->vbt.backlight.pwm_freq_hz;
	out->backlight.vbt_min = panel->vbt.backlight.min_brightness;
	out->backlight.rawclk_khz = I915_LCD_DISPLAY_RUNTIME_INFO(&ms->i915)->rawclk_freq;
	out->backlight.controllers = I915_OUTPUT_SHELL_CONTROLLERS;
	out->backlight.muxed = 0;
	if (INTEL_PCH_TYPE(&ms->i915) >= PCH_ICP && INTEL_PCH_TYPE(&ms->i915) <= PCH_ADP)
		out->backlight.muxed = 1;

	/* A panel that runs its brightness the other way, by the option or a quirk. */
	out->backlight.inverted = 0;
	if (ms->i915.display.params.invert_brightness > 0)
		out->backlight.inverted = 1;
	if (ms->i915.display.params.invert_brightness == 0 && I915_LCD_INTEL_HAS_QUIRK(&ms->i915, QUIRK_INVERT_BRIGHTNESS))
		out->backlight.inverted = 1;
}

/*
 * Describes the display buffer around the coming commit: the object's
 * current global DBUF state and the one the commit leaves.
 */
void
drv_i915_lcd_ms_xo_dbuf(
	struct i915_lcd_modeset *ms)
{
	struct i915_xd_output_dbuf *dbuf;

	dbuf = &ms->xo.dbuf;

	/* Before and after. */
	dbuf->slices_before = ms->wm.old_dbuf.enabled_slices;
	dbuf->slices_after = ms->wm.new_dbuf.enabled_slices;
	dbuf->joined_before = ms->wm.old_dbuf.joined_mbus ? 1U : 0U;
	dbuf->joined_after = ms->wm.new_dbuf.joined_mbus ? 1U : 0U;
	dbuf->pipes_before = ms->wm.old_dbuf.active_pipes;
	dbuf->pipes_after = ms->wm.new_dbuf.active_pipes;
}

/*
 * Brings a modeset object's state in line with what its panel's output
 * holds: the crtc running, the link and the DDI buffer's word, the PLL,
 * the port's and the commit's power references, the crtc's power
 * domains, and the backlight.
 */
void
drv_i915_lcd_ms_xo_sync(
	struct i915_lcd_modeset *ms)
{
	const struct i915_xd_output *out;
	struct intel_shared_dpll *pll;
	struct intel_panel *panel;
	struct intel_dp *intel_dp;
	unsigned held;

	out = &ms->xo;
	intel_dp = &ms->dig_port.dp;
	panel = &ms->connector.panel;
	held = out->held;

	/* The crtc and the link. */
	ms->crtc.active = out->lit != 0;
	intel_dp->link_trained = out->trained != 0;
	kern_memcpy(intel_dp->train_set, out->set, sizeof(intel_dp->train_set));
	intel_dp->DP = out->buf_word;
	intel_dp->hobl_active = out->hobl_active != 0;
	intel_dp->hobl_failed = out->hobl_failed != 0;

	/* The sink's capabilities as the training read them again. */
	kern_memcpy(intel_dp->dpcd, out->caps, sizeof(intel_dp->dpcd));
	kern_memcpy(intel_dp->lttpr_common_caps, out->repeater_caps, sizeof(intel_dp->lttpr_common_caps));
	kern_memcpy(intel_dp->lttpr_phy_caps, out->repeater_phy_caps, sizeof(intel_dp->lttpr_phy_caps));

	/* A combo port's PLL the check gave the crtc (a Type-C port's PLLs keep the pool's own record). */
	pll = ms->crtc_state.shared_dpll;
	if (pll != NULL && !out->tc) {
		pll->on = out->pll_on != 0;
		pll->active_mask = out->pll_pipes;
	}

	/* The port's references and the commit's exit from DC states. */
	ms->dig_port.ddi_io_wakeref = 0;
	if ((held & (1U << I915_XD_OUTPUT_POWER_DDI_IO)) != 0U)
		ms->dig_port.ddi_io_wakeref = out->cookie[I915_XD_OUTPUT_POWER_DDI_IO];
	ms->dig_port.aux_wakeref = 0;
	if ((held & (1U << I915_XD_OUTPUT_POWER_AUX)) != 0U)
		ms->dig_port.aux_wakeref = out->cookie[I915_XD_OUTPUT_POWER_AUX];
	ms->dc_off_held = (held & (1U << I915_XD_OUTPUT_POWER_DC_OFF)) != 0U ? 1 : 0;
	ms->dc_off_wakeref = out->cookie[I915_XD_OUTPUT_POWER_DC_OFF];

	/* The crtc's power domains. */
	i915_output_shell_domain_held(ms, I915_XD_OUTPUT_POWER_CORE, POWER_DOMAIN_DISPLAY_CORE);
	i915_output_shell_domain_held(ms, I915_XD_OUTPUT_POWER_PIPE, POWER_DOMAIN_PIPE(ms->crtc.pipe));
	i915_output_shell_domain_held(ms, I915_XD_OUTPUT_POWER_TRANS, POWER_DOMAIN_TRANSCODER(ms->crtc_state.cpu_transcoder));
	i915_output_shell_domain_held(ms, I915_XD_OUTPUT_POWER_LANES, (enum intel_display_power_domain)ms->dig_port.base.power_domain);

	/* The backlight. */
	panel->backlight.present = out->backlight.present != 0;
	panel->backlight.enabled = out->backlight.enabled != 0;
	panel->backlight.min = out->backlight.min;
	panel->backlight.max = out->backlight.max;
	panel->backlight.level = out->backlight.level;
	panel->backlight.pwm_level_min = out->backlight.min;
	panel->backlight.pwm_level_max = out->backlight.max;
	panel->backlight.controller = (u8)out->backlight.pwm.controller;
	panel->backlight.active_low_pwm = out->backlight.pwm.active_low != 0;
}

/* Takes a power reference of the panel's output through the object's backend: the cookie, or 0. */
static int
i915_output_shell_power_get(
	void *context,
	unsigned use,
	unsigned index)
{
	struct i915_lcd_modeset *ms;
	enum intel_display_power_domain domain;
	intel_wakeref_t cookie;

	(void)index;

	/* The domain the use names on the object's port. */
	ms = context;
	domain = i915_output_shell_domain(ms, use);

	/* The backend's reference. */
	cookie = i915_lcd_intel_display_power_get(&ms->i915, domain);

	/* Succeeded: the cookie (0 when the get failed). */
	return cookie;
}

/* Gives a power reference of the panel's output back through the object's backend. */
static void
i915_output_shell_power_put(
	void *context,
	unsigned use,
	unsigned index,
	int cookie)
{
	struct i915_lcd_modeset *ms;
	enum intel_display_power_domain domain;

	(void)index;

	/* The domain the use names, given back. */
	ms = context;
	domain = i915_output_shell_domain(ms, use);
	i915_lcd_intel_display_power_put(&ms->i915, domain, cookie);
}

/* Gives a power reference of the panel's output back after a delay through the object's backend. */
static void
i915_output_shell_power_put_later(
	void *context,
	unsigned use,
	unsigned index,
	int cookie,
	int delay_ms)
{
	struct i915_lcd_modeset *ms;
	enum intel_display_power_domain domain;

	(void)index;

	/* The domain the use names, given back later. */
	ms = context;
	domain = i915_output_shell_domain(ms, use);
	intel_display_power_put_async_delay(&ms->i915, domain, cookie, delay_ms);
}

/* Runs a step of the panel's power sequencer through the object's backend. */
static int
i915_output_shell_panel(
	void *context,
	unsigned step)
{
	struct i915_lcd_modeset *ms;
	int done;

	/* The backend's operation of the same number. */
	ms = context;
	done = i915_lcd_panel(&ms->i915, (int)step);
	if (done != 0)
		return done;

	/* Succeeded: the step ran. */
	return 0;
}

/* Asks the object's backend for exactly these display buffer slices. */
static void
i915_output_shell_dbuf_slices(
	void *context,
	uint8_t slices)
{
	struct i915_lcd_modeset *ms;

	/* The backend's request. */
	ms = context;
	ms->i915.emit->dbuf_slices_update(ms->i915.emit->ctx, (unsigned)slices);
}

/* Takes or drops the object's PLL or backlight lock. */
static void
i915_output_shell_lock(
	void *context,
	unsigned lock,
	int taken)
{
	struct i915_lcd_modeset *ms;

	ms = context;

	/* The PLLs' lock. */
	if (lock == I915_XD_OUTPUT_LOCK_PLL) {
		if (taken) {
			I915_LCD_MUTEX_LOCK(&ms->i915, &ms->i915.display.dpll.lock);
		} else {
			I915_LCD_MUTEX_UNLOCK(&ms->i915, &ms->i915.display.dpll.lock);
		}
		return;
	}

	/* The backlight's lock. */
	if (taken) {
		I915_LCD_MUTEX_LOCK(&ms->i915, &ms->i915.display.backlight.lock);
	} else {
		I915_LCD_MUTEX_UNLOCK(&ms->i915, &ms->i915.display.backlight.lock);
	}
}

/* Settles the pending flip event of the object's pipe before its transcoder stops. */
static void
i915_output_shell_vblank_off(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The object's world settles it. */
	ms = context;
	drv_i915_lcd_ms_vblank_off(ms->world);
}

/* Asks for a lower link after the panel's link did not train. */
static void
i915_output_shell_link_failed(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The port knows the high-efficiency table's part in it. */
	ms = context;
	ms->dig_port.dp.hobl_active = ms->xo.hobl_active != 0;
	ms->dig_port.dp.hobl_failed = ms->xo.hobl_failed != 0;
	drv_i915_dp_link_failed(&ms->dig_port.dp, &ms->crtc_state);
	ms->xo.hobl_failed = ms->dig_port.dp.hobl_failed ? 1 : 0;
}

/* Marks a point of the panel's commit on the object's backend. */
static void
i915_output_shell_observe(
	void *context,
	unsigned point)
{
	struct i915_lcd_modeset *ms;

	ms = context;

	/* The pipe's underruns: the backend samples them instead of the interrupt. */
	if (point == I915_XD_OUTPUT_SEE_UNDERRUN) {
		I915_LCD_DECIDED(&ms->i915, "intel_set_cpu_fifo_underrun_reporting: underrun IRQ stays masked; the test owns and samples ICL_PIPESTATUS");
		I915_LCD_OBSERVE(&ms->i915, I915_LCD_OBS_UNDERRUN_ARM);
		return;
	}

	/* The start and the end of the commit. */
	if (point == I915_XD_OUTPUT_SEE_BEGIN) {
		I915_LCD_OBSERVE(&ms->i915, I915_LCD_OBS_COMMIT_BEGIN);
	} else {
		I915_LCD_OBSERVE(&ms->i915, I915_LCD_OBS_COMMIT_END);
	}
}

/* Counts an error of the panel's output against the commit. */
static void
i915_output_shell_error(
	void *context,
	const char *text)
{
	(void)context;

	/* The modeset's error sink. */
	drv_i915_lcd_error(text);
}

/* Hands a debug note of the panel's output to the backend. */
static void
i915_output_shell_debug(
	void *context,
	const char *text)
{
	(void)context;

	/* The modeset's debug sink. */
	drv_i915_lcd_debug(text);
}

/* Sets up a branch's protocol converter through the sink's own AUX channel. */
static void
i915_output_shell_converter(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The object's sink. */
	ms = context;
	i915_lcd_dp_configure_protocol_converter(&ms->dig_port.dp);
}

/* Takes the Type-C port's link and picks the crtc's active port PLL by the port's mode. */
static void
i915_output_shell_tc_get_link(
	void *context,
	unsigned lanes)
{
	struct i915_lcd_modeset *ms;

	/* The link, then the PLL the mode selects. */
	ms = context;
	intel_tc_port_get_link(&ms->dig_port, (int)lanes);
	drv_i915_icl_update_active_dpll(&ms->state, &ms->crtc, &ms->dig_port.base);
}

/* Gives the Type-C port's link back. */
static void
i915_output_shell_tc_put_link(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The object's port. */
	ms = context;
	I915_LCD_INTEL_TC_PORT_PUT_LINK(&ms->i915, &ms->dig_port);
}

/* Reports the mode the Type-C port is held in. */
static unsigned
i915_output_shell_tc_mode(
	void *context)
{
	struct i915_lcd_modeset *ms;
	bool in_mode;

	ms = context;

	/* Thunderbolt, DisplayPort alternate, legacy, or none of them. */
	in_mode = i915_lcd_intel_tc_port_in_tbt_alt_mode(&ms->dig_port);
	if (in_mode)
		return I915_XD_OUTPUT_TC_TBT;
	in_mode = intel_tc_port_in_dp_alt_mode(&ms->dig_port);
	if (in_mode)
		return I915_XD_OUTPUT_TC_DP_ALT;
	in_mode = intel_tc_port_in_legacy_mode(&ms->dig_port);
	if (in_mode)
		return I915_XD_OUTPUT_TC_LEGACY;

	/* Succeeded: the port is in none of the modes. */
	return I915_XD_OUTPUT_TC_NONE;
}

/* Gives the display the Type-C PHY's lanes for a link of so many lanes. */
static void
i915_output_shell_tc_lanes(
	void *context,
	unsigned lanes)
{
	struct i915_lcd_modeset *ms;

	/* The object's port. */
	ms = context;
	intel_tc_port_set_fia_lane_count(&ms->dig_port, (int)lanes);
}

/* Programs the Type-C PHY's DisplayPort mode. */
static void
i915_output_shell_tc_dp_mode(
	void *context)
{
	/* The object's PHY. */
	drv_i915_lcd_ms_tc_dp_mode(context);
}

/* Programs the Type-C PHY's signal levels from the training's level bytes. */
static void
i915_output_shell_tc_levels(
	void *context,
	const uint8_t set[4])
{
	struct i915_lcd_modeset *ms;
	struct intel_encoder *encoder;

	/* The port's levels, then its PHY's hook. */
	ms = context;
	kern_memcpy(ms->dig_port.dp.train_set, set, sizeof(ms->dig_port.dp.train_set));
	encoder = &ms->dig_port.base;
	encoder->set_signal_levels(encoder, &ms->crtc_state);
}

/* Turns the crtc's active port PLL on or off through the PLL pool: 0. */
static int
i915_output_shell_tc_pll(
	void *context,
	int on)
{
	struct i915_lcd_modeset *ms;

	/* The pool's PLL of the crtc. */
	ms = context;
	if (on) {
		drv_i915_enable_shared_dpll(&ms->crtc_state);
	} else {
		drv_i915_disable_shared_dpll(&ms->crtc_state);
	}

	/* Succeeded: the pool reports its own errors. */
	return 0;
}

/* Releases the Type-C PHY's core reset before the DDI buffer starts. */
static void
i915_output_shell_tc_phy_wa(
	void *context)
{
	/* The object's PHY. */
	drv_i915_lcd_ms_tc_phy_wa(context);
}

/* Returns the power domain a use of the output names on the object's port and pipe. */
static enum intel_display_power_domain
i915_output_shell_domain(
	struct i915_lcd_modeset *ms,
	unsigned use)
{
	enum intel_display_power_domain domain;

	/* The domain of each use. */
	switch (use) {
	case I915_XD_OUTPUT_POWER_DC_OFF:
		domain = POWER_DOMAIN_DC_OFF;
		break;
	case I915_XD_OUTPUT_POWER_PIPE:
		domain = POWER_DOMAIN_PIPE(ms->crtc.pipe);
		break;
	case I915_XD_OUTPUT_POWER_TRANS:
		domain = POWER_DOMAIN_TRANSCODER(ms->crtc_state.cpu_transcoder);
		break;
	case I915_XD_OUTPUT_POWER_LANES:
		domain = (enum intel_display_power_domain)ms->dig_port.base.power_domain;
		break;
	case I915_XD_OUTPUT_POWER_DDI_IO:
		domain = (enum intel_display_power_domain)ms->dig_port.ddi_io_power_domain;
		break;
	case I915_XD_OUTPUT_POWER_AUX:
		domain = drv_i915_aux_power_domain(&ms->dig_port);
		break;
	default:
		domain = POWER_DOMAIN_DISPLAY_CORE;
		break;
	}

	/* Succeeded: the domain. */
	return domain;
}

/* Records in the crtc's set whether the output holds a use's domain. */
static void
i915_output_shell_domain_held(
	struct i915_lcd_modeset *ms,
	unsigned use,
	enum intel_display_power_domain domain)
{
	/* Held or not. */
	if ((ms->xo.held & (1U << use)) != 0U) {
		i915_set_bit(domain, ms->crtc.enabled_power_domains.mask.bits);
	} else {
		i915_clear_bit(domain, ms->crtc.enabled_power_domains.mask.bits);
	}
}
