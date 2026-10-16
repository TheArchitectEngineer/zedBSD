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
 * Only level 0 of a linear image of four 8-bit unorm or four 32-bit
 * float channels is read: a 2D image, a 3D one, an array of 2D images or
 * of cubes (a cube is six 2D slices, +x, -x, +y, -y, +z, -z), the slices
 * one after the other.  The sampler filters by nearest texel or
 * bilinearly (trilinearly in a 3D image; the magnifying filter at a level
 * of detail up to 0, the minifying one above), wraps or clamps (a cube's
 * face always clamps), and compares the first channel with a reference
 * for a depth compare.  A gather reads one channel (or the compare) of
 * the four texels a bilinear filter weighs.  The level of detail is the
 * explicit level, or the bias, or the one the gradients give; a sample
 * with implicit derivatives is taken at level 0.  The descriptors are
 * written in the GFX10 layout the interpreter decodes (the fields of
 * gfx10-rsrc.json); the driver's own descriptors are not made here.
 */

#ifndef PLAN_WS039_TESTS_GFX10_TEXTURE_H
#define PLAN_WS039_TESTS_GFX10_TEXTURE_H

#include <stdint.h>

/* The image types of the descriptor (SQ_RSRC_IMG codes). */
#define TEXTURE_TYPE_2D		9U
#define TEXTURE_TYPE_3D		10U
#define TEXTURE_TYPE_CUBE	11U
#define TEXTURE_TYPE_2D_ARRAY	13U

/* The two image formats of the tests (GFX10_FORMAT codes). */
#define TEXTURE_FORMAT_UNORM8X4	56U
#define TEXTURE_FORMAT_FLOAT4	77U

/* The filters of texture_descriptors(): bilinear when magnifying, when minifying. */
#define TEXTURE_MAG_BILINEAR	1
#define TEXTURE_MIN_BILINEAR	2

/*
 * One request to the texture unit: the coordinates u, v and a 3D image's
 * r (float bits, normalized to [0, 1] on a cube's face, or the integer
 * texel of a fetch), an array's layer (a cube's number in an array of
 * cubes) and a cube's face, the texel offset, the level of detail (the
 * explicit level or the bias, float bits) or the gradients (each
 * coordinate's along x, then each one's along y, float bits), the depth
 * compare reference, a gather of a channel, or a size query of an integer
 * level.
 */
struct texture_request {
	uint32_t u;
	uint32_t v;
	uint32_t r;
	int32_t layer;
	int face;
	int fetch;
	int offset_u;
	int offset_v;
	int offset_r;
	int has_lod;
	uint32_t lod;
	int has_gradients;
	uint32_t gradients[6];
	int compare;
	uint32_t reference;
	int gather;
	uint32_t channel;
	int size_query;
	uint32_t level;
};

/* Writes an image descriptor (8 words) and a sampler (4 words) with a depth compare function (0 .. 7). */
void texture_descriptors(uint32_t *image, uint32_t *sampler, uint64_t address, uint32_t width, uint32_t height, uint32_t format, int filters, int clamp, uint32_t compare);

/* Makes the descriptor an image of a type with a depth (a 3D image's slices, an array's layers, an array's cubes). */
void texture_shape(uint32_t *image, uint32_t type, uint32_t depth);

/* The bytes of one row of an image (rows start 256-byte aligned). */
uint32_t texture_pitch(uint32_t width, uint32_t format);

/* The image's address in a descriptor. */
uint64_t texture_address(const uint32_t *image);

/*
 * Answers one request: out gets four channel words (a compare's result in
 * the first, 1.0 or 0.0 filtered; a gather's four texels' channel or
 * compare in the order (i0, j1), (i1, j1), (i1, j0), (i0, j0); a size
 * query's width, height, depth and level count as integers).
 */
void texture_sample(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, const struct texture_request *request, uint32_t *out);

#endif /* PLAN_WS039_TESTS_GFX10_TEXTURE_H */
