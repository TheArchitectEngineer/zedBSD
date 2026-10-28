/*
 * ws291: the CPU's view of a stall.  Reads CLOCK_MONOTONIC in a busy loop for SECONDS and reports every gap of
 * MIN_MS or more between two reads (the thread did not run: the kernel kept the CPU, or another thread did), with
 * its time from the start.  Run it beside a GPU load to tell a scheduling stall from a GPU or display one.
 *   usage: gap [SECONDS [MIN_MS]]   (default 20 5)
 * Built by plan/ws291/tests/gap-build.sh.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double
seconds_between(const struct timespec *a, const struct timespec *b)
{
	return (double)(b->tv_sec - a->tv_sec) + (double)(b->tv_nsec - a->tv_nsec) / 1e9;
}

int
main(int argc, char **argv)
{
	struct timespec start;
	struct timespec last;
	struct timespec now;
	double limit;
	double min_ms;
	double gap_ms;
	double longest;
	unsigned gaps;

	limit = argc > 1 ? atof(argv[1]) : 20.0;
	min_ms = argc > 2 ? atof(argv[2]) : 5.0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	last = start;
	gaps = 0U;
	longest = 0.0;
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		gap_ms = 1000.0 * seconds_between(&last, &now);
		if (gap_ms > longest)
			longest = gap_ms;
		if (gap_ms >= min_ms) {
			gaps++;
			if (gaps <= 60U)
				printf("GAP at=%.3f ms=%.2f\n", seconds_between(&start, &now), gap_ms);
		}
		last = now;
		if (seconds_between(&start, &now) >= limit)
			break;
	}
	printf("GAPS gaps=%u longest_ms=%.2f\n", gaps, longest);
	return 0;
}
