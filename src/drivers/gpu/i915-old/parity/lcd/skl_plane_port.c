// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/skl_universal_plane.c
 * (sha256 a286317da480cb543d55dfb43558884f192a6f6e09de00066c1f8b07a14a8228) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: icl_hdr_plane_mask, icl_is_hdr_plane, skl_plane_stride_mult, skl_plane_stride, skl_plane_ctl_format,
 *    skl_plane_ctl_alpha, glk_plane_color_ctl_alpha, skl_plane_ctl_tiling, skl_plane_ctl_rotate, icl_plane_ctl_flip,
 *    adlp_plane_ctl_arb_slots, skl_plane_ctl_crtc, skl_plane_ctl, glk_plane_color_ctl_crtc, glk_plane_color_ctl,
 *    skl_surf_address, skl_plane_surf, skl_plane_aux_dist, skl_plane_keyval, skl_plane_keymsk,
 *    skl_plane_keymax, icl_plane_color_plane, icl_plane_update_sel_fetch_noarm, icl_plane_update_noarm, icl_plane_disable_sel_fetch_arm,
 *    icl_plane_update_sel_fetch_arm, icl_plane_update_arm, icl_plane_disable_arm, icl_plane_min_cdclk, skl_plane_get_hw_state;
 *  - the includes are replaced by lcd_compat.h + lcd_plane_compat.h (register writes go through the emit hook;
 *    callees that are not ported -- skl_write_plane_wm, the scaler and CSC programming -- are recorded as
 *    named steps there, never silently dropped);
 *  - parity_plane_emit_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/, drm/ and i915 includes */
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_plane_compat.h"

u8 icl_hdr_plane_mask(void)
{
	return BIT(PLANE_PRIMARY) | BIT(PLANE_SPRITE0) | BIT(PLANE_SPRITE1);
}

bool icl_is_hdr_plane(struct drm_i915_private *dev_priv, enum plane_id plane_id)
{
	return DISPLAY_VER(dev_priv) >= 11 &&
		icl_hdr_plane_mask() & BIT(plane_id);
}

static unsigned int skl_plane_stride_mult(const struct drm_framebuffer *fb,
					  int color_plane, unsigned int rotation)
{
	/*
	 * The stride is either expressed as a multiple of 64 bytes chunks for
	 * linear buffers or in number of tiles for tiled buffers.
	 */
	if (is_surface_linear(fb, color_plane))
		return 64;
	else if (drm_rotation_90_or_270(rotation))
		return intel_tile_height(fb, color_plane);
	else
		return intel_tile_width_bytes(fb, color_plane);
}

static u32 skl_plane_stride(const struct intel_plane_state *plane_state,
			    int color_plane)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	u32 stride = plane_state->view.color_plane[color_plane].scanout_stride;

	if (color_plane >= fb->format->num_planes)
		return 0;

	return stride / skl_plane_stride_mult(fb, color_plane, rotation);
}

static u32 skl_plane_ctl_format(u32 pixel_format)
{
	switch (pixel_format) {
	case DRM_FORMAT_C8:
		return PLANE_CTL_FORMAT_INDEXED;
	case DRM_FORMAT_RGB565:
		return PLANE_CTL_FORMAT_RGB_565;
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
		return PLANE_CTL_FORMAT_XRGB_8888 | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return PLANE_CTL_FORMAT_XRGB_8888;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		return PLANE_CTL_FORMAT_XRGB_2101010 | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		return PLANE_CTL_FORMAT_XRGB_2101010;
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
		return PLANE_CTL_FORMAT_XRGB_16161616F | PLANE_CTL_ORDER_RGBX;
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
		return PLANE_CTL_FORMAT_XRGB_16161616F;
	case DRM_FORMAT_XYUV8888:
		return PLANE_CTL_FORMAT_XYUV;
	case DRM_FORMAT_YUYV:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_YUYV;
	case DRM_FORMAT_YVYU:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_YVYU;
	case DRM_FORMAT_UYVY:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_UYVY;
	case DRM_FORMAT_VYUY:
		return PLANE_CTL_FORMAT_YUV422 | PLANE_CTL_YUV422_ORDER_VYUY;
	case DRM_FORMAT_NV12:
		return PLANE_CTL_FORMAT_NV12;
	case DRM_FORMAT_P010:
		return PLANE_CTL_FORMAT_P010;
	case DRM_FORMAT_P012:
		return PLANE_CTL_FORMAT_P012;
	case DRM_FORMAT_P016:
		return PLANE_CTL_FORMAT_P016;
	case DRM_FORMAT_Y210:
		return PLANE_CTL_FORMAT_Y210;
	case DRM_FORMAT_Y212:
		return PLANE_CTL_FORMAT_Y212;
	case DRM_FORMAT_Y216:
		return PLANE_CTL_FORMAT_Y216;
	case DRM_FORMAT_XVYU2101010:
		return PLANE_CTL_FORMAT_Y410;
	case DRM_FORMAT_XVYU12_16161616:
		return PLANE_CTL_FORMAT_Y412;
	case DRM_FORMAT_XVYU16161616:
		return PLANE_CTL_FORMAT_Y416;
	default:
		MISSING_CASE(pixel_format);
	}

	return 0;
}

static u32 skl_plane_ctl_alpha(const struct intel_plane_state *plane_state)
{
	if (!plane_state->hw.fb->format->has_alpha)
		return PLANE_CTL_ALPHA_DISABLE;

	switch (plane_state->hw.pixel_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		return PLANE_CTL_ALPHA_DISABLE;
	case DRM_MODE_BLEND_PREMULTI:
		return PLANE_CTL_ALPHA_SW_PREMULTIPLY;
	case DRM_MODE_BLEND_COVERAGE:
		return PLANE_CTL_ALPHA_HW_PREMULTIPLY;
	default:
		MISSING_CASE(plane_state->hw.pixel_blend_mode);
		return PLANE_CTL_ALPHA_DISABLE;
	}
}

static u32 glk_plane_color_ctl_alpha(const struct intel_plane_state *plane_state)
{
	if (!plane_state->hw.fb->format->has_alpha)
		return PLANE_COLOR_ALPHA_DISABLE;

	switch (plane_state->hw.pixel_blend_mode) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		return PLANE_COLOR_ALPHA_DISABLE;
	case DRM_MODE_BLEND_PREMULTI:
		return PLANE_COLOR_ALPHA_SW_PREMULTIPLY;
	case DRM_MODE_BLEND_COVERAGE:
		return PLANE_COLOR_ALPHA_HW_PREMULTIPLY;
	default:
		MISSING_CASE(plane_state->hw.pixel_blend_mode);
		return PLANE_COLOR_ALPHA_DISABLE;
	}
}

static u32 skl_plane_ctl_tiling(u64 fb_modifier)
{
	switch (fb_modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		break;
	case I915_FORMAT_MOD_X_TILED:
		return PLANE_CTL_TILED_X;
	case I915_FORMAT_MOD_Y_TILED:
		return PLANE_CTL_TILED_Y;
	case I915_FORMAT_MOD_4_TILED:
		return PLANE_CTL_TILED_4;
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
		return PLANE_CTL_TILED_4 |
			PLANE_CTL_RENDER_DECOMPRESSION_ENABLE |
			PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
		return PLANE_CTL_TILED_4 |
			PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE |
			PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
		return PLANE_CTL_TILED_4 | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
		return PLANE_CTL_TILED_4 |
			PLANE_CTL_RENDER_DECOMPRESSION_ENABLE |
			PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
		return PLANE_CTL_TILED_4 | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
		return PLANE_CTL_TILED_4 | PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
		return PLANE_CTL_TILED_Y | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
		return PLANE_CTL_TILED_Y |
		       PLANE_CTL_RENDER_DECOMPRESSION_ENABLE |
		       PLANE_CTL_CLEAR_COLOR_DISABLE;
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		return PLANE_CTL_TILED_Y | PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE;
	case I915_FORMAT_MOD_Yf_TILED:
		return PLANE_CTL_TILED_YF;
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		return PLANE_CTL_TILED_YF | PLANE_CTL_RENDER_DECOMPRESSION_ENABLE;
	default:
		MISSING_CASE(fb_modifier);
	}

	return 0;
}

static u32 skl_plane_ctl_rotate(unsigned int rotate)
{
	switch (rotate) {
	case DRM_MODE_ROTATE_0:
		break;
	/*
	 * DRM_MODE_ROTATE_ is counter clockwise to stay compatible with Xrandr
	 * while i915 HW rotation is clockwise, thats why this swapping.
	 */
	case DRM_MODE_ROTATE_90:
		return PLANE_CTL_ROTATE_270;
	case DRM_MODE_ROTATE_180:
		return PLANE_CTL_ROTATE_180;
	case DRM_MODE_ROTATE_270:
		return PLANE_CTL_ROTATE_90;
	default:
		MISSING_CASE(rotate);
	}

	return 0;
}

static u32 icl_plane_ctl_flip(unsigned int reflect)
{
	switch (reflect) {
	case 0:
		break;
	case DRM_MODE_REFLECT_X:
		return PLANE_CTL_FLIP_HORIZONTAL;
	case DRM_MODE_REFLECT_Y:
	default:
		MISSING_CASE(reflect);
	}

	return 0;
}

static u32 adlp_plane_ctl_arb_slots(const struct intel_plane_state *plane_state)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier)) {
		switch (fb->format->cpp[0]) {
		case 2:
			return PLANE_CTL_ARB_SLOTS(1);
		default:
			return PLANE_CTL_ARB_SLOTS(0);
		}
	} else {
		switch (fb->format->cpp[0]) {
		case 8:
			return PLANE_CTL_ARB_SLOTS(3);
		case 4:
			return PLANE_CTL_ARB_SLOTS(1);
		default:
			return PLANE_CTL_ARB_SLOTS(0);
		}
	}
}

static u32 skl_plane_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	u32 plane_ctl = 0;

	if (DISPLAY_VER(dev_priv) >= 10)
		return plane_ctl;

	if (crtc_state->gamma_enable)
		plane_ctl |= PLANE_CTL_PIPE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		plane_ctl |= PLANE_CTL_PIPE_CSC_ENABLE;

	return plane_ctl;
}

static u32 skl_plane_ctl(const struct intel_crtc_state *crtc_state,
			 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u32 plane_ctl;

	plane_ctl = PLANE_CTL_ENABLE;

	if (DISPLAY_VER(dev_priv) < 10) {
		plane_ctl |= skl_plane_ctl_alpha(plane_state);
		plane_ctl |= PLANE_CTL_PLANE_GAMMA_DISABLE;

		if (plane_state->hw.color_encoding == DRM_COLOR_YCBCR_BT709)
			plane_ctl |= PLANE_CTL_YUV_TO_RGB_CSC_FORMAT_BT709;

		if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			plane_ctl |= PLANE_CTL_YUV_RANGE_CORRECTION_DISABLE;
	}

	plane_ctl |= skl_plane_ctl_format(fb->format->format);
	plane_ctl |= skl_plane_ctl_tiling(fb->modifier);
	plane_ctl |= skl_plane_ctl_rotate(rotation & DRM_MODE_ROTATE_MASK);

	if (DISPLAY_VER(dev_priv) >= 11)
		plane_ctl |= icl_plane_ctl_flip(rotation &
						DRM_MODE_REFLECT_MASK);

	if (key->flags & I915_SET_COLORKEY_DESTINATION)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_DESTINATION;
	else if (key->flags & I915_SET_COLORKEY_SOURCE)
		plane_ctl |= PLANE_CTL_KEY_ENABLE_SOURCE;

	/* Wa_22012358565:adl-p */
	if (DISPLAY_VER(dev_priv) == 13)
		plane_ctl |= adlp_plane_ctl_arb_slots(plane_state);

	return plane_ctl;
}

static u32 glk_plane_color_ctl_crtc(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	u32 plane_color_ctl = 0;

	if (DISPLAY_VER(dev_priv) >= 11)
		return plane_color_ctl;

	if (crtc_state->gamma_enable)
		plane_color_ctl |= PLANE_COLOR_PIPE_GAMMA_ENABLE;

	if (crtc_state->csc_enable)
		plane_color_ctl |= PLANE_COLOR_PIPE_CSC_ENABLE;

	return plane_color_ctl;
}

static u32 glk_plane_color_ctl(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	u32 plane_color_ctl = 0;

	plane_color_ctl |= PLANE_COLOR_PLANE_GAMMA_DISABLE;
	plane_color_ctl |= glk_plane_color_ctl_alpha(plane_state);

	if (fb->format->is_yuv && !icl_is_hdr_plane(dev_priv, plane->id)) {
		switch (plane_state->hw.color_encoding) {
		case DRM_COLOR_YCBCR_BT709:
			plane_color_ctl |= PLANE_COLOR_CSC_MODE_YUV709_TO_RGB709;
			break;
		case DRM_COLOR_YCBCR_BT2020:
			plane_color_ctl |=
				PLANE_COLOR_CSC_MODE_YUV2020_TO_RGB2020;
			break;
		default:
			plane_color_ctl |=
				PLANE_COLOR_CSC_MODE_YUV601_TO_RGB601;
		}
		if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			plane_color_ctl |= PLANE_COLOR_YUV_RANGE_CORRECTION_DISABLE;
	} else if (fb->format->is_yuv) {
		plane_color_ctl |= PLANE_COLOR_INPUT_CSC_ENABLE;
		if (plane_state->hw.color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			plane_color_ctl |= PLANE_COLOR_YUV_RANGE_CORRECTION_DISABLE;
	}

	if (plane_state->force_black)
		plane_color_ctl |= PLANE_COLOR_PLANE_CSC_ENABLE;

	return plane_color_ctl;
}

static u32 skl_surf_address(const struct intel_plane_state *plane_state,
			    int color_plane)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	u32 offset = plane_state->view.color_plane[color_plane].offset;

	if (intel_fb_uses_dpt(fb)) {
		/*
		 * The DPT object contains only one vma, so the VMA's offset
		 * within the DPT is always 0.
		 */
		drm_WARN_ON(&i915->drm, plane_state->dpt_vma &&
			    plane_state->dpt_vma->node.start);
		drm_WARN_ON(&i915->drm, offset & 0x1fffff);
		return offset >> 9;
	} else {
		drm_WARN_ON(&i915->drm, offset & 0xfff);
		return offset;
	}
}

static u32 skl_plane_surf(const struct intel_plane_state *plane_state,
			  int color_plane)
{
	u32 plane_surf;

	plane_surf = intel_plane_ggtt_offset(plane_state) +
		skl_surf_address(plane_state, color_plane);

	if (plane_state->decrypt)
		plane_surf |= PLANE_SURF_DECRYPT;

	return plane_surf;
}

static u32 skl_plane_aux_dist(const struct intel_plane_state *plane_state,
			      int color_plane)
{
	struct drm_i915_private *i915 = to_i915(plane_state->uapi.plane->dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int aux_plane = skl_main_to_aux_plane(fb, color_plane);
	u32 aux_dist;

	if (!aux_plane)
		return 0;

	aux_dist = skl_surf_address(plane_state, aux_plane) -
		skl_surf_address(plane_state, color_plane);

	if (DISPLAY_VER(i915) < 12)
		aux_dist |= PLANE_AUX_STRIDE(skl_plane_stride(plane_state, aux_plane));

	return aux_dist;
}

static u32 skl_plane_keyval(const struct intel_plane_state *plane_state)
{
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;

	return key->min_value;
}

static u32 skl_plane_keymsk(const struct intel_plane_state *plane_state)
{
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u8 alpha = plane_state->hw.alpha >> 8;
	u32 keymsk;

	keymsk = key->channel_mask & 0x7ffffff;
	if (alpha < 0xff)
		keymsk |= PLANE_KEYMSK_ALPHA_ENABLE;

	return keymsk;
}

static u32 skl_plane_keymax(const struct intel_plane_state *plane_state)
{
	const struct drm_intel_sprite_colorkey *key = &plane_state->ckey;
	u8 alpha = plane_state->hw.alpha >> 8;

	return (key->max_value & 0xffffff) | PLANE_KEYMAX_ALPHA(alpha);
}

static int icl_plane_color_plane(const struct intel_plane_state *plane_state)
{
	/* Program the UV plane on planar master */
	if (plane_state->planar_linked_plane && !plane_state->planar_slave)
		return 1;
	else
		return 0;
}

static void icl_plane_update_sel_fetch_noarm(struct intel_plane *plane,
					     const struct intel_crtc_state *crtc_state,
					     const struct intel_plane_state *plane_state,
					     int color_plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;
	const struct drm_rect *clip;
	u32 val;
	int x, y;

	if (!crtc_state->enable_psr2_sel_fetch)
		return;

	clip = &plane_state->psr2_sel_fetch_area;

	val = (clip->y1 + plane_state->uapi.dst.y1) << 16;
	val |= plane_state->uapi.dst.x1;
	intel_de_write_fw(i915, PLANE_SEL_FETCH_POS(pipe, plane->id), val);

	x = plane_state->view.color_plane[color_plane].x;

	/*
	 * From Bspec: UV surface Start Y Position = half of Y plane Y
	 * start position.
	 */
	if (!color_plane)
		y = plane_state->view.color_plane[color_plane].y + clip->y1;
	else
		y = plane_state->view.color_plane[color_plane].y + clip->y1 / 2;

	val = y << 16 | x;

	intel_de_write_fw(i915, PLANE_SEL_FETCH_OFFSET(pipe, plane->id),
			  val);

	/* Sizes are 0 based */
	val = (drm_rect_height(clip) - 1) << 16;
	val |= (drm_rect_width(&plane_state->uapi.src) >> 16) - 1;
	intel_de_write_fw(i915, PLANE_SEL_FETCH_SIZE(pipe, plane->id), val);
}

static void
icl_plane_update_noarm(struct intel_plane *plane,
		       const struct intel_crtc_state *crtc_state,
		       const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	int color_plane = icl_plane_color_plane(plane_state);
	u32 stride = skl_plane_stride(plane_state, color_plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	int x = plane_state->view.color_plane[color_plane].x;
	int y = plane_state->view.color_plane[color_plane].y;
	int src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	int src_h = drm_rect_height(&plane_state->uapi.src) >> 16;
	u32 plane_color_ctl;

	plane_color_ctl = plane_state->color_ctl |
		glk_plane_color_ctl_crtc(crtc_state);

	/* The scaler will handle the output position */
	if (plane_state->scaler_id >= 0) {
		crtc_x = 0;
		crtc_y = 0;
	}

	intel_de_write_fw(dev_priv, PLANE_STRIDE(pipe, plane_id),
			  PLANE_STRIDE_(stride));
	intel_de_write_fw(dev_priv, PLANE_POS(pipe, plane_id),
			  PLANE_POS_Y(crtc_y) | PLANE_POS_X(crtc_x));
	intel_de_write_fw(dev_priv, PLANE_SIZE(pipe, plane_id),
			  PLANE_HEIGHT(src_h - 1) | PLANE_WIDTH(src_w - 1));

	intel_de_write_fw(dev_priv, PLANE_KEYVAL(pipe, plane_id), skl_plane_keyval(plane_state));
	intel_de_write_fw(dev_priv, PLANE_KEYMSK(pipe, plane_id), skl_plane_keymsk(plane_state));
	intel_de_write_fw(dev_priv, PLANE_KEYMAX(pipe, plane_id), skl_plane_keymax(plane_state));

	intel_de_write_fw(dev_priv, PLANE_OFFSET(pipe, plane_id),
			  PLANE_OFFSET_Y(y) | PLANE_OFFSET_X(x));

	if (intel_fb_is_rc_ccs_cc_modifier(fb->modifier)) {
		intel_de_write_fw(dev_priv, PLANE_CC_VAL(pipe, plane_id, 0),
				  lower_32_bits(plane_state->ccval));
		intel_de_write_fw(dev_priv, PLANE_CC_VAL(pipe, plane_id, 1),
				  upper_32_bits(plane_state->ccval));
	}

	/* FLAT CCS doesn't need to program AUX_DIST */
	if (!HAS_FLAT_CCS(dev_priv) && DISPLAY_VER(dev_priv) < 20)
		intel_de_write_fw(dev_priv, PLANE_AUX_DIST(pipe, plane_id),
				  skl_plane_aux_dist(plane_state, color_plane));

	if (icl_is_hdr_plane(dev_priv, plane_id))
		intel_de_write_fw(dev_priv, PLANE_CUS_CTL(pipe, plane_id),
				  plane_state->cus_ctl);

	intel_de_write_fw(dev_priv, PLANE_COLOR_CTL(pipe, plane_id), plane_color_ctl);

	if (fb->format->is_yuv && icl_is_hdr_plane(dev_priv, plane_id))
		icl_program_input_csc(plane, crtc_state, plane_state);

	skl_write_plane_wm(plane, crtc_state);

	/*
	 * FIXME: pxp session invalidation can hit any time even at time of commit
	 * or after the commit, display content will be garbage.
	 */
	if (plane_state->force_black)
		icl_plane_csc_load_black(plane);

	icl_plane_update_sel_fetch_noarm(plane, crtc_state, plane_state, color_plane);
}

static void icl_plane_disable_sel_fetch_arm(struct intel_plane *plane,
					    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	if (!crtc_state->enable_psr2_sel_fetch)
		return;

	intel_de_write_fw(i915, PLANE_SEL_FETCH_CTL(pipe, plane->id), 0);
}

static void icl_plane_update_sel_fetch_arm(struct intel_plane *plane,
					   const struct intel_crtc_state *crtc_state,
					   const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
	enum pipe pipe = plane->pipe;

	if (!crtc_state->enable_psr2_sel_fetch)
		return;

	if (drm_rect_height(&plane_state->psr2_sel_fetch_area) > 0)
		intel_de_write_fw(i915, PLANE_SEL_FETCH_CTL(pipe, plane->id),
				  PLANE_SEL_FETCH_CTL_ENABLE);
	else
		icl_plane_disable_sel_fetch_arm(plane, crtc_state);
}

static void
icl_plane_update_arm(struct intel_plane *plane,
		     const struct intel_crtc_state *crtc_state,
		     const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;
	int color_plane = icl_plane_color_plane(plane_state);
	u32 plane_ctl;

	plane_ctl = plane_state->ctl |
		skl_plane_ctl_crtc(crtc_state);

	/*
	 * Enable the scaler before the plane so that we don't
	 * get a catastrophic underrun even if the two operations
	 * end up happening in two different frames.
	 *
	 * TODO: split into noarm+arm pair
	 */
	if (plane_state->scaler_id >= 0)
		skl_program_plane_scaler(plane, crtc_state, plane_state);

	icl_plane_update_sel_fetch_arm(plane, crtc_state, plane_state);

	/*
	 * The control register self-arms if the plane was previously
	 * disabled. Try to make the plane enable atomic by writing
	 * the control register just before the surface register.
	 */
	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), plane_ctl);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id),
			  skl_plane_surf(plane_state, color_plane));
}

static void
icl_plane_disable_arm(struct intel_plane *plane,
		      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum plane_id plane_id = plane->id;
	enum pipe pipe = plane->pipe;

	if (icl_is_hdr_plane(dev_priv, plane_id))
		intel_de_write_fw(dev_priv, PLANE_CUS_CTL(pipe, plane_id), 0);

	skl_write_plane_wm(plane, crtc_state);

	icl_plane_disable_sel_fetch_arm(plane, crtc_state);
	intel_de_write_fw(dev_priv, PLANE_CTL(pipe, plane_id), 0);
	intel_de_write_fw(dev_priv, PLANE_SURF(pipe, plane_id), 0);
}

static int icl_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate = intel_plane_pixel_rate(crtc_state, plane_state);

	/* two pixels per clock */
	return DIV_ROUND_UP(pixel_rate, 2);
}

static bool
skl_plane_get_hw_state(struct intel_plane *plane,
		       enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	enum intel_display_power_domain power_domain;
	enum plane_id plane_id = plane->id;
	intel_wakeref_t wakeref;
	bool ret;

	power_domain = POWER_DOMAIN_PIPE(plane->pipe);
	wakeref = intel_display_power_get_if_enabled(dev_priv, power_domain);
	if (!wakeref)
		return false;

	ret = intel_de_read(dev_priv, PLANE_CTL(plane->pipe, plane_id)) & PLANE_CTL_ENABLE;

	*pipe = plane->pipe;

	intel_display_power_put(dev_priv, power_domain, wakeref);

	return ret;
}


#include "parity_plane_emit_glue.inc"	/* zedBSD: builds the plane / fb state and records the words */
