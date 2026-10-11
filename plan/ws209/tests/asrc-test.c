/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws209-p003: audiod's adaptive rate converter on the host (T2 of
 * plan/ws209/phase001/phase.md; userland/base/audiod/asrc.c): the taps,
 * the pass band (+-0.5 dB) and the stop band (-50 dB or lower) of 8 kHz,
 * 16 kHz and 44.1 kHz against 48 kHz both ways, the outputs' count, and
 * the controller holding a ring's fill against clocks 0.05% and 0.1%
 * apart for 10 simulated minutes, and bringing an 8 ms error back within
 * 2 ms in 10 s.  Prints a line for each failed check and "asrc: all PASS".
 */

#include "userland/base/audiod/asrc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The input and output of a measurement, a second each at most. */
#define TEST_FRAMES	96000U

static unsigned failures;
static float input[TEST_FRAMES * 2U];
static float output[TEST_FRAMES * 2U];

static void check(int good, const char *what);
static double level_db(uint32_t rate_in, uint32_t rate_out, double frequency);
static void test_bands(uint32_t low, const char *name);
static void test_count(void);
static double drift(double apart, double start_error_ms, unsigned seconds, double *worst_ms);

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	const char *measured;

	/* A failure is printed and counted; a measurement is printed either way. */
	if (!good) {
		printf("FAIL %s\n", what);
		failures++;
		return;
	}

	/* A measurement's line. */
	measured = strchr(what, ':');
	if (measured != NULL)
		printf("ok %s\n", what);
}

/*
 * Converts a second of a full-scale sine at a frequency and tells the
 * output's level in dB (its RMS over the second half, against the
 * input's).
 */
static double
level_db(
	uint32_t rate_in,
	uint32_t rate_out,
	double frequency)
{
	struct asrc converter;
	double sum;
	size_t made;
	size_t used;
	size_t index;
	int error;

	/* The sine. */
	for (index = 0U; index < rate_in; index++)
		input[index] = (float)sin(2.0 * M_PI * frequency * (double)index / (double)rate_in);

	/* Converted. */
	error = asrc_init(&converter, rate_in, rate_out, 1U);
	if (error != 0)
		return 999.0;
	made = asrc_process(&converter, input, rate_in, output, TEST_FRAMES, &used);

	/* Done with. */
	asrc_free(&converter);

	/* The RMS of the second half against a sine's 1/sqrt(2). */
	sum = 0.0;
	for (index = made / 2U; index < made; index++)
		sum += (double)output[index] * (double)output[index];
	sum /= (double)(made - made / 2U);

	/* Succeeded: dB. */
	return 10.0 * log10(sum / 0.5 + 1e-30);
}

/* The pass and stop bands of a low rate against 48 kHz, both ways. */
static void
test_bands(
	uint32_t low,
	const char *name)
{
	char what[128];
	double pass;
	double edge;
	double stop;
	double up_pass;
	double up_stop;

	/* Down: 48 kHz to the low rate; a tone at 0.2 and at 0.425 of it passes, one at 0.55 is gone. */
	pass = level_db(48000U, low, 0.2 * (double)low);
	edge = level_db(48000U, low, 0.425 * (double)low);
	stop = level_db(48000U, low, 0.55 * (double)low);
	snprintf(what, sizeof(what), "%s down: pass %.2f edge %.2f stop %.1f dB", name, pass, edge, stop);
	check(fabs(pass) <= 0.5 && fabs(edge) <= 0.5 && stop <= -50.0, what);

	/* Up: the low rate to 48 kHz; its band passes (the image above it is out of the input). */
	up_pass = level_db(low, 48000U, 0.2 * (double)low);
	up_stop = level_db(low, 48000U, 0.425 * (double)low);
	snprintf(what, sizeof(what), "%s up: pass %.2f edge %.2f dB", name, up_pass, up_stop);
	check(fabs(up_pass) <= 0.5 && fabs(up_stop) <= 0.5, what);
}

/* The outputs' count follows the ratio; the taps stay within the limit. */
static void
test_count(
	void)
{
	struct asrc converter;
	size_t made;
	size_t used;
	size_t total;
	size_t offset;
	unsigned taps;

	/* 8 kHz to 48 kHz in pieces of 80: about six outputs an input. */
	memset(input, 0, sizeof(input));
	(void)asrc_init(&converter, 8000U, 48000U, 2U);
	total = 0U;
	for (offset = 0U; offset < 8000U; offset += 80U) {
		made = asrc_process(&converter, input + offset * 2U, 80U, output, TEST_FRAMES / 2U, &used);
		total += made;
		check(used == 80U, "each piece taken");
	}

	/* The count. */
	asrc_free(&converter);
	check(total >= 47800U && total <= 48200U, "8 kHz to 48 kHz: six outputs an input (and the silent window's few)");

	/* The taps: 8 kHz down needs the most, within the limit. */
	taps = asrc_taps(48000U, 8000U);
	check(taps > 200U && taps <= ASRC_TAPS_MAX, "taps of 48 kHz to 8 kHz");
}

/*
 * Simulates a sink endpoint: audiod converts a master period (1024 frames
 * at 48 kHz) to 8 kHz into a ring each period, the endpoint's reader takes
 * 8 kHz times (1 + apart) a second, and the converter is steered by the
 * fill against a target of 40 ms each period.  Starts start_error_ms off
 * the target; returns the error at the end (ms) and the worst after the
 * first 10 s.
 */
static double
drift(
	double apart,
	double start_error_ms,
	unsigned seconds,
	double *worst_ms)
{
	static float silence[1024U];
	static float made_frames[4096U];
	struct asrc converter;
	double fill;
	double target;
	double now;
	double error_ms;
	double size;
	double period;
	size_t made;
	size_t used;
	unsigned periods;
	unsigned index;

	/* The converter and the ring's fill (frames at 8 kHz). */
	(void)asrc_init(&converter, 48000U, 8000U, 1U);
	target = 0.040 * 8000.0;
	fill = target + start_error_ms * 8.0;
	period = 1024.0 / 48000.0;
	periods = (unsigned)((double)seconds / period);
	*worst_ms = 0.0;
	error_ms = 0.0;

	/* Each period: converted in, read out, steered. */
	for (index = 0U; index < periods; index++) {
		made = asrc_process(&converter, silence, 1024U, made_frames, 4096U, &used);
		fill += (double)made;
		fill -= 8000.0 * (1.0 + apart) * period;
		asrc_steer(&converter, (fill - target) / target);
		error_ms = (fill - target) / 8.0;
		now = (double)index * period;
		size = fabs(error_ms);
		if (now >= 10.0 && size > *worst_ms)
			*worst_ms = size;
	}

	/* Done with. */
	asrc_free(&converter);

	/* Succeeded: the error at the end. */
	return error_ms;
}

/* Runs every check. */
int
main(
	void)
{
	char what[128];
	double end_ms;
	double worst_ms;

	/* The bands. */
	test_bands(8000U, "8 kHz");
	test_bands(16000U, "16 kHz");
	test_bands(44100U, "44.1 kHz");
	test_count();

	/* Clocks 0.05% and 0.1% apart, either way, for 10 minutes: within 10 ms after the first 10 s. */
	end_ms = drift(0.0005, 0.0, 600U, &worst_ms);
	snprintf(what, sizeof(what), "drift +0.05%%: end %.2f worst %.2f ms", end_ms, worst_ms);
	check(worst_ms <= 10.0, what);
	end_ms = drift(-0.001, 0.0, 600U, &worst_ms);
	snprintf(what, sizeof(what), "drift -0.1%%: end %.2f worst %.2f ms", end_ms, worst_ms);
	check(worst_ms <= 10.0, what);

	/* An 8 ms error back within 2 ms in 10 s. */
	end_ms = drift(0.0, 8.0, 10U, &worst_ms);
	snprintf(what, sizeof(what), "8 ms step: end %.2f ms after 10 s", end_ms);
	check(fabs(end_ms) <= 2.0, what);

	/* The summary. */
	if (failures != 0U) {
		printf("asrc: %u FAIL\n", failures);
		return EXIT_FAILURE;
	}

	/* Succeeded: every check passed. */
	printf("asrc: all PASS\n");
	return EXIT_SUCCESS;
}
