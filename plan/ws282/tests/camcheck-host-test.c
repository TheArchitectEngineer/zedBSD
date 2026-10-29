/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * camcheck's reading of the test pattern (userland/tests/camcheck/analyze.c)
 * on frames drawn here the way pattern.html draws them: the guess of the
 * matrix and range, the patches' error, the counter, and its run.
 */

#include "userland/tests/camcheck/analyze.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fractions pattern.html draws with that analyze.h does not name. */
#define TEST_STEP_TOP		0.34
#define TEST_STEP_BOTTOM	0.52
#define TEST_CHECKER_TOP	0.84

static unsigned failures;

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	if (!good) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Fills a rectangle of an RGB frame, given in fractions. */
static void
fill(
	uint8_t *rgb,
	uint32_t width,
	uint32_t height,
	double left,
	double top,
	double right,
	double bottom,
	const double *colour)
{
	uint32_t x;
	uint32_t y;
	uint8_t *pixel;

	for (y = (uint32_t)lround(top * height); y < (uint32_t)lround(bottom * height) && y < height; y++) {
		for (x = (uint32_t)lround(left * width); x < (uint32_t)lround(right * width) && x < width; x++) {
			pixel = rgb + ((size_t)y * width + x) * 3U;
			pixel[0] = (uint8_t)lround(colour[0]);
			pixel[1] = (uint8_t)lround(colour[1]);
			pixel[2] = (uint8_t)lround(colour[2]);
		}
	}
}

/* Draws the pattern with a counter value, as pattern.html does in YUYV mode. */
static void
draw(
	uint8_t *rgb,
	uint32_t width,
	uint32_t height,
	uint32_t counter)
{
	static const double black[3] = { 0.0, 0.0, 0.0 };
	static const double white[3] = { 255.0, 255.0, 255.0 };
	double colour[3];
	double grey[3];
	unsigned index;
	uint32_t x;
	uint32_t y;

	memset(rgb, 0, (size_t)width * height * 3U);
	for (index = 0U; index < CAMCHECK_PATCHES; index++) {
		camcheck_patch_expected(index, colour);
		fill(rgb, width, height, (double)index / CAMCHECK_PATCHES, CAMCHECK_PATCH_TOP,
		    (double)(index + 1U) / CAMCHECK_PATCHES, CAMCHECK_PATCH_BOTTOM, colour);
	}
	for (index = 0U; index < 8U; index++) {
		grey[0] = grey[1] = grey[2] = (double)index * 255.0 / 7.0;
		fill(rgb, width, height, index / 8.0, TEST_STEP_TOP, (index + 1U) / 8.0, TEST_STEP_BOTTOM, grey);
	}
	for (index = 0U; index < CAMCHECK_COUNTER_BITS; index++) {
		fill(rgb, width, height, (double)index / CAMCHECK_COUNTER_BITS, CAMCHECK_COUNTER_TOP,
		    (double)(index + 1U) / CAMCHECK_COUNTER_BITS, CAMCHECK_COUNTER_BOTTOM,
		    ((counter >> (CAMCHECK_COUNTER_BITS - 1U - index)) & 1U) != 0U ? white : black);
	}
	for (y = (uint32_t)(TEST_CHECKER_TOP * height); y < height; y++) {
		for (x = 0U; x < width; x++) {
			if (((x / 8U) + (y / 8U)) % 2U == 0U)
				memset(rgb + ((size_t)y * width + x) * 3U, 255, 3U);
		}
	}
}

/* Rounds a sample into a byte, as a device clamps it. */
static uint8_t
clamp(
	double sample)
{
	long rounded;

	rounded = lround(sample);
	if (rounded < 0L)
		return 0U;
	if (rounded > 255L)
		return 255U;
	return (uint8_t)rounded;
}

/* Encodes an RGB frame as YUYV in one matrix and range, the pair's chroma averaged. */
static void
encode(
	const uint8_t *rgb,
	uint32_t width,
	uint32_t height,
	uint32_t matrix,
	uint32_t range,
	uint8_t *yuyv)
{
	double colour[3];
	double first[3];
	double second[3];
	uint32_t x;
	uint32_t y;
	uint8_t *out;

	for (y = 0U; y < height; y++) {
		for (x = 0U; x + 1U < width; x += 2U) {
			colour[0] = rgb[((size_t)y * width + x) * 3U];
			colour[1] = rgb[((size_t)y * width + x) * 3U + 1U];
			colour[2] = rgb[((size_t)y * width + x) * 3U + 2U];
			camcheck_rgb_to_yuv(colour, matrix, range, first);
			colour[0] = rgb[((size_t)y * width + x + 1U) * 3U];
			colour[1] = rgb[((size_t)y * width + x + 1U) * 3U + 1U];
			colour[2] = rgb[((size_t)y * width + x + 1U) * 3U + 2U];
			camcheck_rgb_to_yuv(colour, matrix, range, second);
			out = yuyv + (size_t)y * width * 2U + (size_t)x * 2U;
			out[0] = clamp(first[0]);
			out[1] = clamp((first[1] + second[1]) / 2.0);
			out[2] = clamp(second[0]);
			out[3] = clamp((first[2] + second[2]) / 2.0);
		}
	}
}

/* Reads a YUYV frame as camcheck does: the guess, the patches as RGB, the counter. */
static void
read_yuyv(
	const uint8_t *yuyv,
	uint32_t width,
	uint32_t height,
	uint32_t *matrix,
	uint32_t *range,
	double *error_value,
	uint32_t *counter)
{
	struct camcheck_sample patches[CAMCHECK_PATCHES];
	struct camcheck_sample rgb_patches[CAMCHECK_PATCHES];
	struct camcheck_sample square;
	struct camcheck_region region;
	double brightness[CAMCHECK_COUNTER_BITS];
	unsigned worst;
	unsigned index;
	int error;

	for (index = 0U; index < CAMCHECK_PATCHES; index++) {
		camcheck_patch_region(index, &region);
		error = camcheck_sample_yuyv(yuyv, width, height, width * 2U, &region, &patches[index]);
		check(error == 0, "a patch is sampled");
	}
	camcheck_estimate(patches, matrix, range);
	for (index = 0U; index < CAMCHECK_PATCHES; index++)
		camcheck_yuv_to_rgb(patches[index].value, *matrix, *range, rgb_patches[index].value);
	*error_value = camcheck_patch_error(rgb_patches, &worst);
	for (index = 0U; index < CAMCHECK_COUNTER_BITS; index++) {
		camcheck_counter_region(index, &region);
		error = camcheck_sample_yuyv(yuyv, width, height, width * 2U, &region, &square);
		check(error == 0, "a counter square is sampled");
		brightness[index] = square.value[0];
	}
	*counter = camcheck_counter_decode(brightness, patches[CAMCHECK_PATCH_BLACK].value[0],
	    patches[CAMCHECK_PATCH_WHITE].value[0]);
}

/* Every matrix and range is guessed right and read back within a step; the counter is read. */
static void
test_yuyv(
	void)
{
	static const uint32_t sizes[][2] = { { 640U, 480U }, { 1280U, 720U }, { 320U, 240U } };
	static const uint32_t matrices[] = { CAMCHECK_MATRIX_BT601, CAMCHECK_MATRIX_BT709 };
	static const uint32_t ranges[] = { CAMCHECK_RANGE_LIMITED, CAMCHECK_RANGE_FULL };
	uint8_t *rgb;
	uint8_t *yuyv;
	double error_value;
	uint32_t matrix;
	uint32_t range;
	uint32_t counter;
	unsigned size;
	unsigned m;
	unsigned r;
	char what[128];

	for (size = 0U; size < sizeof(sizes) / sizeof(sizes[0]); size++) {
		rgb = malloc((size_t)sizes[size][0] * sizes[size][1] * 3U);
		yuyv = malloc((size_t)sizes[size][0] * sizes[size][1] * 2U);
		for (m = 0U; m < 2U; m++) {
			for (r = 0U; r < 2U; r++) {
				draw(rgb, sizes[size][0], sizes[size][1], 0xa5c3U + m + r);
				encode(rgb, sizes[size][0], sizes[size][1], matrices[m], ranges[r], yuyv);
				read_yuyv(yuyv, sizes[size][0], sizes[size][1], &matrix, &range, &error_value, &counter);
				snprintf(what, sizeof(what), "%ux%u matrix %u range %u guessed (got %u %u)", sizes[size][0],
				    sizes[size][1], matrices[m], ranges[r], matrix, range);
				check(matrix == matrices[m] && range == ranges[r], what);
				snprintf(what, sizeof(what), "%ux%u matrix %u range %u patches within 2 (%.2f)", sizes[size][0],
				    sizes[size][1], matrices[m], ranges[r], error_value);
				check(error_value <= 2.0, what);
				check(counter == 0xa5c3U + m + r, "the counter is read");
			}
		}
		free(rgb);
		free(yuyv);
	}
}

/* An RGB frame (a decoded MJPEG) with noise: the patches within the noise and the counter read. */
static void
test_rgb(
	void)
{
	struct camcheck_sample patches[CAMCHECK_PATCHES];
	struct camcheck_sample square;
	struct camcheck_region region;
	double brightness[CAMCHECK_COUNTER_BITS];
	double error_value;
	uint8_t *rgb;
	size_t index;
	unsigned worst;
	unsigned bit;
	int value;

	rgb = malloc(640U * 480U * 3U);
	draw(rgb, 640U, 480U, 0x0001U);
	srand(7);
	for (index = 0U; index < 640U * 480U * 3U; index++) {
		value = (int)rgb[index] + (rand() % 11) - 5;
		rgb[index] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
	}
	for (bit = 0U; bit < CAMCHECK_PATCHES; bit++) {
		camcheck_patch_region(bit, &region);
		check(camcheck_sample_rgb(rgb, 640U, 480U, &region, &patches[bit]) == 0, "an RGB patch is sampled");
	}
	error_value = camcheck_patch_error(patches, &worst);
	check(error_value <= 6.0, "RGB patches within the noise");
	for (bit = 0U; bit < CAMCHECK_COUNTER_BITS; bit++) {
		camcheck_counter_region(bit, &region);
		check(camcheck_sample_rgb(rgb, 640U, 480U, &region, &square) == 0, "an RGB square is sampled");
		brightness[bit] = (square.value[0] + square.value[1] + square.value[2]) / 3.0;
	}
	check(camcheck_counter_decode(brightness, 2.5, 252.5) == 0x0001U, "the RGB counter is read");

	/* A wrong colour is seen. */
	patches[CAMCHECK_PATCH_GREEN].value[0] = 40.0;
	error_value = camcheck_patch_error(patches, &worst);
	check(error_value >= 35.0 && worst == CAMCHECK_PATCH_GREEN, "a red cast in green is the worst");
	free(rgb);
}

/* The counter's run: steps, decreases (20 to 5, and 5 to 0xffff), and the 16-bit wrap (0xffff to 1). */
static void
test_counter(
	void)
{
	struct camcheck_counter_run run;

	memset(&run, 0, sizeof(run));
	camcheck_counter_add(&run, 10U);
	camcheck_counter_add(&run, 10U);
	camcheck_counter_add(&run, 11U);
	camcheck_counter_add(&run, 13U);
	camcheck_counter_add(&run, 20U);
	camcheck_counter_add(&run, 5U);
	camcheck_counter_add(&run, 0xffffU);
	camcheck_counter_add(&run, 1U);
	check(run.increments[0] == 1U && run.increments[1] == 1U && run.increments[2] == 2U, "steps 0, 1 and 2");
	check(run.increments[3] == 1U, "a step of 3 and more (13 to 20)");
	check(run.decreases == 2U, "two decreases");
	check(run.last == 1U, "the last value");
}

/* Regions outside the frame or of no pixel, and a short stride, are refused. */
static void
test_bounds(
	void)
{
	struct camcheck_sample sample;
	struct camcheck_region region;
	uint8_t frame[64];

	memset(frame, 0, sizeof(frame));
	region.left = 0.5;
	region.right = 0.5;
	region.top = 0.0;
	region.bottom = 1.0;
	check(camcheck_sample_rgb(frame, 4U, 4U, &region, &sample) == EINVAL, "an empty region");
	region.right = 1.5;
	check(camcheck_sample_rgb(frame, 4U, 4U, &region, &sample) == EINVAL, "a region past the frame");
	region.left = 0.0;
	region.right = 1.0;
	check(camcheck_sample_yuyv(frame, 4U, 4U, 7U, &region, &sample) == EINVAL, "a short stride");
	check(camcheck_sample_yuyv(frame, 4U, 4U, 8U, &region, &sample) == 0, "a whole small frame");
}

int
main(
	void)
{
	test_yuyv();
	test_rgb();
	test_counter();
	test_bounds();
	if (failures != 0U) {
		printf("camcheck-host-test: %u FAIL\n", failures);
		return EXIT_FAILURE;
	}
	printf("camcheck-host-test: PASS\n");
	return EXIT_SUCCESS;
}
