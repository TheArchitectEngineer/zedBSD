/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel backlight of the one-screen path (see panel-backlight.h).
 *
 * The backlight of the built-in panel is its xd output's (xd-output.c):
 * the PWM's range and level, its on and off, and the scaling between the
 * user's range and the backlight's.  The modeset object's hooks run them
 * on the object's output with the object's backend and bring the
 * object's state in line afterwards; the selected screen's entry points
 * check that the screen runs and is not retained, and keep the backlight
 * device's brightness.
 */

#include "modeset-internal.h"
#include "panel-backlight.h"
#include "modeset.h"
#include "xd-output.h"

static struct i915_lcd_modeset *i915_selected_screen(struct i915_display *display);

/*
 * Sets the user brightness of a modeset object's panel: the level scaled
 * to the backlight's range from its lowest level, shown at once when the
 * backlight is on.
 */
void
drv_i915_lcd_ms_set_brightness(
	struct i915_lcd_modeset *ms,
	u32 user_level,
	u32 user_max)
{
	struct i915_xd_output_env env;

	/* The output's brightness, then the object's copy of it. */
	drv_i915_lcd_ms_xo_env(ms, &env);
	drv_i915_xd_output_brightness(&ms->xo, &env, user_level, user_max);
	drv_i915_lcd_ms_xo_sync(ms);
}

/*
 * Sets the brightness of an OpRegion (ASLE) request on a modeset object's
 * panel: the level scaled to the whole range and raised to the lowest
 * level.  A panel without a crtc is not changed (the firmware may ask at
 * any time during the driver's start).
 */
void
drv_i915_lcd_ms_set_acpi(
	struct i915_lcd_modeset *ms,
	u32 user_level,
	u32 user_max)
{
	struct i915_xd_output_env env;

	/* A connector that drives no crtc. */
	if (!ms->conn_state.crtc)
		return;

	/* The output's brightness, then the object's copy of it. */
	drv_i915_lcd_ms_xo_env(ms, &env);
	drv_i915_xd_output_brightness_acpi(&ms->xo, &env, user_level, user_max);
	drv_i915_lcd_ms_xo_sync(ms);
}

/*
 * Turns the backlight of a modeset object's panel on or off: the PWM and
 * the sequencer's backlight enable only; the pipe keeps scanning out.
 */
void
drv_i915_lcd_ms_backlight_power(
	struct i915_lcd_modeset *ms,
	int on)
{
	struct i915_xd_output_env env;

	/* The output's backlight. */
	drv_i915_lcd_ms_xo_env(ms, &env);
	if (on) {
		drv_i915_xd_output_backlight_on(&ms->xo, &env);
	} else {
		drv_i915_xd_output_backlight_off(&ms->xo, &env);
	}

	/* The object's copy of it. */
	drv_i915_lcd_ms_xo_sync(ms);
}

/*
 * Returns the user brightness of the level of a modeset object's panel,
 * in [0..user_max]: what the backlight device shows as its brightness.
 */
u32
drv_i915_lcd_ms_user_level(
	struct i915_lcd_modeset *ms,
	u32 user_max)
{
	u32 user_level;

	/* The output's level in the user's range. */
	user_level = drv_i915_xd_output_user_level(&ms->xo, user_max);

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
