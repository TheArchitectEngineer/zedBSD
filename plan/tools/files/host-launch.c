/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 * SPDX-License-Identifier: Zlib
 */

/*
 * The stand-in for libkeiland's kl_launch on the host, for files' host
 * tests (Open With starts the program chosen apart from Files): the
 * command line by /bin/sh -c, or the arguments, started and not waited
 * for.  No compositor: the window and the token are not asked for.
 */

#include <keiland/keiland.h>

#include <errno.h>
#include <spawn.h>
#include <stddef.h>
#include <sys/types.h>

/* The environment a started program inherits. */
extern char **environ;

/* Starts the program apart from the test; returns 0 or the error of its start. */
int
kl_launch(
	struct kl_app *app,
	const struct kl_launch_options *options)
{
	const char *shell[4];
	const char *const *argv;
	pid_t child;
	int error;

	/* Exactly one of the arguments and the command line. */
	(void)app;
	if (options == NULL)
		return EINVAL;
	if ((options->argv == NULL) == (options->command == NULL))
		return EINVAL;

	/* The command line by the shell. */
	argv = options->argv;
	if (options->command != NULL) {
		shell[0] = "/bin/sh";
		shell[1] = "-c";
		shell[2] = options->command;
		shell[3] = NULL;
		argv = shell;
	}

	/* Started (the test's own children are not waited for). */
	error = posix_spawnp(&child, argv[0], NULL, NULL, (char *const *)argv, environ);
	if (error != 0)
		return error;

	/* Succeeded: the program runs. */
	return 0;
}
