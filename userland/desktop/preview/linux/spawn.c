/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Starting keiland-preview on Linux (WS168 p004; client.h,
 * plan/ws168/phase001/phase.md section 7): a new process with the input as
 * fd 0 and the output as fd 1 and every other descriptor closed, the
 * limits of memory, processor time and size written, and the program run
 * with an empty environment; the program confines itself with seccomp
 * before it reads a byte (linux/confine.c).
 */

/* close_range is a GNU extension of the C library. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "../client.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

/* The program. */
#define SPAWN_PROGRAM		KEILAND_LIBEXECDIR "/keiland-preview"

static void spawn_child(int input, int output, char **argv) __attribute__((noreturn));

/*
 * Starts the child.  Returns 0 with its process ID, or an errno value.
 */
int
preview_spawn(
	int input,
	int output,
	const struct preview_request *request,
	pid_t *pid,
	int *status)
{
	char width[32];
	char height[32];
	char stamp[PREVIEW_STAMP_MAX + 16U];
	char *argv[6];
	size_t count;

	/* The arguments. */
	*pid = 0;
	*status = PREVIEW_NO_SANDBOX;
	(void)snprintf(width, sizeof(width), "--width=%d", request->width);
	(void)snprintf(height, sizeof(height), "--height=%d", request->height);
	count = 0;
	argv[count++] = "keiland-preview";
	argv[count++] = width;
	argv[count++] = height;
	if (request->cover)
		argv[count++] = "--fit=cover";
	if (request->stamp[0] != '\0') {
		(void)snprintf(stamp, sizeof(stamp), "--stamp=%s", request->stamp);
		argv[count++] = stamp;
	}

	/* The list's end. */
	argv[count] = NULL;

	/* The child. */
	*pid = fork();
	if (*pid < 0) {
		*pid = 0;
		return errno;
	}

	/* The child goes on in spawn_child. */
	if (*pid == 0)
		spawn_child(input, output, argv);
	return 0;
}

/* In the child: its two files, its limits, then the program (or the end, 127). */
static void
spawn_child(
	int input,
	int output,
	char **argv)
{
	static char *environment[1];
	struct rlimit limit;
	int first;
	int second;

	/* The input and the output, nothing else. */
	first = dup2(input, 0);
	second = dup2(output, 1);
	if (first < 0 || second < 0)
		_exit(127);
	(void)close_range(2, ~0U, 0);

	/* The limits. */
	limit.rlim_cur = PREVIEW_MEMORY_MAX;
	limit.rlim_max = PREVIEW_MEMORY_MAX;
	(void)setrlimit(RLIMIT_AS, &limit);
	limit.rlim_cur = PREVIEW_CPU_SECONDS;
	limit.rlim_max = PREVIEW_CPU_SECONDS;
	(void)setrlimit(RLIMIT_CPU, &limit);
	limit.rlim_cur = PREVIEW_WRITE_MAX;
	limit.rlim_max = PREVIEW_WRITE_MAX;
	(void)setrlimit(RLIMIT_FSIZE, &limit);

	/* The program, with no environment. */
	environment[0] = NULL;
	(void)execve(SPAWN_PROGRAM, argv, environment);
	_exit(127);
}
