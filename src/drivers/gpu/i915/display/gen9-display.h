/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * How far the i915 drives the display of a Gen9 node (gen9-display.c,
 * WS237).
 *
 * A Gen9 or Gen9.5 node keeps the firmware's display unless the boot line
 * asks for more with i915.gen9.display=: each word takes every step of the
 * words before it, so a failure can be narrowed down by stepping back one
 * word at a time.
 *
 *   copy       every frame is copied into the firmware's framebuffer (bootfb.c)
 *   flip       the firmware's plane is flipped between the driver's buffers (gen9-flip.c)
 *   backlight  and the panel's brightness is offered as a backlight device (gen9-backlight.c)
 *   modeset    and the driver sets modes itself: the panel, external DP and HDMI, hot-plug (gen9-modeset.c)
 *   full       and the display is saved and brought back across suspend (gen9-modeset.c)
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
 * the one place the default is chosen.
 */
#define I915_GEN9_DISPLAY_DEFAULT	I915_GEN9_DISPLAY_COPY

/* Reports the step a word of i915.gen9.display= names, or -1 for another word. */
int drv_i915_gen9_display_word(const char *word);

/* Reports the step the boot line, or the default, asks for. */
int drv_i915_gen9_display_level(void);

/* Reports the name of a step, for the log. */
const char *drv_i915_gen9_display_name(int level);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_DISPLAY_H */
