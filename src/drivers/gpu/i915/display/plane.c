/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The primary plane of the one-screen path (see plane.h): the plane of a
 * modeset object as the xd plane, its writes and its watermarks, the
 * update of a flip that must land in one frame, and the word recorder of
 * the calculation.
 */

#include "modeset-internal.h"
#include "plane.h"
#include "xd-dbuf.h"
#include "xd-pipe.h"
#include "xd-screen.h"

#include <kern/kcrt.h>

#include <uapi/errno.h>

static void i915_plane_fill(struct i915_xd_plane *plane, unsigned pipe, uint32_t width, uint32_t height, uint32_t pitch, uint32_t surf);
static void i915_plane_write_wm(struct i915_xd_io *io, const struct i915_lcd_modeset *ms);
static int i915_plane_vblank_get(void *context);
static void i915_plane_vblank_put(void *context);
static long i915_plane_vblank_sleep(void *context, long ticks);
static void i915_plane_irq_off(void *context);
static void i915_plane_irq_on(void *context);
static void i915_plane_arm(void *context);
static void i915_plane_arm_event(void *context);
static void i915_plane_error(void *context, const char *what);

/*
 * Records the plane words of one full-screen primary plane.
 *
 * The plane covers the pipe without scaling on a linear XRGB8888 buffer;
 * its words that do not arm the update, the watermarks of an empty
 * state (no levels, no buffer range), then the control word and the
 * arming surface go to `emit`.  Anything else is refused before a word is
 * written: 0, or EINVAL.  The device view is the world's.
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
	const struct i915_xd_platform *platform;
	struct drm_i915_private *i915;
	struct i915_xd_plane plane;
	struct i915_xd_plane_wm wm;
	struct i915_xd_ddb ddb;
	struct i915_xd_io *io;
	int error;

	/* Only a buffer the xd plane scans out. */
	error = drv_i915_xd_plane_check(fourcc, modifier, width, height, pitch, surf_ggtt_offset);
	if (error != 0)
		return error;

	/* Only the primary plane of pipes A to D. */
	if (pipe < 0 || pipe > 3)
		return EINVAL;
	if (plane_id != PLANE_PRIMARY)
		return EINVAL;

	/* The recorder's device view writes through the caller's backend. */
	i915 = &world->i915_plane_emit_i915;
	kern_memset(i915, 0, sizeof(*i915));
	i915->emit = emit;
	io = drv_i915_lcd_xd_io(i915);
	platform = drv_i915_lcd_xd_platform();

	/* The plane and its control words. */
	i915_plane_fill(&plane, (unsigned)pipe, width, height, pitch, surf_ggtt_offset);
	drv_i915_xd_plane_words(platform, &plane);

	/* The words that do not arm, the empty watermarks, then the control word and the arming surface. */
	kern_memset(&wm, 0, sizeof(wm));
	kern_memset(&ddb, 0, sizeof(ddb));
	drv_i915_xd_plane_noarm(io, &plane);
	drv_i915_xd_wm_write_plane(io, platform, (unsigned)pipe, 0U, &wm, 0, &ddb);
	drv_i915_xd_plane_arm(io, &plane);

	/* Succeeded: the words are emitted. */
	return 0;
}

/*
 * Prepares the plane of a modeset object: the buffer it shows on the
 * object's pipe and its control words.  0, or EINVAL for anything but a
 * linear XRGB8888 buffer of 1 to 8192 pixels each way with a 64-byte
 * multiple pitch at a page-aligned address.
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

	/* The plane and its control words. */
	i915_plane_fill(&ms->plane, (unsigned)ms->crtc.pipe, width, height, pitch, surf_ggtt_offset);
	drv_i915_xd_plane_words(drv_i915_lcd_xd_platform(), &ms->plane);

	/* Succeeded: the plane is ready for its update. */
	return 0;
}

/*
 * Writes the plane of a modeset object.
 *
 * The words that do not arm the update and the watermarks first, then
 * the control word and the surface; the surface write arms the update,
 * and from then on the buffer may be scanned out.
 */
void
drv_i915_lcd_ms_plane_update(
	struct i915_lcd_modeset *ms)
{
	struct i915_xd_io *io;

	/* The words that do not arm the update, and the watermarks. */
	io = drv_i915_lcd_xd_io(&ms->i915);
	drv_i915_xd_plane_noarm(io, &ms->plane);
	i915_plane_write_wm(io, ms);

	/*
	 * plane_armed is set BEFORE the arming write: from here the buffer
	 * is not the caller's alone.
	 */
	ms->plane_armed = 1;

	/* The control word and the arming surface. */
	drv_i915_xd_plane_arm(io, &ms->plane);
}

/*
 * Disables the plane of a modeset object: its upsampler, its
 * watermarks, then a zero control word and the arming surface.
 */
void
drv_i915_lcd_ms_plane_disable(
	struct i915_lcd_modeset *ms)
{
	struct i915_xd_io *io;

	/* The upsampler, the watermarks, the control and the surface. */
	io = drv_i915_lcd_xd_io(&ms->i915);
	drv_i915_xd_plane_off_noarm(io, (unsigned)ms->crtc.pipe);
	i915_plane_write_wm(io, ms);
	drv_i915_xd_plane_off_arm(io, (unsigned)ms->crtc.pipe);
}

/*
 * Flips the running plane of a modeset object to its plane's new
 * buffer: the words that do not arm and the watermarks, then the arming
 * writes as an update that lands in one frame (out of the lines before
 * the vertical blank, interrupts off), with the flip's completion event
 * armed for the next blank.
 */
void
drv_i915_lcd_ms_plane_update_flip(
	struct i915_lcd_modeset *ms)
{
	struct i915_xd_update_hooks hooks;
	struct i915_xd_update_report report;
	struct i915_xd_update update;
	struct i915_xd_io *io;

	/* The object's device is the one the update works on. */
	ms->world->i915_lcd_cur_i915 = &ms->i915;

	/* The words that do not arm the update, and the watermarks. */
	io = drv_i915_lcd_xd_io(&ms->i915);
	drv_i915_xd_plane_noarm(io, &ms->plane);
	i915_plane_write_wm(io, ms);

	/* The pipe, its frame's lines, the scan line counter's offset and the lines before the blank. */
	update.pipe = (unsigned)ms->crtc.pipe;
	update.vtotal = ms->plan.mode.vtotal;
	update.scanline_offset = I915_XD_SCANLINE_OFFSET;
	if (ms->output_hdmi)
		update.scanline_offset = I915_XD_SCANLINE_OFFSET_HDMI;
	drv_i915_xd_pipe_evade_window((uint32_t)ms->plan.mode.clock_khz, ms->plan.mode.htotal, ms->plan.mode.vdisplay, &update.window);

	/* The backend's side of the update. */
	kern_memset(&hooks, 0, sizeof(hooks));
	hooks.vblank_get = i915_plane_vblank_get;
	hooks.vblank_put = i915_plane_vblank_put;
	hooks.vblank_sleep = i915_plane_vblank_sleep;
	hooks.irq_off = i915_plane_irq_off;
	hooks.irq_on = i915_plane_irq_on;
	hooks.arm = i915_plane_arm;
	hooks.arm_event = i915_plane_arm_event;
	hooks.error = i915_plane_error;
	hooks.context = ms;

	/* The arming writes in one frame, the event armed. */
	drv_i915_xd_screen_update(io, &update, &hooks, &report);
}

/* Fills a full-screen, unscaled, opaque plane of a pipe on a buffer. */
static void
i915_plane_fill(
	struct i915_xd_plane *plane,
	unsigned pipe,
	uint32_t width,
	uint32_t height,
	uint32_t pitch,
	uint32_t surf)
{
	/* The pipe, the buffer's size, pitch and address, at the pipe's origin, opaque. */
	kern_memset(plane, 0, sizeof(*plane));
	plane->pipe = pipe;
	plane->width = width;
	plane->height = height;
	plane->pitch = pitch;
	plane->surf = surf;
	plane->alpha = 0xffU;
}

/* Writes the plane's watermarks and its part of the display buffer as the screen's plan found them. */
static void
i915_plane_write_wm(
	struct i915_xd_io *io,
	const struct i915_lcd_modeset *ms)
{
	/* The levels, the transition, the SAGV pair (not used: SAGV stays off) and the range. */
	drv_i915_xd_wm_write_plane(io, drv_i915_lcd_xd_platform(), (unsigned)ms->crtc.pipe, ms->plan.wm_levels, &ms->plan.wm, 0, &ms->plan.plane_ddb);
}

/* Takes a reference to the pipe's vertical blank interrupt through the backend. */
static int
i915_plane_vblank_get(
	void *context)
{
	struct i915_lcd_modeset *ms;
	int error;

	/* The context is the modeset object. */
	ms = context;

	/* The backend's reference. */
	error = ms->i915.emit->vblank_get(ms->i915.emit->ctx, ms->crtc.pipe);
	if (error != 0)
		return error;

	/* Succeeded: the interrupt is on and referenced. */
	return 0;
}

/* Gives a reference to the pipe's vertical blank interrupt back. */
static void
i915_plane_vblank_put(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The context is the modeset object. */
	ms = context;

	/* The backend's reference goes back. */
	ms->i915.emit->vblank_put(ms->i915.emit->ctx, ms->crtc.pipe);
}

/* Sleeps until the pipe's next vertical blank or the ticks run out; the ticks left. */
static long
i915_plane_vblank_sleep(
	void *context,
	long ticks)
{
	struct i915_lcd_modeset *ms;
	long left;

	/* The context is the modeset object. */
	ms = context;

	/* The backend's sleep. */
	left = ms->i915.emit->vblank_sleep(ms->i915.emit->ctx, ms->crtc.pipe, ticks);

	/* Succeeded: the ticks left (0 when the blank did not come). */
	return left;
}

/* Turns interrupts off for the update, in the world's nesting. */
static void
i915_plane_irq_off(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The context is the modeset object. */
	ms = context;

	/* Off, unless the section already is. */
	I915_LCD_LOCAL_IRQ_DISABLE(ms->world, &ms->i915);
}

/* Turns interrupts back on after the update, in the world's nesting. */
static void
i915_plane_irq_on(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The context is the modeset object. */
	ms = context;

	/* On, when the section had them off. */
	I915_LCD_LOCAL_IRQ_ENABLE(ms->world, &ms->i915);
}

/* Writes the plane's control word and the arming surface. */
static void
i915_plane_arm(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The context is the modeset object. */
	ms = context;

	/* The arming writes. */
	drv_i915_xd_plane_arm(drv_i915_lcd_xd_io(&ms->i915), &ms->plane);
}

/* Arms the flip's completion event for the pipe's next vertical blank. */
static void
i915_plane_arm_event(
	void *context)
{
	struct i915_lcd_modeset *ms;

	/* The context is the modeset object. */
	ms = context;

	/* The backend's event. */
	ms->i915.emit->arm_event(ms->i915.emit->ctx, ms->crtc.pipe);
}

/* Counts an error of the update as one of the screen's. */
static void
i915_plane_error(
	void *context,
	const char *what)
{
	UNUSED_PARAMETER(context);

	/* The world's error count and hook. */
	drv_i915_lcd_error(what);
}
