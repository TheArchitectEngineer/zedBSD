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
 * base and last levels in 15:12 and 19:16 and the type (9, 2D) in 31:28;
 * word 4 the depth less one in 12:0.  Sampler word 0 the clamp of x and y
 * in 2:0 and 5:3 (0 wraps) and the depth compare function in 14:12
 * (never, less, equal, less or equal, greater, not equal, greater or
 * equal, always: the reference against the texel), word 2 the magnifying
 * and minifying filters in 21:20 and 23:22 (1 bilinear).  (Mesa 25.0.7
 * src/amd/registers/gfx10-rsrc.json, SQ_IMG_RSRC_WORD0..4,
 * SQ_IMG_SAMP_WORD0, SQ_IMG_SAMP_WORD2; src/amd/registers/gfx9.json, the
 * enums SQ_TEX_DEPTH_COMPARE and SQ_TEX_XY_FILTER.)
 */

#include "plan/ws039/tests/gfx10-texture.h"

#include <math.h>
#include <string.h>

static void filter(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, const struct texture_request *request, float *result);
static void texel(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, int32_t x, int32_t y, const struct texture_request *request, float *out);
static float level_of_detail(const uint32_t *image, const struct texture_request *request);
static int32_t place(int32_t coordinate, uint32_t size, int clamp);
static uint32_t image_width(const uint32_t *image);
static uint32_t image_height(const uint32_t *image);
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
	image[3] = 4U | (5U << 3) | (6U << 6) | (7U << 9) | (9U << 28);
	sampler[0] = clamp ? (2U | (2U << 3)) : 0U;
	sampler[0] |= (compare & 7U) << 12;
	if ((filters & TEXTURE_MAG_BILINEAR) != 0)
		sampler[2] |= 1U << 20;
	if ((filters & TEXTURE_MIN_BILINEAR) != 0)
		sampler[2] |= 1U << 22;
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
	uint32_t levels;

	/* A size query: the level's size (at least 1), a depth of one, the levels the descriptor has. */
	if (request->size_query != 0) {
		width = image_width(image) >> (request->level & 31U);
		height = image_height(image) >> (request->level & 31U);
		levels = ((image[3] >> 16) & 0xfU) - ((image[3] >> 12) & 0xfU) + 1U;
		out[0] = width != 0U ? width : 1U;
		out[1] = height != 0U ? height : 1U;
		out[2] = (image[4] & 0x1fffU) + 1U;
		out[3] = levels;
		return;
	}

	/* A fetch reads the integer texel. */
	if (request->fetch != 0) {
		texel(texels, image, sampler, (int32_t)request->u, (int32_t)request->v, request, result);
		memcpy(out, result, 16U);
		return;
	}

	/* A sample filters. */
	filter(texels, image, sampler, request, result);
	memcpy(out, result, 16U);
}

/* Filters at (u, v) with the request's offset, by the filter its level of detail picks. */
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
	float x;
	float y;
	float fx;
	float fy;
	float t00[4];
	float t10[4];
	float t01[4];
	float t11[4];
	int32_t x0;
	int32_t y0;
	uint32_t channel;
	uint32_t mode;
	float lod;

	u = as_float(request->u);
	v = as_float(request->v);
	lod = level_of_detail(image, request);
	mode = (sampler[2] >> 20) & 3U;
	if (lod > 0.0f)
		mode = (sampler[2] >> 22) & 3U;

	/* The nearest texel. */
	if (mode != 1U) {
		x = floorf(u * (float)image_width(image));
		y = floorf(v * (float)image_height(image));
		texel(texels, image, sampler, (int32_t)x + request->offset_u, (int32_t)y + request->offset_v, request, result);
		return;
	}

	/* The four texels around the sample point, weighted. */
	x = u * (float)image_width(image) - 0.5f;
	y = v * (float)image_height(image) - 0.5f;
	x0 = (int32_t)floorf(x);
	y0 = (int32_t)floorf(y);
	fx = x - (float)x0;
	fy = y - (float)y0;
	texel(texels, image, sampler, x0 + request->offset_u, y0 + request->offset_v, request, t00);
	texel(texels, image, sampler, x0 + 1 + request->offset_u, y0 + request->offset_v, request, t10);
	texel(texels, image, sampler, x0 + request->offset_u, y0 + 1 + request->offset_v, request, t01);
	texel(texels, image, sampler, x0 + 1 + request->offset_u, y0 + 1 + request->offset_v, request, t11);
	for (channel = 0U; channel < 4U; channel++) {
		result[channel] = (t00[channel] * (1.0f - fx) + t10[channel] * fx) * (1.0f - fy) +
				  (t01[channel] * (1.0f - fx) + t11[channel] * fx) * fy;
	}
}

/* The level of detail of a request: its explicit level or bias, or the one its gradients give, or 0. */
static float
level_of_detail(
	const uint32_t *image,
	const struct texture_request *request)
{
	float width;
	float height;
	float along_x;
	float along_y;

	if (request->has_lod != 0)
		return as_float(request->lod);
	if (request->has_gradients == 0)
		return 0.0f;
	width = (float)image_width(image);
	height = (float)image_height(image);
	along_x = hypotf(as_float(request->gradients[0]) * width, as_float(request->gradients[1]) * height);
	along_y = hypotf(as_float(request->gradients[2]) * width, as_float(request->gradients[3]) * height);
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
	const struct texture_request *request,
	float *out)
{
	const uint8_t *at;
	uint32_t format;
	uint32_t channel;
	float reference;
	float depth;
	int pass;
	int clamp;

	format = image_format(image);
	clamp = (sampler[0] & 7U) != 0U;
	x = place(x, image_width(image), clamp);
	y = place(y, image_height(image), clamp);
	at = texels + (uint32_t)y * texture_pitch(image_width(image), format);
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
