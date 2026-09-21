/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display software state and the state calculation (state.c).
 *
 * The tail of the Linux driver probe's noirq stage that builds the display
 * software state (the mode configuration, the CDCLK and DBUF global
 * objects, the colour state, the quirks and FBC), the error and note sink
 * of the modeset environment's Linux text, and the calculation that turns
 * what the panel reports into the mode, link, M/N and PLL values and into
 * the register words the modeset would write.
 *
 * Only neutral display types appear here.  The world of the modeset
 * environment is passed as an incomplete type.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_STATE_H
#define DRIVERS_GPU_I915_DISPLAY_STATE_H

#include "internal.h"

struct i915_lcd_world;

/*
 * ==== The display software state ====
 */

void drv_i915_mode_config_init(struct i915_display_state *d, int display_ver, int legacy_platform);
int drv_i915_cdclk_init(struct i915_display_state *d);
int drv_i915_color_init(struct i915_display_state *d, int display_ver);
int drv_i915_dbuf_init(struct i915_display_state *d);
void drv_i915_init_quirks(struct i915_display_state *d, uint16_t device, uint16_t subsystem_vendor, uint16_t subsystem_device);
int drv_i915_sanitize_fbc_option(int display_ver, int legacy_platform, unsigned fbc_mask, int enable_fbc_param);
void drv_i915_fbc_init(struct i915_display_state *d, int display_ver, unsigned fbc_mask, int vtd_active, int legacy_platform);
void drv_i915_display_state_fini(struct i915_display_state *d);

/*
 * The global state objects: the storage and the list the bandwidth and PM
 * demand objects (watermark.c) are registered on as well.
 */
int drv_i915_alloc_global_state(struct i915_display_state *d, void *storage, unsigned size);
void drv_i915_atomic_global_obj_init(struct i915_display_state *d, struct i915_global_obj *obj, struct i915_global_state *state, const struct i915_global_state_funcs *funcs);

/*
 * ==== The error and note sink of the modeset environment ====
 *
 * drv_i915_lcd_note() and drv_i915_lcd_error() themselves are declared in
 * modeset-internal.h, where the Linux text reaches them.
 */

void drv_i915_lcd_error_bind(struct i915_lcd_world *world, void (*hook)(void *ctx, const char *what), void *ctx);
unsigned drv_i915_lcd_errors(const struct i915_lcd_world *world);

/*
 * ==== The state calculation ====
 */

int drv_i915_lcd_compute(struct i915_lcd_world *world, const uint8_t *edid128, const uint8_t *dpcd, const uint8_t *edp_dpcd, int vbt_bpp, int ref_nssc_khz, struct i915_lcd_state *out);
int drv_i915_lcd_compute_hdmi(const struct i915_lcd_mode *mode, int ref_nssc_khz, struct i915_lcd_state *out);
int drv_i915_lcd_emit_plane(struct i915_lcd_world *world, int pipe, int plane_id, uint32_t fourcc, uint64_t modifier, uint32_t width, uint32_t height, uint32_t pitch, uint32_t surf_ggtt_offset, struct i915_lcd_words *out);
int drv_i915_lcd_emit_cpu_transcoder(struct i915_lcd_world *world, const struct i915_lcd_state *s, int pipe, int cpu_transcoder, struct i915_lcd_words *out);
int drv_i915_lcd_emit_ddi(struct i915_lcd_world *world, const struct i915_lcd_state *s, int port, int pipe, int cpu_transcoder, uint32_t saved_port_bits, struct i915_lcd_words *out, uint32_t *ddi_buf_ctl_value);
int drv_i915_lcd_emit_transcoder(struct i915_lcd_world *world, const struct i915_lcd_state *s, int pipe, int cpu_transcoder, uint32_t src_width, uint32_t src_height, struct i915_lcd_words *out);
int drv_i915_lcd_words_step(const struct i915_lcd_words *w, const char *name, unsigned from);
unsigned drv_i915_lcd_words_find(const struct i915_lcd_words *w, uint32_t reg, uint32_t *value);

/*
 * ==== The panel the resident display drives ====
 */

int drv_i915_display_panel_mode(const struct i915_lcd_kernel_deps *d, uint32_t *width, uint32_t *height, uint32_t *refresh_millihz);
int drv_i915_display_panel_size_mm(const struct i915_lcd_kernel_deps *d, uint32_t *width_mm, uint32_t *height_mm);

#endif /* DRIVERS_GPU_I915_DISPLAY_STATE_H */
