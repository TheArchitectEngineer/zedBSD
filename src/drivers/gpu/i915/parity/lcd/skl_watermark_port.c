// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/skl_watermark.c
 * (sha256 40d3b12b5bf5a8b0b7363bba4358d25aa67d72ff9635639e1fd99a8a4e8e22df) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_dbuf_enabled_slices, check_mbus_joined, adlp_check_mbus_joined, compute_dbuf_slices,
 *    adlp_compute_dbuf_slices, skl_compute_dbuf_slices, intel_dbuf_slice_size, skl_ddb_entry_init,
 *    mbus_ddb_offset, skl_watermark_ipc_enabled, skl_needs_memory_bw_wa, intel_get_linetime_us,
 *    skl_cursor_allocation, skl_total_relative_data_rate, intel_crtc_dbuf_weights, skl_wm_check_vblank,
 *    skl_wm_latency, skl_wm_method1, skl_wm_method2, skl_compute_wm_params,
 *    skl_compute_plane_wm_params, skl_wm_has_lines, skl_wm_max_lines, skl_compute_plane_wm,
 *    skl_compute_wm_levels, tgl_compute_sagv_wm, skl_compute_transition_wm, skl_build_plane_wm_single,
 *    icl_build_plane_wm, skl_build_plane_wm_uv, skl_build_plane_wm, skl_max_wm0_lines,
 *    skl_max_wm_level_for_vblank, skl_is_vblank_too_short, skl_build_pipe_wm, skl_check_wm_level,
 *    skl_check_nv12_wm_level, skl_need_wm_copy_wa, use_minimal_wm0_only, skl_allocate_plane_ddb,
 *    skl_crtc_allocate_plane_ddb, skl_ddb_entry_for_slices, intel_crtc_ddb_weight, skl_crtc_allocate_ddb,
 *    skl_plane_wm_level, skl_plane_trans_wm, skl_write_wm_level, skl_ddb_entry_write,
 *    skl_write_plane_wm, intel_dbuf_mdclk_cdclk_ratio_update, update_mbus_pre_enable, intel_dbuf_pre_plane_update,
 *    intel_dbuf_post_plane_update, xelpdp_is_only_pipe_per_dbuf_bank, intel_mbus_dbox_update, skl_wm_level_from_reg_val,
 *    skl_pipe_wm_get_hw_state, skl_pipe_ddb_get_hw_state, skl_ddb_get_hw_plane_state, skl_ddb_entry_union,
 *    skl_wm_get_hw_state, skl_dbuf_is_misconfigured, skl_wm_sanitize, skl_wm_get_hw_state_and_sanitize,
 *    skl_ddb_entry_init_from_hw, skl_ddb_dbuf_slice_mask, skl_ddb_entries_overlap, skl_ddb_allocation_overlaps;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_plane_compat.h, lcd_wm_compat.h;
 *  - parity_wm_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_plane_compat.h"
#include "lcd_wm_compat.h"

struct skl_wm_params {
	bool x_tiled, y_tiled;
	bool rc_surface;
	bool is_planar;
	u32 width;
	u8 cpp;
	u32 plane_pixel_rate;
	u32 y_min_scanlines;
	u32 plane_bytes_per_line;
	uint_fixed_16_16_t plane_blocks_per_line;
	uint_fixed_16_16_t y_tile_minimum;
	u32 linetime_us;
	u32 dbuf_block_size;
};

struct skl_plane_ddb_iter {
	u64 data_rate;
	u16 start, size;
};

struct dbuf_slice_conf_entry {
	u8 active_pipes;
	u8 dbuf_mask[I915_MAX_PIPES];
	bool join_mbus;
};

static const struct dbuf_slice_conf_entry adlp_allowed_dbufs[] = {
	/*
	 * Keep the join_mbus cases first so check_mbus_joined()
	 * will prefer them over the !join_mbus cases.
	 */
	{
		.active_pipes = BIT(PIPE_A),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2) | BIT(DBUF_S3) | BIT(DBUF_S4),
		},
		.join_mbus = true,
	},
	{
		.active_pipes = BIT(PIPE_B),
		.dbuf_mask = {
			[PIPE_B] = BIT(DBUF_S1) | BIT(DBUF_S2) | BIT(DBUF_S3) | BIT(DBUF_S4),
		},
		.join_mbus = true,
	},
	{
		.active_pipes = BIT(PIPE_A),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
		.join_mbus = false,
	},
	{
		.active_pipes = BIT(PIPE_B),
		.dbuf_mask = {
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
		},
		.join_mbus = false,
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_B),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
		},
	},
	{
		.active_pipes = BIT(PIPE_C),
		.dbuf_mask = {
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
		},
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_C),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
		},
	},
	{
		.active_pipes = BIT(PIPE_B) | BIT(PIPE_C),
		.dbuf_mask = {
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
		},
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
		},
	},
	{
		.active_pipes = BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_B) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_C) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_C) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{
		.active_pipes = BIT(PIPE_A) | BIT(PIPE_B) | BIT(PIPE_C) | BIT(PIPE_D),
		.dbuf_mask = {
			[PIPE_A] = BIT(DBUF_S1) | BIT(DBUF_S2),
			[PIPE_B] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_C] = BIT(DBUF_S3) | BIT(DBUF_S4),
			[PIPE_D] = BIT(DBUF_S1) | BIT(DBUF_S2),
		},
	},
	{}

};

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static u8 intel_dbuf_enabled_slices(const struct intel_dbuf_state *dbuf_state);
static bool check_mbus_joined(u8 active_pipes,
			      const struct dbuf_slice_conf_entry *dbuf_slices);
static bool adlp_check_mbus_joined(u8 active_pipes);
static u8 compute_dbuf_slices(enum pipe pipe, u8 active_pipes, bool join_mbus,
			      const struct dbuf_slice_conf_entry *dbuf_slices);
static u8 adlp_compute_dbuf_slices(enum pipe pipe, u8 active_pipes, bool join_mbus);
static u8 skl_compute_dbuf_slices(struct intel_crtc *crtc, u8 active_pipes, bool join_mbus);
static int intel_dbuf_slice_size(struct drm_i915_private *i915);
static u16 skl_ddb_entry_init(struct skl_ddb_entry *entry,
			      u16 start, u16 end);
static unsigned int mbus_ddb_offset(struct drm_i915_private *i915, u8 slice_mask);
static bool skl_needs_memory_bw_wa(struct drm_i915_private *i915);
static uint_fixed_16_16_t
intel_get_linetime_us(const struct intel_crtc_state *crtc_state);
static unsigned int
skl_cursor_allocation(const struct intel_crtc_state *crtc_state,
		      int num_active);
static u64
skl_total_relative_data_rate(const struct intel_crtc_state *crtc_state);
static void intel_crtc_dbuf_weights(const struct intel_dbuf_state *dbuf_state,
				    enum pipe for_pipe,
				    unsigned int *weight_start,
				    unsigned int *weight_end,
				    unsigned int *weight_total);
static int skl_wm_check_vblank(struct intel_crtc_state *crtc_state);
static unsigned int skl_wm_latency(struct drm_i915_private *i915, int level,
				   const struct skl_wm_params *wp);
static uint_fixed_16_16_t
skl_wm_method1(const struct drm_i915_private *i915, u32 pixel_rate,
	       u8 cpp, u32 latency, u32 dbuf_block_size);
static uint_fixed_16_16_t
skl_wm_method2(u32 pixel_rate, u32 pipe_htotal, u32 latency,
	       uint_fixed_16_16_t plane_blocks_per_line);
static int
skl_compute_wm_params(const struct intel_crtc_state *crtc_state,
		      int width, const struct drm_format_info *format,
		      u64 modifier, unsigned int rotation,
		      u32 plane_pixel_rate, struct skl_wm_params *wp,
		      int color_plane);
static int
skl_compute_plane_wm_params(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state,
			    struct skl_wm_params *wp, int color_plane);
static bool skl_wm_has_lines(struct drm_i915_private *i915, int level);
static int skl_wm_max_lines(struct drm_i915_private *i915);
static void skl_compute_plane_wm(const struct intel_crtc_state *crtc_state,
				 struct intel_plane *plane,
				 int level,
				 unsigned int latency,
				 const struct skl_wm_params *wp,
				 const struct skl_wm_level *result_prev,
				 struct skl_wm_level *result /* out */);
static void
skl_compute_wm_levels(const struct intel_crtc_state *crtc_state,
		      struct intel_plane *plane,
		      const struct skl_wm_params *wm_params,
		      struct skl_wm_level *levels);
static void tgl_compute_sagv_wm(const struct intel_crtc_state *crtc_state,
				struct intel_plane *plane,
				const struct skl_wm_params *wm_params,
				struct skl_plane_wm *plane_wm);
static void skl_compute_transition_wm(struct drm_i915_private *i915,
				      struct skl_wm_level *trans_wm,
				      const struct skl_wm_level *wm0,
				      const struct skl_wm_params *wp);
static int skl_build_plane_wm_single(struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state,
				     struct intel_plane *plane, int color_plane);
static int icl_build_plane_wm(struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state);
static int skl_build_plane_wm_uv(struct intel_crtc_state *crtc_state,
				 const struct intel_plane_state *plane_state,
				 struct intel_plane *plane);
static int skl_build_plane_wm(struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state);
static int skl_max_wm0_lines(const struct intel_crtc_state *crtc_state);
static int skl_max_wm_level_for_vblank(struct intel_crtc_state *crtc_state,
				       int wm0_lines);
static bool
skl_is_vblank_too_short(const struct intel_crtc_state *crtc_state,
			int wm0_lines, int latency);
static int skl_build_pipe_wm(struct intel_atomic_state *state,
			     struct intel_crtc *crtc);
static void
skl_check_wm_level(struct skl_wm_level *wm, const struct skl_ddb_entry *ddb);
static void
skl_check_nv12_wm_level(struct skl_wm_level *wm, struct skl_wm_level *uv_wm,
			const struct skl_ddb_entry *ddb_y, const struct skl_ddb_entry *ddb);
static bool skl_need_wm_copy_wa(struct drm_i915_private *i915, int level,
				const struct skl_plane_wm *wm);
static bool
use_minimal_wm0_only(const struct intel_crtc_state *crtc_state,
		     struct intel_plane *plane);
static void
skl_allocate_plane_ddb(struct skl_plane_ddb_iter *iter,
		       struct skl_ddb_entry *ddb,
		       const struct skl_wm_level *wm,
		       u64 data_rate);
static int
skl_crtc_allocate_plane_ddb(struct intel_atomic_state *state,
			    struct intel_crtc *crtc);
static void
skl_ddb_entry_for_slices(struct drm_i915_private *i915, u8 slice_mask,
			 struct skl_ddb_entry *ddb);
static unsigned int intel_crtc_ddb_weight(const struct intel_crtc_state *crtc_state);
static int
skl_crtc_allocate_ddb(struct intel_atomic_state *state, struct intel_crtc *crtc);
static const struct skl_wm_level *
skl_plane_wm_level(const struct skl_pipe_wm *pipe_wm,
		   enum plane_id plane_id,
		   int level);
static const struct skl_wm_level *
skl_plane_trans_wm(const struct skl_pipe_wm *pipe_wm,
		   enum plane_id plane_id);
static void skl_write_wm_level(struct drm_i915_private *i915,
			       i915_reg_t reg,
			       const struct skl_wm_level *level);
static void skl_ddb_entry_write(struct drm_i915_private *i915,
				i915_reg_t reg,
				const struct skl_ddb_entry *entry);
static void intel_dbuf_mdclk_cdclk_ratio_update(struct drm_i915_private *i915,
						u8 ratio,
						bool joined_mbus);
static void update_mbus_pre_enable(struct intel_atomic_state *state);
static bool xelpdp_is_only_pipe_per_dbuf_bank(enum pipe pipe, u8 active_pipes);
static void skl_wm_level_from_reg_val(u32 val, struct skl_wm_level *level);
static void skl_pipe_wm_get_hw_state(struct intel_crtc *crtc,
				     struct skl_pipe_wm *out);
static void skl_pipe_ddb_get_hw_state(struct intel_crtc *crtc,
				      struct skl_ddb_entry *ddb,
				      struct skl_ddb_entry *ddb_y);
static void
skl_ddb_get_hw_plane_state(struct drm_i915_private *i915,
			   const enum pipe pipe,
			   const enum plane_id plane_id,
			   struct skl_ddb_entry *ddb,
			   struct skl_ddb_entry *ddb_y);
static void skl_ddb_entry_union(struct skl_ddb_entry *a,
				const struct skl_ddb_entry *b);
static void skl_wm_get_hw_state(struct drm_i915_private *i915);
static bool skl_dbuf_is_misconfigured(struct drm_i915_private *i915);
static void skl_wm_sanitize(struct drm_i915_private *i915);
static void skl_wm_get_hw_state_and_sanitize(struct drm_i915_private *i915);
static void skl_ddb_entry_init_from_hw(struct skl_ddb_entry *entry, u32 reg);
static bool skl_ddb_entries_overlap(const struct skl_ddb_entry *a,
				    const struct skl_ddb_entry *b);

static u8 intel_dbuf_enabled_slices(const struct intel_dbuf_state *dbuf_state)
{
	struct drm_i915_private *i915 = to_i915(dbuf_state->base.state->base.dev);
	u8 enabled_slices;
	enum pipe pipe;

	/*
	 * FIXME: For now we always enable slice S1 as per
	 * the Bspec display initialization sequence.
	 */
	enabled_slices = BIT(DBUF_S1);

	for_each_pipe(i915, pipe)
		enabled_slices |= dbuf_state->slices[pipe];

	return enabled_slices;
}

static bool check_mbus_joined(u8 active_pipes,
			      const struct dbuf_slice_conf_entry *dbuf_slices)
{
	int i;

	for (i = 0; dbuf_slices[i].active_pipes != 0; i++) {
		if (dbuf_slices[i].active_pipes == active_pipes)
			return dbuf_slices[i].join_mbus;
	}
	return false;
}

static bool adlp_check_mbus_joined(u8 active_pipes)
{
	return check_mbus_joined(active_pipes, adlp_allowed_dbufs);
}

static u8 compute_dbuf_slices(enum pipe pipe, u8 active_pipes, bool join_mbus,
			      const struct dbuf_slice_conf_entry *dbuf_slices)
{
	int i;

	for (i = 0; dbuf_slices[i].active_pipes != 0; i++) {
		if (dbuf_slices[i].active_pipes == active_pipes &&
		    dbuf_slices[i].join_mbus == join_mbus)
			return dbuf_slices[i].dbuf_mask[pipe];
	}
	return 0;
}

static u8 adlp_compute_dbuf_slices(enum pipe pipe, u8 active_pipes, bool join_mbus)
{
	return compute_dbuf_slices(pipe, active_pipes, join_mbus,
				   adlp_allowed_dbufs);
}

static u8 skl_compute_dbuf_slices(struct intel_crtc *crtc, u8 active_pipes, bool join_mbus)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (IS_DG2(i915))
		return dg2_compute_dbuf_slices(pipe, active_pipes, join_mbus);
	else if (DISPLAY_VER(i915) >= 13)
		return adlp_compute_dbuf_slices(pipe, active_pipes, join_mbus);
	else if (DISPLAY_VER(i915) == 12)
		return tgl_compute_dbuf_slices(pipe, active_pipes, join_mbus);
	else if (DISPLAY_VER(i915) == 11)
		return icl_compute_dbuf_slices(pipe, active_pipes, join_mbus);
	/*
	 * For anything else just return one slice yet.
	 * Should be extended for other platforms.
	 */
	return active_pipes & BIT(pipe) ? BIT(DBUF_S1) : 0;
}

static int intel_dbuf_slice_size(struct drm_i915_private *i915)
{
	return DISPLAY_INFO(i915)->dbuf.size /
		hweight8(DISPLAY_INFO(i915)->dbuf.slice_mask);
}

static u16 skl_ddb_entry_init(struct skl_ddb_entry *entry,
			      u16 start, u16 end)
{
	entry->start = start;
	entry->end = end;

	return end;
}

static unsigned int mbus_ddb_offset(struct drm_i915_private *i915, u8 slice_mask)
{
	struct skl_ddb_entry ddb;

	if (slice_mask & (BIT(DBUF_S1) | BIT(DBUF_S2)))
		slice_mask = BIT(DBUF_S1);
	else if (slice_mask & (BIT(DBUF_S3) | BIT(DBUF_S4)))
		slice_mask = BIT(DBUF_S3);

	skl_ddb_entry_for_slices(i915, slice_mask, &ddb);

	return ddb.start;
}

bool skl_watermark_ipc_enabled(struct drm_i915_private *i915)
{
	return i915->display.wm.ipc_enabled;
}

/*
 * FIXME: We still don't have the proper code detect if we need to apply the WA,
 * so assume we'll always need it in order to avoid underruns.
 */
static bool skl_needs_memory_bw_wa(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) == 9;
}

static uint_fixed_16_16_t
intel_get_linetime_us(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	u32 pixel_rate;
	u32 crtc_htotal;
	uint_fixed_16_16_t linetime_us;

	if (!crtc_state->hw.active)
		return u32_to_fixed16(0);

	pixel_rate = crtc_state->pixel_rate;

	if (drm_WARN_ON(&i915->drm, pixel_rate == 0))
		return u32_to_fixed16(0);

	crtc_htotal = crtc_state->hw.pipe_mode.crtc_htotal;
	linetime_us = div_fixed16(crtc_htotal * 1000, pixel_rate);

	return linetime_us;
}

static unsigned int
skl_cursor_allocation(const struct intel_crtc_state *crtc_state,
		      int num_active)
{
	struct intel_plane *plane = to_intel_plane(crtc_state->uapi.crtc->cursor);
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	struct skl_wm_level wm = {};
	int ret, min_ddb_alloc = 0;
	struct skl_wm_params wp;
	int level;

	ret = skl_compute_wm_params(crtc_state, 256,
				    drm_format_info(DRM_FORMAT_ARGB8888),
				    DRM_FORMAT_MOD_LINEAR,
				    DRM_MODE_ROTATE_0,
				    crtc_state->pixel_rate, &wp, 0);
	drm_WARN_ON(&i915->drm, ret);

	for (level = 0; level < i915->display.wm.num_levels; level++) {
		unsigned int latency = skl_wm_latency(i915, level, &wp);

		skl_compute_plane_wm(crtc_state, plane, level, latency, &wp, &wm, &wm);
		if (wm.min_ddb_alloc == U16_MAX)
			break;

		min_ddb_alloc = wm.min_ddb_alloc;
	}

	return max(num_active == 1 ? 32 : 8, min_ddb_alloc);
}

static u64
skl_total_relative_data_rate(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum plane_id plane_id;
	u64 data_rate = 0;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		if (plane_id == PLANE_CURSOR && DISPLAY_VER(i915) < 20)
			continue;

		data_rate += crtc_state->rel_data_rate[plane_id];

		if (DISPLAY_VER(i915) < 11)
			data_rate += crtc_state->rel_data_rate_y[plane_id];
	}

	return data_rate;
}

static void intel_crtc_dbuf_weights(const struct intel_dbuf_state *dbuf_state,
				    enum pipe for_pipe,
				    unsigned int *weight_start,
				    unsigned int *weight_end,
				    unsigned int *weight_total)
{
	struct drm_i915_private *i915 =
		to_i915(dbuf_state->base.state->base.dev);
	enum pipe pipe;

	*weight_start = 0;
	*weight_end = 0;
	*weight_total = 0;

	for_each_pipe(i915, pipe) {
		int weight = dbuf_state->weight[pipe];

		/*
		 * Do not account pipes using other slice sets
		 * luckily as of current BSpec slice sets do not partially
		 * intersect(pipes share either same one slice or same slice set
		 * i.e no partial intersection), so it is enough to check for
		 * equality for now.
		 */
		if (dbuf_state->slices[pipe] != dbuf_state->slices[for_pipe])
			continue;

		*weight_total += weight;
		if (pipe < for_pipe) {
			*weight_start += weight;
			*weight_end += weight;
		} else if (pipe == for_pipe) {
			*weight_end += weight;
		}
	}
}

static int skl_wm_check_vblank(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int wm0_lines, level;

	if (!crtc_state->hw.active)
		return 0;

	wm0_lines = skl_max_wm0_lines(crtc_state);

	level = skl_max_wm_level_for_vblank(crtc_state, wm0_lines);
	if (level < 0)
		return level;

	/*
	 * PSR needs to toggle LATENCY_REPORTING_REMOVED_PIPE_*
	 * based on whether we're limited by the vblank duration.
	 */
	crtc_state->wm_level_disabled = level < i915->display.wm.num_levels - 1;

	for (level++; level < i915->display.wm.num_levels; level++) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			struct skl_plane_wm *wm =
				&crtc_state->wm.skl.optimal.planes[plane_id];

			/*
			 * FIXME just clear enable or flag the entire
			 * thing as bad via min_ddb_alloc=U16_MAX?
			 */
			wm->wm[level].enable = false;
			wm->uv_wm[level].enable = false;
		}
	}

	if (DISPLAY_VER(i915) >= 12 &&
	    i915->display.sagv.block_time_us &&
	    skl_is_vblank_too_short(crtc_state, wm0_lines,
				    i915->display.sagv.block_time_us)) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			struct skl_plane_wm *wm =
				&crtc_state->wm.skl.optimal.planes[plane_id];

			wm->sagv.wm0.enable = false;
			wm->sagv.trans_wm.enable = false;
		}
	}

	return 0;
}

static unsigned int skl_wm_latency(struct drm_i915_private *i915, int level,
				   const struct skl_wm_params *wp)
{
	unsigned int latency = i915->display.wm.skl_latency[level];

	if (latency == 0)
		return 0;

	/*
	 * WaIncreaseLatencyIPCEnabled: kbl,cfl
	 * Display WA #1141: kbl,cfl
	 */
	if ((IS_KABYLAKE(i915) || IS_COFFEELAKE(i915) || IS_COMETLAKE(i915)) &&
	    skl_watermark_ipc_enabled(i915))
		latency += 4;

	if (skl_needs_memory_bw_wa(i915) && wp && wp->x_tiled)
		latency += 15;

	return latency;
}

/*
 * The max latency should be 257 (max the punit can code is 255 and we add 2us
 * for the read latency) and cpp should always be <= 8, so that
 * should allow pixel_rate up to ~2 GHz which seems sufficient since max
 * 2xcdclk is 1350 MHz and the pixel rate should never exceed that.
 */
static uint_fixed_16_16_t
skl_wm_method1(const struct drm_i915_private *i915, u32 pixel_rate,
	       u8 cpp, u32 latency, u32 dbuf_block_size)
{
	u32 wm_intermediate_val;
	uint_fixed_16_16_t ret;

	if (latency == 0)
		return FP_16_16_MAX;

	wm_intermediate_val = latency * pixel_rate * cpp;
	ret = div_fixed16(wm_intermediate_val, 1000 * dbuf_block_size);

	if (DISPLAY_VER(i915) >= 10)
		ret = add_fixed16_u32(ret, 1);

	return ret;
}

static uint_fixed_16_16_t
skl_wm_method2(u32 pixel_rate, u32 pipe_htotal, u32 latency,
	       uint_fixed_16_16_t plane_blocks_per_line)
{
	u32 wm_intermediate_val;
	uint_fixed_16_16_t ret;

	if (latency == 0)
		return FP_16_16_MAX;

	wm_intermediate_val = latency * pixel_rate;
	wm_intermediate_val = DIV_ROUND_UP(wm_intermediate_val,
					   pipe_htotal * 1000);
	ret = mul_u32_fixed16(wm_intermediate_val, plane_blocks_per_line);
	return ret;
}

static int
skl_compute_wm_params(const struct intel_crtc_state *crtc_state,
		      int width, const struct drm_format_info *format,
		      u64 modifier, unsigned int rotation,
		      u32 plane_pixel_rate, struct skl_wm_params *wp,
		      int color_plane)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	u32 interm_pbpl;

	/* only planar format has two planes */
	if (color_plane == 1 &&
	    !intel_format_info_is_yuv_semiplanar(format, modifier)) {
		drm_dbg_kms(&i915->drm,
			    "Non planar format have single plane\n");
		return -EINVAL;
	}

	wp->x_tiled = modifier == I915_FORMAT_MOD_X_TILED;
	wp->y_tiled = modifier != I915_FORMAT_MOD_X_TILED &&
		intel_fb_is_tiled_modifier(modifier);
	wp->rc_surface = intel_fb_is_ccs_modifier(modifier);
	wp->is_planar = intel_format_info_is_yuv_semiplanar(format, modifier);

	wp->width = width;
	if (color_plane == 1 && wp->is_planar)
		wp->width /= 2;

	wp->cpp = format->cpp[color_plane];
	wp->plane_pixel_rate = plane_pixel_rate;

	if (DISPLAY_VER(i915) >= 11 &&
	    modifier == I915_FORMAT_MOD_Yf_TILED  && wp->cpp == 1)
		wp->dbuf_block_size = 256;
	else
		wp->dbuf_block_size = 512;

	if (drm_rotation_90_or_270(rotation)) {
		switch (wp->cpp) {
		case 1:
			wp->y_min_scanlines = 16;
			break;
		case 2:
			wp->y_min_scanlines = 8;
			break;
		case 4:
			wp->y_min_scanlines = 4;
			break;
		default:
			MISSING_CASE(wp->cpp);
			return -EINVAL;
		}
	} else {
		wp->y_min_scanlines = 4;
	}

	if (skl_needs_memory_bw_wa(i915))
		wp->y_min_scanlines *= 2;

	wp->plane_bytes_per_line = wp->width * wp->cpp;
	if (wp->y_tiled) {
		interm_pbpl = DIV_ROUND_UP(wp->plane_bytes_per_line *
					   wp->y_min_scanlines,
					   wp->dbuf_block_size);

		if (DISPLAY_VER(i915) >= 10)
			interm_pbpl++;

		wp->plane_blocks_per_line = div_fixed16(interm_pbpl,
							wp->y_min_scanlines);
	} else {
		interm_pbpl = DIV_ROUND_UP(wp->plane_bytes_per_line,
					   wp->dbuf_block_size);

		if (!wp->x_tiled || DISPLAY_VER(i915) >= 10)
			interm_pbpl++;

		wp->plane_blocks_per_line = u32_to_fixed16(interm_pbpl);
	}

	wp->y_tile_minimum = mul_u32_fixed16(wp->y_min_scanlines,
					     wp->plane_blocks_per_line);

	wp->linetime_us = fixed16_to_u32_round_up(intel_get_linetime_us(crtc_state));

	return 0;
}

static int
skl_compute_plane_wm_params(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state,
			    struct skl_wm_params *wp, int color_plane)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int width;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	width = drm_rect_width(&plane_state->uapi.src) >> 16;

	return skl_compute_wm_params(crtc_state, width,
				     fb->format, fb->modifier,
				     plane_state->hw.rotation,
				     intel_plane_pixel_rate(crtc_state, plane_state),
				     wp, color_plane);
}

static bool skl_wm_has_lines(struct drm_i915_private *i915, int level)
{
	if (DISPLAY_VER(i915) >= 10)
		return true;

	/* The number of lines are ignored for the level 0 watermark. */
	return level > 0;
}

static int skl_wm_max_lines(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 13)
		return 255;
	else
		return 31;
}

static void skl_compute_plane_wm(const struct intel_crtc_state *crtc_state,
				 struct intel_plane *plane,
				 int level,
				 unsigned int latency,
				 const struct skl_wm_params *wp,
				 const struct skl_wm_level *result_prev,
				 struct skl_wm_level *result /* out */)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	uint_fixed_16_16_t method1, method2;
	uint_fixed_16_16_t selected_result;
	u32 blocks, lines, min_ddb_alloc = 0;

	if (latency == 0 ||
	    (use_minimal_wm0_only(crtc_state, plane) && level > 0)) {
		/* reject it */
		result->min_ddb_alloc = U16_MAX;
		return;
	}

	method1 = skl_wm_method1(i915, wp->plane_pixel_rate,
				 wp->cpp, latency, wp->dbuf_block_size);
	method2 = skl_wm_method2(wp->plane_pixel_rate,
				 crtc_state->hw.pipe_mode.crtc_htotal,
				 latency,
				 wp->plane_blocks_per_line);

	if (wp->y_tiled) {
		selected_result = max_fixed16(method2, wp->y_tile_minimum);
	} else {
		if ((wp->cpp * crtc_state->hw.pipe_mode.crtc_htotal /
		     wp->dbuf_block_size < 1) &&
		     (wp->plane_bytes_per_line / wp->dbuf_block_size < 1)) {
			selected_result = method2;
		} else if (latency >= wp->linetime_us) {
			if (DISPLAY_VER(i915) == 9)
				selected_result = min_fixed16(method1, method2);
			else
				selected_result = method2;
		} else {
			selected_result = method1;
		}
	}

	blocks = fixed16_to_u32_round_up(selected_result) + 1;
	/*
	 * Lets have blocks at minimum equivalent to plane_blocks_per_line
	 * as there will be at minimum one line for lines configuration. This
	 * is a work around for FIFO underruns observed with resolutions like
	 * 4k 60 Hz in single channel DRAM configurations.
	 *
	 * As per the Bspec 49325, if the ddb allocation can hold at least
	 * one plane_blocks_per_line, we should have selected method2 in
	 * the above logic. Assuming that modern versions have enough dbuf
	 * and method2 guarantees blocks equivalent to at least 1 line,
	 * select the blocks as plane_blocks_per_line.
	 *
	 * TODO: Revisit the logic when we have better understanding on DRAM
	 * channels' impact on the level 0 memory latency and the relevant
	 * wm calculations.
	 */
	if (skl_wm_has_lines(i915, level))
		blocks = max(blocks,
			     fixed16_to_u32_round_up(wp->plane_blocks_per_line));
	lines = div_round_up_fixed16(selected_result,
				     wp->plane_blocks_per_line);

	if (DISPLAY_VER(i915) == 9) {
		/* Display WA #1125: skl,bxt,kbl */
		if (level == 0 && wp->rc_surface)
			blocks += fixed16_to_u32_round_up(wp->y_tile_minimum);

		/* Display WA #1126: skl,bxt,kbl */
		if (level >= 1 && level <= 7) {
			if (wp->y_tiled) {
				blocks += fixed16_to_u32_round_up(wp->y_tile_minimum);
				lines += wp->y_min_scanlines;
			} else {
				blocks++;
			}

			/*
			 * Make sure result blocks for higher latency levels are
			 * at least as high as level below the current level.
			 * Assumption in DDB algorithm optimization for special
			 * cases. Also covers Display WA #1125 for RC.
			 */
			if (result_prev->blocks > blocks)
				blocks = result_prev->blocks;
		}
	}

	if (DISPLAY_VER(i915) >= 11) {
		if (wp->y_tiled) {
			int extra_lines;

			if (lines % wp->y_min_scanlines == 0)
				extra_lines = wp->y_min_scanlines;
			else
				extra_lines = wp->y_min_scanlines * 2 -
					lines % wp->y_min_scanlines;

			min_ddb_alloc = mul_round_up_u32_fixed16(lines + extra_lines,
								 wp->plane_blocks_per_line);
		} else {
			min_ddb_alloc = blocks + DIV_ROUND_UP(blocks, 10);
		}
	}

	if (!skl_wm_has_lines(i915, level))
		lines = 0;

	if (lines > skl_wm_max_lines(i915)) {
		/* reject it */
		result->min_ddb_alloc = U16_MAX;
		return;
	}

	/*
	 * If lines is valid, assume we can use this watermark level
	 * for now.  We'll come back and disable it after we calculate the
	 * DDB allocation if it turns out we don't actually have enough
	 * blocks to satisfy it.
	 */
	result->blocks = blocks;
	result->lines = lines;
	/* Bspec says: value >= plane ddb allocation -> invalid, hence the +1 here */
	result->min_ddb_alloc = max(min_ddb_alloc, blocks) + 1;
	result->enable = true;

	if (DISPLAY_VER(i915) < 12 && i915->display.sagv.block_time_us)
		result->can_sagv = latency >= i915->display.sagv.block_time_us;
}

static void
skl_compute_wm_levels(const struct intel_crtc_state *crtc_state,
		      struct intel_plane *plane,
		      const struct skl_wm_params *wm_params,
		      struct skl_wm_level *levels)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	struct skl_wm_level *result_prev = &levels[0];
	int level;

	for (level = 0; level < i915->display.wm.num_levels; level++) {
		struct skl_wm_level *result = &levels[level];
		unsigned int latency = skl_wm_latency(i915, level, wm_params);

		skl_compute_plane_wm(crtc_state, plane, level, latency,
				     wm_params, result_prev, result);

		result_prev = result;
	}
}

static void tgl_compute_sagv_wm(const struct intel_crtc_state *crtc_state,
				struct intel_plane *plane,
				const struct skl_wm_params *wm_params,
				struct skl_plane_wm *plane_wm)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	struct skl_wm_level *sagv_wm = &plane_wm->sagv.wm0;
	struct skl_wm_level *levels = plane_wm->wm;
	unsigned int latency = 0;

	if (i915->display.sagv.block_time_us)
		latency = i915->display.sagv.block_time_us +
			skl_wm_latency(i915, 0, wm_params);

	skl_compute_plane_wm(crtc_state, plane, 0, latency,
			     wm_params, &levels[0],
			     sagv_wm);
}

static void skl_compute_transition_wm(struct drm_i915_private *i915,
				      struct skl_wm_level *trans_wm,
				      const struct skl_wm_level *wm0,
				      const struct skl_wm_params *wp)
{
	u16 trans_min, trans_amount, trans_y_tile_min;
	u16 wm0_blocks, trans_offset, blocks;

	/* Transition WM don't make any sense if ipc is disabled */
	if (!skl_watermark_ipc_enabled(i915))
		return;

	/*
	 * WaDisableTWM:skl,kbl,cfl,bxt
	 * Transition WM are not recommended by HW team for GEN9
	 */
	if (DISPLAY_VER(i915) == 9)
		return;

	if (DISPLAY_VER(i915) >= 11)
		trans_min = 4;
	else
		trans_min = 14;

	/* Display WA #1140: glk,cnl */
	if (DISPLAY_VER(i915) == 10)
		trans_amount = 0;
	else
		trans_amount = 10; /* This is configurable amount */

	trans_offset = trans_min + trans_amount;

	/*
	 * The spec asks for Selected Result Blocks for wm0 (the real value),
	 * not Result Blocks (the integer value). Pay attention to the capital
	 * letters. The value wm_l0->blocks is actually Result Blocks, but
	 * since Result Blocks is the ceiling of Selected Result Blocks plus 1,
	 * and since we later will have to get the ceiling of the sum in the
	 * transition watermarks calculation, we can just pretend Selected
	 * Result Blocks is Result Blocks minus 1 and it should work for the
	 * current platforms.
	 */
	wm0_blocks = wm0->blocks - 1;

	if (wp->y_tiled) {
		trans_y_tile_min =
			(u16)mul_round_up_u32_fixed16(2, wp->y_tile_minimum);
		blocks = max(wm0_blocks, trans_y_tile_min) + trans_offset;
	} else {
		blocks = wm0_blocks + trans_offset;
	}
	blocks++;

	/*
	 * Just assume we can enable the transition watermark.  After
	 * computing the DDB we'll come back and disable it if that
	 * assumption turns out to be false.
	 */
	trans_wm->blocks = blocks;
	trans_wm->min_ddb_alloc = max_t(u16, wm0->min_ddb_alloc, blocks + 1);
	trans_wm->enable = true;
}

static int skl_build_plane_wm_single(struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state,
				     struct intel_plane *plane, int color_plane)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct skl_plane_wm *wm = &crtc_state->wm.skl.raw.planes[plane->id];
	struct skl_wm_params wm_params;
	int ret;

	ret = skl_compute_plane_wm_params(crtc_state, plane_state,
					  &wm_params, color_plane);
	if (ret)
		return ret;

	skl_compute_wm_levels(crtc_state, plane, &wm_params, wm->wm);

	skl_compute_transition_wm(i915, &wm->trans_wm,
				  &wm->wm[0], &wm_params);

	if (DISPLAY_VER(i915) >= 12) {
		tgl_compute_sagv_wm(crtc_state, plane, &wm_params, wm);

		skl_compute_transition_wm(i915, &wm->sagv.trans_wm,
					  &wm->sagv.wm0, &wm_params);
	}

	return 0;
}

static int icl_build_plane_wm(struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	struct skl_plane_wm *wm = &crtc_state->wm.skl.raw.planes[plane_id];
	int ret;

	/* Watermarks calculated in master */
	if (plane_state->planar_slave)
		return 0;

	memset(wm, 0, sizeof(*wm));

	if (plane_state->planar_linked_plane) {
		const struct drm_framebuffer *fb = plane_state->hw.fb;

		drm_WARN_ON(&i915->drm,
			    !intel_wm_plane_visible(crtc_state, plane_state));
		drm_WARN_ON(&i915->drm, !fb->format->is_yuv ||
			    fb->format->num_planes == 1);

		ret = skl_build_plane_wm_single(crtc_state, plane_state,
						plane_state->planar_linked_plane, 0);
		if (ret)
			return ret;

		ret = skl_build_plane_wm_single(crtc_state, plane_state,
						plane, 1);
		if (ret)
			return ret;
	} else if (intel_wm_plane_visible(crtc_state, plane_state)) {
		ret = skl_build_plane_wm_single(crtc_state, plane_state,
						plane, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int skl_build_plane_wm_uv(struct intel_crtc_state *crtc_state,
				 const struct intel_plane_state *plane_state,
				 struct intel_plane *plane)
{
	struct skl_plane_wm *wm = &crtc_state->wm.skl.raw.planes[plane->id];
	struct skl_wm_params wm_params;
	int ret;

	wm->is_planar = true;

	/* uv plane watermarks must also be validated for NV12/Planar */
	ret = skl_compute_plane_wm_params(crtc_state, plane_state,
					  &wm_params, 1);
	if (ret)
		return ret;

	skl_compute_wm_levels(crtc_state, plane, &wm_params, wm->uv_wm);

	return 0;
}

static int skl_build_plane_wm(struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	enum plane_id plane_id = plane->id;
	struct skl_plane_wm *wm = &crtc_state->wm.skl.raw.planes[plane_id];
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int ret;

	memset(wm, 0, sizeof(*wm));

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	ret = skl_build_plane_wm_single(crtc_state, plane_state,
					plane, 0);
	if (ret)
		return ret;

	if (fb->format->is_yuv && fb->format->num_planes > 1) {
		ret = skl_build_plane_wm_uv(crtc_state, plane_state,
					    plane);
		if (ret)
			return ret;
	}

	return 0;
}

static int skl_max_wm0_lines(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	enum plane_id plane_id;
	int wm0_lines = 0;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		const struct skl_plane_wm *wm = &crtc_state->wm.skl.optimal.planes[plane_id];

		/* FIXME what about !skl_wm_has_lines() platforms? */
		wm0_lines = max_t(int, wm0_lines, wm->wm[0].lines);
	}

	return wm0_lines;
}

static int skl_max_wm_level_for_vblank(struct intel_crtc_state *crtc_state,
				       int wm0_lines)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	int level;

	for (level = i915->display.wm.num_levels - 1; level >= 0; level--) {
		int latency;

		/* FIXME should we care about the latency w/a's? */
		latency = skl_wm_latency(i915, level, NULL);
		if (latency == 0)
			continue;

		/* FIXME is it correct to use 0 latency for wm0 here? */
		if (level == 0)
			latency = 0;

		if (!skl_is_vblank_too_short(crtc_state, wm0_lines, latency))
			return level;
	}

	return -EINVAL;
}

static bool
skl_is_vblank_too_short(const struct intel_crtc_state *crtc_state,
			int wm0_lines, int latency)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	/* FIXME missing scaler and DSC pre-fill time */
	return crtc_state->framestart_delay +
		intel_usecs_to_scanlines(adjusted_mode, latency) +
		wm0_lines >
		adjusted_mode->crtc_vtotal - adjusted_mode->crtc_vblank_start;
}

static int skl_build_pipe_wm(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	int ret, i;

	for_each_new_intel_plane_in_state(state, plane, plane_state, i) {
		/*
		 * FIXME should perhaps check {old,new}_plane_crtc->hw.crtc
		 * instead but we don't populate that correctly for NV12 Y
		 * planes so for now hack this.
		 */
		if (plane->pipe != crtc->pipe)
			continue;

		if (DISPLAY_VER(i915) >= 11)
			ret = icl_build_plane_wm(crtc_state, plane_state);
		else
			ret = skl_build_plane_wm(crtc_state, plane_state);
		if (ret)
			return ret;
	}

	crtc_state->wm.skl.optimal = crtc_state->wm.skl.raw;

	return skl_wm_check_vblank(crtc_state);
}

/*
 * We only disable the watermarks for each plane if
 * they exceed the ddb allocation of said plane. This
 * is done so that we don't end up touching cursor
 * watermarks needlessly when some other plane reduces
 * our max possible watermark level.
 *
 * Bspec has this to say about the PLANE_WM enable bit:
 * "All the watermarks at this level for all enabled
 *  planes must be enabled before the level will be used."
 * So this is actually safe to do.
 */
static void
skl_check_wm_level(struct skl_wm_level *wm, const struct skl_ddb_entry *ddb)
{
	if (wm->min_ddb_alloc > skl_ddb_entry_size(ddb))
		memset(wm, 0, sizeof(*wm));
}

static void
skl_check_nv12_wm_level(struct skl_wm_level *wm, struct skl_wm_level *uv_wm,
			const struct skl_ddb_entry *ddb_y, const struct skl_ddb_entry *ddb)
{
	if (wm->min_ddb_alloc > skl_ddb_entry_size(ddb_y) ||
	    uv_wm->min_ddb_alloc > skl_ddb_entry_size(ddb)) {
		memset(wm, 0, sizeof(*wm));
		memset(uv_wm, 0, sizeof(*uv_wm));
	}
}

static bool skl_need_wm_copy_wa(struct drm_i915_private *i915, int level,
				const struct skl_plane_wm *wm)
{
	/*
	 * Wa_1408961008:icl, ehl
	 * Wa_14012656716:tgl, adl
	 * Wa_14017887344:icl
	 * Wa_14017868169:adl, tgl
	 * Due to some power saving optimizations, different subsystems
	 * like PSR, might still use even disabled wm level registers,
	 * for "reference", so lets keep at least the values sane.
	 * Considering amount of WA requiring us to do similar things, was
	 * decided to simply do it for all of the platforms, as those wm
	 * levels are disabled, this isn't going to do harm anyway.
	 */
	return level > 0 && !wm->wm[level].enable;
}

static bool
use_minimal_wm0_only(const struct intel_crtc_state *crtc_state,
		     struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	return DISPLAY_VER(i915) >= 13 &&
	       crtc_state->uapi.async_flip &&
	       plane->async_flip;
}

static void
skl_allocate_plane_ddb(struct skl_plane_ddb_iter *iter,
		       struct skl_ddb_entry *ddb,
		       const struct skl_wm_level *wm,
		       u64 data_rate)
{
	u16 size, extra = 0;

	if (data_rate) {
		extra = min_t(u16, iter->size,
			      DIV64_U64_ROUND_UP(iter->size * data_rate,
						 iter->data_rate));
		iter->size -= extra;
		iter->data_rate -= data_rate;
	}

	/*
	 * Keep ddb entry of all disabled planes explicitly zeroed
	 * to avoid skl_ddb_add_affected_planes() adding them to
	 * the state when other planes change their allocations.
	 */
	size = wm->min_ddb_alloc + extra;
	if (size)
		iter->start = skl_ddb_entry_init(ddb, iter->start,
						 iter->start + size);
}

static int
skl_crtc_allocate_plane_ddb(struct intel_atomic_state *state,
			    struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_dbuf_state *dbuf_state =
		intel_atomic_get_new_dbuf_state(state);
	const struct skl_ddb_entry *alloc = &dbuf_state->ddb[crtc->pipe];
	int num_active = hweight8(dbuf_state->active_pipes);
	struct skl_plane_ddb_iter iter;
	enum plane_id plane_id;
	u16 cursor_size;
	u32 blocks;
	int level;

	/* Clear the partitioning for disabled planes. */
	memset(crtc_state->wm.skl.plane_ddb, 0, sizeof(crtc_state->wm.skl.plane_ddb));
	memset(crtc_state->wm.skl.plane_ddb_y, 0, sizeof(crtc_state->wm.skl.plane_ddb_y));

	if (!crtc_state->hw.active)
		return 0;

	iter.start = alloc->start;
	iter.size = skl_ddb_entry_size(alloc);
	if (iter.size == 0)
		return 0;

	/* Allocate fixed number of blocks for cursor. */
	if (DISPLAY_VER(i915) < 20) {
		cursor_size = skl_cursor_allocation(crtc_state, num_active);
		iter.size -= cursor_size;
		skl_ddb_entry_init(&crtc_state->wm.skl.plane_ddb[PLANE_CURSOR],
				   alloc->end - cursor_size, alloc->end);
	}

	iter.data_rate = skl_total_relative_data_rate(crtc_state);

	/*
	 * Find the highest watermark level for which we can satisfy the block
	 * requirement of active planes.
	 */
	for (level = i915->display.wm.num_levels - 1; level >= 0; level--) {
		blocks = 0;
		for_each_plane_id_on_crtc(crtc, plane_id) {
			const struct skl_plane_wm *wm =
				&crtc_state->wm.skl.optimal.planes[plane_id];

			if (plane_id == PLANE_CURSOR && DISPLAY_VER(i915) < 20) {
				const struct skl_ddb_entry *ddb =
					&crtc_state->wm.skl.plane_ddb[plane_id];

				if (wm->wm[level].min_ddb_alloc > skl_ddb_entry_size(ddb)) {
					drm_WARN_ON(&i915->drm,
						    wm->wm[level].min_ddb_alloc != U16_MAX);
					blocks = U32_MAX;
					break;
				}
				continue;
			}

			blocks += wm->wm[level].min_ddb_alloc;
			blocks += wm->uv_wm[level].min_ddb_alloc;
		}

		if (blocks <= iter.size) {
			iter.size -= blocks;
			break;
		}
	}

	if (level < 0) {
		drm_dbg_kms(&i915->drm,
			    "Requested display configuration exceeds system DDB limitations");
		drm_dbg_kms(&i915->drm, "minimum required %d/%d\n",
			    blocks, iter.size);
		return -EINVAL;
	}

	/* avoid the WARN later when we don't allocate any extra DDB */
	if (iter.data_rate == 0)
		iter.size = 0;

	/*
	 * Grant each plane the blocks it requires at the highest achievable
	 * watermark level, plus an extra share of the leftover blocks
	 * proportional to its relative data rate.
	 */
	for_each_plane_id_on_crtc(crtc, plane_id) {
		struct skl_ddb_entry *ddb =
			&crtc_state->wm.skl.plane_ddb[plane_id];
		struct skl_ddb_entry *ddb_y =
			&crtc_state->wm.skl.plane_ddb_y[plane_id];
		const struct skl_plane_wm *wm =
			&crtc_state->wm.skl.optimal.planes[plane_id];

		if (plane_id == PLANE_CURSOR && DISPLAY_VER(i915) < 20)
			continue;

		if (DISPLAY_VER(i915) < 11 &&
		    crtc_state->nv12_planes & BIT(plane_id)) {
			skl_allocate_plane_ddb(&iter, ddb_y, &wm->wm[level],
					       crtc_state->rel_data_rate_y[plane_id]);
			skl_allocate_plane_ddb(&iter, ddb, &wm->uv_wm[level],
					       crtc_state->rel_data_rate[plane_id]);
		} else {
			skl_allocate_plane_ddb(&iter, ddb, &wm->wm[level],
					       crtc_state->rel_data_rate[plane_id]);
		}
	}
	drm_WARN_ON(&i915->drm, iter.size != 0 || iter.data_rate != 0);

	/*
	 * When we calculated watermark values we didn't know how high
	 * of a level we'd actually be able to hit, so we just marked
	 * all levels as "enabled."  Go back now and disable the ones
	 * that aren't actually possible.
	 */
	for (level++; level < i915->display.wm.num_levels; level++) {
		for_each_plane_id_on_crtc(crtc, plane_id) {
			const struct skl_ddb_entry *ddb =
				&crtc_state->wm.skl.plane_ddb[plane_id];
			const struct skl_ddb_entry *ddb_y =
				&crtc_state->wm.skl.plane_ddb_y[plane_id];
			struct skl_plane_wm *wm =
				&crtc_state->wm.skl.optimal.planes[plane_id];

			if (DISPLAY_VER(i915) < 11 &&
			    crtc_state->nv12_planes & BIT(plane_id))
				skl_check_nv12_wm_level(&wm->wm[level],
							&wm->uv_wm[level],
							ddb_y, ddb);
			else
				skl_check_wm_level(&wm->wm[level], ddb);

			if (skl_need_wm_copy_wa(i915, level, wm)) {
				wm->wm[level].blocks = wm->wm[level - 1].blocks;
				wm->wm[level].lines = wm->wm[level - 1].lines;
				wm->wm[level].ignore_lines = wm->wm[level - 1].ignore_lines;
			}
		}
	}

	/*
	 * Go back and disable the transition and SAGV watermarks
	 * if it turns out we don't have enough DDB blocks for them.
	 */
	for_each_plane_id_on_crtc(crtc, plane_id) {
		const struct skl_ddb_entry *ddb =
			&crtc_state->wm.skl.plane_ddb[plane_id];
		const struct skl_ddb_entry *ddb_y =
			&crtc_state->wm.skl.plane_ddb_y[plane_id];
		struct skl_plane_wm *wm =
			&crtc_state->wm.skl.optimal.planes[plane_id];

		if (DISPLAY_VER(i915) < 11 &&
		    crtc_state->nv12_planes & BIT(plane_id)) {
			skl_check_wm_level(&wm->trans_wm, ddb_y);
		} else {
			WARN_ON(skl_ddb_entry_size(ddb_y));

			skl_check_wm_level(&wm->trans_wm, ddb);
		}

		skl_check_wm_level(&wm->sagv.wm0, ddb);
		skl_check_wm_level(&wm->sagv.trans_wm, ddb);
	}

	return 0;
}

static void
skl_ddb_entry_for_slices(struct drm_i915_private *i915, u8 slice_mask,
			 struct skl_ddb_entry *ddb)
{
	int slice_size = intel_dbuf_slice_size(i915);

	if (!slice_mask) {
		ddb->start = 0;
		ddb->end = 0;
		return;
	}

	ddb->start = (ffs(slice_mask) - 1) * slice_size;
	ddb->end = fls(slice_mask) * slice_size;

	WARN_ON(ddb->start >= ddb->end);
	WARN_ON(ddb->end > DISPLAY_INFO(i915)->dbuf.size);
}

static unsigned int intel_crtc_ddb_weight(const struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;
	int hdisplay, vdisplay;

	if (!crtc_state->hw.active)
		return 0;

	/*
	 * Watermark/ddb requirement highly depends upon width of the
	 * framebuffer, So instead of allocating DDB equally among pipes
	 * distribute DDB based on resolution/width of the display.
	 */
	drm_mode_get_hv_timing(pipe_mode, &hdisplay, &vdisplay);

	return hdisplay;
}

static int
skl_crtc_allocate_ddb(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	unsigned int weight_total, weight_start, weight_end;
	const struct intel_dbuf_state *old_dbuf_state =
		intel_atomic_get_old_dbuf_state(state);
	struct intel_dbuf_state *new_dbuf_state =
		intel_atomic_get_new_dbuf_state(state);
	struct intel_crtc_state *crtc_state;
	struct skl_ddb_entry ddb_slices;
	enum pipe pipe = crtc->pipe;
	unsigned int mbus_offset = 0;
	u32 ddb_range_size;
	u32 dbuf_slice_mask;
	u32 start, end;
	int ret;

	if (new_dbuf_state->weight[pipe] == 0) {
		skl_ddb_entry_init(&new_dbuf_state->ddb[pipe], 0, 0);
		goto out;
	}

	dbuf_slice_mask = new_dbuf_state->slices[pipe];

	skl_ddb_entry_for_slices(i915, dbuf_slice_mask, &ddb_slices);
	mbus_offset = mbus_ddb_offset(i915, dbuf_slice_mask);
	ddb_range_size = skl_ddb_entry_size(&ddb_slices);

	intel_crtc_dbuf_weights(new_dbuf_state, pipe,
				&weight_start, &weight_end, &weight_total);

	start = ddb_range_size * weight_start / weight_total;
	end = ddb_range_size * weight_end / weight_total;

	skl_ddb_entry_init(&new_dbuf_state->ddb[pipe],
			   ddb_slices.start - mbus_offset + start,
			   ddb_slices.start - mbus_offset + end);

out:
	if (old_dbuf_state->slices[pipe] == new_dbuf_state->slices[pipe] &&
	    skl_ddb_entry_equal(&old_dbuf_state->ddb[pipe],
				&new_dbuf_state->ddb[pipe]))
		return 0;

	ret = intel_atomic_lock_global_state(&new_dbuf_state->base);
	if (ret)
		return ret;

	crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	/*
	 * Used for checking overlaps, so we need absolute
	 * offsets instead of MBUS relative offsets.
	 */
	crtc_state->wm.skl.ddb.start = mbus_offset + new_dbuf_state->ddb[pipe].start;
	crtc_state->wm.skl.ddb.end = mbus_offset + new_dbuf_state->ddb[pipe].end;

	drm_dbg_kms(&i915->drm,
		    "[CRTC:%d:%s] dbuf slices 0x%x -> 0x%x, ddb (%d - %d) -> (%d - %d), active pipes 0x%x -> 0x%x\n",
		    crtc->base.base.id, crtc->base.name,
		    old_dbuf_state->slices[pipe], new_dbuf_state->slices[pipe],
		    old_dbuf_state->ddb[pipe].start, old_dbuf_state->ddb[pipe].end,
		    new_dbuf_state->ddb[pipe].start, new_dbuf_state->ddb[pipe].end,
		    old_dbuf_state->active_pipes, new_dbuf_state->active_pipes);

	return 0;
}

static const struct skl_wm_level *
skl_plane_wm_level(const struct skl_pipe_wm *pipe_wm,
		   enum plane_id plane_id,
		   int level)
{
	const struct skl_plane_wm *wm = &pipe_wm->planes[plane_id];

	if (level == 0 && pipe_wm->use_sagv_wm)
		return &wm->sagv.wm0;

	return &wm->wm[level];
}

static const struct skl_wm_level *
skl_plane_trans_wm(const struct skl_pipe_wm *pipe_wm,
		   enum plane_id plane_id)
{
	const struct skl_plane_wm *wm = &pipe_wm->planes[plane_id];

	if (pipe_wm->use_sagv_wm)
		return &wm->sagv.trans_wm;

	return &wm->trans_wm;
}

static void skl_write_wm_level(struct drm_i915_private *i915,
			       i915_reg_t reg,
			       const struct skl_wm_level *level)
{
	u32 val = 0;

	if (level->enable)
		val |= PLANE_WM_EN;
	if (level->ignore_lines)
		val |= PLANE_WM_IGNORE_LINES;
	val |= REG_FIELD_PREP(PLANE_WM_BLOCKS_MASK, level->blocks);
	val |= REG_FIELD_PREP(PLANE_WM_LINES_MASK, level->lines);

	intel_de_write_fw(i915, reg, val);
}

static void skl_ddb_entry_write(struct drm_i915_private *i915,
				i915_reg_t reg,
				const struct skl_ddb_entry *entry)
{
	if (entry->end)
		intel_de_write_fw(i915, reg,
				  PLANE_BUF_END(entry->end - 1) |
				  PLANE_BUF_START(entry->start));
	else
		intel_de_write_fw(i915, reg, 0);
}

void skl_write_plane_wm(struct intel_plane *plane,
			const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	const struct skl_pipe_wm *pipe_wm = &crtc_state->wm.skl.optimal;
	const struct skl_ddb_entry *ddb =
		&crtc_state->wm.skl.plane_ddb[plane_id];
	const struct skl_ddb_entry *ddb_y =
		&crtc_state->wm.skl.plane_ddb_y[plane_id];
	int level;

	for (level = 0; level < i915->display.wm.num_levels; level++)
		skl_write_wm_level(i915, PLANE_WM(pipe, plane_id, level),
				   skl_plane_wm_level(pipe_wm, plane_id, level));

	skl_write_wm_level(i915, PLANE_WM_TRANS(pipe, plane_id),
			   skl_plane_trans_wm(pipe_wm, plane_id));

	if (HAS_HW_SAGV_WM(i915)) {
		const struct skl_plane_wm *wm = &pipe_wm->planes[plane_id];

		skl_write_wm_level(i915, PLANE_WM_SAGV(pipe, plane_id),
				   &wm->sagv.wm0);
		skl_write_wm_level(i915, PLANE_WM_SAGV_TRANS(pipe, plane_id),
				   &wm->sagv.trans_wm);
	}

	skl_ddb_entry_write(i915,
			    PLANE_BUF_CFG(pipe, plane_id), ddb);

	if (DISPLAY_VER(i915) < 11)
		skl_ddb_entry_write(i915,
				    PLANE_NV12_BUF_CFG(pipe, plane_id), ddb_y);
}

static void intel_dbuf_mdclk_cdclk_ratio_update(struct drm_i915_private *i915,
						u8 ratio,
						bool joined_mbus)
{
	enum dbuf_slice slice;

	if (joined_mbus)
		ratio *= 2;

	for_each_dbuf_slice(i915, slice)
		intel_de_rmw(i915, DBUF_CTL_S(slice),
			     DBUF_MIN_TRACKER_STATE_SERVICE_MASK,
			     DBUF_MIN_TRACKER_STATE_SERVICE(ratio - 1));
}

/*
 * Configure MBUS_CTL and all DBUF_CTL_S of each slice to join_mbus state before
 * update the request state of all DBUS slices.
 */
static void update_mbus_pre_enable(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	u32 mbus_ctl;
	const struct intel_dbuf_state *dbuf_state =
		intel_atomic_get_new_dbuf_state(state);

	if (!HAS_MBUS_JOINING(i915))
		return;

	/*
	 * TODO: Implement vblank synchronized MBUS joining changes.
	 * Must be properly coordinated with dbuf reprogramming.
	 */
	if (dbuf_state->joined_mbus)
		mbus_ctl = MBUS_HASHING_MODE_1x4 | MBUS_JOIN |
			MBUS_JOIN_PIPE_SELECT_NONE;
	else
		mbus_ctl = MBUS_HASHING_MODE_2x2 |
			MBUS_JOIN_PIPE_SELECT_NONE;

	intel_de_rmw(i915, MBUS_CTL,
		     MBUS_HASHING_MODE_MASK | MBUS_JOIN |
		     MBUS_JOIN_PIPE_SELECT_MASK, mbus_ctl);

	intel_dbuf_mdclk_cdclk_ratio_update(i915, 2, dbuf_state->joined_mbus);
}

void intel_dbuf_pre_plane_update(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_dbuf_state *new_dbuf_state =
		intel_atomic_get_new_dbuf_state(state);
	const struct intel_dbuf_state *old_dbuf_state =
		intel_atomic_get_old_dbuf_state(state);

	if (!new_dbuf_state ||
	    (new_dbuf_state->enabled_slices == old_dbuf_state->enabled_slices &&
	     new_dbuf_state->joined_mbus == old_dbuf_state->joined_mbus))
		return;

	WARN_ON(!new_dbuf_state->base.changed);

	update_mbus_pre_enable(state);
	gen9_dbuf_slices_update(i915,
				old_dbuf_state->enabled_slices |
				new_dbuf_state->enabled_slices);
}

void intel_dbuf_post_plane_update(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_dbuf_state *new_dbuf_state =
		intel_atomic_get_new_dbuf_state(state);
	const struct intel_dbuf_state *old_dbuf_state =
		intel_atomic_get_old_dbuf_state(state);

	if (!new_dbuf_state ||
	    (new_dbuf_state->enabled_slices == old_dbuf_state->enabled_slices &&
	     new_dbuf_state->joined_mbus == old_dbuf_state->joined_mbus))
		return;

	WARN_ON(!new_dbuf_state->base.changed);

	gen9_dbuf_slices_update(i915,
				new_dbuf_state->enabled_slices);
}

static bool xelpdp_is_only_pipe_per_dbuf_bank(enum pipe pipe, u8 active_pipes)
{
	switch (pipe) {
	case PIPE_A:
		return !(active_pipes & BIT(PIPE_D));
	case PIPE_D:
		return !(active_pipes & BIT(PIPE_A));
	case PIPE_B:
		return !(active_pipes & BIT(PIPE_C));
	case PIPE_C:
		return !(active_pipes & BIT(PIPE_B));
	default: /* to suppress compiler warning */
		MISSING_CASE(pipe);
		break;
	}

	return false;
}

void intel_mbus_dbox_update(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_dbuf_state *new_dbuf_state, *old_dbuf_state;
	const struct intel_crtc *crtc;
	u32 val = 0;

	if (DISPLAY_VER(i915) < 11)
		return;

	new_dbuf_state = intel_atomic_get_new_dbuf_state(state);
	old_dbuf_state = intel_atomic_get_old_dbuf_state(state);
	if (!new_dbuf_state ||
	    (new_dbuf_state->joined_mbus == old_dbuf_state->joined_mbus &&
	     new_dbuf_state->active_pipes == old_dbuf_state->active_pipes))
		return;

	if (DISPLAY_VER(i915) >= 14)
		val |= MBUS_DBOX_I_CREDIT(2);

	if (DISPLAY_VER(i915) >= 12) {
		val |= MBUS_DBOX_B2B_TRANSACTIONS_MAX(16);
		val |= MBUS_DBOX_B2B_TRANSACTIONS_DELAY(1);
		val |= MBUS_DBOX_REGULATE_B2B_TRANSACTIONS_EN;
	}

	if (DISPLAY_VER(i915) >= 14)
		val |= new_dbuf_state->joined_mbus ? MBUS_DBOX_A_CREDIT(12) :
						     MBUS_DBOX_A_CREDIT(8);
	else if (IS_ALDERLAKE_P(i915))
		/* Wa_22010947358:adl-p */
		val |= new_dbuf_state->joined_mbus ? MBUS_DBOX_A_CREDIT(6) :
						     MBUS_DBOX_A_CREDIT(4);
	else
		val |= MBUS_DBOX_A_CREDIT(2);

	if (DISPLAY_VER(i915) >= 14) {
		val |= MBUS_DBOX_B_CREDIT(0xA);
	} else if (IS_ALDERLAKE_P(i915)) {
		val |= MBUS_DBOX_BW_CREDIT(2);
		val |= MBUS_DBOX_B_CREDIT(8);
	} else if (DISPLAY_VER(i915) >= 12) {
		val |= MBUS_DBOX_BW_CREDIT(2);
		val |= MBUS_DBOX_B_CREDIT(12);
	} else {
		val |= MBUS_DBOX_BW_CREDIT(1);
		val |= MBUS_DBOX_B_CREDIT(8);
	}

	for_each_intel_crtc_in_pipe_mask(&i915->drm, crtc, new_dbuf_state->active_pipes) {
		u32 pipe_val = val;

		if (DISPLAY_VER(i915) >= 14) {
			if (xelpdp_is_only_pipe_per_dbuf_bank(crtc->pipe,
							      new_dbuf_state->active_pipes))
				pipe_val |= MBUS_DBOX_BW_8CREDITS_MTL;
			else
				pipe_val |= MBUS_DBOX_BW_4CREDITS_MTL;
		}

		intel_de_write(i915, PIPE_MBUS_DBOX_CTL(crtc->pipe), pipe_val);
	}
}

static void skl_wm_level_from_reg_val(u32 val, struct skl_wm_level *level)
{
	level->enable = val & PLANE_WM_EN;
	level->ignore_lines = val & PLANE_WM_IGNORE_LINES;
	level->blocks = REG_FIELD_GET(PLANE_WM_BLOCKS_MASK, val);
	level->lines = REG_FIELD_GET(PLANE_WM_LINES_MASK, val);
}

static void skl_pipe_wm_get_hw_state(struct intel_crtc *crtc,
				     struct skl_pipe_wm *out)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	enum plane_id plane_id;
	int level;
	u32 val;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		struct skl_plane_wm *wm = &out->planes[plane_id];

		for (level = 0; level < i915->display.wm.num_levels; level++) {
			if (plane_id != PLANE_CURSOR)
				val = intel_de_read(i915, PLANE_WM(pipe, plane_id, level));
			else
				val = intel_de_read(i915, CUR_WM(pipe, level));

			skl_wm_level_from_reg_val(val, &wm->wm[level]);
		}

		if (plane_id != PLANE_CURSOR)
			val = intel_de_read(i915, PLANE_WM_TRANS(pipe, plane_id));
		else
			val = intel_de_read(i915, CUR_WM_TRANS(pipe));

		skl_wm_level_from_reg_val(val, &wm->trans_wm);

		if (HAS_HW_SAGV_WM(i915)) {
			if (plane_id != PLANE_CURSOR)
				val = intel_de_read(i915, PLANE_WM_SAGV(pipe, plane_id));
			else
				val = intel_de_read(i915, CUR_WM_SAGV(pipe));

			skl_wm_level_from_reg_val(val, &wm->sagv.wm0);

			if (plane_id != PLANE_CURSOR)
				val = intel_de_read(i915, PLANE_WM_SAGV_TRANS(pipe, plane_id));
			else
				val = intel_de_read(i915, CUR_WM_SAGV_TRANS(pipe));

			skl_wm_level_from_reg_val(val, &wm->sagv.trans_wm);
		} else if (DISPLAY_VER(i915) >= 12) {
			wm->sagv.wm0 = wm->wm[0];
			wm->sagv.trans_wm = wm->trans_wm;
		}
	}
}

static void skl_pipe_ddb_get_hw_state(struct intel_crtc *crtc,
				      struct skl_ddb_entry *ddb,
				      struct skl_ddb_entry *ddb_y)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum intel_display_power_domain power_domain;
	enum pipe pipe = crtc->pipe;
	intel_wakeref_t wakeref;
	enum plane_id plane_id;

	power_domain = POWER_DOMAIN_PIPE(pipe);
	wakeref = intel_display_power_get_if_enabled(i915, power_domain);
	if (!wakeref)
		return;

	for_each_plane_id_on_crtc(crtc, plane_id)
		skl_ddb_get_hw_plane_state(i915, pipe,
					   plane_id,
					   &ddb[plane_id],
					   &ddb_y[plane_id]);

	intel_display_power_put(i915, power_domain, wakeref);
}

static void
skl_ddb_get_hw_plane_state(struct drm_i915_private *i915,
			   const enum pipe pipe,
			   const enum plane_id plane_id,
			   struct skl_ddb_entry *ddb,
			   struct skl_ddb_entry *ddb_y)
{
	u32 val;

	/* Cursor doesn't support NV12/planar, so no extra calculation needed */
	if (plane_id == PLANE_CURSOR) {
		val = intel_de_read(i915, CUR_BUF_CFG(pipe));
		skl_ddb_entry_init_from_hw(ddb, val);
		return;
	}

	val = intel_de_read(i915, PLANE_BUF_CFG(pipe, plane_id));
	skl_ddb_entry_init_from_hw(ddb, val);

	if (DISPLAY_VER(i915) >= 11)
		return;

	val = intel_de_read(i915, PLANE_NV12_BUF_CFG(pipe, plane_id));
	skl_ddb_entry_init_from_hw(ddb_y, val);
}

static void skl_ddb_entry_union(struct skl_ddb_entry *a,
				const struct skl_ddb_entry *b)
{
	if (a->end && b->end) {
		a->start = min(a->start, b->start);
		a->end = max(a->end, b->end);
	} else if (b->end) {
		a->start = b->start;
		a->end = b->end;
	}
}

static void skl_wm_get_hw_state(struct drm_i915_private *i915)
{
	struct intel_dbuf_state *dbuf_state =
		to_intel_dbuf_state(i915->display.dbuf.obj.state);
	struct intel_crtc *crtc;

	if (HAS_MBUS_JOINING(i915))
		dbuf_state->joined_mbus = intel_de_read(i915, MBUS_CTL) & MBUS_JOIN;

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		enum pipe pipe = crtc->pipe;
		unsigned int mbus_offset;
		enum plane_id plane_id;
		u8 slices;

		memset(&crtc_state->wm.skl.optimal, 0,
		       sizeof(crtc_state->wm.skl.optimal));
		if (crtc_state->hw.active)
			skl_pipe_wm_get_hw_state(crtc, &crtc_state->wm.skl.optimal);
		crtc_state->wm.skl.raw = crtc_state->wm.skl.optimal;

		memset(&dbuf_state->ddb[pipe], 0, sizeof(dbuf_state->ddb[pipe]));

		for_each_plane_id_on_crtc(crtc, plane_id) {
			struct skl_ddb_entry *ddb =
				&crtc_state->wm.skl.plane_ddb[plane_id];
			struct skl_ddb_entry *ddb_y =
				&crtc_state->wm.skl.plane_ddb_y[plane_id];

			if (!crtc_state->hw.active)
				continue;

			skl_ddb_get_hw_plane_state(i915, crtc->pipe,
						   plane_id, ddb, ddb_y);

			skl_ddb_entry_union(&dbuf_state->ddb[pipe], ddb);
			skl_ddb_entry_union(&dbuf_state->ddb[pipe], ddb_y);
		}

		dbuf_state->weight[pipe] = intel_crtc_ddb_weight(crtc_state);

		/*
		 * Used for checking overlaps, so we need absolute
		 * offsets instead of MBUS relative offsets.
		 */
		slices = skl_compute_dbuf_slices(crtc, dbuf_state->active_pipes,
						 dbuf_state->joined_mbus);
		mbus_offset = mbus_ddb_offset(i915, slices);
		crtc_state->wm.skl.ddb.start = mbus_offset + dbuf_state->ddb[pipe].start;
		crtc_state->wm.skl.ddb.end = mbus_offset + dbuf_state->ddb[pipe].end;

		/* The slices actually used by the planes on the pipe */
		dbuf_state->slices[pipe] =
			skl_ddb_dbuf_slice_mask(i915, &crtc_state->wm.skl.ddb);

		drm_dbg_kms(&i915->drm,
			    "[CRTC:%d:%s] dbuf slices 0x%x, ddb (%d - %d), active pipes 0x%x, mbus joined: %s\n",
			    crtc->base.base.id, crtc->base.name,
			    dbuf_state->slices[pipe], dbuf_state->ddb[pipe].start,
			    dbuf_state->ddb[pipe].end, dbuf_state->active_pipes,
			    str_yes_no(dbuf_state->joined_mbus));
	}

	dbuf_state->enabled_slices = i915->display.dbuf.enabled_slices;
}

static bool skl_dbuf_is_misconfigured(struct drm_i915_private *i915)
{
	const struct intel_dbuf_state *dbuf_state =
		to_intel_dbuf_state(i915->display.dbuf.obj.state);
	struct skl_ddb_entry entries[I915_MAX_PIPES] = {};
	struct intel_crtc *crtc;

	for_each_intel_crtc(&i915->drm, crtc) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		entries[crtc->pipe] = crtc_state->wm.skl.ddb;
	}

	for_each_intel_crtc(&i915->drm, crtc) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		u8 slices;

		slices = skl_compute_dbuf_slices(crtc, dbuf_state->active_pipes,
						 dbuf_state->joined_mbus);
		if (dbuf_state->slices[crtc->pipe] & ~slices)
			return true;

		if (skl_ddb_allocation_overlaps(&crtc_state->wm.skl.ddb, entries,
						I915_MAX_PIPES, crtc->pipe))
			return true;
	}

	return false;
}

static void skl_wm_sanitize(struct drm_i915_private *i915)
{
	struct intel_crtc *crtc;

	/*
	 * On TGL/RKL (at least) the BIOS likes to assign the planes
	 * to the wrong DBUF slices. This will cause an infinite loop
	 * in skl_commit_modeset_enables() as it can't find a way to
	 * transition between the old bogus DBUF layout to the new
	 * proper DBUF layout without DBUF allocation overlaps between
	 * the planes (which cannot be allowed or else the hardware
	 * may hang). If we detect a bogus DBUF layout just turn off
	 * all the planes so that skl_commit_modeset_enables() can
	 * simply ignore them.
	 */
	if (!skl_dbuf_is_misconfigured(i915))
		return;

	drm_dbg_kms(&i915->drm, "BIOS has misprogrammed the DBUF, disabling all planes\n");

	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_plane *plane = to_intel_plane(crtc->base.primary);
		const struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		if (plane_state->uapi.visible)
			intel_plane_disable_noatomic(crtc, plane);

		drm_WARN_ON(&i915->drm, crtc_state->active_planes != 0);

		memset(&crtc_state->wm.skl.ddb, 0, sizeof(crtc_state->wm.skl.ddb));
	}
}

static void skl_wm_get_hw_state_and_sanitize(struct drm_i915_private *i915)
{
	skl_wm_get_hw_state(i915);
	skl_wm_sanitize(i915);
}

static void skl_ddb_entry_init_from_hw(struct skl_ddb_entry *entry, u32 reg)
{
	skl_ddb_entry_init(entry,
			   REG_FIELD_GET(PLANE_BUF_START_MASK, reg),
			   REG_FIELD_GET(PLANE_BUF_END_MASK, reg));
	if (entry->end)
		entry->end++;
}

u32 skl_ddb_dbuf_slice_mask(struct drm_i915_private *i915,
			    const struct skl_ddb_entry *entry)
{
	int slice_size = intel_dbuf_slice_size(i915);
	enum dbuf_slice start_slice, end_slice;
	u8 slice_mask = 0;

	if (!skl_ddb_entry_size(entry))
		return 0;

	start_slice = entry->start / slice_size;
	end_slice = (entry->end - 1) / slice_size;

	/*
	 * Per plane DDB entry can in a really worst case be on multiple slices
	 * but single entry is anyway contigious.
	 */
	while (start_slice <= end_slice) {
		slice_mask |= BIT(start_slice);
		start_slice++;
	}

	return slice_mask;
}

static bool skl_ddb_entries_overlap(const struct skl_ddb_entry *a,
				    const struct skl_ddb_entry *b)
{
	return a->start < b->end && b->start < a->end;
}

bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb,
				 const struct skl_ddb_entry *entries,
				 int num_entries, int ignore_idx)
{
	int i;

	for (i = 0; i < num_entries; i++) {
		if (i != ignore_idx &&
		    skl_ddb_entries_overlap(ddb, &entries[i]))
			return true;
	}

	return false;
}


#include "parity_wm_glue.inc"
