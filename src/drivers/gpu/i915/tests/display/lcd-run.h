/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel runs of the display scenarios (lcd-run.c).
 *
 * A scenario runs on the started device, before its node is served, and
 * drives the panel through the same steps the resident run is made of:
 * the preflight, the configuration from the initialisation's objects, the
 * show body with its observation window, the reference's stop path and
 * the release of everything the run took.  The run is always the
 * display's lk member, because the hooks find their display from it.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_LCD_RUN_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_LCD_RUN_H

#include "../../display/internal.h"
#include "../../display/modeset.h"

#include <stdint.h>

/* The asymmetric test picture of LCD-B. */
#define I915_TEST_LCDB_PATTERN_ID	110U

/* Its hash, pinned at 1920x1080 by the host picture test. */
#define I915_TEST_LCDB_PATTERN_FNV	0xce63f20b23f91f85ULL

/* How long the LCD-B picture stays up: long enough for the camera, then the stop path runs. */
#ifndef I915_TEST_LCDB_WINDOW_MS
#define I915_TEST_LCDB_WINDOW_MS	20000U
#endif

/* DDI_BUF_CTL of port B, whose port bits an HDMI output on it keeps. */
#define I915_TEST_HDMI_DDI_BUF_CTL_B	0x64100U

/* DDI_BUF_PORT_REVERSAL and DDI_A_4_LANES: the bits intel_ddi_init() keeps from the readout. */
#define I915_TEST_SAVED_PORT_BITS	((1U << 16) | (1U << 4))

/* How long a scenario waits for the first frames of its picture. */
#define I915_TEST_LCD_FIRST_FRAMES_MS	1000U

struct i915_device;

/* Returns the display a scenario drives, or NULL (logged as the scenario's failure) when the device has none. */
struct i915_display *drv_i915_test_lcd_display(struct i915_device *device, const char *tag);

/* Starts a panel run: the locks, the hooks and the parameters (params may be NULL); returns display->lk. */
struct i915_lcd_kernel *drv_i915_test_lcd_start(struct i915_display *display, const struct i915_lcd_run_params *params);

/* Reports whether an earlier run kept resources the display may still read. */
int drv_i915_test_lcd_retained(struct i915_display *display);

/* Shows one picture through the show body and stops it: 0 when the run passed, EINVAL, EBUSY or EIO otherwise. */
int drv_i915_test_lcd_run_one(struct i915_display *display, const struct i915_lcd_run_params *params);

/* Sleeps in steps of 100 ms on the run's sleep hook. */
void drv_i915_test_lcd_sleep_ms(struct i915_lcd_kernel *k, unsigned ms);

/* Reads the frame counter of pipe A (ctx is the run). */
uint32_t drv_i915_test_lcd_frame(void *ctx);

/* Counts the power references the run still holds. */
int drv_i915_test_lcd_power_held(const struct i915_lcd_kernel *k);

/* Names a stage of the show body. */
const char *drv_i915_test_lcd_stage_name(int stage);

/* Names the result of a flip. */
const char *drv_i915_test_lcd_flip_name(int result);

#endif
