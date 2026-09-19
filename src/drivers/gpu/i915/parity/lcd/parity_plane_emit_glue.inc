/*
 * WS031 Linux-parity — zedBSD glue at the end of skl_plane_port.c.
 *
 * Builds the plane / framebuffer state for ONE full-screen primary plane on a linear XRGB8888
 * buffer and runs the reference's writers:
 *   plane_state->ctl       = skl_plane_ctl()         as skl_plane_check() does
 *   plane_state->color_ctl = glk_plane_color_ctl()   as skl_plane_check() does (display version >= 10)
 *   icl_plane_update_noarm()   stride, position, size, keys, offset, aux distance, CUS, colour control, [watermarks]
 *   icl_plane_update_arm()     PLANE_CTL then PLANE_SURF (the surface write arms the update)
 * State as the atomic check leaves it for a plane that covers the pipe without scaling: src = the
 * whole framebuffer (16.16), dst = the same rectangle, rotation 0, opaque (alpha 0xffff), the DRM
 * default blend mode (premultiplied; irrelevant without an alpha channel), no colour key, BT.709 /
 * limited range defaults (unused for RGB), scaler_id -1.
 * Anything this slice does not cover is refused HERE, before any reference code runs.
 */
int parity_plane_emit(struct parity_lcd_emit *emit, int pipe, int plane_id, u32 fourcc, u64 modifier,
	u32 width, u32 height, u32 pitch, u32 surf_ggtt_offset)
{
	static struct drm_i915_private i915;
	static const struct drm_format_info xrgb8888 = {
		.format = DRM_FORMAT_XRGB8888, .num_planes = 1, .cpp = { 4, 0, 0, 0 }, .has_alpha = false, .is_yuv = false,
	};
	struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;
	struct intel_plane plane;
	static struct intel_plane_state plane_state;
	struct drm_framebuffer fb;

	if (fourcc != DRM_FORMAT_XRGB8888 || modifier != DRM_FORMAT_MOD_LINEAR)
		return -EINVAL;
	if (pipe < 0 || pipe > 3 || plane_id != PLANE_PRIMARY || width == 0u || height == 0u || width > 8192u ||
	    height > 8192u || pitch < width * 4u || (pitch % 64u) != 0u || (surf_ggtt_offset & 0xfffu) != 0u)
		return -EINVAL;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	memset(&plane, 0, sizeof(plane));
	memset(&plane_state, 0, sizeof(plane_state));
	memset(&fb, 0, sizeof(fb));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	plane.base.dev = &i915.drm;
	plane.id = (enum plane_id)plane_id;
	plane.pipe = (enum pipe)pipe;
	fb.dev = &i915.drm;
	fb.format = &xrgb8888;
	fb.modifier = modifier;
	plane_state.uapi.plane = &plane.base;
	plane_state.uapi.src.x2 = (int)(width << 16);
	plane_state.uapi.src.y2 = (int)(height << 16);
	plane_state.uapi.dst.x2 = (int)width;
	plane_state.uapi.dst.y2 = (int)height;
	plane_state.hw.fb = &fb;
	plane_state.hw.rotation = DRM_MODE_ROTATE_0;
	plane_state.hw.alpha = 0xffff;
	plane_state.hw.pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
	plane_state.hw.color_encoding = DRM_COLOR_YCBCR_BT709;
	plane_state.hw.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;
	plane_state.view.color_plane[0].scanout_stride = pitch;
	plane_state.view.color_plane[0].mapping_stride = pitch;
	plane_state.scaler_id = -1;
	plane_state.ggtt_offset = surf_ggtt_offset;

	plane_state.ctl = skl_plane_ctl(&crtc_state, &plane_state);
	plane_state.color_ctl = glk_plane_color_ctl(&crtc_state, &plane_state);

	icl_plane_update_noarm(&plane, &crtc_state, &plane_state);
	icl_plane_update_arm(&plane, &crtc_state, &plane_state);
	return 0;
}

#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

/*
 * The plane of the modeset object.  _prepare fills the framebuffer / plane state exactly as
 * parity_plane_emit() above does (same checks, same refusals) and computes ctl / color_ctl with the
 * reference's functions; _update runs icl_plane_update_noarm() then icl_plane_update_arm() (the
 * PLANE_SURF write arms the update: from then on the buffer may be scanned out); _disable runs
 * icl_plane_disable_arm().
 */
int parity_lcd_ms_plane_prepare(struct parity_lcd_modeset *ms, u32 fourcc, u64 modifier, u32 width, u32 height,
	u32 pitch, u32 surf_ggtt_offset)
{
	static const struct drm_format_info xrgb8888 = {
		.format = DRM_FORMAT_XRGB8888, .num_planes = 1, .cpp = { 4, 0, 0, 0 }, .has_alpha = false, .is_yuv = false,
	};

	if (fourcc != DRM_FORMAT_XRGB8888 || modifier != DRM_FORMAT_MOD_LINEAR)
		return -EINVAL;
	if (width == 0u || height == 0u || width > 8192u || height > 8192u || pitch < width * 4u || (pitch % 64u) != 0u ||
	    (surf_ggtt_offset & 0xfffu) != 0u)
		return -EINVAL;
	memset(&ms->plane, 0, sizeof(ms->plane));
	memset(&ms->plane_state, 0, sizeof(ms->plane_state));
	memset(&ms->fb, 0, sizeof(ms->fb));
	ms->plane.base.dev = &ms->i915.drm;
	ms->plane.id = PLANE_PRIMARY;
	ms->plane.pipe = ms->crtc.pipe;
	ms->fb.dev = &ms->i915.drm;
	ms->fb.format = &xrgb8888;
	ms->fb.modifier = modifier;
	ms->plane_state.uapi.plane = &ms->plane.base;
	ms->plane_state.uapi.src.x2 = (int)(width << 16);
	ms->plane_state.uapi.src.y2 = (int)(height << 16);
	ms->plane_state.uapi.dst.x2 = (int)width;
	ms->plane_state.uapi.dst.y2 = (int)height;
	ms->plane_state.hw.fb = &ms->fb;
	ms->plane_state.hw.rotation = DRM_MODE_ROTATE_0;
	ms->plane_state.hw.alpha = 0xffff;
	ms->plane_state.hw.pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
	ms->plane_state.hw.color_encoding = DRM_COLOR_YCBCR_BT709;
	ms->plane_state.hw.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;
	ms->plane_state.view.color_plane[0].scanout_stride = pitch;
	ms->plane_state.view.color_plane[0].mapping_stride = pitch;
	ms->plane_state.scaler_id = -1;
	ms->plane_state.ggtt_offset = surf_ggtt_offset;
	ms->plane_state.ctl = skl_plane_ctl(&ms->crtc_state, &ms->plane_state);
	ms->plane_state.color_ctl = glk_plane_color_ctl(&ms->crtc_state, &ms->plane_state);
	return 0;
}

void parity_lcd_ms_plane_update(struct parity_lcd_modeset *ms)
{
	icl_plane_update_noarm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	ms->plane_armed = 1;                    /* set BEFORE the arming write: from here the buffer is not ours alone */
	icl_plane_update_arm(&ms->plane, &ms->crtc_state, &ms->plane_state);
}

void parity_lcd_ms_plane_disable(struct parity_lcd_modeset *ms)
{
	icl_plane_disable_arm(&ms->plane, &ms->crtc_state);
}

/* intel_plane_calc_min_cdclk(): plane->min_cdclk is icl_plane_min_cdclk on display version 11+ (skl_universal_plane_create) */
void parity_lcd_ms_plane_min_cdclk(struct parity_lcd_modeset *ms)
{
	ms->crtc_state.min_cdclk[ms->plane.id] = icl_plane_min_cdclk(&ms->crtc_state, &ms->plane_state);
}

/* the reference update of a running crtc (intel_update_crtc) for its one primary plane: noarm -> intel_pipe_update_start
 * -> arm -> intel_pipe_update_end (intel_crtc_port.c) */
struct intel_atomic_state;
void intel_pipe_update_start(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_pipe_update_end(struct intel_atomic_state *state, struct intel_crtc *crtc);
extern int parity_lcd_flip_pipe;

void parity_lcd_ms_plane_update_flip(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	parity_lcd_flip_pipe = ms->crtc.pipe;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;         /* same timings: old and new crtc state agree */
	ms->state.old_crtc_state = &ms->crtc_state;
	ms->crtc_state.uapi.mode_changed = false;       /* an update of a running crtc, not a modeset */
	ms->crtc_state.uapi.event = &ms->flip_event;    /* the completion the update arms */
	icl_plane_update_noarm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	intel_pipe_update_start(&ms->state, &ms->crtc);
	icl_plane_update_arm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	intel_pipe_update_end(&ms->state, &ms->crtc);
	ms->crtc_state.uapi.event = 0;
}
