/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The word recorder of a primary plane (see plane.h): the plane's words
 * as a light would write them, for the calculation's record.
 */

#include "modeset-internal.h"
#include "plane.h"
#include "xd-dbuf.h"
#include "xd-pipe.h"
#include "xd-screen.h"

#include <kern/kcrt.h>

#include <uapi/errno.h>

static void i915_plane_fill(struct i915_xd_plane *plane, unsigned pipe, uint32_t width, uint32_t height, uint32_t pitch, uint32_t surf);

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
