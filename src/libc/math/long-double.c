/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The long double versions of the functions.
 *
 * On amd64 and i386 long double is binary64 (the compiler is given
 * -mlong-double-64), so each function here is exactly its double version.
 * On arm64 and sparcv9 long double is binary128; there the functions
 * compute in double and give double precision (WS076, a recorded
 * limitation).
 */

#include <float.h>
#include <math.h>

/*
 * Classifies a long double value.
 *
 * The comparisons work in the long double type itself, so a binary128
 * value outside the double range is classified correctly too.
 */
int
__fpclassifyl(
	long double value)
{
	long double magnitude;

	/* A NaN is the only value unequal to itself. */
	if (value != value)
		return FP_NAN;

	/* Zero is the one value equal to its own negation. */
	if (value == 0.0L)
		return FP_ZERO;

	/* Takes the magnitude by comparison, which keeps every bit. */
	magnitude = value;
	if (magnitude < 0.0L)
		magnitude = -magnitude;

	/* An infinity is unchanged by halving. */
	if (magnitude * 0.5L == magnitude)
		return FP_INFINITE;

	/* Below the smallest normal value lies the subnormal range. */
	if (magnitude < LDBL_MIN)
		return FP_SUBNORMAL;

	/* Succeeded: everything else is normal. */
	return FP_NORMAL;
}

/*
 * Reports whether the sign bit of a long double value is set.
 */
int
__signbitl(
	long double value)
{
	int sign;

	/* Converting to double keeps the sign, of zeros and NaNs as well. */
	sign = __signbit((double)value);

	/* Succeeded: the sign of the double is the sign of the value. */
	return sign;
}

/*
 * Returns the magnitude of a long double value.
 */
long double
fabsl(
	long double x)
{
	int negative;

	/* A value with the sign bit set is negated, which is exact. */
	negative = __signbitl(x);
	if (negative)
		return -x;

	/* Succeeded: the value is its own magnitude. */
	return x;
}

/*
 * Returns the magnitude of one long double value with the sign of another.
 */
long double
copysignl(
	long double x,
	long double y)
{
	long double magnitude;
	int negative;

	/* Takes the magnitude of x exactly. */
	magnitude = fabsl(x);

	/* Gives it the sign of y. */
	negative = __signbitl(y);
	if (negative)
		return -magnitude;

	/* Succeeded: y is positive. */
	return magnitude;
}

/*
 * Returns a quiet long double NaN.
 */
long double
nanl(
	const char *tag)
{
	double result;

	/* Computes the operation in double. */
	result = nan(tag);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the next long double value after x in the direction of y.
 */
long double
nextafterl(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = nextafter((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the next long double value after x in the direction of y.
 */
long double
nexttowardl(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = nextafter((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the smaller of two long double values, ignoring a single NaN.
 */
long double
fminl(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = fmin((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the larger of two long double values, ignoring a single NaN.
 */
long double
fmaxl(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = fmax((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the positive difference of two long double values.
 */
long double
fdiml(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = fdim((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the unbiased exponent of a long double value as an int.
 */
int
ilogbl(
	long double x)
{
	int result;

	/* Computes the operation in double. */
	result = ilogb((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return result;
}

/*
 * Returns the unbiased exponent of a long double value.
 */
long double
logbl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = logb((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Splits a long double value into a fraction in [0.5, 1) and a power of two.
 */
long double
frexpl(
	long double x,
	int *exponent)
{
	double result;

	/* Computes the operation in double. */
	result = frexp((double)x, exponent);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Multiplies a long double value by an integral power of two.
 */
long double
scalbnl(
	long double x,
	int n)
{
	double result;

	/* Computes the operation in double. */
	result = scalbn((double)x, n);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Multiplies a long double value by an integral power of two.
 */
long double
ldexpl(
	long double x,
	int n)
{
	double result;

	/* Computes the operation in double. */
	result = ldexp((double)x, n);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Multiplies a long double value by a power of two given as a long.
 */
long double
scalblnl(
	long double x,
	long n)
{
	double result;

	/* Computes the operation in double. */
	result = scalbln((double)x, n);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value towards zero to an integral value.
 */
long double
truncl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = trunc((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value downwards to an integral value.
 */
long double
floorl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = floor((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value upwards to an integral value.
 */
long double
ceill(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = ceil((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value to the nearest integral value, ties away from zero.
 */
long double
roundl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = round((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value to the nearest integral value, ties to even.
 */
long double
rintl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = rint((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value to the nearest integral value without raising inexact.
 */
long double
nearbyintl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = nearbyint((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Rounds a long double value to the nearest long, ties to even.
 */
long
lrintl(
	long double x)
{
	long result;

	/* Computes the operation in double. */
	result = lrint((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return result;
}

/*
 * Rounds a long double value to the nearest long long, ties to even.
 */
long long
llrintl(
	long double x)
{
	long long result;

	/* Computes the operation in double. */
	result = llrint((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return result;
}

/*
 * Rounds a long double value to the nearest long, ties away from zero.
 */
long
lroundl(
	long double x)
{
	long result;

	/* Computes the operation in double. */
	result = lround((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return result;
}

/*
 * Rounds a long double value to the nearest long long, ties away from zero.
 */
long long
llroundl(
	long double x)
{
	long long result;

	/* Computes the operation in double. */
	result = llround((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return result;
}

/*
 * Returns the remainder of x / y with the quotient truncated towards zero.
 */
long double
fmodl(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = fmod((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the remainder of x / y with the quotient rounded to nearest even.
 */
long double
remainderl(
	long double x,
	long double y)
{
	double result;

	/* Computes the operation in double. */
	result = remainder((double)x, (double)y);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the nearest remainder of x / y and the low bits of its quotient.
 */
long double
remquol(
	long double x,
	long double y,
	int *quotient)
{
	double result;

	/* Computes the operation in double. */
	result = remquo((double)x, (double)y, quotient);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns x * y + z rounded once.
 */
long double
fmal(
	long double x,
	long double y,
	long double z)
{
	double result;

	/* Computes the operation in double. */
	result = fma((double)x, (double)y, (double)z);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the square root of a long double value.
 */
long double
sqrtl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = sqrt((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns e raised to the power x.
 */
long double
expl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = exp((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns 2 raised to the power x.
 */
long double
exp2l(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = exp2((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns e raised to the power x, minus one.
 */
long double
expm1l(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = expm1((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the natural logarithm of x.
 */
long double
logl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = log((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the base-2 logarithm of x.
 */
long double
log2l(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = log2((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns the base-10 logarithm of x.
 */
long double
log10l(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = log10((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}

/*
 * Returns log(1 + x).
 */
long double
log1pl(
	long double x)
{
	double result;

	/* Computes the operation in double. */
	result = log1p((double)x);

	/* Succeeded: the double result stands for the long double one. */
	return (long double)result;
}
