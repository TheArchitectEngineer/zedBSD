/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks the integer arithmetic of CUBIC's window function
 * (src/kern/net/tcp-cubic.c) against the same function in double on a grid:
 * W_max from 4 to 8000 segments, segment sizes from 536 to 8960 bytes,
 * cwnd from 0.5 to 1 W_max (and past it), t from 0 to 200 s.  K must be
 * within 1 ms of the double's, the window within one segment.  Also the
 * cube root at the edges of 64 bits.  Prints PASS or the first misses.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "src/kern/net/tcp-cubic.h"

/* The segment sizes and the windows (in segments) of the grid. */
static const uint32_t sizes[] = { 536U, 1220U, 1440U, 1460U, 4096U, 8960U };
static const uint32_t windows[] = { 4U, 10U, 37U, 100U, 428U, 1000U, 3000U, 8000U };

/* Runs the grid and reports whether every point agreed. */
int
main(
	void)
{
	unsigned misses;
	unsigned points;
	unsigned size_index;
	unsigned window_index;
	unsigned share;
	uint32_t mss;
	uint32_t w_max;
	uint32_t cwnd;
	uint32_t k_ms;
	uint32_t window;
	uint64_t t_ms;
	uint64_t value;
	uint32_t root;
	double k_ref;
	double w_ref;
	double t;

	misses = 0;
	points = 0;

	/* The cube root at small values and at the top of 64 bits. */
	for (value = 0; value < 100000U; value++) {
		root = tcp_cubic_root(value);
		if ((uint64_t)root * root * root > value || (uint64_t)(root + 1U) * (root + 1U) * (root + 1U) <= value)
			misses++;
	}
	root = tcp_cubic_root(UINT64_MAX);
	if (root != 2642245U)
		misses++;

	/* Each point of the grid. */
	for (size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]); size_index++) {
		mss = sizes[size_index];
		for (window_index = 0; window_index < sizeof(windows) / sizeof(windows[0]); window_index++) {
			w_max = windows[window_index] * mss;
			for (share = 50U; share <= 110U; share += 5U) {
				cwnd = (uint32_t)((uint64_t)w_max * share / 100U);
				k_ms = tcp_cubic_k_ms(w_max, cwnd, mss);
				k_ref = 0.0;
				if (cwnd < w_max)
					k_ref = cbrt(((double)(w_max - cwnd) / mss) / 0.4) * 1000.0;
				if (fabs((double)k_ms - k_ref) > 1.0) {
					if (misses < 10U)
						printf("miss: K mss=%u w_max=%u cwnd=%u: %u ms, double %.3f\n", mss, w_max, cwnd, k_ms,
						    k_ref);
					misses++;
				}
				for (t_ms = 0; t_ms <= 200000U; t_ms += 997U) {
					window = tcp_cubic_window(w_max, k_ms, t_ms, mss);
					t = (double)t_ms / 1000.0 - (double)k_ms / 1000.0;
					w_ref = 0.4 * t * t * t * mss + w_max;
					if (w_ref < 0.0)
						w_ref = 0.0;
					if (w_ref > (double)TCP_CUBIC_WINDOW_MAX)
						w_ref = (double)TCP_CUBIC_WINDOW_MAX;
					points++;
					if (fabs((double)window - w_ref) > (double)mss) {
						if (misses < 10U)
							printf("miss: W mss=%u w_max=%u K=%u t=%llu: %u, double %.1f\n", mss, w_max,
							    k_ms, (unsigned long long)t_ms, window, w_ref);
						misses++;
					}
				}
			}
		}
	}

	/* The result. */
	printf("cubic-host-test: %u points, %u misses: %s\n", points, misses, misses == 0 ? "PASS" : "FAIL");
	if (misses != 0)
		return 1;
	return 0;
}
