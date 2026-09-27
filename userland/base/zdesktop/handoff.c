/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display's hand-over at a login (ws035-p101).
 *
 * zsessiond keeps the greeter on the screen while the user's session
 * starts.  zdesktop started by zsessiond (the greeter with --auth-fd, a
 * session with --control-fd) says READY on that descriptor just before it
 * first takes the display, when everything slow (the Vulkan device, the
 * wallpaper, the glyphs, the input devices) is done, and waits for GO:
 * zsessiond sends it once the greeter has ended and let go of the display.
 * The screen goes from the greeter's last frame to the desktop's first
 * without the text console between them.  Without GO in time (another
 * zsessiond, or none) zdesktop takes the display anyway.
 *
 * Leaving, zdesktop gives the display back first (the swapchain and its
 * lease) and says RELEASED, before the slower rest of its end: the greeter
 * when zsessiond shuts its side of the socket down, a session told QUIT
 * after its Log Out asked zsessiond for a greeter (LOGOUT).
 */

#include "zwl.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* How long zdesktop waits for GO, and for QUIT after LOGOUT (milliseconds). */
#define HANDOFF_WAIT_MS		20000U
#define HANDOFF_LOGOUT_MS	30000U

/* What the session has read of zsessiond's next line. */
static char handoff_line[64];
static size_t handoff_used;

static int handoff_descriptor(const struct zwl_server *server);
static void handoff_answered(struct zwl_server *server, const char *line);

/*
 * Says READY and waits for GO, once, before the display is first taken.
 * Nothing happens when zsessiond did not start zdesktop.
 */
void
zwl_handoff_wait(
	struct zwl_server *server)
{
	struct pollfd entry;
	uint64_t started;
	uint64_t waited;
	char line[16];
	size_t used;
	ssize_t count;
	char byte;
	int descriptor;
	int status;
	int same;
	int go;

	/* Only the first time the display is taken. */
	if (server->handed_over)
		return;
	server->handed_over = 1;

	/* The greeter's descriptor, or the session's; none when zsessiond did not start zdesktop. */
	descriptor = handoff_descriptor(server);
	if (descriptor < 0)
		return;

	/* READY. */
	count = write(descriptor, "READY\n", 6U);
	if (count != 6) {
		printf("ZWL HANDOFF send errno=%d\n", errno);
		return;
	}

	/* GO, a line of its own, read a byte at a time (nothing after it is taken). */
	started = zwl_milliseconds();
	used = 0;
	go = 0;
	for (;;) {
		/* The time left. */
		waited = zwl_milliseconds() - started;
		if (waited >= HANDOFF_WAIT_MS)
			break;

		/* A byte, or the end of the wait. */
		entry.fd = descriptor;
		entry.events = POLLIN;
		entry.revents = 0;
		status = poll(&entry, 1, (int)(HANDOFF_WAIT_MS - waited));
		if (status < 0 && errno == EINTR)
			continue;
		if (status <= 0)
			break;
		count = read(descriptor, &byte, 1U);
		if (count < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (count <= 0)
			break;

		/* A whole line is looked at; a long one is thrown away. */
		if (byte != '\n') {
			if (used + 1U < sizeof(line))
				line[used++] = byte;
			continue;
		}

		/* The line ends here. */
		line[used] = '\0';
		used = 0;
		same = strcmp(line, "GO");
		if (same == 0) {
			go = 1;
			break;
		}
	}

	/* Succeeded: the display can be taken (with or without GO). */
	printf("ZWL HANDOFF go=%d waited_ms=%llu at_ms=%llu\n", go, (unsigned long long)(zwl_milliseconds() - started), (unsigned long long)zwl_milliseconds());
}

/*
 * Gives the display back ahead of the rest of zdesktop's end, and tells
 * zsessiond so (RELEASED).
 */
void
zwl_handoff_release(
	struct zwl_server *server)
{
	int descriptor;

	/* A directly shown image, then window mode's swapchain and its lease. */
	(void)zwl_unscan(server);
	zwl_compose_output_close(server);

	/* zsessiond hears it (when it started zdesktop). */
	descriptor = handoff_descriptor(server);
	if (descriptor >= 0)
		(void)write(descriptor, "RELEASED\n", 9U);
	printf("ZWL HANDOFF released at_ms=%llu\n", (unsigned long long)zwl_milliseconds());
}

/*
 * Log Out of a session zsessiond started: asks it for a greeter (LOGOUT);
 * the session goes on showing until zsessiond says QUIT.  Returns 1 when
 * asked, 0 when zdesktop should simply end.
 */
int
zwl_handoff_logout(
	struct zwl_server *server)
{
	ssize_t count;

	/* Only a session zsessiond started, and once. */
	if (server->greeter || server->control_fd < 0)
		return 0;
	if (server->logout_ms != 0U)
		return 1;

	/* LOGOUT. */
	count = write(server->control_fd, "LOGOUT\n", 7U);
	if (count != 7)
		return 0;

	/* Succeeded: QUIT will come. */
	server->logout_ms = zwl_milliseconds();
	printf("ZWL HANDOFF logout at_ms=%llu\n", (unsigned long long)server->logout_ms);
	return 1;
}

/*
 * Reads what zsessiond sent a session, and ends a Log Out that was never
 * answered in time.
 */
void
zwl_handoff_tick(
	struct zwl_server *server)
{
	uint64_t now;
	ssize_t count;
	char *end;

	/* Only a session zsessiond started. */
	if (server->greeter || server->control_fd < 0)
		return;

	/* A Log Out zsessiond did not answer in time ends zdesktop anyway. */
	now = zwl_milliseconds();
	if (server->logout_ms != 0U && now - server->logout_ms >= HANDOFF_LOGOUT_MS) {
		printf("ZWL HANDOFF logout unanswered\n");
		server->logout_ms = 0U;
		zwl_request_stop();
		return;
	}

	/* What has come. */
	count = read(server->control_fd, handoff_line + handoff_used, sizeof(handoff_line) - 1U - handoff_used);
	if (count < 0)
		return;

	/* zsessiond gone: the session carries on without it. */
	if (count == 0) {
		printf("ZWL HANDOFF closed\n");
		(void)close(server->control_fd);
		server->control_fd = -1;
		return;
	}

	/* Each whole line. */
	handoff_used += (size_t)count;
	handoff_line[handoff_used] = '\0';
	for (;;) {
		end = strchr(handoff_line, '\n');
		if (end == NULL)
			break;
		*end = '\0';
		handoff_answered(server, handoff_line);
		handoff_used -= (size_t)(end - handoff_line) + 1U;
		memmove(handoff_line, end + 1, handoff_used + 1U);
	}

	/* A line that never ends is thrown away. */
	if (handoff_used + 1U >= sizeof(handoff_line))
		handoff_used = 0U;
}

/* Returns the descriptor to zsessiond: the greeter's, the session's, or -1. */
static int
handoff_descriptor(
	const struct zwl_server *server)
{
	/* The greeter asks on --auth-fd. */
	if (server->greeter)
		return server->auth_fd;

	/* A session on --control-fd (-1 without). */
	return server->control_fd;
}

/* Acts on one line zsessiond sent a session. */
static void
handoff_answered(
	struct zwl_server *server,
	const char *line)
{
	int same;

	/* QUIT: the greeter is ready; the display goes back, then zdesktop ends. */
	same = strcmp(line, "QUIT");
	if (same == 0) {
		printf("ZWL HANDOFF quit at_ms=%llu\n", (unsigned long long)zwl_milliseconds());
		zwl_handoff_release(server);
		server->logout_ms = 0U;
		zwl_request_stop();
		return;
	}

	/* Anything else is not for this zdesktop. */
	printf("ZWL HANDOFF line=%s\n", line);
}
