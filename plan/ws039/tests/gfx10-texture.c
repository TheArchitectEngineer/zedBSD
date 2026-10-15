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
 * 27:14 and the resource level in 31; word 3 the channel selects and the
 * type (9, 2D) in 31:28.  Sampler word 0 the clamp of x and y in 2:0 and
 * 5:3 (0 wraps), word 2 the magnifying and minifying filters in 21:20 and
 * 23:22 (1 bilinear).  (Mesa 25.0.7 src/amd/registers/gfx10-rsrc.json,
 * SQ_IMG_RSRC_WORD0..3, SQ_IMG_SAMP_WORD0, SQ_IMG_SAMP_WORD2.)
 */

#include "plan/ws039/tests/gfx10-texture.h"

#include <math.h>
#include <string.h>

static void texel(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, int32_t x, int32_t y, float *out);
static int32_t place(int32_t coordinate, uint32_t size, int clamp);
static uint32_t image_width(const uint32_t *image);
static uint32_t image_height(const uint32_t *image);
static uint32_t image_format(const uint32_t *image);

void
texture_descriptors(
	uint32_t *image,
	uint32_t *sampler,
	uint64_t address,
	uint32_t width,
	uint32_t height,
	uint32_t format,
	int bilinear,
	int clamp)
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
	sampler[2] = bilinear ? ((1U << 20) | (1U << 22)) : 0U;
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
texture_filter(
	const uint8_t *texels,
	const uint32_t *image,
	const uint32_t *sampler,
	uint32_t u_bits,
	uint32_t v_bits,
	int fetch,
	int offset_u,
	int offset_v,
	uint32_t *out)
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
	float result[4];
	int32_t x0;
	int32_t y0;
	uint32_t channel;
	int bilinear;

	/* A fetch reads the integer texel. */
	if (fetch != 0) {
		texel(texels, image, sampler, (int32_t)u_bits, (int32_t)v_bits, result);
		memcpy(out, result, 16U);
		return;
	}
	memcpy(&u, &u_bits, 4U);
	memcpy(&v, &v_bits, 4U);
	bilinear = ((sampler[2] >> 20) & 3U) == 1U;

	/* The nearest texel. */
	if (bilinear == 0) {
		x = floorf(u * (float)image_width(image));
		y = floorf(v * (float)image_height(image));
		texel(texels, image, sampler, (int32_t)x + offset_u, (int32_t)y + offset_v, result);
		memcpy(out, result, 16U);
		return;
	}

	/* The four texels around the sample point, weighted. */
	x = u * (float)image_width(image) - 0.5f;
	y = v * (float)image_height(image) - 0.5f;
	x0 = (int32_t)floorf(x);
	y0 = (int32_t)floorf(y);
	fx = x - (float)x0;
	fy = y - (float)y0;
	texel(texels, image, sampler, x0 + offset_u, y0 + offset_v, t00);
	texel(texels, image, sampler, x0 + 1 + offset_u, y0 + offset_v, t10);
	texel(texels, image, sampler, x0 + offset_u, y0 + 1 + offset_v, t01);
	texel(texels, image, sampler, x0 + 1 + offset_u, y0 + 1 + offset_v, t11);
	for (channel = 0U; channel < 4U; channel++) {
		result[channel] = (t00[channel] * (1.0f - fx) + t10[channel] * fx) * (1.0f - fy) +
				  (t01[channel] * (1.0f - fx) + t11[channel] * fx) * fy;
	}
	memcpy(out, result, 16U);
}

/* One texel's four channels as floats, its coordinates wrapped or clamped. */
static void
texel(
	const uint8_t *texels,
	const uint32_t *image,
	const uint32_t *sampler,
	int32_t x,
	int32_t y,
	float *out)
{
	const uint8_t *at;
	uint32_t format;
	uint32_t channel;
	int clamp;

	format = image_format(image);
	clamp = (sampler[0] & 7U) != 0U;
	x = place(x, image_width(image), clamp);
	y = place(y, image_height(image), clamp);
	at = texels + (uint32_t)y * texture_pitch(image_width(image), format);
	if (format == TEXTURE_FORMAT_FLOAT4) {
		memcpy(out, at + (uint32_t)x * 16U, 16U);
		return;
	}
	for (channel = 0U; channel < 4U; channel++)
		out[channel] = (float)at[(uint32_t)x * 4U + channel] / 255.0f;
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
