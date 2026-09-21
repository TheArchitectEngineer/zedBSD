/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Single-precision floats computed on integer registers.
 *
 * The kernel is built without the floating-point registers, yet the state a
 * draw writes (the viewport transform, the vertices of a rectangle, its
 * sampling coordinates) is made of IEEE-754 single-precision values.  These
 * helpers produce the bit patterns of those values with integer arithmetic
 * only.  They cover what the render paths need: zeros and normal numbers, no
 * infinities, NaNs or subnormal results.
 */

#ifndef DRIVERS_GPU_I915_RENDER_MATH_H
#define DRIVERS_GPU_I915_RENDER_MATH_H

#include <stdint.h>

/* The bit patterns of 1.0 and -1.0. */
#define I915_FLOAT_ONE		0x3f800000U
#define I915_FLOAT_MINUS_ONE	0xbf800000U

uint32_t drv_i915_float_half(uint32_t value);
uint32_t drv_i915_float_add(uint32_t augend, uint32_t addend);
uint32_t drv_i915_float_sub(uint32_t minuend, uint32_t subtrahend);
uint32_t drv_i915_float_from_u32(uint32_t value);
uint32_t drv_i915_float_ratio(uint32_t numerator, uint32_t denominator);

#endif
