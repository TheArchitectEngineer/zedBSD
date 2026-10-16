/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_backlight.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2021 Intel Corporation
 */

/*
 * The panel backlight of the one-screen path (see panel-backlight.h).
 *
 * The functions follow the Linux 6.8.12 intel_backlight.c text for a
 * native-PWM eDP panel on a CNP+ PCH: the enable, disable and brightness
 * paths with their scaling between the user's range, the backlight range
 * and the PWM range.  The PWM itself (its registers, the period of a
 * frequency, the lowest duty, the scaling) is the xd panel's
 * (xd-panel.c), driven through a register window whose accesses go to
 * the device's backend hooks, so every backend (the device, a model, the
 * recorder) sees the same register writes as before.  Around that text:
 * the backlight setup of the modeset object (what intel_backlight_setup()
 * and intel_backlight_init_funcs() do for this panel), the brightness,
 * ACPI and power entry points of the modeset object, and those of the
 * selected screen.
 *
 * The backlight lock is the device's (I915_LCD_LOCK_BACKLIGHT), taken
 * through the backend's lock hook.
 */

#include "modeset-internal.h"
#include "../intel/mreg.h"
#include "panel-backlight.h"
#include "dp.h"
#include "modeset.h"
#include "xd-io.h"
#include "xd-panel.h"

/*
 * The register window the backlight PWM is driven through.
 *
 * Its accesses go to the backend hooks of the device the call is about
 * (set again by every call), so the device, a model and the recorder see
 * the PWM writes as they always did.  It is used only under the
 * backlight lock, which also keeps its record of the writes; it lives as
 * long as the kernel and is prepared on first use.
 */
static struct i915_xd_io i915_backlight_io;

/* Nonzero once i915_backlight_io is prepared. */
static int i915_backlight_io_ready;

static struct i915_xd_io *i915_backlight_window(struct drm_i915_private *i915);
static uint32_t i915_backlight_window_read(void *context, uint32_t offset);
static void i915_backlight_window_write(void *context, uint32_t offset, uint32_t value);
static void i915_backlight_window_wait(void *context, unsigned microseconds);
static void i915_backlight_pwm_state(const struct intel_panel *panel, struct i915_xd_backlight *pwm);
static u32 i915_scale(u32 source_val, u32 source_min, u32 source_max, u32 target_min, u32 target_max);
static u32 i915_clamp_user_to_hw(struct intel_connector *connector, u32 user_level, u32 user_max);
static u32 i915_scale_hw_to_user(struct intel_connector *connector, u32 hw_level, u32 user_max);
static u32 i915_backlight_invert_pwm_level(struct intel_connector *connector, u32 val);
static void i915_backlight_set_pwm_level(const struct drm_connector_state *conn_state, u32 val);
static unsigned i915_cnp_num_backlight_controllers(struct drm_i915_private *i915);
static u16 i915_get_vbt_pwm_freq(struct intel_connector *connector);
static int i915_cnp_setup_backlight(struct intel_connector *connector);
static u32 i915_pwm_get_backlight(struct intel_connector *connector);
static void i915_pwm_set_backlight(const struct drm_connector_state *conn_state, u32 level);
static void i915_pwm_enable_backlight(const struct drm_connector_state *conn_state, u32 level);
static void i915_pwm_disable_backlight(const struct drm_connector_state *conn_state, u32 level);
static int i915_pwm_setup_backlight(struct intel_connector *connector);
static void i915_backlight_enable(const struct drm_connector_state *conn_state);
static void i915_panel_actually_set_backlight(const struct drm_connector_state *conn_state, u32 level);
static u32 i915_scale_user_to_hw(struct intel_connector *connector, u32 user_level, u32 user_max);
static void i915_panel_set_backlight(const struct drm_connector_state *conn_state, u32 user_level, u32 user_max);
static struct i915_lcd_modeset *i915_selected_screen(struct i915_display *display);

/*
 * Enables the backlight of a panel (the Linux intel_backlight_enable()).
 *
 * Nothing for a panel without a backlight.  A level at or below the
 * minimum comes back at the maximum.
 */
void
drv_i915_backlight_enable(
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	enum pipe pipe;

	/* Finds the connector, its device, its panel and the pipe. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;
	pipe = to_intel_crtc(crtc_state->uapi.crtc)->pipe;

	/* A panel without a backlight has nothing to enable. */
	if (!panel->backlight.present)
		return;

	/* Notes the enable. */
	I915_LCD_DRM_DBG_KMS(&i915->drm, "pipe %c\n", pipe_name(pipe));

	/* Enables the backlight under the backlight lock. */
	I915_LCD_MUTEX_LOCK(i915, &i915->display.backlight.lock);

	i915_backlight_enable(conn_state);

	I915_LCD_MUTEX_UNLOCK(i915, &i915->display.backlight.lock);
}

/*
 * Disables the backlight of a panel (the Linux intel_backlight_disable()).
 *
 * Nothing for a panel without a backlight.  On the vga_switcheroo path
 * the backlight stays on: the other client may depend on i915 to handle
 * it.
 */
void
drv_i915_backlight_disable(
	const struct drm_connector_state *old_conn_state)
{
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;

	/* Finds the connector, its device and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(old_conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* A panel without a backlight has nothing to disable. */
	if (!panel->backlight.present)
		return;

	/* A switch away from i915 leaves the backlight to the other client. */
	if (i915->drm.switch_power_state == DRM_SWITCH_POWER_CHANGING) {
		I915_LCD_DRM_DBG_KMS(&i915->drm,
				     "[CONNECTOR:%d:%s] Skipping backlight disable on vga switch\n",
				     connector->base.base.id,
				     connector->base.name);
		return;
	}

	/* Disables the backlight under the backlight lock. */
	I915_LCD_MUTEX_LOCK(i915, &i915->display.backlight.lock);

	if (panel->backlight.device)
		panel->backlight.device->props.power = FB_BLANK_POWERDOWN;
	panel->backlight.enabled = false;
	i915_pwm_disable_backlight(old_conn_state, 0);

	I915_LCD_MUTEX_UNLOCK(i915, &i915->display.backlight.lock);
}

/*
 * Sets the brightness an ACPI request asks for (the Linux
 * intel_backlight_set_acpi()).
 *
 * The level in [0..user_max] is scaled to the backlight range and clamped
 * to its minimum, which the request is assumed to respect.  Nothing for a
 * panel without a backlight, or while the connector drives no crtc (the
 * BIOS may ask at any time during driver init).
 */
void
drv_i915_backlight_set_acpi(
	const struct drm_connector_state *conn_state,
	u32 user_level,
	u32 user_max)
{
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	u32 hw_level;

	/* Finds the connector, its device and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* A panel without a backlight, or a connector without a crtc, is not changed. */
	if (!panel->backlight.present || !conn_state->crtc)
		return;

	/* Sets the level under the backlight lock. */
	I915_LCD_MUTEX_LOCK(i915, &i915->display.backlight.lock);

	/* The backlight range must be known. */
	I915_LCD_DRM_WARN_ON(&i915->drm, panel->backlight.max == 0);

	/* The new hardware level. */
	hw_level = i915_clamp_user_to_hw(connector, user_level, user_max);
	panel->backlight.level = hw_level;

	/* The backlight device's brightness follows. */
	if (panel->backlight.device) {
		panel->backlight.device->props.brightness = i915_scale_hw_to_user(connector,
										 panel->backlight.level,
										 panel->backlight.device->props.max_brightness);
	}

	/* An enabled backlight shows the level at once. */
	if (panel->backlight.enabled)
		i915_panel_actually_set_backlight(conn_state, hw_level);

	I915_LCD_MUTEX_UNLOCK(i915, &i915->display.backlight.lock);
}

/*
 * Sets up the backlight of a modeset object's panel.
 *
 * What the Linux intel_backlight_setup() does for a native-PWM eDP panel
 * on a CNP+ PCH: under the backlight lock the PWM registers are read and
 * the range and the level derived, and the backlight is present.  Without a VBT backlight block nothing is set
 * up.  0, or the setup hook's negative Linux errno (the status reports it
 * as intel_backlight_setup() returned it).
 */
int
drv_i915_lcd_ms_backlight_setup(
	struct i915_lcd_modeset *ms)
{
	struct intel_panel *panel;
	int ret;

	/* The panel of the object's connector. */
	panel = &ms->connector.panel;

	/* Without a VBT backlight block the panel has no backlight to set up. */
	if (!panel->vbt.backlight.present)
		return 0;

	/* Reads the PWM state under the backlight lock. */
	I915_LCD_MUTEX_LOCK(&ms->i915, &ms->i915.display.backlight.lock);

	ret = i915_pwm_setup_backlight(&ms->connector);

	I915_LCD_MUTEX_UNLOCK(&ms->i915, &ms->i915.display.backlight.lock);

	/* Reports a setup that found no PWM frequency. */
	if (ret != 0)
		return ret;

	/* The panel has a backlight from here. */
	panel->backlight.present = true;

	/* Succeeded: the backlight is set up. */
	return 0;
}

/*
 * Sets the user brightness of a modeset object's panel.
 *
 * The Linux intel_backlight_device_update_status() ->
 * intel_panel_set_backlight(conn_state, brightness, max_brightness): the
 * level is scaled to [backlight.min, backlight.max]; backlight.min comes
 * from the VBT's min_brightness, a coefficient in 0..255 of the PWM range.
 */
void
drv_i915_lcd_ms_set_brightness(
	struct i915_lcd_modeset *ms,
	u32 user_level,
	u32 user_max)
{
	/* The object's device is the one the Linux text works on from here. */
	ms->world->i915_lcd_cur_i915 = &ms->i915;

	/* Sets the level. */
	i915_panel_set_backlight(&ms->conn_state, user_level, user_max);
}

/*
 * Sets the brightness of an OpRegion (ASLE) request on a modeset object's
 * panel: the Linux asle_set_backlight() ->
 * intel_backlight_set_acpi(conn_state, bclp, 255).
 */
void
drv_i915_lcd_ms_set_acpi(
	struct i915_lcd_modeset *ms,
	u32 user_level,
	u32 user_max)
{
	/* The object's device is the one the Linux text works on from here. */
	ms->world->i915_lcd_cur_i915 = &ms->i915;

	/* Sets the level. */
	drv_i915_backlight_set_acpi(&ms->conn_state, user_level, user_max);
}

/*
 * Turns the backlight of a modeset object's panel on or off.
 *
 * The Linux intel_edp_backlight_on() / _off(): the PWM and the panel's
 * backlight-enable only; the pipe keeps scanning out.
 */
void
drv_i915_lcd_ms_backlight_power(
	struct i915_lcd_modeset *ms,
	int on)
{
	/* The object's device is the one the Linux text works on from here. */
	ms->world->i915_lcd_cur_i915 = &ms->i915;

	/* Turns the backlight on or off. */
	if (on) {
		drv_i915_edp_backlight_on(&ms->crtc_state, &ms->conn_state);
	} else {
		drv_i915_edp_backlight_off(&ms->conn_state);
	}
}

/*
 * Returns the user brightness of the current hardware level of a modeset
 * object's panel, in [0..user_max].
 *
 * What intel_backlight_device_register() and __intel_backlight_enable()
 * store as the backlight device's brightness.
 */
u32
drv_i915_lcd_ms_user_level(
	struct i915_lcd_modeset *ms,
	u32 user_max)
{
	u32 user_level;

	/* Scales the hardware level to the user's range. */
	user_level = i915_scale_hw_to_user(&ms->connector, ms->connector.panel.backlight.level, user_max);

	/* Succeeded: reports the level. */
	return user_level;
}

/*
 * Sets the brightness of the selected screen to user_level of user_max.
 *
 * The backlight device's brightness follows: the level itself when the
 * range is the device's, otherwise the hardware level scaled to the
 * device's range.  I915_LCD_MS_NOT_PREPARED when the screen is not
 * running, is retained, or the level is out of range;
 * I915_LCD_MS_ERRORS when the Linux text reported an error.
 */
int
drv_i915_lcd_modeset_brightness(
	struct i915_display *display,
	uint32_t user_level,
	uint32_t user_max)
{
	struct i915_lcd_world *world;
	struct i915_lcd_modeset *ms;
	unsigned before;
	int retained;

	/* Finds the selected screen and the errors it reported so far. */
	world = display->lcd_world;
	ms = i915_selected_screen(display);
	before = world->ms_errors_pool[world->ms_sel];

	/* Only a prepared, running screen that is not retained takes a brightness. */
	if (!ms->prepared)
		return I915_LCD_MS_NOT_PREPARED;
	if (!ms->crtc.active)
		return I915_LCD_MS_NOT_PREPARED;
	retained = drv_i915_lcd_modeset_retained(display);
	if (retained)
		return I915_LCD_MS_NOT_PREPARED;

	/* The level must lie in a non-empty range. */
	if (user_max == 0u)
		return I915_LCD_MS_NOT_PREPARED;
	if (user_level > user_max)
		return I915_LCD_MS_NOT_PREPARED;

	/* Sets the brightness. */
	drv_i915_lcd_ms_set_brightness(ms, user_level, user_max);

	/* The backlight device's brightness follows. */
	if (user_max == ms->bl_user_max) {
		ms->bl_user = user_level;
	} else {
		ms->bl_user = drv_i915_lcd_ms_user_level(ms, ms->bl_user_max);
	}

	/* Reports an error the Linux text reported on the way. */
	if (world->ms_errors_pool[world->ms_sel] != before)
		return I915_LCD_MS_ERRORS;

	/* Succeeded: the brightness is set. */
	return I915_LCD_MS_OK;
}

/*
 * Reports the brightness of the selected screen as a level of user_max.
 *
 * The hardware level scaled to the range, as the Linux backlight device
 * reports it (the backlight device's GETSTATUS).
 * I915_LCD_MS_NOT_PREPARED when the screen is not running, is retained,
 * or the range is empty.
 */
int
drv_i915_lcd_modeset_brightness_get(
	struct i915_display *display,
	uint32_t user_max,
	uint32_t *user_level)
{
	struct i915_lcd_modeset *ms;
	int retained;

	/* Finds the selected screen. */
	ms = i915_selected_screen(display);

	/* Only a prepared, running screen that is not retained has a brightness. */
	if (!ms->prepared)
		return I915_LCD_MS_NOT_PREPARED;
	if (!ms->crtc.active)
		return I915_LCD_MS_NOT_PREPARED;
	retained = drv_i915_lcd_modeset_retained(display);
	if (retained)
		return I915_LCD_MS_NOT_PREPARED;

	/* The range must not be empty. */
	if (user_max == 0u)
		return I915_LCD_MS_NOT_PREPARED;

	/* Succeeded: the hardware level in the caller's range. */
	*user_level = drv_i915_lcd_ms_user_level(ms, user_max);
	return I915_LCD_MS_OK;
}

/*
 * Serves an OpRegion (ASLE) brightness request on the selected screen.
 *
 * The Linux intel_backlight_set_acpi(): hw level = clamp_user_to_hw(level,
 * max), without a backlight device update; the device's brightness is
 * recomputed from the new hardware level.  Results as
 * drv_i915_lcd_modeset_brightness().
 */
int
drv_i915_lcd_modeset_backlight_acpi(
	struct i915_display *display,
	uint32_t level,
	uint32_t max)
{
	struct i915_lcd_world *world;
	struct i915_lcd_modeset *ms;
	unsigned before;
	int retained;

	/* Finds the selected screen and the errors it reported so far. */
	world = display->lcd_world;
	ms = i915_selected_screen(display);
	before = world->ms_errors_pool[world->ms_sel];

	/* Only a prepared, running screen that is not retained takes a request. */
	if (!ms->prepared)
		return I915_LCD_MS_NOT_PREPARED;
	if (!ms->crtc.active)
		return I915_LCD_MS_NOT_PREPARED;
	retained = drv_i915_lcd_modeset_retained(display);
	if (retained)
		return I915_LCD_MS_NOT_PREPARED;

	/* The level must lie in a non-empty range. */
	if (max == 0u)
		return I915_LCD_MS_NOT_PREPARED;
	if (level > max)
		return I915_LCD_MS_NOT_PREPARED;

	/* Serves the request, then recomputes the device's brightness. */
	drv_i915_lcd_ms_set_acpi(ms, level, max);
	ms->bl_user = drv_i915_lcd_ms_user_level(ms, ms->bl_user_max);

	/* Reports an error the Linux text reported on the way. */
	if (world->ms_errors_pool[world->ms_sel] != before)
		return I915_LCD_MS_ERRORS;

	/* Succeeded: the request is served. */
	return I915_LCD_MS_OK;
}

/*
 * Turns the backlight of the selected screen on or off.
 *
 * __intel_backlight_enable() brings a level at or below the minimum back
 * at the maximum; the backlight device's brightness follows such a
 * change.  Results as drv_i915_lcd_modeset_brightness().
 */
int
drv_i915_lcd_modeset_backlight(
	struct i915_display *display,
	int on)
{
	struct i915_lcd_world *world;
	struct i915_lcd_modeset *ms;
	unsigned before;
	int retained;
	u32 before_level;

	/* Finds the selected screen and the errors it reported so far. */
	world = display->lcd_world;
	ms = i915_selected_screen(display);
	before = world->ms_errors_pool[world->ms_sel];

	/* Only a prepared, running screen that is not retained is switched. */
	if (!ms->prepared)
		return I915_LCD_MS_NOT_PREPARED;
	if (!ms->crtc.active)
		return I915_LCD_MS_NOT_PREPARED;
	retained = drv_i915_lcd_modeset_retained(display);
	if (retained)
		return I915_LCD_MS_NOT_PREPARED;

	/* Switches the backlight, remembering the level it had. */
	before_level = ms->connector.panel.backlight.level;
	drv_i915_lcd_ms_backlight_power(ms, on);

	/* An enable that raised the level moves the device's brightness with it. */
	if (on && ms->connector.panel.backlight.level != before_level)
		ms->bl_user = drv_i915_lcd_ms_user_level(ms, ms->bl_user_max);

	/* Reports an error the Linux text reported on the way. */
	if (world->ms_errors_pool[world->ms_sel] != before)
		return I915_LCD_MS_ERRORS;

	/* Succeeded: the backlight is switched. */
	return I915_LCD_MS_OK;
}

/*
 * Returns the register window of the backlight PWM, reaching the backend
 * hooks of a device (prepared on first use).
 */
static struct i915_xd_io *
i915_backlight_window(
	struct drm_i915_private *i915)
{
	struct i915_xd_io_hooks hooks;

	/* Prepares the window once. */
	if (i915_backlight_io_ready == 0) {
		drv_i915_xd_io_init(&i915_backlight_io, NULL, NULL);
		i915_backlight_io_ready = 1;
	}

	/* Points the window at this device's backend. */
	hooks.read = i915_backlight_window_read;
	hooks.write = i915_backlight_window_write;
	hooks.wait = i915_backlight_window_wait;
	hooks.lock = NULL;
	hooks.context = i915;
	drv_i915_xd_io_set_hooks(&i915_backlight_io, &hooks);

	/* Succeeded: the window. */
	return &i915_backlight_io;
}

/* Reads a register through the device's backend; the recorder, which cannot read, answers 0. */
static uint32_t
i915_backlight_window_read(
	void *context,
	uint32_t offset)
{
	struct drm_i915_private *i915;
	uint32_t value;

	/* The pure word recorder has no read hook. */
	i915 = context;
	if (i915->emit->read32 == NULL)
		return 0u;

	/* Reads the register. */
	value = i915->emit->read32(i915->emit->ctx, offset);

	/* Succeeded: the register's value. */
	return value;
}

/* Writes a register through the device's backend. */
static void
i915_backlight_window_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	struct drm_i915_private *i915;

	/* Writes the register. */
	i915 = context;
	i915->emit->write32(i915->emit->ctx, offset, value);
}

/* Lets time pass through the device's backend, when it keeps time. */
static void
i915_backlight_window_wait(
	void *context,
	unsigned microseconds)
{
	struct drm_i915_private *i915;

	/* A backend without a clock lets no time pass. */
	i915 = context;
	if (i915->emit->usleep == NULL)
		return;

	/* Sleeps. */
	i915->emit->usleep(i915->emit->ctx, microseconds);
}

/* Describes the panel's PWM to the xd panel from the backlight state. */
static void
i915_backlight_pwm_state(
	const struct intel_panel *panel,
	struct i915_xd_backlight *pwm)
{
	/* The controller, the period, the polarity and the enable as last known. */
	pwm->controller = panel->backlight.controller;
	pwm->period = panel->backlight.pwm_level_max;
	pwm->active_low = 0;
	if (panel->backlight.active_low_pwm)
		pwm->active_low = 1;
	pwm->running = 0;
	if (panel->backlight.pwm_enabled)
		pwm->running = 1;
}

/*
 * Scales a value from one range to another (the Linux scale()).
 *
 * An empty source range or an inverted target range is reported and
 * answers the target minimum; the scaling itself is the xd panel's.
 */
static u32
i915_scale(
	u32 source_val,
	u32 source_min,
	u32 source_max,
	u32 target_min,
	u32 target_max)
{
	u32 target_val;
	int warned;

	/* Refuses an empty source range. */
	warned = I915_LCD_WARN_ON(source_min >= source_max);
	if (warned)
		return target_min;

	/* Refuses an inverted target range. */
	warned = I915_LCD_WARN_ON(target_min > target_max);
	if (warned)
		return target_min;

	/* Scales, rounded to the closest. */
	target_val = drv_i915_xd_backlight_scale(source_val, source_min, source_max, target_min, target_max);

	/* Succeeded: reports the scaled value. */
	return target_val;
}

/*
 * Scales a user level in [0..user_max] to [0..hw_max] and clamps it to
 * [hw_min..hw_max] (the Linux clamp_user_to_hw()).
 */
static u32
i915_clamp_user_to_hw(
	struct intel_connector *connector,
	u32 user_level,
	u32 user_max)
{
	struct intel_panel *panel;
	u32 hw_level;

	/* The connector's panel. */
	panel = &connector->panel;

	/* Scales, then clamps to the backlight range. */
	hw_level = i915_scale(user_level, 0, user_max, 0, panel->backlight.max);
	hw_level = I915_LCD_CLAMP(hw_level, panel->backlight.min, panel->backlight.max);

	/* Succeeded: reports the hardware level. */
	return hw_level;
}

/* Scales a hardware level in [hw_min..hw_max] to [0..user_max] (the Linux scale_hw_to_user()). */
static u32
i915_scale_hw_to_user(
	struct intel_connector *connector,
	u32 hw_level,
	u32 user_max)
{
	struct intel_panel *panel;
	u32 user_level;

	/* The connector's panel. */
	panel = &connector->panel;

	/* Scales from the backlight range. */
	user_level = i915_scale(hw_level, panel->backlight.min, panel->backlight.max, 0, user_max);

	/* Succeeded: reports the user level. */
	return user_level;
}

/*
 * Inverts a PWM duty where the panel needs it (the Linux
 * intel_backlight_invert_pwm_level()).
 *
 * A negative invert_brightness parameter never inverts; a positive one or
 * the inverted-brightness quirk always does.
 */
static u32
i915_backlight_invert_pwm_level(
	struct intel_connector *connector,
	u32 val)
{
	struct drm_i915_private *i915;
	struct intel_panel *panel;

	/* Finds the device and the panel. */
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* The PWM range must be known. */
	I915_LCD_DRM_WARN_ON(&i915->drm, panel->backlight.pwm_level_max == 0);

	/* The parameter forbids the inversion. */
	if (i915->display.params.invert_brightness < 0)
		return val;

	/* The parameter or the quirk asks for it. */
	if (i915->display.params.invert_brightness > 0 ||
	    I915_LCD_INTEL_HAS_QUIRK(i915, QUIRK_INVERT_BRIGHTNESS)) {
		return panel->backlight.pwm_level_max - val + panel->backlight.pwm_level_min;
	}

	/* Succeeded: the duty is not inverted. */
	return val;
}

/* Sets the PWM duty of the panel's controller (the Linux intel_backlight_set_pwm_level()). */
static void
i915_backlight_set_pwm_level(
	const struct drm_connector_state *conn_state,
	u32 val)
{
	struct i915_xd_backlight pwm;
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	struct i915_xd_io *io;

	/* Finds the connector, its device and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* Notes the duty. */
	I915_LCD_DRM_DBG_KMS(connector->base.dev,
			     "[CONNECTOR:%d:%s] set backlight PWM = %d\n",
			     connector->base.base.id,
			     connector->base.name,
			     val);

	/* Writes it to the controller. */
	i915_backlight_pwm_state(panel, &pwm);
	io = i915_backlight_window(i915);
	drv_i915_xd_backlight_set_duty(&pwm, io, val);
}

/*
 * Returns how many backlight controllers the PCH has (the Linux
 * cnp_num_backlight_controllers()).
 */
static unsigned
i915_cnp_num_backlight_controllers(
	struct drm_i915_private *i915)
{
	UNUSED_PARAMETER(i915);

	/* Meteor Lake PCH and later have two. */
	if (INTEL_PCH_TYPE(i915) >= PCH_MTL)
		return 2;

	/* DG1 has one. */
	if (INTEL_PCH_TYPE(i915) >= PCH_DG1)
		return 1;

	/* Ice Lake PCH and later have two. */
	if (INTEL_PCH_TYPE(i915) >= PCH_ICP)
		return 2;

	/* Succeeded: earlier PCHs have one. */
	return 1;
}

/* Returns the VBT's PWM frequency, or 200 Hz (the Linux get_vbt_pwm_freq()). */
static u16
i915_get_vbt_pwm_freq(
	struct intel_connector *connector)
{
	u16 pwm_freq_hz;

	/* The VBT's frequency. */
	pwm_freq_hz = connector->panel.vbt.backlight.pwm_freq_hz;

	/* The VBT's frequency, or the default. */
	if (pwm_freq_hz) {
		I915_LCD_DRM_DBG_KMS(connector->base.dev, "VBT defined backlight frequency %u Hz\n", pwm_freq_hz);
	} else {
		pwm_freq_hz = 200;
		I915_LCD_DRM_DBG_KMS(connector->base.dev, "default backlight frequency %u Hz\n", pwm_freq_hz);
	}

	/* Succeeded: reports the frequency. */
	return pwm_freq_hz;
}

/*
 * Reads the PWM state of the panel's controller (the Linux
 * cnp_setup_backlight()).
 *
 * The VBT names the controller; one the PCH cannot use falls back to 0.
 * The period is the one the firmware programmed, or the VBT's frequency
 * in raw clock ticks; the lowest duty is the VBT's minimum.  0, or the
 * Linux -ENODEV when neither gives a period.
 */
static int
i915_cnp_setup_backlight(
	struct intel_connector *connector)
{
	struct i915_xd_backlight pwm;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	struct i915_xd_io *io;
	unsigned controllers;
	int muxed;
	int usable;
	int min;
	u16 pwm_freq_hz;

	/* Finds the device, the panel and the PWM's window. */
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;
	io = i915_backlight_window(i915);

	/* The VBT's controller, or 0 when the PCH cannot use it (ICP..ADP mux the second one's pins). */
	panel->backlight.controller = connector->panel.vbt.backlight.controller;
	controllers = i915_cnp_num_backlight_controllers(i915);
	muxed = 0;
	if (INTEL_PCH_TYPE(i915) >= PCH_ICP && INTEL_PCH_TYPE(i915) <= PCH_ADP)
		muxed = 1;
	usable = drv_i915_xd_backlight_controller_usable(io, panel->backlight.controller, controllers, muxed);
	if (usable == 0) {
		I915_LCD_DRM_DBG_KMS(&i915->drm,
				     "[CONNECTOR:%d:%s] Invalid backlight controller %d, assuming 0\n",
				     connector->base.base.id,
				     connector->base.name,
				     panel->backlight.controller);
		panel->backlight.controller = 0;
	}

	/* The polarity and the period the firmware programmed. */
	drv_i915_xd_backlight_read(&pwm, io, panel->backlight.controller);
	panel->backlight.active_low_pwm = false;
	if (pwm.active_low != 0)
		panel->backlight.active_low_pwm = true;
	panel->backlight.pwm_level_max = pwm.period;

	/* Without a programmed period, the VBT's frequency gives it. */
	if (!panel->backlight.pwm_level_max) {
		pwm_freq_hz = i915_get_vbt_pwm_freq(connector);
		panel->backlight.pwm_level_max = drv_i915_xd_backlight_period(I915_LCD_DISPLAY_RUNTIME_INFO(i915)->rawclk_freq, pwm_freq_hz);
		if (!panel->backlight.pwm_level_max)
			I915_LCD_DRM_DBG_KMS(connector->base.dev, "backlight frequency conversion failed\n");
	}

	/* Neither gives one: there is no usable backlight. */
	if (!panel->backlight.pwm_level_max)
		return -I915_LCD_ENODEV;

	/* The lowest duty, from the VBT's minimum (at most 64 of 255). */
	min = connector->panel.vbt.backlight.min_brightness;
	if (min > (int)I915_XD_BACKLIGHT_MIN_LIMIT) {
		I915_LCD_DRM_DBG_KMS(connector->base.dev,
				     "clamping VBT min backlight %d/255 to %d/255\n",
				     min,
				     (int)I915_XD_BACKLIGHT_MIN_LIMIT);
	}
	panel->backlight.pwm_level_min = drv_i915_xd_backlight_min_duty(panel->backlight.pwm_level_max, (unsigned)min);
	panel->backlight.pwm_enabled = false;
	if (pwm.running != 0)
		panel->backlight.pwm_enabled = true;

	/* Notes the setup. */
	I915_LCD_DRM_DBG_KMS(&i915->drm,
			     "[CONNECTOR:%d:%s] Using native PCH PWM for backlight control (controller=%d)\n",
			     connector->base.base.id,
			     connector->base.name,
			     panel->backlight.controller);

	/* Succeeded: the PWM state is read. */
	return 0;
}

/* Reads the backlight level of the PWM duty (the Linux intel_pwm_get_backlight()). */
static u32
i915_pwm_get_backlight(
	struct intel_connector *connector)
{
	struct i915_xd_backlight pwm;
	struct drm_i915_private *i915;
	struct i915_xd_io *io;
	u32 duty;
	u32 level;

	/* Reads the duty of the panel's controller. */
	i915 = i915_lcd_to_i915(connector->base.dev);
	i915_backlight_pwm_state(&connector->panel, &pwm);
	io = i915_backlight_window(i915);
	duty = drv_i915_xd_backlight_duty(&pwm, io);

	/* Un-inverts it. */
	level = i915_backlight_invert_pwm_level(connector, duty);

	/* Succeeded: reports the level. */
	return level;
}

/* Writes a backlight level as the PWM duty (the Linux intel_pwm_set_backlight()). */
static void
i915_pwm_set_backlight(
	const struct drm_connector_state *conn_state,
	u32 level)
{
	struct intel_connector *connector;
	u32 duty;

	/* Inverts the level and writes it. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	duty = i915_backlight_invert_pwm_level(connector, level);
	i915_backlight_set_pwm_level(conn_state, duty);
}

/*
 * Starts the PWM at a backlight level (the Linux
 * intel_pwm_enable_backlight() and cnp_enable_backlight()).
 */
static void
i915_pwm_enable_backlight(
	const struct drm_connector_state *conn_state,
	u32 level)
{
	struct i915_xd_backlight pwm;
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	struct i915_xd_io *io;
	u32 duty;
	int was_running;

	/* Finds the connector, its device and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* Starts the PWM at the inverted level. */
	duty = i915_backlight_invert_pwm_level(connector, level);
	i915_backlight_pwm_state(panel, &pwm);
	io = i915_backlight_window(i915);
	was_running = drv_i915_xd_backlight_enable(&pwm, io, duty);

	/* Notes a PWM found running and the duty. */
	if (was_running)
		I915_LCD_DRM_DBG_KMS(&i915->drm, "backlight already enabled\n");
	I915_LCD_DRM_DBG_KMS(connector->base.dev,
			     "[CONNECTOR:%d:%s] set backlight PWM = %d\n",
			     connector->base.base.id,
			     connector->base.name,
			     duty);
}

/*
 * Stops the PWM after setting a backlight level (the Linux
 * intel_pwm_disable_backlight() and cnp_disable_backlight()).
 */
static void
i915_pwm_disable_backlight(
	const struct drm_connector_state *conn_state,
	u32 level)
{
	struct i915_xd_backlight pwm;
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	struct i915_xd_io *io;
	u32 duty;

	/* Finds the connector, its device and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* Notes the duty, then stops the PWM with it. */
	duty = i915_backlight_invert_pwm_level(connector, level);
	I915_LCD_DRM_DBG_KMS(connector->base.dev,
			     "[CONNECTOR:%d:%s] set backlight PWM = %d\n",
			     connector->base.base.id,
			     connector->base.name,
			     duty);
	i915_backlight_pwm_state(panel, &pwm);
	io = i915_backlight_window(i915);
	drv_i915_xd_backlight_disable(&pwm, io, duty);
}

/*
 * Sets up the backlight over the PWM (the Linux
 * intel_pwm_setup_backlight()): the backlight range is the PWM range, and
 * the level and the enable state are read back.  0, or the PWM setup's
 * negative Linux errno.
 */
static int
i915_pwm_setup_backlight(
	struct intel_connector *connector)
{
	struct intel_panel *panel;
	int ret;

	/* The connector's panel. */
	panel = &connector->panel;

	/* Reads the PWM state. */
	ret = i915_cnp_setup_backlight(connector);
	if (ret < 0)
		return ret;

	/* The backlight range is the PWM range; the level and the enable are read back. */
	panel->backlight.min = panel->backlight.pwm_level_min;
	panel->backlight.max = panel->backlight.pwm_level_max;
	panel->backlight.level = i915_pwm_get_backlight(connector);
	panel->backlight.enabled = panel->backlight.pwm_enabled;

	/* Succeeded: the backlight is set up. */
	return 0;
}

/*
 * Enables the backlight at its level (the Linux __intel_backlight_enable(),
 * under the backlight lock).
 *
 * A level at or below the minimum comes back at the maximum, and the
 * backlight device's brightness follows.
 */
static void
i915_backlight_enable(
	const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector;
	struct intel_panel *panel;

	/* Finds the connector and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	panel = &connector->panel;

	/* The backlight range must be known. */
	I915_LCD_WARN_ON(panel->backlight.max == 0);

	/* A level at or below the minimum would be dark: the maximum instead. */
	if (panel->backlight.level <= panel->backlight.min) {
		panel->backlight.level = panel->backlight.max;
		if (panel->backlight.device) {
			panel->backlight.device->props.brightness = i915_scale_hw_to_user(connector,
											 panel->backlight.level,
											 panel->backlight.device->props.max_brightness);
		}
	}

	/* Enables the backlight at the level. */
	i915_pwm_enable_backlight(conn_state, panel->backlight.level);
	panel->backlight.enabled = true;
	if (panel->backlight.device)
		panel->backlight.device->props.power = FB_BLANK_UNBLANK;
}

/* Writes a backlight level (the Linux intel_panel_actually_set_backlight()). */
static void
i915_panel_actually_set_backlight(
	const struct drm_connector_state *conn_state,
	u32 level)
{
	struct intel_connector *connector;

	/* Finds the connector. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);

	/* Notes the level. */
	I915_LCD_DRM_DBG_KMS(connector->base.dev,
			     "[CONNECTOR:%d:%s] set backlight level = %d\n",
			     connector->base.base.id,
			     connector->base.name,
			     level);

	/* Writes it. */
	i915_pwm_set_backlight(conn_state, level);
}

/* Scales a user level in [0..user_max] to [hw_min..hw_max] (the Linux scale_user_to_hw()). */
static u32
i915_scale_user_to_hw(
	struct intel_connector *connector,
	u32 user_level,
	u32 user_max)
{
	struct intel_panel *panel;
	u32 hw_level;

	/* The connector's panel. */
	panel = &connector->panel;

	/* Scales into the backlight range. */
	hw_level = i915_scale(user_level, 0, user_max, panel->backlight.min, panel->backlight.max);

	/* Succeeded: reports the hardware level. */
	return hw_level;
}

/*
 * Sets the backlight to a level in [0..user_max], scaled with respect to
 * the hardware minimum (the Linux intel_panel_set_backlight()).
 *
 * The level is written only while the backlight is enabled; otherwise it
 * is the level the next enable uses.
 */
static void
i915_panel_set_backlight(
	const struct drm_connector_state *conn_state,
	u32 user_level,
	u32 user_max)
{
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct intel_panel *panel;
	u32 hw_level;

	/* Finds the connector, its device and its panel. */
	connector = I915_LCD_TO_INTEL_CONNECTOR(conn_state->connector);
	i915 = i915_lcd_to_i915(connector->base.dev);
	panel = &connector->panel;

	/* A panel without a backlight is not changed. */
	if (!panel->backlight.present)
		return;

	/* Sets the level under the backlight lock. */
	I915_LCD_MUTEX_LOCK(i915, &i915->display.backlight.lock);

	/* The backlight range must be known. */
	I915_LCD_DRM_WARN_ON(&i915->drm, panel->backlight.max == 0);

	/* The new hardware level. */
	hw_level = i915_scale_user_to_hw(connector, user_level, user_max);
	panel->backlight.level = hw_level;

	/* An enabled backlight shows the level at once. */
	if (panel->backlight.enabled)
		i915_panel_actually_set_backlight(conn_state, hw_level);

	I915_LCD_MUTEX_UNLOCK(i915, &i915->display.backlight.lock);
}

/* Returns the selected screen's modeset object. */
static struct i915_lcd_modeset *
i915_selected_screen(
	struct i915_display *display)
{
	struct i915_lcd_world *world;

	/* Finds the world. */
	world = display->lcd_world;

	/* Succeeded: the object of the selected pool slot. */
	return &world->ms_pool[world->ms_sel];
}
