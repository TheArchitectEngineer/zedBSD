/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: the texture unit of the host tests, shared by the GFX10
 * interpreter (for its image instructions) and the IR reference (for its
 * samples), so that both read the same texels the same way.
 *
 * Only level 0 of a linear 2D image of four 8-bit unorm or four 32-bit
 * float channels is read; the sampler filters by nearest texel or
 * bilinearly (the magnifying filter at a level of detail up to 0, the
 * minifying one above), wraps or clamps, and compares the first channel
 * with a reference for a depth compare.  The level of detail is the
 * explicit level, or the bias, or the one the gradients give; a sample
 * with implicit derivatives is taken at level 0.  The descriptors are
 * written in the GFX10 layout the interpreter decodes (the fields of
 * gfx10-rsrc.json); the driver's own descriptors are not made here.
 */

#ifndef PLAN_WS039_TESTS_GFX10_TEXTURE_H
#define PLAN_WS039_TESTS_GFX10_TEXTURE_H

#include <stdint.h>

/* The two image formats of the tests (GFX10_FORMAT codes). */
#define TEXTURE_FORMAT_UNORM8X4	56U
#define TEXTURE_FORMAT_FLOAT4	77U

/* The filters of texture_descriptors(): bilinear when magnifying, when minifying. */
#define TEXTURE_MAG_BILINEAR	1
#define TEXTURE_MIN_BILINEAR	2

/*
 * One request to the texture unit: the coordinates (float bits, or the
 * integer texel of a fetch), the texel offset, the level of detail (the
 * explicit level or the bias, float bits) or the gradients (du/dx, dv/dx,
 * du/dy, dv/dy, float bits), the depth compare reference, or a size query
 * of an integer level.
 */
struct texture_request {
	uint32_t u;
	uint32_t v;
	int fetch;
	int offset_u;
	int offset_v;
	int has_lod;
	uint32_t lod;
	int has_gradients;
	uint32_t gradients[4];
	int compare;
	uint32_t reference;
	int size_query;
	uint32_t level;
};

/* Writes an image descriptor (8 words) and a sampler (4 words) with a depth compare function (0 .. 7). */
void texture_descriptors(uint32_t *image, uint32_t *sampler, uint64_t address, uint32_t width, uint32_t height, uint32_t format, int filters, int clamp, uint32_t compare);

/* The bytes of one row of an image (rows start 256-byte aligned). */
uint32_t texture_pitch(uint32_t width, uint32_t format);

/* The image's address in a descriptor. */
uint64_t texture_address(const uint32_t *image);

/*
 * Answers one request: out gets four channel words (a compare's result in
 * the first, 1.0 or 0.0 filtered; a size query's width, height, depth and
 * level count as integers).
 */
void texture_sample(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, const struct texture_request *request, uint32_t *out);

#endif /* PLAN_WS039_TESTS_GFX10_TEXTURE_H */
