/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The correctly rounded square root.
 *
 * The significand is square-rooted in integers, one result bit per step,
 * the way square roots are taken by hand in base two.  Fifty-five bits of
 * root are produced, and a nonzero final remainder becomes a sticky bit, so
 * the one rounding at the end is correct.
 */

#include <math.h>
#include <stdint.h>

#include "src/libc/math/math-internal.h"

/*
 * The number of radicand bits appended below the significand.
 *
 * A significand of 53 or 54 bits followed by 56 zero bits has a square
 * root of exactly 55 bits: 53 kept, one to round with, and one more below.
 */
#define LIBM_SQRT_EXTRA_BITS 56

/*
 * Returns the correctly rounded square root of a binary64 value.
 */
double
sqrt(
	double x)
{
	struct libm_unpacked unpacked;
	uint64_t significand;
	uint64_t root;
	uint64_t rest;
	uint64_t trial;
	uint64_t pair;
	int exponent;
	int position;
	double result;

	/*
	 * A NaN, unequal to itself, is passed on; a zero and positive infinity
	 * are their own roots, and anything else below zero has none.
	 */
	if (x != x)
		return x + x;
	if (x == 0.0)
		return x;
	if (x < 0.0)
		return __libm_invalid();
	if (x == HUGE_VAL)
		return x;

	/*
	 * Writes x as significand * 2^exponent with an even exponent, so the
	 * root of the power of two is exact.
	 */
	__libm_unpack(x, &unpacked);
	significand = unpacked.significand;
	exponent = unpacked.exponent;
	if ((exponent & 1) != 0) {
		significand <<= 1;
		exponent--;
	}

	/*
	 * Takes the root of significand * 2^56 two radicand bits at a time,
	 * from the top.  At each step the remainder is at most twice the root,
	 * so it stays below 2^57 however long the radicand is.
	 */
	root = 0U;
	rest = 0U;
	for (position = 54; position >= 0; position--) {
		/* Brings down the next pair; the pairs below the significand are 0. */
		pair = 0U;
		if (2 * position >= LIBM_SQRT_EXTRA_BITS)
			pair = (significand >> (2 * position - LIBM_SQRT_EXTRA_BITS)) & 3U;
		rest = (rest << 2) | pair;

		/* The next bit is one when (2 * root + 1) fits in the remainder. */
		trial = (root << 2) | 1U;
		root <<= 1;
		if (rest >= trial) {
			rest -= trial;
			root |= 1U;
		}
	}

	/*
	 * The root has 55 bits and is worth root * 2^(exponent / 2 - 28).
	 * Moving it up by nine bits and adding the sticky bit gives the packing
	 * helper its 64-bit form.
	 */
	root <<= 9;
	if (rest != 0U)
		root |= 1U;
	result = __libm_pack(0U, exponent / 2 - 28 + 54, root);

	/* Succeeded: the root was rounded once. */
	return result;
}

/*
 * Returns the correctly rounded square root of a binary32 value.
 *
 * The double root is correctly rounded to 53 bits, and rounding it again
 * to 24 bits gives the correctly rounded float root, because 53 is more
 * than twice 24 plus two.
 */
float
sqrtf(
	float x)
{
	double root;

	/* Takes the root in double. */
	root = sqrt((double)x);

	/* Succeeded: the one further rounding is exact enough. */
	return (float)root;
}
