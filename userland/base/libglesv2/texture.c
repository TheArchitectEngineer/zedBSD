/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The textures of zedBSD's OpenGL ES (WS068 p008, p023, p025, p028): 2D
 * textures, cube maps, 3D textures and 2D array textures, and sampler
 * objects.
 *
 * Each level keeps its texels on the CPU in the kept form of its format
 * (format.c; OpenGL ES 2's unsized formats as RGBA8, which
 * texture_convert makes), a 3D texture's slices and a 2D array's layers
 * one after another; the first draw that samples a changed texture makes
 * a new device image of the levels from the base level (a chain for a
 * mipmapping minification filter, else one level) and uploads them.  The
 * application's texels are found by the pixel store and the pixel unpack
 * buffer (pixels.c).  Samplers are made once per set of sampling state
 * and level count; a sampler object bound to a unit replaces its
 * textures' own sampling state.
 */

#include "gles.h"

#include <stdlib.h>
#include <string.h>

/* The largest level of detail GL starts with (MAX_LOD and MAX_LEVEL's initial values). */
#define TEXTURE_LOD_MAX		1000.0f
#define TEXTURE_LEVEL_MAX	1000

/*
 * The targets a call takes, as bits: GL_TEXTURE_2D, a cube map's faces
 * (the calls that give a face's level texels), GL_TEXTURE_CUBE_MAP as a
 * whole, GL_TEXTURE_3D and GL_TEXTURE_2D_ARRAY.
 */
#define TEXTURE_TAKES_2D	1U
#define TEXTURE_TAKES_FACES	2U
#define TEXTURE_TAKES_CUBE	4U
#define TEXTURE_TAKES_3D	8U
#define TEXTURE_TAKES_ARRAY	16U

/* The targets of the calls that give a 2D image, a 3D image, fixed 2D levels, and of the calls on a texture as a whole. */
#define TEXTURE_IMAGE_2D	(TEXTURE_TAKES_2D | TEXTURE_TAKES_FACES)
#define TEXTURE_IMAGE_3D	(TEXTURE_TAKES_3D | TEXTURE_TAKES_ARRAY)
#define TEXTURE_STORAGE_2D	(TEXTURE_TAKES_2D | TEXTURE_TAKES_CUBE)
#define TEXTURE_WHOLE		(TEXTURE_TAKES_2D | TEXTURE_TAKES_CUBE | TEXTURE_TAKES_3D | TEXTURE_TAKES_ARRAY)

static struct gles_texture *texture_bound(struct zegl_context *context, GLenum target, unsigned takes, unsigned *face);
static struct gles_texture *texture_changing(struct zegl_context *context, GLenum target, unsigned takes, unsigned *face);
static struct gles_texture *texture_new(GLuint name, GLenum target);
static void texture_level_set(struct gles_texture *texture, unsigned face, GLint level, int width, int height, int depth, unsigned char *pixels, const struct gles_format *format);
static void texture_sampling_initial(struct gles_sampling *sampling);
static unsigned texture_shape(const struct gles_texture *texture);
static uint32_t texture_faces(const struct gles_texture *texture);
static int texture_mipmaps(struct gles_texture *texture, unsigned face, GLint last);
static int texture_mipmaps_kept(struct gles_texture *texture, unsigned face, GLint last);
static void texture_views_free(struct gles_state *state, struct gles_texture *texture);
static void texture_opaque(const struct gles_format *format, VkComponentMapping *components);
static unsigned char *texture_convert(struct zegl_context *context, GLenum format, GLenum type, GLsizei width, GLsizei height, const void *pixels);
static unsigned char *texture_texels(struct zegl_context *context, const struct gles_format *storage, GLenum format, GLenum type, GLsizei width, GLsizei height, GLsizei depth, const void *pixels);
static unsigned char *texture_from_framebuffer(struct zegl_context *context, const struct gles_format *storage, GLint x, GLint y, GLsizei width, GLsizei height);
static void texture_rows_place(struct gles_level *destination, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, const unsigned char *rows);
static int texture_volume_size(struct gles_state *state, const struct gles_texture *texture, GLsizei width, GLsizei height, GLsizei depth);
static int texture_parameter(struct zegl_context *context, struct gles_texture *texture, GLenum pname, GLint value, GLfloat number);
static int texture_sampling_parameter(struct zegl_context *context, struct gles_sampling *sampling, GLenum pname, GLint value, GLfloat number);
static int texture_get(struct gles_texture *texture, GLenum pname, GLfloat *value);
static int texture_sampling_get(const struct gles_sampling *sampling, GLenum pname, GLfloat *value);
static struct gles_sampler_object *texture_sampler_object(struct zegl_context *context, GLuint name);
static int texture_mipmapped(GLenum filter);
static int texture_nearest(const struct gles_sampling *sampling);
static uint32_t texture_levels(struct gles_texture *texture);
static VkFilter texture_filter(GLenum filter);
static VkSamplerAddressMode texture_wrap(GLenum wrap);
static VkComponentSwizzle texture_swizzle(GLenum source, VkComponentSwizzle identity);

/*
 * Brings a texture's device image up to date with its levels from the
 * base level, in the Vulkan format the levels are kept in: a 2D image (a
 * cube map's with a layer per face, a 2D array's with a layer per layer)
 * or a 3D image.  Returns 0, or -1 when the device has no memory for it.
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
	VkImageAspectFlags copy_aspect;
	VkImageAspectFlags whole_aspect;
	const struct gles_format *format;
	const struct gles_level *source;
	unsigned char *mapped;
	void *pointer;
	size_t total;
	size_t bytes;
	uint32_t features;
	uint32_t levels;
	uint32_t faces;
	uint32_t layers;
	uint32_t level;
	uint32_t face;
	uint32_t count;
	uint32_t type;
	uint32_t base;
	unsigned shape;
	VkResult result;
	int status;

	/* An image that is up to date stays. */
	if (!texture->dirty && texture->image != VK_NULL_HANDLE)
		return 0;

	/* The levels and faces the image has, their format, and the bytes they take (every slice of every level). */
	base = (uint32_t)texture->base_level;
	levels = texture_levels(texture);
	faces = texture_faces(texture);
	shape = texture_shape(texture);
	format = texture->levels[base].format;
	if (format == NULL)
		return -1;
	total = 0U;
	for (face = 0U; face < faces; face++) {
		for (level = 0U; level < levels; level++) {
			source = &texture->levels[face * GLES_LEVELS + base + level];
			total += (size_t)source->width * (size_t)source->height * (size_t)source->depth * format->bytes;
		}
	}

	/* The image's layers: a cube map's faces, a 2D array's layers, one otherwise. */
	layers = faces;
	if (shape == GLES_SHAPE_ARRAY)
		layers = (uint32_t)texture->levels[base].depth;

	/* A depth format is copied into its depth aspect; a barrier names every aspect it has. */
	copy_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	whole_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format->kind == GLES_TEXEL_DEPTH) {
		copy_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		whole_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (format->stencil)
			whole_aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	/* The image (a cube map's can be viewed as one, a 3D texture's is 3D). */
	features = gles_image_features(state, format->vk);
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	if (shape == GLES_SHAPE_CUBE)
		create.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = format->vk;
	create.extent.width = (uint32_t)texture->levels[base].width;
	create.extent.height = (uint32_t)texture->levels[base].height;
	create.extent.depth = 1U;
	if (shape == GLES_SHAPE_3D) {
		create.imageType = VK_IMAGE_TYPE_3D;
		create.extent.depth = (uint32_t)texture->levels[base].depth;
	}

	/* Its levels and layers, sampled and copied. */
	create.mipLevels = levels;
	create.arrayLayers = layers;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	/* A 2D texture, a cube map or a 2D array is drawn into when its format can be (a 3D texture's slices cannot be viewed as 2D images). */
	if (shape != GLES_SHAPE_3D) {
		if (format->kind != GLES_TEXEL_DEPTH && (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0U)
			create.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if (format->kind == GLES_TEXEL_DEPTH && (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U)
			create.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	/* Made. */
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

	/* Bound, with a view of every level in the texture's shape whose channels follow the swizzle. */
	result = vkBindImageMemory(state->device, image, memory, 0U);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	if (shape == GLES_SHAPE_CUBE)
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	if (shape == GLES_SHAPE_3D)
		view.viewType = VK_IMAGE_VIEW_TYPE_3D;
	if (shape == GLES_SHAPE_ARRAY)
		view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	view.format = format->vk;
	view.components.r = texture_swizzle(texture->swizzle[0], VK_COMPONENT_SWIZZLE_R);
	view.components.g = texture_swizzle(texture->swizzle[1], VK_COMPONENT_SWIZZLE_G);
	view.components.b = texture_swizzle(texture->swizzle[2], VK_COMPONENT_SWIZZLE_B);
	view.components.a = texture_swizzle(texture->swizzle[3], VK_COMPONENT_SWIZZLE_A);
	texture_opaque(format, &view.components);
	view.subresourceRange.aspectMask = copy_aspect;
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

	/* The staging buffer with the levels one after another. */
	status = gles_device_buffer(state, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &staging_memory, &pointer);
	if (status != 0) {
		gles_throw_away(state, VK_NULL_HANDLE, image, image_view, memory);
		return -1;
	}

	/*
	 * Each face's levels one after another (the image's level 0 is the
	 * base level): a level's slices are a 3D image's depth, a 2D array's
	 * its layers.
	 */
	mapped = pointer;
	total = 0U;
	count = 0U;
	memset(copies, 0, sizeof(copies));
	for (face = 0U; face < faces; face++) {
		for (level = 0U; level < levels; level++) {
			source = &texture->levels[face * GLES_LEVELS + base + level];
			bytes = (size_t)source->width * (size_t)source->height * (size_t)source->depth * format->bytes;
			memcpy(mapped + total, source->pixels, bytes);
			copies[count].bufferOffset = total;
			copies[count].imageSubresource.aspectMask = copy_aspect;
			copies[count].imageSubresource.mipLevel = level;
			copies[count].imageSubresource.baseArrayLayer = face;
			copies[count].imageSubresource.layerCount = 1U;
			copies[count].imageExtent.width = (uint32_t)source->width;
			copies[count].imageExtent.height = (uint32_t)source->height;
			copies[count].imageExtent.depth = 1U;
			if (shape == GLES_SHAPE_3D)
				copies[count].imageExtent.depth = (uint32_t)source->depth;
			if (shape == GLES_SHAPE_ARRAY)
				copies[count].imageSubresource.layerCount = (uint32_t)source->depth;
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
		barrier.subresourceRange.aspectMask = whole_aspect;
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
		gles_throw_away(state, VK_NULL_HANDLE, image, image_view, memory);
		return -1;
	}

	/* The new image replaces the old one, which waits for the frame with the views of it framebuffer objects drew into. */
	gles_throw_away(state, VK_NULL_HANDLE, texture->image, texture->view, texture->memory);
	texture_views_free(state, texture);
	texture->image = image;
	texture->memory = memory;
	texture->view = image_view;
	texture->level_count = levels;
	texture->image_format = format;
	texture->dirty = 0;

	/* Succeeded: the image has the levels. */
	return 0;
}

/*
 * Reports whether a texture can be sampled with a sampling state (NULL:
 * its own): its base level is specified (a cube map's on every face,
 * square, all the same size and format), and a format that cannot be
 * filtered (integers, 32-bit floats, depth without comparison) is read
 * with nearest filters only.
 */
int
gles_texture_complete(
	struct gles_texture *texture,
	const struct gles_sampling *sampling)
{
	const struct gles_level *level;
	const struct gles_level *first;
	const struct gles_format *format;
	unsigned faces;
	unsigned face;
	int nearest;

	/* A texture, bound to a target at least once, whose base level is not above its most. */
	if (texture == NULL || texture->target == 0U)
		return 0;
	if (texture->base_level > texture->max_level)
		return 0;

	/* Its own sampling unless a sampler object's is given. */
	if (sampling == NULL)
		sampling = &texture->sampling;

	/* The base level of each face, as large as the first face's and of its format. */
	faces = texture_faces(texture);
	first = &texture->levels[texture->base_level];
	for (face = 0U; face < faces; face++) {
		level = &texture->levels[face * GLES_LEVELS + (unsigned)texture->base_level];
		if (level->width <= 0 || level->height <= 0 || level->format == NULL)
			return 0;
		if (level->width != first->width || level->height != first->height)
			return 0;
		if (level->format != first->format)
			return 0;
	}

	/* A cube map's faces are square. */
	if (faces > 1U && first->width != first->height)
		return 0;

	/* A format a linear filter cannot read, read with one. */
	format = first->format;
	nearest = texture_nearest(sampling);
	if (!nearest && (format->kind == GLES_TEXEL_INT || format->kind == GLES_TEXEL_UINT))
		return 0;
	if (!nearest && format->kind != GLES_TEXEL_DEPTH && !format->filterable)
		return 0;
	if (!nearest && format->kind == GLES_TEXEL_DEPTH && sampling->compare_mode == GL_NONE)
		return 0;

	/* Succeeded: it can be sampled. */
	return 1;
}

/*
 * Returns the sampler for a sampling state (NULL: the texture's own) and
 * the texture's level count, making it the first time; VK_NULL_HANDLE
 * when it cannot be made.
 */
VkSampler
gles_sampler_get(
	struct gles_state *state,
	struct gles_texture *texture,
	const struct gles_sampling *sampling)
{
	VkSamplerCreateInfo create;
	struct gles_sampler *sampler;
	VkResult result;
	float levels;
	int mipmapped;
	int differs;

	/* Its own sampling unless a sampler object's is given. */
	if (sampling == NULL)
		sampling = &texture->sampling;

	/* One already made for the same state and levels. */
	for (sampler = state->samplers; sampler != NULL; sampler = sampler->next) {
		differs = memcmp(&sampler->sampling, sampling, sizeof(*sampling));
		if (differs == 0 && sampler->levels == texture->level_count)
			return sampler->sampler;
	}

	/* A new one: the filters, the mipmap mode and the wraps. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	create.magFilter = texture_filter(sampling->mag_filter);
	create.minFilter = texture_filter(sampling->min_filter);
	create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	if (sampling->min_filter == GL_NEAREST_MIPMAP_LINEAR || sampling->min_filter == GL_LINEAR_MIPMAP_LINEAR)
		create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	create.addressModeU = texture_wrap(sampling->wrap_s);
	create.addressModeV = texture_wrap(sampling->wrap_t);
	create.addressModeW = texture_wrap(sampling->wrap_r);
	create.maxAnisotropy = 1.0f;

	/* The depth comparison (GL's compare functions are Vulkan's in the same order). */
	create.compareOp = VK_COMPARE_OP_NEVER;
	if (sampling->compare_mode == GL_COMPARE_REF_TO_TEXTURE) {
		create.compareEnable = VK_TRUE;
		create.compareOp = (VkCompareOp)(sampling->compare_func - GL_NEVER);
	}

	/* A filter without mipmaps samples the first level only (the 0.25 keeps the magnification test right). */
	create.maxLod = 0.25f;
	mipmapped = texture_mipmapped(sampling->min_filter);
	if (mipmapped) {
		levels = (float)texture->level_count;
		create.minLod = sampling->min_lod;
		if (create.minLod < 0.0f)
			create.minLod = 0.0f;
		if (create.minLod > levels)
			create.minLod = levels;
		create.maxLod = sampling->max_lod;
		if (create.maxLod > levels)
			create.maxLod = levels;
		if (create.maxLod < create.minLod)
			create.maxLod = create.minLod;
	}

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
	sampler->sampling = *sampling;
	sampler->levels = texture->level_count;
	sampler->next = state->samplers;
	state->samplers = sampler;
	return sampler->sampler;
}

/*
 * Returns the black texture of a shape (GLES_SHAPE_*) and a kind (0
 * float, 1 int, 2 unsigned, 3 depth) sampled where a unit has no complete
 * texture of the kind its sampler reads, making it at its first use; NULL
 * when there is no memory or no format.
 */
struct gles_texture *
gles_texture_black(
	struct gles_state *state,
	unsigned shape,
	unsigned kind)
{
	static const GLenum internals[GLES_BLACK_KINDS] = { GL_RGBA, GL_RGBA8I, GL_RGBA8UI, GL_DEPTH_COMPONENT16 };
	static const GLenum types[GLES_BLACK_KINDS] = { GL_UNSIGNED_BYTE, GL_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT };
	static const GLenum targets[GLES_SHAPES] = { GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY };
	const struct gles_format *format;
	struct gles_texture *texture;
	struct gles_texture **kept;
	unsigned char *pixels;
	unsigned faces;
	unsigned face;

	/* Made once per shape and kind (a 3D texture has no depth one: no sampler compares it). */
	if (kind >= GLES_BLACK_KINDS)
		kind = 0U;
	if (shape >= GLES_SHAPES)
		shape = GLES_SHAPE_2D;
	if (shape == GLES_SHAPE_3D && kind == 3U)
		kind = 0U;
	kept = &state->blacks[shape][kind];

	/* Already made. */
	if (*kept != NULL)
		return *kept;

	/* The kind's format. */
	format = gles_format_find(state, internals[kind], types[kind]);
	if (format == NULL)
		return NULL;

	/* The texture, read with nearest filters (integers and depth cannot be filtered). */
	texture = texture_new(0U, targets[shape]);
	if (texture == NULL)
		return NULL;
	texture->sampling.min_filter = GL_NEAREST;
	texture->sampling.mag_filter = GL_NEAREST;

	/* One black texel on each face, or in the one slice or layer (opaque: alpha 255, or 1 for the integers). */
	faces = texture_faces(texture);
	for (face = 0U; face < faces; face++) {
		pixels = calloc(format->bytes, 1U);
		if (pixels == NULL) {
			gles_texture_free(state, texture);
			return NULL;
		}

		/* Opaque. */
		if (kind == 0U)
			pixels[3] = 255U;
		if (kind == 1U || kind == 2U)
			pixels[3] = 1U;
		texture_level_set(texture, face, 0, 1, 1, 1, pixels, format);
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
	texture_views_free(state, texture);

	/* The levels of every face, then the object. */
	for (level = 0U; level < GLES_FACES * GLES_LEVELS; level++)
		free(texture->levels[level].pixels);
	free(texture);
}

/*
 * Gives a level of a texture's face (0 for a 2D texture) new texels of one
 * image kept in a format, which the texture takes over.
 */
void
gles_texture_define(
	struct gles_texture *texture,
	unsigned face,
	GLint level,
	int width,
	int height,
	unsigned char *pixels,
	const struct gles_format *format)
{
	/* One slice (none when the level is let go). */
	if (width == 0 || height == 0) {
		texture_level_set(texture, face, level, width, height, 0, pixels, format);
		return;
	}

	/* The level's one image. */
	texture_level_set(texture, face, level, width, height, 1, pixels, format);
}

/*
 * Gives a level of a 3D or 2D array texture new texels of its slices or
 * layers, one after another, kept in a format, which the texture takes
 * over.
 */
void
gles_texture_define_volume(
	struct gles_texture *texture,
	GLint level,
	int width,
	int height,
	int depth,
	unsigned char *pixels,
	const struct gles_format *format)
{
	/* The level of the only face. */
	texture_level_set(texture, 0U, level, width, height, depth, pixels, format);
}

/*
 * Returns the view of one level and one layer (a cube map's face, a 2D
 * array's layer) of a texture's image that a framebuffer object draws
 * into, making it at its first use; VK_NULL_HANDLE when the image has no
 * such level or layer, or the device refused.  The level is the image's
 * (0: the base level).
 */
VkImageView
gles_texture_attach_view(
	struct gles_state *state,
	struct gles_texture *texture,
	uint32_t level,
	uint32_t layer)
{
	VkImageViewCreateInfo create;
	struct gles_attach_view *entry;
	VkResult result;

	/* An image with the level. */
	if (texture->image == VK_NULL_HANDLE || level >= texture->level_count)
		return VK_NULL_HANDLE;

	/* One made before. */
	for (entry = texture->attach_views; entry != NULL; entry = entry->next) {
		if (entry->level == level && entry->layer == layer)
			return entry->view;
	}

	/* A new entry. */
	entry = calloc(1U, sizeof(*entry));
	if (entry == NULL)
		return VK_NULL_HANDLE;

	/* A 2D view of the level and layer, every aspect the format draws. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	create.image = texture->image;
	create.viewType = VK_IMAGE_VIEW_TYPE_2D;
	create.format = texture->image_format->vk;
	create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	if (texture->image_format->kind == GLES_TEXEL_DEPTH)
		create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (texture->image_format->stencil)
		create.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	create.subresourceRange.baseMipLevel = level;
	create.subresourceRange.levelCount = 1U;
	create.subresourceRange.baseArrayLayer = layer;
	create.subresourceRange.layerCount = 1U;
	result = vkCreateImageView(state->device, &create, NULL, &entry->view);
	if (result != VK_SUCCESS) {
		free(entry);
		return VK_NULL_HANDLE;
	}

	/* Succeeded: the view, kept with the image. */
	entry->level = level;
	entry->layer = layer;
	entry->next = texture->attach_views;
	texture->attach_views = entry;
	return entry->view;
}

/*
 * Frees the sampler objects and their namespace.
 */
void
gles_samplers_release(
	struct gles_state *state)
{
	GLuint name;

	/* Each sampler object. */
	for (name = 1U; name < state->sampler_objects.capacity; name++)
		free(state->sampler_objects.objects[name]);

	/* The namespace. */
	free(state->sampler_objects.objects);
	state->sampler_objects.objects = NULL;
	state->sampler_objects.capacity = 0U;
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
			if (state->volume_units[unit] == texture)
				state->volume_units[unit] = NULL;
			if (state->array_units[unit] == texture)
				state->array_units[unit] = NULL;
		}

		/* Detached from the bound framebuffer object; the name and the texture go (its image waits for the frame). */
		gles_framebuffers_forget(state, GLES_ATTACH_TEXTURE, textures[index]);
		gles_names_remove(&state->textures, textures[index]);
		gles_texture_free(state, texture);
	}
}

/*
 * Binds a texture to the active unit's GL_TEXTURE_2D,
 * GL_TEXTURE_CUBE_MAP, GL_TEXTURE_3D or GL_TEXTURE_2D_ARRAY, making it
 * when the name is new; a texture keeps the target it was first bound to.
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

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;

	/* One of the four targets. */
	switch (target) {
	case GL_TEXTURE_2D:
	case GL_TEXTURE_CUBE_MAP:
	case GL_TEXTURE_3D:
	case GL_TEXTURE_2D_ARRAY:
		break;
	default:
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
	switch (target) {
	case GL_TEXTURE_CUBE_MAP:
		state->cube_units[state->active_unit] = texture;
		break;
	case GL_TEXTURE_3D:
		state->volume_units[state->active_unit] = texture;
		break;
	case GL_TEXTURE_2D_ARRAY:
		state->array_units[state->active_unit] = texture;
		break;
	default:
		state->units[state->active_unit] = texture;
		break;
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
 * Specifies a level of the bound texture in an internal format.
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
	const struct gles_format *storage;
	unsigned char *converted;
	unsigned face;

	/* The bound texture (a cube map's face), a level and a size it can have. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_IMAGE_2D, &face);
	if (texture == NULL)
		return;
	if (level < 0 ||
	    level >= (GLint)GLES_LEVELS ||
	    width < 0 ||
	    height < 0 ||
	    width > 16384 ||
	    height > 16384 ||
	    border != 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A cube map's faces are square. */
	if (texture->target == GL_TEXTURE_CUBE_MAP && width != height) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* glTexStorage2D fixed the levels. */
	if (texture->immutable) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The format the level is kept in. */
	storage = gles_format_find(gles_state(context), (GLenum)internalformat, type);
	if (storage == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The pixels in the kept form (NULL pixels: zeros). */
	converted = texture_texels(context, storage, format, type, width, height, 0, pixels);
	if (converted == NULL)
		return;

	/* Succeeded: the level. */
	gles_texture_define(texture, face, level, width, height, converted, storage);
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
	size_t bytes;
	unsigned face;
	GLsizei row;

	/* The bound texture (a cube map's face) and a rectangle inside a specified level. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_IMAGE_2D, &face);
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

	/* A level that has texels. */
	if (destination->format == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The pixels in the level's kept form. */
	converted = texture_texels(context, destination->format, format, type, width, height, 0, pixels);
	if (converted == NULL)
		return;

	/* Each row into place. */
	bytes = destination->format->bytes;
	for (row = 0; row < height; row++) {
		memcpy(destination->pixels + ((size_t)(yoffset + row) * (size_t)destination->width + (size_t)xoffset) * bytes,
		       converted + (size_t)row * (size_t)width * bytes, (size_t)width * bytes);
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
	const struct gles_format *storage;
	unsigned char *pixels;
	unsigned face;

	/* The bound texture (a cube map's face), a level and a size. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_IMAGE_2D, &face);
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

	/* glTexStorage2D fixed the levels. */
	if (texture->immutable) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The format the level is kept in (unsized formats: RGBA8). */
	storage = gles_format_find(gles_state(context), internalformat, GL_UNSIGNED_BYTE);
	if (storage == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The framebuffer's pixels in the kept form. */
	pixels = texture_from_framebuffer(context, storage, x, y, width, height);
	if (pixels == NULL)
		return;

	/* Succeeded: the level. */
	gles_texture_define(texture, face, level, width, height, pixels, storage);
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
	size_t bytes;
	unsigned face;
	GLsizei row;

	/* The bound texture (a cube map's face) and a rectangle inside a specified level. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_IMAGE_2D, &face);
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

	/* A level that has texels. */
	if (destination->format == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The framebuffer's pixels in the level's kept form. */
	pixels = texture_from_framebuffer(context, destination->format, x, y, width, height);
	if (pixels == NULL)
		return;

	/* Each row into place. */
	bytes = destination->format->bytes;
	for (row = 0; row < height; row++) {
		memcpy(destination->pixels + ((size_t)(yoffset + row) * (size_t)destination->width + (size_t)xoffset) * bytes,
		       pixels + (size_t)row * (size_t)width * bytes, (size_t)width * bytes);
	}

	/* Succeeded: the image is stale. */
	free(pixels);
	texture->dirty = 1;
}

/*
 * Gives the bound texture a fixed set of levels of a sized internal
 * format (every face's, halving from the size given), whose texels are
 * zeros until glTexSubImage2D gives them.
 */
GL_APICALL void GL_APIENTRY
glTexStorage2D(
	GLenum target,
	GLsizei levels,
	GLenum internalformat,
	GLsizei width,
	GLsizei height)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	const struct gles_format *storage;
	unsigned char *pixels;
	unsigned faces;
	unsigned face;
	GLsizei level;
	GLsizei largest;
	int level_width;
	int level_height;

	/* The bound texture as a whole. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_STORAGE_2D, &face);
	if (texture == NULL)
		return;

	/* A size and at least one level. */
	if (levels < 1 || width < 1 || height < 1 || width > 16384 || height > 16384) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A sized internal format (an unsized one is not an enum this call takes). */
	storage = gles_format_find(gles_state(context), internalformat, GL_NONE);
	if (storage == NULL || storage->legacy) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* No more levels than halving the larger side gives, square faces, and fixed only once. */
	largest = width;
	if (height > largest)
		largest = height;
	for (level = 1; (largest >> level) > 0; level++)
		continue;
	if (levels > level || levels > (GLsizei)GLES_LEVELS || texture->immutable) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* A cube map's faces are square. */
	if (texture->target == GL_TEXTURE_CUBE_MAP && width != height) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each face's levels: the ones asked for with zero texels, none beyond them. */
	faces = texture_faces(texture);
	for (face = 0U; face < faces; face++) {
		level_width = width;
		level_height = height;
		for (level = 0; level < (GLsizei)GLES_LEVELS; level++) {
			pixels = NULL;
			if (level < levels) {
				pixels = calloc((size_t)level_width * (size_t)level_height * storage->bytes + 1U, 1U);
				if (pixels == NULL) {
					gles_error(context, GL_OUT_OF_MEMORY);
					return;
				}
			}

			/* The level (a level beyond them is not specified). */
			if (level < levels) {
				gles_texture_define(texture, face, level, level_width, level_height, pixels, storage);
			} else {
				gles_texture_define(texture, face, level, 0, 0, NULL, NULL);
			}

			/* The next level's size. */
			level_width /= 2;
			if (level_width < 1)
				level_width = 1;
			level_height /= 2;
			if (level_height < 1)
				level_height = 1;
		}
	}

	/* Succeeded: the levels are fixed. */
	texture->immutable = 1;
	texture->immutable_levels = levels;
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
 * Specifies a level of the bound 3D or 2D array texture in an internal
 * format: its slices or layers, one image after another.
 */
GL_APICALL void GL_APIENTRY
glTexImage3D(
	GLenum target,
	GLint level,
	GLint internalformat,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
	GLint border,
	GLenum format,
	GLenum type,
	const void *pixels)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_texture *texture;
	const struct gles_format *storage;
	unsigned char *converted;
	unsigned face;
	int fits;

	/* The bound 3D or 2D array texture, a level and a border of 0. */
	context = gles_context();
	state = gles_state(context);
	texture = texture_changing(context, target, TEXTURE_IMAGE_3D, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS || border != 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A size the shape allows. */
	fits = texture_volume_size(state, texture, width, height, depth);
	if (!fits) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* glTexStorage3D fixed the levels. */
	if (texture->immutable) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The format the level is kept in. */
	storage = gles_format_find(state, (GLenum)internalformat, type);
	if (storage == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A 3D texture holds no depth. */
	if (texture->target == GL_TEXTURE_3D && storage->kind == GLES_TEXEL_DEPTH) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The texels of every slice in the kept form (none: zeros). */
	converted = texture_texels(context, storage, format, type, width, height, depth, pixels);
	if (converted == NULL)
		return;

	/* Succeeded: the level. */
	gles_texture_define_volume(texture, level, width, height, depth, converted, storage);
}

/*
 * Replaces a box of a level of the bound 3D or 2D array texture.
 */
GL_APICALL void GL_APIENTRY
glTexSubImage3D(
	GLenum target,
	GLint level,
	GLint xoffset,
	GLint yoffset,
	GLint zoffset,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
	GLenum format,
	GLenum type,
	const void *pixels)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	struct gles_level *destination;
	unsigned char *converted;
	unsigned face;

	/* The bound 3D or 2D array texture and a level. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_IMAGE_3D, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A box inside the level. */
	destination = &texture->levels[level];
	if (xoffset < 0 ||
	    yoffset < 0 ||
	    zoffset < 0 ||
	    width < 0 ||
	    height < 0 ||
	    depth < 0 ||
	    xoffset + width > destination->width ||
	    yoffset + height > destination->height ||
	    zoffset + depth > destination->depth) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A level that has texels. */
	if (destination->format == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The texels in the level's kept form. */
	converted = texture_texels(context, destination->format, format, type, width, height, depth, pixels);
	if (converted == NULL)
		return;

	/* Each row of each slice into place. */
	texture_rows_place(destination, xoffset, yoffset, zoffset, width, height, depth, converted);

	/* Succeeded: the image is stale. */
	free(converted);
	texture->dirty = 1;
}

/*
 * Replaces a rectangle of one slice or layer of a level of the bound 3D
 * or 2D array texture with one of the framebuffer.
 */
GL_APICALL void GL_APIENTRY
glCopyTexSubImage3D(
	GLenum target,
	GLint level,
	GLint xoffset,
	GLint yoffset,
	GLint zoffset,
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

	/* The bound 3D or 2D array texture and a level. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_IMAGE_3D, &face);
	if (texture == NULL)
		return;
	if (level < 0 || level >= (GLint)GLES_LEVELS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A rectangle inside one slice of the level. */
	destination = &texture->levels[level];
	if (xoffset < 0 ||
	    yoffset < 0 ||
	    zoffset < 0 ||
	    width <= 0 ||
	    height <= 0 ||
	    xoffset + width > destination->width ||
	    yoffset + height > destination->height ||
	    zoffset >= destination->depth) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A level that has texels. */
	if (destination->format == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The framebuffer's pixels in the level's kept form. */
	pixels = texture_from_framebuffer(context, destination->format, x, y, width, height);
	if (pixels == NULL)
		return;

	/* Each row into place in the slice. */
	texture_rows_place(destination, xoffset, yoffset, zoffset, width, height, 1, pixels);

	/* Succeeded: the image is stale. */
	free(pixels);
	texture->dirty = 1;
}

/*
 * Gives the bound 3D or 2D array texture a fixed set of levels of a sized
 * internal format (halving from the size given; a 2D array keeps its
 * layers), whose texels are zeros until glTexSubImage3D gives them.
 */
GL_APICALL void GL_APIENTRY
glTexStorage3D(
	GLenum target,
	GLsizei levels,
	GLenum internalformat,
	GLsizei width,
	GLsizei height,
	GLsizei depth)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_texture *texture;
	const struct gles_format *storage;
	unsigned char *pixels;
	unsigned face;
	GLsizei level;
	GLsizei largest;
	int level_width;
	int level_height;
	int level_depth;
	int fits;

	/* The bound 3D or 2D array texture. */
	context = gles_context();
	state = gles_state(context);
	texture = texture_changing(context, target, TEXTURE_IMAGE_3D, &face);
	if (texture == NULL)
		return;

	/* At least one level, and a size the shape allows. */
	fits = texture_volume_size(state, texture, width, height, depth);
	if (levels < 1 || width < 1 || height < 1 || depth < 1 || !fits) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A sized internal format (an unsized one is not an enum this call takes). */
	storage = gles_format_find(state, internalformat, GL_NONE);
	if (storage == NULL || storage->legacy) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A 3D texture holds no depth. */
	if (texture->target == GL_TEXTURE_3D && storage->kind == GLES_TEXEL_DEPTH) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* No more levels than halving the largest side gives (a 2D array's layers are not halved), and fixed only once. */
	largest = width;
	if (height > largest)
		largest = height;
	if (texture->target == GL_TEXTURE_3D && depth > largest)
		largest = depth;
	for (level = 1; (largest >> level) > 0; level++)
		continue;
	if (levels > level || levels > (GLsizei)GLES_LEVELS || texture->immutable) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The levels asked for with zero texels, none beyond them. */
	level_width = width;
	level_height = height;
	level_depth = depth;
	for (level = 0; level < (GLsizei)GLES_LEVELS; level++) {
		pixels = NULL;
		if (level < levels) {
			pixels = calloc((size_t)level_width * (size_t)level_height * (size_t)level_depth * storage->bytes + 1U, 1U);
			if (pixels == NULL) {
				gles_error(context, GL_OUT_OF_MEMORY);
				return;
			}
		}

		/* The level (a level beyond them is not specified). */
		if (level < levels) {
			gles_texture_define_volume(texture, level, level_width, level_height, level_depth, pixels, storage);
		} else {
			gles_texture_define_volume(texture, level, 0, 0, 0, NULL, NULL);
		}

		/* The next level's size: a 3D texture's depth halves too. */
		level_width /= 2;
		if (level_width < 1)
			level_width = 1;
		level_height /= 2;
		if (level_height < 1)
			level_height = 1;
		if (texture->target == GL_TEXTURE_3D) {
			level_depth /= 2;
			if (level_depth < 1)
				level_depth = 1;
		}
	}

	/* Succeeded: the levels are fixed. */
	texture->immutable = 1;
	texture->immutable_levels = levels;
}

/*
 * Refuses compressed 3D textures: no compressed format is offered.
 */
GL_APICALL void GL_APIENTRY
glCompressedTexImage3D(
	GLenum target,
	GLint level,
	GLenum internalformat,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
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
	(void)depth;
	(void)border;
	(void)imageSize;
	(void)data;
	context = gles_context();
	if (context != NULL)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Refuses compressed 3D textures: no compressed format is offered.
 */
GL_APICALL void GL_APIENTRY
glCompressedTexSubImage3D(
	GLenum target,
	GLint level,
	GLint xoffset,
	GLint yoffset,
	GLint zoffset,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
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
	(void)zoffset;
	(void)width;
	(void)height;
	(void)depth;
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
	texture = texture_bound(context, target, TEXTURE_WHOLE, &face);
	if (texture == NULL)
		return;
	(void)texture_parameter(context, texture, pname, param, (GLfloat)param);
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
	struct zegl_context *context;
	struct gles_texture *texture;
	unsigned face;

	/* The bound texture takes it (an enum or a level as the integer, a level of detail as the float). */
	context = gles_context();
	texture = texture_bound(context, target, TEXTURE_WHOLE, &face);
	if (texture == NULL)
		return;
	(void)texture_parameter(context, texture, pname, (GLint)param, param);
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
	glTexParameterf(target, pname, params[0]);
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
	GLfloat value;
	unsigned face;
	int status;

	/* The bound texture's parameter. */
	context = gles_context();
	texture = texture_bound(context, target, TEXTURE_WHOLE, &face);
	if (texture == NULL)
		return;
	status = texture_get(texture, pname, &value);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Succeeded: the value, rounded. */
	*params = (GLint)value;
	if (value > 0.0f)
		*params = (GLint)(value + 0.5f);
	if (value < 0.0f)
		*params = (GLint)(value - 0.5f);
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
	struct zegl_context *context;
	struct gles_texture *texture;
	GLfloat value;
	unsigned face;
	int status;

	/* The bound texture's parameter. */
	context = gles_context();
	texture = texture_bound(context, target, TEXTURE_WHOLE, &face);
	if (texture == NULL)
		return;
	status = texture_get(texture, pname, &value);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Succeeded: the value. */
	*params = value;
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
 * Makes every level of the bound texture below its base level by halving
 * it with a box filter.
 */
GL_APICALL void GL_APIENTRY
glGenerateMipmap(
	GLenum target)
{
	struct zegl_context *context;
	struct gles_texture *texture;
	const struct gles_format *format;
	struct gles_sampling nearest;
	unsigned faces;
	unsigned face;
	unsigned shape;
	GLint last;
	int complete;
	int status;

	/* The bound texture (a cube map as a whole) with a base level. */
	context = gles_context();
	texture = texture_changing(context, target, TEXTURE_WHOLE, &face);
	if (texture == NULL)
		return;
	format = texture->levels[texture->base_level].format;
	if (format == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* A format of integers or depth, or one that cannot be filtered, has no mipmaps made for it. */
	if (format->kind == GLES_TEXEL_INT ||
	    format->kind == GLES_TEXEL_UINT ||
	    format->kind == GLES_TEXEL_DEPTH ||
	    !format->filterable) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The base levels must match (a cube map's faces), whatever the filters. */
	nearest = texture->sampling;
	nearest.min_filter = GL_NEAREST;
	nearest.mag_filter = GL_NEAREST;
	complete = gles_texture_complete(texture, &nearest);
	if (!complete) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The last level made: the most the texture has (glTexStorage2D's, MAX_LEVEL). */
	last = (GLint)GLES_LEVELS - 1;
	if (texture->max_level < last)
		last = texture->max_level;
	if (texture->immutable && texture->immutable_levels - 1 < last)
		last = texture->immutable_levels - 1;

	/*
	 * Each face's chain (the other shapes have one face): RGBA8 images
	 * averaged byte by byte, other formats and every 3D or 2D array
	 * texture through floats.
	 */
	faces = texture_faces(texture);
	shape = texture_shape(texture);
	for (face = 0U; face < faces; face++) {
		if (format->vk == VK_FORMAT_R8G8B8A8_UNORM && shape != GLES_SHAPE_3D && shape != GLES_SHAPE_ARRAY) {
			status = texture_mipmaps(texture, face, last);
		} else {
			status = texture_mipmaps_kept(texture, face, last);
		}

		/* Without memory the chain stops. */
		if (status != 0) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}
	}
}

/*
 * Makes names for sampler objects, each with GL's initial sampling state.
 */
GL_APICALL void GL_APIENTRY
glGenSamplers(
	GLsizei count,
	GLuint *samplers)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sampler_object *sampler;
	GLsizei index;
	int status;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name gets a sampler object. */
	for (index = 0; index < count; index++) {
		sampler = calloc(1U, sizeof(*sampler));
		if (sampler == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* GL's initial state under the first free name. */
		texture_sampling_initial(&sampler->sampling);
		sampler->name = gles_names_free(&state->sampler_objects);
		status = gles_names_add(&state->sampler_objects, sampler->name, sampler);
		if (status != 0) {
			free(sampler);
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The name goes back to the application. */
		samplers[index] = sampler->name;
	}
}

/*
 * Deletes sampler objects, unbinding them from every unit.
 */
GL_APICALL void GL_APIENTRY
glDeleteSamplers(
	GLsizei count,
	const GLuint *samplers)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sampler_object *sampler;
	GLsizei index;
	unsigned unit;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name that is a sampler object. */
	for (index = 0; index < count; index++) {
		sampler = gles_names_get(&state->sampler_objects, samplers[index]);
		if (sampler == NULL)
			continue;

		/* Unbound from every unit. */
		for (unit = 0U; unit < GLES_UNITS; unit++) {
			if (state->unit_samplers[unit] == sampler)
				state->unit_samplers[unit] = NULL;
		}

		/* The name and the object go. */
		gles_names_remove(&state->sampler_objects, samplers[index]);
		free(sampler);
	}
}

/*
 * Binds a sampler object to a texture unit (0: the unit's textures use
 * their own sampling state again).
 */
GL_APICALL void GL_APIENTRY
glBindSampler(
	GLuint unit,
	GLuint sampler)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_sampler_object *object;

	/* A context with its state, and a unit it has. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;
	if (unit >= GLES_UNITS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The object: none for name 0, else one glGenSamplers made. */
	object = NULL;
	if (sampler != 0U) {
		object = gles_names_get(&state->sampler_objects, sampler);
		if (object == NULL) {
			gles_error(context, GL_INVALID_OPERATION);
			return;
		}
	}

	/* Bound to the unit. */
	state->unit_samplers[unit] = object;
}

/*
 * Reports whether a name is a sampler object.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsSampler(
	GLuint sampler)
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
	object = gles_names_get(&state->sampler_objects, sampler);
	if (object == NULL)
		return GL_FALSE;
	return GL_TRUE;
}

/*
 * Sets an integer parameter of a sampler object.
 */
GL_APICALL void GL_APIENTRY
glSamplerParameteri(
	GLuint sampler,
	GLenum pname,
	GLint param)
{
	struct zegl_context *context;
	struct gles_sampler_object *object;
	int status;

	/* The sampler object takes it. */
	context = gles_context();
	object = texture_sampler_object(context, sampler);
	if (object == NULL)
		return;
	status = texture_sampling_parameter(context, &object->sampling, pname, param, (GLfloat)param);
	if (status > 0)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Sets a parameter of a sampler object from a float.
 */
GL_APICALL void GL_APIENTRY
glSamplerParameterf(
	GLuint sampler,
	GLenum pname,
	GLfloat param)
{
	struct zegl_context *context;
	struct gles_sampler_object *object;
	int status;

	/* The sampler object takes it. */
	context = gles_context();
	object = texture_sampler_object(context, sampler);
	if (object == NULL)
		return;
	status = texture_sampling_parameter(context, &object->sampling, pname, (GLint)param, param);
	if (status > 0)
		gles_error(context, GL_INVALID_ENUM);
}

/*
 * Sets an integer parameter of a sampler object from an array.
 */
GL_APICALL void GL_APIENTRY
glSamplerParameteriv(
	GLuint sampler,
	GLenum pname,
	const GLint *param)
{
	/* The first value. */
	glSamplerParameteri(sampler, pname, param[0]);
}

/*
 * Sets a parameter of a sampler object from an array of floats.
 */
GL_APICALL void GL_APIENTRY
glSamplerParameterfv(
	GLuint sampler,
	GLenum pname,
	const GLfloat *param)
{
	/* The first value. */
	glSamplerParameterf(sampler, pname, param[0]);
}

/*
 * Reports a parameter of a sampler object.
 */
GL_APICALL void GL_APIENTRY
glGetSamplerParameteriv(
	GLuint sampler,
	GLenum pname,
	GLint *params)
{
	GLfloat value;

	/* The float, rounded. */
	value = 0.0f;
	glGetSamplerParameterfv(sampler, pname, &value);
	*params = (GLint)value;
	if (value > 0.0f)
		*params = (GLint)(value + 0.5f);
	if (value < 0.0f)
		*params = (GLint)(value - 0.5f);
}

/*
 * Reports a parameter of a sampler object as a float.
 */
GL_APICALL void GL_APIENTRY
glGetSamplerParameterfv(
	GLuint sampler,
	GLenum pname,
	GLfloat *params)
{
	struct zegl_context *context;
	struct gles_sampler_object *object;
	GLfloat value;
	int status;

	/* The sampler object's parameter. */
	context = gles_context();
	object = texture_sampler_object(context, sampler);
	if (object == NULL)
		return;
	status = texture_sampling_get(&object->sampling, pname, &value);
	if (status != 0) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Succeeded: the value. */
	*params = value;
}

/*
 * Returns the texture a target names on the active unit, and the face it
 * means, when the call takes the target (TEXTURE_TAKES_* bits):
 * GL_TEXTURE_2D, a cube map's face (the calls that give a level texels),
 * GL_TEXTURE_CUBE_MAP as a whole (parameters, storage and mipmaps),
 * GL_TEXTURE_3D and GL_TEXTURE_2D_ARRAY.  NULL with the error recorded
 * when the target is wrong or nothing is bound.
 */
static struct gles_texture *
texture_bound(
	struct zegl_context *context,
	GLenum target,
	unsigned takes,
	unsigned *face)
{
	struct gles_state *state;
	struct gles_texture *texture;
	unsigned taken;

	/* A context with its state. */
	*face = 0U;
	state = gles_state(context);
	if (state == NULL)
		return NULL;

	/* The unit's texture of the target, and which of the targets it is. */
	texture = NULL;
	taken = 0U;
	if (target == GL_TEXTURE_2D) {
		texture = state->units[state->active_unit];
		taken = TEXTURE_TAKES_2D;
	} else if (target == GL_TEXTURE_CUBE_MAP) {
		texture = state->cube_units[state->active_unit];
		taken = TEXTURE_TAKES_CUBE;
	} else if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
		texture = state->cube_units[state->active_unit];
		*face = target - GL_TEXTURE_CUBE_MAP_POSITIVE_X;
		taken = TEXTURE_TAKES_FACES;
	} else if (target == GL_TEXTURE_3D) {
		texture = state->volume_units[state->active_unit];
		taken = TEXTURE_TAKES_3D;
	} else if (target == GL_TEXTURE_2D_ARRAY) {
		texture = state->array_units[state->active_unit];
		taken = TEXTURE_TAKES_ARRAY;
	}

	/* A target the call does not take. */
	if ((taken & takes) == 0U) {
		*face = 0U;
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
	unsigned takes,
	unsigned *face)
{
	struct gles_texture *texture;
	int status;

	/* The bound texture. */
	texture = texture_bound(context, target, takes, face);
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

	/* GL's initial state: the sampling, every level from 0, each channel its own. */
	texture->name = name;
	texture->target = target;
	texture_sampling_initial(&texture->sampling);
	texture->base_level = 0;
	texture->max_level = TEXTURE_LEVEL_MAX;
	texture->swizzle[0] = GL_RED;
	texture->swizzle[1] = GL_GREEN;
	texture->swizzle[2] = GL_BLUE;
	texture->swizzle[3] = GL_ALPHA;

	/* Succeeded: the texture. */
	return texture;
}

/* Gives a level of a face new texels (the texture takes them over) of a size and slices, kept in a format; the image is stale. */
static void
texture_level_set(
	struct gles_texture *texture,
	unsigned face,
	GLint level,
	int width,
	int height,
	int depth,
	unsigned char *pixels,
	const struct gles_format *format)
{
	struct gles_level *defined;

	/* The old pixels go; the image is stale. */
	defined = &texture->levels[face * GLES_LEVELS + (unsigned)level];
	free(defined->pixels);
	defined->pixels = pixels;
	defined->width = width;
	defined->height = height;
	defined->depth = depth;
	defined->format = format;
	texture->dirty = 1;
}

/* Gives a sampling state GL's initial values. */
static void
texture_sampling_initial(
	struct gles_sampling *sampling)
{
	/* Mipmapped minification, linear magnification, repeating, every level, no comparison. */
	memset(sampling, 0, sizeof(*sampling));
	sampling->min_filter = GL_NEAREST_MIPMAP_LINEAR;
	sampling->mag_filter = GL_LINEAR;
	sampling->wrap_s = GL_REPEAT;
	sampling->wrap_t = GL_REPEAT;
	sampling->wrap_r = GL_REPEAT;
	sampling->min_lod = -TEXTURE_LOD_MAX;
	sampling->max_lod = TEXTURE_LOD_MAX;
	sampling->compare_mode = GL_NONE;
	sampling->compare_func = GL_LEQUAL;
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

/*
 * Converts the application's pixels of a format and type into a level's
 * kept form (OpenGL ES 2's unsized formats by texture_convert, the rest
 * by format.c): one image, or depth slices or layers (depth 0 for a 2D
 * call), found by the pixel store and the pixel unpack buffer.  Returns
 * the texels, or NULL with the error recorded.
 */
static unsigned char *
texture_texels(
	struct zegl_context *context,
	const struct gles_format *storage,
	GLenum format,
	GLenum type,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
	const void *pixels)
{
	struct gles_state *state;
	const void *packed;
	unsigned char *owned;
	unsigned char *converted;
	GLsizei rows;
	GLenum error;

	/* The texels as tight rows, wherever the pixel store and the unpack buffer put them. */
	state = gles_state(context);
	error = gles_unpack(state, format, type, width, height, depth, pixels, &packed, &owned);
	if (error != GL_NO_ERROR) {
		gles_error(context, error);
		return NULL;
	}

	/* Every slice's rows, one after another. */
	rows = height;
	if (depth > 1)
		rows = height * depth;

	/* OpenGL ES 2's formats and types into RGBA8. */
	if (storage->legacy) {
		converted = texture_convert(context, format, type, width, rows, packed);
		free(owned);
		return converted;
	}

	/* Any other format through four channels. */
	converted = NULL;
	error = gles_texels_convert(storage, format, type, width, rows, state->unpack_alignment, packed, &converted);
	free(owned);
	if (error != GL_NO_ERROR) {
		gles_error(context, error);
		return NULL;
	}

	/* Succeeded: the kept texels. */
	return converted;
}

/*
 * Reads a rectangle of the framebuffer (RGBA8) into a level's kept form.
 * Returns the texels, or NULL with the error recorded (an integer or
 * depth format cannot be copied from a colour framebuffer).
 */
static unsigned char *
texture_from_framebuffer(
	struct zegl_context *context,
	const struct gles_format *storage,
	GLint x,
	GLint y,
	GLsizei width,
	GLsizei height)
{
	unsigned char *pixels;
	unsigned char *converted;
	GLenum error;
	int status;

	/* The framebuffer's pixels as RGBA8. */
	pixels = malloc((size_t)width * (size_t)height * 4U);
	if (pixels == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return NULL;
	}

	/* The framebuffer's pixels. */
	status = gles_read_rgba(context, x, y, width, height, pixels);
	if (status != 0) {
		free(pixels);
		return NULL;
	}

	/* RGBA8 kept texels are the pixels. */
	if (storage->vk == VK_FORMAT_R8G8B8A8_UNORM)
		return pixels;

	/* Any other kept form through four channels. */
	converted = NULL;
	error = gles_texels_from_rgba8(storage, pixels, (size_t)width * (size_t)height, &converted);
	free(pixels);
	if (error != GL_NO_ERROR) {
		gles_error(context, error);
		return NULL;
	}

	/* Succeeded: the kept texels. */
	return converted;
}

/* Copies tight rows of kept texels, slice by slice, into a box of a level at an offset (the caller checked the box fits). */
static void
texture_rows_place(
	struct gles_level *destination,
	GLint xoffset,
	GLint yoffset,
	GLint zoffset,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
	const unsigned char *rows)
{
	size_t bytes;
	size_t target;
	size_t source;
	GLsizei slice;
	GLsizei row;

	/* Each row of each slice into place. */
	bytes = destination->format->bytes;
	for (slice = 0; slice < depth; slice++) {
		for (row = 0; row < height; row++) {
			target = (((size_t)(zoffset + slice) * (size_t)destination->height + (size_t)(yoffset + row)) * (size_t)destination->width + (size_t)xoffset) * bytes;
			source = ((size_t)slice * (size_t)height + (size_t)row) * (size_t)width * bytes;
			memcpy(destination->pixels + target, rows + source, (size_t)width * bytes);
		}
	}
}

/* Reports whether a 3D texture (every side up to the most) or a 2D array (its sides and layers up to theirs) may have a level of a size. */
static int
texture_volume_size(
	struct gles_state *state,
	const struct gles_texture *texture,
	GLsizei width,
	GLsizei height,
	GLsizei depth)
{
	GLsizei side;
	GLsizei layers;

	/* No side is negative. */
	if (width < 0 || height < 0 || depth < 0)
		return 0;

	/* A 3D texture's sides: GL's most, and the device's. */
	if (texture->target == GL_TEXTURE_3D) {
		side = GLES_MAX_3D_SIZE;
		if ((GLsizei)state->limits.maxImageDimension3D < side)
			side = (GLsizei)state->limits.maxImageDimension3D;
		if (width > side || height > side || depth > side)
			return 0;
		return 1;
	}

	/* A 2D array's sides are a 2D texture's, its layers up to the most. */
	side = 16384;
	if ((GLsizei)state->limits.maxImageDimension2D < side)
		side = (GLsizei)state->limits.maxImageDimension2D;
	layers = GLES_MAX_LAYERS;
	if ((GLsizei)state->limits.maxImageArrayLayers < layers)
		layers = (GLsizei)state->limits.maxImageArrayLayers;
	if (width > side || height > side || depth > layers)
		return 0;

	/* Succeeded: the size fits. */
	return 1;
}

/*
 * Sets one parameter of a texture: its base and most levels and swizzle,
 * or its sampling state.  Returns 0, or nonzero with the error recorded
 * when the parameter or its value is not one.
 */
static int
texture_parameter(
	struct zegl_context *context,
	struct gles_texture *texture,
	GLenum pname,
	GLint value,
	GLfloat number)
{
	GLenum mode;
	int status;

	/* The value as an enum. */
	mode = (GLenum)value;

	/* The texture's own parameters change which levels the image has, or its view: what the device drew is read back first. */
	switch (pname) {
	case GL_TEXTURE_BASE_LEVEL:
	case GL_TEXTURE_MAX_LEVEL:
		if (value < 0) {
			gles_error(context, GL_INVALID_VALUE);
			return -1;
		}

		/* The levels. */
		status = gles_texture_fetch(context, texture);
		if (status != 0)
			return -1;
		if (pname == GL_TEXTURE_BASE_LEVEL)
			texture->base_level = value;
		if (pname == GL_TEXTURE_MAX_LEVEL)
			texture->max_level = value;
		if (texture->base_level >= (GLint)GLES_LEVELS)
			texture->base_level = (GLint)GLES_LEVELS - 1;
		texture->dirty = 1;
		return 0;
	case GL_TEXTURE_SWIZZLE_R:
	case GL_TEXTURE_SWIZZLE_G:
	case GL_TEXTURE_SWIZZLE_B:
	case GL_TEXTURE_SWIZZLE_A:
		if (mode != GL_RED &&
		    mode != GL_GREEN &&
		    mode != GL_BLUE &&
		    mode != GL_ALPHA &&
		    mode != GL_ZERO &&
		    mode != GL_ONE) {
			gles_error(context, GL_INVALID_ENUM);
			return -1;
		}

		/* The channel's source, in a new view. */
		status = gles_texture_fetch(context, texture);
		if (status != 0)
			return -1;
		texture->swizzle[pname - GL_TEXTURE_SWIZZLE_R] = mode;
		texture->dirty = 1;
		return 0;
	default:
		break;
	}

	/* The sampling state. */
	status = texture_sampling_parameter(context, &texture->sampling, pname, value, number);
	if (status > 0) {
		gles_error(context, GL_INVALID_ENUM);
		return -1;
	}

	/* Reports a value the parameter does not take. */
	if (status < 0)
		return -1;

	/* Succeeded: the parameter is set. */
	return 0;
}

/*
 * Sets one parameter of a sampling state.  Returns 0; -1 with the error
 * recorded for a value the parameter does not take; 1 (nothing recorded)
 * when the name is not a sampling parameter.
 */
static int
texture_sampling_parameter(
	struct zegl_context *context,
	struct gles_sampling *sampling,
	GLenum pname,
	GLint value,
	GLfloat number)
{
	GLenum mode;
	int mipmapped;

	/* The value as an enum. */
	mode = (GLenum)value;

	/* The parameter. */
	switch (pname) {
	case GL_TEXTURE_MIN_FILTER:
		mipmapped = texture_mipmapped(mode);
		if (mode != GL_NEAREST && mode != GL_LINEAR && !mipmapped)
			break;
		sampling->min_filter = mode;
		return 0;
	case GL_TEXTURE_MAG_FILTER:
		if (mode != GL_NEAREST && mode != GL_LINEAR)
			break;
		sampling->mag_filter = mode;
		return 0;
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:
	case GL_TEXTURE_WRAP_R:
		if (mode != GL_REPEAT && mode != GL_CLAMP_TO_EDGE && mode != GL_MIRRORED_REPEAT)
			break;
		if (pname == GL_TEXTURE_WRAP_S)
			sampling->wrap_s = mode;
		if (pname == GL_TEXTURE_WRAP_T)
			sampling->wrap_t = mode;
		if (pname == GL_TEXTURE_WRAP_R)
			sampling->wrap_r = mode;
		return 0;
	case GL_TEXTURE_MIN_LOD:
		sampling->min_lod = number;
		return 0;
	case GL_TEXTURE_MAX_LOD:
		sampling->max_lod = number;
		return 0;
	case GL_TEXTURE_COMPARE_MODE:
		if (mode != GL_NONE && mode != GL_COMPARE_REF_TO_TEXTURE)
			break;
		sampling->compare_mode = mode;
		return 0;
	case GL_TEXTURE_COMPARE_FUNC:
		if (mode < GL_NEVER || mode > GL_ALWAYS)
			break;
		sampling->compare_func = mode;
		return 0;
	default:
		return 1;
	}

	/* A value the parameter does not take. */
	gles_error(context, GL_INVALID_ENUM);
	return -1;
}

/* Reads one parameter of a texture as a float; nonzero when the name is not one. */
static int
texture_get(
	struct gles_texture *texture,
	GLenum pname,
	GLfloat *value)
{
	int status;

	/* The texture's own parameters. */
	switch (pname) {
	case GL_TEXTURE_BASE_LEVEL:
		*value = (GLfloat)texture->base_level;
		return 0;
	case GL_TEXTURE_MAX_LEVEL:
		*value = (GLfloat)texture->max_level;
		return 0;
	case GL_TEXTURE_SWIZZLE_R:
	case GL_TEXTURE_SWIZZLE_G:
	case GL_TEXTURE_SWIZZLE_B:
	case GL_TEXTURE_SWIZZLE_A:
		*value = (GLfloat)texture->swizzle[pname - GL_TEXTURE_SWIZZLE_R];
		return 0;
	case GL_TEXTURE_IMMUTABLE_FORMAT:
		*value = (GLfloat)texture->immutable;
		return 0;
	case GL_TEXTURE_IMMUTABLE_LEVELS:
		*value = (GLfloat)texture->immutable_levels;
		return 0;
	default:
		break;
	}

	/* The sampling state. */
	status = texture_sampling_get(&texture->sampling, pname, value);
	if (status != 0)
		return -1;

	/* Succeeded: the value. */
	return 0;
}

/* Reads one parameter of a sampling state as a float; nonzero when the name is not one. */
static int
texture_sampling_get(
	const struct gles_sampling *sampling,
	GLenum pname,
	GLfloat *value)
{
	/* The parameter. */
	switch (pname) {
	case GL_TEXTURE_MIN_FILTER:
		*value = (GLfloat)sampling->min_filter;
		return 0;
	case GL_TEXTURE_MAG_FILTER:
		*value = (GLfloat)sampling->mag_filter;
		return 0;
	case GL_TEXTURE_WRAP_S:
		*value = (GLfloat)sampling->wrap_s;
		return 0;
	case GL_TEXTURE_WRAP_T:
		*value = (GLfloat)sampling->wrap_t;
		return 0;
	case GL_TEXTURE_WRAP_R:
		*value = (GLfloat)sampling->wrap_r;
		return 0;
	case GL_TEXTURE_MIN_LOD:
		*value = sampling->min_lod;
		return 0;
	case GL_TEXTURE_MAX_LOD:
		*value = sampling->max_lod;
		return 0;
	case GL_TEXTURE_COMPARE_MODE:
		*value = (GLfloat)sampling->compare_mode;
		return 0;
	case GL_TEXTURE_COMPARE_FUNC:
		*value = (GLfloat)sampling->compare_func;
		return 0;
	default:
		break;
	}

	/* Not a parameter. */
	return -1;
}

/* Returns the sampler object of a name, recording GL_INVALID_OPERATION when the name is not one. */
static struct gles_sampler_object *
texture_sampler_object(
	struct zegl_context *context,
	GLuint name)
{
	struct gles_state *state;
	struct gles_sampler_object *sampler;

	/* A context with its state. */
	state = gles_state(context);
	if (state == NULL)
		return NULL;

	/* The name's object. */
	sampler = gles_names_get(&state->sampler_objects, name);
	if (sampler == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the sampler object. */
	return sampler;
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

/* Reports whether a sampling state reads nearest texels only (a format that cannot be filtered may be read so). */
static int
texture_nearest(
	const struct gles_sampling *sampling)
{
	/* The magnification filter. */
	if (sampling->mag_filter != GL_NEAREST)
		return 0;

	/* The minification filter, which may choose the nearest level too. */
	if (sampling->min_filter != GL_NEAREST && sampling->min_filter != GL_NEAREST_MIPMAP_NEAREST)
		return 0;

	/* Nearest only. */
	return 1;
}

/*
 * Returns how many levels a texture's image has: from the base level, the
 * chain of halving sizes (on every face, of the base level's format, up
 * to the most level; a 3D texture's depth halves too, a 2D array's layers
 * stay as many).  The image has the chain whatever the filter, so a
 * framebuffer object can draw into any of its levels; a sampler without
 * mipmaps reads the first only.
 */
static uint32_t
texture_levels(
	struct gles_texture *texture)
{
	const struct gles_level *level;
	const struct gles_level *base;
	uint32_t levels;
	uint32_t limit;
	uint32_t faces;
	uint32_t face;
	int width;
	int height;
	int depth;
	int halve_depth;
	int whole;

	/* No more than the levels from the base to the most one. */
	limit = GLES_LEVELS - (uint32_t)texture->base_level;
	if (texture->max_level >= texture->base_level && (uint32_t)(texture->max_level - texture->base_level) + 1U < limit)
		limit = (uint32_t)(texture->max_level - texture->base_level) + 1U;

	/* Each next level whose size is the halved one on every face, and whose format is the base level's. */
	faces = texture_faces(texture);
	base = &texture->levels[texture->base_level];
	width = base->width;
	height = base->height;
	depth = base->depth;
	halve_depth = 0;
	if (texture->target == GL_TEXTURE_3D)
		halve_depth = 1;
	for (levels = 1U; levels < limit; levels++) {
		if (width == 1 && height == 1 && (!halve_depth || depth == 1))
			break;
		width = width / 2;
		if (width < 1)
			width = 1;
		height = height / 2;
		if (height < 1)
			height = 1;
		if (halve_depth) {
			depth = depth / 2;
			if (depth < 1)
				depth = 1;
		}

		/* The level of every face, of that size and format. */
		whole = 1;
		for (face = 0U; face < faces; face++) {
			level = &texture->levels[face * GLES_LEVELS + (uint32_t)texture->base_level + levels];
			if (level->width != width ||
			    level->height != height ||
			    level->depth != depth ||
			    level->pixels == NULL ||
			    level->format != base->format)
				whole = 0;
		}

		/* The chain ends where a face lacks the level. */
		if (!whole)
			break;
	}

	/* Succeeded: the chain found. */
	return levels;
}

/* Returns how many faces a texture has: six for a cube map, one for the other shapes. */
static uint32_t
texture_faces(
	const struct gles_texture *texture)
{
	/* A cube map. */
	if (texture->target == GL_TEXTURE_CUBE_MAP)
		return GLES_FACES;

	/* Succeeded: one face. */
	return 1U;
}

/* Returns a texture's shape (GLES_SHAPE_*) by its target (a texture not bound yet is 2D). */
static unsigned
texture_shape(
	const struct gles_texture *texture)
{
	/* The target it was bound to. */
	switch (texture->target) {
	case GL_TEXTURE_CUBE_MAP:
		return GLES_SHAPE_CUBE;
	case GL_TEXTURE_3D:
		return GLES_SHAPE_3D;
	case GL_TEXTURE_2D_ARRAY:
		return GLES_SHAPE_ARRAY;
	default:
		break;
	}

	/* Succeeded: a 2D texture. */
	return GLES_SHAPE_2D;
}

/* Makes every level of an RGBA8 face below its base level by halving it with a box filter, until 1x1 or the last level; nonzero when there is no memory. */
static int
texture_mipmaps(
	struct gles_texture *texture,
	unsigned face,
	GLint last)
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
	for (level = texture->base_level + 1; level <= last; level++) {
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
		gles_texture_define(texture, face, level, width, height, pixels, source->format);
	}

	/* Succeeded: the chain. */
	return 0;
}

/*
 * Makes every level of a face of any normalized or float format below its
 * base level, through floats (format.c): a 3D texture's slices halve too,
 * a 2D array's layers each on their own.  Nonzero when there is no memory.
 */
static int
texture_mipmaps_kept(
	struct gles_texture *texture,
	unsigned face,
	GLint last)
{
	const struct gles_level *source;
	unsigned char *pixels;
	int width;
	int height;
	int depth;
	int halve_depth;
	int status;
	GLint level;

	/* A 3D texture's depth halves with its sides. */
	halve_depth = 0;
	if (texture->target == GL_TEXTURE_3D)
		halve_depth = 1;

	/* Each level from the one above it, until 1x1 (and one slice in 3D). */
	for (level = texture->base_level + 1; level <= last; level++) {
		source = &texture->levels[face * GLES_LEVELS + (unsigned)level - 1U];
		if (source->width == 1 && source->height == 1 && (!halve_depth || source->depth == 1))
			break;

		/* The halved level. */
		status = gles_texels_halve(source->format, source->pixels, source->width, source->height, source->depth, halve_depth,
					   &pixels, &width, &height, &depth);
		if (status != 0)
			return -1;

		/* The level. */
		texture_level_set(texture, face, level, width, height, depth, pixels, source->format);
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
	/* The three wraps of OpenGL ES. */
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

/* Returns Vulkan's component swizzle for a GL swizzle source, the identity one when it is the channel's own. */
static VkComponentSwizzle
texture_swizzle(
	GLenum source,
	VkComponentSwizzle identity)
{
	VkComponentSwizzle swizzle;

	/* The source channel, or a constant. */
	switch (source) {
	case GL_RED:
		swizzle = VK_COMPONENT_SWIZZLE_R;
		break;
	case GL_GREEN:
		swizzle = VK_COMPONENT_SWIZZLE_G;
		break;
	case GL_BLUE:
		swizzle = VK_COMPONENT_SWIZZLE_B;
		break;
	case GL_ALPHA:
		swizzle = VK_COMPONENT_SWIZZLE_A;
		break;
	case GL_ZERO:
		return VK_COMPONENT_SWIZZLE_ZERO;
	default:
		return VK_COMPONENT_SWIZZLE_ONE;
	}

	/* A channel's own source is the identity. */
	if (swizzle == identity)
		return VK_COMPONENT_SWIZZLE_IDENTITY;

	/* Another channel. */
	return swizzle;
}

/* Lets the views of a texture's image framebuffer objects drew into go (they wait for the frame), leaving none. */
static void
texture_views_free(
	struct gles_state *state,
	struct gles_texture *texture)
{
	struct gles_attach_view *entry;

	/* Each view, then its entry. */
	while (texture->attach_views != NULL) {
		entry = texture->attach_views;
		texture->attach_views = entry->next;
		gles_throw_away(state, VK_NULL_HANDLE, VK_NULL_HANDLE, entry->view, VK_NULL_HANDLE);
		free(entry);
	}
}

/*
 * Makes a view read alpha as 1 where it would read the kept alpha of a
 * format GL has none for (an RGB format kept in four channels, which a
 * framebuffer object may have drawn alpha into).
 */
static void
texture_opaque(
	const struct gles_format *format,
	VkComponentMapping *components)
{
	VkComponentSwizzle *channels[4];
	unsigned index;

	/* Only the formats with three channels of GL's kept in four. */
	if (format->base != GL_RGB && format->base != GL_RGB_INTEGER)
		return;
	if (format->components != 4U)
		return;

	/* Each channel that reads alpha (its own, the identity, for the fourth) reads 1. */
	channels[0] = &components->r;
	channels[1] = &components->g;
	channels[2] = &components->b;
	channels[3] = &components->a;
	for (index = 0U; index < 4U; index++) {
		if (*channels[index] == VK_COMPONENT_SWIZZLE_A)
			*channels[index] = VK_COMPONENT_SWIZZLE_ONE;
		if (index == 3U && *channels[index] == VK_COMPONENT_SWIZZLE_IDENTITY)
			*channels[index] = VK_COMPONENT_SWIZZLE_ONE;
	}
}
