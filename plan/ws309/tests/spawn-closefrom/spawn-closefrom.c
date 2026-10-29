/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Test of libc's posix_spawn_file_actions_addclosefrom_np, run on zedBSD:
 * the program opens descriptors without close-on-exec, spawns itself with
 * a close-from-5 action (and a dup2 to 4 before it), and the child reports
 * which of 3..20 are open.  3 and 4 must stay open, 5.. must be closed; a
 * negative descriptor is refused with EBADF.  Prints
 * "spawn-closefrom: PASS" or the failures.
 *
 * With "--closed FILE" it only writes into FILE "closed-ok" when every one
 * of 3..20 is closed, else the open ones (for a program the compositor
 * starts).
 */

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The descriptors the child looks at. */
#define LOOK_LAST	20

extern char **environ;

static int child_report(void);
static int closed_report(const char *path);

int
main(
	int argc,
	char **argv)
{
	posix_spawn_file_actions_t actions;
	char *child_argv[3];
	unsigned failures;
	pid_t child;
	int status;
	int descriptor;
	int index;
	int error;

	/* Every line out at once (the static runtime's buffer may not be flushed at the end). */
	setvbuf(stdout, NULL, _IONBF, 0);

	/* The child's side, and the compositor's program's. */
	if (argc > 1 && strcmp(argv[1], "--child") == 0)
		return child_report();
	if (argc > 2 && strcmp(argv[1], "--closed") == 0)
		return closed_report(argv[2]);

	/* Descriptors 3.. open without close-on-exec. */
	failures = 0U;
	for (index = 0; index < 8; index++) {
		descriptor = open("/dev/null", O_RDONLY);
		if (descriptor < 0) {
			printf("spawn-closefrom: open errno=%d\n", errno);
			failures++;
		}
	}

	/* A negative descriptor is refused. */
	posix_spawn_file_actions_init(&actions);
	error = posix_spawn_file_actions_addclosefrom_np(&actions, -1);
	if (error != EBADF) {
		printf("spawn-closefrom: negative gave %d\n", error);
		failures++;
	}

	/* 4 kept through a dup2 before the close-from-5. */
	(void)posix_spawn_file_actions_adddup2(&actions, 3, 4);
	error = posix_spawn_file_actions_addclosefrom_np(&actions, 5);
	if (error != 0) {
		printf("spawn-closefrom: add gave %d\n", error);
		failures++;
	}
	child_argv[0] = argv[0];
	child_argv[1] = "--child";
	child_argv[2] = NULL;
	error = posix_spawn(&child, argv[0], &actions, NULL, child_argv, environ);
	posix_spawn_file_actions_destroy(&actions);
	if (error != 0) {
		printf("spawn-closefrom: spawn gave %d\n", error);
		return 1;
	}

	/* The child's verdict is its exit status. */
	status = 0;
	(void)waitpid(child, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("spawn-closefrom: child status=0x%x\n", (unsigned)status);
		failures++;
	}
	if (failures != 0U) {
		printf("spawn-closefrom: FAIL failures=%u\n", failures);
		return 1;
	}
	printf("spawn-closefrom: PASS\n");
	return 0;
}

/* Reports which descriptors the child has: 0 when 3 and 4 are open and 5.. are closed. */
static int
child_report(void)
{
	int descriptor;
	int flags;
	int wrong;

	/* Each one looked at. */
	wrong = 0;
	for (descriptor = 3; descriptor <= LOOK_LAST; descriptor++) {
		flags = fcntl(descriptor, F_GETFD);
		if (descriptor <= 4 && flags < 0) {
			printf("spawn-closefrom: child fd %d closed\n", descriptor);
			wrong = 1;
		}
		if (descriptor >= 5 && flags >= 0) {
			printf("spawn-closefrom: child fd %d open\n", descriptor);
			wrong = 1;
		}
	}
	return wrong;
}

/* Writes into a file whether every one of 3..LOOK_LAST is closed ("closed-ok"), else the open ones. */
static int
closed_report(
	const char *path)
{
	FILE *out;
	int descriptor;
	int flags;
	int open_count;

	/* The open ones, counted first (the file is opened after, at the lowest free number). */
	open_count = 0;
	for (descriptor = 3; descriptor <= LOOK_LAST; descriptor++) {
		flags = fcntl(descriptor, F_GETFD);
		if (flags >= 0)
			open_count |= 1 << descriptor;
	}

	/* The report. */
	out = fopen(path, "w");
	if (out == NULL)
		return 1;
	if (open_count == 0) {
		fprintf(out, "closed-ok\n");
	} else {
		for (descriptor = 3; descriptor <= LOOK_LAST; descriptor++) {
			if ((open_count & (1 << descriptor)) != 0)
				fprintf(out, "open %d\n", descriptor);
		}
	}
	fclose(out);
	return 0;
}
