/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The watermark layer of the modeset environment.
 *
 * The Linux watermark and DDB text (skl_watermark and its helpers) is
 * compiled against this header, which is layered on modeset-internal.h.
 * Its scope is one pipe with one visible plane (the primary; no cursor
 * plane is created in this path): the Linux walks over "the planes of this
 * crtc" and "the crtcs of this state" visit that one plane and crtc, and
 * the global DBUF state is the one object the modeset carries.
 *
 * The Linux text found that object -- the old and new DBUF states and the
 * crtc state being worked on -- through a file-scope pointer.  The explicit
 * forms here take it as an argument: a struct i915_lcd_wm_ctx, which is the
 * wm member of the modeset object (modeset-internal.h).
 *
 * Errors: as in modeset-internal.h; this layer adds no error numbers.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_WATERMARK_INTERNAL_H
#define DRIVERS_GPU_I915_DISPLAY_WATERMARK_INTERNAL_H

#include "internal.h"
#include "modeset-internal.h"

#ifndef I915_DISPLAY_WORLD_MODESET
#error "watermark-internal.h is layered on modeset-internal.h"
#endif

/* This translation unit also carries the watermark layer. */
#define I915_DISPLAY_WORLD_WATERMARK 1

/*
 * The Linux 16.16 fixed-point helpers.  Their bodies call WARN_ON(), whose
 * meaning differs between the environments, so the modeset meaning is
 * given to that name only while the helpers are defined.
 */
#define WARN_ON(cond) I915_LCD_WARN_ON(cond)
#include "../intel/fixed.h"
#undef WARN_ON

/* The watermark, DBUF and MBUS registers. */
#include "../intel/mreg.h"

/*
 * ==== Macros and constants ====
 */

/* 64-bit division, rounded up and truncated (linux/math64.h). */
#define DIV64_U64_ROUND_UP(n, d) ((u64)(((u64)(n) + (u64)(d) - 1u) / (u64)(d)))
#define div64_u64(n, d) ((u64)(n) / (u64)(d))

/* The error-pointer tests: no accessor of this path returns an error pointer. */
#define IS_ERR(p) (0)
#define PTR_ERR(p) (0)

/* A warning of the text, reported every time (there is no once-only state). */
#define WARN_ON_ONCE(c) I915_LCD_WARN_ON(c)

/* The platforms the watermark text asks about that this path never runs on. */
#define IS_KABYLAKE(i915) 0
#define IS_COFFEELAKE(i915) 0
#define IS_COMETLAKE(i915) 0
#define IS_DGFX(i915) 0

/* The DBUF slice tables of other platforms are not reached (display version 13, not DG2). */
#define dg2_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)
#define icl_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)

/*
 * The crtcs of the active pipes (the Linux for_each_intel_crtc_in_pipe_mask()
 * as intel_mbus_dbox_update() walks it): the one crtc of the modeset the
 * watermark context names, when its pipe is in the mask.  This is the form
 * that was in effect at the use in skl_watermark (intel_mbus_dbox_update);
 * the takeover layer has the registry walk under its own name.
 */
#define I915_WM_FOR_EACH_INTEL_CRTC_IN_PIPE_MASK(wm, crtc, mask) \
	for ((crtc) = to_intel_crtc((wm)->crtc_state->uapi.crtc); (crtc) != 0; (crtc) = 0) \
		for_each_if((mask) & BIT((crtc)->pipe))

/*
 * gen9_dbuf_slices_update(): the body the normal initialisation already
 * uses (it owns the enabled-slices state and the power-domains lock),
 * reached through the backend so the model and the device see the same
 * request.
 */
#define gen9_dbuf_slices_update(i915, req_slices) (i915)->emit->dbuf_slices_update((i915)->emit->ctx, (unsigned)(req_slices))

/* [fixed] linear framebuffers only. */
#define intel_fb_is_ccs_modifier(modifier) (0)
#define intel_fb_is_tiled_modifier(modifier) ((modifier) != FORMAT_MOD_LINEAR)

/* The drm and intel atomic states are one object in this path. */
#define to_intel_atomic_state(s) (s)

/* Locking a global state object: nothing to lock, one commit at a time. */
#define intel_atomic_lock_global_state(global_state) (0)

/* The planes of a crtc state: the one plane the modeset carries. */
#define intel_atomic_crtc_state_for_each_plane_state(plane, plane_state, crtc_state) \
	for ((plane) = (crtc_state)->only_plane, (plane_state) = (crtc_state)->only_plane_state; (plane) != 0; (plane) = 0)

/*
 * The planes on the crtc of the watermark context (the watermark text's
 * Linux for_each_intel_plane_on_crtc()).  Never in effect at a use: the
 * readout's registry walk (takeover-internal.h) was always defined first
 * and this form was skipped; carried for completeness only.
 */
#define I915_WM_FOR_EACH_INTEL_PLANE_ON_CRTC(wm, dev, crtc, plane) \
	for ((plane) = (wm)->crtc_state->only_plane; (plane) != 0; (plane) = 0)

/*
 * The new plane states of the commit (the Linux
 * for_each_new_intel_plane_in_state()): the one plane of the crtc state the
 * watermark context names.  The atomic state is not evaluated.
 */
#define I915_WM_FOR_EACH_NEW_INTEL_PLANE_IN_STATE(wm, state, plane, new_plane_state, i) \
	for ((i) = 0, (plane) = (wm)->crtc_state->only_plane, (new_plane_state) = (struct intel_plane_state *)(wm)->crtc_state->only_plane_state; \
	     (i) < 1 && (plane) != 0; \
	     (i)++)

/* The plane ids of a crtc: the primary (crtc->plane_ids_mask). */
#define for_each_plane_id_on_crtc(crtc, p) for ((p) = PLANE_PRIMARY; (p) <= PLANE_PRIMARY; (p)++)

/* The DBUF slices of the platform, and those of a mask. */
#define for_each_dbuf_slice(i915, slice) \
	for ((slice) = DBUF_S1; (slice) < I915_MAX_DBUF_SLICES; (slice)++) \
		for_each_if(DISPLAY_INFO(i915)->dbuf.slice_mask & BIT(slice))
#define for_each_dbuf_slice_in_mask(i915, slice, mask) \
	for_each_dbuf_slice((i915), (slice)) \
		for_each_if((mask) & BIT(slice))

/*
 * ==== Types ====
 */

/*
 * The state of the watermark layer: the file-scope variables of the
 * watermark translation unit.
 *
 * The display root holds a pointer to one of these, allocated by the owner
 * of the watermark path.  It is used only by the modeset path, which runs
 * on the display owner's thread one operation at a time; no lock protects
 * it.  A zeroed structure is the state at load time.
 */
struct i915_wm_world {
	/*
	 * The device's current global DBUF state: what the last commit
	 * published (enabled slices, MBUS joining, per-pipe DDB), the "old"
	 * state of the next commit.  Valid only while
	 * i915_lcd_dbuf_dev_valid is set.
	 */
	struct intel_dbuf_state i915_lcd_dbuf_dev;

	/*
	 * Nonzero once a commit has published i915_lcd_dbuf_dev; zero at load
	 * and after the state is forgotten, when the current-state query
	 * answers that there is none yet.
	 */
	int i915_lcd_dbuf_dev_valid;

	/*
	 * The watermark context the Linux text works on: the wm member of the
	 * modeset object being computed or committed (NULL before the first).
	 * Set by the watermark entry points before they call in; the explicit
	 * forms of this layer take the context as an argument instead, and the
	 * field lives until no caller reads it.
	 */
	struct i915_lcd_wm_ctx *i915_lcd_wm;

	/*
	 * The display this world belongs to, set when the world is created.
	 * The watermark text reaches the takeover registry through it.
	 */
	struct i915_display *display;
};

/*
 * ==== Inline helpers ====
 */

/*
 * The format description of a fourcc (the Linux drm_format_info()): only
 * the cursor's ARGB8888 is looked up (skl_cursor_allocation); any other
 * format answers NULL.
 */
static __inline const struct drm_format_info *
i915_drm_format_info(
	u32 format)
{
	static const struct drm_format_info argb8888 = {
		.format = FORMAT_ARGB8888,
		.num_planes = 1,
		.cpp = { 4, 0, 0, 0 },
		.has_alpha = true,
	};

	/* Only ARGB8888 is described. */
	if (format != FORMAT_ARGB8888)
		return NULL;

	/* Succeeded: reports the cursor format. */
	return &argb8888;
}

/* The DBUF state after the commit (the Linux intel_atomic_get_new_dbuf_state()). */
static __inline struct intel_dbuf_state *
i915_wm_intel_atomic_get_new_dbuf_state(
	struct i915_lcd_wm_ctx *wm)
{
	/* The new state is the one the watermark context carries. */
	return &wm->new_dbuf;
}

/* The DBUF state before the commit (the Linux intel_atomic_get_old_dbuf_state()). */
static __inline struct intel_dbuf_state *
i915_wm_intel_atomic_get_old_dbuf_state(
	struct i915_lcd_wm_ctx *wm)
{
	/* The old state is the one the watermark context carries. */
	return &wm->old_dbuf;
}

/*
 * The crtc state of a crtc in the commit (the watermark text's Linux
 * intel_atomic_get_crtc_state()): the crtc state of the watermark context.
 * Never in effect at a use: the readout's form (takeover-internal.h) was
 * always defined first; carried for completeness only.
 */
static __inline struct intel_crtc_state *
i915_wm_intel_atomic_get_crtc_state(
	struct i915_lcd_wm_ctx *wm)
{
	/* The one crtc state of the watermark context. */
	return wm->crtc_state;
}

#endif /* DRIVERS_GPU_I915_DISPLAY_WATERMARK_INTERNAL_H */
