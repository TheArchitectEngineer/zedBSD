/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * 2D textures and cube maps of zedBSD's OpenGL ES (WS068 p008, p023).
 *
 * glTexImage2D converts what the application gives into RGBA8 and keeps
 * it per level on the CPU; the first draw that samples a changed texture
 * makes a new device image of the levels (a full chain for a mipmapping
 * minification filter, else level 0) and uploads them.  Samplers are made
 * once per set of filters, wraps and level count.
 */

#include "gles.h"

#include <stdlib.h>
#include <string.h>

static struct gles_texture *texture_bound(struct zegl_context *context, GLenum target, int images, unsigned *face);
static struct gles_texture *texture_changing(struct zegl_context *context, GLenum target, int images, unsigned *face);
static struct gles_texture *texture_new(GLuint name, GLenum target);
static uint32_t texture_layers(const struct gles_texture *texture);
static int texture_mipmaps(struct gles_texture *texture, unsigned face);
static void texture_views_free(struct gles_state *state, VkImageView *views);
static unsigned char *texture_convert(struct zegl_context *context, GLenum format, GLenum type, GLsizei width, GLsizei height, const void *pixels);
static int texture_parameter(struct zegl_context *context, struct gles_texture *texture, GLenum pname, GLint value);
static int texture_mipmapped(GLenum filter);
static uint32_t texture_levels(struct gles_texture *texture);
static VkFilter texture_filter(GLenum filter);
static VkSamplerAddressMode texture_wrap(GLenum wrap);

/*
 * Brings a texture's device image up to date with its levels (a cube
 * map's image has a layer per face).  Returns 0, or -1 when the device has
 * no memory for it.
 */
int
gles_texture_sync(
	struct gles_state *state,
	struct gles_texture *texture)
{
	VkImageCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view;
	VkImageMemoryBarrier barrier;
	VkBufferImageCopy copies[GLES_FACES * GLES_LEVELS];
	VkBuffer staging;
	VkDeviceMemory staging_memory;
	VkImage image;
	VkDeviceMemory memory;
	VkImageView image_view;
	VkImageView attach_views[GLES_FACES];
	const struct gles_level *source;
	unsigned char *mapped;
	void *pointer;
	size_t total;
	size_t bytes;
	uint32_t levels;
	uint32_t layers;
	uint32_t level;
	uint32_t layer;
	uint32_t count;
	uint32_t type;
	VkResult result;
	int status;

	/* An image that is up to date stays. */
	if (!texture->dirty && texture->image != VK_NULL_HANDLE)
		return 0;

	/* The levels and layers the image has, and the bytes they take. */
	levels = texture_levels(texture);
	layers = texture_layers(texture);
	total = 0U;
	for (layer = 0U; layer < layers; layer++) {
		for (level = 0U; level < levels; level++) {
			source = &texture->levels[layer * GLES_LEVELS + level];
			total += (size_t)source->width * (size_t)source->height * 4U;
		}
	}

	/* The image (a cube map's can be viewed as one). */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	if (layers == GLES_FACES)
		create.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = VK_FORMAT_R8G8B8A8_UNORM;
	create.extent.width = (uint32_t)texture->levels[0].width;
	create.extent.height = (uint32_t)texture->levels[0].height;
	create.extent.depth = 1U;
	create.mipLevels = levels;
	create.arrayLayers = layers;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(state->device, &create, NULL, &image);
	if (result != VK_SUCCESS)
		return -1;

	/* Its memory on the device. */
	vkGetImageMemoryRequirements(state->device, image, &requirements);
	type = gles_memory_type(state, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (type == UINT32_MAX)
		type = gles_memory_type(state, requirements.memoryTypeBits, 0U);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = type;
	result = vkAllocateMemory(state->device, &allocate, NULL, &memory);
	if (result != VK_SUCCESS) {
		vkDestroyImage(state->device, image, NULL);
		return -1;
	}

	/* Bound, with a view of every level (a cube view of a cube map). */
	result = vkBindImageMemory(state->device, image, memory, 0U);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	if (layers == GLES_FACES)
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	view.format = VK_FORMAT_R8G8B8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = levels;
	view.subresourceRange.layerCount = layers;
	image_view = VK_NULL_HANDLE;
	if (result == VK_SUCCESS)
		result = vkCreateImageView(state->device, &view, NULL, &image_view);
	if (result != VK_SUCCESS) {
		vkDestroyImage(state->device, image, NULL);
		vkFreeMemory(state->device, memory, NULL);
		return -1;
	}

	/* A 2D view of each face's level 0, which a framebuffer object draws into. */
	memset(attach_views, 0, sizeof(attach_views));
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	for (layer = 0U; layer < layers; layer++) {
		view.subresourceRange.baseArrayLayer = layer;
		result = vkCreateImageView(state->device, &view, NULL, &attach_views[layer]);
		if (result != VK_SUCCESS) {
			attach_views[layer] = VK_NULL_HANDLE;
			texture_views_free(state, attach_views);
			gles_throw_away(state, VK_NULL_HANDLE, image, image_view, memory);
			return -1;
		}
	}

	/* The staging buffer with the levels one after another. */
	status = gles_device_buffer(state, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &staging_memory, &pointer);
	if (status != 0) {
		texture_views_free(state, attach_views);
		gles_throw_away(state, VK_NULL_HANDLE, image, image_view, memory);
		return -1;
	}

	/* Each face's levels one after another. */
	mapped = pointer;
	total = 0U;
	count = 0U;
	memset(copies, 0, sizeof(copies));
	for (layer = 0U; layer < layers; layer++) {
		for (level = 0U; level < levels; level++) {
			source = &texture->levels[layer * GLES_LEVELS + level];
			bytes = (size_t)source->width * (size_t)source->height * 4U;
			memcpy(mapped + total, source->pixels, bytes);
			copies[count].bufferOffset = total;
			copies[count].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copies[count].imageSubresource.mipLevel = level;
			copies[count].imageSubresource.baseArrayLayer = layer;
			copies[count].imageSubresource.layerCount = 1U;
			copies[count].imageExtent.width = (uint32_t)source->width;
			copies[count].imageExtent.height = (uint32_t)source->height;
			copies[count].imageExtent.depth = 1U;
			total += bytes;
			count++;
		}
	}

	/* The upload: ready to be written, the copies, ready to be sampled. */
	status = gles_upload_begin(state);
	if (status == 0) {
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = levels;
		barrier.subresourceRange.layerCount = layers;
		vkCmdPipelineBarrier(state->upload, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);
		vkCmdCopyBufferToImage(state->upload, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, copies);
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(state->upload, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0U, 0U, NULL, 0U, NULL, 1U, &barrier);
		status = gles_upload_end(state);
	}

	/* The staging buffer is done with (the upload was waited for). */
	vkDestroyBuffer(state->device, staging, NULL);
	vkFreeMemory(state->device, staging_memory, NULL);
	if (status != 0) {
		texture_views_free(state, attach_views);
		gles_throw_away(state, VK_NULL_HANDLE, image, image_view, memory);
		return -1;
	}

	/* The new image replaces the old one, which waits for the frame with its views. */
	gles_throw_away(state, VK_NULL_HANDLE, texture->image, texture->view, texture->memory);
	texture_views_free(state, texture->attach_views);
	texture->image = image;
	texture->memory = memory;
	texture->view = image_view;
	memcpy(texture->attach_views, attach_views, sizeof(attach_views));
	texture->level_count = levels;
	texture->dirty = 0;

	/* Succeeded: the image has the levels. */
	return 0;
}

/*
 * Reports whether a texture can be sampled: its level 0 is specified (a
 * cube map's on every face, square and all the same size).
 */
int
gles_texture_complete(
	struct gles_texture *texture)
{
	const struct gles_level *level;
	unsigned faces;
	unsigned face;

	/* A texture, bound to a target at least once. */
	if (texture == NULL || texture->target == 0U)
		return 0;

	/* Level 0 of each face, as large as the first face's. */
	faces = texture_layers(texture);
	for (face = 0U; face < faces; face++) {
		level = &texture->levels[face * GLES_LEVELS];
		if (level->width <= 0 || level->height <= 0)
			return 0;
		if (level->width != texture->levels[0].width || level->height != texture->levels[0].height)
			return 0;
	}

	/* A cube map's faces are square. */
	if (faces > 1U && texture->levels[0].width != texture->levels[0].height)
		return 0;

	/* Succeeded: it can be sampled. */
	return 1;
}

/*
 * Returns the sampler for a texture's filters, wraps and level count,
 * making it the first time; VK_NULL_HANDLE when it cannot be made.
 */
VkSampler
gles_sampler_get(
	struct gles_state *state,
	struct gles_texture *texture)
{
	VkSamplerCreateInfo create;
	struct gles_sampler *sampler;
	VkResult result;
	int mipmapped;

	/* One already made for the same state. */
	for (sampler = state->samplers; sampler != NULL; sampler = sampler->next) {
		if (sampler->min_filter == texture->min_filter && sampler->mag_filter == texture->mag_filter &&
		    sampler->wrap_s == texture->wrap_s && sampler->wrap_t == texture->wrap_t &&
		    sampler->levels == texture->level_count)
			return sampler->sampler;
	}

	/* A new one: the filters, the mipmap mode and the wraps. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	create.magFilter = texture_filter(texture->mag_filter);
	create.minFilter = texture_filter(texture->min_filter);
	create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	if (texture->min_filter == GL_NEAREST_MIPMAP_LINEAR || texture->min_filter == GL_LINEAR_MIPMAP_LINEAR)
		create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	create.addressModeU = texture_wrap(texture->wrap_s);
	create.addressModeV = texture_wrap(texture->wrap_t);
	create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	create.maxAnisotropy = 1.0f;
	create.compareOp = VK_COMPARE_OP_NEVER;

	/* A filter without mipmaps samples level 0 only (the 0.25 keeps the magnification test right). */
	create.maxLod = 0.25f;
	mipmapped = texture_mipmapped(texture->min_filter);
	if (mipmapped)
		create.maxLod = (float)texture->level_count;

	/* Made and kept. */
	sampler = calloc(1U, sizeof(*sampler));
	if (sampler == NULL)
		return VK_NULL_HANDLE;
	result = vkCreateSampler(state->device, &create, NULL, &sampler->sampler);
	if (result != VK_SUCCESS) {
		free(sampler);
		return VK_NULL_HANDLE;
	}

	/* Succeeded: the sampler, kept for the next texture like it. */
	sampler->min_filter = texture->min_filter;
	sampler->mag_filter = texture->mag_filter;
	sampler->wrap_s = texture->wrap_s;
	sampler->wrap_t = texture->wrap_t;
	sampler->levels = texture->level_count;
	sampler->next = state->samplers;
	state->samplers = sampler;
	return sampler->sampler;
}

/*
 * Returns the black texture (a 2D one, or a cube map when cube is
 * nonzero) sampled where a unit has no complete texture, making it at its
 * first use; NULL when there is no memory.
 */
struct gles_texture *
gles_texture_black(
	struct gles_state *state,
	int cube)
{
	struct gles_texture *texture;
	struct gles_texture **kept;
	unsigned char *pixels;
	unsigned faces;
	unsigned face;

	/* Made once. */
	kept = &state->black;
	faces = 1U;
	if (cube) {
		kept = &state->black_cube;
		faces = GLES_FACES;
	}

	/* Already made. */
	if (*kept != NULL)
		return *kept;

	/* The texture. */
	texture = texture_new(0U, GL_TEXTURE_2D);
	if (texture == NULL)
		return NULL;
	if (cube)
		texture->target = GL_TEXTURE_CUBE_MAP;

	/* One opaque black texel on each face. */
	for (face = 0U; face < faces; face++) {
		pixels = calloc(4U, 1U);
		if (pixels == NULL) {
			gles_texture_free(state, texture);
			return NULL;
		}

		/* Opaque. */
		pixels[3] = 255U;
		gles_texture_define(texture, face, 0, 1, 1, pixels);
	}

	/* Succeeded: the texture, kept. */
	*kept = texture;
	return texture;
}

/*
 * Frees a texture; its device image waits for the frame.
 */
void
gles_texture_free(
	struct gles_state *state,
	struct gles_texture *texture)
{
	unsigned level;

	/* The device image and the views of its faces. */
	gles_throw_away(state, VK_NULL_HANDLE, texture->image, texture->view, texture->memory);
	texture_views_free(state, texture->attach_views);

	/* The levels of every face, then the object. */
	for (level = 0U; level < GLES_FACES * GLES_LEVELS; level++)
		free(texture->levels[level].pixels);
	free(texture);
}

/*
 * Gives a level of a texture's face (0 for a 2D texture) new RGBA8 pixels,
 * which the texture takes over.
 */
void
gles_texture_define(
	struct gles_texture *texture,
	unsigned face,
	GLint level,
	int width,
	int height,
	unsigned char *pixels)
{
	struct gles_level *defined;

	/* The old pixels go; the image is stale. */
	defined = &texture->levels[face * GLES_LEVELS + (unsigned)level];
	free(defined->pixels);
	defined->pixels = pixels;
	defined->width = width;
	defined->height = height;
	texture->dirty = 1;
}

/*
 * Makes names for textures.
 */
GL_APICALL void GL_APIENTRY
glGenTextures(
	GLsizei n,
	GLuint *textures)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_texture *texture;
	GLsizei index;
	GLuint name;
	int status;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name gets a texture with no levels. */
	for (index = 0; index < n; index++) {
		name = gles_names_free(&state->textures);
		texture = texture_new(name, 0U);
		if (texture == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name. */
		status = gles_names_add(&state->textures, name, texture);
		if (status != 0) {
			free(texture);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name goes back to the application. */
		textures[index] = name;
	}
}

/*
 * Deletes textures, unbinding them from every unit.
 */
GL_APICALL void GL_APIENTRY
glDeleteTextures(
	GLsizei n,
	const GLuint *textures)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_texture *texture;
	GLsizei index;
	unsigned unit;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (n < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name that is a texture. */
	for (index = 0; index < n; index++) {
		texture = gles_names_get(&state->textures, textures[index]);
		if (texture == NULL)
			continue;

		/* Unbound from every unit. */
		for (unit = 0U; unit < GLES_UNITS; unit++) {
			if (state->units[unit] == texture)
				state->units[unit] = NULL;
			if (state->cube_units[unit] == texture)
				state->cube_units[unit] = NULL;
		}

		/* Detached from the bound framebuffer object; the name and the texture go (its image waits for the frame). */
		gles_framebuffers_forget(state, GLES_ATTACH_TEXTURE, textures[index]);
		gles_names_remove(&state->textures, textures[index]);
		gles_texture_free(state, texture);
	}
}

/*
 * Binds a texture to the active unit's GL_TEXTURE_2D or
 * GL_TEXTURE_CUBE_MAP, making it when the name is new; a texture keeps
 * the target it was first bound to.
 */
GL_APICALL void GL_APIENTRY
glBindTexture(
	GLenum target,
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_texture *texture;
	int status;

	/* A context with its state, and the 2D or cube map target. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The texture: none for name 0, made for a name not yet used. */
	texture = NULL;
	if (name != 0U) {
		texture = gles_names_get(&state->textures, name);
		if (texture == NULL) {
			texture = texture_new(name, target);
			if (texture == NULL) {
				gles_error(context, GL_OUT_OF_MEMORY);
				return;
			}

			/* Under the name. */
			status = gles_names_add(&state->textures, name, texture);
			if (status != 0) {
				free(texture);
				gles_error(context, GL_OUT_OF_MEMORY);
				return;
			}
		}
	}

	/* A texture made by glGenTextures takes the target now; one of the other target cannot be bound here. */
	if (texture != NULL && texture->target == 0U)
		texture->target = target;
	if (texture != NULL && texture->target != target) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Bound to the active unit's target. */
	if (target == GL_TEXTURE_CUBE_MAP) {
		state->cube_units[state->active_unit] = texture;
	} else {
		state->units[state->active_unit] = texture;
	}
}

/*
 * Selects the texture unit glBindTexture and glTexImage2D work on.
 */
GL_APICALL void GL_APIENTRY
glActiveTexture(
	GLenum texture)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, and a unit it has. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + GLES_UNITS) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The unit. */
	state->active_unit = texture - GL_TEXTURE0;
}

/*
 * Specifies a level of the bound texture.
 */
GL_APICALL void GL_APIENTRY
glTexImage2D(
	GLenum target,
	GLint level,
	GLint internalformat,
	GLsizei width,
	GLsizei height,
	GLint border,
	GLenum format,
	GLenum type,
	const void *pixels)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	unsigned char *converted;
	unsigned face;

	/* The bound texture (a cube map's face), a level and a size it can have. */
	context = gles_context();
	texture = texture_changing(context, target, 1, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS || width < 0 || height < 0 || width > 16384 || height > 16384 || border != 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A cube map's faces are square. */
	if (texture->target == GL_TEXTURE_CUBE_MAP && width != height) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* OpenGL ES 2 wants the internal format to be the format. */
	(void)internalformat;

	/* The pixels as RGBA8 (NULL pixels: black). */
	converted = texture_convert(context, format, type, width, height, pixels);
	if (converted == NULL)
		return;

	/* Succeeded: the level. */
	gles_texture_define(texture, face, level, width, height, converted);
}

/*
 * Replaces a rectangle of a level of the bound texture.
 */
GL_APICALL void GL_APIENTRY
glTexSubImage2D(
	GLenum target,
	GLint level,
	GLint xoffset,
	GLint yoffset,
	GLsizei width,
	GLsizei height,
	GLenum format,
	GLenum type,
	const void *pixels)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	struct gles_level *destination;
	unsigned char *converted;
	unsigned face;
	GLsizei row;

	/* The bound texture (a cube map's face) and a rectangle inside a specified level. */
	context = gles_context();
	texture = texture_changing(context, target, 1, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The level. */
	destination = &texture->levels[face * GLES_LEVELS + (unsigned)level];
	if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
	    xoffset + width > destination->width || yoffset + height > destination->height) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The pixels as RGBA8. */
	converted = texture_convert(context, format, type, width, height, pixels);
	if (converted == NULL)
		return;

	/* Each row into place. */
	for (row = 0; row < height; row++) {
		memcpy(destination->pixels + ((size_t)(yoffset + row) * (size_t)destination->width + (size_t)xoffset) * 4U,
		       converted + (size_t)row * (size_t)width * 4U, (size_t)width * 4U);
	}

	/* Succeeded: the image is stale. */
	free(converted);
	texture->dirty = 1;
}

/*
 * Makes a level of the bound texture from a rectangle of the framebuffer.
 */
GL_APICALL void GL_APIENTRY
glCopyTexImage2D(
	GLenum target,
	GLint level,
	GLenum internalformat,
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height,
	GLint border)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	unsigned char *pixels;
	unsigned face;
	int status;

	/* The bound texture (a cube map's face), a level and a size. */
	context = gles_context();
	texture = texture_changing(context, target, 1, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS || width <= 0 || height <= 0 || border != 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A cube map's faces are square. */
	if (texture->target == GL_TEXTURE_CUBE_MAP && width != height) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The format is RGBA8 whatever was asked. */
	(void)internalformat;

	/* The framebuffer's pixels. */
	pixels = malloc((size_t)width * (size_t)height * 4U);
	if (pixels == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The framebuffer's pixels. */
	status = gles_read_rgba(context, x, y, width, height, pixels);
	if (status != 0) {
		free(pixels);
		return;
	}

	/* Succeeded: the level. */
	gles_texture_define(texture, face, level, width, height, pixels);
}

/*
 * Replaces a rectangle of a level of the bound texture with one of the
 * framebuffer.
 */
GL_APICALL void GL_APIENTRY
glCopyTexSubImage2D(
	GLenum target,
	GLint level,
	GLint xoffset,
	GLint yoffset,
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	struct gles_level *destination;
	unsigned char *pixels;
	unsigned face;
	GLsizei row;
	int status;

	/* The bound texture (a cube map's face) and a rectangle inside a specified level. */
	context = gles_context();
	texture = texture_changing(context, target, 1, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The level. */
	destination = &texture->levels[face * GLES_LEVELS + (unsigned)level];
	if (xoffset < 0 || yoffset < 0 || width <= 0 || height <= 0 ||
	    xoffset + width > destination->width || yoffset + height > destination->height) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The framebuffer's pixels. */
	pixels = malloc((size_t)width * (size_t)height * 4U);
	if (pixels == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The framebuffer's pixels. */
	status = gles_read_rgba(context, x, y, width, height, pixels);
	if (status != 0) {
		free(pixels);
		return;
	}

	/* Each row into place. */
	for (row = 0; row < height; row++) {
		memcpy(destination->pixels + ((size_t)(yoffset + row) * (size_t)destination->width + (size_t)xoffset) * 4U,
		       pixels + (size_t)row * (size_t)width * 4U, (size_t)width * 4U);
	}

	/* Succeeded: the image is stale. */
	free(pixels);
	texture->dirty = 1;
}

/*
 * Refuses compressed textures: no compressed format is offered.
 */
GL_APICALL void GL_APIENTRY
glCompressedTexImage2D(
	GLenum target,
	GLint level,
	GLenum internalformat,
	GLsizei width,
	GLsizei height,
	GLint border,
	GLsizei imageSize,
	const void *data)
{
	struct zegl_context *context;

	/* GL_NUM_COMPRESSED_TEXTURE_FORMATS is 0. */
	(void)target;
	(void)level;
	(void)internalformat;
	(void)width;
	(void)height;
	(void)border;
	(void)imageSize;
	(void)data;
	context = gles_context();
	if (context != NULL)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Refuses compressed textures: no compressed format is offered.
 */
GL_APICALL void GL_APIENTRY
glCompressedTexSubImage2D(
	GLenum target,
	GLint level,
	GLint xoffset,
	GLint yoffset,
	GLsizei width,
	GLsizei height,
	GLenum format,
	GLsizei imageSize,
	const void *data)
{
	struct zegl_context *context;

	/* GL_NUM_COMPRESSED_TEXTURE_FORMATS is 0. */
	(void)target;
	(void)level;
	(void)xoffset;
	(void)yoffset;
	(void)width;
	(void)height;
	(void)format;
	(void)imageSize;
	(void)data;
	context = gles_context();
	if (context != NULL)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Sets an integer parameter of the bound texture.
 */
GL_APICALL void GL_APIENTRY
glTexParameteri(
	GLenum target,
	GLenum pname,
	GLint param)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	unsigned face;

	/* The bound texture takes it. */
	context = gles_context();
	texture = texture_bound(context, target, 0, &face);
	if (texture == NULL)
		return;
	(void)texture_parameter(context, texture, pname, param);
}

/*
 * Sets a parameter of the bound texture from a float.
 */
GL_APICALL void GL_APIENTRY
glTexParameterf(
	GLenum target,
	GLenum pname,
	GLfloat param)
{
	/* Every parameter of a 2D texture in OpenGL ES 2 is an enum. */
	glTexParameteri(target, pname, (GLint)param);
}

/*
 * Sets an integer parameter of the bound texture from an array.
 */
GL_APICALL void GL_APIENTRY
glTexParameteriv(
	GLenum target,
	GLenum pname,
	const GLint *params)
{
	/* The first value. */
	glTexParameteri(target, pname, params[0]);
}

/*
 * Sets a parameter of the bound texture from an array of floats.
 */
GL_APICALL void GL_APIENTRY
glTexParameterfv(
	GLenum target,
	GLenum pname,
	const GLfloat *params)
{
	/* The first value. */
	glTexParameteri(target, pname, (GLint)params[0]);
}

/*
 * Reports a parameter of the bound texture.
 */
GL_APICALL void GL_APIENTRY
glGetTexParameteriv(
	GLenum target,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	unsigned face;

	/* The bound texture. */
	context = gles_context();
	texture = texture_bound(context, target, 0, &face);
	if (texture == NULL)
		return;

	/* The parameter asked for. */
	switch (pname) {
	case GL_TEXTURE_MIN_FILTER:
		*params = (GLint)texture->min_filter;
		return;
	case GL_TEXTURE_MAG_FILTER:
		*params = (GLint)texture->mag_filter;
		return;
	case GL_TEXTURE_WRAP_S:
		*params = (GLint)texture->wrap_s;
		return;
	case GL_TEXTURE_WRAP_T:
		*params = (GLint)texture->wrap_t;
		return;
	default:
		break;
	}

	/* Any other is an error. */
	gles_error(context, GL_INVALID_ENUM);
}

/*
 * Reports a parameter of the bound texture as a float.
 */
GL_APICALL void GL_APIENTRY
glGetTexParameterfv(
	GLenum target,
	GLenum pname,
	GLfloat *params)
{
	GLint value;

	/* The integer, converted. */
	value = 0;
	glGetTexParameteriv(target, pname, &value);
	*params = (GLfloat)value;
}

/*
 * Reports whether a name is a texture.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsTexture(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	void *object;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return GL_FALSE;

	/* The name's object. */
	object = gles_names_get(&state->textures, name);
	if (object == NULL)
		return GL_FALSE;
	return GL_TRUE;
}

/*
 * Makes every level of the bound texture below level 0 by halving it with
 * a box filter.
 */
GL_APICALL void GL_APIENTRY
glGenerateMipmap(
	GLenum target)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	unsigned faces;
	unsigned face;
	int complete;
	int status;

	/* The bound texture (a cube map as a whole) with a level 0. */
	context = gles_context();
	texture = texture_changing(context, target, 0, &face);
	if (texture == NULL)
		return;
	complete = gles_texture_complete(texture);
	if (!complete) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Each face's chain (a 2D texture has one face). */
	faces = texture_layers(texture);
	for (face = 0U; face < faces; face++) {
		status = texture_mipmaps(texture, face);
		if (status != 0) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}
	}
}

/*
 * Returns the texture a target names on the active unit, and the face it
 * means: GL_TEXTURE_2D (face 0), and either a cube map face (images
 * nonzero: the calls that give a level pixels) or GL_TEXTURE_CUBE_MAP as
 * a whole (images zero: parameters and mipmaps).  NULL with the error
 * recorded when the target is wrong or nothing is bound.
 */
static struct gles_texture *
texture_bound(
	struct zegl_context *context,
	GLenum target,
	int images,
	unsigned *face)
{
	struct gles_state *state;
	struct gles_texture *texture;

	/* A context with its state. */
	*face = 0U;
	state = gles_state(context);
	if (state == NULL)
		return NULL;

	/* The unit's 2D texture, its cube map as a whole, or one face of it. */
	if (target == GL_TEXTURE_2D) {
		texture = state->units[state->active_unit];
	} else if (target == GL_TEXTURE_CUBE_MAP && !images) {
		texture = state->cube_units[state->active_unit];
	} else if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z && images) {
		texture = state->cube_units[state->active_unit];
		*face = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
	} else {
		gles_error(context, GL_INVALID_ENUM);
		return NULL;
	}

	/* Name 0 is not a texture that can be changed. */
	if (texture == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the texture. */
	return texture;
}

/*
 * Returns the bound texture for a call that changes it on the CPU, what
 * framebuffer objects drew into it read back first; NULL with the error
 * recorded.
 */
static struct gles_texture *
texture_changing(
	struct zegl_context *context,
	GLenum target,
	int images,
	unsigned *face)
{
	struct gles_texture *texture;
	int status;

	/* The bound texture. */
	texture = texture_bound(context, target, images, face);
	if (texture == NULL)
		return NULL;

	/* What the device drew into it, on the CPU before the change. */
	status = gles_texture_fetch(context, texture);
	if (status != 0)
		return NULL;

	/* Succeeded: the texture, newest on the CPU. */
	return texture;
}

/* Makes a texture of a target (0: taken at its first bind) with GL's initial sampling state and no levels; NULL when there is no memory. */
static struct gles_texture *
texture_new(
	GLuint name,
	GLenum target)
{
	struct gles_texture *texture;

	/* The object. */
	texture = calloc(1U, sizeof(*texture));
	if (texture == NULL)
		return NULL;

	/* GL's initial state. */
	texture->name = name;
	texture->target = target;
	texture->min_filter = GL_NEAREST_MIPMAP_LINEAR;
	texture->mag_filter = GL_LINEAR;
	texture->wrap_s = GL_REPEAT;
	texture->wrap_t = GL_REPEAT;
	return texture;
}

/*
 * Converts the application's pixels to RGBA8 rows, honouring the unpack
 * alignment; NULL pixels give black.  Returns the new rows, or NULL with
 * the error recorded.
 */
static unsigned char *
texture_convert(
	struct zegl_context *context,
	GLenum format,
	GLenum type,
	GLsizei width,
	GLsizei height,
	const void *pixels)
{
	struct gles_state *state;
	const unsigned char *source;
	const unsigned char *row_start;
	unsigned char *converted;
	unsigned char *out;
	unsigned value;
	size_t texel;
	size_t stride;
	size_t alignment;
	GLsizei x;
	GLsizei y;

	/* The bytes a source texel takes, by format and type. */
	state = gles_state(context);
	texel = 0U;
	if (type == GL_UNSIGNED_BYTE) {
		if (format == GL_RGBA || format == GL_BGRA_EXT)
			texel = 4U;
		if (format == GL_RGB)
			texel = 3U;
		if (format == GL_LUMINANCE_ALPHA)
			texel = 2U;
		if (format == GL_LUMINANCE || format == GL_ALPHA)
			texel = 1U;
	} else if (type == GL_UNSIGNED_SHORT_5_6_5 && format == GL_RGB) {
		texel = 2U;
	} else if ((type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) && format == GL_RGBA) {
		texel = 2U;
	}

	/* A combination not offered. */
	if (texel == 0U) {
		gles_error(context, GL_INVALID_ENUM);
		return NULL;
	}

	/* The rows. */
	converted = calloc((size_t)width * (size_t)height + 1U, 4U);
	if (converted == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* Without pixels the level is black. */
	if (pixels == NULL)
		return converted;

	/* The source rows are aligned to the unpack alignment. */
	alignment = (size_t)state->unpack_alignment;
	stride = ((size_t)width * texel + alignment - 1U) / alignment * alignment;

	/* Each texel into RGBA. */
	for (y = 0; y < height; y++) {
		row_start = (const unsigned char *)pixels + (size_t)y * stride;
		out = converted + (size_t)y * (size_t)width * 4U;
		for (x = 0; x < width; x++) {
			source = row_start + (size_t)x * texel;
			value = 0U;
			if (texel == 2U && type != GL_UNSIGNED_BYTE)
				value = (unsigned)source[0] | ((unsigned)source[1] << 8);

			/* The channels of the format. */
			switch (format) {
			case GL_RGBA:
				if (type == GL_UNSIGNED_BYTE) {
					memcpy(out, source, 4U);
				} else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
					out[0] = (unsigned char)(((value >> 12) & 15U) * 17U);
					out[1] = (unsigned char)(((value >> 8) & 15U) * 17U);
					out[2] = (unsigned char)(((value >> 4) & 15U) * 17U);
					out[3] = (unsigned char)((value & 15U) * 17U);
				} else {
					out[0] = (unsigned char)(((value >> 11) & 31U) * 255U / 31U);
					out[1] = (unsigned char)(((value >> 6) & 31U) * 255U / 31U);
					out[2] = (unsigned char)(((value >> 1) & 31U) * 255U / 31U);
					out[3] = (unsigned char)((value & 1U) * 255U);
				}

				break;
			case GL_BGRA_EXT:
				out[0] = source[2];
				out[1] = source[1];
				out[2] = source[0];
				out[3] = source[3];
				break;
			case GL_RGB:
				if (type == GL_UNSIGNED_BYTE) {
					memcpy(out, source, 3U);
				} else {
					out[0] = (unsigned char)(((value >> 11) & 31U) * 255U / 31U);
					out[1] = (unsigned char)(((value >> 5) & 63U) * 255U / 63U);
					out[2] = (unsigned char)((value & 31U) * 255U / 31U);
				}

				/* Opaque. */
				out[3] = 255U;
				break;
			case GL_LUMINANCE_ALPHA:
				out[0] = source[0];
				out[1] = source[0];
				out[2] = source[0];
				out[3] = source[1];
				break;
			case GL_LUMINANCE:
				out[0] = source[0];
				out[1] = source[0];
				out[2] = source[0];
				out[3] = 255U;
				break;
			default:
				out[3] = source[0];
				break;
			}

			/* The next texel. */
			out += 4;
		}
	}

	/* Succeeded: the RGBA rows. */
	return converted;
}

/* Sets one sampling parameter; nonzero with the error recorded when the parameter or its value is not one. */
static int
texture_parameter(
	struct zegl_context *context,
	struct gles_texture *texture,
	GLenum pname,
	GLint value)
{
	GLenum mode;
	int mipmapped;
	int status;

	/* The value as an enum. */
	mode = (GLenum)value;

	/* The parameter. */
	switch (pname) {
	case GL_TEXTURE_MIN_FILTER:
		mipmapped = texture_mipmapped(mode);
		if (mode != GL_NEAREST && mode != GL_LINEAR && !mipmapped)
			break;

		/* A new filter may change the image's levels: what the device drew is read back first. */
		if (texture->min_filter != mode) {
			status = gles_texture_fetch(context, texture);
			if (status != 0)
				return -1;
			texture->dirty = 1;
		}

		/* The filter. */
		texture->min_filter = mode;
		return 0;
	case GL_TEXTURE_MAG_FILTER:
		if (mode != GL_NEAREST && mode != GL_LINEAR)
			break;
		texture->mag_filter = mode;
		return 0;
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:
		if (mode != GL_REPEAT && mode != GL_CLAMP_TO_EDGE && mode != GL_MIRRORED_REPEAT)
			break;
		if (pname == GL_TEXTURE_WRAP_S)
			texture->wrap_s = mode;
		if (pname == GL_TEXTURE_WRAP_T)
			texture->wrap_t = mode;
		return 0;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return -1;
	}

	/* A value the parameter does not take. */
	gles_error(context, GL_INVALID_ENUM);
	return -1;
}

/* Reports whether a minification filter samples mipmaps. */
static int
texture_mipmapped(
	GLenum filter)
{
	/* The four mipmap filters. */
	switch (filter) {
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_LINEAR_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
	case GL_LINEAR_MIPMAP_LINEAR:
		return 1;
	default:
		break;
	}

	/* The other two. */
	return 0;
}

/* Returns how many levels a texture's image has: the chain from level 0 of halving sizes (on every face) when it mipmaps, else 1. */
static uint32_t
texture_levels(
	struct gles_texture *texture)
{
	const struct gles_level *level;
	uint32_t levels;
	uint32_t faces;
	uint32_t face;
	int width;
	int height;
	int mipmapped;
	int whole;

	/* Level 0 only, unless the filter mipmaps. */
	mipmapped = texture_mipmapped(texture->min_filter);
	if (!mipmapped)
		return 1U;

	/* Each next level whose size is the halved one on every face. */
	faces = texture_layers(texture);
	width = texture->levels[0].width;
	height = texture->levels[0].height;
	for (levels = 1U; levels < GLES_LEVELS; levels++) {
		if (width == 1 && height == 1)
			break;
		width = width / 2;
		if (width < 1)
			width = 1;
		height = height / 2;
		if (height < 1)
			height = 1;

		/* The level of every face, of that size. */
		whole = 1;
		for (face = 0U; face < faces; face++) {
			level = &texture->levels[face * GLES_LEVELS + levels];
			if (level->width != width || level->height != height || level->pixels == NULL)
				whole = 0;
		}

		/* The chain ends where a face lacks the level. */
		if (!whole)
			break;
	}

	/* Succeeded: the chain found. */
	return levels;
}

/* Returns how many layers a texture's image has: six faces for a cube map, one for a 2D texture. */
static uint32_t
texture_layers(
	const struct gles_texture *texture)
{
	/* A cube map. */
	if (texture->target == GL_TEXTURE_CUBE_MAP)
		return GLES_FACES;

	/* Succeeded: a 2D texture. */
	return 1U;
}

/* Makes every level of a face below level 0 by halving it with a box filter, until 1x1; nonzero when there is no memory. */
static int
texture_mipmaps(
	struct gles_texture *texture,
	unsigned face)
{
	const struct gles_level *source;
	unsigned char *pixels;
	unsigned sum;
	unsigned channel;
	int width;
	int height;
	int x;
	int y;
	int dx;
	int dy;
	int sx;
	int sy;
	GLint level;

	/* Each level from the one above it, until 1x1. */
	for (level = 1; level < (GLint)GLES_LEVELS; level++) {
		source = &texture->levels[face * GLES_LEVELS + (unsigned)level - 1U];
		if (source->width == 1 && source->height == 1)
			break;

		/* The halved size, at least 1. */
		width = source->width / 2;
		if (width < 1)
			width = 1;
		height = source->height / 2;
		if (height < 1)
			height = 1;

		/* Its pixels. */
		pixels = malloc((size_t)width * (size_t)height * 4U);
		if (pixels == NULL)
			return -1;

		/* Each texel: the mean of the (up to) four above it. */
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				for (channel = 0U; channel < 4U; channel++) {
					sum = 0U;
					for (dy = 0; dy < 2; dy++) {
						for (dx = 0; dx < 2; dx++) {
							sx = x * 2 + dx;
							sy = y * 2 + dy;
							if (sx >= source->width)
								sx = source->width - 1;
							if (sy >= source->height)
								sy = source->height - 1;
							sum += source->pixels[((size_t)sy * (size_t)source->width + (size_t)sx) * 4U + channel];
						}
					}

					/* The mean, rounded. */
					pixels[((size_t)y * (size_t)width + (size_t)x) * 4U + channel] = (unsigned char)((sum + 2U) / 4U);
				}
			}
		}

		/* The level. */
		gles_texture_define(texture, face, level, width, height, pixels);
	}

	/* Succeeded: the chain. */
	return 0;
}

/* Returns Vulkan's filter for a GL filter. */
static VkFilter
texture_filter(
	GLenum filter)
{
	/* The nearest filters, then the linear ones. */
	switch (filter) {
	case GL_NEAREST:
	case GL_NEAREST_MIPMAP_NEAREST:
	case GL_NEAREST_MIPMAP_LINEAR:
		return VK_FILTER_NEAREST;
	default:
		break;
	}

	/* The rest are linear. */
	return VK_FILTER_LINEAR;
}

/* Returns Vulkan's address mode for a GL wrap mode. */
static VkSamplerAddressMode
texture_wrap(
	GLenum wrap)
{
	/* The three wraps of OpenGL ES 2. */
	switch (wrap) {
	case GL_CLAMP_TO_EDGE:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case GL_MIRRORED_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	default:
		break;
	}

	/* GL_REPEAT. */
	return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

/* Lets the per-face views of an image go (they wait for the frame), leaving the array empty. */
static void
texture_views_free(
	struct gles_state *state,
	VkImageView *views)
{
	unsigned face;

	/* Each face's view (none is VK_NULL_HANDLE, which throws nothing away). */
	for (face = 0U; face < GLES_FACES; face++) {
		gles_throw_away(state, VK_NULL_HANDLE, VK_NULL_HANDLE, views[face], VK_NULL_HANDLE);
		views[face] = VK_NULL_HANDLE;
	}
}
