/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The terminal's own settings, kept from one run to the next (ws128-p009),
 * through the desktop's settings (libkeiland's kl_settings_*, WS135): the
 * key terminal.ambiguous-wide (0 or 1: View > Treat Ambiguous-Width
 * Characters as Wide), which libkeiland keeps in the terminal's own file
 * (~/.config/keiland/terminal.conf, ambiguous-wide=...).  The terminal opens
 * no settings file itself.
 *
 * The windows and tabs of the terminal that changed it follow at once;
 * another terminal reads it when it starts (an application's own setting
 * is not told to other processes, WS135 D3).
 */

#include "terminal.h"

#include <keiland.h>

#include <errno.h>
#include <string.h>

/* The key of the Ambiguous-width setting. */
#define SETTINGS_AMBIGUOUS_WIDE	"terminal.ambiguous-wide"

/* The application's name, which names its settings. */
#define SETTINGS_APP		"terminal"

/*
 * Reads the settings into settings; without them (no home, no memory) the
 * defaults (everything off) stay.
 */
void
terminal_settings_load(
	struct terminal_settings *settings)
{
	struct kl_settings *desktop;

	/* The defaults: the terminal as it was before the setting existed. */
	memset(settings, 0, sizeof(*settings));

	/* Opens the terminal's settings; its own keys need no display. */
	desktop = kl_settings_open(NULL, SETTINGS_APP);
	if (desktop == NULL)
		return;

	/* Reads whether Ambiguous-width characters are wide. */
	settings->ambiguous_wide = kl_settings_get_int(desktop, SETTINGS_AMBIGUOUS_WIDE, 0);

	/* Closes the settings, which are not needed any more. */
	kl_settings_close(desktop);
}

/*
 * Keeps the settings for the next run.  Returns 0, or an errno value; the
 * old setting is then kept.
 */
int
terminal_settings_save(
	const struct terminal_settings *settings)
{
	struct kl_settings *desktop;
	int wide;
	int error;

	/* Opens the terminal's settings. */
	desktop = kl_settings_open(NULL, SETTINGS_APP);
	if (desktop == NULL)
		return errno;

	/* Gives the setting as 0 or 1. */
	wide = 0;
	if (settings->ambiguous_wide)
		wide = 1;

	/* Keeps it in the terminal's file; a failure leaves the old setting. */
	error = kl_settings_set_int(desktop, SETTINGS_AMBIGUOUS_WIDE, wide, NULL);
	if (error != 0) {
		kl_settings_close(desktop);
		return error;
	}

	/* Closes the settings, which are not needed any more. */
	kl_settings_close(desktop);

	/* Succeeded: the next run reads it. */
	return 0;
}
