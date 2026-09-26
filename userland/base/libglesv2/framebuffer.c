/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Framebuffer and renderbuffer objects of zedBSD's OpenGL ES (WS068
 * p022), and the target every draw, clear and read goes to.
 *
 * A framebuffer object draws into the level 0 of a 2D texture or into a
 * renderbuffer, in the draw surface's frame: its render pass is recorded
 * into the same command buffer as the surface's own passes, and stays
 * open until something else records into the frame or the frame is
 * submitted (libEGL's frame_closing).  Its images keep GL's rows from the
 * bottom up, as textures do, so its draws do not turn y over.  A texture
 * drawn into this way is newer on the device than on the CPU until
 * gles_texture_fetch reads it back.
 */

#include "gles.h"

#include <stdlib.h>
#include <string.h>

/* The size a renderbuffer may have (GL_MAX_RENDERBUFFER_SIZE is at most this). */
#define FRAMEBUFFER_SIZE_MAX	16384

static struct gles_framebuffer *framebuffer_bound(struct gles_state *state);
static GLenum framebuffer_status(struct gles_state *state, struct gles_framebuffer *fbo, struct gles_texture **texture, struct gles_renderbuffer **color, struct gles_renderbuffer **depth);
static GLenum framebuffer_build(struct gles_state *state, struct gles_framebuffer *fbo);
static void framebuffer_mark(struct gles_state *state, struct gles_framebuffer *fbo);
static void framebuffer_forget_views(struct gles_state *state, struct gles_framebuffer *fbo);
static VkResult framebuffer_pass(struct gles_state *state, int has_color, int has_depth, VkImageLayout color_layout, VkRenderPass *pass);
static VkRenderPass framebuffer_compatible(struct gles_state *state, int has_color, int has_depth);
static struct gles_renderbuffer *renderbuffer_bound(struct zegl_context *context, GLenum target);
static int renderbuffer_depth_format(GLenum format);
static int renderbuffer_image(struct gles_state *state, struct gles_renderbuffer *renderbuffer);
static void renderbuffer_discard(struct gles_state *state, struct gles_renderbuffer *renderbuffer);
static void renderbuffer_free(struct gles_state *state, struct gles_renderbuffer *renderbuffer);
static int framebuffer_point(GLenum attachment, struct gles_attachment **point, struct gles_framebuffer *fbo);

/*
 * Opens where a draw, clear or read goes: the draw surface's frame, and
 * in it the surface's render pass (clearing with clear when it is the
 * frame's first) or the bound framebuffer object's.  Returns 0, or -1 with
 * the error recorded (no draw surface, an incomplete framebuffer, no
 * memory).
 */
int
gles_target_open(
	struct zegl_context *context,
	struct gles_state *state,
	const VkClearValue *clear,
	struct gles_target *target)
{
	struct zegl_surface *surface;
	struct gles_framebuffer *fbo;
	VkRenderPassBeginInfo begin;
	VkRenderPass pass;
	GLenum status;
	EGLint error;

	/* A draw surface, whose frame records everything. */
	surface = context->draw;
	if (surface == NULL) {
		gles_error(context, GL_INVALID_FRAMEBUFFER_OPERATION);
		return -1;
	}

	/* The frame, opened when this is its first command. */
	error = zegl_frame_begin(surface);
	if (error != EGL_SUCCESS) {
		gles_report("the frame", error);
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* The target starts as the surface's own images. */
	memset(target, 0, sizeof(*target));
	target->surface = surface;
	target->command = surface->command;

	/* Framebuffer 0: the surface's pass, after any framebuffer object's. */
	if (state->framebuffer == 0U) {
		gles_target_close(state);
		zegl_frame_pass(surface, clear);
		target->pass = surface->pass;
		target->extent = surface->extent;
		target->flip = 1;
		target->has_color = 1;
		if (surface->depth_format != VK_FORMAT_UNDEFINED)
			target->depth_aspects = surface->depth_aspects;
		return 0;
	}

	/* A framebuffer object: complete, with its pass and framebuffer made for what is attached now. */
	fbo = framebuffer_bound(state);
	status = GL_FRAMEBUFFER_UNSUPPORTED;
	if (fbo != NULL)
		status = framebuffer_build(state, fbo);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		gles_error(context, GL_INVALID_FRAMEBUFFER_OPERATION);
		return -1;
	}

	/* The pass its pipelines are made with. */
	pass = framebuffer_compatible(state, fbo->built_color != VK_NULL_HANDLE, fbo->built_depth != VK_NULL_HANDLE);
	if (pass == VK_NULL_HANDLE) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* Its pass entered, unless it is the one open: the surface's pass or another object's ends first. */
	if (state->open_fbo != fbo) {
		gles_target_close(state);
		zegl_frame_leave_pass(surface);
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = fbo->pass;
		begin.framebuffer = fbo->framebuffer;
		begin.renderArea.extent = fbo->extent;
		vkCmdBeginRenderPass(surface->command, &begin, VK_SUBPASS_CONTENTS_INLINE);

		/*
		 * The pass is open in this surface's frame until gles_target_close;
		 * recorded makes a pbuffer's frame be submitted even when nothing
		 * else was drawn.
		 */
		state->open_fbo = fbo;
		state->open_surface = surface;
		surface->recorded = 1;
	}

	/* The attached objects are drawn into by this frame. */
	framebuffer_mark(state, fbo);

	/* Succeeded: the framebuffer object, with GL's rows. */
	target->fbo = fbo;
	target->pass = pass;
	target->extent = fbo->extent;
	target->has_color = fbo->built_color != VK_NULL_HANDLE;
	target->depth_aspects = fbo->depth_aspects;
	return 0;
}

/*
 * Ends the framebuffer object's render pass open in a frame, if there is
 * one, so the frame can go on with something else or be submitted.
 */
void
gles_target_close(
	struct gles_state *state)
{
	/* Nothing is open. */
	if (state->open_fbo == NULL)
		return;

	/* The pass ends in the frame it was opened in; the images rest in their layouts again. */
	vkCmdEndRenderPass(state->open_surface->command);
	state->open_fbo = NULL;
	state->open_surface = NULL;
}

/*
 * Detaches a deleted texture or renderbuffer from the bound framebuffer
 * object (GL detaches it only there; elsewhere its name is looked up again
 * and finds nothing).
 */
void
gles_framebuffers_forget(
	struct gles_state *state,
	int kind,
	GLuint name)
{
	struct gles_framebuffer *fbo;

	/* The bound framebuffer object, if there is one. */
	fbo = framebuffer_bound(state);
	if (fbo == NULL)
		return;

	/* Each attachment point that names the object. */
	if (fbo->color.kind == kind && fbo->color.name == name)
		fbo->color.kind = GLES_ATTACH_NONE;
	if (fbo->depth.kind == kind && fbo->depth.name == name)
		fbo->depth.kind = GLES_ATTACH_NONE;
	if (fbo->stencil.kind == kind && fbo->stencil.name == name)
		fbo->stencil.kind = GLES_ATTACH_NONE;
}

/*
 * Frees every framebuffer object and renderbuffer of a context, and the
 * render passes its pipelines were made with (nothing may still run).
 */
void
gles_framebuffers_release(
	struct gles_state *state)
{
	struct gles_framebuffer *fbo;
	struct gles_renderbuffer *renderbuffer;
	GLuint name;
	unsigned color;
	unsigned depth;

	/* The framebuffer objects, whose passes and framebuffers wait for the garbage. */
	state->open_fbo = NULL;
	state->open_surface = NULL;
	for (name = 1U; name < state->framebuffers.capacity; name++) {
		fbo = state->framebuffers.objects[name];
		if (fbo == NULL)
			continue;

		/* Its pass and framebuffer, then the object. */
		framebuffer_forget_views(state, fbo);
		state->framebuffers.objects[name] = NULL;
		free(fbo);
	}

	/* The renderbuffers. */
	for (name = 1U; name < state->renderbuffers.capacity; name++) {
		renderbuffer = state->renderbuffers.objects[name];
		if (renderbuffer == NULL)
			continue;

		/* Its image, then the object. */
		state->renderbuffers.objects[name] = NULL;
		renderbuffer_free(state, renderbuffer);
	}

	/* The passes pipelines were made with. */
	for (color = 0U; color < 2U; color++) {
		for (depth = 0U; depth < 2U; depth++) {
			if (state->fbo_passes[color][depth] != VK_NULL_HANDLE)
				vkDestroyRenderPass(state->device, state->fbo_passes[color][depth], NULL);
			state->fbo_passes[color][depth] = VK_NULL_HANDLE;
		}
	}

	/* The name tables. */
	free(state->framebuffers.objects);
	free(state->renderbuffers.objects);
	memset(&state->framebuffers, 0, sizeof(state->framebuffers));
	memset(&state->renderbuffers, 0, sizeof(state->renderbuffers));
}

/*
 * Brings a texture's level 0 on the CPU up to date with what framebuffer
 * objects drew into its image, before the CPU changes the texture.
 * Returns 0, or -1 with the error recorded.
 */
int
gles_texture_fetch(
	struct zegl_context *context,
	struct gles_texture *texture)
{
	struct gles_state *state;
	struct zegl_surface *surface;
	VkImageMemoryBarrier barrier;
	VkBufferImageCopy copy;
	VkBuffer buffer;
	VkDeviceSize offset;
	unsigned char *mapped;
	unsigned char *pixels;
	size_t bytes;
	EGLint error;

	/* Nothing was drawn into it since its levels were read. */
	if (!texture->gpu_written)
		return 0;

	/* The frame that drew into it is the draw surface's; without one there is nothing to read. */
	state = gles_state(context);
	surface = context->draw;
	if (state == NULL ||
	    surface == NULL ||
	    texture->image == VK_NULL_HANDLE) {
		texture->gpu_written = 0;
		return 0;
	}

	/* The frame, with no pass open. */
	gles_target_close(state);
	error = zegl_frame_begin(surface);
	if (error != EGL_SUCCESS) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* No pass of the surface either. */
	zegl_frame_leave_pass(surface);

	/* Room in the stream for level 0. */
	bytes = (size_t)texture->levels[0].width * (size_t)texture->levels[0].height * 4U;
	mapped = gles_stream(state, bytes, 16U, &buffer, &offset);
	if (mapped == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* Level 0 copied out between two layout changes (its rows are GL's already). */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = texture->image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(surface->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			     0U, 0U, NULL, 0U, NULL, 1U, &barrier);

	/* The copy of level 0. */
	memset(&copy, 0, sizeof(copy));
	copy.bufferOffset = offset;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = (uint32_t)texture->levels[0].width;
	copy.imageExtent.height = (uint32_t)texture->levels[0].height;
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(surface->command, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1U, &copy);

	/* Back to be sampled. */
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(surface->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			     0U, 0U, NULL, 0U, NULL, 1U, &barrier);
	surface->recorded = 1;

	/* Done before the bytes are read. */
	error = zegl_frame_flush(surface);
	if (error != EGL_SUCCESS) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* Level 0's pixels on the CPU (a level made without data has none yet). */
	pixels = texture->levels[0].pixels;
	if (pixels == NULL) {
		pixels = malloc(bytes);
		if (pixels == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return -1;
		}

		/* The level keeps them. */
		texture->levels[0].pixels = pixels;
	}

	/* The bytes, and the CPU's copy is the newest again. */
	memcpy(pixels, mapped, bytes);
	texture->gpu_written = 0;

	/* Everything recorded before is done: the frame's resources are free again. */
	state->frame++;
	gles_collect(state);

	/* Succeeded: level 0 is what the device drew. */
	return 0;
}

/*
 * Makes framebuffer names.
 */
GL_APICALL void GL_APIENTRY
glGenFramebuffers(
	GLsizei n,
	GLuint *framebuffers)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLsizei index;
	GLuint name;

	/* A context with its state and a count. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Free names; the objects are made at their first bind. */
	for (index = 0; index < n; index++) {
		name = gles_names_free(&state->framebuffers);
		framebuffers[index] = name;
	}
}

/*
 * Deletes framebuffer objects, binding 0 in place of a deleted one.
 */
GL_APICALL void GL_APIENTRY
glDeleteFramebuffers(
	GLsizei n,
	const GLuint *framebuffers)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_framebuffer *fbo;
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

	/* Each name that is a framebuffer object. */
	for (index = 0; index < n; index++) {
		fbo = gles_names_get(&state->framebuffers, framebuffers[index]);
		if (fbo == NULL)
			continue;

		/* Its pass ends if it is open, the bound one goes back to 0. */
		if (state->open_fbo == fbo)
			gles_target_close(state);
		if (state->framebuffer == framebuffers[index])
			state->framebuffer = 0U;

		/* The name, the pass and framebuffer (they wait for the frame), and the object go. */
		gles_names_remove(&state->framebuffers, framebuffers[index]);
		framebuffer_forget_views(state, fbo);
		free(fbo);
	}
}

/*
 * Binds a framebuffer (0: the draw surface's), making the object when the
 * name is new.
 */
GL_APICALL void GL_APIENTRY
glBindFramebuffer(
	GLenum target,
	GLuint framebuffer)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_framebuffer *fbo;
	int status;

	/* A context with its state and the one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_FRAMEBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A name seen for the first time gets its object, with nothing attached. */
	fbo = NULL;
	if (framebuffer != 0U)
		fbo = gles_names_get(&state->framebuffers, framebuffer);
	if (framebuffer != 0U && fbo == NULL) {
		fbo = calloc(1U, sizeof(*fbo));
		if (fbo == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name finds it from now on. */
		fbo->name = framebuffer;
		status = gles_names_add(&state->framebuffers, framebuffer, fbo);
		if (status != 0) {
			free(fbo);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}
	}

	/* Bound; draws go there from now on. */
	state->framebuffer = framebuffer;
}

/*
 * Reports whether a name is a framebuffer object.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsFramebuffer(
	GLuint framebuffer)
{
	struct gles_state *state;
	struct gles_framebuffer *fbo;

	/* A context with its state. */
	state = gles_state(gles_context());
	if (state == NULL)
		return GL_FALSE;

	/* A name that was bound once. */
	fbo = gles_names_get(&state->framebuffers, framebuffer);
	if (fbo == NULL)
		return GL_FALSE;

	/* Succeeded: it is one. */
	return GL_TRUE;
}

/*
 * Reports whether the bound framebuffer can be drawn into, and when not,
 * why.
 */
GL_APICALL GLenum GL_APIENTRY
glCheckFramebufferStatus(
	GLenum target)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_framebuffer *fbo;
	struct gles_texture *texture;
	struct gles_renderbuffer *color;
	struct gles_renderbuffer *depth;
	GLenum status;

	/* A context with its state and the one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return 0U;
	if (target != GL_FRAMEBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return 0U;
	}

	/* The draw surface's framebuffer is complete. */
	if (state->framebuffer == 0U)
		return GL_FRAMEBUFFER_COMPLETE;

	/* A framebuffer object's attachments decide. */
	fbo = framebuffer_bound(state);
	status = GL_FRAMEBUFFER_UNSUPPORTED;
	if (fbo != NULL)
		status = framebuffer_status(state, fbo, &texture, &color, &depth);

	/* Succeeded: the status. */
	return status;
}

/*
 * Attaches level 0 of a 2D texture to the bound framebuffer object's
 * colour attachment (texture 0 detaches).
 */
GL_APICALL void GL_APIENTRY
glFramebufferTexture2D(
	GLenum target,
	GLenum attachment,
	GLenum textarget,
	GLuint texture,
	GLint level)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_framebuffer *fbo;
	struct gles_attachment *point;
	struct gles_texture *object;
	int status;

	/* A context with its state and the one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_FRAMEBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A framebuffer object bound (the draw surface's takes no attachments). */
	fbo = framebuffer_bound(state);
	if (fbo == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* An attachment point. */
	status = framebuffer_point(attachment, &point, fbo);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Texture 0 detaches. */
	if (texture == 0U) {
		point->kind = GLES_ATTACH_NONE;
		return;
	}

	/* A 2D texture's level 0 (cube map faces come with ws068-p023). */
	if (textarget != GL_TEXTURE_2D) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Level 0 only (OpenGL ES 2). */
	if (level != 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A name that is a texture. */
	object = gles_names_get(&state->textures, texture);
	if (object == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Attached by name; the pass is made again at the next draw. */
	if (state->open_fbo == fbo)
		gles_target_close(state);
	point->kind = GLES_ATTACH_TEXTURE;
	point->name = texture;
}

/*
 * Attaches a renderbuffer to the bound framebuffer object (renderbuffer 0
 * detaches).
 */
GL_APICALL void GL_APIENTRY
glFramebufferRenderbuffer(
	GLenum target,
	GLenum attachment,
	GLenum renderbuffertarget,
	GLuint renderbuffer)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_framebuffer *fbo;
	struct gles_attachment *point;
	struct gles_renderbuffer *object;
	int status;

	/* A context with its state and the one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_FRAMEBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A framebuffer object bound (the draw surface's takes no attachments). */
	fbo = framebuffer_bound(state);
	if (fbo == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* An attachment point. */
	status = framebuffer_point(attachment, &point, fbo);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Renderbuffer 0 detaches. */
	if (renderbuffer == 0U) {
		point->kind = GLES_ATTACH_NONE;
		return;
	}

	/* The renderbuffer target. */
	if (renderbuffertarget != GL_RENDERBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A name that is a renderbuffer. */
	object = gles_names_get(&state->renderbuffers, renderbuffer);
	if (object == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Attached by name; the pass is made again at the next draw. */
	if (state->open_fbo == fbo)
		gles_target_close(state);
	point->kind = GLES_ATTACH_RENDERBUFFER;
	point->name = renderbuffer;
}

/*
 * Reports what an attachment point of the bound framebuffer object has.
 */
GL_APICALL void GL_APIENTRY
glGetFramebufferAttachmentParameteriv(
	GLenum target,
	GLenum attachment,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_framebuffer *fbo;
	struct gles_attachment *point;
	int status;

	/* A context with its state and the one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_FRAMEBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A framebuffer object bound. */
	fbo = framebuffer_bound(state);
	if (fbo == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* An attachment point. */
	status = framebuffer_point(attachment, &point, fbo);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The parameter. */
	switch (pname) {
	case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
		*params = GL_NONE;
		if (point->kind == GLES_ATTACH_TEXTURE)
			*params = GL_TEXTURE;
		if (point->kind == GLES_ATTACH_RENDERBUFFER)
			*params = GL_RENDERBUFFER;
		break;
	case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
		*params = 0;
		if (point->kind != GLES_ATTACH_NONE)
			*params = (GLint)point->name;
		break;
	case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
	case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
		/* Level 0 of a 2D texture is all that is attached. */
		*params = 0;
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		break;
	}
}

/*
 * Makes renderbuffer names.
 */
GL_APICALL void GL_APIENTRY
glGenRenderbuffers(
	GLsizei n,
	GLuint *renderbuffers)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLsizei index;
	GLuint name;

	/* A context with its state and a count. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Free names; the objects are made at their first bind. */
	for (index = 0; index < n; index++) {
		name = gles_names_free(&state->renderbuffers);
		renderbuffers[index] = name;
	}
}

/*
 * Deletes renderbuffers, detaching them from the bound framebuffer object.
 */
GL_APICALL void GL_APIENTRY
glDeleteRenderbuffers(
	GLsizei n,
	const GLuint *renderbuffers)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_renderbuffer *renderbuffer;
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

	/* Each name that is a renderbuffer. */
	for (index = 0; index < n; index++) {
		renderbuffer = gles_names_get(&state->renderbuffers, renderbuffers[index]);
		if (renderbuffer == NULL)
			continue;

		/* An open pass may draw into it: it ends; the bound one goes back to 0. */
		gles_target_close(state);
		if (state->renderbuffer == renderbuffers[index])
			state->renderbuffer = 0U;

		/* Detached, and the name and the image (it waits for the frame) go. */
		gles_framebuffers_forget(state, GLES_ATTACH_RENDERBUFFER, renderbuffers[index]);
		gles_names_remove(&state->renderbuffers, renderbuffers[index]);
		renderbuffer_free(state, renderbuffer);
	}
}

/*
 * Binds a renderbuffer, making the object when the name is new.
 */
GL_APICALL void GL_APIENTRY
glBindRenderbuffer(
	GLenum target,
	GLuint renderbuffer)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_renderbuffer *object;
	int status;

	/* A context with its state and the one target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_RENDERBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A name seen for the first time gets its object, with no storage. */
	object = NULL;
	if (renderbuffer != 0U)
		object = gles_names_get(&state->renderbuffers, renderbuffer);
	if (renderbuffer != 0U && object == NULL) {
		object = calloc(1U, sizeof(*object));
		if (object == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name finds it from now on. */
		object->name = renderbuffer;
		status = gles_names_add(&state->renderbuffers, renderbuffer, object);
		if (status != 0) {
			free(object);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}
	}

	/* Bound; glRenderbufferStorage gives it storage. */
	state->renderbuffer = renderbuffer;
}

/*
 * Reports whether a name is a renderbuffer.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsRenderbuffer(
	GLuint renderbuffer)
{
	struct gles_state *state;
	struct gles_renderbuffer *object;

	/* A context with its state. */
	state = gles_state(gles_context());
	if (state == NULL)
		return GL_FALSE;

	/* A name that was bound once. */
	object = gles_names_get(&state->renderbuffers, renderbuffer);
	if (object == NULL)
		return GL_FALSE;

	/* Succeeded: it is one. */
	return GL_TRUE;
}

/*
 * Gives the bound renderbuffer storage of a format and size: a new device
 * image (the old one waits for the frame).
 */
GL_APICALL void GL_APIENTRY
glRenderbufferStorage(
	GLenum target,
	GLenum internalformat,
	GLsizei width,
	GLsizei height)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_renderbuffer *renderbuffer;
	int depth;
	int status;

	/* The bound renderbuffer. */
	context = gles_context();
	state = gles_state(context);
	renderbuffer = renderbuffer_bound(context, target);
	if (renderbuffer == NULL)
		return;

	/* A format OpenGL ES 2 renders into: colour, or depth and stencil. */
	depth = renderbuffer_depth_format(internalformat);
	if (depth < 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A size that fits. */
	if (width < 0 ||
	    height < 0 ||
	    width > FRAMEBUFFER_SIZE_MAX ||
	    height > FRAMEBUFFER_SIZE_MAX) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A pass that draws into the old image ends; the image waits for the frame. */
	gles_target_close(state);
	renderbuffer_discard(state, renderbuffer);

	/* The new storage (a zero size has no image and leaves a framebuffer incomplete). */
	renderbuffer->format = internalformat;
	renderbuffer->width = width;
	renderbuffer->height = height;
	renderbuffer->depth = depth;
	if (width == 0 || height == 0)
		return;

	/* Its image. */
	status = renderbuffer_image(state, renderbuffer);
	if (status != 0)
		gles_error(context, GL_OUT_OF_MEMORY);
}

/*
 * Reports a parameter of the bound renderbuffer.
 */
GL_APICALL void GL_APIENTRY
glGetRenderbufferParameteriv(
	GLenum target,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_renderbuffer *renderbuffer;
	GLint colour;
	GLint depth;
	GLint stencil;

	/* The bound renderbuffer. */
	context = gles_context();
	renderbuffer = renderbuffer_bound(context, target);
	if (renderbuffer == NULL)
		return;

	/* The bits of each component its format was given (the image keeps at least these). */
	colour = 0;
	depth = 0;
	stencil = 0;
	if (renderbuffer->format != 0U && !renderbuffer->depth)
		colour = 8;
	if (renderbuffer->format == GL_DEPTH_COMPONENT16)
		depth = 16;
	if (renderbuffer->format == GL_DEPTH_COMPONENT24_OES || renderbuffer->format == GL_DEPTH24_STENCIL8_OES)
		depth = 24;
	if (renderbuffer->format == GL_STENCIL_INDEX8 || renderbuffer->format == GL_DEPTH24_STENCIL8_OES)
		stencil = 8;

	/* The parameter. */
	switch (pname) {
	case GL_RENDERBUFFER_WIDTH:
		*params = renderbuffer->width;
		break;
	case GL_RENDERBUFFER_HEIGHT:
		*params = renderbuffer->height;
		break;
	case GL_RENDERBUFFER_INTERNAL_FORMAT:
		*params = GL_RGBA4;
		if (renderbuffer->format != 0U)
			*params = (GLint)renderbuffer->format;
		break;
	case GL_RENDERBUFFER_RED_SIZE:
	case GL_RENDERBUFFER_GREEN_SIZE:
	case GL_RENDERBUFFER_BLUE_SIZE:
	case GL_RENDERBUFFER_ALPHA_SIZE:
		*params = colour;
		break;
	case GL_RENDERBUFFER_DEPTH_SIZE:
		*params = depth;
		break;
	case GL_RENDERBUFFER_STENCIL_SIZE:
		*params = stencil;
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		break;
	}
}

/* Returns the bound framebuffer object, or NULL for framebuffer 0. */
static struct gles_framebuffer *
framebuffer_bound(
	struct gles_state *state)
{
	struct gles_framebuffer *fbo;

	/* Framebuffer 0 is the draw surface's. */
	if (state->framebuffer == 0U)
		return NULL;

	/* The object of the bound name. */
	fbo = gles_names_get(&state->framebuffers, state->framebuffer);
	if (fbo == NULL)
		return NULL;

	/* Succeeded: the object. */
	return fbo;
}

/*
 * Reports whether a framebuffer object can be drawn into, finding what its
 * attachment points name: the colour texture or renderbuffer, and the one
 * depth and stencil renderbuffer.  Returns GL_FRAMEBUFFER_COMPLETE or why
 * not.
 */
static GLenum
framebuffer_status(
	struct gles_state *state,
	struct gles_framebuffer *fbo,
	struct gles_texture **texture,
	struct gles_renderbuffer **color,
	struct gles_renderbuffer **depth)
{
	struct gles_renderbuffer *depth_object;
	struct gles_renderbuffer *stencil_object;
	int width;
	int height;

	/* Nothing found yet. */
	*texture = NULL;
	*color = NULL;
	*depth = NULL;
	depth_object = NULL;
	stencil_object = NULL;
	width = 0;
	height = 0;

	/* Nothing attached at all. */
	if (fbo->color.kind == GLES_ATTACH_NONE &&
	    fbo->depth.kind == GLES_ATTACH_NONE &&
	    fbo->stencil.kind == GLES_ATTACH_NONE)
		return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;

	/* The colour: a texture with level 0, or a colour renderbuffer with storage. */
	if (fbo->color.kind == GLES_ATTACH_TEXTURE) {
		*texture = gles_names_get(&state->textures, fbo->color.name);
		if (*texture == NULL ||
		    (*texture)->levels[0].width <= 0 ||
		    (*texture)->levels[0].height <= 0)
			return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
		width = (*texture)->levels[0].width;
		height = (*texture)->levels[0].height;
	} else if (fbo->color.kind == GLES_ATTACH_RENDERBUFFER) {
		*color = gles_names_get(&state->renderbuffers, fbo->color.name);
		if (*color == NULL ||
		    (*color)->image == VK_NULL_HANDLE ||
		    (*color)->depth)
			return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
		width = (*color)->width;
		height = (*color)->height;
	}

	/* Depth: a renderbuffer with a depth format (depth textures are not there). */
	if (fbo->depth.kind == GLES_ATTACH_TEXTURE)
		return GL_FRAMEBUFFER_UNSUPPORTED;
	if (fbo->depth.kind == GLES_ATTACH_RENDERBUFFER) {
		depth_object = gles_names_get(&state->renderbuffers, fbo->depth.name);
		if (depth_object == NULL ||
		    depth_object->image == VK_NULL_HANDLE ||
		    !depth_object->depth ||
		    depth_object->format == GL_STENCIL_INDEX8)
			return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
	}

	/* Stencil: a renderbuffer with a stencil format, on a device whose depth format has one. */
	if (fbo->stencil.kind == GLES_ATTACH_TEXTURE)
		return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
	if (fbo->stencil.kind == GLES_ATTACH_RENDERBUFFER) {
		stencil_object = gles_names_get(&state->renderbuffers, fbo->stencil.name);
		if (stencil_object == NULL || stencil_object->image == VK_NULL_HANDLE)
			return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
		if (stencil_object->format != GL_STENCIL_INDEX8 && stencil_object->format != GL_DEPTH24_STENCIL8_OES)
			return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
		if ((state->depth_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) == 0U)
			return GL_FRAMEBUFFER_UNSUPPORTED;
	}

	/* Separate depth and stencil images cannot be one Vulkan attachment. */
	if (depth_object != NULL &&
	    stencil_object != NULL &&
	    depth_object != stencil_object)
		return GL_FRAMEBUFFER_UNSUPPORTED;

	/* The one depth and stencil image. */
	*depth = depth_object;
	if (*depth == NULL)
		*depth = stencil_object;

	/* Every attachment the same size (OpenGL ES 2). */
	if (*depth != NULL && width == 0) {
		width = (*depth)->width;
		height = (*depth)->height;
	}

	/* The depth and stencil image the size of the colour image. */
	if (*depth != NULL &&
	    ((*depth)->width != width || (*depth)->height != height))
		return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;

	/* Succeeded: it can be drawn into. */
	return GL_FRAMEBUFFER_COMPLETE;
}

/*
 * Makes a complete framebuffer object's render pass and framebuffer for
 * the views its attachments have now, unless they were made for them.
 * Returns GL_FRAMEBUFFER_COMPLETE, why it is not, or
 * GL_FRAMEBUFFER_UNSUPPORTED when the device refused.
 */
static GLenum
framebuffer_build(
	struct gles_state *state,
	struct gles_framebuffer *fbo)
{
	struct gles_texture *texture;
	struct gles_renderbuffer *color;
	struct gles_renderbuffer *depth;
	VkFramebufferCreateInfo create;
	VkImageView views[2];
	VkImage color_image;
	VkImageView color_view;
	VkImageView depth_view;
	VkImageLayout color_layout;
	VkExtent2D extent;
	uint32_t count;
	GLenum status;
	VkResult result;
	int synced;

	/* Complete. */
	status = framebuffer_status(state, fbo, &texture, &color, &depth);
	if (status != GL_FRAMEBUFFER_COMPLETE)
		return status;

	/* A colour texture's image made (or brought up to date) from its levels. */
	if (texture != NULL) {
		synced = gles_texture_sync(state, texture);
		if (synced != 0)
			return GL_FRAMEBUFFER_UNSUPPORTED;
	}

	/* The views and size of what is attached, the colour image and where it rests. */
	color_image = VK_NULL_HANDLE;
	color_view = VK_NULL_HANDLE;
	color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	depth_view = VK_NULL_HANDLE;
	extent.width = 0U;
	extent.height = 0U;
	if (texture != NULL) {
		color_image = texture->image;
		color_view = texture->attach_view;
		color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		extent.width = (uint32_t)texture->levels[0].width;
		extent.height = (uint32_t)texture->levels[0].height;
	} else if (color != NULL) {
		color_image = color->image;
		color_view = color->view;
		extent.width = (uint32_t)color->width;
		extent.height = (uint32_t)color->height;
	}

	/* The depth and stencil image. */
	if (depth != NULL) {
		depth_view = depth->view;
		extent.width = (uint32_t)depth->width;
		extent.height = (uint32_t)depth->height;
	}

	/* Made for these already. */
	if (fbo->framebuffer != VK_NULL_HANDLE &&
	    fbo->built_color == color_view &&
	    fbo->built_depth == depth_view)
		return GL_FRAMEBUFFER_COMPLETE;

	/* The old ones go (an open pass over them ends first); they wait for the frame. */
	if (state->open_fbo == fbo)
		gles_target_close(state);
	framebuffer_forget_views(state, fbo);

	/* The render pass, which loads and keeps what is there. */
	result = framebuffer_pass(state, color_view != VK_NULL_HANDLE, depth_view != VK_NULL_HANDLE, color_layout, &fbo->pass);
	if (result != VK_SUCCESS) {
		gles_report("vkCreateRenderPass", (int)result);
		return GL_FRAMEBUFFER_UNSUPPORTED;
	}

	/* The framebuffer over the views. */
	count = 0U;
	if (color_view != VK_NULL_HANDLE)
		views[count++] = color_view;
	if (depth_view != VK_NULL_HANDLE)
		views[count++] = depth_view;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	create.renderPass = fbo->pass;
	create.attachmentCount = count;
	create.pAttachments = views;
	create.width = extent.width;
	create.height = extent.height;
	create.layers = 1U;
	result = vkCreateFramebuffer(state->device, &create, NULL, &fbo->framebuffer);
	if (result != VK_SUCCESS) {
		gles_report("vkCreateFramebuffer", (int)result);
		framebuffer_forget_views(state, fbo);
		return GL_FRAMEBUFFER_UNSUPPORTED;
	}

	/* Succeeded: made for these views. */
	fbo->built_color = color_view;
	fbo->built_depth = depth_view;
	fbo->extent = extent;
	fbo->color_image = color_image;
	fbo->color_layout = color_layout;
	fbo->depth_aspects = 0U;
	if (depth_view != VK_NULL_HANDLE)
		fbo->depth_aspects = state->depth_aspects;
	return GL_FRAMEBUFFER_COMPLETE;
}

/* Marks what a framebuffer object's pass draws into as used by this frame, and a texture as newer on the device. */
static void
framebuffer_mark(
	struct gles_state *state,
	struct gles_framebuffer *fbo)
{
	struct gles_texture *texture;
	struct gles_renderbuffer *color;
	struct gles_renderbuffer *depth;
	GLenum status;

	/* What is attached (the framebuffer was found complete just before). */
	status = framebuffer_status(state, fbo, &texture, &color, &depth);
	if (status != GL_FRAMEBUFFER_COMPLETE)
		return;

	/* A texture's level 0 on the CPU is older than its image from now on. */
	if (texture != NULL) {
		texture->used = state->frame;
		texture->gpu_written = 1;
	}

	/* The renderbuffers wait for the frame before they may go. */
	if (color != NULL)
		color->used = state->frame;
	if (depth != NULL)
		depth->used = state->frame;
}

/* Lets a framebuffer object's render pass and framebuffer go (they wait for the frame), and forgets what they were made for. */
static void
framebuffer_forget_views(
	struct gles_state *state,
	struct gles_framebuffer *fbo)
{
	struct gles_garbage objects;

	/* The pass and framebuffer wait for the frame. */
	memset(&objects, 0, sizeof(objects));
	objects.pass = fbo->pass;
	objects.framebuffer = fbo->framebuffer;
	gles_garbage_keep(state, &objects);

	/* Nothing is made for any views now. */
	fbo->pass = VK_NULL_HANDLE;
	fbo->framebuffer = VK_NULL_HANDLE;
	fbo->built_color = VK_NULL_HANDLE;
	fbo->built_depth = VK_NULL_HANDLE;
}

/*
 * Makes a render pass over an RGBA8 colour image resting in color_layout
 * and the device's depth and stencil image, each optional, that loads and
 * keeps both, and waits for (and makes later work wait for) the draws,
 * copies and samples around it.
 */
static VkResult
framebuffer_pass(
	struct gles_state *state,
	int has_color,
	int has_depth,
	VkImageLayout color_layout,
	VkRenderPass *pass)
{
	VkAttachmentDescription attachments[2];
	VkAttachmentReference color_reference;
	VkAttachmentReference depth_reference;
	VkSubpassDescription subpass;
	VkSubpassDependency dependencies[2];
	VkRenderPassCreateInfo create;
	uint32_t count;
	VkResult result;

	/* The colour attachment, loaded and kept in its resting layout. */
	memset(attachments, 0, sizeof(attachments));
	memset(&color_reference, 0, sizeof(color_reference));
	memset(&depth_reference, 0, sizeof(depth_reference));
	count = 0U;
	if (has_color) {
		attachments[count].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachments[count].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[count].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[count].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[count].initialLayout = color_layout;
		attachments[count].finalLayout = color_layout;
		color_reference.attachment = count;
		color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		count++;
	}

	/* The depth and stencil attachment, loaded and kept. */
	if (has_depth) {
		attachments[count].format = state->depth_format;
		attachments[count].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[count].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[count].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[count].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[count].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_reference.attachment = count;
		depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		count++;
	}

	/* One subpass over both. */
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	if (has_color) {
		subpass.colorAttachmentCount = 1U;
		subpass.pColorAttachments = &color_reference;
	}

	/* The depth and stencil reference. */
	if (has_depth)
		subpass.pDepthStencilAttachment = &depth_reference;

	/* Earlier draws, copies and samples of the images finish before the pass writes them. */
	memset(dependencies, 0, sizeof(dependencies));
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0U;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_TRANSFER_WRITE_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	/* Later samples, copies and draws wait for what the pass wrote. */
	dependencies[1].srcSubpass = 0U;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
				       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	/* The pass. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	create.attachmentCount = count;
	create.pAttachments = attachments;
	create.subpassCount = 1U;
	create.pSubpasses = &subpass;
	create.dependencyCount = 2U;
	create.pDependencies = dependencies;

	/* Made, or refused. */
	*pass = VK_NULL_HANDLE;
	result = vkCreateRenderPass(state->device, &create, NULL, pass);
	if (result != VK_SUCCESS) {
		*pass = VK_NULL_HANDLE;
		return result;
	}

	/* Succeeded: the pass. */
	return VK_SUCCESS;
}

/*
 * Returns the render pass framebuffer objects' pipelines of a kind are
 * made with (colour and depth each present or not; every such pass is
 * compatible with it), making it the first time; VK_NULL_HANDLE when the
 * device refused.
 */
static VkRenderPass
framebuffer_compatible(
	struct gles_state *state,
	int has_color,
	int has_depth)
{
	VkRenderPass *pass;
	VkResult result;

	/* Made before. */
	pass = &state->fbo_passes[has_color != 0][has_depth != 0];
	if (*pass != VK_NULL_HANDLE)
		return *pass;

	/* Made now (the layouts do not matter to compatibility). */
	result = framebuffer_pass(state, has_color, has_depth, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, pass);
	if (result != VK_SUCCESS) {
		gles_report("vkCreateRenderPass", (int)result);
		*pass = VK_NULL_HANDLE;
		return VK_NULL_HANDLE;
	}

	/* Succeeded: the pass, kept. */
	return *pass;
}

/* Returns the bound renderbuffer, recording the error when the target is wrong or none is bound. */
static struct gles_renderbuffer *
renderbuffer_bound(
	struct zegl_context *context,
	GLenum target)
{
	struct gles_state *state;
	struct gles_renderbuffer *renderbuffer;

	/* A context with its state and the one target. */
	state = gles_state(context);
	if (state == NULL)
		return NULL;
	if (target != GL_RENDERBUFFER) {
		gles_error(context, GL_INVALID_ENUM);
		return NULL;
	}

	/* The bound one (renderbuffer 0 is not one). */
	renderbuffer = NULL;
	if (state->renderbuffer != 0U)
		renderbuffer = gles_names_get(&state->renderbuffers, state->renderbuffer);
	if (renderbuffer == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the renderbuffer. */
	return renderbuffer;
}

/* Reports whether a renderbuffer format is a depth or stencil one (1) or a colour one (0); -1 when OpenGL ES 2 has no such format. */
static int
renderbuffer_depth_format(
	GLenum format)
{
	/* The formats, colour first. */
	switch (format) {
	case GL_RGBA4:
	case GL_RGB565:
	case GL_RGB5_A1:
	case GL_RGB8_OES:
	case GL_RGBA8_OES:
		return 0;
	case GL_DEPTH_COMPONENT16:
	case GL_DEPTH_COMPONENT24_OES:
	case GL_DEPTH24_STENCIL8_OES:
	case GL_STENCIL_INDEX8:
		return 1;
	default:
		break;
	}

	/* Not one. */
	return -1;
}

/*
 * Makes a renderbuffer's device image and view, in the layout it rests in
 * as an attachment.  Returns 0, or -1 when the device refused.
 */
static int
renderbuffer_image(
	struct gles_state *state,
	struct gles_renderbuffer *renderbuffer)
{
	VkImageCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view;
	VkImageMemoryBarrier barrier;
	VkImageAspectFlags aspects;
	VkFormat format;
	uint32_t type;
	VkResult result;
	int status;

	/* A colour image is RGBA8, a depth one the device's depth and stencil format (asked for once). */
	format = VK_FORMAT_R8G8B8A8_UNORM;
	aspects = VK_IMAGE_ASPECT_COLOR_BIT;
	if (renderbuffer->depth && state->depth_format == VK_FORMAT_UNDEFINED)
		state->depth_format = zegl_depth_format(state->display, &state->depth_aspects);
	if (renderbuffer->depth) {
		format = state->depth_format;
		aspects = state->depth_aspects;
	}

	/* A device with no depth format has no depth renderbuffers. */
	if (format == VK_FORMAT_UNDEFINED)
		return -1;

	/* The image. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = format;
	create.extent.width = (uint32_t)renderbuffer->width;
	create.extent.height = (uint32_t)renderbuffer->height;
	create.extent.depth = 1U;
	create.mipLevels = 1U;
	create.arrayLayers = 1U;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	if (renderbuffer->depth)
		create.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(state->device, &create, NULL, &renderbuffer->image);
	if (result != VK_SUCCESS) {
		renderbuffer->image = VK_NULL_HANDLE;
		return -1;
	}

	/* Its memory on the device. */
	vkGetImageMemoryRequirements(state->device, renderbuffer->image, &requirements);
	type = gles_memory_type(state, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (type == UINT32_MAX)
		type = gles_memory_type(state, requirements.memoryTypeBits, 0U);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = type;
	result = vkAllocateMemory(state->device, &allocate, NULL, &renderbuffer->memory);
	if (result != VK_SUCCESS) {
		renderbuffer->memory = VK_NULL_HANDLE;
		renderbuffer_discard(state, renderbuffer);
		return -1;
	}

	/* Bound. */
	result = vkBindImageMemory(state->device, renderbuffer->image, renderbuffer->memory, 0U);
	if (result != VK_SUCCESS) {
		renderbuffer_discard(state, renderbuffer);
		return -1;
	}

	/* A view of the whole image. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = renderbuffer->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange.aspectMask = aspects;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	result = vkCreateImageView(state->device, &view, NULL, &renderbuffer->view);
	if (result != VK_SUCCESS) {
		renderbuffer->view = VK_NULL_HANDLE;
		renderbuffer_discard(state, renderbuffer);
		return -1;
	}

	/* Its resting layout, set by the upload queue before any pass uses it. */
	status = gles_upload_begin(state);
	if (status != 0) {
		renderbuffer_discard(state, renderbuffer);
		return -1;
	}

	/* The layout change. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	if (renderbuffer->depth) {
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	/* The whole image, on the one queue. */
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = renderbuffer->image;
	barrier.subresourceRange.aspectMask = aspects;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(state->upload, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
			     0U, 0U, NULL, 0U, NULL, 1U, &barrier);

	/* Submitted and waited for. */
	status = gles_upload_end(state);
	if (status != 0) {
		renderbuffer_discard(state, renderbuffer);
		return -1;
	}

	/* Succeeded: the renderbuffer can be attached. */
	return 0;
}

/* Lets a renderbuffer's image, view and memory go (they wait for the frame); the object stays, with no storage made. */
static void
renderbuffer_discard(
	struct gles_state *state,
	struct gles_renderbuffer *renderbuffer)
{
	/* They wait for the frame. */
	gles_throw_away(state, VK_NULL_HANDLE, renderbuffer->image, renderbuffer->view, renderbuffer->memory);

	/* Nothing made now. */
	renderbuffer->image = VK_NULL_HANDLE;
	renderbuffer->memory = VK_NULL_HANDLE;
	renderbuffer->view = VK_NULL_HANDLE;
}

/* Frees a renderbuffer: its image waits for the frame, the object goes now. */
static void
renderbuffer_free(
	struct gles_state *state,
	struct gles_renderbuffer *renderbuffer)
{
	/* The image, its view and memory, then the object. */
	renderbuffer_discard(state, renderbuffer);
	free(renderbuffer);
}

/* Finds a framebuffer object's attachment point for GL's name of it; nonzero when it is not one of the three. */
static int
framebuffer_point(
	GLenum attachment,
	struct gles_attachment **point,
	struct gles_framebuffer *fbo)
{
	/* The three points of OpenGL ES 2. */
	switch (attachment) {
	case GL_COLOR_ATTACHMENT0:
		*point = &fbo->color;
		return 0;
	case GL_DEPTH_ATTACHMENT:
		*point = &fbo->depth;
		return 0;
	case GL_STENCIL_ATTACHMENT:
		*point = &fbo->stencil;
		return 0;
	default:
		break;
	}

	/* Not one. */
	*point = NULL;
	return -1;
}
