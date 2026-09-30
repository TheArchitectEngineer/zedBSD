/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-123 regression, on the host, against the library's own sources
 * (plan/ws073/tests/wayland-dispatch-once.sh builds it): wl_display_dispatch
 * waits and reads once, like the standard library, and returns 0 when what
 * it read held no event of the default queue.  The peer end of a
 * socketpair sends the done event of a callback that lives on a private
 * queue; the default queue's dispatch must come back with 0 instead of
 * waiting for an event that never comes (an alarm fails the test).  The
 * private queue's dispatch then delivers the event.
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-client.h>

/* The seconds the default queue's dispatch may take before the test fails. */
#define DISPATCH_LIMIT_SECONDS 3

/* Nonzero once the private queue's callback ran. */
static int done_seen;

/* Records that the private callback's done event was dispatched. */
static void
callback_done(
	void *data,
	struct wl_callback *callback,
	uint32_t serial)
{
	(void)data;
	(void)callback;
	(void)serial;
	done_seen = 1;
}

static const struct wl_callback_listener callback_listener = {
	callback_done
};

/* Ends the test when the dispatch did not return. */
static void
expired(
	int signal_number)
{
	static const char message[] = "FAIL wl_display_dispatch waited for an event of its queue (BUG-123)\n";

	(void)signal_number;
	(void)write(STDOUT_FILENO, message, sizeof(message) - 1U);
	_exit(1);
}

int
main(void)
{
	struct wl_event_queue *queue;
	struct wl_display *display;
	struct wl_display *wrapper;
	struct wl_callback *callback;
	uint32_t event[3];
	char requests[64];
	int ends[2];
	int dispatched;

	/* The connection over a socketpair; the test is the compositor end. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) != 0)
		return 2;
	display = wl_display_connect_to_fd(ends[0]);
	if (display == NULL)
		return 2;

	/* A sync callback (object 2) on a private queue, sent to the peer. */
	queue = wl_display_create_queue(display);
	wrapper = wl_proxy_create_wrapper(display);
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);
	callback = wl_display_sync(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	(void)wl_callback_add_listener(callback, &callback_listener, NULL);
	(void)wl_display_flush(display);
	(void)read(ends[1], requests, sizeof(requests));

	/* The peer answers with the callback's done event: object 2, opcode 0, 12 bytes, serial 7. */
	event[0] = wl_proxy_get_id((struct wl_proxy *)callback);
	event[1] = (12U << 16) | 0U;
	event[2] = 7U;
	if (write(ends[1], event, sizeof(event)) != (ssize_t)sizeof(event))
		return 2;

	/* The default queue has nothing: its dispatch reads once and returns 0. */
	signal(SIGALRM, expired);
	alarm(DISPATCH_LIMIT_SECONDS);
	dispatched = wl_display_dispatch(display);
	alarm(0);
	if (dispatched != 0 || done_seen) {
		printf("FAIL default dispatch returned %d (done %d)\n", dispatched, done_seen);
		return 1;
	}

	/* The private queue then delivers the event it read. */
	dispatched = wl_display_dispatch_queue_pending(display, queue);
	if (dispatched != 1 || !done_seen) {
		printf("FAIL private dispatch returned %d (done %d)\n", dispatched, done_seen);
		return 1;
	}

	/* Succeeded. */
	wl_callback_destroy(callback);
	wl_event_queue_destroy(queue);
	wl_display_disconnect(display);
	close(ends[1]);
	printf("PASS wl_display_dispatch reads once and returns 0 without an event of its queue\n");
	return 0;
}
