/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The libm test runner (WS076).
 *
 * It reads the reference cases written by gen-reference.py, calls the
 * functions of the C library it is linked with, and prints one line per
 * function: the number of cases, the largest and mean error in ulps, and
 * the number of exact results that differ.  It then checks the special
 * values, errno and exceptions of C11 Annex F from a table of its own.
 *
 *     libm-test REFERENCE.bin [NAME...]
 *
 * It uses only the standard C library, so the same source is built on the
 * host (linked with src/libc/math) and for zedBSD (linked with libc.so).
 * The exception flags and errno codes are those of the zedBSD ABI, which
 * the library under test uses in both builds.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The exception flags of the zedBSD ABI (include/libc/fenv.h). */
#define TEST_FE_INVALID 0x01
#define TEST_FE_DIVBYZERO 0x02
#define TEST_FE_OVERFLOW 0x04
#define TEST_FE_UNDERFLOW 0x08
#define TEST_FE_ALL 0x1f

/* The flags a case may require; inexact is never checked. */
#define TEST_FE_CHECKED 0x0f

/* The errno codes of the zedBSD ABI (include/uapi/errno.h). */
#define TEST_EDOM 1
#define TEST_ERANGE 2

/* The size of one reference record. */
#define TEST_RECORD_SIZE 72

/* The modes of a reference record. */
#define TEST_MODE_EXACT 0
#define TEST_MODE_ULP_DOUBLE 1
#define TEST_MODE_ULP_FLOAT 2

/*
 * The largest error, in ulps, a function measured by ulps may show.
 */
#define TEST_ULP_BOUND 1.0

/*
 * The shapes of the functions under test.
 */
enum test_kind {
	TEST_D_D,
	TEST_D_DD,
	TEST_D_DDD,
	TEST_D_DI,
	TEST_F_F,
	TEST_F_FF,
	TEST_F_FFF,
	TEST_F_FI,
	TEST_I_D,
	TEST_L_D,
	TEST_LL_D,
	TEST_FREXP,
	TEST_FREXPF,
	TEST_MODF,
	TEST_REMQUO,
	TEST_REMQUOF
};

/*
 * One function under test and what the run has measured of it.
 */
struct test_function {
	const char *name;
	enum test_kind kind;
	void (*function)(void);
	unsigned long count;
	unsigned long exact_failures;
	unsigned long over_half;
	double max_ulp;
	double sum_ulp;
	double worst_args[3];
};

/*
 * One special value case of Annex F.
 */
struct test_special {
	const char *name;
	double args[3];
	double expected;
	int expected_errno;
	int expected_flags;
};

/*
 * One reference record, decoded.
 */
struct test_record {
	char name[17];
	int mode;
	double args[3];
	double ref_hi;
	double ref_lo;
	int64_t ref_int;
};

int feclearexcept(int);
int fetestexcept(int);

static struct test_function *find_function(const char *name);
static void run_record(const struct test_record *record);
static int same_double(double left, double right);
static double ulp_error(double value, double high, double low, int is_float);
static int run_specials(void);
static int report(const char *host_path);
static int host_error(const char *host_path, const char *name, double *error);
static void decode(const unsigned char *bytes, struct test_record *record);

/*
 * Every function the runner knows.  The pointer is stored untyped and cast
 * back to the shape the kind names.
 */
static struct test_function test_functions[] = {
	{ "fmod", TEST_D_DD, (void (*)(void))fmod, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "remainder", TEST_D_DD, (void (*)(void))remainder, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "remquo", TEST_REMQUO, (void (*)(void))remquo, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "fmodf", TEST_F_FF, (void (*)(void))fmodf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "remainderf", TEST_F_FF, (void (*)(void))remainderf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "remquof", TEST_REMQUOF, (void (*)(void))remquof, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "fma", TEST_D_DDD, (void (*)(void))fma, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "fmaf", TEST_F_FFF, (void (*)(void))fmaf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "sqrt", TEST_D_D, (void (*)(void))sqrt, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "sqrtf", TEST_F_F, (void (*)(void))sqrtf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "trunc", TEST_D_D, (void (*)(void))trunc, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "floor", TEST_D_D, (void (*)(void))floor, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "ceil", TEST_D_D, (void (*)(void))ceil, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "round", TEST_D_D, (void (*)(void))round, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "rint", TEST_D_D, (void (*)(void))rint, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "nearbyint", TEST_D_D, (void (*)(void))nearbyint, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "lrint", TEST_L_D, (void (*)(void))lrint, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "llrint", TEST_LL_D, (void (*)(void))llrint, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "lround", TEST_L_D, (void (*)(void))lround, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "llround", TEST_LL_D, (void (*)(void))llround, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "rintf", TEST_F_F, (void (*)(void))rintf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "truncf", TEST_F_F, (void (*)(void))truncf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "floorf", TEST_F_F, (void (*)(void))floorf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "frexp", TEST_FREXP, (void (*)(void))frexp, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "frexpf", TEST_FREXPF, (void (*)(void))frexpf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "ldexp", TEST_D_DI, (void (*)(void))ldexp, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "ldexpf", TEST_F_FI, (void (*)(void))ldexpf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "scalbn", TEST_D_DI, (void (*)(void))scalbn, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "ilogb", TEST_I_D, (void (*)(void))ilogb, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "logb", TEST_D_D, (void (*)(void))logb, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "modf", TEST_MODF, (void (*)(void))modf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "nextafter", TEST_D_DD, (void (*)(void))nextafter, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "nextafterf", TEST_F_FF, (void (*)(void))nextafterf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "fmin", TEST_D_DD, (void (*)(void))fmin, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "fmax", TEST_D_DD, (void (*)(void))fmax, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "fdim", TEST_D_DD, (void (*)(void))fdim, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "exp", TEST_D_D, (void (*)(void))exp, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "exp2", TEST_D_D, (void (*)(void))exp2, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "expm1", TEST_D_D, (void (*)(void))expm1, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "expf", TEST_F_F, (void (*)(void))expf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "exp2f", TEST_F_F, (void (*)(void))exp2f, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "expm1f", TEST_F_F, (void (*)(void))expm1f, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log", TEST_D_D, (void (*)(void))log, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log2", TEST_D_D, (void (*)(void))log2, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log10", TEST_D_D, (void (*)(void))log10, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log1p", TEST_D_D, (void (*)(void))log1p, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "logf", TEST_F_F, (void (*)(void))logf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log2f", TEST_F_F, (void (*)(void))log2f, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log10f", TEST_F_F, (void (*)(void))log10f, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "log1pf", TEST_F_F, (void (*)(void))log1pf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "pow", TEST_D_DD, (void (*)(void))pow, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "powf", TEST_F_FF, (void (*)(void))powf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "sin", TEST_D_D, (void (*)(void))sin, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "cos", TEST_D_D, (void (*)(void))cos, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "tan", TEST_D_D, (void (*)(void))tan, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "sinf", TEST_F_F, (void (*)(void))sinf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "cosf", TEST_F_F, (void (*)(void))cosf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "tanf", TEST_F_F, (void (*)(void))tanf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "asin", TEST_D_D, (void (*)(void))asin, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "acos", TEST_D_D, (void (*)(void))acos, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "atan", TEST_D_D, (void (*)(void))atan, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "atan2", TEST_D_DD, (void (*)(void))atan2, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "asinf", TEST_F_F, (void (*)(void))asinf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "acosf", TEST_F_F, (void (*)(void))acosf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "atanf", TEST_F_F, (void (*)(void))atanf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "atan2f", TEST_F_FF, (void (*)(void))atan2f, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "sinh", TEST_D_D, (void (*)(void))sinh, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "cosh", TEST_D_D, (void (*)(void))cosh, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "tanh", TEST_D_D, (void (*)(void))tanh, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "asinh", TEST_D_D, (void (*)(void))asinh, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "acosh", TEST_D_D, (void (*)(void))acosh, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "atanh", TEST_D_D, (void (*)(void))atanh, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "sinhf", TEST_F_F, (void (*)(void))sinhf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "coshf", TEST_F_F, (void (*)(void))coshf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "tanhf", TEST_F_F, (void (*)(void))tanhf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "asinhf", TEST_F_F, (void (*)(void))asinhf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "acoshf", TEST_F_F, (void (*)(void))acoshf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ "atanhf", TEST_F_F, (void (*)(void))atanhf, 0, 0, 0, 0, 0, { 0, 0, 0 } },
	{ NULL, TEST_D_D, NULL, 0, 0, 0, 0, 0, { 0, 0, 0 } }
};

/*
 * The special values, errno and exceptions of C11 Annex F.
 */
static const struct test_special test_specials[] = {
	{ "fmod", { 0.0, 3.0, 0 }, 0.0, 0, 0 },
	{ "fmod", { -0.0, 3.0, 0 }, -0.0, 0, 0 },
	{ "fmod", { 5.0, INFINITY, 0 }, 5.0, 0, 0 },
	{ "fmod", { INFINITY, 3.0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "fmod", { 5.0, 0.0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "fmod", { NAN, 3.0, 0 }, NAN, 0, 0 },
	{ "fmod", { 1e17, 7.0, 0 }, 5.0, 0, 0 },
	{ "fmod", { -6.0, 3.0, 0 }, -0.0, 0, 0 },
	{ "remainder", { 5.0, 0.0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "remainder", { INFINITY, 2.0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "remainder", { 5.0, INFINITY, 0 }, 5.0, 0, 0 },
	{ "remainder", { 5.0, 2.0, 0 }, 1.0, 0, 0 },
	{ "remainder", { 7.0, 2.0, 0 }, -1.0, 0, 0 },
	{ "remainder", { -4.0, 2.0, 0 }, -0.0, 0, 0 },
	{ "sqrt", { -1.0, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "sqrt", { -INFINITY, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "sqrt", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "sqrt", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "sqrt", { 4.0, 0, 0 }, 2.0, 0, 0 },
	{ "fma", { INFINITY, 0.0, 1.0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "fma", { INFINITY, 1.0, -INFINITY }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "fma", { 1.7976931348623157e308, 2.0, 0.0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "fma", { 1e-300, 1e-300, 0.0 }, 0.0, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "fma", { 1.0, 1.0, -1.0 }, 0.0, 0, 0 },
	{ "fma", { -0.0, 1.0, -0.0 }, -0.0, 0, 0 },
	{ "fma", { 0.0, 1.0, -0.0 }, 0.0, 0, 0 },
	{ "fma", { 3.0, 5.0, 1.0 }, 16.0, 0, 0 },
	{ "logb", { 0.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "logb", { -INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "nextafter", { 1.7976931348623157e308, INFINITY, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "nextafter", { 0.0, 1.0, 0 }, 4.9406564584124654e-324, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "nextafter", { -0.0, 0.0, 0 }, 0.0, 0, 0 },
	{ "ldexp", { 1.0, 2000, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "ldexp", { 1.0, -2000, 0 }, 0.0, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "ldexp", { 3.0, -1075, 0 }, 9.8813129168249309e-324, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "ldexp", { 1.0, -1074, 0 }, 4.9406564584124654e-324, 0, 0 },
	{ "rint", { -0.3, 0, 0 }, -0.0, 0, 0 },
	{ "round", { -0.5, 0, 0 }, -1.0, 0, 0 },
	{ "round", { 0.5, 0, 0 }, 1.0, 0, 0 },
	{ "trunc", { -0.5, 0, 0 }, -0.0, 0, 0 },
	{ "ceil", { -0.5, 0, 0 }, -0.0, 0, 0 },
	{ "floor", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "floor", { -0.5, 0, 0 }, -1.0, 0, 0 },
	{ "floor", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "fmin", { NAN, 1.0, 0 }, 1.0, 0, 0 },
	{ "fmax", { -0.0, 0.0, 0 }, 0.0, 0, 0 },
	{ "fmin", { 0.0, -0.0, 0 }, -0.0, 0, 0 },
	{ "fdim", { 1.7976931348623157e308, -1.7976931348623157e308, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "exp", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "exp", { -INFINITY, 0, 0 }, 0.0, 0, 0 },
	{ "exp", { NAN, 0, 0 }, NAN, 0, 0 },
	{ "exp", { 710.0, 0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "exp", { -750.0, 0, 0 }, 0.0, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "exp", { -0.0, 0, 0 }, 1.0, 0, 0 },
	{ "exp2", { 1024.0, 0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "exp2", { -1075.0, 0, 0 }, 0.0, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "exp2", { -1074.0, 0, 0 }, 4.9406564584124654e-324, 0, 0 },
	{ "exp2", { 10.0, 0, 0 }, 1024.0, 0, 0 },
	{ "expm1", { -INFINITY, 0, 0 }, -1.0, 0, 0 },
	{ "expm1", { 710.0, 0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "expm1", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "log", { 0.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "log", { -0.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "log", { -1.0, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "log", { -INFINITY, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "log", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "log", { 1.0, 0, 0 }, 0.0, 0, 0 },
	{ "log2", { 0.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "log2", { 1024.0, 0, 0 }, 10.0, 0, 0 },
	{ "log10", { -1.0, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "log10", { 1000.0, 0, 0 }, 3.0, 0, 0 },
	{ "log1p", { -1.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "log1p", { -2.0, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "log1p", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "log1p", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "pow", { NAN, 0.0, 0 }, 1.0, 0, 0 },
	{ "pow", { 1.0, NAN, 0 }, 1.0, 0, 0 },
	{ "pow", { NAN, 1.0, 0 }, NAN, 0, 0 },
	{ "pow", { 0.0, -3.0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "pow", { -0.0, -3.0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "pow", { -0.0, -2.0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "pow", { -0.0, -0.5, 0 }, INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "pow", { -0.0, 3.0, 0 }, -0.0, 0, 0 },
	{ "pow", { -0.0, 2.0, 0 }, 0.0, 0, 0 },
	{ "pow", { -0.0, 0.5, 0 }, 0.0, 0, 0 },
	{ "pow", { -1.0, INFINITY, 0 }, 1.0, 0, 0 },
	{ "pow", { -1.0, -INFINITY, 0 }, 1.0, 0, 0 },
	{ "pow", { 0.5, -INFINITY, 0 }, INFINITY, 0, 0 },
	{ "pow", { 2.0, -INFINITY, 0 }, 0.0, 0, 0 },
	{ "pow", { 0.5, INFINITY, 0 }, 0.0, 0, 0 },
	{ "pow", { -2.0, INFINITY, 0 }, INFINITY, 0, 0 },
	{ "pow", { -INFINITY, -3.0, 0 }, -0.0, 0, 0 },
	{ "pow", { -INFINITY, -2.0, 0 }, 0.0, 0, 0 },
	{ "pow", { -INFINITY, 3.0, 0 }, -INFINITY, 0, 0 },
	{ "pow", { -INFINITY, 2.5, 0 }, INFINITY, 0, 0 },
	{ "pow", { INFINITY, -1.0, 0 }, 0.0, 0, 0 },
	{ "pow", { INFINITY, 0.5, 0 }, INFINITY, 0, 0 },
	{ "pow", { -2.0, 0.5, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "pow", { 10.0, 400.0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "pow", { -10.0, 401.0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "pow", { 10.0, -400.0, 0 }, 0.0, TEST_ERANGE, TEST_FE_UNDERFLOW },
	{ "pow", { 2.0, -1074.0, 0 }, 4.9406564584124654e-324, 0, 0 },
	{ "pow", { 14.0, 2.0, 0 }, 196.0, 0, 0 },
	{ "pow", { -3.0, 3.0, 0 }, -27.0, 0, 0 },
	{ "pow", { 1.0000000000000002, 1e300, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "sin", { INFINITY, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "cos", { -INFINITY, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "tan", { INFINITY, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "sin", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "tan", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "cos", { -0.0, 0, 0 }, 1.0, 0, 0 },
	{ "sin", { NAN, 0, 0 }, NAN, 0, 0 },
	{ "asin", { 2.0, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "acos", { -1.5, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "asin", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "acos", { 1.0, 0, 0 }, 0.0, 0, 0 },
	{ "acos", { -1.0, 0, 0 }, 3.141592653589793, 0, 0 },
	{ "asin", { 1.0, 0, 0 }, 1.5707963267948966, 0, 0 },
	{ "atan", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "atan", { INFINITY, 0, 0 }, 1.5707963267948966, 0, 0 },
	{ "atan", { -INFINITY, 0, 0 }, -1.5707963267948966, 0, 0 },
	{ "atan2", { 0.0, -0.0, 0 }, 3.141592653589793, 0, 0 },
	{ "atan2", { -0.0, -0.0, 0 }, -3.141592653589793, 0, 0 },
	{ "atan2", { 0.0, 0.0, 0 }, 0.0, 0, 0 },
	{ "atan2", { -0.0, 0.0, 0 }, -0.0, 0, 0 },
	{ "atan2", { -0.0, -1.0, 0 }, -3.141592653589793, 0, 0 },
	{ "atan2", { 1.0, 0.0, 0 }, 1.5707963267948966, 0, 0 },
	{ "atan2", { -1.0, -0.0, 0 }, -1.5707963267948966, 0, 0 },
	{ "atan2", { 1.0, -INFINITY, 0 }, 3.141592653589793, 0, 0 },
	{ "atan2", { -1.0, INFINITY, 0 }, -0.0, 0, 0 },
	{ "atan2", { INFINITY, 1.0, 0 }, 1.5707963267948966, 0, 0 },
	{ "atan2", { INFINITY, -INFINITY, 0 }, 2.3561944901923448, 0, 0 },
	{ "atan2", { -INFINITY, INFINITY, 0 }, -0.78539816339744828, 0, 0 },
	{ "atan2", { 1.0, 1.0, 0 }, 0.78539816339744828, 0, 0 },
	{ "sinh", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "sinh", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "sinh", { 711.0, 0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "sinh", { -711.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "cosh", { -INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "cosh", { 711.0, 0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_OVERFLOW },
	{ "cosh", { 0.0, 0, 0 }, 1.0, 0, 0 },
	{ "tanh", { INFINITY, 0, 0 }, 1.0, 0, 0 },
	{ "tanh", { -INFINITY, 0, 0 }, -1.0, 0, 0 },
	{ "tanh", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "asinh", { -INFINITY, 0, 0 }, -INFINITY, 0, 0 },
	{ "asinh", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ "acosh", { 0.5, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "acosh", { 1.0, 0, 0 }, 0.0, 0, 0 },
	{ "acosh", { INFINITY, 0, 0 }, INFINITY, 0, 0 },
	{ "atanh", { 1.0, 0, 0 }, INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "atanh", { -1.0, 0, 0 }, -INFINITY, TEST_ERANGE, TEST_FE_DIVBYZERO },
	{ "atanh", { 1.5, 0, 0 }, NAN, TEST_EDOM, TEST_FE_INVALID },
	{ "atanh", { -0.0, 0, 0 }, -0.0, 0, 0 },
	{ NULL, { 0, 0, 0 }, 0, 0, 0 }
};

/*
 * Runs the reference file and the special cases and reports.
 */
int
main(
	int argc,
	char **argv)
{
	FILE *file;
	unsigned char bytes[TEST_RECORD_SIZE];
	struct test_record record;
	size_t got;
	int failures;
	int index;
	int wanted;
	int order;
	char host_path[1024];

	/* Needs the reference file. */
	if (argc < 2) {
		fprintf(stderr, "usage: libm-test REFERENCE.bin [NAME...]\n");
		return 2;
	}

	/* Opens the reference file. */
	file = fopen(argv[1], "rb");
	if (file == NULL) {
		fprintf(stderr, "libm-test: cannot open %s\n", argv[1]);
		return 2;
	}

	/* Runs every record, or only those of the named functions. */
	for (;;) {
		got = fread(bytes, 1, sizeof(bytes), file);
		if (got != sizeof(bytes))
			break;

		/* Decodes the record and keeps it if its function was named. */
		decode(bytes, &record);
		wanted = argc == 2;
		for (index = 2; index < argc; index++) {
			order = strcmp(argv[index], record.name);
			if (order == 0)
				wanted = 1;
		}

		/* Runs a record that was asked for. */
		if (wanted)
			run_record(&record);
	}

	/* Every record has been read. */
	fclose(file);

	/* Prints the table, with the host's errors when the generator left them. */
	snprintf(host_path, sizeof(host_path), "%s.host", argv[1]);
	failures = report(host_path);
	failures += run_specials();

	/* Reports the verdict. */
	if (failures != 0) {
		printf("libm-test: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every function is inside its bound. */
	printf("libm-test: PASS\n");
	return 0;
}

/*
 * Decodes one little-endian record.
 */
static void
decode(
	const unsigned char *bytes,
	struct test_record *record)
{
	int32_t mode;

	/* The records are written in the byte order of amd64, the only target. */
	memcpy(record->name, bytes, 16);
	record->name[16] = '\0';
	memcpy(&mode, bytes + 16, 4);
	record->mode = mode;
	memcpy(record->args, bytes + 24, 24);
	memcpy(&record->ref_hi, bytes + 48, 8);
	memcpy(&record->ref_lo, bytes + 56, 8);
	memcpy(&record->ref_int, bytes + 64, 8);
}

/*
 * Finds a function by name.
 */
static struct test_function *
find_function(
	const char *name)
{
	int index;
	int order;

	/* Searches the table in order. */
	for (index = 0; test_functions[index].name != NULL; index++) {
		order = strcmp(test_functions[index].name, name);
		if (order == 0)
			return &test_functions[index];
	}

	/* The name is not a function the runner knows. */
	return NULL;
}

/*
 * Calls a function on one record and records the outcome.
 */
static void
run_record(
	const struct test_record *record)
{
	struct test_function *function;
	double result;
	double second;
	int64_t integer;
	int exponent;
	int quotient;
	double error;
	int correct;
	int matches;

	/* Skips records of functions this build does not know. */
	function = find_function(record->name);
	if (function == NULL)
		return;

	/* Calls the function in its own shape. */
	second = 0.0;
	integer = 0;
	switch (function->kind) {
	case TEST_D_D:
		result = ((double (*)(double))function->function)(record->args[0]);
		break;
	case TEST_D_DD:
		result = ((double (*)(double, double))function->function)(record->args[0], record->args[1]);
		break;
	case TEST_D_DDD:
		result = ((double (*)(double, double, double))function->function)(record->args[0], record->args[1], record->args[2]);
		break;
	case TEST_D_DI:
		result = ((double (*)(double, int))function->function)(record->args[0], (int)record->args[1]);
		break;
	case TEST_F_F:
		result = ((float (*)(float))function->function)((float)record->args[0]);
		break;
	case TEST_F_FF:
		result = ((float (*)(float, float))function->function)((float)record->args[0], (float)record->args[1]);
		break;
	case TEST_F_FFF:
		result = ((float (*)(float, float, float))function->function)((float)record->args[0], (float)record->args[1], (float)record->args[2]);
		break;
	case TEST_F_FI:
		result = ((float (*)(float, int))function->function)((float)record->args[0], (int)record->args[1]);
		break;
	case TEST_I_D:
		result = 0.0;
		integer = ((int (*)(double))function->function)(record->args[0]);
		break;
	case TEST_L_D:
		result = 0.0;
		integer = ((long (*)(double))function->function)(record->args[0]);
		break;
	case TEST_LL_D:
		result = 0.0;
		integer = ((long long (*)(double))function->function)(record->args[0]);
		break;
	case TEST_FREXP:
		result = ((double (*)(double, int *))function->function)(record->args[0], &exponent);
		integer = exponent;
		break;
	case TEST_FREXPF:
		result = ((float (*)(float, int *))function->function)((float)record->args[0], &exponent);
		integer = exponent;
		break;
	case TEST_MODF:
		result = ((double (*)(double, double *))function->function)(record->args[0], &second);
		break;
	case TEST_REMQUO:
		result = ((double (*)(double, double, int *))function->function)(record->args[0], record->args[1], &quotient);
		integer = quotient;
		break;
	case TEST_REMQUOF:
		result = ((float (*)(float, float, int *))function->function)((float)record->args[0], (float)record->args[1], &quotient);
		integer = quotient;
		break;
	default:
		return;
	}

	/* Counts the case. */
	function->count++;

	/* An exact case must match bit for bit, in every output. */
	if (record->mode == TEST_MODE_EXACT) {
		correct = same_double(result, record->ref_hi);
		matches = same_double(second, record->ref_lo);
		if (function->kind == TEST_MODF && !matches)
			correct = 0;
		if (integer != record->ref_int)
			correct = 0;

		/* Shows the first few mismatches and counts them all. */
		if (!correct) {
			if (function->exact_failures < 5) {
				printf("  %s(%.17g, %.17g, %.17g) = %.17g [%lld], expected %.17g [%lld]\n",
				    function->name, record->args[0], record->args[1],
				    record->args[2], result, (long long)integer,
				    record->ref_hi, (long long)record->ref_int);
			}

			/* Every mismatch counts, shown or not. */
			function->exact_failures++;
		}

		/* The exact case is done. */
		return;
	}

	/* Other cases are measured in ulps of the true value. */
	error = ulp_error(result, record->ref_hi, record->ref_lo,
	    record->mode == TEST_MODE_ULP_FLOAT);
	function->sum_ulp += error;
	if (error > 0.5)
		function->over_half++;
	if (error > function->max_ulp) {
		function->max_ulp = error;
		function->worst_args[0] = record->args[0];
		function->worst_args[1] = record->args[1];
		function->worst_args[2] = record->args[2];
	}
}

/*
 * Reports whether two doubles are the same value: equal encodings, or two
 * NaNs.
 */
static int
same_double(
	double left,
	double right)
{
	uint64_t left_bits;
	uint64_t right_bits;

	/* Any NaN matches any NaN. */
	if (left != left && right != right)
		return 1;

	/* Otherwise the encodings, which tell the zeros apart, must be equal. */
	memcpy(&left_bits, &left, 8);
	memcpy(&right_bits, &right, 8);
	if (left_bits == right_bits)
		return 1;

	/* The values differ. */
	return 0;
}

/*
 * Measures the error of a result in ulps of the true value high + low.
 */
static double
ulp_error(
	double value,
	double high,
	double low,
	int is_float)
{
	int exponent;
	int minimum;
	int precision;
	double unit;
	double difference;
	int matches;

	/* A zero, infinite or NaN true value must be met exactly. */
	if (high == 0.0 || high != high || high - high != 0.0) {
		matches = same_double(value, high);
		if (matches)
			return 0.0;

		/* Any other value is an unbounded error. */
		return 1e9;
	}

	/* A non-finite result for a finite true value is a failure. */
	if (value != value || value - value != 0.0)
		return 1e9;

	/* The ulp of the true value's binade, clamped at the subnormal one. */
	(void)frexp(high, &exponent);
	minimum = -1021;
	precision = 53;
	if (is_float) {
		minimum = -125;
		precision = 24;
	}

	/* Below the normal range the ulp stays that of the smallest binade. */
	if (exponent < minimum)
		exponent = minimum;
	unit = ldexp(1.0, exponent - precision);

	/* value - high is exact when they are close; low is then taken off. */
	difference = (value - high) - low;

	/* Succeeded: the error relative to the unit. */
	return fabs(difference) / unit;
}

/*
 * Prints the table and counts the functions outside their bounds.
 */
static int
report(
	const char *host_path)
{
	struct test_function *function;
	int failures;
	int index;
	int found;
	double host;
	char host_text[32];
	const char *verdict;

	/* One line per function that ran. */
	failures = 0;
	printf("%-12s %8s %10s %10s %8s %6s %10s  %s\n", "function", "cases",
	    "max ulp", "mean ulp", ">0.5", "exact", "host max", "worst input");
	for (index = 0; test_functions[index].name != NULL; index++) {
		function = &test_functions[index];
		if (function->count == 0)
			continue;

		/* A function fails on any exact mismatch or an error at the bound. */
		verdict = "";
		if (function->exact_failures != 0 || function->max_ulp >= TEST_ULP_BOUND) {
			verdict = "  FAIL";
			failures++;
		}

		/* The host's largest error on the same cases, when it is known. */
		found = host_error(host_path, function->name, &host);
		snprintf(host_text, sizeof(host_text), "-");
		if (found)
			snprintf(host_text, sizeof(host_text), "%.4f", host);

		/* Prints the line of the function. */
		printf("%-12s %8lu %10.4f %10.6f %8lu %6lu %10s  %.17g %.17g%s\n",
		    function->name, function->count, function->max_ulp,
		    function->sum_ulp / (double)function->count,
		    function->over_half, function->exact_failures, host_text,
		    function->worst_args[0], function->worst_args[1], verdict);
	}

	/* The number of functions outside their bounds. */
	return failures;
}

/*
 * Runs the special cases and counts the failures.
 */
static int
run_specials(void)
{
	const struct test_special *special;
	struct test_function *function;
	double result;
	int flags;
	int failures;
	int saved_errno;
	int index;
	int matches;

	/* Runs each case through the same dispatch as the reference records. */
	failures = 0;
	for (index = 0; test_specials[index].name != NULL; index++) {
		special = &test_specials[index];
		function = find_function(special->name);
		if (function == NULL)
			continue;

		/* Clears errno and the flags, calls, and reads both back. */
		errno = 0;
		feclearexcept(TEST_FE_ALL);
		switch (function->kind) {
		case TEST_D_D:
			result = ((double (*)(double))function->function)(special->args[0]);
			break;
		case TEST_D_DD:
			result = ((double (*)(double, double))function->function)(special->args[0], special->args[1]);
			break;
		case TEST_D_DDD:
			result = ((double (*)(double, double, double))function->function)(special->args[0], special->args[1], special->args[2]);
			break;
		case TEST_D_DI:
			result = ((double (*)(double, int))function->function)(special->args[0], (int)special->args[1]);
			break;
		default:
			continue;
		}

		/* Reads back what the call reported. */
		saved_errno = errno;
		flags = fetestexcept(TEST_FE_ALL) & TEST_FE_CHECKED;

		/* The value, errno and the required flags must all match. */
		matches = same_double(result, special->expected);
		if (!matches ||
		    saved_errno != special->expected_errno ||
		    flags != special->expected_flags) {
			printf("  special %s(%.17g, %.17g, %.17g) = %.17g errno %d flags %#x; expected %.17g errno %d flags %#x\n",
			    special->name, special->args[0], special->args[1],
			    special->args[2], result, saved_errno, flags,
			    special->expected, special->expected_errno,
			    special->expected_flags);
			failures++;
		}
	}

	/* Reports the count of special cases that failed. */
	printf("special cases: %d failed\n", failures);
	return failures;
}

/*
 * Looks up the host's largest error for a function in the generator's
 * side file, whose lines are "name error".
 */
static int
host_error(
	const char *host_path,
	const char *name,
	double *error)
{
	FILE *file;
	char line[128];
	char line_name[64];
	double value;
	int fields;
	int order;
	char *got;

	/* Without the side file nothing is known. */
	file = fopen(host_path, "r");
	if (file == NULL)
		return 0;

	/* Scans the lines for the name. */
	for (;;) {
		got = fgets(line, sizeof(line), file);
		if (got == NULL)
			break;

		/* Parses the line and compares its name. */
		fields = sscanf(line, "%63s %lf", line_name, &value);
		if (fields != 2)
			continue;

		/* The line of the function gives its error. */
		order = strcmp(line_name, name);
		if (order == 0) {
			fclose(file);
			*error = value;
			return 1;
		}
	}

	/* The function is not in the side file. */
	fclose(file);
	return 0;
}
