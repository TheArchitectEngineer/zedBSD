/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p026: the engine's number conversions (vm/number.c) against known
 * values and, on the host, against the C library's exact printf and strtod
 * for many random doubles (glibc converts exactly; zedBSD's libc is not the
 * reference, so the random comparisons run only when HOST_NUMBER_ORACLE is
 * given on the command line as "--oracle").
 *
 *   host-number [--oracle]
 */

#include "vm/vm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many random doubles the oracle comparisons use. */
#define HOST_NUMBER_RANDOM	200000

/* The checks run and failed. */
static int host_number_checks;
static int host_number_failures;

static void host_number_expect_text(double number, int radix, const char *expected);
static void host_number_expect_fixed(double number, int digits, const char *expected);
static void host_number_expect_exponential(double number, int digits, const char *expected);
static void host_number_expect_precision(double number, int precision, const char *expected);
static void host_number_expect_parse(const char *text, double expected);
static void host_number_compare(const char *what, const char *got, const char *expected, double number);
static double host_number_random(unsigned long long *state);
static void host_number_oracle(void);
static void host_number_check_shortest(double number);
static int host_number_same_exponential(const char *got, const char *expected);
static void host_number_check_reading(double number, int precision);
static void host_number_check_fixed(double number, int fixed);
static int host_number_is_tie(const char *exact, int fixed);

/*
 * Runs the checks.
 */
int
main(
	int argc,
	char **argv)
{
	int oracle;

	/* Number::toString in radix 10: integers, the fixed forms and the exponential ones. */
	host_number_expect_text(0.0, 10, "0");
	host_number_expect_text(-0.0, 10, "0");
	host_number_expect_text(1.0, 10, "1");
	host_number_expect_text(-42.0, 10, "-42");
	host_number_expect_text(0.1, 10, "0.1");
	host_number_expect_text(0.1 + 0.2, 10, "0.30000000000000004");
	host_number_expect_text(1.0 / 3.0, 10, "0.3333333333333333");
	host_number_expect_text(123456789012345680000.0, 10, "123456789012345680000");
	host_number_expect_text(1e21, 10, "1e+21");
	host_number_expect_text(1.5e21, 10, "1.5e+21");
	host_number_expect_text(0.000001, 10, "0.000001");
	host_number_expect_text(0.0000001, 10, "1e-7");
	host_number_expect_text(1.2345e-7, 10, "1.2345e-7");
	host_number_expect_text(5e-324, 10, "5e-324");
	host_number_expect_text(1.7976931348623157e308, 10, "1.7976931348623157e+308");
	host_number_expect_text(2.2250738585072014e-308, 10, "2.2250738585072014e-308");
	host_number_expect_text(9007199254740993.0, 10, "9007199254740992");
	host_number_expect_text(INFINITY, 10, "Infinity");
	host_number_expect_text(-INFINITY, 10, "-Infinity");
	host_number_expect_text(NAN, 10, "NaN");

	/* Other radixes (the expected texts are Chromium's). */
	host_number_expect_text(255.0, 16, "ff");
	host_number_expect_text(255.0, 2, "11111111");
	host_number_expect_text(-255.0, 36, "-73");
	host_number_expect_text(0.5, 2, "0.1");
	host_number_expect_text(0.1, 3, "0.0022002200220022002200220022002201");
	host_number_expect_text(1e21, 36, "5v1j4f4ds7c000");
	host_number_expect_text(1152921504606846977.0, 7, "2031000661631341064200");
	host_number_expect_text(123.456, 16, "7b.74bc6a7ef9dc");
	host_number_expect_text(1.0 / 3.0, 2, "0.010101010101010101010101010101010101010101010101010101");

	/* toFixed: a tie goes up; big and small values. */
	host_number_expect_fixed(0.5, 0, "1");
	host_number_expect_fixed(1.5, 0, "2");
	host_number_expect_fixed(2.5, 0, "3");
	host_number_expect_fixed(1.005, 2, "1.00");
	host_number_expect_fixed(1.255, 2, "1.25");
	host_number_expect_fixed(123.456, 2, "123.46");
	host_number_expect_fixed(0.000001, 2, "0.00");
	host_number_expect_fixed(-1.5, 0, "-2");
	host_number_expect_fixed(-0.0, 2, "0.00");
	host_number_expect_fixed(1000000000000000128.0, 0, "1000000000000000128");
	host_number_expect_fixed(0.1, 20, "0.10000000000000000555");

	/* toExponential: as many digits as needed, or a count. */
	host_number_expect_exponential(123456.0, -1, "1.23456e+5");
	host_number_expect_exponential(123456.0, 2, "1.23e+5");
	host_number_expect_exponential(0.0, 2, "0.00e+0");
	host_number_expect_exponential(0.00015, 1, "1.5e-4");
	host_number_expect_exponential(-1.25, 1, "-1.3e+0");
	host_number_expect_exponential(1e21, 3, "1.000e+21");

	/* toPrecision: fixed or exponential by the exponent. */
	host_number_expect_precision(123.456, 4, "123.5");
	host_number_expect_precision(0.000123, 2, "0.00012");
	host_number_expect_precision(1e21, 3, "1.00e+21");
	host_number_expect_precision(123456.0, 2, "1.2e+5");
	host_number_expect_precision(0.0, 3, "0.00");
	host_number_expect_precision(1.0e-7, 2, "1.0e-7");
	host_number_expect_precision(-5.5, 1, "-6");

	/* Reading decimal numerals: exact, halfway cases, and the edges of the doubles. */
	host_number_expect_parse("0", 0.0);
	host_number_expect_parse("1", 1.0);
	host_number_expect_parse("0.1", 0.1);
	host_number_expect_parse(".5", 0.5);
	host_number_expect_parse("5.", 5.0);
	host_number_expect_parse("1e3", 1000.0);
	host_number_expect_parse("1E-3", 0.001);
	host_number_expect_parse("9007199254740993", 9007199254740992.0);
	host_number_expect_parse("9007199254740995", 9007199254740996.0);
	host_number_expect_parse("1.7976931348623157e308", 1.7976931348623157e308);
	host_number_expect_parse("1.7976931348623159e308", INFINITY);
	host_number_expect_parse("4.9406564584124654e-324", 5e-324);
	host_number_expect_parse("2.4703282292062328e-324", 5e-324);
	host_number_expect_parse("2.4703282292062327e-324", 0.0);
	host_number_expect_parse("2.2250738585072011e-308", 2.225073858507201e-308);
	host_number_expect_parse("0.000000000000000000000000000000000000000000001e45", 1.0);
	host_number_expect_parse("1e400", INFINITY);
	host_number_expect_parse("1e-400", 0.0);

	/* The comparisons with the C library, on the host. */
	oracle = 0;
	if (argc > 1)
		oracle = strcmp(argv[1], "--oracle") == 0;
	if (oracle)
		host_number_oracle();

	/* The totals. */
	printf("host-number: %d checks, %d failed\n", host_number_checks, host_number_failures);
	if (host_number_failures != 0)
		return 1;

	/* Succeeded: every check held. */
	return 0;
}

/* Checks Number::toString. */
static void
host_number_expect_text(
	double number,
	int radix,
	const char *expected)
{
	struct wb_buffer out;

	/* The text, compared. */
	wb_buffer_init(&out);
	vm_number_to_text(number, radix, &out);
	host_number_compare("toString", wb_buffer_string(&out), expected, number);
	wb_buffer_release(&out);
}

/* Checks toFixed. */
static void
host_number_expect_fixed(
	double number,
	int digits,
	const char *expected)
{
	struct wb_buffer out;

	/* The text, compared. */
	wb_buffer_init(&out);
	vm_number_to_fixed(number, digits, &out);
	host_number_compare("toFixed", wb_buffer_string(&out), expected, number);
	wb_buffer_release(&out);
}

/* Checks toExponential. */
static void
host_number_expect_exponential(
	double number,
	int digits,
	const char *expected)
{
	struct wb_buffer out;

	/* The text, compared. */
	wb_buffer_init(&out);
	vm_number_to_exponential(number, digits, &out);
	host_number_compare("toExponential", wb_buffer_string(&out), expected, number);
	wb_buffer_release(&out);
}

/* Checks toPrecision. */
static void
host_number_expect_precision(
	double number,
	int precision,
	const char *expected)
{
	struct wb_buffer out;

	/* The text, compared. */
	wb_buffer_init(&out);
	vm_number_to_precision(number, precision, &out);
	host_number_compare("toPrecision", wb_buffer_string(&out), expected, number);
	wb_buffer_release(&out);
}

/* Checks reading a numeral (bit for bit). */
static void
host_number_expect_parse(
	const char *text,
	double expected)
{
	double got;
	int differs;

	/* The number, compared by its bits. */
	host_number_checks++;
	got = vm_number_parse(text, strlen(text));
	differs = memcmp(&got, &expected, sizeof(got));
	if (differs != 0) {
		host_number_failures++;
		printf("FAIL parse %s: got %.17g, expected %.17g\n", text, got, expected);
	}
}

/* Compares a text with the expected one. */
static void
host_number_compare(
	const char *what,
	const char *got,
	const char *expected,
	double number)
{
	int differs;

	/* One check. */
	host_number_checks++;
	differs = strcmp(got, expected);
	if (differs != 0) {
		host_number_failures++;
		printf("FAIL %s of %.17g: got %s, expected %s\n", what, number, got, expected);
	}
}

/* Makes a random finite double of any exponent (xorshift on the bits). */
static double
host_number_random(
	unsigned long long *state)
{
	unsigned long long bits;
	double number;

	/* Random bits until they are a finite double (NaN and the infinities have all exponent bits set). */
	do {
		*state ^= *state << 13;
		*state ^= *state >> 7;
		*state ^= *state << 17;
		bits = *state;
		memcpy(&number, &bits, sizeof(number));
	} while ((bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL);

	/* The double. */
	return number;
}

/* Tells whether an exact decimal text rounds to fixed digits on a tie: a 5 then only zeros after them. */
static int
host_number_is_tie(
	const char *exact,
	int fixed)
{
	const char *point;
	const char *rest;

	/* The digit after the kept ones. */
	point = strchr(exact, '.');
	if (point == NULL)
		return 0;
	rest = point + 1 + fixed;
	if (*rest != '5')
		return 0;

	/* Only zeros after it. */
	for (rest++; *rest != '\0'; rest++) {
		if (*rest != '0')
			return 0;
	}

	/* A tie. */
	return 1;
}

/* Compares the shortest digits, the fixed and exponential forms and the reading of numerals with glibc. */
static void
host_number_oracle(void)
{
	unsigned long long state;
	double number;
	int index;

	/* Many random doubles. */
	state = 0x9E3779B97F4A7C15ULL;
	for (index = 0; index < HOST_NUMBER_RANDOM; index++) {
		number = fabs(host_number_random(&state));
		host_number_check_shortest(number);
		host_number_check_reading(number, (int)(state % 17U));

		/* toFixed of values below 1e21. */
		if (number < 1e21)
			host_number_check_fixed(number, (int)(state % 21U));
	}
}

/* Checks the shortest digits: the shortest %e that reads back is what toExponential() without digits writes. */
static void
host_number_check_shortest(
	double number)
{
	struct wb_buffer out;
	char expected[64];
	char got[64];
	double read;
	int precision;
	int same;

	/* The fewest digits of %e that read back as the number. */
	for (precision = 0; precision < 17; precision++) {
		snprintf(expected, sizeof(expected), "%.*e", precision, number);
		read = strtod(expected, NULL);
		if (read == number)
			break;
	}

	/* The engine's form. */
	wb_buffer_init(&out);
	vm_number_to_exponential(number, -1, &out);
	snprintf(got, sizeof(got), "%s", wb_buffer_string(&out));
	wb_buffer_release(&out);

	/* glibc writes e+05 where the language writes e+5: the mantissas and the exponents' values are compared. */
	host_number_checks++;
	same = host_number_same_exponential(got, expected);
	if (!same) {
		host_number_failures++;
		printf("FAIL shortest of %.17g: got %s, expected %s\n", number, got, expected);
	}
}

/* Tells whether two exponential forms have the same mantissa text and the same exponent value. */
static int
host_number_same_exponential(
	const char *got,
	const char *expected)
{
	const char *got_e;
	const char *expected_e;
	int differs;
	long got_exponent;
	long expected_exponent;

	/* Both have an exponent. */
	got_e = strchr(got, 'e');
	expected_e = strchr(expected, 'e');
	if (got_e == NULL || expected_e == NULL)
		return 0;

	/* The same mantissa. */
	if (got_e - got != expected_e - expected)
		return 0;
	differs = strncmp(got, expected, (size_t)(expected_e - expected));
	if (differs != 0)
		return 0;

	/* The same exponent. */
	got_exponent = strtol(got_e + 1, NULL, 10);
	expected_exponent = strtol(expected_e + 1, NULL, 10);
	if (got_exponent != expected_exponent)
		return 0;

	/* The same number. */
	return 1;
}

/* Checks reading numerals: the %.17e form reads back exactly, and a shorter rounded form reads as strtod reads it. */
static void
host_number_check_reading(
	double number,
	int precision)
{
	char text[64];
	double read;
	double expected;
	int differs;

	/* The %.17e form. */
	snprintf(text, sizeof(text), "%.17e", number);
	read = vm_number_parse(text, strlen(text));
	host_number_checks++;
	differs = memcmp(&read, &number, sizeof(read));
	if (differs != 0) {
		host_number_failures++;
		printf("FAIL parse %s: got %.17g\n", text, read);
	}

	/* A shorter form. */
	snprintf(text, sizeof(text), "%.*e", precision, number);
	read = vm_number_parse(text, strlen(text));
	expected = strtod(text, NULL);
	host_number_checks++;
	if (read != expected) {
		host_number_failures++;
		printf("FAIL parse %s: got %.17g, expected %.17g\n", text, read, expected);
	}
}

/* Checks toFixed against %f (glibc's %f is exact but breaks a tie to even, the language up: ties are skipped). */
static void
host_number_check_fixed(
	double number,
	int fixed)
{
	struct wb_buffer out;
	char expected[1200];
	int tie;

	/* Whether the exact value is a tie at that many digits. */
	snprintf(expected, sizeof(expected), "%.*f", fixed + 1100, number);
	tie = host_number_is_tie(expected, fixed);
	if (tie)
		return;

	/* The two texts. */
	snprintf(expected, sizeof(expected), "%.*f", fixed, number);
	wb_buffer_init(&out);
	vm_number_to_fixed(number, fixed, &out);
	host_number_compare("toFixed", wb_buffer_string(&out), expected, number);
	wb_buffer_release(&out);
}
