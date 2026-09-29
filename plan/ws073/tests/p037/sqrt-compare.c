/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws073-p037 (BUG-109): compares libm's sqrt and sqrtf under test (the
 * processor's instruction) with the integer square root they replaced
 * (reference_sqrt, reference_sqrtf): the same bits, the same errno, and the
 * same FE_INEXACT and FE_INVALID in libc's software exceptions, over special
 * values, every binade, subnormals, exact squares and random values; and
 * the time of each.  run-sqrt-compare.sh builds and runs it on the host.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* libc's exceptions and the reference roots (the host's fenv.h names the same constants for amd64). */
int zedbsd_feclearexcept(int exceptions);
int zedbsd_fetestexcept(int exceptions);
double reference_sqrt(double x);
float reference_sqrtf(float x);

/* The number of random values of each kind, and the rounds of the timing. */
#define COMPARE_RANDOM		2000000U
#define COMPARE_TIMING		4000000U

static uint64_t compare_state = 0x9e3779b97f4a7c15ULL;
static unsigned compare_failures;

static uint64_t compare_random(void);
static void compare_double(double x);
static void compare_float(float x);
static double compare_seconds(void);

/*
 * Runs the comparisons and the timing.
 */
int
main(
	void)
{
	static const double specials[] = { 0.0, -0.0, 1.0, -1.0, 2.0, 4.0, 0.25, 1e-310, 4.9e-324, 1.7976931348623157e308, -1e-300 };
	uint64_t bits;
	uint32_t word;
	unsigned index;
	unsigned exponent;
	double x;
	double root;
	double scale;
	double sum;
	double start;
	double tested_seconds;
	double reference_seconds;
	float y;

	/* The special values. */
	for (index = 0; index < sizeof(specials) / sizeof(specials[0]); index++) {
		compare_double(specials[index]);
		compare_float((float)specials[index]);
	}

	/* The infinities and NaNs. */
	compare_double(INFINITY);
	compare_double(-INFINITY);
	compare_double(NAN);
	compare_float(INFINITY);
	compare_float(-INFINITY);
	compare_float(NAN);

	/* Each binade's first value and exact squares. */
	for (exponent = 0; exponent < 2047U; exponent++) {
		bits = (uint64_t)exponent << 52;
		memcpy(&x, &bits, sizeof(x));
		compare_double(x);
		compare_double((double)exponent * (double)exponent);
	}

	/* The same for floats. */
	for (exponent = 0; exponent < 255U; exponent++) {
		word = (uint32_t)exponent << 23;
		memcpy(&y, &word, sizeof(y));
		compare_float(y);
		compare_float((float)(exponent * exponent));
	}

	/* Exact and inexact squares of tiny roots, whose squares are subnormal or just above (the scaled exactness check). */
	scale = 1.0;
	for (index = 0; index < 520U; index++)
		scale *= 0.5;
	for (index = 1; index < 200000U; index++) {
		root = (double)index * scale;
		compare_double(root * root);
		compare_double(root * root * 3.0);
	}

	/* Random bit patterns of both widths (the negative ones and NaNs among them). */
	for (index = 0; index < COMPARE_RANDOM; index++) {
		bits = compare_random();
		memcpy(&x, &bits, sizeof(x));
		compare_double(x);
		word = (uint32_t)(bits >> 32);
		memcpy(&y, &word, sizeof(y));
		compare_float(y);
	}

	/* The time of each double root over positive values. */
	sum = 0.0;
	start = compare_seconds();
	for (index = 0; index < COMPARE_TIMING; index++)
		sum += sqrt((double)index + 0.5);
	tested_seconds = compare_seconds() - start;
	start = compare_seconds();
	for (index = 0; index < COMPARE_TIMING; index++)
		sum += reference_sqrt((double)index + 0.5);
	reference_seconds = compare_seconds() - start;
	printf("sqrt: %u calls, tested %.3f s, reference %.3f s (sum %g)\n", COMPARE_TIMING, tested_seconds, reference_seconds, sum);

	/* The verdict. */
	if (compare_failures != 0U) {
		printf("sqrt-compare: %u differences FAIL\n", compare_failures);
		return 1;
	}

	/* Succeeded: the same roots, errno and exceptions everywhere. */
	printf("sqrt-compare: PASS\n");
	return 0;
}

/* Returns the next value of a xorshift generator. */
static uint64_t
compare_random(
	void)
{
	/* The generator's three shifts. */
	compare_state ^= compare_state << 13;
	compare_state ^= compare_state >> 7;
	compare_state ^= compare_state << 17;
	return compare_state;
}

/* Compares one double root: its bits, errno, and the inexact and invalid exceptions. */
static void
compare_double(
	double x)
{
	uint64_t tested_bits;
	uint64_t reference_bits;
	double tested;
	double reference;
	int tested_errno;
	int reference_errno;
	int tested_flags;
	int reference_flags;

	/* The root under test. */
	errno = 0;
	zedbsd_feclearexcept(FE_ALL_EXCEPT);
	tested = sqrt(x);
	tested_errno = errno;
	tested_flags = zedbsd_fetestexcept(FE_INEXACT | FE_INVALID);

	/* The reference root. */
	errno = 0;
	zedbsd_feclearexcept(FE_ALL_EXCEPT);
	reference = reference_sqrt(x);
	reference_errno = errno;
	reference_flags = zedbsd_fetestexcept(FE_INEXACT | FE_INVALID);

	/* Any difference (NaNs compared as NaNs). */
	memcpy(&tested_bits, &tested, sizeof(tested));
	memcpy(&reference_bits, &reference, sizeof(reference));
	if (tested != tested && reference != reference)
		tested_bits = reference_bits;
	if (tested_bits != reference_bits || tested_errno != reference_errno || tested_flags != reference_flags) {
		if (compare_failures < 20U)
			printf("sqrt(%a): %a errno=%d flags=%d, reference %a errno=%d flags=%d\n", x, tested, tested_errno, tested_flags, reference, reference_errno, reference_flags);
		compare_failures++;
	}
}

/* Compares one float root, as compare_double does a double one. */
static void
compare_float(
	float x)
{
	uint32_t tested_bits;
	uint32_t reference_bits;
	float tested;
	float reference;
	int tested_errno;
	int reference_errno;
	int tested_flags;
	int reference_flags;

	/* The root under test. */
	errno = 0;
	zedbsd_feclearexcept(FE_ALL_EXCEPT);
	tested = sqrtf(x);
	tested_errno = errno;
	tested_flags = zedbsd_fetestexcept(FE_INEXACT | FE_INVALID);

	/* The reference root. */
	errno = 0;
	zedbsd_feclearexcept(FE_ALL_EXCEPT);
	reference = reference_sqrtf(x);
	reference_errno = errno;
	reference_flags = zedbsd_fetestexcept(FE_INEXACT | FE_INVALID);

	/* Any difference (NaNs compared as NaNs). */
	memcpy(&tested_bits, &tested, sizeof(tested));
	memcpy(&reference_bits, &reference, sizeof(reference));
	if (tested != tested && reference != reference)
		tested_bits = reference_bits;
	if (tested_bits != reference_bits || tested_errno != reference_errno || tested_flags != reference_flags) {
		if (compare_failures < 20U)
			printf("sqrtf(%a): %a errno=%d flags=%d, reference %a errno=%d flags=%d\n", (double)x, (double)tested, tested_errno, tested_flags, (double)reference, reference_errno, reference_flags);
		compare_failures++;
	}
}

/* Reports a monotonic time in seconds. */
static double
compare_seconds(
	void)
{
	struct timespec now;

	/* The monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}
