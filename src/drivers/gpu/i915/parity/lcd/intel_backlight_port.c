// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_backlight.c
 * (sha256 990c147ae25d589cf38abf0ca7209609a0aa1a828a18b8ab822560775ebbeecf) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: scale, clamp_user_to_hw, scale_hw_to_user, intel_backlight_invert_pwm_level,
 *    intel_backlight_set_pwm_level, intel_backlight_level_to_pwm, intel_backlight_level_from_pwm, bxt_get_backlight,
 *    bxt_set_backlight, cnp_disable_backlight, cnp_enable_backlight, cnp_num_backlight_controllers,
 *    cnp_backlight_controller_is_valid, cnp_hz_to_pwm, get_vbt_pwm_freq, get_backlight_max_vbt,
 *    get_backlight_min_vbt, cnp_setup_backlight, intel_pwm_get_backlight, intel_pwm_set_backlight,
 *    intel_pwm_enable_backlight, intel_pwm_disable_backlight, intel_pwm_setup_backlight, __intel_backlight_enable,
 *    intel_backlight_enable, intel_backlight_disable, intel_panel_actually_set_backlight, scale_user_to_hw,
 *    intel_panel_set_backlight;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_mreg_backlight.h;
 *  - parity_backlight_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_mreg_backlight.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static u32 scale(u32 source_val,
		 u32 source_min, u32 source_max,
		 u32 target_min, u32 target_max);
static u32 clamp_user_to_hw(struct intel_connector *connector,
			    u32 user_level, u32 user_max);
static u32 scale_hw_to_user(struct intel_connector *connector,
			    u32 hw_level, u32 user_max);
static u32 bxt_get_backlight(struct intel_connector *connector, enum pipe unused);
static void bxt_set_backlight(const struct drm_connector_state *conn_state, u32 level);
static void cnp_disable_backlight(const struct drm_connector_state *old_conn_state, u32 val);
static void cnp_enable_backlight(const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state, u32 level);
static int cnp_num_backlight_controllers(struct drm_i915_private *i915);
static bool cnp_backlight_controller_is_valid(struct drm_i915_private *i915, int controller);
static u32 cnp_hz_to_pwm(struct intel_connector *connector, u32 pwm_freq_hz);
static u16 get_vbt_pwm_freq(struct intel_connector *connector);
static u32 get_backlight_max_vbt(struct intel_connector *connector);
static u32 get_backlight_min_vbt(struct intel_connector *connector);
static int
cnp_setup_backlight(struct intel_connector *connector, enum pipe unused);
static u32 intel_pwm_get_backlight(struct intel_connector *connector, enum pipe pipe);
static void intel_pwm_set_backlight(const struct drm_connector_state *conn_state, u32 level);
static void intel_pwm_enable_backlight(const struct intel_crtc_state *crtc_state,
				       const struct drm_connector_state *conn_state, u32 level);
static void intel_pwm_disable_backlight(const struct drm_connector_state *conn_state, u32 level);
static int intel_pwm_setup_backlight(struct intel_connector *connector, enum pipe pipe);
static void __intel_backlight_enable(const struct intel_crtc_state *crtc_state,
				     const struct drm_connector_state *conn_state);
static void
intel_panel_actually_set_backlight(const struct drm_connector_state *conn_state, u32 level);
static u32 scale_user_to_hw(struct intel_connector *connector,
			    u32 user_level, u32 user_max);
static void intel_panel_set_backlight(const struct drm_connector_state *conn_state,
				      u32 user_level, u32 user_max);

/**
 * scale - scale values from one range to another
 * @source_val: value in range [@source_min..@source_max]
 * @source_min: minimum legal value for @source_val
 * @source_max: maximum legal value for @source_val
 * @target_min: corresponding target value for @source_min
 * @target_max: corresponding target value for @source_max
 *
 * Return @source_val in range [@source_min..@source_max] scaled to range
 * [@target_min..@target_max].
 */
static u32 scale(u32 source_val,
		 u32 source_min, u32 source_max,
		 u32 target_min, u32 target_max)
{
	u64 target_val;

	if (WARN_ON(source_min >= source_max) ||
	    WARN_ON(target_min > target_max))
		return target_min;

	/* defensive */
	source_val = clamp(source_val, source_min, source_max);

	/* avoid overflows */
	target_val = mul_u32_u32(source_val - source_min,
				 target_max - target_min);
	target_val = DIV_ROUND_CLOSEST_ULL(target_val, source_max - source_min);
	target_val += target_min;

	return target_val;
}

/*
 * Scale user_level in range [0..user_max] to [0..hw_max], clamping the result
 * to [hw_min..hw_max].
 */
static u32 clamp_user_to_hw(struct intel_connector *connector,
			    u32 user_level, u32 user_max)
{
	struct intel_panel *panel = &connector->panel;
	u32 hw_level;

	hw_level = scale(user_level, 0, user_max, 0, panel->backlight.max);
	hw_level = clamp(hw_level, panel->backlight.min, panel->backlight.max);

	return hw_level;
}

/* Scale hw_level in range [hw_min..hw_max] to [0..user_max]. */
static u32 scale_hw_to_user(struct intel_connector *connector,
			    u32 hw_level, u32 user_max)
{
	struct intel_panel *panel = &connector->panel;

	return scale(hw_level, panel->backlight.min, panel->backlight.max,
		     0, user_max);
}

u32 intel_backlight_invert_pwm_level(struct intel_connector *connector, u32 val)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	drm_WARN_ON(&i915->drm, panel->backlight.pwm_level_max == 0);

	if (i915->display.params.invert_brightness < 0)
		return val;

	if (i915->display.params.invert_brightness > 0 ||
	    intel_has_quirk(i915, QUIRK_INVERT_BRIGHTNESS)) {
		return panel->backlight.pwm_level_max - val + panel->backlight.pwm_level_min;
	}

	return val;
}

void intel_backlight_set_pwm_level(const struct drm_connector_state *conn_state, u32 val)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s] set backlight PWM = %d\n",
		    connector->base.base.id, connector->base.name, val);
	panel->backlight.pwm_funcs->set(conn_state, val);
}

u32 intel_backlight_level_to_pwm(struct intel_connector *connector, u32 val)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	drm_WARN_ON_ONCE(&i915->drm,
			 panel->backlight.max == 0 || panel->backlight.pwm_level_max == 0);

	val = scale(val, panel->backlight.min, panel->backlight.max,
		    panel->backlight.pwm_level_min, panel->backlight.pwm_level_max);

	return intel_backlight_invert_pwm_level(connector, val);
}

u32 intel_backlight_level_from_pwm(struct intel_connector *connector, u32 val)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	drm_WARN_ON_ONCE(&i915->drm,
			 panel->backlight.max == 0 || panel->backlight.pwm_level_max == 0);

	if (i915->display.params.invert_brightness > 0 ||
	    (i915->display.params.invert_brightness == 0 &&
	     intel_has_quirk(i915, QUIRK_INVERT_BRIGHTNESS)))
		val = panel->backlight.pwm_level_max - (val - panel->backlight.pwm_level_min);

	return scale(val, panel->backlight.pwm_level_min, panel->backlight.pwm_level_max,
		     panel->backlight.min, panel->backlight.max);
}

static u32 bxt_get_backlight(struct intel_connector *connector, enum pipe unused)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	return intel_de_read(i915, BXT_BLC_PWM_DUTY(panel->backlight.controller));
}

static void bxt_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	intel_de_write(i915, BXT_BLC_PWM_DUTY(panel->backlight.controller), level);
}

static void cnp_disable_backlight(const struct drm_connector_state *old_conn_state, u32 val)
{
	struct intel_connector *connector = to_intel_connector(old_conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	intel_backlight_set_pwm_level(old_conn_state, val);

	intel_de_rmw(i915, BXT_BLC_PWM_CTL(panel->backlight.controller),
		     BXT_BLC_PWM_ENABLE, 0);
}

static void cnp_enable_backlight(const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	u32 pwm_ctl;

	pwm_ctl = intel_de_read(i915, BXT_BLC_PWM_CTL(panel->backlight.controller));
	if (pwm_ctl & BXT_BLC_PWM_ENABLE) {
		drm_dbg_kms(&i915->drm, "backlight already enabled\n");
		pwm_ctl &= ~BXT_BLC_PWM_ENABLE;
		intel_de_write(i915, BXT_BLC_PWM_CTL(panel->backlight.controller),
			       pwm_ctl);
	}

	intel_de_write(i915, BXT_BLC_PWM_FREQ(panel->backlight.controller),
		       panel->backlight.pwm_level_max);

	intel_backlight_set_pwm_level(conn_state, level);

	pwm_ctl = 0;
	if (panel->backlight.active_low_pwm)
		pwm_ctl |= BXT_BLC_PWM_POLARITY;

	intel_de_write(i915, BXT_BLC_PWM_CTL(panel->backlight.controller), pwm_ctl);
	intel_de_posting_read(i915, BXT_BLC_PWM_CTL(panel->backlight.controller));
	intel_de_write(i915, BXT_BLC_PWM_CTL(panel->backlight.controller),
		       pwm_ctl | BXT_BLC_PWM_ENABLE);
}

static int cnp_num_backlight_controllers(struct drm_i915_private *i915)
{
	if (INTEL_PCH_TYPE(i915) >= PCH_MTL)
		return 2;

	if (INTEL_PCH_TYPE(i915) >= PCH_DG1)
		return 1;

	if (INTEL_PCH_TYPE(i915) >= PCH_ICP)
		return 2;

	return 1;
}

static bool cnp_backlight_controller_is_valid(struct drm_i915_private *i915, int controller)
{
	if (controller < 0 || controller >= cnp_num_backlight_controllers(i915))
		return false;

	if (controller == 1 &&
	    INTEL_PCH_TYPE(i915) >= PCH_ICP &&
	    INTEL_PCH_TYPE(i915) <= PCH_ADP)
		return intel_de_read(i915, SOUTH_CHICKEN1) & ICP_SECOND_PPS_IO_SELECT;

	return true;
}

/*
 * CNP: PWM clock frequency is 19.2 MHz or 24 MHz.
 *      PWM increment = 1
 */
static u32 cnp_hz_to_pwm(struct intel_connector *connector, u32 pwm_freq_hz)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);

	return DIV_ROUND_CLOSEST(KHz(DISPLAY_RUNTIME_INFO(i915)->rawclk_freq),
				 pwm_freq_hz);
}

static u16 get_vbt_pwm_freq(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	u16 pwm_freq_hz = connector->panel.vbt.backlight.pwm_freq_hz;

	if (pwm_freq_hz) {
		drm_dbg_kms(&i915->drm,
			    "VBT defined backlight frequency %u Hz\n",
			    pwm_freq_hz);
	} else {
		pwm_freq_hz = 200;
		drm_dbg_kms(&i915->drm,
			    "default backlight frequency %u Hz\n",
			    pwm_freq_hz);
	}

	return pwm_freq_hz;
}

static u32 get_backlight_max_vbt(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	u16 pwm_freq_hz = get_vbt_pwm_freq(connector);
	u32 pwm;

	if (!panel->backlight.pwm_funcs->hz_to_pwm) {
		drm_dbg_kms(&i915->drm,
			    "backlight frequency conversion not supported\n");
		return 0;
	}

	pwm = panel->backlight.pwm_funcs->hz_to_pwm(connector, pwm_freq_hz);
	if (!pwm) {
		drm_dbg_kms(&i915->drm,
			    "backlight frequency conversion failed\n");
		return 0;
	}

	return pwm;
}

/*
 * Note: The setup hooks can't assume pipe is set!
 */
static u32 get_backlight_min_vbt(struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	int min;

	drm_WARN_ON(&i915->drm, panel->backlight.pwm_level_max == 0);

	/*
	 * XXX: If the vbt value is 255, it makes min equal to max, which leads
	 * to problems. There are such machines out there. Either our
	 * interpretation is wrong or the vbt has bogus data. Or both. Safeguard
	 * against this by letting the minimum be at most (arbitrarily chosen)
	 * 25% of the max.
	 */
	min = clamp_t(int, connector->panel.vbt.backlight.min_brightness, 0, 64);
	if (min != connector->panel.vbt.backlight.min_brightness) {
		drm_dbg_kms(&i915->drm,
			    "clamping VBT min backlight %d/255 to %d/255\n",
			    connector->panel.vbt.backlight.min_brightness, min);
	}

	/* vbt value is a coefficient in range [0..255] */
	return scale(min, 0, 255, 0, panel->backlight.pwm_level_max);
}

static int
cnp_setup_backlight(struct intel_connector *connector, enum pipe unused)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	u32 pwm_ctl;

	/*
	 * CNP has the BXT implementation of backlight, but with only one
	 * controller. ICP+ can have two controllers, depending on pin muxing.
	 */
	panel->backlight.controller = connector->panel.vbt.backlight.controller;
	if (!cnp_backlight_controller_is_valid(i915, panel->backlight.controller)) {
		drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s] Invalid backlight controller %d, assuming 0\n",
			    connector->base.base.id, connector->base.name,
			    panel->backlight.controller);
		panel->backlight.controller = 0;
	}

	pwm_ctl = intel_de_read(i915,
				BXT_BLC_PWM_CTL(panel->backlight.controller));

	panel->backlight.active_low_pwm = pwm_ctl & BXT_BLC_PWM_POLARITY;
	panel->backlight.pwm_level_max =
		intel_de_read(i915, BXT_BLC_PWM_FREQ(panel->backlight.controller));

	if (!panel->backlight.pwm_level_max)
		panel->backlight.pwm_level_max = get_backlight_max_vbt(connector);

	if (!panel->backlight.pwm_level_max)
		return -ENODEV;

	panel->backlight.pwm_level_min = get_backlight_min_vbt(connector);

	panel->backlight.pwm_enabled = pwm_ctl & BXT_BLC_PWM_ENABLE;

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] Using native PCH PWM for backlight control (controller=%d)\n",
		    connector->base.base.id, connector->base.name,
		    panel->backlight.controller);

	return 0;
}

static u32 intel_pwm_get_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct intel_panel *panel = &connector->panel;

	return intel_backlight_invert_pwm_level(connector,
					    panel->backlight.pwm_funcs->get(connector, pipe));
}

static void intel_pwm_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	panel->backlight.pwm_funcs->set(conn_state,
					intel_backlight_invert_pwm_level(connector, level));
}

static void intel_pwm_enable_backlight(const struct intel_crtc_state *crtc_state,
				       const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	panel->backlight.pwm_funcs->enable(crtc_state, conn_state,
					   intel_backlight_invert_pwm_level(connector, level));
}

static void intel_pwm_disable_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	panel->backlight.pwm_funcs->disable(conn_state,
					    intel_backlight_invert_pwm_level(connector, level));
}

static int intel_pwm_setup_backlight(struct intel_connector *connector, enum pipe pipe)
{
	struct intel_panel *panel = &connector->panel;
	int ret;

	ret = panel->backlight.pwm_funcs->setup(connector, pipe);
	if (ret < 0)
		return ret;

	panel->backlight.min = panel->backlight.pwm_level_min;
	panel->backlight.max = panel->backlight.pwm_level_max;
	panel->backlight.level = intel_pwm_get_backlight(connector, pipe);
	panel->backlight.enabled = panel->backlight.pwm_enabled;

	return 0;
}

static void __intel_backlight_enable(const struct intel_crtc_state *crtc_state,
				     const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct intel_panel *panel = &connector->panel;

	WARN_ON(panel->backlight.max == 0);

	if (panel->backlight.level <= panel->backlight.min) {
		panel->backlight.level = panel->backlight.max;
		if (panel->backlight.device)
			panel->backlight.device->props.brightness =
				scale_hw_to_user(connector,
						 panel->backlight.level,
						 panel->backlight.device->props.max_brightness);
	}

	panel->backlight.funcs->enable(crtc_state, conn_state, panel->backlight.level);
	panel->backlight.enabled = true;
	if (panel->backlight.device)
		panel->backlight.device->props.power = FB_BLANK_UNBLANK;
}

void intel_backlight_enable(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	enum pipe pipe = to_intel_crtc(crtc_state->uapi.crtc)->pipe;

	if (!panel->backlight.present)
		return;

	drm_dbg_kms(&i915->drm, "pipe %c\n", pipe_name(pipe));

	mutex_lock(&i915->display.backlight.lock);

	__intel_backlight_enable(crtc_state, conn_state);

	mutex_unlock(&i915->display.backlight.lock);
}

void intel_backlight_disable(const struct drm_connector_state *old_conn_state)
{
	struct intel_connector *connector = to_intel_connector(old_conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	if (!panel->backlight.present)
		return;

	/*
	 * Do not disable backlight on the vga_switcheroo path. When switching
	 * away from i915, the other client may depend on i915 to handle the
	 * backlight. This will leave the backlight on unnecessarily when
	 * another client is not activated.
	 */
	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_CHANGING) {
		drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s] Skipping backlight disable on vga switch\n",
			    connector->base.base.id, connector->base.name);
		return;
	}

	mutex_lock(&i915->display.backlight.lock);

	if (panel->backlight.device)
		panel->backlight.device->props.power = FB_BLANK_POWERDOWN;
	panel->backlight.enabled = false;
	panel->backlight.funcs->disable(old_conn_state, 0);

	mutex_unlock(&i915->display.backlight.lock);
}

static void
intel_panel_actually_set_backlight(const struct drm_connector_state *conn_state, u32 level)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;

	drm_dbg_kms(&i915->drm, "[CONNECTOR:%d:%s] set backlight level = %d\n",
		    connector->base.base.id, connector->base.name, level);

	panel->backlight.funcs->set(conn_state, level);
}

/* Scale user_level in range [0..user_max] to [hw_min..hw_max]. */
static u32 scale_user_to_hw(struct intel_connector *connector,
			    u32 user_level, u32 user_max)
{
	struct intel_panel *panel = &connector->panel;

	return scale(user_level, 0, user_max,
		     panel->backlight.min, panel->backlight.max);
}

/* set backlight brightness to level in range [0..max], scaling wrt hw min */
static void intel_panel_set_backlight(const struct drm_connector_state *conn_state,
				      u32 user_level, u32 user_max)
{
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct intel_panel *panel = &connector->panel;
	u32 hw_level;

	if (!panel->backlight.present)
		return;

	mutex_lock(&i915->display.backlight.lock);

	drm_WARN_ON(&i915->drm, panel->backlight.max == 0);

	hw_level = scale_user_to_hw(connector, user_level, user_max);
	panel->backlight.level = hw_level;

	if (panel->backlight.enabled)
		intel_panel_actually_set_backlight(conn_state, hw_level);

	mutex_unlock(&i915->display.backlight.lock);
}


#include "parity_backlight_glue.inc"
