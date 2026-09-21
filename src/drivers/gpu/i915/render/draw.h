/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One recorded draw on the GPU.
 *
 * A draw writes the session's state object (heap.h) and batch object, and
 * runs the batch to its end on the render engine before it returns.  The two
 * objects are made on the session's first draw or rectangle and kept until
 * the session closes; every draw and every rectangle rewrites them.
 */

#ifndef DRIVERS_GPU_I915_RENDER_DRAW_H
#define DRIVERS_GPU_I915_RENDER_DRAW_H

#include <stdint.h>

#include "gfx.h"

struct i915_gem_object;
struct i915_render_session;

/*
 * What a session's draws and rectangles keep between them.
 *
 * The session owns it from its first draw or rectangle to its close.  The
 * objects are bound into the session's address space; one operation at a
 * time rewrites them, and the operation runs to its end before the next.
 */
struct i915_gfx_session {
	/* The state object every heap lives in, and the batch object. */
	struct i915_gem_object *state;
	struct i915_gem_object *batch;

	/* How many draws the session has run, for the first-draw log line and the checkpoint. */
	unsigned draws;
};

struct i915_gfx_session *drv_i915_gfx_session_get(struct i915_render_session *session);
void drv_i915_gfx_session_close(struct i915_render_session *session);
int drv_i915_gfx_draw(struct i915_render_session *session, const struct i915_gfx_draw_state *state, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

#endif
