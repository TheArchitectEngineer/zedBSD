/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The modeset world of a display: the display version and the xd platform
 * description the older display code asks for, the register window its
 * xd calls write through, and the reports of that code that take no world.
 *
 * XXX: drv_i915_lcd_display_ver(), drv_i915_lcd_backend_fault() and
 * drv_i915_lcd_debug() take no world, as their callers in the older code
 * have none; they reach the one bound world through a file-scope pointer.
 */

#include "modeset-internal.h"
#include "modeset.h"
#include "state.h"
#include <kern/kcrt.h>

#include "../i915.h"

#include <kern/kmem.h>

#include <uapi/errno.h>
#include <stddef.h>

/* The display version the platform predicates answer before the probe sets one. */
#define I915_LCD_DEFAULT_DISPLAY_VER	13

/*
 * The modeset world the older code's world-less hooks report to.
 *
 * Set by the world's creation and cleared by its destruction; one display
 * binds it at a time.  NULL means no display has a modeset world.
 */
static struct i915_lcd_world *i915_modeset_bound_world;

/*
 * The display of the bound modeset world: its pictures hear the errors
 * the older code reports outside a screen's hooks.  Set and cleared with
 * the bound world.
 */
static struct i915_display *i915_modeset_bound_display;

/*
 * The register window the xd parts the older code calls are reached
 * through: the backend hooks of the device, so their writes go where the
 * rest of the older code's go (the hardware, or a host test's model).
 * Every user holds the runs' locks or runs in the display's start, before
 * a run can.  ready is nonzero once the window was prepared; it is never
 * cleared.
 */
static struct i915_xd_io i915_lcd_xd_window;
static int i915_lcd_xd_window_ready;

static uint32_t i915_lcd_xd_window_read(void *context, uint32_t offset);
static void i915_lcd_xd_window_write(void *context, uint32_t offset, uint32_t value);
static void i915_lcd_xd_window_wait(void *context, unsigned microseconds);
static void i915_modeset_on_error(void *ctx, const char *what);

/*
 * Allocates the modeset world of a display.
 *
 * The world starts zeroed except for the display version, which starts at
 * 13 until the probe sets the device's.  Returns 0, ENOMEM, or EBUSY when
 * another display already has a modeset world.
 */
int
drv_i915_lcd_world_create(
	struct i915_display *display)
{
	struct i915_lcd_world *world;

	/* One display binds the world-less hooks at a time. */
	if (i915_modeset_bound_world != NULL)
		return EBUSY;

	/* Allocates the zeroed world. */
	world = kern_calloc(1U, sizeof(*world));
	if (world == NULL)
		return ENOMEM;

	/* The display version the platform predicates answer until the probe sets one. */
	world->i915_lcd_ver = I915_LCD_DEFAULT_DISPLAY_VER;

	/* The display owns the world, and the world-less hooks report to it and to its pictures. */
	display->lcd_world = world;
	i915_modeset_bound_world = world;
	i915_modeset_bound_display = display;
	drv_i915_lcd_error_bind(world, i915_modeset_on_error, display);

	/* Succeeded: the modeset path has its state. */
	return 0;
}

/*
 * Frees the modeset world of a display.
 */
void
drv_i915_lcd_world_destroy(
	struct i915_display *display)
{
	/* A display without a world has nothing to free. */
	if (display->lcd_world == NULL)
		return;

	/* The world-less hooks no longer report to this world. */
	if (i915_modeset_bound_world == display->lcd_world) {
		drv_i915_lcd_error_bind(NULL, NULL, NULL);
		i915_modeset_bound_world = NULL;
		i915_modeset_bound_display = NULL;
	}

	kern_free(display->lcd_world);
	display->lcd_world = NULL;
}

/*
 * Sets the display version of the device.
 *
 * The tables that differ between the Gen12 platforms (buffer
 * translations, DBUF geometry, CDCLK) are chosen from it; the sequences are
 * the same text.
 */
void
drv_i915_lcd_set_display_ver(
	struct i915_display *display,
	int ver)
{
	/* The world keeps the version the platform predicates answer from. */
	display->lcd_world->i915_lcd_ver = ver;
}

/*
 * Sets the xd platform description of the device, which the modeset's
 * port PLLs and DDI clock map read.
 */
void
drv_i915_lcd_set_xd_platform(
	struct i915_display *display,
	const struct i915_xd_platform *platform)
{
	/* The world keeps the description. */
	display->lcd_world->xd_platform = platform;
}

/*
 * Returns the xd platform description of the device: the one the display
 * set, or else the first description of the display version (a host
 * test's world, which names only the version).
 */
const struct i915_xd_platform *
drv_i915_lcd_xd_platform(void)
{
	const struct i915_xd_platform *platform;
	unsigned count;
	unsigned index;
	int version;

	/* The display's own. */
	if (i915_modeset_bound_world != NULL && i915_modeset_bound_world->xd_platform != NULL)
		return i915_modeset_bound_world->xd_platform;

	/* The first description of the version. */
	version = drv_i915_lcd_display_ver();
	count = drv_i915_xd_platform_count();
	for (index = 0U; index < count; index++) {
		platform = drv_i915_xd_platform_row(index);
		if ((int)platform->display_ver == version)
			return platform;
	}

	/* Succeeded: no description of the version; the first one serves. */
	return drv_i915_xd_platform_row(0U);
}


/* Points the window at a device's backend (prepared on first use) and returns it. */
struct i915_xd_io *
drv_i915_lcd_xd_io(
	struct drm_i915_private *i915)
{
	struct i915_xd_io_hooks hooks;

	/* Prepares the window once. */
	if (i915_lcd_xd_window_ready == 0) {
		drv_i915_xd_io_init(&i915_lcd_xd_window, NULL, NULL);
		i915_lcd_xd_window_ready = 1;
	}

	/* Points it at this device's backend. */
	hooks.read = i915_lcd_xd_window_read;
	hooks.write = i915_lcd_xd_window_write;
	hooks.wait = i915_lcd_xd_window_wait;
	hooks.lock = NULL;
	hooks.context = i915;
	drv_i915_xd_io_set_hooks(&i915_lcd_xd_window, &hooks);

	/* Succeeded: the window. */
	return &i915_lcd_xd_window;
}

/* Reads a register through the device's backend; the recorder, which cannot read, answers 0. */
static uint32_t
i915_lcd_xd_window_read(
	void *context,
	uint32_t offset)
{
	struct drm_i915_private *i915;

	/* The pure word recorder has no read hook. */
	i915 = context;
	if (i915->emit->read32 == NULL)
		return 0U;

	/* Succeeded: the register's value. */
	return i915->emit->read32(i915->emit->ctx, offset);
}

/* Writes a register through the device's backend. */
static void
i915_lcd_xd_window_write(
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
i915_lcd_xd_window_wait(
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

/*
 * Returns the display version of the device (12 = Tiger Lake, 13 = ADL-P class).
 */
int
drv_i915_lcd_display_ver(void)
{
	/* Before a world exists the predicates answer the default version. */
	if (i915_modeset_bound_world == NULL)
		return I915_LCD_DEFAULT_DISPLAY_VER;

	/* Reports the version the probe set. */
	return i915_modeset_bound_world->i915_lcd_ver;
}

/*
 * Records an anomaly the backend found as an error of the
 * selected screen.
 *
 * A power well kept on or a broken time base is the run's first anomaly,
 * not a silent condition.
 */
void
drv_i915_lcd_backend_fault(
	const char *what)
{
	/* Without a display there is no run to report to. */
	if (i915_modeset_bound_display == NULL)
		return;

	/* Counts the fault as an error of the screen whose operation runs. */
	i915_modeset_on_error(i915_modeset_bound_display, what);
}

/*
 * Hands a debug message of the older code to the backend of the screen whose
 * operation runs.
 */
void
drv_i915_lcd_debug(
	const char *what)
{
	const struct i915_xd_backend *backend;
	struct i915_xd_pictures *pictures;

	/* Without a display there is no backend to hand it to. */
	if (i915_modeset_bound_display == NULL)
		return;

	/* The backend of the screen whose operation runs; one without a debug hook ignores the message. */
	pictures = &i915_modeset_bound_display->xd.pictures;
	backend = pictures->screen[pictures->current].backend;
	if (backend == NULL || backend->debug == NULL)
		return;

	backend->debug(backend->ctx, what);
}

/* Counts an error of the older code against the screen whose operation runs (ctx: the display). */
static void
i915_modeset_on_error(
	void *ctx,
	const char *what)
{
	struct i915_display *display;

	/* The display's pictures count it. */
	display = ctx;
	drv_i915_xd_pictures_fault(&display->xd.pictures, what);
}
