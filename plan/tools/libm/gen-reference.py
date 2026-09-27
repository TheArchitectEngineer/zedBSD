#!/usr/bin/env python3
"""Writes reference cases for the libm test runner (WS076).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    python3 plan/tools/libm/gen-reference.py OUT.bin [--count N] [--seed S]
        [--only NAME,NAME...]

Every record is 72 bytes, little-endian (the runner is built for amd64 on
the host and in the guest):

    char     name[16]   the function, NUL-padded
    int32    mode       0: exact (bitwise, NaN matches NaN)
                        1: ulp error of a double result
                        2: ulp error of a float result
    int32    reserved
    double   args[3]    the arguments (ints are stored as doubles)
    double   ref_hi     the correctly rounded result (mode 0) or the high
    double   ref_lo     part and the low part of the true value (modes 1, 2);
                        mode 0 uses ref_lo for a second double result
                        (modf's integral part)
    int64    ref_int    a second integer result (frexp's exponent, remquo's
                        quotient, the result of lrint and ilogb)

The true values come from MPFR through gmpy2 at 192 bits, from exact
rational arithmetic (fractions.Fraction) for the remainders and the
rounding functions, and from IEEE contexts of gmpy2 (gmpy2.ieee(64) and
gmpy2.ieee(32), which round subnormals like the hardware) for the
correctly rounded operations.  No libm source is involved.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import sys
from fractions import Fraction

import gmpy2
from gmpy2 import mpfr

RECORD = struct.Struct("<16sii3dddq")

EXACT = 0
ULP_DOUBLE = 1
ULP_FLOAT = 2

HIGH = gmpy2.context(precision=192, emin=-100000, emax=100000)
IEEE64 = gmpy2.ieee(64)
IEEE32 = gmpy2.ieee(32)

LONG_MIN = -(1 << 63)


def bits_double(bits: int) -> float:
	"""Returns the double with an encoding."""
	return struct.unpack("<d", struct.pack("<Q", bits))[0]


def to_float32(value: float) -> float:
	"""Rounds a double to the nearest float, as a Python float."""
	return struct.unpack("<f", struct.pack("<f", value))[0]


def random_double(rng: random.Random, low: int, high: int) -> float:
	"""A double with a uniformly chosen binade in [low, high] and sign."""
	exponent = rng.randint(low, high)
	if exponent < -1022:
		# A subnormal: fewer significant bits.
		width = exponent + 1074
		mantissa = rng.getrandbits(max(width, 1)) | (1 << max(width - 1, 0))
		value = math.ldexp(mantissa, -1074)
	else:
		mantissa = rng.getrandbits(52) | (1 << 52)
		value = math.ldexp(mantissa, exponent - 52)
	if rng.random() < 0.5:
		value = -value
	return value


def random_float(rng: random.Random, low: int, high: int) -> float:
	"""A float (as a Python float) with a chosen binade and sign."""
	exponent = rng.randint(low, high)
	if exponent < -126:
		width = exponent + 149
		mantissa = rng.getrandbits(max(width, 1)) | (1 << max(width - 1, 0))
		value = math.ldexp(mantissa, -149)
	else:
		mantissa = rng.getrandbits(23) | (1 << 23)
		value = math.ldexp(mantissa, exponent - 23)
	if rng.random() < 0.5:
		value = -value
	return value


def split(value) -> tuple[float, float]:
	"""Splits a high-precision value into a double and the rest."""
	with gmpy2.context(IEEE64):
		high = float(+value)
	if math.isinf(high) or high == 0.0:
		return high, 0.0
	with gmpy2.context(HIGH):
		rest = value - mpfr(high)
	with gmpy2.context(IEEE64):
		low = float(+rest)
	return high, low


def round_half_even(value: Fraction) -> int:
	"""The nearest integer to a rational, ties to even."""
	floor = value.numerator // value.denominator
	fraction = value - floor
	if fraction > Fraction(1, 2):
		return floor + 1
	if fraction < Fraction(1, 2):
		return floor
	return floor + (floor & 1)


def trunc_fraction(value: Fraction) -> int:
	"""The integer part of a rational, towards zero."""
	if value >= 0:
		return value.numerator // value.denominator
	return -((-value.numerator) // value.denominator)


def quotient_bits(quotient: int) -> int:
	"""The low 31 bits of a quotient's magnitude, with its sign (remquo)."""
	magnitude = abs(quotient) & 0x7fffffff
	if quotient < 0:
		return -magnitude
	return magnitude


def exact_remainder(x: float, y: float, nearest: bool) -> tuple[float, int]:
	"""fmod or remainder of two finite doubles, and the quotient."""
	quotient_value = Fraction(x) / Fraction(y)
	if nearest:
		quotient = round_half_even(quotient_value)
	else:
		quotient = trunc_fraction(quotient_value)
	rest = Fraction(x) - quotient * Fraction(y)
	result = float(rest)
	if result == 0.0:
		result = math.copysign(0.0, x)
	return result, quotient


class Writer:
	"""Collects records."""

	def __init__(self) -> None:
		self.records: list[bytes] = []
		self.counts: dict[str, int] = {}

	def add(self, name: str, mode: int, args, ref_hi: float, ref_lo: float = 0.0,
		ref_int: int = 0) -> None:
		padded = list(args) + [0.0] * (3 - len(args))
		self.records.append(RECORD.pack(name.encode(), mode, 0,
			float(padded[0]), float(padded[1]), float(padded[2]),
			ref_hi, ref_lo, ref_int))
		self.counts[name] = self.counts.get(name, 0) + 1


def gen_remainders(w: Writer, rng: random.Random, count: int) -> None:
	"""fmod, remainder and remquo, double and float."""
	pairs = [(1e17, 7.0), (2.0 ** 60, 7.0), (5.5, 2.0), (-5.5, 2.0), (6.5, 2.0),
		(7.5, 2.0), (1.0, 0.75), (-4.0, 3.0), (bits_double(0x7fefffffffffffff), 1e-300),
		(bits_double(0x7fefffffffffffff), bits_double(1)), (3.0 * 2.0 ** -1074, 2.0 ** -1073)]
	for _ in range(count):
		x = random_double(rng, -1074, 1023)
		if rng.random() < 0.5:
			y = random_double(rng, -1074, 1023)
		else:
			e = int(math.frexp(x)[1]) - rng.randint(0, 70)
			y = random_double(rng, max(e, -1074), max(e, -1074))
		if y != 0.0:
			pairs.append((x, y))
	for x, y in pairs:
		r, q = exact_remainder(x, y, False)
		w.add("fmod", EXACT, (x, y), r)
		r, q = exact_remainder(x, y, True)
		w.add("remainder", EXACT, (x, y), r)
		w.add("remquo", EXACT, (x, y), r, 0.0, quotient_bits(q))
	for _ in range(count):
		x = random_float(rng, -149, 127)
		y = random_float(rng, -149, 127)
		r, q = exact_remainder(x, y, False)
		w.add("fmodf", EXACT, (x, y), r)
		r, q = exact_remainder(x, y, True)
		w.add("remainderf", EXACT, (x, y), r)
		w.add("remquof", EXACT, (x, y), r, 0.0, quotient_bits(q))


def gen_fma(w: Writer, rng: random.Random, count: int) -> None:
	"""fma and fmaf, including heavy cancellation and subnormal results."""
	for index in range(count):
		kind = index % 4
		x = random_double(rng, -600, 600)
		y = random_double(rng, -600, 600)
		if kind == 0:
			z = random_double(rng, -1074, 1023)
		elif kind == 1:
			# z close to -x*y: cancellation.
			with gmpy2.context(IEEE64):
				z = -float(mpfr(x) * mpfr(y))
			z = z * (1.0 + rng.choice([0.0, 2.0 ** -52, -2.0 ** -53, 2.0 ** -30]))
		elif kind == 2:
			x = random_double(rng, -600, -500)
			y = random_double(rng, -600, -500)
			z = random_double(rng, -1074, -1000)
		else:
			z = random_double(rng, -1074, 1023)
			x = random_double(rng, 400, 600)
			y = random_double(rng, 400, 600)
		with gmpy2.context(IEEE64):
			result = float(gmpy2.fma(mpfr(x), mpfr(y), mpfr(z)))
		w.add("fma", EXACT, (x, y, z), result)
	for index in range(count):
		x = random_float(rng, -70, 60)
		y = random_float(rng, -70, 60)
		if index % 2:
			z = to_float32(-(x * y) * (1.0 + rng.choice([0.0, 2.0 ** -23, 2.0 ** -12])))
		else:
			z = random_float(rng, -149, 127)
		with gmpy2.context(IEEE32):
			result = float(gmpy2.fma(mpfr(x), mpfr(y), mpfr(z)))
		w.add("fmaf", EXACT, (x, y, z), result)


def gen_sqrt(w: Writer, rng: random.Random, count: int) -> None:
	"""sqrt and sqrtf over every binade, subnormals and squares."""
	values = [abs(random_double(rng, -1074, 1023)) for _ in range(count)]
	values += [float(n * n) for n in range(1, 200)]
	values += [bits_double(0x7fefffffffffffff), bits_double(1), 2.0 ** -1022]
	for x in values:
		with gmpy2.context(IEEE64):
			result = float(gmpy2.sqrt(mpfr(x)))
		w.add("sqrt", EXACT, (x,), result)
	for _ in range(count):
		x = abs(random_float(rng, -149, 127))
		with gmpy2.context(IEEE32):
			result = float(gmpy2.sqrt(mpfr(x)))
		w.add("sqrtf", EXACT, (x,), result)


def rounding_inputs(rng: random.Random, count: int) -> list[float]:
	"""Values around the integers: ties, near-ties and large magnitudes."""
	values = [0.5, -0.5, 1.5, 2.5, -2.5, 0.49999999999999994, 4503599627370495.5,
		-4503599627370495.5, 4503599627370497.0, 0.0, -0.0, 1e300, -1e-300]
	for _ in range(count):
		choice = rng.random()
		if choice < 0.3:
			values.append(rng.randint(-10 ** 6, 10 ** 6) + 0.5)
		else:
			values.append(random_double(rng, -5, 60))
	return values


def gen_rounding(w: Writer, rng: random.Random, count: int) -> None:
	"""trunc, floor, ceil, round, rint, nearbyint and the integer ones."""
	for x in rounding_inputs(rng, count):
		f = Fraction(x)
		down = math.floor(f)
		up = math.ceil(f)
		toward = trunc_fraction(f)
		if abs(f - toward) >= Fraction(1, 2):
			away = toward + (1 if f > 0 else -1)
		else:
			away = toward
		even = round_half_even(f)

		def signed(n: int) -> float:
			return math.copysign(float(n), x) if n == 0 else float(n)

		w.add("trunc", EXACT, (x,), signed(toward))
		w.add("floor", EXACT, (x,), signed(down))
		w.add("ceil", EXACT, (x,), signed(up))
		w.add("round", EXACT, (x,), signed(away))
		w.add("rint", EXACT, (x,), signed(even))
		w.add("nearbyint", EXACT, (x,), signed(even))
		if -(2 ** 63) <= even < 2 ** 63:
			w.add("lrint", EXACT, (x,), 0.0, 0.0, even)
			w.add("llrint", EXACT, (x,), 0.0, 0.0, even)
		if -(2 ** 63) <= away < 2 ** 63:
			w.add("lround", EXACT, (x,), 0.0, 0.0, away)
			w.add("llround", EXACT, (x,), 0.0, 0.0, away)
	for _ in range(count):
		x = to_float32(random_double(rng, -3, 30))
		f = Fraction(x)
		even = round_half_even(f)
		result = math.copysign(float(even), x) if even == 0 else float(even)
		w.add("rintf", EXACT, (x,), result)
		toward = trunc_fraction(f)
		result = math.copysign(float(toward), x) if toward == 0 else float(toward)
		w.add("truncf", EXACT, (x,), result)
		down = math.floor(f)
		result = math.copysign(float(down), x) if down == 0 else float(down)
		w.add("floorf", EXACT, (x,), result)


def gen_exponent(w: Writer, rng: random.Random, count: int) -> None:
	"""frexp, ldexp, scalbn, ilogb, logb, modf, nextafter."""
	for _ in range(count):
		x = random_double(rng, -1074, 1023)
		fraction, exponent = math.frexp(x)
		w.add("frexp", EXACT, (x,), fraction, 0.0, exponent)
		n = rng.randint(-2200, 2200)
		if rng.random() < 0.5:
			n = rng.randint(-60, 60)
		try:
			scaled = math.ldexp(x, n)
		except OverflowError:
			scaled = math.copysign(math.inf, x)
		w.add("ldexp", EXACT, (x, float(n)), scaled)
		w.add("scalbn", EXACT, (x, float(n)), scaled)
		w.add("ilogb", EXACT, (x,), 0.0, 0.0, exponent - 1)
		w.add("logb", EXACT, (x,), float(exponent - 1))
		fraction_part, integral_part = math.modf(x)
		w.add("modf", EXACT, (x,), fraction_part, integral_part)
		y = random_double(rng, -1074, 1023)
		w.add("nextafter", EXACT, (x, y), math.nextafter(x, y))
		xf = random_float(rng, -149, 127)
		fraction, exponent = math.frexp(xf)
		w.add("frexpf", EXACT, (xf,), fraction, 0.0, exponent)
		n = rng.randint(-300, 300)
		exact = mpfr(math.ldexp(xf, n))
		with gmpy2.context(IEEE32):
			scaled = float(+exact)
		w.add("ldexpf", EXACT, (xf, float(n)), scaled)
		yf = random_float(rng, -149, 127)
		if xf == yf:
			nxt = yf
		else:
			bits = struct.unpack("<I", struct.pack("<f", xf))[0]
			if xf == 0.0:
				nxt = math.copysign(math.ldexp(1.0, -149), yf)
			else:
				if (yf > xf) == (xf > 0):
					bits += 1
				else:
					bits -= 1
				nxt = struct.unpack("<f", struct.pack("<I", bits))[0]
		w.add("nextafterf", EXACT, (xf, yf), nxt)


GENERATORS = {
	"remainders": gen_remainders,
	"fma": gen_fma,
	"sqrt": gen_sqrt,
	"rounding": gen_rounding,
	"exponent": gen_exponent,
}


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("out")
	parser.add_argument("--count", type=int, default=20000)
	parser.add_argument("--seed", type=int, default=76)
	parser.add_argument("--only", default="")
	options = parser.parse_args()
	selected = list(GENERATORS)
	if options.only:
		selected = options.only.split(",")
	writer = Writer()
	for name in selected:
		rng = random.Random(f"{options.seed}-{name}")
		GENERATORS[name](writer, rng, options.count)
	with open(options.out, "wb") as out:
		out.write(b"".join(writer.records))
	total = sum(writer.counts.values())
	print(f"gen-reference: {total} records, {len(writer.counts)} functions -> {options.out}",
		file=sys.stderr)


if __name__ == "__main__":
	main()
