/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The accurate integer inverse DCT: the Loeffler, Ligtenberg and
 * Moschytz algorithm with 13-bit constants in 64-bit arithmetic (libjpeg's
 * JLONG), in two passes (columns, then
 * rows) with 2 extra bits kept between them, as libjpeg's "islow" method
 * computes it, so that the samples are the ones libjpeg gives.  The
 * results are clamped to 0..255 around 128 with libjpeg's range limit
 * (whose masking also folds values far out of range).
 */

#include "internal.h"

/* The fraction bits of the constants, and the extra bits kept after the first pass. */
#define IDCT_CONST_BITS		13
#define IDCT_PASS1_BITS		2

/* The constants: FIX(x) = x * 2^13, rounded. */
#define FIX_0_298631336		((int32_t)2446)
#define FIX_0_390180644		((int32_t)3196)
#define FIX_0_541196100		((int32_t)4433)
#define FIX_0_765366865		((int32_t)6270)
#define FIX_0_899976223		((int32_t)7373)
#define FIX_1_175875602		((int32_t)9633)
#define FIX_1_501321110		((int32_t)12299)
#define FIX_1_847759065		((int32_t)15137)
#define FIX_1_961570560		((int32_t)16069)
#define FIX_2_053119869		((int32_t)16819)
#define FIX_2_562915447		((int32_t)20995)
#define FIX_3_072711026		((int32_t)25172)

/* The bits of the range limit's index (values are folded modulo 1024). */
#define IDCT_RANGE_MASK		1023

/*
 * The eight outputs of one 8-point pass: the even part's four sums and
 * the odd part's four terms, combined as out[k] = even[k] +/- odd.
 */
struct idct_terms {
	int64_t tmp10;
	int64_t tmp11;
	int64_t tmp12;
	int64_t tmp13;
	int64_t tmp0;
	int64_t tmp1;
	int64_t tmp2;
	int64_t tmp3;
};

static void idct_pass(int64_t in0, int64_t in1, int64_t in2, int64_t in3, int64_t in4, int64_t in5, int64_t in6, int64_t in7, struct idct_terms *terms);
static int32_t idct_descale(int64_t value, int bits);
static JSAMPLE idct_limit(int32_t value);

/*
 * Transforms one block: the coefficients (natural order) times the
 * quantization (natural order) into 8 rows of 8 samples, stride bytes
 * apart.
 */
void
jpeg_compat_idct(
	const JCOEF *coefficients,
	const UINT16 *quantization,
	JSAMPLE *out,
	size_t stride)
{
	struct idct_terms terms;
	int32_t work[DCTSIZE2];
	int32_t in[DCTSIZE];
	int32_t value;
	const int32_t *row;
	JSAMPLE *line;
	int column;
	int index;
	int zero;

	/* Pass 1: each column, dequantized, into the work array with PASS1_BITS more bits. */
	for (column = 0; column < DCTSIZE; column++) {
		zero = 1;
		for (index = 0; index < DCTSIZE; index++) {
			in[index] = (int32_t)coefficients[index * DCTSIZE + column] * (int32_t)quantization[index * DCTSIZE + column];
			if (index != 0 && in[index] != 0)
				zero = 0;
		}

		/* A column of DC alone is flat (the same as the full pass gives). */
		if (zero) {
			value = in[0] * (1 << IDCT_PASS1_BITS);
			for (index = 0; index < DCTSIZE; index++)
				work[index * DCTSIZE + column] = value;
			continue;
		}

		/* The 8-point transform, descaled to CONST_BITS - PASS1_BITS. */
		idct_pass(in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], &terms);
		work[0 * DCTSIZE + column] = idct_descale(terms.tmp10 + terms.tmp3, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[7 * DCTSIZE + column] = idct_descale(terms.tmp10 - terms.tmp3, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[1 * DCTSIZE + column] = idct_descale(terms.tmp11 + terms.tmp2, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[6 * DCTSIZE + column] = idct_descale(terms.tmp11 - terms.tmp2, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[2 * DCTSIZE + column] = idct_descale(terms.tmp12 + terms.tmp1, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[5 * DCTSIZE + column] = idct_descale(terms.tmp12 - terms.tmp1, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[3 * DCTSIZE + column] = idct_descale(terms.tmp13 + terms.tmp0, IDCT_CONST_BITS - IDCT_PASS1_BITS);
		work[4 * DCTSIZE + column] = idct_descale(terms.tmp13 - terms.tmp0, IDCT_CONST_BITS - IDCT_PASS1_BITS);
	}

	/* Pass 2: each row of the work array into samples, descaled by 8 and the PASS1_BITS. */
	for (index = 0; index < DCTSIZE; index++) {
		row = &work[index * DCTSIZE];
		line = out + (size_t)index * stride;
		idct_pass(row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7], &terms);
		line[0] = idct_limit(idct_descale(terms.tmp10 + terms.tmp3, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[7] = idct_limit(idct_descale(terms.tmp10 - terms.tmp3, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[1] = idct_limit(idct_descale(terms.tmp11 + terms.tmp2, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[6] = idct_limit(idct_descale(terms.tmp11 - terms.tmp2, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[2] = idct_limit(idct_descale(terms.tmp12 + terms.tmp1, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[5] = idct_limit(idct_descale(terms.tmp12 - terms.tmp1, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[3] = idct_limit(idct_descale(terms.tmp13 + terms.tmp0, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
		line[4] = idct_limit(idct_descale(terms.tmp13 - terms.tmp0, IDCT_CONST_BITS + IDCT_PASS1_BITS + 3));
	}
}

/* One 8-point inverse transform: the even part from inputs 0, 2, 4, 6, the odd part from 1, 3, 5, 7. */
static void
idct_pass(
	int64_t in0,
	int64_t in1,
	int64_t in2,
	int64_t in3,
	int64_t in4,
	int64_t in5,
	int64_t in6,
	int64_t in7,
	struct idct_terms *terms)
{
	int64_t z1;
	int64_t z2;
	int64_t z3;
	int64_t z4;
	int64_t z5;
	int64_t tmp0;
	int64_t tmp1;
	int64_t tmp2;
	int64_t tmp3;

	/* The even part: the rotation of 2 and 6, and the butterfly of 0 and 4. */
	z1 = (in2 + in6) * FIX_0_541196100;
	tmp2 = z1 + in6 * -FIX_1_847759065;
	tmp3 = z1 + in2 * FIX_0_765366865;
	tmp0 = (in0 + in4) * (1 << IDCT_CONST_BITS);
	tmp1 = (in0 - in4) * (1 << IDCT_CONST_BITS);
	terms->tmp10 = tmp0 + tmp3;
	terms->tmp13 = tmp0 - tmp3;
	terms->tmp11 = tmp1 + tmp2;
	terms->tmp12 = tmp1 - tmp2;

	/* The odd part: 7, 5, 3 and 1 through the shared rotation. */
	tmp0 = in7;
	tmp1 = in5;
	tmp2 = in3;
	tmp3 = in1;
	z1 = tmp0 + tmp3;
	z2 = tmp1 + tmp2;
	z3 = tmp0 + tmp2;
	z4 = tmp1 + tmp3;
	z5 = (z3 + z4) * FIX_1_175875602;
	tmp0 = tmp0 * FIX_0_298631336;
	tmp1 = tmp1 * FIX_2_053119869;
	tmp2 = tmp2 * FIX_3_072711026;
	tmp3 = tmp3 * FIX_1_501321110;
	z1 = z1 * -FIX_0_899976223;
	z2 = z2 * -FIX_2_562915447;
	z3 = z3 * -FIX_1_961570560 + z5;
	z4 = z4 * -FIX_0_390180644 + z5;
	terms->tmp0 = tmp0 + z1 + z3;
	terms->tmp1 = tmp1 + z2 + z4;
	terms->tmp2 = tmp2 + z2 + z3;
	terms->tmp3 = tmp3 + z1 + z4;
}

/* Divides by 2^bits, rounding half up (libjpeg's DESCALE with an arithmetic shift). */
static int32_t
idct_descale(
	int64_t value,
	int bits)
{
	/* The rounded quotient (it fits a sample's range after the passes, or folds as libjpeg's does). */
	return (int32_t)((value + ((int64_t)1 << (bits - 1))) >> bits);
}

/* Clamps a centred value to a sample as libjpeg's range-limit table does (folded modulo 1024). */
static JSAMPLE
idct_limit(
	int32_t value)
{
	int32_t folded;

	/* 0..127 above the centre, the rest of the top saturated, the far bottom saturated, and -128..-1. */
	folded = value & IDCT_RANGE_MASK;
	if (folded < 128)
		return (JSAMPLE)(folded + 128);
	if (folded < 384)
		return 255;
	if (folded < 896)
		return 0;

	/* A value from -128 to -1. */
	return (JSAMPLE)(folded - 896);
}
