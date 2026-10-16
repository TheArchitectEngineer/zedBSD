/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_atomic_plane.c),
 * which carries the following notice.
 *
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/skl_universal_plane.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

/*
 * The universal plane of the one-screen path (see plane.h).
 *
 * The functions follow the Linux 6.8.12 text of skl_universal_plane.c (the
 * control words and the arming and non-arming register writers of display
 * version 11 and later, the plane's CDCLK need and its readout) and
 * intel_atomic_plane.c (the plane's pixel and data rates).  Every register
 * access goes through the device's backend (modeset-internal.h), so the
 * same text records register words, drives the model in the tests and
 * programs the real display; the callees that are not ported (the scaler,
 * the input CSC, the black CSC) are named steps.  PLANE_CTL is the Linux
 * word (0x94000000 for the linear XRGB8888 primary plane of display 13).
 *
 * Around that text: the plane of the modeset object (one full-screen
 * primary plane on a linear XRGB8888 framebuffer, refused before any Linux
 * text runs when anything else is asked for), its update and its
 * synchronous flip, and the word recorder of the calculation.
 */

#include "modeset-internal.h"
#include "takeover-internal.h"
#include "plane.h"
#include "pipe.h"
#include "xd-pipe.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

static void i915_plane_xd_of(const struct intel_plane *plane, const struct intel_plane_state *plane_state, struct i915_xd_plane *xd_plane);
static void i915_plane_xd_words(const struct intel_plane *plane, struct intel_plane_state *plane_state);
static void i915_icl_plane_update_noarm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
static void i915_icl_plane_update_arm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
static void i915_icl_plane_disable_arm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);
static int i915_icl_plane_min_cdclk(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
static bool i915_use_min_ddb(const struct intel_crtc_state *crtc_state, struct intel_plane *plane);
static unsigned int i915_plane_relative_data_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state, int color_plane);
static const struct drm_format_info *i915_xrgb8888_format(void);

/*
 * Scales a pixel rate by a plane's downscaling (the Linux
 * intel_adjusted_rate()).
 *
 * The source rectangle is in 16.16 fixed point.  Downscaling limits the
 * maximum pixel rate: the destination is taken no larger than the source.
 */
unsigned int
drv_i915_adjusted_rate(
	const struct drm_rect *src,
	const struct drm_rect *dst,
	unsigned int rate)
{
	unsigned int src_w;
	unsigned int src_h;
	unsigned int dst_w;
	unsigned int dst_h;

	/* The source size in whole pixels and the destination size. */
	src_w = i915_drm_rect_width(src) >> 16;
	src_h = i915_drm_rect_height(src) >> 16;
	dst_w = i915_drm_rect_width(dst);
	dst_h = i915_drm_rect_height(dst);

	/* Downscaling limits the maximum pixel rate. */
	dst_w = min(src_w, dst_w);
	dst_h = min(src_h, dst_h);

	/* Succeeded: reports the rate scaled by the area ratio, rounded up. */
	return DIV_ROUND_UP_ULL(mul_u32_u32(rate, src_w * src_h), dst_w * dst_h);
}

/*
 * Returns the pixel rate a plane fetches at (the Linux
 * intel_plane_pixel_rate()).
 *
 * The plane's visibility is not checked: the watermark code uses this for
 * a cursor that is fully off screen too.
 */
unsigned int
drv_i915_plane_pixel_rate(
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state)
{
	unsigned int rate;

	/* Scales the pipe's pixel rate by the plane's downscaling. */
	rate = drv_i915_adjusted_rate(&plane_state->uapi.src, &plane_state->uapi.dst, crtc_state->pixel_rate);

	/* Succeeded: reports the rate. */
	return rate;
}

/*
 * Returns the data rate of one colour plane of a visible plane (the Linux
 * intel_plane_data_rate()): its pixel rate times its bytes per pixel.
 */
unsigned int
drv_i915_plane_data_rate(
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state,
	int color_plane)
{
	const struct drm_framebuffer *fb;
	unsigned int pixel_rate;

	/* The framebuffer of the plane. */
	fb = plane_state->hw.fb;

	/* An invisible plane fetches nothing. */
	if (!plane_state->uapi.visible)
		return 0;

	/* The plane's pixel rate. */
	pixel_rate = drv_i915_plane_pixel_rate(crtc_state, plane_state);

	/* Succeeded: reports the bytes per second of the colour plane. */
	return pixel_rate * fb->format->cpp[color_plane];
}

/*
 * Records the plane words of one full-screen primary plane.
 *
 * The plane covers the pipe without scaling on a linear XRGB8888 buffer:
 * source the whole framebuffer (16.16), destination the same rectangle,
 * rotation 0, opaque, the DRM default blend mode, no colour key, BT.709
 * limited-range defaults (unused for RGB), no scaler.  PLANE_CTL and
 * PLANE_COLOR_CTL are the Linux skl_plane_check() words; then the Linux
 * icl_plane_update_noarm() and icl_plane_update_arm() write them.  The
 * words go to `emit`.  Anything else is refused before any Linux text
 * runs: 0, or EINVAL.  The device view, the crtc state and the plane
 * state are the world's (former function statics).
 */
int
drv_i915_plane_emit(
	struct i915_lcd_world *world,
	struct i915_lcd_emit *emit,
	int pipe,
	int plane_id,
	u32 fourcc,
	u64 modifier,
	u32 width,
	u32 height,
	u32 pitch,
	u32 surf_ggtt_offset)
{
	struct drm_i915_private *i915;
	struct intel_crtc crtc;
	struct intel_crtc_state *crtc_state;
	struct intel_plane plane;
	struct intel_plane_state *plane_state;
	struct drm_framebuffer fb;
	int error;

	/* The recorder's device view and states are the world's. */
	i915 = &world->i915_plane_emit_i915;
	crtc_state = &world->i915_plane_emit_crtc_state;
	plane_state = &world->i915_plane_emit_plane_state;

	/* Only a buffer the xd plane scans out. */
	error = drv_i915_xd_plane_check(fourcc, modifier, width, height, pitch, surf_ggtt_offset);
	if (error != 0)
		return error;

	/* Only the primary plane of pipes A to D. */
	if (pipe < 0 || pipe > 3)
		return EINVAL;
	if (plane_id != PLANE_PRIMARY)
		return EINVAL;

	/* Starts every object from zero. */
	kern_memset(i915, 0, sizeof(*i915));
	kern_memset(&crtc, 0, sizeof(crtc));
	kern_memset(crtc_state, 0, sizeof(*crtc_state));
	kern_memset(&plane, 0, sizeof(plane));
	kern_memset(plane_state, 0, sizeof(*plane_state));
	kern_memset(&fb, 0, sizeof(fb));

	/* The device writes through the caller's backend. */
	i915->emit = emit;

	/* The crtc and its state. */
	crtc.base.dev = &i915->drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state->uapi.crtc = &crtc.base;

	/* The primary plane of the pipe. */
	plane.base.dev = &i915->drm;
	plane.id = (enum plane_id)plane_id;
	plane.pipe = (enum pipe)pipe;

	/* The linear XRGB8888 framebuffer. */
	fb.dev = &i915->drm;
	fb.format = i915_xrgb8888_format();
	fb.modifier = modifier;

	/* The plane state of a full-screen, unscaled, opaque plane. */
	plane_state->uapi.plane = &plane.base;
	plane_state->uapi.src.x2 = (int)(width << 16);
	plane_state->uapi.src.y2 = (int)(height << 16);
	plane_state->uapi.dst.x2 = (int)width;
	plane_state->uapi.dst.y2 = (int)height;
	plane_state->hw.fb = &fb;
	plane_state->hw.rotation = MODE_ROTATE_0;
	plane_state->hw.alpha = 0xffff;
	plane_state->hw.pixel_blend_mode = MODE_BLEND_PREMULTI;
	plane_state->hw.color_encoding = COLOR_YCBCR_BT709;
	plane_state->hw.color_range = COLOR_YCBCR_LIMITED_RANGE;
	plane_state->view.color_plane[0].scanout_stride = pitch;
	plane_state->view.color_plane[0].mapping_stride = pitch;
	plane_state->scaler_id = -1;
	plane_state->ggtt_offset = surf_ggtt_offset;

	/* The control words of the xd plane. */
	i915_plane_xd_words(&plane, plane_state);

	/* Writes the plane: the unarmed registers, then PLANE_CTL and the arming PLANE_SURF. */
	i915_icl_plane_update_noarm(&plane, crtc_state, plane_state);
	i915_icl_plane_update_arm(&plane, crtc_state, plane_state);

	/* Succeeded: the words are emitted. */
	return 0;
}

/*
 * Prepares the plane of a modeset object.
 *
 * The framebuffer and plane state are filled exactly as
 * drv_i915_plane_emit() fills them (same checks, same refusals), and
 * PLANE_CTL and PLANE_COLOR_CTL are computed with the Linux functions.
 * 0, or EINVAL for anything but a linear XRGB8888 buffer of 1 to 8192
 * pixels each way with a 64-byte multiple pitch at a page-aligned address.
 */
int
drv_i915_lcd_ms_plane_prepare(
	struct i915_lcd_modeset *ms,
	u32 fourcc,
	u64 modifier,
	u32 width,
	u32 height,
	u32 pitch,
	u32 surf_ggtt_offset)
{
	int error;

	/* Only a buffer the xd plane scans out. */
	error = drv_i915_xd_plane_check(fourcc, modifier, width, height, pitch, surf_ggtt_offset);
	if (error != 0)
		return error;

	/* Starts the plane, its state and the framebuffer from zero. */
	kern_memset(&ms->plane, 0, sizeof(ms->plane));
	kern_memset(&ms->plane_state, 0, sizeof(ms->plane_state));
	kern_memset(&ms->fb, 0, sizeof(ms->fb));

	/* The primary plane of the object's pipe. */
	ms->plane.base.dev = &ms->i915.drm;
	ms->plane.id = PLANE_PRIMARY;
	ms->plane.pipe = ms->crtc.pipe;

	/* The linear XRGB8888 framebuffer. */
	ms->fb.dev = &ms->i915.drm;
	ms->fb.format = i915_xrgb8888_format();
	ms->fb.modifier = modifier;

	/* The plane state of a full-screen, unscaled, opaque plane. */
	ms->plane_state.uapi.plane = &ms->plane.base;
	ms->plane_state.uapi.src.x2 = (int)(width << 16);
	ms->plane_state.uapi.src.y2 = (int)(height << 16);
	ms->plane_state.uapi.dst.x2 = (int)width;
	ms->plane_state.uapi.dst.y2 = (int)height;
	ms->plane_state.hw.fb = &ms->fb;
	ms->plane_state.hw.rotation = MODE_ROTATE_0;
	ms->plane_state.hw.alpha = 0xffff;
	ms->plane_state.hw.pixel_blend_mode = MODE_BLEND_PREMULTI;
	ms->plane_state.hw.color_encoding = COLOR_YCBCR_BT709;
	ms->plane_state.hw.color_range = COLOR_YCBCR_LIMITED_RANGE;
	ms->plane_state.view.color_plane[0].scanout_stride = pitch;
	ms->plane_state.view.color_plane[0].mapping_stride = pitch;
	ms->plane_state.scaler_id = -1;
	ms->plane_state.ggtt_offset = surf_ggtt_offset;

	/* The control words of the xd plane. */
	i915_plane_xd_words(&ms->plane, &ms->plane_state);

	/* Succeeded: the plane is ready for its update. */
	return 0;
}

/*
 * Writes the plane of a modeset object.
 *
 * The unarmed registers first, then PLANE_CTL and PLANE_SURF; the surface
 * write arms the update, and from then on the buffer may be scanned out.
 */
void
drv_i915_lcd_ms_plane_update(
	struct i915_lcd_modeset *ms)
{
	/* Writes the registers that do not arm the update. */
	i915_icl_plane_update_noarm(&ms->plane, &ms->crtc_state, &ms->plane_state);

	/*
	 * plane_armed is set BEFORE the arming write: from here the buffer
	 * is not the caller's alone.
	 */
	ms->plane_armed = 1;

	/* Writes PLANE_CTL and the arming PLANE_SURF. */
	i915_icl_plane_update_arm(&ms->plane, &ms->crtc_state, &ms->plane_state);
}

/*
 * Disables the plane of a modeset object (the Linux icl_plane_disable_arm()).
 */
void
drv_i915_lcd_ms_plane_disable(
	struct i915_lcd_modeset *ms)
{
	/* Writes the disable, armed by the PLANE_SURF write. */
	i915_icl_plane_disable_arm(&ms->plane, &ms->crtc_state);
}

/*
 * Records the CDCLK the plane of a modeset object needs.
 *
 * The Linux intel_plane_calc_min_cdclk(): plane->min_cdclk is
 * icl_plane_min_cdclk on display version 11 and later
 * (skl_universal_plane_create()).
 */
void
drv_i915_lcd_ms_plane_min_cdclk(
	struct i915_lcd_modeset *ms)
{
	/* Stores the plane's need in the crtc state. */
	ms->crtc_state.min_cdclk[ms->plane.id] = i915_icl_plane_min_cdclk(&ms->crtc_state, &ms->plane_state);
}

/*
 * Updates the running crtc of a modeset object to its plane's new state
 * (a synchronous flip).
 *
 * The Linux update of a running crtc (intel_update_crtc()) for its one
 * primary plane: intel_crtc_planes_update_noarm(),
 * intel_pipe_update_start(), intel_crtc_planes_update_arm(),
 * intel_pipe_update_end(); commit_pipe_pre_planes() and
 * commit_pipe_post_planes() have nothing to do for a plane-only update.
 * The old and new crtc state are the object's own (same timings), and the
 * update arms the object's flip event.
 */
void
drv_i915_lcd_ms_plane_update_flip(
	struct i915_lcd_modeset *ms)
{
	struct i915_lcd_world *world;

	/* The world the object belongs to. */
	world = ms->world;

	/* The object's device and pipe are the ones the update works on and sleeps for. */
	world->i915_lcd_cur_i915 = &ms->i915;
	world->i915_lcd_flip_pipe = ms->crtc.pipe;

	/* The update's atomic state: same timings on both sides, not a modeset. */
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;
	ms->state.old_crtc_state = &ms->crtc_state;
	ms->crtc_state.uapi.mode_changed = false;

	/* The completion the update arms. */
	ms->crtc_state.uapi.event = &ms->flip_event;

	/* The unarmed registers, then the arming writes inside the vblank evasion. */
	i915_icl_plane_update_noarm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	drv_i915_pipe_update_start(world, &ms->state, &ms->crtc);
	i915_icl_plane_update_arm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	drv_i915_pipe_update_end(world, &ms->state, &ms->crtc);

	/* The event is armed; the state no longer carries it. */
	ms->crtc_state.uapi.event = 0;
}

/*
 * Records the data rates of the plane of a modeset object.
 *
 * The two assignments of the Linux intel_plane_atomic_check_with_state()
 * that give a visible plane its data rates; the DDB allocation weighs the
 * planes by rel_data_rate.
 */
void
drv_i915_lcd_ms_plane_data_rates(
	struct i915_lcd_modeset *ms)
{
	struct intel_crtc_state *cs;

	/* The object's crtc state. */
	cs = &ms->crtc_state;

	/* The data rate and the relative data rate of the primary plane's colour plane 0. */
	cs->data_rate[ms->plane.id] = drv_i915_plane_data_rate(cs, &ms->plane_state, 0);
	cs->rel_data_rate[ms->plane.id] = i915_plane_relative_data_rate(cs, &ms->plane_state, 0);
}

/* Converts a plane state of the modeset path into the xd plane, with its control words. */
static void
i915_plane_xd_of(
	const struct intel_plane *plane,
	const struct intel_plane_state *plane_state,
	struct i915_xd_plane *xd_plane)
{
	/* The pipe, the source size, the pitch and the position (0 behind a scaler). */
	kern_memset(xd_plane, 0, sizeof(*xd_plane));
	xd_plane->pipe = (unsigned)plane->pipe;
	xd_plane->width = (uint32_t)(i915_drm_rect_width(&plane_state->uapi.src) >> 16);
	xd_plane->height = (uint32_t)(i915_drm_rect_height(&plane_state->uapi.src) >> 16);
	xd_plane->pitch = plane_state->view.color_plane[0].scanout_stride;
	if (plane_state->scaler_id < 0) {
		xd_plane->x = (uint32_t)plane_state->uapi.dst.x1;
		xd_plane->y = (uint32_t)plane_state->uapi.dst.y1;
	}

	/* The surface, the plane alpha and the control words. */
	xd_plane->surf = intel_plane_ggtt_offset(plane_state) + plane_state->view.color_plane[0].offset;
	xd_plane->alpha = (uint8_t)(plane_state->hw.alpha >> 8);
	xd_plane->ctl = plane_state->ctl;
	xd_plane->color_ctl = plane_state->color_ctl;
}

/* Computes the control words of a plane state with the xd plane. */
static void
i915_plane_xd_words(
	const struct intel_plane *plane,
	struct intel_plane_state *plane_state)
{
	struct i915_xd_plane xd_plane;

	/* The xd plane's words, kept in the state. */
	i915_plane_xd_of(plane, plane_state, &xd_plane);
	drv_i915_xd_plane_words(drv_i915_lcd_xd_platform(), &xd_plane);
	plane_state->ctl = xd_plane.ctl;
	plane_state->color_ctl = xd_plane.color_ctl;
}

/*
 * Writes the plane registers that do not arm the update through the xd
 * pipe, then the plane's watermarks and DDB.
 */
static void
i915_icl_plane_update_noarm(
	struct intel_plane *plane,
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_plane xd_plane;

	/* Finds the device. */
	dev_priv = i915_lcd_to_i915(plane->base.dev);

	/* The unarmed words. */
	i915_plane_xd_of(plane, plane_state, &xd_plane);
	drv_i915_xd_plane_noarm(drv_i915_lcd_xd_io(dev_priv), &xd_plane);

	/* The watermarks and DDB of the plane. */
	skl_write_plane_wm(plane, crtc_state);
}

/* Writes PLANE_CTL and the arming PLANE_SURF through the xd pipe. */
static void
i915_icl_plane_update_arm(
	struct intel_plane *plane,
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_plane xd_plane;

	UNUSED_PARAMETER(crtc_state);

	/* Finds the device. */
	dev_priv = i915_lcd_to_i915(plane->base.dev);

	/* The control word and the surface. */
	i915_plane_xd_of(plane, plane_state, &xd_plane);
	drv_i915_xd_plane_arm(drv_i915_lcd_xd_io(dev_priv), &xd_plane);
}

/*
 * Disables a plane through the xd pipe: its upsampler, its watermarks,
 * then PLANE_CTL = 0 and the arming PLANE_SURF = 0.
 */
static void
i915_icl_plane_disable_arm(
	struct intel_plane *plane,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_io *io;

	/* Finds the device and the window. */
	dev_priv = i915_lcd_to_i915(plane->base.dev);
	io = drv_i915_lcd_xd_io(dev_priv);

	/* The upsampler, the watermarks of the disabled plane, the control and the surface. */
	drv_i915_xd_plane_off_noarm(io, (unsigned)plane->pipe);
	skl_write_plane_wm(plane, crtc_state);
	drv_i915_xd_plane_off_arm(io, (unsigned)plane->pipe);
}

/* Returns the CDCLK a plane needs (the Linux icl_plane_min_cdclk()): two pixels per clock. */
static int
i915_icl_plane_min_cdclk(
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state)
{
	unsigned int pixel_rate;

	/* The plane's pixel rate. */
	pixel_rate = drv_i915_plane_pixel_rate(crtc_state, plane_state);

	/* Succeeded: two pixels per clock, rounded up. */
	return DIV_ROUND_UP(pixel_rate, 2);
}

/*
 * Tells whether an async flip gives the plane its minimum DDB only (the
 * Linux use_min_ddb(), display version 13 and later).
 */
static bool
i915_use_min_ddb(
	const struct intel_crtc_state *crtc_state,
	struct intel_plane *plane)
{
	/* Only display version 13 and later. */
	if (drv_i915_lcd_display_ver() < 13)
		return false;

	/* Only an async flip on a plane that does them. */
	if (!crtc_state->uapi.async_flip)
		return false;
	if (!plane->async_flip)
		return false;

	/* Succeeded: the plane gets its minimum DDB. */
	return true;
}

/*
 * Returns the relative data rate a plane weighs in the DDB allocation
 * with (the Linux intel_plane_relative_data_rate()).
 *
 * The source is already rotated for 90/270 degrees (to match the GTT
 * mapping); the UV plane sub-samples by 2.  A cursor counts its bytes, any
 * other plane its bytes scaled by its downscaling.
 */
static unsigned int
i915_plane_relative_data_rate(
	const struct intel_crtc_state *crtc_state,
	const struct intel_plane_state *plane_state,
	int color_plane)
{
	struct intel_plane *plane;
	const struct drm_framebuffer *fb;
	int width;
	int height;
	unsigned int rel_data_rate;
	bool min_ddb;
	unsigned int adjusted;

	/* The plane and its framebuffer. */
	plane = to_intel_plane(plane_state->uapi.plane);
	fb = plane_state->hw.fb;

	/* An invisible plane weighs nothing. */
	if (!plane_state->uapi.visible)
		return 0;

	/* A plane that gets its minimum DDB is not weighed for the extra DDB. */
	min_ddb = i915_use_min_ddb(crtc_state, plane);
	if (min_ddb)
		return 0;

	/* The source size, halved for the UV plane. */
	width = i915_drm_rect_width(&plane_state->uapi.src) >> 16;
	height = i915_drm_rect_height(&plane_state->uapi.src) >> 16;
	if (color_plane == 1) {
		width /= 2;
		height /= 2;
	}

	/* The bytes of the source. */
	rel_data_rate = width * height * fb->format->cpp[color_plane];

	/* A cursor weighs its bytes. */
	if (plane->id == PLANE_CURSOR)
		return rel_data_rate;

	/* Scales the bytes by the downscaling. */
	adjusted = drv_i915_adjusted_rate(&plane_state->uapi.src, &plane_state->uapi.dst, rel_data_rate);

	/* Succeeded: reports the weight. */
	return adjusted;
}

/* Returns the description of the XRGB8888 format this path scans out. */
static const struct drm_format_info *
i915_xrgb8888_format(void)
{
	/* The one format of this path: 4 bytes per pixel, one plane, no alpha. */
	static const struct drm_format_info xrgb8888 = {
		FORMAT_XRGB8888,
		1,
		{ 4, 0, 0, 0 },
		false,
		false,
	};

	/* Succeeded: the description is constant and shared. */
	return &xrgb8888;
}
