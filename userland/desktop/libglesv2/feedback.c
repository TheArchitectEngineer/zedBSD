/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Transform feedback of zedBSD's OpenGL ES 3.0 (WS068 p030).
 *
 * libvulkan offers no transform feedback extension, so a program that
 * captures outputs has its vertex shader write them into a storage buffer
 * (the GLSL compiler's capture, glsl.h): a record per vertex the draw
 * fetches, placed by the instance and the vertex number.  After the draw
 * the records are copied into the transform feedback buffers in the order
 * GL writes vertices (the draw's list of vertices, strips and fans made
 * lists), outside the render pass.  The buffers' device copies are then
 * newer than their bytes on the CPU (gpu_written) until gles_buffer_fetch
 * reads them back.
 */

#include "gles.h"

#include <stdlib.h>
#include <string.h>

static struct gles_feedback *feedback_named(struct gles_state *state, GLuint name);
static unsigned feedback_vertices(GLenum mode);
static size_t feedback_room(const struct gles_buffer_range *range, size_t written);
static int feedback_copy(struct gles_state *state, VkCommandBuffer command, const struct gles_capture_target *capture, struct gles_buffer_range *range, size_t written, unsigned offset, unsigned words, GLint first, GLsizei instances, const uint32_t *list, uint32_t expanded, size_t *bytes);

/*
 * Checks a draw against the active transform feedback: glDrawArrays of a
 * mode of the capture's kind only.  *capturing is nonzero when the draw
 * captures (active, not paused, the program that began it).  Returns 0,
 * or -1 with the error recorded.
 */
int
gles_feedback_check(
	struct zegl_context *context,
	struct gles_state *state,
	GLenum mode,
	GLenum type,
	int *capturing)
{
	struct gles_feedback *feedback;
	unsigned wanted;
	unsigned kind;

	/* Nothing captured while transform feedback is not active or paused. */
	*capturing = 0;
	feedback = state->feedback;
	if (!feedback->active || feedback->paused)
		return 0;

	/* Indices are not drawn while capturing, and the mode is of the capture's kind. */
	wanted = feedback_vertices(mode);
	kind = feedback_vertices(feedback->primitive_mode);
	if (type != GL_NONE || wanted != kind) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* Succeeded: the draw captures with the program that began it. */
	if (state->program == feedback->program && feedback->program->capture_count != 0U)
		*capturing = 1;
	return 0;
}

/*
 * Makes the capture buffer of a draw with a program that captures
 * outputs, in the stream (its header says how many vertices an instance
 * has: first + count; the vertex shader writes it whether or not the draw
 * captures), after checking, for a draw that captures, that the transform
 * feedback buffers have room for the vertices it writes.  Returns 0, or
 * -1 with the error recorded.
 */
int
gles_feedback_prepare(
	struct zegl_context *context,
	struct gles_state *state,
	int capturing,
	GLint first,
	GLsizei count,
	GLsizei instances,
	uint32_t expanded,
	struct gles_capture_target *capture)
{
	struct gles_program *program;
	uint32_t *header;
	size_t vertices;
	size_t bytes;
	size_t room;
	size_t alignment;
	unsigned index;

	/* The vertices written. */
	program = state->program;
	vertices = (size_t)expanded * (size_t)instances;

	/* Room when capturing: one buffer for every output interleaved, else one per output. */
	if (!capturing) {
		bytes = 0U;
	} else if (program->capture_mode == GL_INTERLEAVED_ATTRIBS) {
		bytes = vertices * program->capture_stride * 4U;
		room = feedback_room(&state->feedback_ranges[0], state->feedback->written[0]);
		if (bytes > room) {
			gles_error(context, GL_INVALID_OPERATION);
			return -1;
		}
	} else {
		for (index = 0U; index < program->capture_count; index++) {
			bytes = vertices * program->captures[index].words * 4U;
			room = feedback_room(&state->feedback_ranges[index], state->feedback->written[index]);
			if (bytes > room) {
				gles_error(context, GL_INVALID_OPERATION);
				return -1;
			}
		}
	}

	/* The capture buffer: the header, then a record for every vertex number of every instance. */
	capture->vertices = (uint32_t)first + (uint32_t)count;
	bytes = ((size_t)GLES_CAPTURE_HEADER + (size_t)instances * capture->vertices * program->capture_stride) * 4U;
	alignment = (size_t)state->limits.minStorageBufferOffsetAlignment;
	if (alignment < 16U)
		alignment = 16U;
	header = gles_stream(state, bytes, alignment, &capture->buffer.buffer, &capture->buffer.offset);
	if (header == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* The header: the vertices an instance has. */
	memset(header, 0, GLES_CAPTURE_HEADER * 4U);
	header[0] = capture->vertices;
	capture->buffer.range = bytes;

	/* Succeeded: the capture buffer. */
	return 0;
}

/*
 * Copies a capturing draw's records into the transform feedback buffers
 * in GL's order (the draw's list of vertices, or first on), outside the
 * render pass, and counts what was written.
 */
void
gles_feedback_record(
	struct zegl_context *context,
	struct gles_state *state,
	const struct gles_capture_target *capture,
	GLint first,
	GLsizei instances,
	const uint32_t *list,
	uint32_t expanded)
{
	struct zegl_surface *surface;
	struct gles_program *program;
	struct gles_feedback *feedback;
	VkMemoryBarrier barrier;
	size_t bytes;
	unsigned index;
	unsigned vertices;
	int status;

	/* The frame's command buffer, with no pass open (copies are made outside one). */
	surface = context->draw;
	program = state->feedback->program;
	feedback = state->feedback;
	gles_queries_suspend(state);
	gles_target_close(state);
	zegl_frame_leave_pass(surface);

	/* The vertex shader's writes, seen by the copies. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(surface->command, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			     0U, 1U, &barrier, 0U, NULL, 0U, NULL);

	/* Every output into the first buffer, or each into its own. */
	if (program->capture_mode == GL_INTERLEAVED_ATTRIBS) {
		status = feedback_copy(state, surface->command, capture, &state->feedback_ranges[0], feedback->written[0], 0U,
				       program->capture_stride, first, instances, list, expanded, &bytes);
		if (status == 0)
			feedback->written[0] += bytes;
	} else {
		for (index = 0U; index < program->capture_count; index++) {
			status = feedback_copy(state, surface->command, capture, &state->feedback_ranges[index], feedback->written[index],
					       program->captures[index].offset, program->captures[index].words, first, instances, list,
					       expanded, &bytes);
			if (status == 0)
				feedback->written[index] += bytes;
		}
	}

	/* The copies seen by what reads the buffers after (vertices, indices, uniforms, copies). */
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
				VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(surface->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
			     0U, 1U, &barrier, 0U, NULL, 0U, NULL);
	surface->recorded = 1;

	/* The primitives written, for the query of them. */
	vertices = feedback_vertices(feedback->primitive_mode);
	gles_query_primitives(state, (GLuint)(expanded / vertices) * (GLuint)instances);
}

/*
 * Brings a buffer's bytes up to date with its device copy when transform
 * feedback wrote it, waiting for the frame that did.  Returns 0, or -1
 * with the error recorded.
 */
int
gles_buffer_fetch(
	struct zegl_context *context,
	struct gles_buffer *buffer)
{
	struct gles_state *state;
	int status;

	/* Nothing written by the device. */
	if (!buffer->gpu_written)
		return 0;

	/* The frame that wrote it done. */
	state = gles_state(context);
	if (state == NULL)
		return -1;
	status = gles_frame_wait(context, state, buffer->used);
	if (status != 0)
		return -1;

	/* The device copy's bytes (host visible) are the buffer's. */
	if (buffer->mapped != NULL && buffer->device_size >= buffer->size && buffer->size != 0U)
		memcpy(buffer->data, buffer->mapped, buffer->size);

	/* Succeeded: the bytes are the newest again. */
	buffer->gpu_written = 0;
	return 0;
}

/*
 * Frees a context's transform feedback objects and their namespace.
 */
void
gles_feedbacks_release(
	struct gles_state *state)
{
	GLuint name;

	/* Each object. */
	for (name = 1U; name < state->feedbacks.capacity; name++)
		free(state->feedbacks.objects[name]);

	/* The namespace. */
	free(state->feedbacks.objects);
	memset(&state->feedbacks, 0, sizeof(state->feedbacks));
	state->feedback = &state->default_feedback;
}

/*
 * Makes names for transform feedback objects.
 */
GL_APICALL void GL_APIENTRY
glGenTransformFeedbacks(
	GLsizei n,
	GLuint *ids)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_feedback *feedback;
	GLsizei index;
	int status;

	/* A context with its state and a count. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name gets its object now, so the next name is another. */
	for (index = 0; index < n; index++) {
		feedback = calloc(1U, sizeof(*feedback));
		if (feedback == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* Under the first free name. */
		feedback->name = gles_names_free(&state->feedbacks);
		status = gles_names_add(&state->feedbacks, feedback->name, feedback);
		if (status != 0) {
			free(feedback);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name goes back to the application. */
		ids[index] = feedback->name;
	}
}

/*
 * Deletes transform feedback objects (not an active one); a bound one
 * gives way to the default.
 */
GL_APICALL void GL_APIENTRY
glDeleteTransformFeedbacks(
	GLsizei n,
	const GLuint *ids)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_feedback *feedback;
	GLsizei index;

	/* A context with its state and a count. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name that is an object, not active. */
	for (index = 0; index < n; index++) {
		feedback = feedback_named(state, ids[index]);
		if (feedback == NULL || feedback == &state->default_feedback)
			continue;
		if (feedback->active) {
			gles_error(context, GL_INVALID_OPERATION);
			continue;
		}

		/* The default one bound in its place. */
		if (state->feedback == feedback) {
			memcpy(state->feedback_ranges, state->default_feedback.ranges, sizeof(state->feedback_ranges));
			state->feedback = &state->default_feedback;
		}

		/* The name and the object go. */
		gles_names_remove(&state->feedbacks, ids[index]);
		free(feedback);
	}
}

/*
 * Reports whether a name is a transform feedback object (one bound at
 * least once).
 */
GL_APICALL GLboolean GL_APIENTRY
glIsTransformFeedback(
	GLuint id)
{
	struct gles_state *state;
	struct gles_feedback *feedback;

	/* A context with its state. */
	state = gles_state(gles_context());
	if (state == NULL || id == 0U)
		return GL_FALSE;

	/* A name bound once. */
	feedback = feedback_named(state, id);
	if (feedback == NULL || !feedback->bound)
		return GL_FALSE;

	/* Succeeded: it is one. */
	return GL_TRUE;
}

/*
 * Binds a transform feedback object (0: the default one); its binding
 * points become the context's.
 */
GL_APICALL void GL_APIENTRY
glBindTransformFeedback(
	GLenum target,
	GLuint id)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_feedback *feedback;

	/* A context with its state, the one target, and no capture going on. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_TRANSFORM_FEEDBACK) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Not while capturing. */
	if (state->feedback->active && !state->feedback->paused) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* A name glGenTransformFeedbacks made. */
	feedback = feedback_named(state, id);
	if (feedback == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The bound one keeps its binding points; the new one's are the context's. */
	memcpy(state->feedback->ranges, state->feedback_ranges, sizeof(state->feedback_ranges));
	memcpy(state->feedback_ranges, feedback->ranges, sizeof(state->feedback_ranges));
	feedback->bound = 1;
	state->feedback = feedback;
}

/*
 * Starts capturing primitives of a mode (points, lines or triangles) with
 * the current program into the bound transform feedback buffers.
 */
GL_APICALL void GL_APIENTRY
glBeginTransformFeedback(
	GLenum primitiveMode)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	unsigned needed;
	unsigned index;

	/* A context with its state and a mode. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (primitiveMode != GL_POINTS && primitiveMode != GL_LINES && primitiveMode != GL_TRIANGLES) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Not active yet, with a linked program that captures outputs. */
	program = state->program;
	if (state->feedback->active || program == NULL || !program->linked || program->capture_count == 0U) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* A buffer at every binding point the capture writes. */
	needed = 1U;
	if (program->capture_mode == GL_SEPARATE_ATTRIBS)
		needed = program->capture_count;
	for (index = 0U; index < needed; index++) {
		if (state->feedback_ranges[index].buffer == NULL) {
			gles_error(context, GL_INVALID_OPERATION);
			return;
		}
	}

	/* Active, writing from the ranges' starts. */
	state->feedback->active = 1;
	state->feedback->paused = 0;
	state->feedback->primitive_mode = primitiveMode;
	state->feedback->program = program;
	memset(state->feedback->written, 0, sizeof(state->feedback->written));
}

/*
 * Ends capturing.
 */
GL_APICALL void GL_APIENTRY
glEndTransformFeedback(void)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, capturing. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (!state->feedback->active) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Not active. */
	state->feedback->active = 0;
	state->feedback->paused = 0;
	state->feedback->program = NULL;
}

/*
 * Pauses capturing.
 */
GL_APICALL void GL_APIENTRY
glPauseTransformFeedback(void)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, capturing and not paused. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (!state->feedback->active || state->feedback->paused) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Paused. */
	state->feedback->paused = 1;
}

/*
 * Resumes paused capturing.
 */
GL_APICALL void GL_APIENTRY
glResumeTransformFeedback(void)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, capturing and paused. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (!state->feedback->active || !state->feedback->paused) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Capturing again. */
	state->feedback->paused = 0;
}

/* Returns the transform feedback object of a name (0: the default one), or NULL. */
static struct gles_feedback *
feedback_named(
	struct gles_state *state,
	GLuint name)
{
	struct gles_feedback *feedback;

	/* Name 0 is the default one. */
	if (name == 0U)
		return &state->default_feedback;

	/* The name's object. */
	feedback = gles_names_get(&state->feedbacks, name);
	if (feedback == NULL)
		return NULL;

	/* Succeeded: the object. */
	return feedback;
}

/* Returns the vertices of a primitive of a mode's kind: 1 for points, 2 for lines, 3 for triangles (0 for a name that is not a mode). */
static unsigned
feedback_vertices(
	GLenum mode)
{
	/* The modes by kind. */
	switch (mode) {
	case GL_POINTS:
		return 1U;
	case GL_LINES:
	case GL_LINE_LOOP:
	case GL_LINE_STRIP:
		return 2U;
	case GL_TRIANGLES:
	case GL_TRIANGLE_STRIP:
	case GL_TRIANGLE_FAN:
		return 3U;
	default:
		break;
	}

	/* Not a mode. */
	return 0U;
}

/* Returns the bytes left in a binding point's range after what was written (a range of size 0 runs to the buffer's end). */
static size_t
feedback_room(
	const struct gles_buffer_range *range,
	size_t written)
{
	size_t size;

	/* No buffer has no room. */
	if (range->buffer == NULL || range->offset > range->buffer->size)
		return 0U;

	/* The range's size, or the rest of the buffer. */
	size = range->size;
	if (size == 0U || range->offset + size > range->buffer->size)
		size = range->buffer->size - range->offset;
	if (written >= size)
		return 0U;

	/* Succeeded: what is left. */
	return size - written;
}

/*
 * Copies words (from offset in each record) of a draw's records into a
 * binding point's buffer after what was written, in GL's order of
 * vertices, instance after instance; *bytes receives the bytes written.
 * Returns 0, or -1 when the buffer has no device copy or there is no
 * memory.
 */
static int
feedback_copy(
	struct gles_state *state,
	VkCommandBuffer command,
	const struct gles_capture_target *capture,
	struct gles_buffer_range *range,
	size_t written,
	unsigned offset,
	unsigned words,
	GLint first,
	GLsizei instances,
	const uint32_t *list,
	uint32_t expanded,
	size_t *bytes)
{
	struct gles_buffer *buffer;
	struct gles_program *program;
	VkBufferCopy *regions;
	VkDeviceSize source;
	VkDeviceSize target;
	uint32_t count;
	uint32_t vertex;
	uint32_t item;
	GLsizei instance;
	int status;

	/* The buffer's device copy, up to date with its bytes. */
	*bytes = 0U;
	buffer = range->buffer;
	program = state->feedback->program;
	status = gles_buffer_sync(state, buffer);
	if (status != 0 || buffer->buffer == VK_NULL_HANDLE)
		return -1;

	/* A region per vertex at most. */
	regions = malloc((size_t)expanded * (size_t)instances * sizeof(*regions) + sizeof(*regions));
	if (regions == NULL)
		return -1;

	/* Each vertex written: its record's words, after the ones before (a run of vertices one after another is one region). */
	count = 0U;
	target = range->offset + written;
	for (instance = 0; instance < instances; instance++) {
		for (item = 0U; item < expanded; item++) {
			vertex = (uint32_t)first + item;
			if (list != NULL)
				vertex = list[item];
			source = capture->buffer.offset +
				 ((VkDeviceSize)GLES_CAPTURE_HEADER +
				  ((VkDeviceSize)instance * capture->vertices + vertex) * program->capture_stride + offset) * 4U;

			/* Joined to the region before when it follows it in both buffers. */
			if (count != 0U &&
			    words == program->capture_stride &&
			    regions[count - 1U].srcOffset + regions[count - 1U].size == source &&
			    regions[count - 1U].dstOffset + regions[count - 1U].size == target) {
				regions[count - 1U].size += (VkDeviceSize)words * 4U;
			} else {
				regions[count].srcOffset = source;
				regions[count].dstOffset = target;
				regions[count].size = (VkDeviceSize)words * 4U;
				count++;
			}

			/* The next vertex's place. */
			target += (VkDeviceSize)words * 4U;
		}
	}

	/* The copies. */
	vkCmdCopyBuffer(command, capture->buffer.buffer, buffer->buffer, count, regions);
	free(regions);

	/*
	 * The device copy now has what the CPU's bytes lack (gles_buffer_fetch
	 * reads it back), and waits for the frame before it may go.
	 */
	buffer->gpu_written = 1;
	buffer->used = state->frame;

	/* Succeeded: the bytes written. */
	*bytes = (size_t)expanded * (size_t)instances * words * 4U;
	return 0;
}
