/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Starting keiland-preview on zedBSD (WS168 p004; client.h,
 * plan/ws168/phase001/phase.md sections 3 and 5): sandbox_spawn with the
 * program's image (a static one), the input as fd 0 and the output as fd
 * 1 and no other, the arguments, and the limits of memory, processor time
 * and size written.  The child is in its sandbox from its first
 * instruction.
 */

#include "../client.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <sandbox.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The program. */
#define SPAWN_PROGRAM		KEILAND_LIBEXECDIR "/keiland-preview"

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
	struct sandbox_spawn spawn;
	struct sandbox_fd fds[2];
	char width[32];
	char height[32];
	char stamp[PREVIEW_STAMP_MAX + 16U];
	char *argv[6];
	size_t count;
	int image;
	int error;

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

	/* The program's image. */
	image = open(SPAWN_PROGRAM, O_RDONLY | O_CLOEXEC);
	if (image < 0)
		return errno;

	/* The request: two files, the limits, nothing else allowed. */
	fds[0].from = input;
	fds[0].to = 0;
	fds[1].from = output;
	fds[1].to = 1;
	memset(&spawn, 0, sizeof(spawn));
	spawn.size = sizeof(spawn);
	spawn.image = image;
	spawn.fd_count = 2;
	spawn.fds = (uint64_t)(uintptr_t)fds;
	spawn.argv = (uint64_t)(uintptr_t)argv;
	spawn.memory_max = PREVIEW_MEMORY_MAX;
	spawn.cpu_seconds = PREVIEW_CPU_SECONDS;
	spawn.write_max = PREVIEW_WRITE_MAX;
	*pid = sandbox_spawn(&spawn);
	error = errno;
	(void)close(image);
	if (*pid < 0) {
		*pid = 0;
		return error;
	}

	/* Started. */
	return 0;
}
