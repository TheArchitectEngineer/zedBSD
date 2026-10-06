/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display's hand-over and the session's end (ws035-p101), over
 * libkeiland-backend's session (ws131-p006).
 *
 * The session manager that started the compositor (zedBSD's sessiond)
 * keeps the greeter on the screen while a session starts: the compositor
 * says it is ready just before it first takes the display and waits to be
 * let.  Leaving, the compositor gives the display back first (the
 * swapchain and its lease) and the backend then tells the manager so,
 * before the slower rest of the end: the login screen when its manager is
 * done with it, a session told to quit after its Log Out.  Without a
 * session manager (Linux, FreeBSD) the display is taken at once and Log
 * Out simply ends the compositor.
 *
 * The backend's answers to the login and the lock screens' requests come
 * here too and go to greeter.c.
 */

#include "userland/desktop/wayland/kwl.h"

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <errno.h>
#include <stdio.h>

/*
 * Says ready and waits to be let take the display, once, before it is
 * first taken.
 */
void
kwl_handoff_wait(
	struct kwl_server *server)
{
	uint64_t started;
	int error;

	/* Only the first time the display is taken. */
	if (server->handed_over)
		return;

	/* Publishes that the first display acquisition has entered the hand-over. */
	server->handed_over = 1;

	/* Ready, and the wait for the manager (none: the display is taken at once). */
	started = kwl_milliseconds();
	error = kl_backend_session_ready(server->backend);
	if (error == ENOTSUP || error == EINVAL)
		return;
	if (error != 0 && error != ETIMEDOUT) {
		printf("ZWL HANDOFF send errno=%d\n", error);
		return;
	}

	/* The display can be taken (with or without the manager's word). */
	printf("ZWL HANDOFF go=%d waited_ms=%llu at_ms=%llu\n", error == 0, (unsigned long long)(kwl_milliseconds() - started), (unsigned long long)kwl_milliseconds());
}

/*
 * Log Out: asks the manager for a greeter; the session goes on showing
 * until it says to quit.  Returns 1 when asked, 0 when the compositor
 * should simply end.
 */
int
kwl_handoff_logout(
	struct kwl_server *server)
{
	int error;

	/* Only a session, and once. */
	if (server->greeter)
		return 0;
	if (server->logout_ms != 0U)
		return 1;

	/* Asks the manager (none: the compositor ends by itself). */
	error = kl_backend_session_logout(server->backend);
	if (error != 0)
		return 0;

	/* The clipboard's history goes (clipboard.c), and the session's volume and settings are kept for the next login (volume.c, settings.c). */
	kwl_clipboard_history_clear(server, "logout");
	kwl_volume_keep(server, "logout");
	kwl_settings_logout(server);

	/* Succeeded: the quit will come. */
	server->logout_ms = kwl_milliseconds();
	printf("ZWL HANDOFF logout at_ms=%llu\n", (unsigned long long)server->logout_ms);
	return 1;
}

/*
 * Reads what the manager sent (the backend calls back below).
 */
void
kwl_handoff_tick(
	struct kwl_server *server)
{
	/* The backend reads the manager's lines and keeps the Log Out's deadline. */
	kl_backend_tick(server->backend, kwl_milliseconds());
}

/*
 * The backend's session_stop: gives the display back and ends the
 * compositor (the backend then tells the manager the display is free).
 */
void
kwl_handoff_stop(
	void *data,
	unsigned reason)
{
	struct kwl_server *server;

	/* The compositor the backend was opened for. */
	server = data;

	/* The seat's authority failed: the ordinary cleanup ends the compositor. */
	if (reason == KL_BACKEND_SESSION_LOST) {
		printf("ZWL SEAT lost\n");
		server->failed = 1;
		return;
	}

	/* A Log Out the manager did not answer in time: the compositor ends anyway. */
	if (reason == KL_BACKEND_SESSION_UNANSWERED) {
		printf("ZWL HANDOFF logout unanswered\n");
		server->logout_ms = 0U;
		kwl_request_stop();
		return;
	}

	/* Which end: the session's quit, or the login screen's manager done with it. */
	if (reason == KL_BACKEND_SESSION_QUIT)
		printf("ZWL HANDOFF quit at_ms=%llu\n", (unsigned long long)kwl_milliseconds());
	else
		printf("ZWL GREETER closed at_ms=%llu\n", (unsigned long long)kwl_milliseconds());

	/* The swapchain goes, and with it the display's lease, before anything slower. */
	kwl_compose_output_close(server);
	printf("ZWL HANDOFF released at_ms=%llu\n", (unsigned long long)kwl_milliseconds());
	server->logout_ms = 0U;
	kwl_request_stop();
}

/*
 * The backend's session_answer: the manager's answer to the login or the
 * lock screen.
 */
void
kwl_handoff_answer(
	void *data,
	unsigned request,
	int error)
{
	struct kwl_server *server;
	int taken;

	/* The compositor the backend was opened for. */
	server = data;

	/* Remote Login's answer is the system extension's, also while the screen is locked (ws089-p025). */
	if (request == KL_BACKEND_SESSION_SERVICE) {
		kwl_system_sharing_answer(server, error);
		return;
	}

	/* A PIN's change and what the user has enrolled are the system extension's, also while the screen is locked (ws172-p002). */
	if (request == KL_BACKEND_SESSION_ENROLL) {
		taken = kwl_system_pin_answer(server, error);
		if (taken)
			return;
	}

	/* What the user has enrolled. */
	if (request == KL_BACKEND_SESSION_ENROLLED) {
		kwl_system_enrolled_answer(server, error);
		return;
	}

	/* The login screen and a locked session's lock screen act on it (greeter.c). */
	if (server->greeter || server->locked) {
		kwl_greeter_answer(server, request, error);
		return;
	}

	/* A session's Power Off or Restart (the Power Off dialog, ws099-p037): refused (EACCES) or not done is said. */
	if (request == KL_BACKEND_SESSION_POWER) {
		printf("ZWL POWER answer error=%d\n", error);
		return;
	}

	/* Anything else is not for this compositor. */
	printf("ZWL HANDOFF answer request=%u error=%d\n", request, error);
}
