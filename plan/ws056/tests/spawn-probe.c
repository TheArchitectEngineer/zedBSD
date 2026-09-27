/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws056-p002: posix_spawn of /bin/posix-r2 (R2_SPAWN_CHILD=1, which exits
 * 23 at once) from a process that has used a SIGEV_THREAD timer, as the
 * end of POSIX-R2.ELF does.  Prints the child's status, or hangs.
 */

#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

static void callback(union sigval value);

/* A callback that does nothing. */
static void
callback(
	union sigval value)
{
	(void)value;
}

/*
 * Arms and fires one SIGEV_THREAD timer, then spawns the child.
 */
int
main(
	int argc,
	char **argv)
{
	char *arguments[] = { "/bin/sh", NULL };
	char *environment[] = { "R2_SPAWN_CHILD=1", NULL };
	struct sigevent event;
	struct itimerspec value;
	struct timespec pause;
	const char *path;
	timer_t timer;
	pid_t child;
	int status;
	int error;

	/* The program to spawn. */
	path = "/bin/posix-r2";
	if (argc > 1)
		path = argv[1];

	/* Starts the library's timer worker, as the test does. */
	memset(&event, 0, sizeof(event));
	event.sigev_notify = SIGEV_THREAD;
	event.sigev_notify_function = callback;
	timer_create(CLOCK_MONOTONIC, &event, &timer);
	memset(&value, 0, sizeof(value));
	value.it_value.tv_nsec = 1000000L;
	timer_settime(timer, 0, &value, NULL);
	pause.tv_sec = 0;
	pause.tv_nsec = 50000000L;
	nanosleep(&pause, NULL);
	timer_delete(timer);

	/* Spawns the child and waits for it. */
	error = posix_spawn(&child, path, NULL, NULL, arguments, environment);
	printf("spawn error %d\n", error);
	fflush(stdout);
	if (error != 0)
		return 1;
	waitpid(child, &status, 0);
	printf("child status %d\n", WEXITSTATUS(status));

	/* Succeeded. */
	return 0;
}
