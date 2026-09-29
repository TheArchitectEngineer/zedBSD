/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws035-p129: renders every layer of the Kei mark with the mark.c under test
 * and with a reference mark.c (built with keiland_mark_raster renamed to
 * reference_mark_raster), and reports every pixel that differs and how long
 * each took on the host.  run-mark-compare.sh builds and runs it.
 */

#include "mark.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void reference_mark_raster(unsigned layer, unsigned pixels, uint8_t *coverage, size_t stride);

/* The sizes the programs draw the mark at, and a few others. */
static const unsigned compare_sizes[] = { 14U, 26U, 48U, 64U, 96U, 128U, 200U };

static double compare_seconds(void);

/*
 * Compares the two renderings of every layer at every size.
 */
int
main(
	void)
{
	static uint8_t tested[256U * 256U];
	static uint8_t reference[256U * 256U];
	unsigned size_index;
	unsigned pixels;
	unsigned layer;
	unsigned differing;
	size_t at;
	double tested_seconds;
	double reference_seconds;
	double start;
	int status;

	/* Every size and layer. */
	status = 0;
	tested_seconds = 0.0;
	reference_seconds = 0.0;
	for (size_index = 0; size_index < sizeof(compare_sizes) / sizeof(compare_sizes[0]); size_index++) {
		pixels = compare_sizes[size_index];
		for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
			/* Both renderings, timed. */
			memset(tested, 0, sizeof(tested));
			memset(reference, 0, sizeof(reference));
			start = compare_seconds();
			keiland_mark_raster(layer, pixels, tested, pixels);
			tested_seconds += compare_seconds() - start;
			start = compare_seconds();
			reference_mark_raster(layer, pixels, reference, pixels);
			reference_seconds += compare_seconds() - start;

			/* The pixels that differ. */
			differing = 0;
			for (at = 0; at < (size_t)pixels * pixels; at++) {
				if (tested[at] != reference[at])
					differing++;
			}
			if (differing != 0U) {
				printf("size=%u layer=%u differing=%u FAIL\n", pixels, layer, differing);
				status = 1;
			}
		}
	}

	/* The times and the verdict. */
	printf("tested %.3f s, reference %.3f s\n", tested_seconds, reference_seconds);
	printf("mark-compare: %s\n", status == 0 ? "PASS" : "FAIL");
	return status;
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
