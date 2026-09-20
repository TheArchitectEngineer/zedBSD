/*
 * WS031 Linux-parity — environment of the watermark / DDB code (skl_watermark_port.c and its helpers) on top
 * of the other LCD compat headers.  zedBSD project code.
 *
 * Scope: ONE pipe with ONE visible plane (the primary; no cursor plane is created in this path).  The
 * reference's iterators over "the planes of this crtc" / "the crtcs of this state" therefore visit that one
 * plane / crtc, and the global DBUF state is the one object the modeset carries.
 */
#ifndef PARITY_LCD_WM_COMPAT_H
#define PARITY_LCD_WM_COMPAT_H
#include "lcd_i915_fixed.h"           /* reference, extracted: uint_fixed_16_16_t arithmetic */
#include "lcd_mreg_wm.h"              /* reference, extracted macros: PLANE_WM, PLANE_BUF_CFG, ... */

#ifndef UINT_MAX
#define UINT_MAX 0xffffffffu
#endif
#define ffs(x) __builtin_ffs((int)(x))
#define fls(x) ((x) ? 32 - __builtin_clz((unsigned int)(x)) : 0)
#ifndef hweight8
#define hweight8(x) ((unsigned int)__builtin_popcount((unsigned int)(x) & 0xffu))
#endif
#define max_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a > _b ? _a : _b; })
#define DIV64_U64_ROUND_UP(n, d) ((u64)(((u64)(n) + (u64)(d) - 1u) / (u64)(d)))
#define div64_u64(n, d) ((u64)(n) / (u64)(d))
#define IS_ERR(p) (0)
#define PTR_ERR(p) (0)
#define WARN_ON_ONCE(c) WARN_ON(c)
#define IS_KABYLAKE(i915) 0
#define IS_COFFEELAKE(i915) 0
#define IS_COMETLAKE(i915) 0
#define IS_DGFX(i915) 0

/* drm_format_info(): only the cursor's ARGB8888 is looked up (skl_cursor_allocation) */
static inline const struct drm_format_info *drm_format_info(u32 format)
{
	static const struct drm_format_info argb8888 = { .format = DRM_FORMAT_ARGB8888, .num_planes = 1, .cpp = { 4, 0, 0, 0 }, .has_alpha = true };
	return format == DRM_FORMAT_ARGB8888 ? &argb8888 : 0;
}

/* the DBUF slice tables of other platforms are not reached (display version 13, not DG2) */
#define dg2_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)
#define tgl_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)
#define icl_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)

/* intel_mbus_dbox_update() walks the crtcs of the active pipes: here the one crtc of the modeset (lcd_modeset_compat.h
 * has the big-joiner form of this iterator, which never runs) */
#undef for_each_intel_crtc_in_pipe_mask
#define for_each_intel_crtc_in_pipe_mask(dev, crtc, mask) \
	for ((crtc) = to_intel_crtc(parity_lcd_wm->crtc_state->uapi.crtc); (crtc) != 0; (crtc) = 0) for_each_if((mask) & BIT((crtc)->pipe))

/* gen9_dbuf_slices_update(): the body the normal initialisation already uses (it owns the enabled-slices state and the
 * power-domains lock); reached through the ops so that the model and the real device see the same request */
#define gen9_dbuf_slices_update(i915, req_slices) (i915)->emit->dbuf_slices_update((i915)->emit->ctx, (unsigned)(req_slices))

/* [fixed] linear framebuffers only */
#define intel_fb_is_ccs_modifier(modifier) (0)
#define intel_fb_is_tiled_modifier(modifier) ((modifier) != DRM_FORMAT_MOD_LINEAR)

/* ---- the global DBUF state and the atomic accessors, reduced to the modeset's one object ---- */
struct intel_atomic_state;
struct intel_global_state { struct intel_atomic_state *state; bool changed; };
#define to_intel_atomic_state(s) (s)
#include "lcd_dbuf_types.h"           /* reference, extracted: struct intel_dbuf_state */
struct parity_lcd_wm_ctx { struct intel_dbuf_state old_dbuf, new_dbuf; struct intel_crtc_state *crtc_state; };
extern struct parity_lcd_wm_ctx *parity_lcd_wm;
#define intel_atomic_get_new_dbuf_state(state) (&parity_lcd_wm->new_dbuf)
#define intel_atomic_get_old_dbuf_state(state) (&parity_lcd_wm->old_dbuf)
#define intel_atomic_get_crtc_state(state, crtc) (parity_lcd_wm->crtc_state)
#define intel_atomic_lock_global_state(global_state) (0)
#define to_intel_plane_state(x) ((struct intel_plane_state *)(x))

/* ---- iterators over the one plane / crtc ---- */
#define intel_atomic_crtc_state_for_each_plane_state(plane, plane_state, crtc_state) \
	for ((plane) = (crtc_state)->only_plane, (plane_state) = (crtc_state)->only_plane_state; (plane) != 0; (plane) = 0)
#define for_each_intel_plane_on_crtc(dev, crtc, plane) for ((plane) = parity_lcd_wm->crtc_state->only_plane; (plane) != 0; (plane) = 0)
#define for_each_new_intel_plane_in_state(state, plane, new_plane_state, i) \
	for ((i) = 0, (plane) = parity_lcd_wm->crtc_state->only_plane, (new_plane_state) = (struct intel_plane_state *)parity_lcd_wm->crtc_state->only_plane_state; (i) < 1 && (plane) != 0; (i)++)
#define for_each_plane_id_on_crtc(crtc, p) for ((p) = PLANE_PRIMARY; (p) <= PLANE_PRIMARY; (p)++)   /* crtc->plane_ids_mask = the primary */
#define for_each_dbuf_slice(i915, slice) for ((slice) = DBUF_S1; (slice) < I915_MAX_DBUF_SLICES; (slice)++) for_each_if(DISPLAY_INFO(i915)->dbuf.slice_mask & BIT(slice))
#define for_each_dbuf_slice_in_mask(i915, slice, mask) for_each_dbuf_slice((i915), (slice)) for_each_if((mask) & BIT(slice))

/* prototypes of kept non-static reference functions called across the generated files */
unsigned int intel_plane_data_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state, int color_plane);
bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode, int usecs);
void drm_mode_get_hv_timing(const struct drm_display_mode *mode, int *hdisplay, int *vdisplay);
void skl_write_plane_wm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);

#endif /* PARITY_LCD_WM_COMPAT_H */
