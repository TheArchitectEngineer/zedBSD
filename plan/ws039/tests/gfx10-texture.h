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
 * bilinearly, and wraps or clamps.  The descriptors are written in the
 * GFX10 layout the interpreter decodes (the fields of gfx10-rsrc.json);
 * the driver's own descriptors are not made here.
 */

#ifndef PLAN_WS039_TESTS_GFX10_TEXTURE_H
#define PLAN_WS039_TESTS_GFX10_TEXTURE_H

#include <stdint.h>

/* The two image formats of the tests (GFX10_FORMAT codes). */
#define TEXTURE_FORMAT_UNORM8X4	56U
#define TEXTURE_FORMAT_FLOAT4	77U

/* Writes an image descriptor (8 words) and a sampler (4 words). */
void texture_descriptors(uint32_t *image, uint32_t *sampler, uint64_t address, uint32_t width, uint32_t height, uint32_t format, int bilinear, int clamp);

/* The bytes of one row of an image (rows start 256-byte aligned). */
uint32_t texture_pitch(uint32_t width, uint32_t format);

/* The image's address in a descriptor. */
uint64_t texture_address(const uint32_t *image);

/*
 * Filters one texel group: at normalized (u, v) with a texel offset, or
 * (fetch) at the integer texel (u, v); out gets four channel words.
 */
void texture_filter(const uint8_t *texels, const uint32_t *image, const uint32_t *sampler, uint32_t u_bits, uint32_t v_bits, int fetch, int offset_u, int offset_v, uint32_t *out);

#endif /* PLAN_WS039_TESTS_GFX10_TEXTURE_H */
