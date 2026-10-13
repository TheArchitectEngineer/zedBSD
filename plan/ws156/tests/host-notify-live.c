/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws156-p006: the Linux backend's notification server on a real session
 * bus (run-host-notify-live.sh starts one with dbus-run-session and calls
 * it with gdbus and dbus-send).  This program is the compositor's side: it
 * serves the notifications for a number of seconds, prints each post and
 * withdrawal its callbacks are given, and tells a withdrawal back as
 * closed, as the compositor does.
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The number the next post gets. */
static uint32_t live_next_id;

static uint32_t live_posted(void *data, uint32_t sender, const char *app, const char *title, const char *body, uint32_t replaces, unsigned flags, int *error);
static int live_withdrawn(void *data, uint32_t sender, uint32_t id);

/* Serves the notifications for argv[1] seconds. */
int
main(int argc, char **argv)
{
	struct kl_backend_notify_callbacks callbacks;
	struct kl_backend *backend;
	struct pollfd descriptor;
	time_t end;
	time_t now;
	size_t count;
	int error;

	/* How long to serve. */
	if (argc != 2)
		return 2;
	end = time(NULL) + atoi(argv[1]);

	/* The server on the session bus of the environment. */
	backend = calloc(1, sizeof(*backend));
	if (backend == NULL)
		return 2;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.posted = live_posted;
	callbacks.withdrawn = live_withdrawn;
	error = kl_backend_notify_open(backend, &callbacks, backend);
	printf("live: open error=%d\n", error);
	fflush(stdout);
	if (error != 0)
		return 1;

	/* The loop, until the time is up or the bus breaks. */
	for (;;) {
		now = time(NULL);
		if (now >= end)
			break;
		count = kl_backend_notify_poll_count(backend);
		if (count == 0) {
			printf("live: bus closed\n");
			break;
		}

		/* One wait, then what came handled. */
		memset(&descriptor, 0, sizeof(descriptor));
		kl_backend_notify_poll_fill(backend, &descriptor);
		poll(&descriptor, 1, 100);
		kl_backend_notify_poll_done(backend, &descriptor);
	}

	/* Done. */
	kl_backend_notify_close(backend);
	free(backend);
	printf("live: done\n");
	return 0;
}

/* Prints a post and gives it the next number. */
static uint32_t
live_posted(
	void *data,
	uint32_t sender,
	const char *app,
	const char *title,
	const char *body,
	uint32_t replaces,
	unsigned flags,
	int *error)
{
	(void)data;
	(void)error;

	/* The next number, printed with the words. */
	live_next_id++;
	printf("live: posted sender=%u id=%u replaces=%u flags=%u app=\"%s\" title=\"%s\" body=\"%s\"\n", sender, live_next_id, replaces, flags, app, title, body);
	fflush(stdout);
	return live_next_id;
}

/* Prints a withdrawal and tells it back as closed. */
static int
live_withdrawn(
	void *data,
	uint32_t sender,
	uint32_t id)
{
	/* Closed by its sender. */
	printf("live: withdrawn sender=%u id=%u\n", sender, id);
	fflush(stdout);
	kl_backend_notify_closed(data, id, KL_BACKEND_NOTIFY_CLOSED);
	return 0;
}
