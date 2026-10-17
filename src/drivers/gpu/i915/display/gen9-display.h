/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * How far the i915 drives the display of a Gen9 node (gen9-display.c).
 *
 * A Gen9 or Gen9.5 node keeps the firmware's display unless the boot line
 * asks for more with i915.gen9.display=: each word takes every step of the
 * words before it, so a failure can be narrowed down by stepping back one
 * word at a time.
 *
 *   copy       every frame is copied into the firmware's framebuffer (bootfb.c)
 *   flip       the firmware's plane is flipped between the driver's buffers (gen9-flip.c)
 *   backlight  and the panel's brightness is offered as a backlight device (gen9-backlight.c)
 *   modeset    and external DP and HDMI displays are lit beside the panel, with hot-plug (gen9-node.c)
 *   full       the same as modeset: the sleep keeps the display at every step (the device sleeps in D0)
 *
 * The word asks for a step; the node takes the highest step up to it that
 * works.  A step that fails lowers the node's step below it for the rest
 * of the boot, with one line in the log
 * ("i915: gen9 display: <step> failed (<reason>); using <step>"):
 *
 *   flip     the readout refuses the firmware's display, the window or the
 *            buffers cannot be had, or later a flip does not latch, the
 *            plane faults or the buffers cannot be mapped: the copies come
 *            back (bootfb.c), and the external displays are withdrawn
 *   modeset  the panel's pixel clock or the CD clock cannot be read, the
 *            window has no room for the outputs' buffers, or later an
 *            output's mode set fails or its flip does not latch: the
 *            external displays are withdrawn (gen9-node.c), the panel goes
 *            on flipped
 *
 * so a higher step that fails leaves the display of a lower one, at worst
 * the copies into the firmware's framebuffer.  A backlight the panel does
 * not have is not a failure (the node goes on without the device), and a
 * backlight device once offered stays.  The panel's mode, the clocks and
 * the power wells stay the firmware's at every step;
 * i915.gen9.display=copy keeps the firmware's display and only copies into it.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_DISPLAY_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_DISPLAY_H

/* The steps of i915.gen9.display=, each taking the ones before it. */
#define I915_GEN9_DISPLAY_COPY		0
#define I915_GEN9_DISPLAY_FLIP		1
#define I915_GEN9_DISPLAY_BACKLIGHT	2
#define I915_GEN9_DISPLAY_MODESET	3
#define I915_GEN9_DISPLAY_FULL		4

/*
 * The step a Gen9 node takes when the boot line has no i915.gen9.display=:
 * the one place the default is chosen (full: the display control is on by
 * default).
 */
#define I915_GEN9_DISPLAY_DEFAULT	I915_GEN9_DISPLAY_FULL

/* Reports the step a word of i915.gen9.display= names, or -1 for another word. */
int drv_i915_gen9_display_word(const char *word);

/* Reports the step the boot line, or the default, asks for. */
int drv_i915_gen9_display_level(void);

/* Reports the name of a step, for the log. */
const char *drv_i915_gen9_display_name(int level);

/* Reports the step the node takes now: the one asked for, lowered by a step that failed. */
int drv_i915_gen9_display_step(void);

/* Logs that a step failed and lowers the node's step below it. */
void drv_i915_gen9_display_fail(int level, const char *reason, int error);

/* Logs the step the node takes and the one asked for, once its display is bound. */
void drv_i915_gen9_display_report(void);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_DISPLAY_H */
