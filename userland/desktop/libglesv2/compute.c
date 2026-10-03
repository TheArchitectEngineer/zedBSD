/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * OpenGL ES 3.1's compute dispatches (ws101-p009), in libGLESv2 only
 * (libGL does not build this file: desktop GL's compute is not offered).
 *
 * A dispatch is recorded into the draw surface's frame outside any render
 * pass, like transform feedback's copies: the current compute program's
 * pipeline, a descriptor set with its uniforms, named blocks and the
 * buffer ranges bound to its shader storage blocks' binding points, and
 * vkCmdDispatch (or vkCmdDispatchIndirect from the buffer bound to
 * GL_DISPATCH_INDIRECT_BUFFER).  A buffer bound to a block the shader may
 * write is then newer on the device than on the CPU (gpu_written), and
 * whatever reads its bytes first waits for the frame and reads them back
 * (gles_buffer_fetch): glMapBufferRange, glBufferSubData, copies.
 *
 * Each dispatch is fenced by barriers of its own: before it, from every
 * earlier write (draws, copies, dispatches) to its reads; after it, from
 * its writes to every later use (vertices, indices, uniforms, storage,
 * indirect sizes, copies, the host).  glMemoryBarrier has nothing left to
 * order and only checks its bits.
 *
 * With KEI_GLES_COMPUTE_TRACE set in the environment, each dispatch recorded
 * says so on stderr (ws101-p011: the evidence that a program such as Noct's
 * accelerator ran its kernel on the GPU and did not fall back to the CPU).
 */

#include "gles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bytes of a dispatch's three workgroup counts read from GL_DISPATCH_INDIRECT_BUFFER. */
#define COMPUTE_INDIRECT_SIZE	12U

/* The barrier bits glMemoryBarrierByRegion takes (those of what fragments read). */
#define COMPUTE_REGION_BITS \
	(GL_ATOMIC_COUNTER_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | \
	 GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT)

/* Every barrier bit OpenGL ES 3.1 names. */
#define COMPUTE_BARRIER_BITS \
	(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT | \
	 GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | \
	 GL_PIXEL_BUFFER_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT | \
	 GL_FRAMEBUFFER_BARRIER_BIT | GL_TRANSFORM_FEEDBACK_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT | \
	 GL_SHADER_STORAGE_BARRIER_BIT)

static void compute_dispatch(const GLuint *groups, GLintptr indirect, int is_indirect);
static struct gles_program *compute_program(struct zegl_context *context, struct gles_state *state);
static int compute_indirect_check(struct zegl_context *context, struct gles_state *state, GLintptr indirect, int *skip);
static int compute_storages(struct zegl_context *context, struct gles_state *state, const struct gles_program *program, VkDescriptorBufferInfo *storages);
static void compute_barriers(VkCommandBuffer command, int after);
static void compute_memory_barrier(GLbitfield barriers, GLbitfield allowed);
static void compute_trace(const GLuint *groups, GLintptr indirect, int is_indirect);

/*
 * Whether dispatches are traced: -1 until the environment is read, then 0
 * or 1 for the life of the process.
 */
static int compute_tracing = -1;

/*
 * Runs the current compute program over a grid of workgroups.
 */
GL_APICALL void GL_APIENTRY
glDispatchCompute(
	GLuint num_groups_x,
	GLuint num_groups_y,
	GLuint num_groups_z)
{
	GLuint groups[3];
	uint64_t started;

	/* The grid's three counts; the recording is timed when KEI_GLES_COMPUTE_TRACE is 2 (ws101-p016). */
	groups[0] = num_groups_x;
	groups[1] = num_groups_y;
	groups[2] = num_groups_z;
	started = gles_time_begin();
	compute_dispatch(groups, 0, 0);
	gles_time_end("dispatch-record", started, 0U);
}

/*
 * Runs the current compute program over the grid the buffer bound to
 * GL_DISPATCH_INDIRECT_BUFFER gives at an offset (three unsigned counts).
 */
GL_APICALL void GL_APIENTRY
glDispatchComputeIndirect(
	GLintptr indirect)
{
	GLuint groups[3];

	/* The counts are the buffer's. */
	memset(groups, 0, sizeof(groups));
	compute_dispatch(groups, indirect, 1);
}

/*
 * Orders shader writes before later uses of what they wrote: each
 * dispatch already records the barriers after it, so only the bits are
 * checked.
 */
GL_APICALL void GL_APIENTRY
glMemoryBarrier(
	GLbitfield barriers)
{
	/* Any of OpenGL ES 3.1's bits, or all of them. */
	compute_memory_barrier(barriers, COMPUTE_BARRIER_BITS);
}

/*
 * Orders shader writes before later fragments' reads of the same region:
 * as glMemoryBarrier, of the bits of what fragments read.
 */
GL_APICALL void GL_APIENTRY
glMemoryBarrierByRegion(
	GLbitfield barriers)
{
	/* The bits of what fragments read, or all of them. */
	compute_memory_barrier(barriers, COMPUTE_REGION_BITS);
}

/*
 * Records a dispatch of the current compute program: the grid's counts,
 * or (is_indirect) the ones at an offset of the buffer bound to
 * GL_DISPATCH_INDIRECT_BUFFER.  Errors are recorded.
 */
static void
compute_dispatch(
	const GLuint *groups,
	GLintptr indirect,
	int is_indirect)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	struct zegl_surface *surface;
	struct gles_buffer *sizes;
	VkDescriptorBufferInfo blocks[GLES_NAMED_BLOCKS];
	VkDescriptorBufferInfo storages[GLES_STORAGE_BINDINGS];
	VkDescriptorSet set;
	uint32_t dynamic_offset;
	uint32_t dynamic_count;
	unsigned index;
	EGLint error;
	int status;
	int skip;

	/* A context with its state, and a linked compute program current. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	program = compute_program(context, state);
	if (program == NULL)
		return;

	/* Direct: counts no larger than the device's; a grid without workgroups runs nothing. */
	skip = 0;
	if (!is_indirect) {
		for (index = 0U; index < 3U; index++) {
			if (groups[index] > state->limits.maxComputeWorkGroupCount[index]) {
				gles_error(context, GL_INVALID_VALUE);
				return;
			}

			/* An empty dimension empties the grid. */
			if (groups[index] == 0U)
				skip = 1;
		}
	}

	/* Indirect: the sizes' buffer and offset. */
	if (is_indirect) {
		status = compute_indirect_check(context, state, indirect, &skip);
		if (status != 0)
			return;
	}

	/* The frame the dispatch is recorded in: the draw surface's. */
	surface = context->draw;
	if (surface == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Nothing to run. */
	if (skip)
		return;

	/* The buffer ranges the storage blocks and the named uniform blocks read. */
	status = compute_storages(context, state, program, storages);
	if (status != 0)
		return;
	status = gles_draw_blocks(context, state, blocks);
	if (status != 0)
		return;

	/* The frame, open, with no pass open in it (a dispatch is recorded outside one). */
	gles_target_close(state);
	error = zegl_frame_begin(surface);
	if (error != EGL_SUCCESS) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* An occlusion query's segment and the surface's pass end. */
	gles_queries_suspend(state);
	zegl_frame_leave_pass(surface);

	/* The indirect sizes' device copy, up to date, which this frame reads. */
	sizes = NULL;
	if (is_indirect) {
		sizes = state->dispatch_buffer;
		status = gles_buffer_sync(state, sizes);
		if (status != 0 || sizes->buffer == VK_NULL_HANDLE) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* This frame reads it: a later change does not write it in place. */
		sizes->used = state->frame;
	}

	/* The descriptors. */
	set = gles_draw_descriptors(state, blocks, NULL, storages, &dynamic_offset);
	if (set == VK_NULL_HANDLE) {
		gles_report("the dispatch's descriptors", -1);
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* Earlier writes seen by the dispatch. */
	compute_barriers(surface->command, 0);

	/* The recording: pipeline, descriptors (the uniform block at its dynamic offset), the dispatch. */
	vkCmdBindPipeline(surface->command, VK_PIPELINE_BIND_POINT_COMPUTE, program->compute_pipeline);
	dynamic_count = 0U;
	if (program->uniform_data != NULL)
		dynamic_count = 1U;
	vkCmdBindDescriptorSets(surface->command, VK_PIPELINE_BIND_POINT_COMPUTE, program->layout, 0U, 1U, &set, dynamic_count,
				&dynamic_offset);
	if (is_indirect) {
		vkCmdDispatchIndirect(surface->command, sizes->buffer, (VkDeviceSize)indirect);
	} else {
		vkCmdDispatch(surface->command, groups[0], groups[1], groups[2]);
	}

	/* The dispatch's writes seen by what comes after. */
	compute_barriers(surface->command, 1);
	surface->recorded = 1;

	/* Says so when asked to. */
	compute_trace(groups, indirect, is_indirect);

	/*
	 * A buffer bound to a block the shader may write is newer on the
	 * device now: whatever reads its bytes waits for this frame and reads
	 * them back first (gles_buffer_fetch).
	 */
	for (index = 0U; index < program->storage_count; index++) {
		if (program->storages[index].readonly)
			continue;
		state->storage_ranges[program->storages[index].binding].buffer->gpu_written = 1;
	}
}

/* Returns the current program when it is a linked compute program, else NULL with the error recorded. */
static struct gles_program *
compute_program(
	struct zegl_context *context,
	struct gles_state *state)
{
	struct gles_program *program;

	/* A context that offers compute. */
	if (!state->compute) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* A current program linked with a compute shader. */
	program = state->program;
	if (program == NULL || !program->linked || program->compute_pipeline == VK_NULL_HANDLE) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the program. */
	return program;
}

/*
 * Checks an indirect dispatch's sizes: a buffer bound to
 * GL_DISPATCH_INDIRECT_BUFFER, unmapped, with the three counts at a
 * word-aligned offset inside it.  *skip is set when the counts (known on
 * the CPU while the device has not written the buffer) run nothing or
 * are larger than the device's, which GL leaves undefined and which is
 * not run.  Returns 0, or -1 with the error recorded.
 */
static int
compute_indirect_check(
	struct zegl_context *context,
	struct gles_state *state,
	GLintptr indirect,
	int *skip)
{
	struct gles_buffer *buffer;
	uint32_t counts[3];
	unsigned index;

	/* An offset that is not negative and is a word's. */
	if (indirect < 0 || (indirect % 4) != 0) {
		gles_error(context, GL_INVALID_VALUE);
		return -1;
	}

	/* A buffer bound, not mapped. */
	buffer = state->dispatch_buffer;
	if (buffer == NULL || buffer->map_active) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* The counts inside it. */
	if ((size_t)indirect > buffer->size || buffer->size - (size_t)indirect < COMPUTE_INDIRECT_SIZE) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* Counts the device wrote are read by the dispatch only. */
	if (buffer->gpu_written)
		return 0;

	/* Counts on the CPU: none empty, none past the device's. */
	memcpy(counts, buffer->data + indirect, sizeof(counts));
	for (index = 0U; index < 3U; index++) {
		if (counts[index] == 0U || counts[index] > state->limits.maxComputeWorkGroupCount[index])
			*skip = 1;
	}

	/* Succeeded: the sizes can be read. */
	return 0;
}

/*
 * Describes the buffer range bound to each of the program's shader
 * storage blocks' binding points (the bound size, or the rest of the
 * buffer from the offset), each device copy up to date and marked used by
 * the frame.  Returns 0, or -1 with the error recorded when a binding
 * point has no buffer, a mapped one, or a range smaller than the block.
 */
static int
compute_storages(
	struct zegl_context *context,
	struct gles_state *state,
	const struct gles_program *program,
	VkDescriptorBufferInfo *storages)
{
	const struct gles_storage *storage;
	struct gles_buffer_range *range;
	struct gles_buffer *buffer;
	size_t available;
	size_t length;
	unsigned index;
	int status;

	/* Each block's range. */
	memset(storages, 0, GLES_STORAGE_BINDINGS * sizeof(*storages));
	for (index = 0U; index < program->storage_count; index++) {
		storage = &program->storages[index];
		range = &state->storage_ranges[storage->binding];
		buffer = range->buffer;

		/* A buffer bound to the point, not mapped. */
		if (buffer == NULL || buffer->map_active) {
			gles_error(context, GL_INVALID_OPERATION);
			return -1;
		}

		/* The range: the bound size, or the rest of the buffer, within what the buffer has. */
		available = 0U;
		if (range->offset < buffer->size)
			available = buffer->size - range->offset;
		length = available;
		if (range->size != 0U && range->size < available)
			length = range->size;

		/* A range the block's fixed part does not fit in is an error. */
		if (length == 0U || length < storage->size) {
			gles_error(context, GL_INVALID_OPERATION);
			return -1;
		}

		/* The device reads at most its largest storage buffer range. */
		if (length > state->limits.maxStorageBufferRange)
			length = state->limits.maxStorageBufferRange;

		/*
		 * The buffer's device copy, up to date; the frame's use of it
		 * keeps a later change from writing it in place.
		 */
		status = gles_buffer_sync(state, buffer);
		if (status != 0 || buffer->buffer == VK_NULL_HANDLE) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return -1;
		}

		/* This frame uses it: a later change does not write it in place. */
		buffer->used = state->frame;

		/* The descriptor's range. */
		storages[index].buffer = buffer->buffer;
		storages[index].offset = range->offset;
		storages[index].range = length;
	}

	/* Succeeded: every block has its range. */
	return 0;
}

/*
 * Records a dispatch's barrier: before it (after 0), every earlier
 * command's writes seen by the compute shader's reads and writes and by
 * the indirect sizes' read; after it, the compute shader's writes seen by
 * every later use and by the host.
 */
static void
compute_barriers(
	VkCommandBuffer command,
	int after)
{
	VkMemoryBarrier barrier;

	/* Before: from everything written, to the dispatch. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	if (!after) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT |
					VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0U, 1U, &barrier,
				     0U, NULL, 0U, NULL);
		return;
	}

	/* After: from the dispatch's writes, to everything that may read or write them next. */
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
				VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
				VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
				VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &barrier, 0U, NULL, 0U,
			     NULL);
}

/*
 * Checks glMemoryBarrier's (or glMemoryBarrierByRegion's) bits: all of
 * them, or only allowed ones; the dispatches' own barriers order the
 * writes.  Errors are recorded.
 */
static void
compute_memory_barrier(
	GLbitfield barriers,
	GLbitfield allowed)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, which offers compute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (!state->compute) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Every bit, or bits of the allowed ones. */
	if (barriers != GL_ALL_BARRIER_BITS && (barriers & ~allowed) != 0U)
		gles_error(context, GL_INVALID_VALUE);
}

/* Writes one line on stderr for a recorded dispatch when KEI_GLES_COMPUTE_TRACE is set (any value; 2 also times the steps, gles.c). */
static void
compute_trace(
	const GLuint *groups,
	GLintptr indirect,
	int is_indirect)
{
	const char *setting;

	/* The environment, read once. */
	if (compute_tracing < 0) {
		setting = getenv("KEI_GLES_COMPUTE_TRACE");
		compute_tracing = 0;
		if (setting != NULL)
			compute_tracing = 1;
	}

	/* Nothing unless asked. */
	if (!compute_tracing)
		return;

	/* The grid, or the indirect offset. */
	if (is_indirect) {
		fprintf(stderr, "gles: compute dispatch indirect offset=%ld\n", (long)indirect);
		return;
	}

	/* A direct dispatch's groups. */
	fprintf(stderr, "gles: compute dispatch groups=%u,%u,%u\n", groups[0], groups[1], groups[2]);
}
