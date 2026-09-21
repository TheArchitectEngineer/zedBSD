/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Single-precision floats computed on integer registers (see math.h).
 *
 * Every value here is an IEEE-754 single-precision bit pattern held in a
 * uint32_t: the sign in bit 31, the biased exponent in bits 30..23 and the
 * fraction in bits 22..0.
 */

#include "math.h"

#include <stdint.h>

/* The sign bit of a single-precision value. */
#define I915_FLOAT_SIGN		0x80000000U

/* Everything but the sign: the magnitude, comparable as an integer. */
#define I915_FLOAT_MAGNITUDE	0x7fffffffU

/* The fraction field, and the hidden leading one of a normal number. */
#define I915_FLOAT_FRACTION	0x007fffffU
#define I915_FLOAT_HIDDEN_ONE	0x00800000U

/* Where the biased exponent starts, and the mask of its eight bits once shifted down. */
#define I915_FLOAT_EXPONENT_SHIFT	23U
#define I915_FLOAT_EXPONENT_MASK	0xffU

/* The exponent bias of single precision. */
#define I915_FLOAT_BIAS		127U

/*
 * Halves a value.
 *
 * A normal value loses one from its exponent.  A value whose exponent is
 * already at most one would become subnormal and is flushed to a zero of
 * the same sign.
 */
uint32_t
drv_i915_float_half(
	uint32_t value)
{
	uint32_t exponent;

	/* Takes the biased exponent. */
	exponent = (value >> I915_FLOAT_EXPONENT_SHIFT) & I915_FLOAT_EXPONENT_MASK;

	/* Flushes what would become subnormal to a signed zero. */
	if (exponent <= 1U)
		return value & I915_FLOAT_SIGN;

	/* Succeeded: the same fraction one binary order lower. */
	return value - (1U << I915_FLOAT_EXPONENT_SHIFT);
}

/*
 * Adds two values, rounding to nearest.
 *
 * Only zeros and normal numbers are handled, which is all the viewport
 * arithmetic needs.  The mantissas are aligned with eight guard bits, added
 * or subtracted, normalized back to 32 bits and rounded.
 */
uint32_t
drv_i915_float_add(
	uint32_t augend,
	uint32_t addend)
{
	uint32_t larger;
	uint32_t smaller;
	uint32_t larger_exponent;
	uint32_t smaller_exponent;
	uint32_t sign;
	uint64_t larger_mantissa;
	uint64_t smaller_mantissa;
	uint64_t mantissa;

	/* A zero leaves the other value unchanged. */
	if ((augend & I915_FLOAT_MAGNITUDE) == 0U)
		return addend;
	if ((addend & I915_FLOAT_MAGNITUDE) == 0U)
		return augend;

	/* Orders the operands so that the first has the larger magnitude. */
	larger = augend;
	smaller = addend;
	if ((augend & I915_FLOAT_MAGNITUDE) < (addend & I915_FLOAT_MAGNITUDE)) {
		larger = addend;
		smaller = augend;
	}

	/*
	 * Unpacks both operands.  The result takes the sign of the larger one,
	 * and each mantissa gets its hidden one and eight guard bits.
	 */
	larger_exponent = (larger >> I915_FLOAT_EXPONENT_SHIFT) & I915_FLOAT_EXPONENT_MASK;
	smaller_exponent = (smaller >> I915_FLOAT_EXPONENT_SHIFT) & I915_FLOAT_EXPONENT_MASK;
	sign = larger & I915_FLOAT_SIGN;
	larger_mantissa = ((uint64_t)((larger & I915_FLOAT_FRACTION) | I915_FLOAT_HIDDEN_ONE)) << 8;
	smaller_mantissa = ((uint64_t)((smaller & I915_FLOAT_FRACTION) | I915_FLOAT_HIDDEN_ONE)) << 8;

	/* A value more than 40 binary orders smaller does not change the sum. */
	if (larger_exponent - smaller_exponent > 40U)
		return larger;

	/* Aligns the smaller mantissa to the larger one's exponent. */
	smaller_mantissa >>= larger_exponent - smaller_exponent;

	/* Operands of opposite signs subtract their magnitudes; equal signs add them. */
	if (((larger ^ smaller) & I915_FLOAT_SIGN) != 0U) {
		mantissa = larger_mantissa - smaller_mantissa;
	} else {
		mantissa = larger_mantissa + smaller_mantissa;
	}

	/* Equal magnitudes of opposite signs cancel to a positive zero. */
	if (mantissa == 0U)
		return 0U;

	/* Brings a carry out of 32 bits back down. */
	while (mantissa >= ((uint64_t)1U << 32)) {
		mantissa >>= 1;
		larger_exponent++;
	}

	/* Brings a cancelled leading bit back up to bit 31. */
	while (mantissa < ((uint64_t)1U << 31)) {
		mantissa <<= 1;
		larger_exponent--;
	}

	/* Rounds the eight guard bits away to nearest. */
	mantissa = (mantissa + 0x80U) >> 8;

	/* A rounding that carried into bit 24 renormalizes once more. */
	if (mantissa >= (1U << 24)) {
		mantissa >>= 1;
		larger_exponent++;
	}

	/* Succeeded: packs the sign, the exponent and the fraction. */
	return sign | (larger_exponent << I915_FLOAT_EXPONENT_SHIFT) | ((uint32_t)mantissa & I915_FLOAT_FRACTION);
}

/*
 * Subtracts one value from another, rounding to nearest.
 *
 * The subtrahend's sign is flipped and the two are added.
 */
uint32_t
drv_i915_float_sub(
	uint32_t minuend,
	uint32_t subtrahend)
{
	uint32_t difference;

	/* Adds the negated subtrahend. */
	difference = drv_i915_float_add(minuend, subtrahend ^ I915_FLOAT_SIGN);

	/* Succeeded: reports the difference. */
	return difference;
}

/*
 * Converts a non-negative integer to a value.
 *
 * The conversion is exact below 2^24; above it the low bits are truncated.
 */
uint32_t
drv_i915_float_from_u32(
	uint32_t value)
{
	uint32_t msb;

	/* Zero has an all-zero pattern. */
	if (value == 0U)
		return 0U;

	/* Finds the highest set bit, which becomes the hidden one. */
	msb = 31U - (uint32_t)__builtin_clz(value);

	/* Moves the highest set bit to bit 23. */
	if (msb > 23U) {
		value >>= msb - 23U;
	} else {
		value <<= 23U - msb;
	}

	/* Succeeded: the exponent of the highest bit and the bits below it. */
	return ((I915_FLOAT_BIAS + msb) << I915_FLOAT_EXPONENT_SHIFT) | (value & I915_FLOAT_FRACTION);
}

/*
 * Converts the ratio of two integers to a value, rounding to nearest.
 *
 * Both integers are at most 2^20 and the denominator is not zero; a zero
 * numerator or denominator gives zero.  The quotient is taken with 40
 * fraction bits, which leaves at least 20 significant bits above them.
 */
uint32_t
drv_i915_float_ratio(
	uint32_t numerator,
	uint32_t denominator)
{
	uint64_t quotient;
	uint64_t shifted;
	uint32_t msb;
	uint32_t mantissa;
	int exponent;

	/* A zero on either side gives zero. */
	if (numerator == 0U || denominator == 0U)
		return 0U;

	/* Divides with 40 fraction bits: numerator / denominator * 2^40. */
	quotient = ((uint64_t)numerator << 40) / denominator;

	/* Finds the highest set bit, which gives the unbiased exponent. */
	msb = 63U - (uint32_t)__builtin_clzll(quotient);
	exponent = (int)msb - 40;

	/* Takes 25 bits from the highest set bit down: the mantissa and one rounding bit. */
	if (msb >= 24U) {
		shifted = quotient >> (msb - 24U);
	} else {
		shifted = quotient << (24U - msb);
	}

	/* Rounds the rounding bit away to nearest. */
	mantissa = (uint32_t)((shifted + 1U) >> 1);

	/* A rounding that carried into bit 24 renormalizes once. */
	if ((mantissa >> 24) != 0U) {
		mantissa >>= 1;
		exponent++;
	}

	/* Succeeded: packs the biased exponent and the fraction. */
	return ((uint32_t)(127 + exponent) << I915_FLOAT_EXPONENT_SHIFT) | (mantissa & I915_FLOAT_FRACTION);
}
