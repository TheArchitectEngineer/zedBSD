/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declarations shared by the files of the mathematical library.
 *
 * The library assumes binary64 doubles and binary32 floats rounded to
 * nearest, which every configured target provides: SSE2 on amd64, the
 * soft-float runtime on i386, and the hardware on arm64 and sparcv9.  The
 * floating-point environment is a software cell (src/libc/fenv.c) that only
 * knows round-to-nearest, so exceptions are raised here explicitly.
 */

#ifndef LIBC_MATH_INTERNAL_H
#define LIBC_MATH_INTERNAL_H

#include <stdint.h>

/*
 * The error-free transformations below depend on every product being
 * rounded before it is added.  The arm64 compiler fuses a*b+c into one
 * instruction unless it is told not to, which would silently change their
 * results.  GCC builds only sparcv9 here, with soft float and no fused
 * instruction to contract into, and it rejects this pragma.
 */
#ifdef __clang__
#pragma STDC FP_CONTRACT OFF
#endif

/*
 * Keeps the helpers out of the dynamic symbol table of the C library.
 *
 * They are shared between the files of the library only, and a program
 * must not be able to interpose them.
 */
#define LIBM_HIDDEN __attribute__((visibility("hidden")))

/* The bias of the binary64 exponent field. */
#define LIBM_DOUBLE_BIAS 1023

/* The sign bit of a binary64 value. */
#define LIBM_DOUBLE_SIGN UINT64_C(0x8000000000000000)

/* The bits of the magnitude of a binary64 value. */
#define LIBM_DOUBLE_MAGNITUDE UINT64_C(0x7fffffffffffffff)

/* The encoding of positive infinity, above which every magnitude is a NaN. */
#define LIBM_DOUBLE_INFINITY UINT64_C(0x7ff0000000000000)

/* The implicit leading bit of a normal binary64 significand. */
#define LIBM_DOUBLE_HIDDEN UINT64_C(0x0010000000000000)

/* The stored fraction bits of a binary64 value. */
#define LIBM_DOUBLE_FRACTION UINT64_C(0x000fffffffffffff)

/* The sign bit of a binary32 value. */
#define LIBM_FLOAT_SIGN UINT32_C(0x80000000)

/* The bits of the magnitude of a binary32 value. */
#define LIBM_FLOAT_MAGNITUDE UINT32_C(0x7fffffff)

/* The encoding of positive infinity in binary32. */
#define LIBM_FLOAT_INFINITY UINT32_C(0x7f800000)

/* The implicit leading bit of a normal binary32 significand. */
#define LIBM_FLOAT_HIDDEN UINT32_C(0x00800000)

/* The stored fraction bits of a binary32 value. */
#define LIBM_FLOAT_FRACTION UINT32_C(0x007fffff)

/*
 * One binary64 value seen both as a number and as its encoding.
 *
 * Reading the member that was not written is how the library reaches the
 * bits; the compilers of every target define that access.
 */
union libm_double_shape {
	double value;
	uint64_t bits;
};

/*
 * One binary32 value seen both as a number and as its encoding.
 */
union libm_float_shape {
	float value;
	uint32_t bits;
};

/*
 * An unpacked finite nonzero binary64 value.
 *
 * The value is significand * 2^exponent with the significand in
 * [2^52, 2^53), so subnormal inputs are normalized and every caller sees
 * the same shape.
 */
struct libm_unpacked {
	unsigned int sign;
	int exponent;
	uint64_t significand;
};

LIBM_HIDDEN double __libm_invalid(void);
LIBM_HIDDEN double __libm_pole(unsigned int sign);
LIBM_HIDDEN double __libm_overflow(unsigned int sign);
LIBM_HIDDEN double __libm_underflow(unsigned int sign);
LIBM_HIDDEN double __libm_check_underflow(double value);
LIBM_HIDDEN float __libm_narrow(double value);
LIBM_HIDDEN double __libm_pack(unsigned int sign, int exponent, uint64_t significand);
LIBM_HIDDEN void __libm_unpack(double value, struct libm_unpacked *unpacked);

/*
 * Returns the encoding of a binary64 value.
 */
static __inline uint64_t
libm_bits(
	double value)
{
	union libm_double_shape shape;

	/* Stores the value into the shared storage. */
	shape.value = value;

	/* The encoding is the other view of the same storage. */
	return shape.bits;
}

/*
 * Returns the binary64 value of an encoding.
 */
static __inline double
libm_from_bits(
	uint64_t bits)
{
	union libm_double_shape shape;

	/* Stores the encoding into the shared storage. */
	shape.bits = bits;

	/* The value is the other view of the same storage. */
	return shape.value;
}

/*
 * Returns the encoding of a binary32 value.
 */
static __inline uint32_t
libm_float_bits(
	float value)
{
	union libm_float_shape shape;

	/* Stores the value into the shared storage. */
	shape.value = value;

	/* The encoding is the other view of the same storage. */
	return shape.bits;
}

/*
 * Returns the binary32 value of an encoding.
 */
static __inline float
libm_float_from_bits(
	uint32_t bits)
{
	union libm_float_shape shape;

	/* Stores the encoding into the shared storage. */
	shape.bits = bits;

	/* The value is the other view of the same storage. */
	return shape.value;
}

#endif
