/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The one-screen modeset, the one picture on the panel and the resident
 * panel run (modeset.c).
 *
 * The modeset object of each screen (prepare, the enable and disable
 * commits, status), the show body that puts one buffer on the panel and
 * stops it safely, and the run of the resident node that keeps the panel
 * up while the request worker serves.  Only neutral display types appear
 * here; the functions that take the modeset environment's Linux types are
 * declared in modeset-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_MODESET_H
#define DRIVERS_GPU_I915_DISPLAY_MODESET_H

#include "internal.h"

struct i915_display;
struct i915_xd_platform;

/*
 * ==== The modeset world ====
 */

/* Allocates and frees the modeset world (display->lcd_world); create returns 0, ENOMEM or EBUSY. */
int drv_i915_lcd_world_create(struct i915_display *display);
void drv_i915_lcd_world_destroy(struct i915_display *display);

/* Sets and reports the display version of the device (12 = Tiger Lake, 13 = ADL-P class). */
void drv_i915_lcd_set_display_ver(struct i915_display *display, int ver);
int drv_i915_lcd_display_ver(void);

/*
 * Sets and reports the xd platform description the modeset's clocks read
 * (the display's; before one is set, the first description of the
 * display version).
 */
void drv_i915_lcd_set_xd_platform(struct i915_display *display, const struct i915_xd_platform *platform);
const struct i915_xd_platform *drv_i915_lcd_xd_platform(void);

/* Records an anomaly the backend found (a power well kept on, a broken time base) as an error of the screen whose operation runs. */
void drv_i915_lcd_backend_fault(const char *what);

/* Names the power domain of one power use of a screen's output: a screen backend's power_domain hook (pipe.c). */
int drv_i915_lcd_power_domain_of(void *ctx, unsigned use, unsigned index, int thunderbolt);

/*
 * Converts a run's state and configuration of an output into the
 * configuration a screen is prepared from (xd-picture.h).
 */
void drv_i915_lcd_picture_config(const struct i915_lcd_state *s, const struct i915_lcd_modeset_cfg *cfg, struct i915_xd_picture_config *config);

/*
 * ==== One picture on the panel ====
 */

/* Shows a buffer somebody else prepared; the buffer is not created, unpinned or destroyed here. */
int drv_i915_lcd_show_prepared(struct i915_display *display, struct i915_lcd_show_env *env, struct i915_scanout *so, uint32_t (*verify)(void *ctx, const struct i915_scanout *so), void *verify_ctx, struct i915_lcd_show_report *out);

/* The device-side latches of an unconfirmed stop and of a GPU not shown to be done. */
int drv_i915_lcd_show_retained(struct i915_display *display);
int drv_i915_lcd_show_gpu_retained(struct i915_display *display);

/*
 * ==== The steps of a panel run ====
 *
 * The resident run is made of these; a test scenario uses the same steps
 * with its own parameters.  The run is always the display's lk member.
 */

/* The second output's parameters, encoder and link fallback, as the resident run's of that kind. */
int drv_i915_lcd_output_params(const struct i915_display_output *output, struct i915_lcd_run_params *params);
void drv_i915_lcd_output_cfg(struct i915_display *display, const struct i915_lcd_kernel_deps *d, const struct i915_display_output *output, struct i915_lcd_modeset_cfg *cfg);
int drv_i915_lcd_output_link_fallback(struct i915_display *display, struct i915_display_output *output, int enable_rc);

/* Creates the two modeset mutexes once; they live as long as the device. */
void drv_i915_lcd_kernel_locks_init(struct i915_display *display);

/* Binds the register, wait, panel, power and vblank hooks of a panel run (k is display->lk). */
void drv_i915_lcd_kernel_bind_ops(struct i915_lcd_kernel *k);

/* Checks that the hardware is as the normal initialisation left it: 0, or EBUSY when nothing may be written. */
int drv_i915_lcd_kernel_preflight(struct i915_lcd_kernel *k);

/* Fills the modeset configuration of a panel run from the initialisation's objects: 0, or EINVAL. */
int drv_i915_lcd_kernel_fill_cfg(struct i915_lcd_kernel *k, struct i915_lcd_modeset_cfg *c);

/* The stage hook of a panel run (ctx is the run). */
void drv_i915_lcd_kernel_at_stage(void *ctx, int stage);

/*
 * ==== The resident panel run ====
 */

/*
 * Lights the panel with the two resident buffers, calls serve while the
 * picture is up, and runs the stop path when serve returns: 0 when the
 * panel came up and was stopped and released cleanly.
 */
int drv_i915_lcd_kernel_resident_run(struct i915_display *display, const struct i915_lcd_kernel_deps *d, int (*serve)(void *ctx), void *ctx);

#endif /* DRIVERS_GPU_I915_DISPLAY_MODESET_H */
