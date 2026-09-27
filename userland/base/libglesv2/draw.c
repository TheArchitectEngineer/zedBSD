/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Drawing in zedBSD's OpenGL ES (WS068 p008, p024): vertex arrays,
 * pipelines, glDrawArrays and glDrawElements and their instanced forms,
 * glClear and glReadPixels, recorded into the draw surface's frame
 * (libEGL, vulkan.c).
 *
 * A draw reads its indices on the CPU (buffer objects keep their bytes
 * there): triangle strips and fans, line strips and loops become lists,
 * byte indices become 32-bit ones, the fixed restart index splits them
 * when GL_PRIMITIVE_RESTART_FIXED_INDEX is on, and the largest index
 * bounds the client arrays copied into the stream.  An attribute whose
 * format the device cannot fetch is converted to floats (or to 32-bit
 * integers for glVertexAttribIPointer's arrays).  An array with a divisor
 * is read per instance: divisor 1 is Vulkan's instance rate as it is, a
 * larger divisor is spread into the stream so that each instance has its
 * element.  Each draw gets a descriptor set with a copy of the default
 * uniform block, the samplers, and the ranges of the buffers bound to
 * the named uniform blocks' binding points.
 */

#include "gles.h"

#include <stdlib.h>
#include <string.h>

/* The descriptor sets and descriptors of one pool. */
#define DRAW_POOL_SETS		256U

static void draw_primitives(GLenum mode, GLint first, GLsizei count, GLenum type, const void *indices, GLsizei instances);
static int draw_indices(struct zegl_context *context, GLsizei count, GLenum type, const void *indices, uint32_t **out, uint32_t *largest, int *restarted);
static uint32_t *draw_restart(GLenum mode, const uint32_t *indices, GLsizei count, uint32_t restart, int rotate, uint32_t *expanded);
static uint32_t draw_restart_index(const struct gles_state *state, GLenum type);
static void draw_base_vertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instances, GLint basevertex);
static void draw_program(GLenum mode, GLint first, GLsizei count, GLenum type, const void *indices, GLsizei instances, int flat);
static int draw_topology(GLenum mode, uint32_t *topology, int *strip);
static int draw_vertices(struct gles_state *state, uint32_t vertices, uint32_t instances, struct gles_vertex_layout *layout, VkBuffer *buffers, VkDeviceSize *offsets);
static int draw_current(struct gles_state *state, const struct gles_attrib *attrib, GLenum attribute_type, VkBuffer *buffer, VkDeviceSize *offset, uint32_t *format);
static int draw_spread(struct gles_state *state, const struct gles_attrib *attrib, unsigned base, const unsigned char *source, size_t stride, uint32_t elements, uint32_t repeat, int fetchable, VkBuffer *buffer, VkDeviceSize *offset, uint32_t *format, uint32_t *element_size);
static int draw_signed(GLenum type);
static VkFormat draw_format(GLint size, GLenum type, GLboolean normalized, int integer, size_t *bytes);
static int draw_format_ok(struct gles_state *state, VkFormat format);
static unsigned draw_attribute_base(GLenum type);
static float draw_component(const unsigned char *element, GLenum type, GLboolean normalized, GLint component);
static uint32_t draw_integer(const unsigned char *element, GLenum type, GLint component);
static unsigned draw_sampler_shape(GLenum type);
static unsigned draw_sampler_kind(GLenum type);
static int draw_texture_matches(const struct gles_texture *texture, unsigned kind);
static void draw_raster(struct gles_state *state, const struct gles_target *target, uint32_t topology, struct gles_raster *raster);
static uint32_t draw_channels(const struct gles_state *state, unsigned index);
static VkPipeline draw_pipeline(struct gles_state *state, const struct gles_target *target, const struct gles_raster *raster, const struct gles_vertex_layout *layout);
static int draw_blocks(struct zegl_context *context, struct gles_state *state, VkDescriptorBufferInfo *blocks);
static VkDescriptorSet draw_descriptors(struct gles_state *state, const VkDescriptorBufferInfo *blocks, const VkDescriptorBufferInfo *capture, uint32_t *offset);
static void draw_vertex_attrib_integer(GLuint index, GLenum type, const uint32_t *values);
static void draw_dynamic(struct gles_state *state, struct zegl_context *context, const struct gles_target *target);
static VkRect2D draw_scissor_rect(struct gles_state *state, const struct gles_target *target);
static void draw_clear_buffer(GLenum buffer, GLint drawbuffer, const VkClearValue *value, VkImageAspectFlags aspects);
static VkBlendFactor draw_blend_factor(GLenum factor);
static VkBlendOp draw_blend_op(GLenum equation);
static VkStencilOp draw_stencil_op(GLenum op);

/*
 * Forgets the pipelines made for a program's link (they wait for the
 * frame).
 */
void
gles_pipelines_forget(
	struct gles_state *state,
	uint64_t program)
{
	struct gles_pipeline **link;
	struct gles_pipeline *pipeline;
	struct gles_garbage objects;

	/* Each pipeline of the program leaves the list. */
	link = &state->pipelines;
	while (*link != NULL) {
		pipeline = *link;
		if (pipeline->key.program != program) {
			link = &pipeline->next;
			continue;
		}

		/* It waits for the frame. */
		*link = pipeline->next;
		memset(&objects, 0, sizeof(objects));
		objects.pipeline = pipeline->pipeline;
		gles_garbage_keep(state, &objects);
		free(pipeline);
	}
}

/*
 * Reads a rectangle of the read framebuffer's read buffer (the read
 * surface's image, or a framebuffer object's colour attachment) as RGBA8
 * rows from the bottom up, waiting for what the frame drew.  Pixels
 * outside it are black.  Returns 0, or -1 with the error recorded (an
 * integer attachment cannot be read so).
 */
int
gles_read_rgba(
	struct zegl_context *context,
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height,
	unsigned char *rows)
{
	int status;

	/* The rows as RGBA unsigned bytes (a float buffer's clamped). */
	status = gles_read_pixels(context, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 1, rows);
	if (status != 0)
		return -1;

	/* Succeeded: the rows. */
	return 0;
}

/*
 * Reads a rectangle of the read framebuffer's read buffer as tight rows
 * of the application's pixels of a format and type (which the read
 * buffer's format must allow, gles_read_format_ok, unless clamped is
 * nonzero and they are RGBA bytes of a normalized or float buffer: a
 * copy into a texture's) from the bottom up, waiting for what the frame
 * drew.  Pixels outside it are zeros.  Returns 0, or -1 with the error
 * recorded.
 */
int
gles_read_pixels(
	struct zegl_context *context,
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height,
	GLenum format,
	GLenum type,
	int clamped,
	unsigned char *rows)
{
	struct gles_state *state;
	struct gles_read read;
	VkImageMemoryBarrier barrier;
	VkBufferImageCopy copy;
	VkBuffer buffer;
	VkDeviceSize offset;
	unsigned char *mapped;
	unsigned char *swapped;
	unsigned char red;
	const unsigned char *source;
	size_t texel;
	size_t pixel;
	size_t span;
	GLint left;
	GLint bottom;
	GLint right;
	GLint top;
	GLint row;
	GLint column;
	int allowed;
	int status;
	EGLint error;

	/* A context with its state. */
	state = gles_state(context);
	if (state == NULL)
		return -1;

	/* What the read buffer is, in a frame with no pass open. */
	status = gles_read_source(context, state, &read);
	if (status != 0)
		return -1;

	/* A format and type its texels can be read as (a copy reads any colour that is not integers as bytes). */
	allowed = gles_read_format_ok(read.format, format, type);
	if (clamped && read.format->kind == GLES_TEXEL_FLOAT)
		allowed = 1;
	if (!allowed) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* Everything starts as zeros; only the part inside the image is copied. */
	texel = read.format->bytes;
	pixel = gles_pixel_size(format, type);
	memset(rows, 0, (size_t)width * (size_t)height * pixel);
	left = x;
	if (left < 0)
		left = 0;
	bottom = y;
	if (bottom < 0)
		bottom = 0;
	right = x + width;
	if (right > (GLint)read.extent.width)
		right = (GLint)read.extent.width;
	top = y + height;
	if (top > (GLint)read.extent.height)
		top = (GLint)read.extent.height;
	if (left >= right || bottom >= top)
		return 0;

	/* Room in the stream for the copy. */
	span = (size_t)(right - left);
	mapped = gles_stream(state, span * (size_t)(top - bottom) * texel, 16U, &buffer, &offset);
	if (mapped == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* The image copied out between two layout changes. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.oldLayout = read.layout;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = read.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = read.level;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.baseArrayLayer = read.layer;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(read.surface->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			     0U, 0U, NULL, 0U, NULL, 1U, &barrier);

	/* The rectangle (a window's rows go down from its top, a framebuffer object's are GL's). */
	memset(&copy, 0, sizeof(copy));
	copy.bufferOffset = offset;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.mipLevel = read.level;
	copy.imageSubresource.baseArrayLayer = read.layer;
	copy.imageSubresource.layerCount = 1U;
	copy.imageOffset.x = left;
	copy.imageOffset.y = bottom;
	copy.imageOffset.z = (int32_t)read.slice;
	if (read.flip)
		copy.imageOffset.y = (int32_t)read.extent.height - top;
	copy.imageExtent.width = (uint32_t)(right - left);
	copy.imageExtent.height = (uint32_t)(top - bottom);
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(read.surface->command, read.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1U, &copy);

	/* Back to where the image rests. */
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = 0U;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = read.layout;
	vkCmdPipelineBarrier(read.surface->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			     0U, 0U, NULL, 0U, NULL, 1U, &barrier);
	read.surface->recorded = 1;

	/* Done before the bytes are read. */
	error = zegl_frame_flush(read.surface);
	if (error != EGL_SUCCESS) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* A window's BGRA bytes become RGBA, in place. */
	if (read.swizzle) {
		for (swapped = mapped; swapped < mapped + span * (size_t)(top - bottom) * texel; swapped += 4) {
			red = swapped[2];
			swapped[2] = swapped[0];
			swapped[0] = red;
		}
	}

	/* Each row into GL's order (bottom up), its texels in the application's form. */
	for (row = bottom; row < top; row++) {
		source = mapped + (size_t)(row - bottom) * span * texel;
		if (read.flip)
			source = mapped + (size_t)(top - 1 - row) * span * texel;
		column = left;
		gles_texels_read(read.format,
				 source,
				 span,
				 format,
				 type,
				 rows + ((size_t)(row - y) * (size_t)width + (size_t)(column - x)) * pixel);
	}

	/* Everything recorded before is done: the frame's resources are free again. */
	state->frame++;
	gles_collect(state);

	/* Succeeded: the rows. */
	return 0;
}
/*
 * Draws primitives from the enabled arrays, vertices first to first +
 * count - 1.
 */
GL_APICALL void GL_APIENTRY
glDrawArrays(
	GLenum mode,
	GLint first,
	GLsizei count)
{
	/* No indices, one instance. */
	draw_primitives(mode, first, count, GL_NONE, NULL, 1);
}

/*
 * Draws primitives from the enabled arrays with count indices.
 */
GL_APICALL void GL_APIENTRY
glDrawElements(
	GLenum mode,
	GLsizei count,
	GLenum type,
	const void *indices)
{
	/* The indices of the type, one instance. */
	if (type == GL_NONE)
		type = GL_INVALID_ENUM;
	draw_primitives(mode, 0, count, type, indices, 1);
}

/*
 * Draws primitives from the enabled arrays with count indices that lie
 * between start and end (the range is only a hint).
 */
GL_APICALL void GL_APIENTRY
glDrawRangeElements(
	GLenum mode,
	GLuint start,
	GLuint end,
	GLsizei count,
	GLenum type,
	const void *indices)
{
	struct zegl_context *context;

	/* A range that does not end before it starts. */
	if (end < start) {
		context = gles_context();
		if (context != NULL)
			gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The indices of the type, one instance. */
	if (type == GL_NONE)
		type = GL_INVALID_ENUM;
	draw_primitives(mode, 0, count, type, indices, 1);
}

/*
 * Draws instances of primitives from the enabled arrays, vertices first
 * to first + count - 1.
 */
GL_APICALL void GL_APIENTRY
glDrawArraysInstanced(
	GLenum mode,
	GLint first,
	GLsizei count,
	GLsizei instancecount)
{
	/* No indices. */
	draw_primitives(mode, first, count, GL_NONE, NULL, instancecount);
}

/*
 * Draws instances of primitives from the enabled arrays with count
 * indices.
 */
GL_APICALL void GL_APIENTRY
glDrawElementsInstanced(
	GLenum mode,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLsizei instancecount)
{
	/* The indices of the type. */
	if (type == GL_NONE)
		type = GL_INVALID_ENUM;
	draw_primitives(mode, 0, count, type, indices, instancecount);
}

/*
 * Draws primitives from the enabled arrays with count indices, each
 * added to a base vertex (desktop GL 3.2, libGL).
 */
GL_APICALL void GL_APIENTRY
glDrawElementsBaseVertex(
	GLenum mode,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLint basevertex)
{
	/* One instance. */
	draw_base_vertex(mode, count, type, indices, 1, basevertex);
}

/*
 * Draws primitives from the enabled arrays with count indices that lie
 * between start and end (a hint), each added to a base vertex.
 */
GL_APICALL void GL_APIENTRY
glDrawRangeElementsBaseVertex(
	GLenum mode,
	GLuint start,
	GLuint end,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLint basevertex)
{
	struct zegl_context *context;

	/* A range that does not end before it starts. */
	if (end < start) {
		context = gles_context();
		if (context != NULL)
			gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* One instance. */
	draw_base_vertex(mode, count, type, indices, 1, basevertex);
}

/*
 * Draws instances of primitives from the enabled arrays with count
 * indices, each added to a base vertex.
 */
GL_APICALL void GL_APIENTRY
glDrawElementsInstancedBaseVertex(
	GLenum mode,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLsizei instancecount,
	GLint basevertex)
{
	/* The instances. */
	draw_base_vertex(mode, count, type, indices, instancecount, basevertex);
}

/*
 * Clears the buffers of the frame in the mask (inside the scissor box
 * when the scissor test is on): every colour attachment a draw buffer
 * writes, and the depth and stencil buffer.
 */
GL_APICALL void GL_APIENTRY
glClear(
	GLbitfield mask)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct zegl_surface *surface;
	struct gles_target target;
	VkClearAttachment attachments[GLES_COLOR_ATTACHMENTS + 1U];
	VkClearValue values[2];
	VkClearRect rect;
	const VkClearValue *clear;
	uint32_t count;
	uint32_t channels;
	unsigned index;
	unsigned channel;
	EGLint error;
	int status;

	/* A context with its state, and a mask of the three buffers. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if ((mask & ~(GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0U) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Discarded rasterization clears nothing, nor does a conditional rendering whose query saw nothing. */
	if (state->rasterizer_discard || state->conditional_skip)
		return;

	/* A draw surface, whose frame records the clear. */
	surface = context->draw;
	if (surface == NULL) {
		gles_error(context, GL_INVALID_FRAMEBUFFER_OPERATION);
		return;
	}

	/* The frame, opened when this is its first command. */
	error = zegl_frame_begin(surface);
	if (error != EGL_SUCCESS) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The values. */
	memset(values, 0, sizeof(values));
	memcpy(values[0].color.float32, context->gles.clear_color, 4U * sizeof(float));
	values[1].depthStencil.depth = state->clear_depth;
	values[1].depthStencil.stencil = (uint32_t)state->clear_stencil & 0xffU;

	/* A whole clear of the surface at the frame's start is the first pass's own. */
	clear = NULL;
	if (state->framebuffer == 0U &&
	    state->default_draw_buffer == GL_BACK &&
	    surface->passes == 0U &&
	    !state->scissor_test &&
	    !state->indexed_masked &&
	    state->color_mask[0] &&
	    state->color_mask[1] &&
	    state->color_mask[2] &&
	    state->color_mask[3])
		clear = values;

	/* The target's pass, which a whole clear has just cleared. */
	status = gles_target_open(context, state, clear, &target);
	if (status != 0)
		return;
	if (clear != NULL)
		return;

	/* Each colour attachment a draw buffer writes (integers get the colour's values as integers). */
	count = 0U;
	memset(attachments, 0, sizeof(attachments));
	for (index = 0U; (mask & GL_COLOR_BUFFER_BIT) != 0U && index < target.color_count; index++) {
		if ((target.draw_mask & (1U << index)) == 0U)
			continue;

		/* Not an attachment whose draw buffer's colour mask writes no channel. */
		channels = draw_channels(state, index);
		if (channels == 0U)
			continue;

		/* The attachment and its value. */
		attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachments[count].colorAttachment = index;
		attachments[count].clearValue = values[0];
		if ((target.integer_mask & (1U << index)) != 0U) {
			for (channel = 0U; channel < 4U; channel++)
				attachments[count].clearValue.color.int32[channel] = (int32_t)values[0].color.float32[channel];
		}

		/* Counted. */
		count++;
	}

	/* The depth and stencil aspects in the mask, when the target has a depth buffer. */
	if (target.depth_aspects != 0U && (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) != 0U) {
		if ((mask & GL_DEPTH_BUFFER_BIT) != 0U && state->depth_mask)
			attachments[count].aspectMask |= target.depth_aspects & VK_IMAGE_ASPECT_DEPTH_BIT;
		if ((mask & GL_STENCIL_BUFFER_BIT) != 0U)
			attachments[count].aspectMask |= target.depth_aspects & VK_IMAGE_ASPECT_STENCIL_BIT;
		attachments[count].clearValue = values[1];
		if (attachments[count].aspectMask != 0U)
			count++;
	}

	/* Nothing in the mask the target has. */
	if (count == 0U)
		return;

	/* The clear over the scissor box or the whole target. */
	memset(&rect, 0, sizeof(rect));
	rect.rect = draw_scissor_rect(state, &target);
	rect.layerCount = 1U;
	if (rect.rect.extent.width == 0U || rect.rect.extent.height == 0U)
		return;
	vkCmdClearAttachments(target.command, count, attachments, 1U, &rect);
}

/*
 * Clears the colour attachment a draw buffer writes to float values, or
 * (GL_DEPTH) the depth buffer.
 */
GL_APICALL void GL_APIENTRY
glClearBufferfv(
	GLenum buffer,
	GLint drawbuffer,
	const GLfloat *value)
{
	VkClearValue clear;

	/* A colour's four floats, or the depth. */
	memset(&clear, 0, sizeof(clear));
	if (buffer == GL_COLOR) {
		memcpy(clear.color.float32, value, 4U * sizeof(float));
		draw_clear_buffer(buffer, drawbuffer, &clear, VK_IMAGE_ASPECT_COLOR_BIT);
		return;
	}

	/* The depth (GL_STENCIL and the others are not float buffers). */
	clear.depthStencil.depth = value[0];
	if (buffer != GL_DEPTH)
		buffer = GL_INVALID_ENUM;
	draw_clear_buffer(buffer, drawbuffer, &clear, VK_IMAGE_ASPECT_DEPTH_BIT);
}

/*
 * Clears the colour attachment a draw buffer writes to signed integers,
 * or (GL_STENCIL) the stencil buffer.
 */
GL_APICALL void GL_APIENTRY
glClearBufferiv(
	GLenum buffer,
	GLint drawbuffer,
	const GLint *value)
{
	VkClearValue clear;

	/* A colour's four integers, or the stencil. */
	memset(&clear, 0, sizeof(clear));
	if (buffer == GL_COLOR) {
		memcpy(clear.color.int32, value, 4U * sizeof(int32_t));
		draw_clear_buffer(buffer, drawbuffer, &clear, VK_IMAGE_ASPECT_COLOR_BIT);
		return;
	}

	/* The stencil (GL_DEPTH and the others are not integer buffers). */
	clear.depthStencil.stencil = (uint32_t)value[0] & 0xffU;
	if (buffer != GL_STENCIL)
		buffer = GL_INVALID_ENUM;
	draw_clear_buffer(buffer, drawbuffer, &clear, VK_IMAGE_ASPECT_STENCIL_BIT);
}

/*
 * Clears the colour attachment a draw buffer writes to unsigned integers.
 */
GL_APICALL void GL_APIENTRY
glClearBufferuiv(
	GLenum buffer,
	GLint drawbuffer,
	const GLuint *value)
{
	VkClearValue clear;

	/* A colour's four unsigned integers (the others are not unsigned buffers). */
	memset(&clear, 0, sizeof(clear));
	memcpy(clear.color.uint32, value, 4U * sizeof(uint32_t));
	if (buffer != GL_COLOR)
		buffer = GL_INVALID_ENUM;
	draw_clear_buffer(buffer, drawbuffer, &clear, VK_IMAGE_ASPECT_COLOR_BIT);
}

/*
 * Clears the depth and the stencil buffer together (GL_DEPTH_STENCIL).
 */
GL_APICALL void GL_APIENTRY
glClearBufferfi(
	GLenum buffer,
	GLint drawbuffer,
	GLfloat depth,
	GLint stencil)
{
	VkClearValue clear;

	/* The depth and the stencil. */
	memset(&clear, 0, sizeof(clear));
	clear.depthStencil.depth = depth;
	clear.depthStencil.stencil = (uint32_t)stencil & 0xffU;
	if (buffer != GL_DEPTH_STENCIL)
		buffer = GL_INVALID_ENUM;
	draw_clear_buffer(buffer, drawbuffer, &clear, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

/*
 * Reads a rectangle of the read framebuffer's read buffer as pixels of a
 * format and type: RGBA unsigned bytes from a normalized buffer, or the
 * buffer format's own pair (GL_IMPLEMENTATION_COLOR_READ_FORMAT, _TYPE).
 */
GL_APICALL void GL_APIENTRY
glReadPixels(
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height,
	GLenum format,
	GLenum type,
	void *pixels)
{
	struct zegl_context *context;
	struct gles_state *state;
	unsigned char *rows;
	unsigned char *base;
	size_t stride;
	size_t pixel;
	GLsizei row;
	GLenum error;
	int status;

	/* A context with its state, and a format and type there are. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	pixel = gles_pixel_size(format, type);
	if (pixel == 0U) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A size that is not negative. */
	if (width < 0 || height < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Nothing to read. */
	if (width == 0 || height == 0)
		return;

	/* The pixels, tightly packed. */
	rows = malloc((size_t)width * (size_t)height * pixel);
	if (rows == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The rectangle, in the format and type the read buffer allows. */
	status = gles_read_pixels(context, x, y, width, height, format, type, 0, rows);
	if (status != 0) {
		free(rows);
		return;
	}

	/* Where the rows go: the application's memory or the pack buffer, by the pack alignment and the pixel store. */
	error = gles_pack_target(state, format, type, width, height, pixels, &base, &stride);
	if (error != GL_NO_ERROR) {
		free(rows);
		gles_error(context, error);
		return;
	}

	/* Each row into place. */
	for (row = 0; row < height; row++)
		memcpy(base + (size_t)row * stride, rows + (size_t)row * (size_t)width * pixel, (size_t)width * pixel);
	free(rows);
}
/*
 * Describes a vertex attribute's array.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribPointer(
	GLuint index,
	GLint size,
	GLenum type,
	GLboolean normalized,
	GLsizei stride,
	const void *pointer)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *attrib;

	/* A context with its state, an attribute, a size and a stride. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS ||
	    size < 1 ||
	    size > 4 ||
	    stride < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A type OpenGL ES 3 has (the packed ones hold four components). */
	switch (type) {
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:
	case GL_INT:
	case GL_UNSIGNED_INT:
	case GL_FIXED:
	case GL_FLOAT:
	case GL_HALF_FLOAT:
		break;
	case GL_INT_2_10_10_10_REV:
	case GL_UNSIGNED_INT_2_10_10_10_REV:
		if (size != 4) {
			gles_error(context, GL_INVALID_OPERATION);
			return;
		}

		/* Four packed components. */
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* OpenGL ES 3 has a vertex array object other than the default one read buffer objects only. */
	if (gles_fixed == NULL &&
	    state->vertex_array != 0U &&
	    state->array_buffer == NULL &&
	    pointer != NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The array, in the bound array buffer or the application's memory, read as floats. */
	attrib = &state->attribs[index];
	attrib->size = size;
	attrib->type = type;
	attrib->normalized = normalized;
	attrib->stride = stride;
	attrib->pointer = pointer;
	attrib->buffer = state->array_buffer;
	attrib->integer = 0;
}

/*
 * Describes a vertex attribute's array of integers, which the shader
 * reads as they are (an int or uint input).
 */
GL_APICALL void GL_APIENTRY
glVertexAttribIPointer(
	GLuint index,
	GLint size,
	GLenum type,
	GLsizei stride,
	const void *pointer)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *attrib;

	/* A context with its state, an attribute, a size and a stride. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS ||
	    size < 1 ||
	    size > 4 ||
	    stride < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* An integer type. */
	switch (type) {
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:
	case GL_INT:
	case GL_UNSIGNED_INT:
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A vertex array object other than the default one reads buffer objects only. */
	if (gles_fixed == NULL &&
	    state->vertex_array != 0U &&
	    state->array_buffer == NULL &&
	    pointer != NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The array, in the bound array buffer or the application's memory, read as integers. */
	attrib = &state->attribs[index];
	attrib->size = size;
	attrib->type = type;
	attrib->normalized = GL_FALSE;
	attrib->stride = stride;
	attrib->pointer = pointer;
	attrib->buffer = state->array_buffer;
	attrib->integer = 1;
}

/*
 * Makes an attribute's array advance once every divisor instances
 * instead of once per vertex (0: per vertex again).
 */
GL_APICALL void GL_APIENTRY
glVertexAttribDivisor(
	GLuint index,
	GLuint divisor)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The divisor. */
	state->attribs[index].divisor = divisor;
}

/*
 * Makes an attribute read its array.
 */
GL_APICALL void GL_APIENTRY
glEnableVertexAttribArray(
	GLuint index)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Enabled. */
	state->attribs[index].enabled = 1;
}

/*
 * Makes an attribute take its current value instead of its array.
 */
GL_APICALL void GL_APIENTRY
glDisableVertexAttribArray(
	GLuint index)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Disabled. */
	state->attribs[index].enabled = 0;
}

/*
 * Sets an attribute's current value from four floats (the rest of the
 * glVertexAttrib calls come here).
 */
GL_APICALL void GL_APIENTRY
glVertexAttrib4f(
	GLuint index,
	GLfloat x,
	GLfloat y,
	GLfloat z,
	GLfloat w)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The value, floats. */
	state->attribs[index].value[0] = x;
	state->attribs[index].value[1] = y;
	state->attribs[index].value[2] = z;
	state->attribs[index].value[3] = w;
	state->attribs[index].value_type = GL_FLOAT;
}

/*
 * Sets an attribute's current value from four ints.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribI4i(
	GLuint index,
	GLint x,
	GLint y,
	GLint z,
	GLint w)
{
	uint32_t values[4];

	/* The ints' bits. */
	values[0] = (uint32_t)x;
	values[1] = (uint32_t)y;
	values[2] = (uint32_t)z;
	values[3] = (uint32_t)w;
	draw_vertex_attrib_integer(index, GL_INT, values);
}

/*
 * Sets an attribute's current value from four unsigned ints.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribI4ui(
	GLuint index,
	GLuint x,
	GLuint y,
	GLuint z,
	GLuint w)
{
	uint32_t values[4];

	/* The unsigned ints. */
	values[0] = x;
	values[1] = y;
	values[2] = z;
	values[3] = w;
	draw_vertex_attrib_integer(index, GL_UNSIGNED_INT, values);
}

GL_APICALL void GL_APIENTRY
glVertexAttribI4iv(
	GLuint index,
	const GLint *v)
{
	/* Four ints. */
	glVertexAttribI4i(index, v[0], v[1], v[2], v[3]);
}

GL_APICALL void GL_APIENTRY
glVertexAttribI4uiv(
	GLuint index,
	const GLuint *v)
{
	/* Four unsigned ints. */
	glVertexAttribI4ui(index, v[0], v[1], v[2], v[3]);
}

/*
 * Reports an attribute's array state, or its current value as ints.
 */
GL_APICALL void GL_APIENTRY
glGetVertexAttribIiv(
	GLuint index,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *attrib;
	float value;
	int32_t integer;
	unsigned component;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Anything but the current value is the array's state. */
	if (pname != GL_CURRENT_VERTEX_ATTRIB) {
		glGetVertexAttribiv(index, pname, params);
		return;
	}

	/* The current value: integers' bits as they are, floats converted. */
	attrib = &state->attribs[index];
	for (component = 0U; component < 4U; component++) {
		value = attrib->value[component];
		memcpy(&integer, &attrib->value[component], 4U);
		if (attrib->value_type != GL_INT && attrib->value_type != GL_UNSIGNED_INT)
			integer = (int32_t)value;
		params[component] = integer;
	}
}

/*
 * Reports an attribute's array state, or its current value as unsigned
 * ints.
 */
GL_APICALL void GL_APIENTRY
glGetVertexAttribIuiv(
	GLuint index,
	GLenum pname,
	GLuint *params)
{
	GLint values[4];
	unsigned component;

	/* The ints' bits, as unsigned. */
	memset(values, 0, sizeof(values));
	glGetVertexAttribIiv(index, pname, values);
	for (component = 0U; component < 4U; component++)
		params[component] = (GLuint)values[component];
}

GL_APICALL void GL_APIENTRY
glVertexAttrib1f(
	GLuint index,
	GLfloat x)
{
	/* y, z and w are 0, 0 and 1. */
	glVertexAttrib4f(index, x, 0.0f, 0.0f, 1.0f);
}

GL_APICALL void GL_APIENTRY
glVertexAttrib2f(
	GLuint index,
	GLfloat x,
	GLfloat y)
{
	/* z and w are 0 and 1. */
	glVertexAttrib4f(index, x, y, 0.0f, 1.0f);
}

GL_APICALL void GL_APIENTRY
glVertexAttrib3f(
	GLuint index,
	GLfloat x,
	GLfloat y,
	GLfloat z)
{
	/* w is 1. */
	glVertexAttrib4f(index, x, y, z, 1.0f);
}

GL_APICALL void GL_APIENTRY
glVertexAttrib1fv(
	GLuint index,
	const GLfloat *v)
{
	/* One value. */
	glVertexAttrib4f(index, v[0], 0.0f, 0.0f, 1.0f);
}

GL_APICALL void GL_APIENTRY
glVertexAttrib2fv(
	GLuint index,
	const GLfloat *v)
{
	/* Two values. */
	glVertexAttrib4f(index, v[0], v[1], 0.0f, 1.0f);
}

GL_APICALL void GL_APIENTRY
glVertexAttrib3fv(
	GLuint index,
	const GLfloat *v)
{
	/* Three values. */
	glVertexAttrib4f(index, v[0], v[1], v[2], 1.0f);
}

GL_APICALL void GL_APIENTRY
glVertexAttrib4fv(
	GLuint index,
	const GLfloat *v)
{
	/* Four values. */
	glVertexAttrib4f(index, v[0], v[1], v[2], v[3]);
}

/*
 * Reports an attribute's array state or current value as floats.
 */
GL_APICALL void GL_APIENTRY
glGetVertexAttribfv(
	GLuint index,
	GLenum pname,
	GLfloat *params)
{
	GLint value;
	struct zegl_context *context;
	struct gles_state *state;

	/* The current value is floats already. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The rest as an integer. */
	if (pname == GL_CURRENT_VERTEX_ATTRIB) {
		memcpy(params, state->attribs[index].value, 4U * sizeof(float));
		return;
	}

	/* The rest as an integer. */
	value = 0;
	glGetVertexAttribiv(index, pname, &value);
	params[0] = (GLfloat)value;
}

/*
 * Reports an attribute's array state or current value as integers.
 */
GL_APICALL void GL_APIENTRY
glGetVertexAttribiv(
	GLuint index,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *attrib;
	unsigned component;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The attribute. */
	attrib = &state->attribs[index];

	/* The state asked for. */
	switch (pname) {
	case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
		*params = attrib->enabled;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_SIZE:
		*params = attrib->size;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
		*params = attrib->stride;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_TYPE:
		*params = (GLint)attrib->type;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
		*params = attrib->normalized;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
		*params = 0;
		if (attrib->buffer != NULL)
			*params = (GLint)attrib->buffer->name;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
		*params = attrib->integer;
		return;
	case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
		*params = (GLint)attrib->divisor;
		return;
	case GL_CURRENT_VERTEX_ATTRIB:
		for (component = 0U; component < 4U; component++)
			params[component] = (GLint)attrib->value[component];
		return;
	default:
		break;
	}

	/* Any other is an error. */
	gles_error(context, GL_INVALID_ENUM);
}

/*
 * Reports an attribute's array pointer.
 */
GL_APICALL void GL_APIENTRY
glGetVertexAttribPointerv(
	GLuint index,
	GLenum pname,
	void **pointer)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, an attribute and the one name. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Only the pointer. */
	if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The pointer (an offset for a buffer object). */
	*pointer = (void *)state->attribs[index].pointer;
}

/*
 * Draws with the current program, or, without one, with the
 * fixed-function layer's program for this draw (libGL).
 */
static void
draw_primitives(
	GLenum mode,
	GLint first,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLsizei instances)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	int flat;

	/* A context with its state, not in a conditional rendering whose query saw nothing. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL || state->conditional_skip)
		return;

	/*
	 * A program the application made draws as it is (OpenGL ES has
	 * nothing else); its flat inputs take GL's provoking vertex, the last
	 * of each primitive unless desktop GL's glProvokingVertex chose the
	 * first (Vulkan's).  A program that captures outputs keeps GL's order
	 * of the vertices instead (transform feedback records them so).
	 */
	if (state->program != NULL || gles_fixed == NULL) {
		flat = 0;
		if (state->program != NULL &&
		    state->program->flat_inputs &&
		    state->program->capture_count == 0U &&
		    state->provoking_vertex != GL_FIRST_VERTEX_CONVENTION)
			flat = 1;
		draw_program(mode, first, count, type, indices, instances, flat);
		return;
	}

	/* Without one, the fixed-function layer's, current for this draw only. */
	flat = 0;
	program = gles_fixed->program(context, &flat);
	if (program == NULL) {
		state->program = NULL;
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Current for this draw only. */
	state->program = program;
	draw_program(mode, first, count, type, indices, instances, flat);
	state->program = NULL;
}

/*
 * Draws with the current program: the indices read and turned into a
 * list when needed (always, and each primitive turned to start at GL's
 * provoking vertex, when the shading is flat), the vertices and uniforms
 * put where the GPU reads them, the pipeline and descriptors bound, and
 * the draw recorded into the frame.
 */
static void
draw_program(
	GLenum mode,
	GLint first,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLsizei instances,
	int flat)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_target target;
	struct gles_raster raster;
	struct gles_vertex_layout layout;
	VkBuffer buffers[GLES_ATTRIBS];
	VkDeviceSize offsets[GLES_ATTRIBS];
	VkDescriptorBufferInfo blocks[GLES_NAMED_BLOCKS];
	VkBuffer index_buffer;
	VkDeviceSize index_offset;
	VkPipeline pipeline;
	VkDescriptorSet set;
	uint32_t dynamic_offset;
	uint32_t dynamic_count;
	uint32_t *read;
	uint32_t *list;
	uint32_t *stream;
	uint32_t largest;
	uint32_t expanded;
	uint32_t topology;
	uint32_t restart;
	struct gles_capture_target capture;
	const VkDescriptorBufferInfo *capture_buffer;
	int restarted;
	int capturing;
	int strip;
	int status;

	/* A context with its state, a linked program and a window surface. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (first < 0 ||
	    count < 0 ||
	    instances < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A mode. */
	status = draw_topology(mode, &topology, &strip);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A linked program. */
	if (state->program == NULL || !state->program->linked) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* A draw transform feedback allows while it is active (glDrawArrays of its kind), and whether it captures. */
	status = gles_feedback_check(context, state, mode, type, &capturing);
	if (status != 0)
		return;

	/* A draw surface. */
	if (context->draw == NULL) {
		gles_error(context, GL_INVALID_FRAMEBUFFER_OPERATION);
		return;
	}

	/* Nothing to draw. */
	if (count == 0 || instances == 0)
		return;

	/* The indices: read for glDrawElements (whether the restart index splits them), the range for glDrawArrays. */
	read = NULL;
	restarted = 0;
	largest = (uint32_t)first + (uint32_t)count - 1U;
	if (type != GL_NONE) {
		status = draw_indices(context, count, type, indices, &read, &largest, &restarted);
		if (status != 0)
			return;
	}

	/* The restart index (the largest of the type, or desktop GL's own). */
	restart = draw_restart_index(state, type);

	/*
	 * Indices the restart index splits become a list of the pieces; a
	 * strip, loop or fan (or any mode, shaded flat) becomes a list;
	 * indices that were read become 32-bit ones.
	 */
	list = read;
	expanded = (uint32_t)count;
	if (restarted) {
		list = draw_restart(mode, read, count, restart, flat, &expanded);
		free(read);
		read = NULL;
		if (list == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}
	} else if (strip || flat) {
		list = gles_expand(mode, read, (uint32_t)first, count, flat, &expanded);
		free(read);
		read = NULL;
		if (list == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}
	}

	/* A list too short for one primitive draws nothing. */
	if (expanded == 0U) {
		free(list);
		return;
	}

	/* The indices in the stream. */
	index_buffer = VK_NULL_HANDLE;
	index_offset = 0U;
	if (list != NULL) {
		stream = gles_stream(state, expanded * sizeof(uint32_t), 4U, &index_buffer, &index_offset);
		if (stream == NULL) {
			free(list);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* Copied (a capturing draw keeps the list: it is the order vertices are written in). */
		memcpy(stream, list, expanded * sizeof(uint32_t));
		if (!capturing) {
			free(list);
			list = NULL;
		}
	}

	/* The vertices (and instances) each attribute reads. */
	status = draw_vertices(state, largest + 1U, (uint32_t)instances, &layout, buffers, offsets);
	if (status != 0) {
		gles_report("the vertices", status);
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The buffer ranges the named uniform blocks read. */
	status = draw_blocks(context, state, blocks);
	if (status != 0)
		return;

	/* The frame, open and in the target's pass (the surface's or the framebuffer object's). */
	status = gles_target_open(context, state, NULL, &target);
	if (status != 0)
		return;

	/* The pipeline for the state, and the descriptors. */
	draw_raster(state, &target, topology, &raster);
	pipeline = draw_pipeline(state, &target, &raster, &layout);
	if (pipeline == VK_NULL_HANDLE) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* A program that captures outputs writes them into a capture buffer of the draw's (copied on only while capturing). */
	capture_buffer = NULL;
	if (state->program->capture_count != 0U) {
		status = gles_feedback_prepare(context, state, capturing, first, count, instances, expanded, &capture);
		if (status != 0) {
			free(list);
			return;
		}

		/* The capture buffer is described with the draw's descriptors. */
		capture_buffer = &capture.buffer;
	}

	/* The descriptors. */
	set = draw_descriptors(state, blocks, capture_buffer, &dynamic_offset);
	if (set == VK_NULL_HANDLE) {
		gles_report("the descriptors", -1);
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The recording: pipeline, dynamic state, descriptors, vertices, indices, the draw. */
	vkCmdBindPipeline(target.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	draw_dynamic(state, context, &target);
	dynamic_count = 0U;
	if (state->program->uniform_data != NULL)
		dynamic_count = 1U;
	vkCmdBindDescriptorSets(target.command, VK_PIPELINE_BIND_POINT_GRAPHICS, state->program->layout, 0U, 1U, &set,
				dynamic_count, &dynamic_offset);
	if (layout.count != 0U)
		vkCmdBindVertexBuffers(target.command, 0U, layout.count, buffers, offsets);

	/* An active occlusion query counts the draw (a segment of it open in the pass). */
	gles_queries_draw(state, &target);

	/* Indexed, or not. */
	if (index_buffer != VK_NULL_HANDLE) {
		vkCmdBindIndexBuffer(target.command, index_buffer, index_offset, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(target.command, expanded, (uint32_t)instances, 0U, 0, 0U);
	} else {
		vkCmdDraw(target.command, (uint32_t)count, (uint32_t)instances, (uint32_t)first, 0U);
	}

	/* What a capturing draw wrote goes into the transform feedback buffers. */
	if (capturing)
		gles_feedback_record(context, state, &capture, first, instances, list, expanded);
	free(list);
}

/*
 * Reads glDrawElements' indices (from the element buffer, or the
 * application's memory) as 32-bit ones, and the largest; with
 * GL_PRIMITIVE_RESTART_FIXED_INDEX on, the largest index of the type is
 * the restart index, left out of the largest and reported in *restarted
 * when there is one.  Returns 0, or -1 with the error recorded.
 */
static int
draw_indices(
	struct zegl_context *context,
	GLsizei count,
	GLenum type,
	const void *indices,
	uint32_t **out,
	uint32_t *largest,
	int *restarted)
{
	struct gles_state *state;
	const unsigned char *bytes;
	uint32_t *read;
	uint32_t restart;
	uint16_t half;
	size_t size;
	size_t offset;
	GLsizei index;
	int restarts;

	/* The index size. */
	state = gles_state(context);
	size = 0U;
	if (type == GL_UNSIGNED_BYTE)
		size = 1U;
	if (type == GL_UNSIGNED_SHORT)
		size = 2U;
	if (type == GL_UNSIGNED_INT)
		size = 4U;
	if (size == 0U) {
		gles_error(context, GL_INVALID_ENUM);
		return -1;
	}

	/* The restart index: the largest of the type, or desktop GL's own, when restarts are on. */
	restart = draw_restart_index(state, type);
	restarts = 0;
	if (state->primitive_restart || state->primitive_restart_any)
		restarts = 1;
	*restarted = 0;

	/* Where they are: an offset into the element buffer, or a pointer. */
	bytes = indices;
	if (state->element_buffer != NULL) {
		offset = (size_t)(uintptr_t)indices;
		if (offset + (size_t)count * size > state->element_buffer->size) {
			gles_error(context, GL_INVALID_OPERATION);
			return -1;
		}

		/* The bytes at the offset. */
		bytes = state->element_buffer->data + offset;
	}

	/* Somewhere to read them from. */
	if (bytes == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* Each one, as 32 bits. */
	read = malloc((size_t)count * sizeof(uint32_t));
	if (read == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* The largest seen so far. */
	*largest = 0U;
	for (index = 0; index < count; index++) {
		if (size == 1U) {
			read[index] = bytes[index];
		} else if (size == 2U) {
			memcpy(&half, bytes + (size_t)index * 2U, 2U);
			read[index] = half;
		} else {
			memcpy(&read[index], bytes + (size_t)index * 4U, 4U);
		}

		/* A restart index splits the primitives and names no vertex. */
		if (restarts && read[index] == restart) {
			*restarted = 1;
			continue;
		}

		/* The base vertex of glDrawElementsBaseVertex added. */
		read[index] += (uint32_t)state->base_vertex;

		/* The largest. */
		if (read[index] > *largest)
			*largest = read[index];
	}

	/* Succeeded: the indices. */
	*out = read;
	return 0;
}

/*
 * Turns count vertices of a mode (indices, or first up when there are
 * none) into a list of triangles, lines or points.  With rotate, each
 * primitive starts with GL's provoking vertex (the last of a primitive,
 * the first of a polygon), which Vulkan's flat shading takes from the
 * first; the winding is kept.  Returns the list and its length, or NULL
 * when there is no memory.
 */
uint32_t *
gles_expand(
	GLenum mode,
	const uint32_t *indices,
	uint32_t first,
	GLsizei count,
	int rotate,
	uint32_t *expanded)
{
	uint32_t *list;
	uint32_t n;
	uint32_t total;
	uint32_t index;
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;

	/* At most six indices per vertex (quads). */
	n = (uint32_t)count;
	list = malloc(((size_t)n * 6U + 6U) * sizeof(uint32_t));
	if (list == NULL)
		return NULL;

	/* Each primitive of the mode, as vertex numbers from 0. */
	total = 0U;
	for (index = 0U; index < n; index++) {
		switch (mode) {
		case GL_POINTS:
			list[total++] = index;
			break;
		case GL_LINES:
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			/* A segment: every pair of a list, every vertex and the next of a strip, and a loop's closing one. */
			a = index;
			b = index + 1U;
			if (mode == GL_LINES && (index & 1U) != 0U)
				break;
			if (b == n && mode == GL_LINE_LOOP && n > 1U)
				b = 0U;
			if (b >= n || (b == 0U && mode != GL_LINE_LOOP))
				break;
			list[total++] = a;
			list[total++] = b;
			if (rotate) {
				list[total - 2U] = b;
				list[total - 1U] = a;
			}

			/* The segment is in. */
			break;
		case GL_TRIANGLES:
		case GL_TRIANGLE_STRIP:
		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
			/* A triangle: every three of a list, each vertex of a strip (every other one turned round), a fan's or polygon's. */
			if (index + 2U >= n || (mode == GL_TRIANGLES && index % 3U != 0U))
				break;
			a = index;
			b = index + 1U;
			c = index + 2U;
			if (mode == GL_TRIANGLE_STRIP && (index & 1U) != 0U) {
				a = index + 1U;
				b = index;
			}

			/* A fan or polygon turns about the first vertex. */
			if (mode == GL_TRIANGLE_FAN || mode == GL_POLYGON)
				a = 0U;

			/* GL's provoking vertex is the third of a triangle (the first of a polygon's): it goes first, cyclically. */
			if (rotate && mode != GL_POLYGON) {
				d = c;
				c = b;
				b = a;
				a = d;
			}

			/* The triangle. */
			list[total++] = a;
			list[total++] = b;
			list[total++] = c;
			break;
		default:
			/* Quads and quad strips: two triangles each, both starting at the provoking (last) vertex. */
			if (mode == GL_QUADS && (index % 4U != 0U || index + 3U >= n))
				break;
			if (mode == GL_QUAD_STRIP && ((index & 1U) != 0U || index + 3U >= n))
				break;
			a = index;
			b = index + 1U;
			c = index + 2U;
			d = index + 3U;
			if (mode == GL_QUAD_STRIP) {
				c = index + 3U;
				d = index + 2U;
			}

			/* The quad a b c d, from its provoking vertex: d a b and d b c (a quad strip's provoking vertex is c). */
			if (mode == GL_QUAD_STRIP) {
				list[total++] = c;
				list[total++] = d;
				list[total++] = a;
				list[total++] = c;
				list[total++] = a;
				list[total++] = b;
				break;
			}

			/* The quad from its provoking vertex. */
			list[total++] = d;
			list[total++] = a;
			list[total++] = b;
			list[total++] = d;
			list[total++] = b;
			list[total++] = c;
			break;
		}
	}

	/* The vertex numbers become vertices: the indices given, or first up. */
	for (index = 0U; index < total; index++) {
		if (indices != NULL)
			list[index] = indices[list[index]];
		else
			list[index] += first;
	}

	/* Succeeded: the list. */
	*expanded = total;
	return list;
}

/*
 * Turns indices that the restart index splits into one list: each piece
 * between two restarts is expanded as a draw of its own (gles_expand,
 * rotated for flat shading when rotate is set) and the lists are joined.
 * Returns the list and its length, or NULL when there is no memory.
 */
static uint32_t *
draw_restart(
	GLenum mode,
	const uint32_t *indices,
	GLsizei count,
	uint32_t restart,
	int rotate,
	uint32_t *expanded)
{
	uint32_t *list;
	uint32_t *piece;
	uint32_t total;
	uint32_t length;
	GLsizei start;
	GLsizei index;

	/* At most six indices per index, as gles_expand makes. */
	list = malloc(((size_t)count * 6U + 6U) * sizeof(uint32_t));
	if (list == NULL)
		return NULL;

	/* Each piece: the indices up to the next restart (or the end). */
	total = 0U;
	start = 0;
	for (index = 0; index <= count; index++) {
		if (index < count && indices[index] != restart)
			continue;

		/* An empty piece (two restarts in a row) adds nothing. */
		if (index == start) {
			start = index + 1;
			continue;
		}

		/* The piece as a list of its own. */
		piece = gles_expand(mode, indices + start, 0U, index - start, rotate, &length);
		if (piece == NULL) {
			free(list);
			return NULL;
		}

		/* Joined to the others. */
		memcpy(list + total, piece, (size_t)length * sizeof(uint32_t));
		total += length;
		free(piece);
		start = index + 1;
	}

	/* Succeeded: the joined list. */
	*expanded = total;
	return list;
}

/* Returns Vulkan's topology for a GL mode, and whether the mode must become a list first; nonzero for a mode that is not one. */
static int
draw_topology(
	GLenum mode,
	uint32_t *topology,
	int *strip)
{
	/* Lists are drawn as they are; strips, loops and fans are turned into lists. */
	*strip = 0;
	switch (mode) {
	case GL_POINTS:
		*topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		return 0;
	case GL_LINES:
		*topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		return 0;
	case GL_LINE_STRIP:
	case GL_LINE_LOOP:
		*topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		*strip = 1;
		return 0;
	case GL_TRIANGLES:
		*topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		return 0;
	case GL_TRIANGLE_STRIP:
	case GL_TRIANGLE_FAN:
		*topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		*strip = 1;
		return 0;
	default:
		break;
	}

	/* Desktop GL's quads, quad strips and polygons, with the fixed-function layer. */
	if (gles_fixed != NULL && (mode == GL_QUADS || mode == GL_QUAD_STRIP || mode == GL_POLYGON)) {
		*topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		*strip = 1;
		return 0;
	}

	/* Not a mode. */
	return -1;
}

/*
 * Puts each active attribute's vertices (0 to vertices - 1, or for an
 * array with a divisor the elements its instances read) where the GPU
 * reads them, and describes the layout: a buffer object's device copy as
 * it is, client arrays copied into the stream, unfetchable formats and
 * divisors above 1 spread into the stream element by element, and a
 * disabled array's current value as one vertex with stride 0.
 */
static int
draw_vertices(
	struct gles_state *state,
	uint32_t vertices,
	uint32_t instances,
	struct gles_vertex_layout *layout,
	VkBuffer *buffers,
	VkDeviceSize *offsets)
{
	struct gles_program *program;
	struct gles_attrib *attrib;
	const unsigned char *source;
	unsigned char *target;
	VkFormat format;
	size_t bytes;
	size_t stride;
	size_t needed;
	uint32_t elements;
	uint32_t repeat;
	uint32_t element_size;
	uint32_t index;
	unsigned base;
	int fetchable;
	int is_signed;
	int status;

	/* One binding per active attribute. */
	program = state->program;
	memset(layout, 0, sizeof(*layout));
	for (index = 0U; index < program->attribute_count; index++) {
		attrib = &state->attribs[program->attributes[index].location];
		layout->locations[index] = program->attributes[index].location;
		layout->rates[index] = VK_VERTEX_INPUT_RATE_VERTEX;

		/* A disabled array: the current value, the same for every vertex. */
		if (!attrib->enabled) {
			status = draw_current(state, attrib, program->attributes[index].type, &buffers[index], &offsets[index],
					      &layout->formats[index]);
			if (status != 0)
				return -1;
			layout->strides[index] = 0U;
			continue;
		}

		/* The array's format and stride. */
		format = draw_format(attrib->size, attrib->type, attrib->normalized, attrib->integer, &bytes);
		fetchable = draw_format_ok(state, format);
		stride = (size_t)attrib->stride;
		if (stride == 0U)
			stride = bytes;

		/*
		 * Integers of the other signedness than the shader's input are
		 * converted to the input's kind (Vulkan fetches a format only
		 * into an input of its own kind).
		 */
		base = draw_attribute_base(program->attributes[index].type);
		is_signed = draw_signed(attrib->type);
		if (attrib->integer &&
		    base == 1U &&
		    !is_signed)
			fetchable = 0;
		if (attrib->integer &&
		    base == 2U &&
		    is_signed)
			fetchable = 0;

		/*
		 * The elements it has to supply: one per vertex, or with a
		 * divisor one per divisor instances (repeat: how many instances
		 * of the stream read each one when it is spread).
		 */
		elements = vertices;
		repeat = 1U;
		if (attrib->divisor != 0U) {
			layout->rates[index] = VK_VERTEX_INPUT_RATE_INSTANCE;
			elements = (instances + attrib->divisor - 1U) / attrib->divisor;
			if (attrib->divisor > 1U)
				repeat = attrib->divisor;
		}

		/* The bytes those elements span. */
		needed = (size_t)(elements - 1U) * stride + bytes;

		/* A buffer object the device can fetch from as it is. */
		if (attrib->buffer != NULL) {
			if ((size_t)(uintptr_t)attrib->pointer + needed > attrib->buffer->size)
				return -1;
			if (fetchable && repeat == 1U) {
				status = gles_buffer_sync(state, attrib->buffer);
				if (status != 0)
					return -1;
				attrib->buffer->used = state->frame;
				buffers[index] = attrib->buffer->buffer;
				offsets[index] = (VkDeviceSize)(uintptr_t)attrib->pointer;
				layout->formats[index] = (uint32_t)format;
				layout->strides[index] = (uint32_t)stride;
				continue;
			}
		}

		/* The bytes: in the buffer object, or the application's memory. */
		source = attrib->pointer;
		if (attrib->buffer != NULL)
			source = attrib->buffer->data + (size_t)(uintptr_t)attrib->pointer;
		if (source == NULL)
			return -1;

		/* Fetchable and one element per vertex or instance: copied as they are. */
		if (fetchable && repeat == 1U) {
			target = gles_stream(state, needed, 16U, &buffers[index], &offsets[index]);
			if (target == NULL)
				return -1;
			memcpy(target, source, needed);
			layout->formats[index] = (uint32_t)format;
			layout->strides[index] = (uint32_t)stride;
			continue;
		}

		/* Otherwise spread into the stream element by element (converted when the device cannot fetch them). */
		status = draw_spread(state, attrib, base, source, stride, elements * repeat, repeat, fetchable, &buffers[index],
				     &offsets[index], &layout->formats[index], &element_size);
		if (status != 0)
			return -1;
		layout->strides[index] = element_size;
	}

	/* Succeeded: every attribute has its vertices. */
	layout->count = program->attribute_count;
	return 0;
}

/*
 * Puts a disabled array's current value into the stream, in the format
 * the shader's input reads (floats, or ints or unsigned ints for an
 * integer input).  Returns 0, or -1 when there is no memory.
 */
static int
draw_current(
	struct gles_state *state,
	const struct gles_attrib *attrib,
	GLenum attribute_type,
	VkBuffer *buffer,
	VkDeviceSize *offset,
	uint32_t *format)
{
	unsigned char *target;
	unsigned base;

	/* Four words for the value. */
	target = gles_stream(state, 4U * sizeof(float), 16U, buffer, offset);
	if (target == NULL)
		return -1;

	/* The value's bits (an integer value keeps its integers there). */
	memcpy(target, attrib->value, 4U * sizeof(float));

	/* The format of the shader's input. */
	base = draw_attribute_base(attribute_type);
	*format = VK_FORMAT_R32G32B32A32_SFLOAT;
	if (base == 1U)
		*format = VK_FORMAT_R32G32B32A32_SINT;
	if (base == 2U)
		*format = VK_FORMAT_R32G32B32A32_UINT;

	/* Succeeded: the value is in the stream. */
	return 0;
}

/*
 * Spreads count elements of an array into the stream, element k taken
 * from the array's element k / repeat: as they are when the device can
 * fetch them, else each component converted (to a float, or for an
 * integer array to a 32-bit integer of the kind the shader's input reads,
 * base: 1 ints, 2 unsigned ints).  Returns 0 with the stream place, the
 * format and the size of one element, or -1 when there is no memory.
 */
static int
draw_spread(
	struct gles_state *state,
	const struct gles_attrib *attrib,
	unsigned base,
	const unsigned char *source,
	size_t stride,
	uint32_t count,
	uint32_t repeat,
	int fetchable,
	VkBuffer *buffer,
	VkDeviceSize *offset,
	uint32_t *format,
	uint32_t *element_size)
{
	const unsigned char *element;
	unsigned char *target;
	float number;
	uint32_t integer;
	size_t bytes;
	size_t size;
	uint32_t index;
	GLint component;
	GLenum converted;

	/* The size of one element in the stream: as it is, or four bytes a component. */
	*format = (uint32_t)draw_format(attrib->size, attrib->type, attrib->normalized, attrib->integer, &bytes);
	size = bytes;
	if (!fetchable) {
		size = (size_t)attrib->size * 4U;
		converted = GL_FLOAT;
		if (attrib->integer && base == 2U) {
			converted = GL_UNSIGNED_INT;
		} else if (attrib->integer) {
			converted = GL_INT;
		}

		/* The format of the converted components. */
		*format = (uint32_t)draw_format(attrib->size, converted, GL_FALSE, attrib->integer, &bytes);
	}

	/* The room. */
	target = gles_stream(state, (size_t)count * size, 16U, buffer, offset);
	if (target == NULL)
		return -1;

	/* Each element, from the one of the array it repeats. */
	for (index = 0U; index < count; index++) {
		element = source + (size_t)(index / repeat) * stride;

		/* As it is, when the device fetches it. */
		if (fetchable) {
			memcpy(target + (size_t)index * size, element, size);
			continue;
		}

		/* Each component converted: an integer array's to 32-bit integers, any other's to floats. */
		for (component = 0; component < attrib->size; component++) {
			if (attrib->integer) {
				integer = draw_integer(element, attrib->type, component);
				memcpy(target + (size_t)index * size + (size_t)component * 4U, &integer, 4U);
			} else {
				number = draw_component(element, attrib->type, attrib->normalized, component);
				memcpy(target + (size_t)index * size + (size_t)component * 4U, &number, 4U);
			}
		}
	}

	/* Succeeded: the elements are in the stream. */
	*element_size = (uint32_t)size;
	return 0;
}

/*
 * Returns the Vulkan format of an array's components (integers read as
 * they are for an integer array) and the bytes of one vertex's;
 * VK_FORMAT_UNDEFINED for arrays Vulkan has no format for (fixed point,
 * 32-bit integers read as floats), which are converted.
 */
static VkFormat
draw_format(
	GLint size,
	GLenum type,
	GLboolean normalized,
	int integer,
	size_t *bytes)
{
	static const VkFormat floats[4] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
	static const VkFormat halves[4] = { VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
	static const VkFormat ubyte_norm[4] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
	static const VkFormat ubyte[4] = { VK_FORMAT_R8_USCALED, VK_FORMAT_R8G8_USCALED, VK_FORMAT_R8G8B8_USCALED, VK_FORMAT_R8G8B8A8_USCALED };
	static const VkFormat byte_norm[4] = { VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM };
	static const VkFormat byte[4] = { VK_FORMAT_R8_SSCALED, VK_FORMAT_R8G8_SSCALED, VK_FORMAT_R8G8B8_SSCALED, VK_FORMAT_R8G8B8A8_SSCALED };
	static const VkFormat ushort_norm[4] = { VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM, VK_FORMAT_R16G16B16A16_UNORM };
	static const VkFormat ushort[4] = { VK_FORMAT_R16_USCALED, VK_FORMAT_R16G16_USCALED, VK_FORMAT_R16G16B16_USCALED, VK_FORMAT_R16G16B16A16_USCALED };
	static const VkFormat short_norm[4] = { VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM };
	static const VkFormat shorts[4] = { VK_FORMAT_R16_SSCALED, VK_FORMAT_R16G16_SSCALED, VK_FORMAT_R16G16B16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED };
	static const VkFormat byte_int[4] = { VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT };
	static const VkFormat ubyte_int[4] = { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT };
	static const VkFormat short_int[4] = { VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT };
	static const VkFormat ushort_int[4] = { VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT };
	static const VkFormat int_int[4] = { VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT };
	static const VkFormat uint_int[4] = { VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT };

	/* An integer array: the integer formats, read as they are. */
	if (integer) {
		switch (type) {
		case GL_BYTE:
			*bytes = (size_t)size;
			return byte_int[size - 1];
		case GL_UNSIGNED_BYTE:
			*bytes = (size_t)size;
			return ubyte_int[size - 1];
		case GL_SHORT:
			*bytes = (size_t)size * 2U;
			return short_int[size - 1];
		case GL_UNSIGNED_SHORT:
			*bytes = (size_t)size * 2U;
			return ushort_int[size - 1];
		case GL_INT:
			*bytes = (size_t)size * 4U;
			return int_int[size - 1];
		default:
			break;
		}

		/* GL_UNSIGNED_INT. */
		*bytes = (size_t)size * 4U;
		return uint_int[size - 1];
	}

	/* The type's table. */
	switch (type) {
	case GL_FLOAT:
		*bytes = (size_t)size * 4U;
		return floats[size - 1];
	case GL_HALF_FLOAT:
		*bytes = (size_t)size * 2U;
		return halves[size - 1];
	case GL_UNSIGNED_BYTE:
		*bytes = (size_t)size;
		if (normalized)
			return ubyte_norm[size - 1];
		return ubyte[size - 1];
	case GL_BYTE:
		*bytes = (size_t)size;
		if (normalized)
			return byte_norm[size - 1];
		return byte[size - 1];
	case GL_UNSIGNED_SHORT:
		*bytes = (size_t)size * 2U;
		if (normalized)
			return ushort_norm[size - 1];
		return ushort[size - 1];
	case GL_SHORT:
		*bytes = (size_t)size * 2U;
		if (normalized)
			return short_norm[size - 1];
		return shorts[size - 1];
	case GL_INT_2_10_10_10_REV:
		*bytes = 4U;
		if (normalized)
			return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
		return VK_FORMAT_A2B10G10R10_SSCALED_PACK32;
	case GL_UNSIGNED_INT_2_10_10_10_REV:
		*bytes = 4U;
		if (normalized)
			return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		return VK_FORMAT_A2B10G10R10_USCALED_PACK32;
	default:
		break;
	}

	/* GL_FIXED, GL_INT and GL_UNSIGNED_INT: four bytes a component, converted. */
	*bytes = (size_t)size * 4U;
	return VK_FORMAT_UNDEFINED;
}

/* Reports whether the device fetches vertices of a format, asking the device once per format. */
static int
draw_format_ok(
	struct gles_state *state,
	VkFormat format)
{
	VkFormatProperties properties;
	unsigned char answer;

	/* Fixed point has no format. */
	if (format == VK_FORMAT_UNDEFINED || (unsigned)format >= GLES_FORMATS)
		return 0;

	/* The device's buffer features for it, the first time. */
	answer = state->vertex_formats[format];
	if (answer == 0U) {
		vkGetPhysicalDeviceFormatProperties(state->display->physical, format, &properties);
		answer = 2U;
		if ((properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) != 0U)
			answer = 1U;
		state->vertex_formats[format] = answer;
	}

	/* The answer. */
	if (answer != 1U)
		return 0;
	return 1;
}

/* Reports whether an array type's integers are signed. */
static int
draw_signed(
	GLenum type)
{
	/* The signed integer types. */
	if (type == GL_BYTE ||
	    type == GL_SHORT ||
	    type == GL_INT)
		return 1;

	/* The others. */
	return 0;
}

/* Returns the scalar kind a shader input of a GL type reads: 0 floats, 1 ints, 2 unsigned ints. */
static unsigned
draw_attribute_base(
	GLenum type)
{
	/* The integer types. */
	switch (type) {
	case GL_INT:
	case GL_INT_VEC2:
	case GL_INT_VEC3:
	case GL_INT_VEC4:
		return 1U;
	case GL_UNSIGNED_INT:
	case GL_UNSIGNED_INT_VEC2:
	case GL_UNSIGNED_INT_VEC3:
	case GL_UNSIGNED_INT_VEC4:
		return 2U;
	default:
		break;
	}

	/* Anything else reads floats. */
	return 0U;
}

/* Converts one component of an array's element to a float, as GL reads it. */
static float
draw_component(
	const unsigned char *element,
	GLenum type,
	GLboolean normalized,
	GLint component)
{
	int8_t signed_byte;
	uint16_t unsigned_short;
	int16_t signed_short;
	int32_t signed_int;
	uint32_t unsigned_int;
	int32_t fixed;
	uint32_t packed;
	int32_t field;
	float value;

	/* The component's type. */
	switch (type) {
	case GL_UNSIGNED_BYTE:
		value = (float)element[component];
		if (normalized)
			value /= 255.0f;
		return value;
	case GL_BYTE:
		memcpy(&signed_byte, element + component, 1U);
		value = (float)signed_byte;
		if (normalized)
			value = (value * 2.0f + 1.0f) / 255.0f;
		return value;
	case GL_UNSIGNED_SHORT:
		memcpy(&unsigned_short, element + component * 2, 2U);
		value = (float)unsigned_short;
		if (normalized)
			value /= 65535.0f;
		return value;
	case GL_SHORT:
		memcpy(&signed_short, element + component * 2, 2U);
		value = (float)signed_short;
		if (normalized)
			value = (value * 2.0f + 1.0f) / 65535.0f;
		return value;
	case GL_HALF_FLOAT:
		memcpy(&unsigned_short, element + component * 2, 2U);
		value = gles_half_float(unsigned_short);
		return value;
	case GL_INT:
		memcpy(&signed_int, element + component * 4, 4U);
		value = (float)signed_int;
		if (normalized)
			value = (float)((double)signed_int / 2147483647.0);
		if (normalized && value < -1.0f)
			value = -1.0f;
		return value;
	case GL_UNSIGNED_INT:
		memcpy(&unsigned_int, element + component * 4, 4U);
		value = (float)unsigned_int;
		if (normalized)
			value = (float)((double)unsigned_int / 4294967295.0);
		return value;
	case GL_FIXED:
		memcpy(&fixed, element + component * 4, 4U);
		return (float)fixed / 65536.0f;
	default:
		break;
	}

	/* A packed type: x, y and z are 10 bits from bit 0, 10 and 20, w is 2 bits from bit 30. */
	if (type == GL_INT_2_10_10_10_REV || type == GL_UNSIGNED_INT_2_10_10_10_REV) {
		memcpy(&packed, element, 4U);
		field = (int32_t)((packed >> (10 * component)) & 0x3ffU);
		if (component == 3)
			field = (int32_t)(packed >> 30);

		/* The unsigned kind: as it is, or over the field's largest. */
		if (type == GL_UNSIGNED_INT_2_10_10_10_REV) {
			value = (float)field;
			if (normalized && component == 3)
				value /= 3.0f;
			else if (normalized)
				value /= 1023.0f;
			return value;
		}

		/* The signed kind: the field's top bit is its sign. */
		if (component < 3 && field >= 512)
			field -= 1024;
		if (component == 3 && field >= 2)
			field -= 4;
		value = (float)field;
		if (normalized && component < 3)
			value /= 511.0f;
		if (normalized && value < -1.0f)
			value = -1.0f;
		return value;
	}

	/* A float. */
	memcpy(&value, element + component * 4, 4U);
	return value;
}

/* Reads one component of an integer array's element as a 32-bit integer (sign-extended for the signed types). */
static uint32_t
draw_integer(
	const unsigned char *element,
	GLenum type,
	GLint component)
{
	int8_t signed_byte;
	uint16_t unsigned_short;
	int16_t signed_short;
	uint32_t word;

	/* The component's type. */
	switch (type) {
	case GL_UNSIGNED_BYTE:
		return element[component];
	case GL_BYTE:
		memcpy(&signed_byte, element + component, 1U);
		return (uint32_t)(int32_t)signed_byte;
	case GL_UNSIGNED_SHORT:
		memcpy(&unsigned_short, element + component * 2, 2U);
		return unsigned_short;
	case GL_SHORT:
		memcpy(&signed_short, element + component * 2, 2U);
		return (uint32_t)(int32_t)signed_short;
	default:
		break;
	}

	/* GL_INT and GL_UNSIGNED_INT: the word as it is. */
	memcpy(&word, element + component * 4, 4U);
	return word;
}

/* Returns the shape of texture a sampler type reads (GLES_SHAPE_*). */
static unsigned
draw_sampler_shape(
	GLenum type)
{
	/* The cube map, 3D and 2D array samplers of every kind. */
	switch (type) {
	case GL_SAMPLER_CUBE:
	case GL_SAMPLER_CUBE_SHADOW:
	case GL_INT_SAMPLER_CUBE:
	case GL_UNSIGNED_INT_SAMPLER_CUBE:
		return GLES_SHAPE_CUBE;
	case GL_SAMPLER_3D:
	case GL_INT_SAMPLER_3D:
	case GL_UNSIGNED_INT_SAMPLER_3D:
		return GLES_SHAPE_3D;
	case GL_SAMPLER_2D_ARRAY:
	case GL_SAMPLER_2D_ARRAY_SHADOW:
	case GL_INT_SAMPLER_2D_ARRAY:
	case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
		return GLES_SHAPE_ARRAY;
	case GL_SAMPLER_2D_RECT:
	case GL_SAMPLER_2D_RECT_SHADOW:
	case GL_INT_SAMPLER_2D_RECT:
	case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
		return GLES_SHAPE_RECT;
	case GL_SAMPLER_BUFFER:
	case GL_INT_SAMPLER_BUFFER:
	case GL_UNSIGNED_INT_SAMPLER_BUFFER:
		return GLES_SHAPE_BUFFER;
	default:
		break;
	}

	/* A 2D texture's. */
	return GLES_SHAPE_2D;
}

/* Returns the kind of texels a sampler type reads: 0 floats, 1 ints, 2 unsigned ints, 3 depth compared (a shadow sampler). */
static unsigned
draw_sampler_kind(
	GLenum type)
{
	/* The kinds other than floats. */
	switch (type) {
	case GL_INT_SAMPLER_2D:
	case GL_INT_SAMPLER_CUBE:
	case GL_INT_SAMPLER_3D:
	case GL_INT_SAMPLER_2D_ARRAY:
	case GL_INT_SAMPLER_2D_RECT:
	case GL_INT_SAMPLER_BUFFER:
		return 1U;
	case GL_UNSIGNED_INT_SAMPLER_2D:
	case GL_UNSIGNED_INT_SAMPLER_CUBE:
	case GL_UNSIGNED_INT_SAMPLER_3D:
	case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
	case GL_UNSIGNED_INT_SAMPLER_2D_RECT:
	case GL_UNSIGNED_INT_SAMPLER_BUFFER:
		return 2U;
	case GL_SAMPLER_2D_SHADOW:
	case GL_SAMPLER_CUBE_SHADOW:
	case GL_SAMPLER_2D_ARRAY_SHADOW:
	case GL_SAMPLER_2D_RECT_SHADOW:
		return 3U;
	default:
		break;
	}

	/* Floats. */
	return 0U;
}

/*
 * Reports whether a texture holds texels a sampler of a kind reads: ints
 * and unsigned ints their own formats, a shadow sampler a depth format,
 * a float sampler any other (a depth texture reads its depth).
 */
static int
draw_texture_matches(
	const struct gles_texture *texture,
	unsigned kind)
{
	unsigned texel;
	unsigned wanted;

	/* The kind of the base level's format. */
	texel = texture->levels[texture->base_level].format->kind;

	/* A float sampler reads anything but integers. */
	if (kind == 0U) {
		if (texel == GLES_TEXEL_INT || texel == GLES_TEXEL_UINT)
			return 0;
		return 1;
	}

	/* The others read one kind: signed or unsigned integers, or depth for a shadow sampler. */
	wanted = GLES_TEXEL_DEPTH;
	if (kind == 1U)
		wanted = GLES_TEXEL_INT;
	if (kind == 2U)
		wanted = GLES_TEXEL_UINT;
	if (texel != wanted)
		return 0;

	/* The texture's texels are the sampler's kind. */
	return 1;
}

/* Fills the fixed-function part of a pipeline's key from the state. */
static void
draw_raster(
	struct gles_state *state,
	const struct gles_target *target,
	uint32_t topology,
	struct gles_raster *raster)
{
	unsigned face;
	unsigned index;
	uint32_t channels;
	uint32_t buffers;

	/* Everything not set is 0, so equal states compare equal. */
	memset(raster, 0, sizeof(*raster));
	raster->topology = topology;

	/* Blending, on each attachment whose draw buffer blends (not integers, not formats the device does not blend). */
	raster->color_count = target->color_count;
	buffers = 0U;
	if (state->blend)
		buffers = (1U << GLES_DRAW_BUFFERS) - 1U;
	if (state->blend_indexed)
		buffers = state->blend_buffers;
	if (buffers != 0U) {
		raster->blend = buffers & ((1U << target->color_count) - 1U) & ~(target->integer_mask | target->opaque_mask);
		raster->blend_src_rgb = state->blend_src_rgb;
		raster->blend_dst_rgb = state->blend_dst_rgb;
		raster->blend_src_alpha = state->blend_src_alpha;
		raster->blend_dst_alpha = state->blend_dst_alpha;
		raster->blend_equation_rgb = state->blend_equation_rgb;
		raster->blend_equation_alpha = state->blend_equation_alpha;
	}

	/* The colour mask as four bits on each attachment a draw buffer writes (its own mask once one was set). */
	for (index = 0U; index < target->color_count; index++) {
		if ((target->draw_mask & (1U << index)) == 0U)
			continue;
		channels = draw_channels(state, index);
		raster->color_mask |= channels << (index * 4U);
	}

	/* Depth. */
	raster->depth_test = (uint32_t)state->depth_test;
	if (state->depth_test) {
		raster->depth_func = state->depth_func;
		raster->depth_write = state->depth_mask;
	}

	/* Stencil, both faces. */
	raster->stencil_test = (uint32_t)state->stencil_test;
	for (face = 0U; state->stencil_test && face < 2U; face++) {
		raster->stencil_func[face] = state->stencil_func[face];
		raster->stencil_fail[face] = state->stencil_fail[face];
		raster->stencil_zfail[face] = state->stencil_zfail[face];
		raster->stencil_zpass[face] = state->stencil_zpass[face];
	}

	/* Culling (triangles only). */
	raster->front_face = state->front_face;
	if (state->cull && topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
		raster->cull = 1U;
		raster->cull_mode = state->cull_mode;
	}

	/* Polygon offset, and whether rasterization is discarded. */
	raster->polygon_offset = (uint32_t)state->polygon_offset;
	raster->discard = (uint32_t)state->rasterizer_discard;

	/* Depth clamping, when the device has it. */
	if (state->depth_clamp && state->display->features.depthClamp)
		raster->depth_clamp = 1U;

	/* A device that blends every colour attachment alike blends none when they differ. */
	if (!state->display->features.independentBlend &&
	    raster->blend != 0U &&
	    raster->blend != (1U << target->color_count) - 1U)
		raster->blend = 0U;
}

/* Returns the channels draws write into the attachment of a draw buffer, as four bits (red first): its own mask (glColorMaski) or the shared one. */
static uint32_t
draw_channels(
	const struct gles_state *state,
	unsigned index)
{
	const GLboolean *mask;
	uint32_t channels;
	unsigned channel;

	/* The buffer's own mask once one was set. */
	mask = state->color_mask;
	if (state->indexed_masked && index < GLES_DRAW_BUFFERS)
		mask = state->indexed_masks[index];

	/* Its channels as bits. */
	channels = 0U;
	for (channel = 0U; channel < 4U; channel++) {
		if (mask[channel])
			channels |= 1U << channel;
	}

	/* Succeeded: the four bits. */
	return channels;
}

/* Returns the pipeline for the program, the target's pass, the state and the vertex layout, making it the first time. */
static VkPipeline
draw_pipeline(
	struct gles_state *state,
	const struct gles_target *target,
	const struct gles_raster *raster,
	const struct gles_vertex_layout *layout)
{
	static const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_LINE_WIDTH,
		VK_DYNAMIC_STATE_DEPTH_BIAS,
		VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
		VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE
	};
	struct gles_pipeline_key key;
	struct gles_pipeline *entry;
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription bindings[GLES_ATTRIBS];
	VkVertexInputAttributeDescription attributes[GLES_ATTRIBS];
	VkPipelineVertexInputStateCreateInfo vertex;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo rasterization;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkStencilOpState *faces[2];
	VkPipelineColorBlendAttachmentState blend_attachments[GLES_COLOR_ATTACHMENTS];
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDynamicStateCreateInfo dynamic;
	VkGraphicsPipelineCreateInfo create;
	unsigned index;
	unsigned face;
	int clockwise;
	int differs;
	VkResult result;

	/* The key. */
	memset(&key, 0, sizeof(key));
	key.program = state->program->serial;
	key.pass = target->pass;
	key.raster = *raster;
	key.vertex = *layout;

	/* One made before for the same key. */
	for (entry = state->pipelines; entry != NULL; entry = entry->next) {
		differs = memcmp(&entry->key, &key, sizeof(key));
		if (differs == 0)
			return entry->pipeline;
	}

	/* The two stages. */
	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = state->program->vertex_module;
	if (!target->flip)
		stages[0].module = state->program->vertex_module_fbo;
	stages[0].pName = "main";
	stages[1] = stages[0];
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = state->program->fragment_module;

	/* One binding per attribute at its rate (per vertex or per instance), the attribute at offset 0 of it. */
	memset(bindings, 0, sizeof(bindings));
	memset(attributes, 0, sizeof(attributes));
	for (index = 0U; index < layout->count; index++) {
		bindings[index].binding = index;
		bindings[index].stride = layout->strides[index];
		bindings[index].inputRate = (VkVertexInputRate)layout->rates[index];
		attributes[index].location = layout->locations[index];
		attributes[index].binding = index;
		attributes[index].format = (VkFormat)layout->formats[index];
	}

	/* The vertex input. */
	memset(&vertex, 0, sizeof(vertex));
	vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex.vertexBindingDescriptionCount = layout->count;
	vertex.pVertexBindingDescriptions = bindings;
	vertex.vertexAttributeDescriptionCount = layout->count;
	vertex.pVertexAttributeDescriptions = attributes;

	/* The topology, and one dynamic viewport and scissor. */
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = (VkPrimitiveTopology)raster->topology;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1U;
	viewport.scissorCount = 1U;

	/* Rasterization: culling and the front face (the rewritten gl_Position keeps GL's winding), polygon offset. */
	memset(&rasterization, 0, sizeof(rasterization));
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	if (raster->cull) {
		rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
		if (raster->cull_mode == GL_FRONT)
			rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
		if (raster->cull_mode == GL_FRONT_AND_BACK)
			rasterization.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
	}

	/* The front face: GL's winding, turned over for a framebuffer object (its y is not turned over as a window's is). */
	clockwise = 0;
	if (raster->front_face == GL_CW)
		clockwise = 1;
	if (!target->flip)
		clockwise = !clockwise;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	if (clockwise)
		rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterization.depthBiasEnable = raster->polygon_offset;
	rasterization.rasterizerDiscardEnable = raster->discard;
	rasterization.depthClampEnable = raster->depth_clamp;
	rasterization.lineWidth = 1.0f;

	/* One sample, alpha to coverage as GL has it. */
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	if (target->samples > 1U)
		multisample.rasterizationSamples = (VkSampleCountFlagBits)target->samples;
	multisample.alphaToCoverageEnable = (VkBool32)state->sample_alpha_to_coverage;

	/* Depth and stencil (GL's compare functions are Vulkan's in the same order). */
	memset(&depth, 0, sizeof(depth));
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = raster->depth_test;
	depth.depthWriteEnable = raster->depth_write;
	depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
	if (raster->depth_test)
		depth.depthCompareOp = (VkCompareOp)(raster->depth_func - GL_NEVER);
	depth.stencilTestEnable = raster->stencil_test;
	faces[0] = &depth.front;
	faces[1] = &depth.back;
	for (face = 0U; raster->stencil_test && face < 2U; face++) {
		faces[face]->failOp = draw_stencil_op(raster->stencil_fail[face]);
		faces[face]->passOp = draw_stencil_op(raster->stencil_zpass[face]);
		faces[face]->depthFailOp = draw_stencil_op(raster->stencil_zfail[face]);
		faces[face]->compareOp = (VkCompareOp)(raster->stencil_func[face] - GL_NEVER);
	}

	/* The depth bounds test is off. */
	depth.maxDepthBounds = 1.0f;

	/* Blending and the colour mask of each colour attachment. */
	memset(blend_attachments, 0, sizeof(blend_attachments));
	for (index = 0U; index < raster->color_count; index++) {
		blend_attachments[index].blendEnable = (raster->blend >> index) & 1U;
		blend_attachments[index].srcColorBlendFactor = draw_blend_factor(raster->blend_src_rgb);
		blend_attachments[index].dstColorBlendFactor = draw_blend_factor(raster->blend_dst_rgb);
		blend_attachments[index].colorBlendOp = draw_blend_op(raster->blend_equation_rgb);
		blend_attachments[index].srcAlphaBlendFactor = draw_blend_factor(raster->blend_src_alpha);
		blend_attachments[index].dstAlphaBlendFactor = draw_blend_factor(raster->blend_dst_alpha);
		blend_attachments[index].alphaBlendOp = draw_blend_op(raster->blend_equation_alpha);
		blend_attachments[index].colorWriteMask = (raster->color_mask >> (index * 4U)) & 15U;
	}

	/* The attachments' states. */
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = raster->color_count;
	blend.pAttachments = blend_attachments;

	/* The dynamic states. */
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]);
	dynamic.pDynamicStates = dynamic_states;

	/* The pipeline. */
	entry = calloc(1U, sizeof(*entry));
	if (entry == NULL)
		return VK_NULL_HANDLE;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create.stageCount = 2U;
	create.pStages = stages;
	create.pVertexInputState = &vertex;
	create.pInputAssemblyState = &assembly;
	create.pViewportState = &viewport;
	create.pRasterizationState = &rasterization;
	create.pMultisampleState = &multisample;
	create.pDepthStencilState = &depth;
	create.pColorBlendState = &blend;
	create.pDynamicState = &dynamic;
	create.layout = state->program->layout;
	create.renderPass = target->pass;
	result = vkCreateGraphicsPipelines(state->device, VK_NULL_HANDLE, 1U, &create, NULL, &entry->pipeline);
	if (result != VK_SUCCESS) {
		gles_report("vkCreateGraphicsPipelines", (int)result);
		free(entry);
		return VK_NULL_HANDLE;
	}

	/* Succeeded: kept for the next draw with the same key. */
	entry->key = key;
	entry->next = state->pipelines;
	state->pipelines = entry;
	return entry->pipeline;
}

/*
 * Finds the buffer range each named uniform block of the current program
 * reads (the one bound to the block's binding point, or its whole buffer
 * from the offset), brings the buffer's device copy up to date, and
 * describes it.  Returns 0, or -1 with the error recorded when a block's
 * binding point has no buffer, or a range smaller than the block.
 */
static int
draw_blocks(
	struct zegl_context *context,
	struct gles_state *state,
	VkDescriptorBufferInfo *blocks)
{
	struct gles_program *program;
	struct gles_block *block;
	struct gles_buffer_range *range;
	struct gles_buffer *buffer;
	size_t available;
	size_t length;
	unsigned index;
	int status;

	/* Each block (a binding no stage reads has no descriptor). */
	program = state->program;
	memset(blocks, 0, GLES_NAMED_BLOCKS * sizeof(*blocks));
	for (index = 0U; index < program->block_count; index++) {
		block = &program->blocks[index];
		if (block->stages == 0U)
			continue;

		/* The buffer bound to its binding point. */
		range = &state->uniform_ranges[block->buffer_binding];
		buffer = range->buffer;
		if (buffer == NULL) {
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

		/* A range the block does not fit in is an error. */
		if (length == 0U || length < block->size) {
			gles_error(context, GL_INVALID_OPERATION);
			return -1;
		}

		/* The device reads at most its largest uniform buffer range. */
		if (length > state->limits.maxUniformBufferRange)
			length = state->limits.maxUniformBufferRange;

		/*
		 * The buffer's device copy, up to date; the frame's use of it
		 * keeps a later change from writing it in place.
		 */
		status = gles_buffer_sync(state, buffer);
		if (status != 0) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return -1;
		}

		/* This frame reads it. */
		buffer->used = state->frame;

		/* The descriptor's range. */
		blocks[index].buffer = buffer->buffer;
		blocks[index].offset = range->offset;
		blocks[index].range = length;
	}

	/* Succeeded: every block has its range. */
	return 0;
}

/*
 * Returns a descriptor set for the current program: its uniform block
 * (dynamic: the offset of this draw's copy in the stream is returned in
 * *offset), each sampler's texture (black when the unit has none that
 * can be sampled), and the named blocks' buffer ranges.  A draw with the
 * same program, stream buffer, textures and ranges as the one before
 * reuses its set.  VK_NULL_HANDLE when there is no memory.
 */
static VkDescriptorSet
draw_descriptors(
	struct gles_state *state,
	const VkDescriptorBufferInfo *blocks,
	const VkDescriptorBufferInfo *capture,
	uint32_t *offset)
{
	VkDescriptorPoolSize sizes[5];
	VkDescriptorPoolCreateInfo create;
	VkDescriptorSetAllocateInfo allocate;
	VkWriteDescriptorSet writes[GLES_UNITS + 2U + GLES_NAMED_BLOCKS];
	VkDescriptorBufferInfo block;
	VkDescriptorImageInfo images[GLES_UNITS];
	VkBufferView texel_views[GLES_UNITS];
	uint32_t bindings[GLES_UNITS];
	struct gles_set_cache *cache;
	struct gles_program *program;
	struct gles_texture *texture;
	const struct gles_sampling *sampling;
	struct gles_pool *pool;
	VkDescriptorSet set;
	VkDeviceSize place;
	void *data;
	uint32_t count;
	uint32_t samplers;
	unsigned index;
	int status;
	int unit;
	unsigned shape;
	unsigned kind;
	int same;
	int differs;
	VkResult result;

	/* This draw's copy of the uniform block. */
	program = state->program;
	cache = &state->set_cache;
	*offset = 0U;
	memset(&block, 0, sizeof(block));
	if (program->uniform_data != NULL) {
		data = gles_stream(state, program->uniform_size, (size_t)state->limits.minUniformBufferOffsetAlignment,
				   &block.buffer, &place);
		if (data == NULL)
			return VK_NULL_HANDLE;
		memcpy(data, program->uniform_data, program->uniform_size);
		*offset = (uint32_t)place;
		block.range = program->uniform_size;
	}

	/* Each sampler's texture, up to date, with its sampler (a buffer texture's view of its texels). */
	samplers = 0U;
	memset(images, 0, sizeof(images));
	memset(texel_views, 0, sizeof(texel_views));
	for (index = 0U; index < program->uniform_count && samplers < GLES_UNITS; index++) {
		if (!program->uniforms[index].sampler)
			continue;
		shape = draw_sampler_shape(program->uniforms[index].type);
		kind = draw_sampler_kind(program->uniforms[index].type);
		unit = program->uniforms[index].unit;

		/* A buffer texture: the view of its buffer's texels (zeros without one). */
		if (shape == GLES_SHAPE_BUFFER) {
			texture = NULL;
			if (unit >= 0 && (unsigned)unit < GLES_UNITS)
				texture = state->buffer_units[unit];
			texel_views[samplers] = gles_texture_buffer_view(state, texture, kind);
			if (texel_views[samplers] == VK_NULL_HANDLE)
				return VK_NULL_HANDLE;
			bindings[samplers] = program->uniforms[index].binding;
			samplers++;
			continue;
		}

		/* The unit's texture of the sampler's target, and the unit's sampler object's sampling (NULL: the texture's own). */
		texture = NULL;
		sampling = NULL;
		if (unit >= 0 && (unsigned)unit < GLES_UNITS) {
			texture = state->units[unit];
			if (shape == GLES_SHAPE_CUBE)
				texture = state->cube_units[unit];
			if (shape == GLES_SHAPE_3D)
				texture = state->volume_units[unit];
			if (shape == GLES_SHAPE_ARRAY)
				texture = state->array_units[unit];
			if (shape == GLES_SHAPE_RECT)
				texture = state->rect_units[unit];
			if (state->unit_samplers[unit] != NULL)
				sampling = &state->unit_samplers[unit]->sampling;
		}

		/* One that cannot be sampled so, or holds texels of another kind than the sampler reads, reads as black (a rectangle's as a 2D texture's). */
		status = gles_texture_complete(texture, sampling);
		if (status)
			status = draw_texture_matches(texture, kind);
		if (!status && shape == GLES_SHAPE_RECT)
			shape = GLES_SHAPE_2D;
		if (!status) {
			texture = gles_texture_black(state, shape, kind);
			sampling = NULL;
		}

		/* The texture's image, up to date. */
		if (texture == NULL)
			return VK_NULL_HANDLE;
		status = gles_texture_sync(state, texture);
		if (status != 0)
			return VK_NULL_HANDLE;
		texture->used = state->frame;

		/* The image and its sampler. */
		images[samplers].sampler = gles_sampler_get(state, texture, sampling);
		images[samplers].imageView = texture->view;
		images[samplers].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		bindings[samplers] = program->uniforms[index].binding;
		if (images[samplers].sampler == VK_NULL_HANDLE)
			return VK_NULL_HANDLE;
		samplers++;
	}

	/* The set of the draw before, when it describes the same things. */
	same = 0;
	if (cache->set != VK_NULL_HANDLE && cache->program == program->serial && cache->block == block.buffer &&
	    cache->count == samplers)
		same = 1;
	differs = 0;
	if (same && samplers != 0U)
		differs = memcmp(cache->images, images, samplers * sizeof(images[0]));
	if (differs != 0)
		same = 0;

	/* And the same buffer textures' views. */
	differs = 0;
	if (same && samplers != 0U)
		differs = memcmp(cache->texel_views, texel_views, samplers * sizeof(texel_views[0]));
	if (differs != 0)
		same = 0;

	/* The same blocks' ranges too; a draw's capture buffer is its own. */
	differs = 0;
	if (same && program->block_count != 0U)
		differs = memcmp(cache->blocks, blocks, program->block_count * sizeof(blocks[0]));
	if (differs != 0 || capture != NULL)
		same = 0;
	if (same)
		return cache->set;

	/* A set from the first pool with room, else from a new pool. */
	set = VK_NULL_HANDLE;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorSetCount = 1U;
	allocate.pSetLayouts = &program->set_layout;
	result = VK_ERROR_OUT_OF_POOL_MEMORY;
	for (pool = state->pools; pool != NULL; pool = pool->next) {
		allocate.descriptorPool = pool->pool;
		result = vkAllocateDescriptorSets(state->device, &allocate, &set);
		if (result == VK_SUCCESS)
			break;
	}

	/* No room: a new pool at the front. */
	if (result != VK_SUCCESS) {
		pool = calloc(1U, sizeof(*pool));
		if (pool == NULL)
			return VK_NULL_HANDLE;
		sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		sizes[0].descriptorCount = DRAW_POOL_SETS;
		sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[1].descriptorCount = DRAW_POOL_SETS * GLES_UNITS;
		sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sizes[2].descriptorCount = DRAW_POOL_SETS * GLES_NAMED_BLOCKS;
		sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		sizes[3].descriptorCount = DRAW_POOL_SETS;
		sizes[4].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		sizes[4].descriptorCount = DRAW_POOL_SETS * GLES_UNITS;
		memset(&create, 0, sizeof(create));
		create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		create.maxSets = DRAW_POOL_SETS;
		create.poolSizeCount = 5U;
		create.pPoolSizes = sizes;
		result = vkCreateDescriptorPool(state->device, &create, NULL, &pool->pool);
		if (result != VK_SUCCESS) {
			free(pool);
			return VK_NULL_HANDLE;
		}

		/* The pool goes to the front of the list. */
		pool->next = state->pools;
		state->pools = pool;
		allocate.descriptorPool = pool->pool;
		result = vkAllocateDescriptorSets(state->device, &allocate, &set);
		if (result != VK_SUCCESS)
			return VK_NULL_HANDLE;
	}

	/* The uniform block (its offset is the draw's dynamic offset). */
	count = 0U;
	memset(writes, 0, sizeof(writes));
	if (program->uniform_data != NULL) {
		writes[count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[count].dstSet = set;
		writes[count].dstBinding = program->uniform_binding;
		writes[count].descriptorCount = 1U;
		writes[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		writes[count].pBufferInfo = &block;
		count++;
	}

	/* The samplers (a buffer texture's texels as a texel buffer). */
	for (index = 0U; index < samplers; index++) {
		writes[count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[count].dstSet = set;
		writes[count].dstBinding = bindings[index];
		writes[count].descriptorCount = 1U;
		if (texel_views[index] != VK_NULL_HANDLE) {
			writes[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			writes[count].pTexelBufferView = &texel_views[index];
		} else {
			writes[count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[count].pImageInfo = &images[index];
		}

		/* Counted. */
		count++;
	}

	/* The capture buffer (transform feedback). */
	if (capture != NULL) {
		writes[count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[count].dstSet = set;
		writes[count].dstBinding = GLES_CAPTURE_BINDING;
		writes[count].descriptorCount = 1U;
		writes[count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[count].pBufferInfo = capture;
		count++;
	}

	/* The named blocks' ranges. */
	for (index = 0U; index < program->block_count; index++) {
		if (program->blocks[index].stages == 0U)
			continue;
		writes[count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[count].dstSet = set;
		writes[count].dstBinding = program->blocks[index].binding;
		writes[count].descriptorCount = 1U;
		writes[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[count].pBufferInfo = &blocks[index];
		count++;
	}

	/* Written. */
	if (count != 0U)
		vkUpdateDescriptorSets(state->device, count, writes, 0U, NULL);

	/* Succeeded: the set, kept for the next draw like this one. */
	cache->set = set;
	cache->program = program->serial;
	cache->block = block.buffer;
	cache->count = samplers;
	memcpy(cache->images, images, sizeof(images));
	memcpy(cache->texel_views, texel_views, sizeof(texel_views));
	cache->block_count = program->block_count;
	memcpy(cache->blocks, blocks, GLES_NAMED_BLOCKS * sizeof(blocks[0]));
	return set;
}

/* Sets an attribute's current value from four integers' bits of a type (GL_INT or GL_UNSIGNED_INT). */
static void
draw_vertex_attrib_integer(
	GLuint index,
	GLenum type,
	const uint32_t *values)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state and an attribute. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The integers' bits, kept where the floats would be, and their type. */
	memcpy(state->attribs[index].value, values, 4U * sizeof(uint32_t));
	state->attribs[index].value_type = type;
}

/* Records the dynamic state: viewport (GL's, turned over), scissor, line width, depth bias, blend colour, stencil values. */
static void
draw_dynamic(
	struct gles_state *state,
	struct zegl_context *context,
	const struct gles_target *target)
{
	VkViewport viewport;
	VkRect2D scissor;
	float width;
	float height;

	/* The viewport, from the top of a window (a zero size is made one pixel so Vulkan takes it). */
	width = (float)context->gles.viewport[2];
	height = (float)context->gles.viewport[3];
	if (width < 1.0f)
		width = 1.0f;
	if (height < 1.0f)
		height = 1.0f;
	viewport.x = (float)context->gles.viewport[0];
	viewport.y = (float)context->gles.viewport[1];
	if (target->flip)
		viewport.y = (float)target->extent.height - (float)context->gles.viewport[1] - height;
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = state->depth_near;
	viewport.maxDepth = state->depth_far;
	vkCmdSetViewport(target->command, 0U, 1U, &viewport);

	/* The scissor box, or the whole surface. */
	scissor = draw_scissor_rect(state, target);
	vkCmdSetScissor(target->command, 0U, 1U, &scissor);

	/* Lines are one pixel wide (the device is made without wide lines). */
	vkCmdSetLineWidth(target->command, 1.0f);

	/* The polygon offset, the blend colour, and the stencil values of both faces. */
	vkCmdSetDepthBias(target->command, state->polygon_units, 0.0f, state->polygon_factor);
	vkCmdSetBlendConstants(target->command, state->blend_color);
	vkCmdSetStencilCompareMask(target->command, VK_STENCIL_FACE_FRONT_BIT, state->stencil_value_mask[0]);
	vkCmdSetStencilCompareMask(target->command, VK_STENCIL_FACE_BACK_BIT, state->stencil_value_mask[1]);
	vkCmdSetStencilWriteMask(target->command, VK_STENCIL_FACE_FRONT_BIT, state->stencil_write_mask[0]);
	vkCmdSetStencilWriteMask(target->command, VK_STENCIL_FACE_BACK_BIT, state->stencil_write_mask[1]);
	vkCmdSetStencilReference(target->command, VK_STENCIL_FACE_FRONT_BIT, (uint32_t)state->stencil_ref[0]);
	vkCmdSetStencilReference(target->command, VK_STENCIL_FACE_BACK_BIT, (uint32_t)state->stencil_ref[1]);
}

/*
 * Clears one buffer of the draw framebuffer to a value (glClearBuffer*):
 * the colour attachment draw buffer drawbuffer writes (GL_COLOR), or the
 * aspects of the depth and stencil buffer (drawbuffer 0), inside the
 * scissor box when the test is on; a buffer of GL_INVALID_ENUM is an
 * error of that name.
 */
static void
draw_clear_buffer(
	GLenum buffer,
	GLint drawbuffer,
	const VkClearValue *value,
	VkImageAspectFlags aspects)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_target target;
	VkClearAttachment attachment;
	VkClearRect rect;
	uint32_t channels;
	int status;

	/* A context with its state, and a buffer of the call's kind. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (buffer == GL_INVALID_ENUM) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A draw buffer there is (the depth and stencil buffer is draw buffer 0). */
	if (drawbuffer < 0 ||
	    (buffer == GL_COLOR && drawbuffer >= (GLint)GLES_DRAW_BUFFERS) ||
	    (buffer != GL_COLOR && drawbuffer != 0)) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A conditional rendering whose query saw nothing clears nothing. */
	if (state->conditional_skip)
		return;

	/* The target's pass. */
	status = gles_target_open(context, state, NULL, &target);
	if (status != 0)
		return;

	/* The colour attachment the draw buffer writes (none: nothing), or the depth and stencil aspects the target has and writes. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.clearValue = *value;
	if (buffer == GL_COLOR) {
		if ((target.draw_mask & (1U << (unsigned)drawbuffer)) == 0U)
			return;
		channels = draw_channels(state, (unsigned)drawbuffer);
		if (channels == 0U)
			return;
		attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachment.colorAttachment = (uint32_t)drawbuffer;
	} else {
		attachment.aspectMask = aspects & target.depth_aspects;
		if (!state->depth_mask)
			attachment.aspectMask &= ~(VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT;
		if (attachment.aspectMask == 0U)
			return;
	}

	/* The clear over the scissor box or the whole target. */
	memset(&rect, 0, sizeof(rect));
	rect.rect = draw_scissor_rect(state, &target);
	rect.layerCount = 1U;
	if (rect.rect.extent.width == 0U || rect.rect.extent.height == 0U)
		return;
	vkCmdClearAttachments(target.command, 1U, &attachment, 1U, &rect);
}

/* Returns the scissor box in Vulkan's coordinates inside the target, or the whole target when the test is off. */
static VkRect2D
draw_scissor_rect(
	struct gles_state *state,
	const struct gles_target *target)
{
	VkRect2D rect;
	int32_t left;
	int32_t right;
	int32_t top;
	int32_t bottom;

	/* The whole target. */
	rect.offset.x = 0;
	rect.offset.y = 0;
	rect.extent = target->extent;
	if (!state->scissor_test)
		return rect;

	/* The box, from GL's bottom-left origin to a window's top-left one (a framebuffer object keeps GL's rows). */
	left = state->scissor[0];
	right = state->scissor[0] + state->scissor[2];
	top = state->scissor[1];
	bottom = state->scissor[1] + state->scissor[3];
	if (target->flip) {
		top = (int32_t)target->extent.height - (state->scissor[1] + state->scissor[3]);
		bottom = (int32_t)target->extent.height - state->scissor[1];
	}

	/* Clipped to the target. */
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > (int32_t)target->extent.width)
		right = (int32_t)target->extent.width;
	if (bottom > (int32_t)target->extent.height)
		bottom = (int32_t)target->extent.height;
	if (right < left)
		right = left;
	if (bottom < top)
		bottom = top;

	/* The clipped box. */
	rect.offset.x = left;
	rect.offset.y = top;
	rect.extent.width = (uint32_t)(right - left);
	rect.extent.height = (uint32_t)(bottom - top);
	return rect;
}

/* Returns Vulkan's blend factor for a GL one. */
static VkBlendFactor
draw_blend_factor(
	GLenum factor)
{
	/* The factors of OpenGL ES 2. */
	switch (factor) {
	case GL_ZERO:
		return VK_BLEND_FACTOR_ZERO;
	case GL_SRC_COLOR:
		return VK_BLEND_FACTOR_SRC_COLOR;
	case GL_ONE_MINUS_SRC_COLOR:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case GL_DST_COLOR:
		return VK_BLEND_FACTOR_DST_COLOR;
	case GL_ONE_MINUS_DST_COLOR:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case GL_SRC_ALPHA:
		return VK_BLEND_FACTOR_SRC_ALPHA;
	case GL_ONE_MINUS_SRC_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case GL_DST_ALPHA:
		return VK_BLEND_FACTOR_DST_ALPHA;
	case GL_ONE_MINUS_DST_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case GL_CONSTANT_COLOR:
		return VK_BLEND_FACTOR_CONSTANT_COLOR;
	case GL_ONE_MINUS_CONSTANT_COLOR:
		return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
	case GL_CONSTANT_ALPHA:
		return VK_BLEND_FACTOR_CONSTANT_ALPHA;
	case GL_ONE_MINUS_CONSTANT_ALPHA:
		return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
	case GL_SRC_ALPHA_SATURATE:
		return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	default:
		break;
	}

	/* GL_ONE (and a pipeline without blending). */
	return VK_BLEND_FACTOR_ONE;
}

/* Returns Vulkan's blend operation for a GL equation. */
static VkBlendOp
draw_blend_op(
	GLenum equation)
{
	/* The three equations of OpenGL ES 2, and the two of EXT_blend_minmax. */
	switch (equation) {
	case GL_FUNC_SUBTRACT:
		return VK_BLEND_OP_SUBTRACT;
	case GL_FUNC_REVERSE_SUBTRACT:
		return VK_BLEND_OP_REVERSE_SUBTRACT;
	case GL_MIN_EXT:
		return VK_BLEND_OP_MIN;
	case GL_MAX_EXT:
		return VK_BLEND_OP_MAX;
	default:
		break;
	}

	/* GL_FUNC_ADD. */
	return VK_BLEND_OP_ADD;
}

/* Returns Vulkan's stencil operation for a GL one. */
static VkStencilOp
draw_stencil_op(
	GLenum op)
{
	/* The operations of OpenGL ES 2. */
	switch (op) {
	case GL_ZERO:
		return VK_STENCIL_OP_ZERO;
	case GL_REPLACE:
		return VK_STENCIL_OP_REPLACE;
	case GL_INCR:
		return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	case GL_DECR:
		return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
	case GL_INVERT:
		return VK_STENCIL_OP_INVERT;
	case GL_INCR_WRAP:
		return VK_STENCIL_OP_INCREMENT_AND_WRAP;
	case GL_DECR_WRAP:
		return VK_STENCIL_OP_DECREMENT_AND_WRAP;
	default:
		break;
	}

	/* GL_KEEP. */
	return VK_STENCIL_OP_KEEP;
}

/*
 * Returns the index that restarts primitives for indices of a type: the
 * largest of the type with GL_PRIMITIVE_RESTART_FIXED_INDEX on (which
 * takes precedence), else desktop GL's GL_PRIMITIVE_RESTART_INDEX.
 */
static uint32_t
draw_restart_index(
	const struct gles_state *state,
	GLenum type)
{
	/* Desktop GL's own index, when only its restarts are on. */
	if (!state->primitive_restart && state->primitive_restart_any)
		return state->restart_index;

	/* The largest of the type. */
	if (type == GL_UNSIGNED_BYTE)
		return 0xffU;
	if (type == GL_UNSIGNED_SHORT)
		return 0xffffU;

	/* Succeeded: the largest 32-bit index. */
	return 0xffffffffU;
}

/* Draws indexed primitives with a base vertex added to each index (restart indices left as they are). */
static void
draw_base_vertex(
	GLenum mode,
	GLsizei count,
	GLenum type,
	const void *indices,
	GLsizei instances,
	GLint basevertex)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;

	/* The draw, with the base vertex for its indices only. */
	if (type == GL_NONE)
		type = GL_INVALID_ENUM;
	state->base_vertex = basevertex;
	draw_primitives(mode, 0, count, type, indices, instances);
	state->base_vertex = 0;
}
