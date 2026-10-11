/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of audio-loop's arithmetic (ws204-p003): the levels of a
 * known sine, of known noise and of both, a DC offset, silence, a tone
 * made in two calls, and the WAV header.
 */

#include "userland/tests/audio-loop/measure.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The rate, the frames of one second, and the channels. */
#define TEST_RATE	48000U
#define TEST_FRAMES	48000U
#define TEST_CHANNELS	2U

int main(void);
static void test_near(const char *what, double value, double expected, double tolerance);
static void test_tone(int16_t *frames, double *mono);
static void test_noise(int16_t *frames, double *mono);
static void test_silence(double *mono);
static void test_halves(int16_t *frames);
static void test_wav(void);
static double test_random(unsigned *state);

/* The checks that failed. */
static unsigned test_failures;

/*
 * Runs the checks; exits 1 when any failed.
 */
int
main(
	void)
{
	int16_t *frames;
	double *mono;

	/* A second of stereo frames and its mono mix. */
	frames = calloc(TEST_FRAMES * TEST_CHANNELS, sizeof(int16_t));
	mono = calloc(TEST_FRAMES, sizeof(double));
	if (frames == NULL || mono == NULL) {
		printf("audio-loop measure: no memory\n");
		return 1;
	}

	/* The checks. */
	test_tone(frames, mono);
	test_noise(frames, mono);
	test_silence(mono);
	test_halves(frames);
	test_wav();
	free(frames);
	free(mono);

	/* The verdict. */
	if (test_failures != 0U) {
		printf("audio-loop measure: %u FAIL\n", test_failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("audio-loop measure: all PASS\n");
	return 0;
}

/* Checks that a value is within tolerance of what is expected. */
static void
test_near(
	const char *what,
	double value,
	double expected,
	double tolerance)
{
	double difference;

	/* Within the tolerance, or a failure. */
	difference = fabs(value - expected);
	if (difference > tolerance) {
		printf("FAIL %s: %.3f, expected %.3f +- %.3f\n", what, value, expected, tolerance);
		test_failures++;
		return;
	}

	/* Passed. */
	printf("ok %s: %.3f\n", what, value);
}

/* A 1 kHz sine at -20 dBFS: its tone level and RMS level read -20 dB, also with a DC offset and at 997 Hz. */
static void
test_tone(
	int16_t *frames,
	double *mono)
{
	double amplitude;
	size_t index;

	/* The sine. */
	amplitude = pow(10.0, -20.0 / 20.0);
	loop_tone(frames, TEST_FRAMES, TEST_CHANNELS, 0U, 1000.0, TEST_RATE, amplitude);
	loop_mono(frames, TEST_FRAMES, TEST_CHANNELS, mono);
	test_near("tone level", loop_tone_level(mono, TEST_FRAMES, 1000.0, TEST_RATE), -20.0, 0.1);
	test_near("tone rms", loop_rms_level(mono, TEST_FRAMES), -20.0, 0.1);
	test_near("tone peak", loop_peak(mono, TEST_FRAMES), amplitude, 0.001);

	/* Another frequency's level is far below. */
	test_near("tone at 3 kHz", loop_tone_level(mono, TEST_FRAMES, 3000.0, TEST_RATE), -150.0, 70.0);

	/* An offset changes neither level. */
	for (index = 0U; index < TEST_FRAMES; index++)
		mono[index] += 0.1;
	test_near("tone level with DC", loop_tone_level(mono, TEST_FRAMES, 1000.0, TEST_RATE), -20.0, 0.1);
	test_near("tone rms with DC", loop_rms_level(mono, TEST_FRAMES), -20.0, 0.1);

	/* A frequency that is not a whole number of cycles in the window. */
	loop_tone(frames, TEST_FRAMES, TEST_CHANNELS, 0U, 997.3, TEST_RATE, amplitude);
	loop_mono(frames, TEST_FRAMES, TEST_CHANNELS, mono);
	test_near("tone level at 997.3 Hz", loop_tone_level(mono, TEST_FRAMES, 997.3, TEST_RATE), -20.0, 0.2);
}

/*
 * Uniform noise at -60 dBFS RMS (against a full-scale sine): its RMS
 * level reads -60 dB, its level at 1 kHz is far lower (one narrow bin),
 * and a -20 dB tone in it still reads -20 dB.
 */
static void
test_noise(
	int16_t *frames,
	double *mono)
{
	double width;
	double tone[1];
	unsigned state;
	size_t index;

	/*
	 * Uniform noise in -w..w has an RMS of w / sqrt(3); -60 dBFS against
	 * a full-scale sine is an RMS of 0.001 / sqrt(2).
	 */
	width = 0.001 / sqrt(2.0) * sqrt(3.0);
	state = 12345U;
	for (index = 0U; index < TEST_FRAMES; index++)
		mono[index] = width * (2.0 * test_random(&state) - 1.0);
	test_near("noise rms", loop_rms_level(mono, TEST_FRAMES), -60.0, 0.3);
	test_near("noise at 1 kHz", loop_tone_level(mono, TEST_FRAMES, 1000.0, TEST_RATE), -95.0, 15.0);

	/* The tone added to the noise. */
	for (index = 0U; index < TEST_FRAMES; index++) {
		loop_tone(frames, 1U, TEST_CHANNELS, index, 1000.0, TEST_RATE, pow(10.0, -20.0 / 20.0));
		loop_mono(frames, 1U, TEST_CHANNELS, tone);
		mono[index] += tone[0];
	}
	test_near("tone in noise", loop_tone_level(mono, TEST_FRAMES, 1000.0, TEST_RATE), -20.0, 0.1);
}

/* Silence reads the floor and has no peak; too few samples read the floor. */
static void
test_silence(
	double *mono)
{
	/* All zero. */
	memset(mono, 0, TEST_FRAMES * sizeof(double));
	test_near("silence level", loop_tone_level(mono, TEST_FRAMES, 1000.0, TEST_RATE), LOOP_FLOOR_DB, 0.0);
	test_near("silence rms", loop_rms_level(mono, TEST_FRAMES), LOOP_FLOOR_DB, 0.0);
	test_near("silence peak", loop_peak(mono, TEST_FRAMES), 0.0, 0.0);
	test_near("one sample", loop_tone_level(mono, 1U, 1000.0, TEST_RATE), LOOP_FLOOR_DB, 0.0);
	test_near("no samples", loop_rms_level(mono, 0U), LOOP_FLOOR_DB, 0.0);
	test_near("full scale", loop_db(1.0), 0.0, 0.0);
}

/* A tone made in two calls is the tone made in one. */
static void
test_halves(
	int16_t *frames)
{
	int16_t *whole;
	int same;

	/* The tone in one call. */
	whole = calloc(TEST_FRAMES * TEST_CHANNELS, sizeof(int16_t));
	if (whole == NULL) {
		printf("FAIL halves: no memory\n");
		test_failures++;
		return;
	}
	loop_tone(whole, TEST_FRAMES, TEST_CHANNELS, 0U, 1000.0, TEST_RATE, 0.5);

	/* In two, the second going on from the first's end. */
	loop_tone(frames, 1234U, TEST_CHANNELS, 0U, 1000.0, TEST_RATE, 0.5);
	loop_tone(frames + 1234U * TEST_CHANNELS, TEST_FRAMES - 1234U, TEST_CHANNELS, 1234U, 1000.0, TEST_RATE, 0.5);
	same = memcmp(whole, frames, TEST_FRAMES * TEST_CHANNELS * sizeof(int16_t));
	free(whole);
	if (same != 0) {
		printf("FAIL halves: the two calls differ from one\n");
		test_failures++;
		return;
	}

	/* Passed. */
	printf("ok halves\n");
}

/* The WAV header of 10 stereo frames at 48 kHz. */
static void
test_wav(
	void)
{
	static const uint8_t expected[LOOP_WAV_HEADER] = {
		'R', 'I', 'F', 'F', 76, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
		0x80, 0xbb, 0, 0, 0x00, 0xee, 0x02, 0, 4, 0, 16, 0,
		'd', 'a', 't', 'a', 40, 0, 0, 0
	};
	uint8_t header[LOOP_WAV_HEADER];
	int same;

	/* The header against the canonical bytes. */
	loop_wav_header(header, 48000U, 2U, 10U);
	same = memcmp(header, expected, sizeof(header));
	if (same != 0) {
		printf("FAIL wav header\n");
		test_failures++;
		return;
	}

	/* Passed. */
	printf("ok wav header\n");
}

/* A repeatable random number in 0..1 (a linear congruential generator). */
static double
test_random(
	unsigned *state)
{
	/* The next state. */
	*state = *state * 1103515245U + 12345U;

	/* Its high bits as a fraction. */
	return (double)((*state >> 8) & 0xffffffU) / (double)0xffffffU;
}
