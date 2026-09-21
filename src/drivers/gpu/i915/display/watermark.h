/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DRAM and display bandwidth, SAGV, and the watermark and DDB text
 * (watermark.c).
 *
 * The PCODE-probed DRAM and bandwidth table, the forced SAGV disable and the
 * bandwidth and PM demand global objects of the driver probe, and the
 * Linux watermark, DBUF and MBUS text of the modeset.  The functions of the
 * Linux text take the modeset environment's types; those types appear here
 * only as incomplete structures, so a neutral file may include this header.
 * The Linux-named functions the other modeset files call (skl_write_plane_wm,
 * skl_ddb_allocation_overlaps, skl_ddb_dbuf_slice_mask and
 * drv_i915_lcd_wm_get_hw_state) are declared in modeset-internal.h and
 * takeover-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_WATERMARK_H
#define DRIVERS_GPU_I915_DISPLAY_WATERMARK_H

#include "internal.h"

struct i915_display;
struct i915_mmio;
struct i915_lcd_modeset;
struct i915_lcd_wm_ctx;
struct i915_wm_world;
struct intel_atomic_state;
struct intel_bw_state;
struct intel_crtc_state;
struct intel_dbuf_state;
struct intel_plane_state;

/*
 * ==== The watermark world ====
 */

int drv_i915_wm_world_create(struct i915_display *display);
void drv_i915_wm_world_destroy(struct i915_display *display);

/*
 * ==== The DRAM and the bandwidth table (driver probe) ====
 */

int drv_i915_dram_decode(uint32_t val, struct i915_dram_info *di);
int drv_i915_dram_detect(struct mutex *sb_lock, struct i915_mmio *mmio, struct i915_dram_info *di);
int drv_i915_bw_init_hw(struct mutex *sb_lock, struct i915_mmio *mmio, const struct i915_dram_info *di, struct i915_bw_state *bw);

/*
 * ==== SAGV, the QGV points and the bandwidth and PM demand objects ====
 */

int drv_i915_has_sagv(int display_ver, int legacy_platform, int sagv_status);
uint16_t drv_i915_icl_qgv_points_mask(const struct i915_bw_state *bw);
unsigned drv_i915_icl_qgv_bw(const struct i915_bw_state *bw, int display_ver, int num_active_planes, int qgv_point);
unsigned drv_i915_icl_max_bw_qgv_point_mask(const struct i915_bw_state *bw, int display_ver, int num_active_planes);
unsigned drv_i915_icl_max_bw_psf_gv_point_mask(const struct i915_bw_state *bw);
int drv_i915_icl_pcode_restrict_qgv_points(struct mutex *sb_lock, struct i915_mmio *mmio, int display_ver, struct i915_bw_state *bw, uint32_t points_mask);
int drv_i915_bw_init(struct i915_display_state *d, int display_ver, struct i915_bw_state *bw, struct mutex *sb_lock, struct i915_mmio *mmio);
int drv_i915_pmdemand_init(struct i915_display_state *d, int display_ver, struct i915_mmio *mmio, int ip_step);

/*
 * ==== The bandwidth and watermark text of the modeset ====
 */

void drv_i915_bw_crtc_update(struct intel_bw_state *bw_state, const struct intel_crtc_state *crtc_state);
int drv_i915_lcd_ms_bw_min_cdclk(struct i915_lcd_modeset *ms);
unsigned int drv_i915_lcd_ms_bw_data_rate(struct i915_lcd_modeset *ms);
bool drv_i915_wm_plane_visible(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
int drv_i915_lcd_ms_wm_compute(struct i915_wm_world *wm_world, struct i915_lcd_modeset *ms);
void drv_i915_lcd_ms_wm_compute_off(struct i915_wm_world *wm_world, struct i915_lcd_modeset *ms);
void drv_i915_dbuf_pre_plane_update(struct i915_lcd_wm_ctx *wm, struct intel_atomic_state *state);
void drv_i915_dbuf_post_plane_update(struct i915_lcd_wm_ctx *wm, struct intel_atomic_state *state);
void drv_i915_mbus_dbox_update(struct i915_lcd_wm_ctx *wm, struct intel_atomic_state *state);
int drv_i915_lcd_dbuf_current(struct i915_wm_world *wm_world, struct intel_dbuf_state *out);
void drv_i915_lcd_dbuf_publish(struct i915_wm_world *wm_world, const struct intel_dbuf_state *now);
void drv_i915_lcd_dbuf_forget(struct i915_wm_world *wm_world);

#endif /* DRIVERS_GPU_I915_DISPLAY_WATERMARK_H */
