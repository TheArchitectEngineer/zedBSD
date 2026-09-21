/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One recorded draw on the GPU (see draw.h).
 *
 * The pipeline's kernels come from the executor's own compiler.  The state
 * object gets the heaps and the kernels, the batch object the command list,
 * and the batch runs synchronously on the session's render context.
 */

#include "draw.h"
#include "batch.h"
#include "gfx.h"
#include "heap.h"
#include "internal.h"
#include "state.h"

#include "../i915.h"
#include "../memory.h"
#include "../session.h"
#include "../worker.h"

#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "../intel/genxml.h"

/*
 * The draw checkpoint.
 *
 * The test build defines it to look at what the first draws of a session
 * left in their render target (the pixel census and the image dump the
 * oracle test rebuilds).  It is weak so that a production kernel links
 * without it and the call site is a null test.
 */
extern void drv_i915_gfx_draw_checkpoint(const struct i915_gfx_image *target, unsigned draw) __attribute__((weak));

static int i915_draw_object_create(struct i915_render_session *session, uint64_t bytes, struct i915_gem_object **result);
static int i915_draw_build_batch(struct i915_gfx_batch *batch, uint64_t state_va, const struct i915_gfx_draw_state *state, const struct i915_gfx_kernels *kernels, const struct i915_gfx_image *target, const struct i915_gfx_image *depth, uint32_t mocs, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

/*
 * Returns the objects the session's draws and rectangles share, making them
 * on first use.
 *
 * Returns NULL when either object cannot be made.
 * XXX: the state object is not released when the batch object fails.
 */
struct i915_gfx_session *
drv_i915_gfx_session_get(
	struct i915_render_session *session)
{
	struct i915_gfx_session *work;
	int error;

	/* Reuses the objects made by an earlier draw or rectangle. */
	if (session->gfx != NULL)
		return session->gfx;

	/* Allocates the record of the objects. */
	work = kern_calloc(1U, sizeof(*work));
	if (work == NULL)
		return NULL;

	/* Makes the state object every heap lives in. */
	error = i915_draw_object_create(session, I915_GFX_STATE_BYTES, &work->state);
	if (error != 0) {
		kern_free(work);
		return NULL;
	}

	/* Makes the batch object. */
	error = i915_draw_object_create(session, I915_GFX_BATCH_BYTES, &work->batch);
	if (error != 0) {
		kern_free(work);
		return NULL;
	}

	/* Keeps the objects with the session until it closes. */
	session->gfx = work;

	/* Succeeded: the session has its state and batch objects. */
	return work;
}

/*
 * Releases what the session's draws and rectangles kept: the state and
 * batch objects.
 */
void
drv_i915_gfx_session_close(
	struct i915_render_session *session)
{
	struct i915_gfx_session *work;
	struct i915_device *device;

	/* A session that never drew keeps nothing. */
	work = session->gfx;
	if (work == NULL)
		return;

	/* Unbinds and destroys both objects. */
	device = session->vk->i915;
	mutex_lock(&device->mutex);

	drv_i915_gem_unbind_vm(work->state);
	drv_i915_gem_destroy(&device->gem, work->state);
	drv_i915_gem_unbind_vm(work->batch);
	drv_i915_gem_destroy(&device->gem, work->batch);

	mutex_unlock(&device->mutex);

	/* Forgets the record, so a later draw makes new objects. */
	kern_free(work);
	session->gfx = NULL;
}

/*
 * Runs one draw to its end on the GPU.
 *
 * The draw needs a prepared pipeline, a render pass and a colour
 * attachment; the depth attachment is optional.  Returns EINVAL or ENOTSUP
 * for a draw the path refuses, ENOMEM when the session's objects cannot be
 * made, ENOSPC when the batch does not fit, or the error of the GPU run.
 */
int
drv_i915_gfx_draw(
	struct i915_render_session *session,
	const struct i915_gfx_draw_state *state,
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	struct i915_gfx_session *work;
	struct i915_gfx_batch batch;
	struct i915_gfx_kernels kernels;
	const struct i915_gfx_image *target;
	const struct i915_gfx_image *depth;
	uint64_t target_va;
	uint32_t mocs;
	int refused;
	int error;

	/* Refuses a draw with no prepared pipeline, render pass or colour attachment bound. */
	refused = 0;
	if (state->pipeline == NULL) {
		refused = 1;
	} else if (state->pipeline->kernels_ready == 0) {
		refused = 1;
	} else if (state->pass == NULL) {
		refused = 1;
	} else if (state->framebuffer == NULL) {
		refused = 1;
	} else if (state->pass->color_attachment >= state->framebuffer->view_count) {
		refused = 1;
	} else if (state->framebuffer->views[state->pass->color_attachment] == NULL) {
		refused = 1;
	}

	/* Says why the draw is refused. */
	if (refused != 0) {
		kern_logf("i915: vk: draw refused: no pipeline, render pass or colour attachment is bound\n");
		return EINVAL;
	}

	/* Takes the colour target, and the depth target when the pass has one. */
	target = state->framebuffer->views[state->pass->color_attachment]->image;
	depth = NULL;
	if (state->pass->depth_attachment < state->framebuffer->view_count &&
	    state->framebuffer->views[state->pass->depth_attachment] != NULL)
		depth = state->framebuffer->views[state->pass->depth_attachment]->image;

	/* Takes the session's state and batch objects, making them on the first draw. */
	work = drv_i915_gfx_session_get(session);
	if (work == NULL)
		return ENOMEM;

	/* Every surface, vertex buffer and state of a draw is uncached. */
	mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX);

	/* Writes the heaps and the kernels into the state object. */
	drv_i915_gfx_pipeline_kernels(state->pipeline, &kernels);
	error = drv_i915_gfx_write_state(work->state->address, state, &kernels, target, mocs);
	if (error != 0)
		return error;

	/* Builds the batch into the batch object. */
	batch.cmds = work->batch->address;
	batch.count = 0U;
	batch.capacity = I915_GFX_BATCH_BYTES / 4U;
	batch.overflow = 0;
	error = i915_draw_build_batch(&batch,
				      work->state->va,
				      state,
				      &kernels,
				      target,
				      depth,
				      mocs,
				      vertex_count,
				      instance_count,
				      first_vertex,
				      first_instance);
	if (error != 0)
		return error;

	/* Makes the CPU writes visible before the GPU reads them. */
	kern_io_write_barrier();

	/* Counts the draw; the first one of the session is logged with its layout. */
	work->draws++;
	if (work->draws == 1U) {
		target_va = drv_i915_gfx_memory_va(target->memory, target->offset);
		kern_logf("i915: vk: first draw: batch %u dwords at 0x%llx, state at 0x%llx, target %ux%u at 0x%llx, "
			  "depth %s, %u vertices\n",
			  batch.count,
			  (unsigned long long)work->batch->va,
			  (unsigned long long)work->state->va,
			  target->width,
			  target->height,
			  (unsigned long long)target_va,
			  depth != NULL ? "yes" : "no",
			  vertex_count);
	}

	/* Runs the batch to its end on the session's render context. */
	error = drv_i915_worker_run_sync(session->vk->i915, &session->gpu->contexts[I915_ENGINE_RCS0], work->batch->va);
	if (error != 0) {
		kern_logf("i915: vk: draw %u failed on the GPU: error %d\n", work->draws, error);
		return error;
	}

	/* Lets the test build look at what the first three draws left in the target. */
	if (work->draws <= 3U) {
		if (drv_i915_gfx_draw_checkpoint != NULL)
			drv_i915_gfx_draw_checkpoint(target, work->draws);
	}

	/* Succeeded: the draw has run to its end. */
	return 0;
}

/* Makes one session object bound into the session's address space. */
static int
i915_draw_object_create(
	struct i915_render_session *session,
	uint64_t bytes,
	struct i915_gem_object **result)
{
	struct i915_device *device;
	int error;

	/* Creates the object and binds it, destroying it again when the binding fails. */
	device = session->vk->i915;
	mutex_lock(&device->mutex);

	error = drv_i915_gem_create(&device->gem, bytes, result);
	if (error == 0) {
		error = drv_i915_gem_bind_vm(session->gpu->vm, *result);
		if (error != 0)
			drv_i915_gem_destroy(&device->gem, *result);
	}

	mutex_unlock(&device->mutex);

	/* Reports why the object could not be made, with no object. */
	if (error != 0) {
		*result = NULL;
		return error;
	}

	/* Succeeded: the object is bound and ready. */
	return 0;
}

/*
 * Builds the command list of one draw.
 *
 * The order is the fixture draw's: the context setup, the vertex fetcher,
 * the URB and push constants, the state pointers, the geometry stages with
 * only the vertex shader enabled, the rasterizer, the pixel stage, the depth
 * state, and the primitive.
 */
static int
i915_draw_build_batch(
	struct i915_gfx_batch *batch,
	uint64_t state_va,
	const struct i915_gfx_draw_state *state,
	const struct i915_gfx_kernels *kernels,
	const struct i915_gfx_image *target,
	const struct i915_gfx_image *depth,
	uint32_t mocs,
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	uint32_t entry_size;
	int error;

	/* Switches to 3D, programs the state bases and the once-per-context state. */
	drv_i915_gfx_emit_context_setup(batch, state_va, mocs);

	/* Programs the vertex buffers and elements. */
	error = drv_i915_gfx_emit_vertex_input(batch, state, kernels, mocs);
	if (error != 0)
		return error;

	/* Sizes a VUE entry for the header, the position and the varyings, in 64-byte units. */
	entry_size = ((2U + kernels->varyings) * 16U + 63U) / 64U;
	drv_i915_gfx_emit_urb(batch, entry_size);

	/* Points the vertex stage at the push constants. */
	drv_i915_gfx_emit_constants(batch, state_va + I915_GFX_PUSH_BUFFER, kernels->vs_push_regs, mocs);

	/* Points the pipeline at the colour calc, blend, viewport, scissor and coarse pixel state. */
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_CC_STATE_POINTERS, I915_GFX_DYN_COLOR_CALC | 1U);
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_BLEND_STATE_POINTERS, I915_GFX_DYN_BLEND | 1U);
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_CC, I915_GFX_DYN_CC_VIEWPORT);
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP, I915_GFX_DYN_SF_CLIP_VIEWPORT);
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_SCISSOR_STATE_POINTERS, I915_GFX_DYN_SCISSOR);
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_CPS_POINTERS, I915_GFX_DYN_CPS);

	/* Disables the binding table pool, as anv does against state leaking from other contexts. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_BINDING_TABLE_POOL_ALLOC, GEN12_3DSTATE_BINDING_TABLE_POOL_ALLOC_DWORDS));
	drv_i915_batch_emit(batch, mocs);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);

	/* Renders one sample per pixel. */
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_MULTISAMPLE, GEN12_3DSTATE_MULTISAMPLE_DWORDS);
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_MASK, GEN12_3DSTATE_SAMPLE_MASK_DWORDS));
	drv_i915_batch_emit(batch, 1U);

	/* Enables the vertex shader and no other geometry stage. */
	drv_i915_gfx_emit_vertex_shader(batch, kernels);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_HS, GEN12_3DSTATE_HS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_TE, GEN12_3DSTATE_TE_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_DS, GEN12_3DSTATE_DS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_STREAMOUT, GEN12_3DSTATE_STREAMOUT_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_GS, GEN12_3DSTATE_GS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_PRIMITIVE_REPLICATION, GEN12_3DSTATE_PRIMITIVE_REPLICATION_DWORDS);

	/* Programs the clipper, setup and rasterizer, then the pixel stage. */
	drv_i915_gfx_emit_raster(batch, state->pipeline);
	drv_i915_gfx_emit_pixel_shader(batch, kernels);

	/* Declares a writeable render target. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_BLEND, GEN12_3DSTATE_PS_BLEND_DWORDS));
	drv_i915_batch_emit(batch, 1U << 30);

	/* Programs the depth test and buffer. */
	error = drv_i915_gfx_emit_depth(batch, state, depth, state_va + I915_GFX_SCRATCH, mocs);
	if (error != 0)
		return error;

	/* Draws the triangle list over the target and ends the batch. */
	drv_i915_gfx_emit_primitive(batch,
				    target->width,
				    target->height,
				    GEN12_3DPRIM_TRILIST,
				    vertex_count,
				    first_vertex,
				    instance_count,
				    first_instance);

	/* Refuses a batch that did not fit its object. */
	if (batch->overflow != 0)
		return ENOSPC;

	/* Succeeded: the batch is complete. */
	return 0;
}
