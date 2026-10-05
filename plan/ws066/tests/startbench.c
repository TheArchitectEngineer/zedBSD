/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws066-p001: the time to start a program and wait for it.
 *
 *     startbench COUNT LABEL PROGRAM [ARGUMENT...]
 *
 * Runs PROGRAM COUNT times with posix_spawn and waitpid, after a fifth as
 * many runs to warm the caches, and prints one line:
 *
 *     STARTBENCH label=LABEL runs=COUNT min=N median=N mean=N us
 *
 * The environment is passed on, so LD_LIBRARY_PATH chooses a library to
 * compare.  A run that does not exit with 0 ends the program with 1.
 */

#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>

/* The most runs one measurement takes. */
#define STARTBENCH_MAX_RUNS 100000L

extern char **environ;

int main(int argc, char **argv);
static long run_once(char **command);
static int compare_times(const void *left, const void *right);

/*
 * Measures the start of one program.
 */
int
main(
	int argc,
	char **argv)
{
	long *times;
	long count;
	long warm;
	long total;
	long elapsed;
	long i;

	/* The count, the label and the command. */
	if (argc < 4) {
		fprintf(stderr, "usage: startbench COUNT LABEL PROGRAM [ARGUMENT...]\n");
		return 2;
	}

	/* Refuses a count that is not a number in range. */
	count = strtol(argv[1], NULL, 10);
	if (count < 1 || count > STARTBENCH_MAX_RUNS) {
		fprintf(stderr, "startbench: bad count %s\n", argv[1]);
		return 2;
	}

	/* The table of the run times. */
	times = calloc((size_t)count, sizeof(*times));
	if (times == NULL) {
		fprintf(stderr, "startbench: out of memory\n");
		return 1;
	}

	/* Warms the page cache and the loader's files. */
	warm = count / 5L + 1L;
	for (i = 0; i < warm; i++) {
		/* One run, not counted. */
		elapsed = run_once(&argv[3]);
		if (elapsed < 0)
			return 1;
	}

	/* The measured runs. */
	total = 0;
	for (i = 0; i < count; i++) {
		/* One run, counted. */
		elapsed = run_once(&argv[3]);
		if (elapsed < 0)
			return 1;

		/* Kept for the median. */
		times[i] = elapsed;
		total += elapsed;
	}

	/* The middle of the sorted times. */
	qsort(times, (size_t)count, sizeof(*times), compare_times);
	printf("STARTBENCH label=%s runs=%ld min=%ld median=%ld mean=%ld us\n",
	       argv[2], count, times[0], times[count / 2L], total / count);

	/* Succeeded: the line is printed. */
	free(times);
	return 0;
}

/*
 * Starts the command, waits for it, and returns the time in microseconds,
 * or -1 when it could not start or did not exit with 0.
 */
static long
run_once(
	char **command)
{
	struct timespec start;
	struct timespec end;
	pid_t child;
	int status;
	int error;
	int exited;
	int code;
	pid_t waited;

	/* The time before the start. */
	clock_gettime(CLOCK_MONOTONIC, &start);

	/* Starts the program with this environment. */
	error = posix_spawn(&child, command[0], NULL, NULL, command, environ);
	if (error != 0) {
		fprintf(stderr, "startbench: cannot start %s (%d)\n", command[0], error);
		return -1;
	}

	/* Waits for it to end. */
	waited = waitpid(child, &status, 0);
	if (waited != child) {
		fprintf(stderr, "startbench: waitpid failed\n");
		return -1;
	}

	/* The time after the end. */
	clock_gettime(CLOCK_MONOTONIC, &end);

	/* Refuses a run that failed. */
	exited = WIFEXITED(status);
	code = WEXITSTATUS(status);
	if (!exited || code != 0) {
		fprintf(stderr, "startbench: %s did not exit with 0\n", command[0]);
		return -1;
	}

	/* Succeeded: the elapsed microseconds. */
	return (long)(end.tv_sec - start.tv_sec) * 1000000L +
	       (long)(end.tv_nsec - start.tv_nsec) / 1000L;
}

/*
 * Orders two run times for qsort.
 */
static int
compare_times(
	const void *left,
	const void *right)
{
	long a;
	long b;

	/* The two times. */
	a = *(const long *)left;
	b = *(const long *)right;

	/* Earlier first. */
	if (a < b)
		return -1;

	/* Later after. */
	if (a > b)
		return 1;

	/* The same. */
	return 0;
}
