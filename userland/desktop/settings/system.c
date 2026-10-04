/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's system as Settings follows it (WS131 p011): libkeiland's
 * kl_system_* on the window's display, which the network pages (network.c)
 * and the Sound page (sound.c) read and ask through.  Settings speaks to no
 * daemon itself: the compositor holds the network, the sound and the
 * power, and tells every change (plan/ws131/design.md section 4).
 *
 * Each round of the main loop dispatches the system once, after the
 * window's display was read: what changed is kept for the network and the
 * sound to follow, and each answered request goes to the network, which
 * takes its own, or is logged.
 */

#include "settings.h"

#include <errno.h>

/*
 * Opens the system on the window's display.  Without Keiland's system
 * extension the network and the sound pages say they are not available on
 * this desktop.
 */
void
se_system_open(
	struct se_app *app,
	struct wl_display *display)
{
	unsigned capabilities;

	/* The system; without it nothing is offered. */
	app->system = kl_system_open(display);
	if (app->system == NULL) {
		se_log("SYSTEM none errno=%d", errno);
		return;
	}

	/* The log line the tests read. */
	capabilities = kl_system_capabilities(app->system);
	se_log("SYSTEM open capabilities=0x%x", capabilities);
}

/*
 * Takes what the compositor sent since the last round: the changes for
 * the network and the sound, and the answered requests.
 */
void
se_system_poll(
	struct se_app *app)
{
	uint32_t request;
	int taken;
	int error;
	int status;
	int consumed;

	/* Nothing to follow. */
	app->system_changed = 0U;
	if (app->system == NULL)
		return;

	/* The changes; a compositor that went leaves the state as it last was. */
	status = kl_system_dispatch(app->system, &app->system_changed);
	if (status != 0)
		se_log("SYSTEM dispatch errno=%d", status);

	/* Each answer: the network's own, or one of the sound's, logged. */
	for (;;) {
		taken = kl_system_take_result(app->system, &request, &error);
		if (!taken)
			break;
		consumed = se_network_result(app, request, error);
		if (!consumed)
			consumed = se_users_result(app, request, error);
		if (!consumed)
			se_log("SYSTEM result request=%u errno=%d", request, error);
	}
}

/*
 * Closes the system.
 */
void
se_system_close(
	struct se_app *app)
{
	/* No password typed stays (ws160-p002). */
	se_users_close(app);

	/* The system, once. */
	if (app->system != NULL)
		kl_system_close(app->system);
	app->system = NULL;
}
