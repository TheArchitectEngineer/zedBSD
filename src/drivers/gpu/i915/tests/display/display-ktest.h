/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the display ktest scenario.
 *
 * Each part lives in its own file under tests/display/, drives one layer
 * of the display code (mostly against a register or sink model, a few on
 * the started device's GT memory) and adds its checks to the tally the
 * display ktest scenario (ktest.c) owns.  The tally's device is the
 * started device.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_DISPLAY_KTEST_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_DISPLAY_KTEST_H

struct i915_ktest;

/* The scanout buffer: display window, aligned and guarded pin, coexistence, lifetime. */
void drv_i915_display_ktest_scanout(struct i915_ktest *ktest);

/* The body of one picture on the panel: the modeset commits and a real scanout object on the models. */
void drv_i915_display_ktest_lcd_show(struct i915_ktest *ktest);

/* The release contract of a GPU-drawn picture: TLB, mappings, retained GPU state, reclaim. */
void drv_i915_display_ktest_lcdg(struct i915_ktest *ktest);

/* The one-screen modeset on the register and sink models. */
void drv_i915_display_ktest_lcd_modeset(struct i915_ktest *ktest);

/* The eDP first stage on the register model: PPS and VDD ownership, AUX, DPCD, EDID. */
void drv_i915_display_ktest_edp(struct i915_ktest *ktest);

/* The eDP stage on real threads, locks and ticks: delayed work and the stage running on it. */
void drv_i915_display_ktest_edp_sync(struct i915_ktest *ktest);

/* The OpRegion receive side on a shadow mailbox with synthetic ACPI video events. */
void drv_i915_display_ktest_opregion(struct i915_ktest *ktest);

/* The HDMI hotplug receive path on fake hotplug and interrupt status registers. */
void drv_i915_display_ktest_hpd(struct i915_ktest *ktest);

#endif
