/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window mode of zdesktop-browser.
 *
 * The zdesktop window (wl_shm presentation, the CONTROLS titlebar, the
 * scrolling and the links) arrives in ws074-p014; until then the window
 * mode reports that it is not there and the headless modes are the whole
 * program.
 */

#include "shell/shell.h"
#include "base/base.h"

#include <stdio.h>

/*
 * Runs the browser in a zdesktop window until it is closed.
 *
 * Returns the program's exit status.
 */
int
shell_run(
	const struct shell_options *options)
{
	UNUSED_PARAMETER(options);

	/* Tells the user the window is not built yet. */
	fprintf(stderr, "zdesktop-browser: the window mode is not available yet; use a headless mode (--help)\n");

	/* Reports the failure to the caller. */
	return 1;
}
