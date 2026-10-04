/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel's backlight as a backlight device (backlight.c, ws113-p013).
 *
 * A node whose output is the eDP panel registers it with the kernel's
 * backlight class (/dev/backlight/backlight0) when the request worker
 * starts serving the published node, and withdraws it when the worker is
 * asked to stop, while the worker still answers.  A request is queued to the
 * request worker, which alone touches the panel's modeset state, and is
 * answered from inside the display window.
 *
 * The header is neutral: the request worker (../worker.c) includes it
 * without the display's types.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_BACKLIGHT_H
#define DRIVERS_GPU_I915_DISPLAY_BACKLIGHT_H

#include <stdint.h>

struct i915_device;

int drv_i915_display_backlight_register(struct i915_device *device);
void drv_i915_display_backlight_unregister(struct i915_device *device);
int drv_i915_display_backlight_serve(struct i915_device *device, int in_display, int set, uint32_t *percent);

#endif
