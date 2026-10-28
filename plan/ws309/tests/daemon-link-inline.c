/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host test's stand-in for daemon-link-zedbsd.c: the same calls with no
 * thread, for a test that drives one daemon's protocol step by step from a
 * single thread (a fake daemon in the same thread and a clock of the
 * test's).  A request given to the link runs at once, as the thread would
 * run it before its next step, and each take of the lock first steps the
 * protocol and publishes, as if the thread had run to rest just before the
 * event loop's update.  The protocol's code is the product's; only the
 * thread is left out, which the link's own host test covers.
 */

#include "userland/desktop/libkeiland-backend-zedbsd/daemon-link-zedbsd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Starts a link with no thread. */
int
kl_daemon_link_start(
	struct kl_daemon_link *link,
	const char *name,
	const struct kl_daemon_link_ops *ops,
	void *owner)
{
	/* The record; nothing runs until the first request or lock. */
	memset(link, 0, sizeof(*link));
	link->ops = ops;
	link->owner = owner;
	(void)snprintf(link->name, sizeof(link->name), "%s", name);
	link->pipe[0] = -1;
	link->pipe[1] = -1;
	link->wake = -1;

	/* Succeeded: no thread is needed. */
	return 0;
}

/* Stops a link with no thread: nothing waits, every request ran at once. */
void
kl_daemon_link_stop(
	struct kl_daemon_link *link)
{
	/* Nothing to join or to drop. */
	link->owner = NULL;
}

/* Runs a request at once, as the thread would before its next step. */
void
kl_daemon_link_submit(
	struct kl_daemon_link *link,
	struct kl_daemon_link_op *op)
{
	/* The request, then its memory. */
	op->next = NULL;
	op->run(link->owner, op);
	free(op);
}

/* Steps the protocol and publishes, as the thread does between two takes of the lock. */
void
kl_daemon_link_lock(
	struct kl_daemon_link *link)
{
	/* One step, then what it found published. */
	link->ops->step(link->owner);
	(void)link->ops->publish(link->owner);
}

/* Gives the lock back: nothing is held. */
void
kl_daemon_link_unlock(
	struct kl_daemon_link *link)
{
	/* No lock to give back. */
	(void)link;
}
