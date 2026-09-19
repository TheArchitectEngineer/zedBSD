/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_backlight_port.c.
 *
 * What intel_backlight_setup() / intel_backlight_init_funcs() do for a native-PWM eDP panel on a CNP+ PCH:
 * pwm_funcs = cnp_pwm_funcs, funcs = pwm_bl_funcs (the two tables below repeat the reference's member
 * assignments for the members this path uses), then funcs->setup() -- which READS the PWM registers and
 * derives max / min / level -- and backlight.present = true.  Without a VBT backlight block
 * (vbt.backlight.present == false) the reference sets nothing up and enable / disable return at once.
 */
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

static const struct intel_panel_bl_funcs parity_cnp_pwm_funcs = {
	.setup = cnp_setup_backlight,
	.enable = cnp_enable_backlight,
	.disable = cnp_disable_backlight,
	.set = bxt_set_backlight,
	.get = bxt_get_backlight,
	.hz_to_pwm = cnp_hz_to_pwm,
};

static const struct intel_panel_bl_funcs parity_pwm_bl_funcs = {
	.setup = intel_pwm_setup_backlight,
	.enable = intel_pwm_enable_backlight,
	.disable = intel_pwm_disable_backlight,
	.set = intel_pwm_set_backlight,
	.get = intel_pwm_get_backlight,
};

int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms)
{
	struct intel_panel *panel = &ms->connector.panel;
	int ret;

	if (!panel->vbt.backlight.present)
		return 0;
	panel->backlight.pwm_funcs = &parity_cnp_pwm_funcs;
	panel->backlight.funcs = &parity_pwm_bl_funcs;
	mutex_lock(&ms->i915.display.backlight.lock);
	ret = panel->backlight.funcs->setup(&ms->connector, ms->crtc.pipe);
	mutex_unlock(&ms->i915.display.backlight.lock);
	if (ret != 0)
		return ret;
	panel->backlight.present = true;
	return 0;
}

/*
 * The user-visible brightness: intel_backlight_device_update_status() -> intel_panel_set_backlight(conn_state,
 * brightness, max_brightness), with the backlight device's range [0, backlight.max] (intel_backlight_device_register).
 * The level is scaled to [backlight.min, backlight.max] -- backlight.min comes from the VBT's min_brightness, a
 * coefficient in 0..255 of the PWM range (get_backlight_min_vbt), not a count and not a percentage.
 */
void parity_lcd_ms_set_brightness(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max)
{
	parity_lcd_cur_i915 = &ms->i915;
	intel_panel_set_backlight(&ms->conn_state, user_level, user_max);
}

/* intel_edp_backlight_on() / _off(): the PWM and the panel's backlight-enable only -- the pipe keeps scanning out */
void parity_lcd_ms_backlight_power(struct parity_lcd_modeset *ms, int on)
{
	parity_lcd_cur_i915 = &ms->i915;
	if (on)
		intel_edp_backlight_on(&ms->crtc_state, &ms->conn_state);
	else
		intel_edp_backlight_off(&ms->conn_state);
}

/* the backlight device's brightness for the current hardware level (intel_backlight_device_register / __intel_backlight_enable) */
u32 parity_lcd_ms_user_level(struct parity_lcd_modeset *ms, u32 user_max)
{
	return scale_hw_to_user(&ms->connector, ms->connector.panel.backlight.level, user_max);
}
