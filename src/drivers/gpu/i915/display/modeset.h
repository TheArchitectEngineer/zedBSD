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

/*
 * ==== The modeset world ====
 */

/* Allocates and frees the modeset world (display->lcd_world); create returns 0, ENOMEM or EBUSY. */
int drv_i915_lcd_world_create(struct i915_display *display);
void drv_i915_lcd_world_destroy(struct i915_display *display);

/* Sets and reports the display version of the device (12 = Tiger Lake, 13 = ADL-P class). */
void drv_i915_lcd_set_display_ver(struct i915_display *display, int ver);
int drv_i915_lcd_display_ver(void);

/* Records an anomaly the backend found (a power well kept on, a broken time base) as a Linux-text error. */
void drv_i915_lcd_backend_fault(const char *what);

/*
 * ==== The modeset object of the selected screen ====
 */

/* Selects the screen the calls below work on: 0, or EINVAL for a screen out of range. */
int drv_i915_lcd_modeset_select(struct i915_display *display, unsigned screen);
unsigned drv_i915_lcd_modeset_selected(struct i915_display *display);

/*
 * Builds the new state of the selected screen and runs the check phase:
 * 0, EINVAL (a configuration this path does not cover) or EBUSY (the
 * screen is running or retained, or no DPLL is free), or a negative
 * watermark or CDCLK result of the Linux text.
 */
int drv_i915_lcd_modeset_prepare(struct i915_display *display, const struct i915_lcd_state *s, const struct i915_lcd_modeset_cfg *cfg, struct i915_lcd_emit *ops);

/* The two commits (I915_LCD_MS_* results). */
int drv_i915_lcd_modeset_commit_enable(struct i915_display *display);
int drv_i915_lcd_modeset_commit_disable(struct i915_display *display);

/* The stages the commits are made of (I915_LCD_MS_* results). */
int drv_i915_lcd_modeset_enable(struct i915_display *display);
int drv_i915_lcd_modeset_plane_update(struct i915_display *display);
int drv_i915_lcd_modeset_plane_disable(struct i915_display *display);
int drv_i915_lcd_modeset_disable(struct i915_display *display);

/* What the selected screen holds, and the sink's link status read now. */
void drv_i915_lcd_modeset_status(struct i915_display *display, struct i915_lcd_modeset_status *out);
void drv_i915_lcd_modeset_link_status(struct i915_display *display, struct i915_lcd_modeset_status *out);

/* The caller confirmed on the hardware that the plane is off. */
void drv_i915_lcd_modeset_plane_released(struct i915_display *display);

/* After an unconfirmed stop: the retained state and its only way out on a model backend. */
void drv_i915_lcd_modeset_abandoned(struct i915_display *display);
int drv_i915_lcd_modeset_retained(struct i915_display *display);
int drv_i915_lcd_modeset_discard_model(struct i915_display *display, const struct i915_lcd_emit *ops);

/* Flips the running picture to another buffer (I915_LCD_MS_* result, details in out). */
int drv_i915_lcd_modeset_flip(struct i915_display *display, uint32_t new_surf, struct i915_lcd_flip_result *out);

/*
 * ==== One picture on the panel ====
 */

/* 0 when the picture was up for the whole window, the stop was confirmed and everything was given back. */
int drv_i915_lcd_show_run(struct i915_display *display, struct i915_lcd_show_env *env, struct i915_lcd_show_report *out);

/* Shows a buffer somebody else prepared; the buffer is not created, unpinned or destroyed here. */
int drv_i915_lcd_show_prepared(struct i915_display *display, struct i915_lcd_show_env *env, struct i915_scanout *so, uint32_t (*verify)(void *ctx, const struct i915_scanout *so), void *verify_ctx, struct i915_lcd_show_report *out);

/* The device-side latches of an unconfirmed stop and of a GPU not shown to be done. */
int drv_i915_lcd_show_retained(struct i915_display *display);
void drv_i915_lcd_show_retain_gpu(struct i915_display *display, const void *gm, const char *why);
int drv_i915_lcd_show_gpu_retained(struct i915_display *display);
int drv_i915_lcd_show_discard_gpu_model(struct i915_display *display, const void *gm, int gm_finalised);
int drv_i915_lcd_show_discard_model(struct i915_display *display, struct i915_lcd_show_env *env);

/*
 * ==== The steps of a panel run ====
 *
 * The resident run is made of these; a test scenario uses the same steps
 * with its own parameters.  The run is always the display's lk member.
 */

/*
 * The parameters of one panel run.
 *
 * The resident run has none (its pointer stays NULL); a scenario of the
 * tests names the picture, the window, an HDMI output instead of the
 * panel, and whether the run starts from an empty DPLL pool.
 */
struct i915_lcd_run_params {
	unsigned pattern_id;
	uint64_t pattern_fnv;
	unsigned window_ms;
	int (*in_window)(void *ctx, struct i915_lcd_observer *o);
	int output_hdmi;
	int port;
	int pipe;
	int cpu_transcoder;
	int dpll_id;
	const struct i915_lcd_state *state;
	const char *tag;
	int reset_dplls;
};

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
