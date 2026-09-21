/*
 * zedBSD WS031: plane-related DRM definitions extracted textually from Linux v6.8.12 by tools/port_lcd_calc.py:
 *   include/drm/drm_blend.h        (sha256 46a47b37fcb9dbb1da3f98d58db0075d21b1a0cde21290c5d627d4ace05f044f): DRM_MODE_BLEND_*, drm_rotation_90_or_270
 *   include/uapi/drm/drm_mode.h    (sha256 6f1e99012854f40c59e62ba9ab031aa6e0f7354f41f25d0a9d23e6dfc6bd370b): DRM_MODE_ROTATE_* / REFLECT_*
 *   include/drm/drm_color_mgmt.h   (sha256 6332adb2c833e3a6ac6a7b1083446eb1e2964c3e2b7f40014309a067f0b8ee5e): enum drm_color_encoding / drm_color_range
 * Each source file carries its own copyright / permission notice (kept unmodified in
 * plan/ws031/linux-parity/linux-reference/drm-v6.8.12/); three sources in one extract is recorded in the
 * provenance ledger as not yet audited.  Do not edit by hand.
 */
#ifndef PARITY_LCD_DRM_PLANE_DEFS_H
#define PARITY_LCD_DRM_PLANE_DEFS_H

#define DRM_MODE_BLEND_PREMULTI		0
#define DRM_MODE_BLEND_COVERAGE		1
#define DRM_MODE_BLEND_PIXEL_NONE	2

#define DRM_MODE_ROTATE_0       (1<<0)
#define DRM_MODE_ROTATE_90      (1<<1)
#define DRM_MODE_ROTATE_180     (1<<2)
#define DRM_MODE_ROTATE_270     (1<<3)
#define DRM_MODE_REFLECT_X      (1<<4)
#define DRM_MODE_REFLECT_Y      (1<<5)
#define DRM_MODE_ROTATE_MASK (\
		DRM_MODE_ROTATE_0  | \
		DRM_MODE_ROTATE_90  | \
		DRM_MODE_ROTATE_180 | \
		DRM_MODE_ROTATE_270)
#define DRM_MODE_REFLECT_MASK (\
		DRM_MODE_REFLECT_X | \
		DRM_MODE_REFLECT_Y)

static inline bool drm_rotation_90_or_270(unsigned int rotation)
{
	return rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270);
}

enum drm_color_encoding {
	DRM_COLOR_YCBCR_BT601,
	DRM_COLOR_YCBCR_BT709,
	DRM_COLOR_YCBCR_BT2020,
	DRM_COLOR_ENCODING_MAX,
};

enum drm_color_range {
	DRM_COLOR_YCBCR_LIMITED_RANGE,
	DRM_COLOR_YCBCR_FULL_RANGE,
	DRM_COLOR_RANGE_MAX,
};

#endif /* PARITY_LCD_DRM_PLANE_DEFS_H */
