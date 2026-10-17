/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel's diagnostics (diagnostics.c).
 *
 * The recorder stacked on the modeset hooks, the observer of the pipe's
 * underrun status and frame counter, the named registers the run log reads
 * back next to Linux's dump, the last-resort stop of a pipe left scanning
 * out, the known test picture, and the run log itself.  Only the functions
 * whose arguments are neutral display types are declared here; struct
 * i915_lcd_world is an incomplete type outside the modeset environment.
 *
 * Errors: the functions that report an error return a positive errno.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DIAGNOSTICS_H
#define DRIVERS_GPU_I915_DISPLAY_DIAGNOSTICS_H

#include "internal.h"

struct i915_display;
struct i915_lcd_world;
struct i915_mmio;

/*
 * ==== The modeset run log ====
 */

/* Stacks the recorder on a backend; hand trace->ops to the modeset. */
void drv_i915_lcd_trace_init(struct i915_lcd_trace *trace, struct i915_lcd_emit *backend);

/* Records a phase marker written by the caller. */
void drv_i915_lcd_trace_phase(struct i915_lcd_trace *trace, const char *name);

/*
 * ==== The commit observer ====
 */

/* Prepares the observer of one pipe over the backend it reads. */
void drv_i915_lcd_observer_init(struct i915_lcd_observer *observer, struct i915_lcd_emit *hw, int pipe);

/* The observe hook: samples at a named point of the commit (ctx is the observer). */
void drv_i915_lcd_observer_point(void *ctx, int point);

/* Samples once more as start-up, then starts the steady period. */
void drv_i915_lcd_observer_steady_begin(struct i915_lcd_observer *observer);

/* Samples during the steady period. */
void drv_i915_lcd_observer_steady_sample(struct i915_lcd_observer *observer);

/* Ends the steady period with a last sample (nothing when not steady). */
void drv_i915_lcd_observer_steady_end(struct i915_lcd_observer *observer);

/* Waits up to window_ms for min_frames frames; 0, or ETIMEDOUT when the counter did not advance enough. */
int drv_i915_lcd_observer_frames(struct i915_lcd_observer *observer, unsigned window_ms, unsigned min_frames, uint32_t *first, uint32_t *last);

/* Reports the stop evidence; 0, EINVAL when none was taken, or EBUSY when the pipe was not stopped. */
int drv_i915_lcd_observer_stopped(struct i915_lcd_observer *observer, unsigned window_ms, uint32_t *first, uint32_t *last);

/*
 * ==== The named registers and the last-resort stop ====
 */

/* Stops every pipe and DDI buffer still on; returns how many were stopped. */
unsigned drv_i915_lcd_last_resort_stop(struct i915_mmio *mmio);

/*
 * ==== The readout survey ====
 */

/* Logs the pipes, DDI buffers, PLLs and power wells the firmware left on; changes no state. */
void drv_i915_display_survey(struct i915_display *display);

/*
 * ==== The test picture ====
 */

/*
 * ==== The panel run's hooks and log ====
 */

/* The step hook: counts and prints a decided or an unresolved callee (ctx is the struct i915_lcd_kernel). */
void drv_i915_lcd_kernel_step(void *ctx, const char *name);

/* The error hook: counts and prints an error of the Linux text (ctx is the struct i915_lcd_kernel). */
void drv_i915_lcd_kernel_error(void *ctx, const char *what);

/* The debug hook: prints a debug message under i915.debug=display, ignores it otherwise (ctx is the struct i915_lcd_kernel). */
void drv_i915_lcd_kernel_debug(void *ctx, const char *what);

/* Turns the detailed display log of i915.debug=display on (nonzero) or off. */
void drv_i915_lcd_debug_set(int enabled);

/* Reports whether i915.debug=display asked for the detailed display log. */
int drv_i915_lcd_debug_enabled(void);

/* Logs the run log. */
void drv_i915_lcd_log_trace(const struct i915_lcd_trace *trace);

/* Logs the observer's findings and samples. */
void drv_i915_lcd_log_observer(const struct i915_lcd_observer *observer);

#endif /* DRIVERS_GPU_I915_DISPLAY_DIAGNOSTICS_H */
