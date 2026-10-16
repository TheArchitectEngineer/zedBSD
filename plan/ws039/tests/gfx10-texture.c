/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: the texture unit of the host tests (gfx10-texture.h).
 *
 * The descriptor fields: image word 0 the address >> 8, word 1 its bits
 * 47:40 in 7:0, the format in 28:20 and the width less one's low two bits
 * in 31:30; word 2 the rest of the width in 11:0, the height less one in
 * 27:14 and the resource level in 31; word 3 the channel selects, the
 * base and last levels in 15:12 and 19:16 and the type (9 2D, 10 3D, 11
 * cube, 13 an array of 2D images) in 31:28; word 4 the depth less one in
 * 12:0 (a 3D image's slices, an array's layers, an array's cubes).  Sampler word 0 the clamp of x and y
 * in 2:0 and 5:3 (0 wraps) and the depth compare function in 14:12
 * (never, less, equal, less or equal, greater, not equal, greater or
 * equal, always: the reference against the texel) and the unnormalized
 * coordinates in 15, word 2 the magnifying
 * and minifying filters in 21:20 and 23:22 (1 bilinear).  (Mesa 25.0.7
 * src/amd/registers/gfx10-rsrc.json, SQ_IMG_RSRC_WORD0..4,
 * SQ_IMG_SAMP_WORD0, SQ_IMG_SAMP_WORD2; src/amd/registers/gfx9.json, the
 * enums SQ_TEX_DEPTH_COMPARE and SQ_TEX_XY_FILTER.)
 */

#include "plan/ws039/tests/gfx10-texture.h"

#include <math.h>
#include <string.h>

static void swizzle(const uint32_t *image, float *result);
static void filter(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, const struct texture_request *request, float *result);
static void gather(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, const struct texture_request *request, float *result);
static void texel(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, int32_t x, int32_t y, int32_t z, const struct texture_request *request, float *out);
static int32_t slice_of(const uint32_t *image, const struct texture_request *request);
static float level_of_detail(const uint32_t *image, const struct texture_request *request);
static int32_t place(int32_t coordinate, uint32_t size, int clamp);
static uint32_t image_width(const uint32_t *image);
static uint32_t image_height(const uint32_t *image);
static uint32_t image_depth(const uint32_t *image);
static uint32_t image_type(const uint32_t *image);
static uint32_t image_format(const uint32_t *image);
static float as_float(uint32_t bits);

void
texture_descriptors(
	uint32_t *image,
	uint32_t *sampler,
	uint64_t address,
	uint32_t width,
	uint32_t height,
	uint32_t format,
	int filters,
	int clamp,
	uint32_t compare)
{
	memset(image, 0, 32U);
	memset(sampler, 0, 16U);
	image[0] = (uint32_t)(address >> 8);
	image[1] = (uint32_t)(address >> 40) & 0xffU;
	image[1] |= (format & 0x1ffU) << 20;
	image[1] |= ((width - 1U) & 3U) << 30;
	image[2] = ((width - 1U) >> 2) & 0xfffU;
	image[2] |= ((height - 1U) & 0x3fffU) << 14;
	image[2] |= 1U << 31;
	image[3] = 4U | (5U << 3) | (6U << 6) | (7U << 9) | (TEXTURE_TYPE_2D << 28);
	sampler[0] = clamp ? (2U | (2U << 3) | (2U << 6)) : 0U;
	sampler[0] |= (compare & 7U) << 12;
	if ((filters & TEXTURE_MAG_BILINEAR) != 0)
		sampler[2] |= 1U << 20;
	if ((filters & TEXTURE_MIN_BILINEAR) != 0)
		sampler[2] |= 1U << 22;
}

void
texture_shape(
	uint32_t *image,
	uint32_t type,
	uint32_t depth)
{
	image[3] = (image[3] & 0x0fffffffU) | ((type & 0xfU) << 28);
	image[4] = (image[4] & ~0x1fffU) | ((depth - 1U) & 0x1fffU);
}

uint32_t
texture_pitch(
	uint32_t width,
	uint32_t format)
{
	uint32_t bytes;

	bytes = format == TEXTURE_FORMAT_FLOAT4 ? 16U : 4U;
	return (width * bytes + 255U) & ~255U;
}

uint64_t
texture_address(
	const uint32_t *image)
{
	return ((uint64_t)image[0] << 8) | ((uint64_t)(image[1] & 0xffU) << 40);
}

void
texture_sample(
	const uint8_t *texels,
	const uint32_t *image,
	const uint32_t *sampler,
	const struct texture_request *request,
	uint32_t *out)
{
	float result[4];
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t levels;

	/* A size query: the level's size (at least 1; a 3D image's depth too), the layers or cubes of an array, the levels the descriptor has. */
	if (request->size_query != 0) {
		width = image_width(image) >> (request->level & 31U);
		height = image_height(image) >> (request->level & 31U);
		depth = image_depth(image);
		if (image_type(image) == TEXTURE_TYPE_3D)
			depth >>= (request->level & 31U);
		levels = ((image[3] >> 16) & 0xfU) - ((image[3] >> 12) & 0xfU) + 1U;
		out[0] = width != 0U ? width : 1U;
		out[1] = height != 0U ? height : 1U;
		out[2] = depth != 0U ? depth : 1U;
		out[3] = levels;
		return;
	}

	/* A fetch reads the integer texel (a 3D image's at the integer r); outside the image it reads zero, whatever the sampler. */
	if (request->fetch != 0) {
		if ((int32_t)request->u < 0 || (int32_t)request->u >= (int32_t)image_width(image) ||
		    (int32_t)request->v < 0 || (int32_t)request->v >= (int32_t)image_height(image) ||
		    (image_type(image) == TEXTURE_TYPE_3D &&
		     ((int32_t)request->r < 0 || (int32_t)request->r >= (int32_t)image_depth(image)))) {
			memset(out, 0, 16U);
			return;
		}
		texel(texels, image, sampler, (int32_t)request->u, (int32_t)request->v,
		      image_type(image) == TEXTURE_TYPE_3D ? (int32_t)request->r : 0, request, result);
		swizzle(image, result);
		memcpy(out, result, 16U);
		return;
	}

	/* A gather reads four texels' channel. */
	if (request->gather != 0) {
		gather(texels, image, sampler, request, result);
		memcpy(out, result, 16U);
		return;
	}

	/* A sample filters. */
	filter(texels, image, sampler, request, result);
	if (request->compare == 0)
		swizzle(image, result);
	memcpy(out, result, 16U);
}

/* The descriptor's channel selects (word 3, 3 bits each): 0 and 1 the constants, 4 .. 7 the texel's channels. */
static void
swizzle(
	const uint32_t *image,
	float *result)
{
	float texel_channels[4];
	uint32_t channel;
	uint32_t select;

	memcpy(texel_channels, result, sizeof(texel_channels));
	for (channel = 0U; channel < 4U; channel++) {
		select = (image[3] >> (3U * channel)) & 7U;
		if (select >= 4U)
			result[channel] = texel_channels[select - 4U];
		else
			result[channel] = select == 1U ? 1.0f : 0.0f;
	}
}

/* Filters at (u, v, r) with the request's offset, by the filter its level of detail picks. */
static void
filter(
	const uint8_t *texels,
	const uint32_t *image,
	const uint32_t *sampler,
	const struct texture_request *request,
	float *result)
{
	float u;
	float v;
	float r;
	float x;
	float y;
	float z;
	float fx;
	float fy;
	float fz;
	float corner[2][2][2][4];
	int32_t x0;
	int32_t y0;
	int32_t z0;
	int32_t slice;
	uint32_t channel;
	uint32_t mode;
	uint32_t dz;
	uint32_t slices;
	int volume;
	float lod;

	/* Coordinates in texels, normalized or given so; a 3D image's r too, any other image's slice by its layer. */
	volume = image_type(image) == TEXTURE_TYPE_3D;
	u = as_float(request->u);
	v = as_float(request->v);
	r = as_float(request->r);
	if ((sampler[0] & (1U << 15)) == 0U) {
		u *= (float)image_width(image);
		v *= (float)image_height(image);
		r *= (float)image_depth(image);
	}
	slice = slice_of(image, request);
	lod = level_of_detail(image, request);
	mode = (sampler[2] >> 20) & 3U;
	if (lod > 0.0f)
		mode = (sampler[2] >> 22) & 3U;

	/* The nearest texel. */
	if (mode != 1U) {
		x = floorf(u);
		y = floorf(v);
		z = floorf(r);
		if (volume != 0)
			slice = (int32_t)z + request->offset_r;
		texel(texels, image, sampler, (int32_t)x + request->offset_u, (int32_t)y + request->offset_v, slice, request, result);
		return;
	}

	/* The four (a 3D image: eight) texels around the sample point, weighted. */
	x = u - 0.5f;
	y = v - 0.5f;
	z = r - 0.5f;
	x0 = (int32_t)floorf(x);
	y0 = (int32_t)floorf(y);
	z0 = (int32_t)floorf(z);
	fx = x - (float)x0;
	fy = y - (float)y0;
	fz = z - (float)z0;
	slices = 1U;
	if (volume != 0)
		slices = 2U;
	for (dz = 0U; dz < slices; dz++) {
		if (volume != 0)
			slice = z0 + (int32_t)dz + request->offset_r;
		texel(texels, image, sampler, x0 + request->offset_u, y0 + request->offset_v, slice, request, corner[dz][0][0]);
		texel(texels, image, sampler, x0 + 1 + request->offset_u, y0 + request->offset_v, slice, request, corner[dz][0][1]);
		texel(texels, image, sampler, x0 + request->offset_u, y0 + 1 + request->offset_v, slice, request, corner[dz][1][0]);
		texel(texels, image, sampler, x0 + 1 + request->offset_u, y0 + 1 + request->offset_v, slice, request, corner[dz][1][1]);
	}
	for (channel = 0U; channel < 4U; channel++) {
		result[channel] = (corner[0][0][0][channel] * (1.0f - fx) + corner[0][0][1][channel] * fx) * (1.0f - fy) +
				  (corner[0][1][0][channel] * (1.0f - fx) + corner[0][1][1][channel] * fx) * fy;
		if (volume != 0) {
			result[channel] = result[channel] * (1.0f - fz) +
					  ((corner[1][0][0][channel] * (1.0f - fx) + corner[1][0][1][channel] * fx) * (1.0f - fy) +
					   (corner[1][1][0][channel] * (1.0f - fx) + corner[1][1][1][channel] * fx) * fy) * fz;
		}
	}
}

/*
 * Gathers one channel (after the selects), or the compare, of the four
 * texels a bilinear filter at (u, v) weighs: (i0, j1), (i1, j1), (i1, j0),
 * (i0, j0).
 */
static void
gather(
	const uint8_t *texels,
	const uint32_t *image,
	const uint32_t *sampler,
	const struct texture_request *request,
	float *result)
{
	static const int32_t step_x[4] = { 0, 1, 1, 0 };
	static const int32_t step_y[4] = { 1, 1, 0, 0 };
	float u;
	float v;
	float one[4];
	int32_t x0;
	int32_t y0;
	int32_t slice;
	uint32_t index;

	u = as_float(request->u);
	v = as_float(request->v);
	if ((sampler[0] & (1U << 15)) == 0U) {
		u *= (float)image_width(image);
		v *= (float)image_height(image);
	}
	x0 = (int32_t)floorf(u - 0.5f) + request->offset_u;
	y0 = (int32_t)floorf(v - 0.5f) + request->offset_v;
	slice = slice_of(image, request);
	for (index = 0U; index < 4U; index++) {
		texel(texels, image, sampler, x0 + step_x[index], y0 + step_y[index], slice, request, one);
		if (request->compare != 0) {
			result[index] = one[0];
		} else {
			swizzle(image, one);
			result[index] = one[request->channel & 3U];
		}
	}
}

/* The slice an array's layer or a cube's face picks (the layer clamped to the array); 0 for a 2D image. */
static int32_t
slice_of(
	const uint32_t *image,
	const struct texture_request *request)
{
	int32_t layer;
	int32_t last;

	layer = request->layer;
	last = (int32_t)image_depth(image) - 1;
	if (layer < 0)
		layer = 0;
	if (layer > last)
		layer = last;
	if (image_type(image) == TEXTURE_TYPE_2D_ARRAY)
		return layer;
	if (image_type(image) == TEXTURE_TYPE_CUBE)
		return layer * 6 + request->face;
	return 0;
}

/* The level of detail of a request: its explicit level or bias, or the one its gradients give, or 0. */
static float
level_of_detail(
	const uint32_t *image,
	const struct texture_request *request)
{
	float size[3];
	float along_x;
	float along_y;
	uint32_t count;
	uint32_t index;
	float component;

	if (request->has_lod != 0)
		return as_float(request->lod);
	if (request->has_gradients == 0)
		return 0.0f;
	size[0] = (float)image_width(image);
	size[1] = (float)image_height(image);
	size[2] = (float)image_depth(image);
	count = image_type(image) == TEXTURE_TYPE_3D ? 3U : 2U;
	along_x = 0.0f;
	along_y = 0.0f;
	for (index = 0U; index < count; index++) {
		component = as_float(request->gradients[index]) * size[index];
		along_x += component * component;
		component = as_float(request->gradients[count + index]) * size[index];
		along_y += component * component;
	}
	along_x = sqrtf(along_x);
	along_y = sqrtf(along_y);
	return log2f(along_x > along_y ? along_x : along_y);
}

/* One texel's four channels as floats (a compare: its result in the first), its coordinates wrapped or clamped. */
static void
texel(
	const uint8_t *texels,
	const uint32_t *image,
	const uint32_t *sampler,
	int32_t x,
	int32_t y,
	int32_t z,
	const struct texture_request *request,
	float *out)
{
	const uint8_t *at;
	uint32_t format;
	uint32_t channel;
	uint32_t pitch;
	float reference;
	float depth;
	int pass;
	int clamp;

	format = image_format(image);
	clamp = (sampler[0] & 7U) != 0U;
	if (image_type(image) == TEXTURE_TYPE_CUBE)
		clamp = 1;
	x = place(x, image_width(image), clamp);
	y = place(y, image_height(image), clamp);
	if (image_type(image) == TEXTURE_TYPE_3D)
		z = place(z, image_depth(image), clamp);
	pitch = texture_pitch(image_width(image), format);
	at = texels + ((uint32_t)z * image_height(image) + (uint32_t)y) * pitch;
	if (format == TEXTURE_FORMAT_FLOAT4) {
		memcpy(out, at + (uint32_t)x * 16U, 16U);
	} else {
		for (channel = 0U; channel < 4U; channel++)
			out[channel] = (float)at[(uint32_t)x * 4U + channel] / 255.0f;
	}
	if (request->compare == 0)
		return;

	/* The reference against the first channel, by the sampler's function. */
	reference = as_float(request->reference);
	depth = out[0];
	switch ((sampler[0] >> 12) & 7U) {
	case 1U: pass = reference < depth; break;
	case 2U: pass = reference == depth; break;
	case 3U: pass = reference <= depth; break;
	case 4U: pass = reference > depth; break;
	case 5U: pass = reference != depth; break;
	case 6U: pass = reference >= depth; break;
	case 7U: pass = 1; break;
	default: pass = 0; break;
	}
	out[0] = pass ? 1.0f : 0.0f;
	out[1] = 0.0f;
	out[2] = 0.0f;
	out[3] = 0.0f;
}

/* A coordinate wrapped around, or clamped into, the size. */
static int32_t
place(
	int32_t coordinate,
	uint32_t size,
	int clamp)
{
	int32_t limit;

	limit = (int32_t)size;
	if (clamp != 0) {
		if (coordinate < 0)
			return 0;
		if (coordinate >= limit)
			return limit - 1;
		return coordinate;
	}
	coordinate %= limit;
	if (coordinate < 0)
		coordinate += limit;
	return coordinate;
}

static uint32_t
image_width(
	const uint32_t *image)
{
	return (((image[1] >> 30) & 3U) | ((image[2] & 0xfffU) << 2)) + 1U;
}

static uint32_t
image_height(
	const uint32_t *image)
{
	return ((image[2] >> 14) & 0x3fffU) + 1U;
}

static uint32_t
image_depth(
	const uint32_t *image)
{
	return (image[4] & 0x1fffU) + 1U;
}

static uint32_t
image_type(
	const uint32_t *image)
{
	return (image[3] >> 28) & 0xfU;
}

static uint32_t
image_format(
	const uint32_t *image)
{
	return (image[1] >> 20) & 0x1ffU;
}

static float
as_float(
	uint32_t bits)
{
	float value;

	memcpy(&value, &bits, 4U);
	return value;
}
