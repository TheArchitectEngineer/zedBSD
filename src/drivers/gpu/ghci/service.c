/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The start of the GHCI device's service thread, which recovers from
 * faults and hangs and gives deferred memory back (ghci_service_main in
 * cp.c).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/sched.h>
#include <kern/thread.h>

#include "ghci.h"

/*
 * Starts the service thread of the device.
 */
int
ghci_service_start(
	struct ghci_device *device)
{
	struct thread *thread;
	int error;

	/* The thread; nobody joins it, it says when it has left. */
	error = kthread_create(ghci_service_main, device, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0)
		return error;

	/* service_running is cleared by the thread as it leaves. */
	device->service_running = 1;
	thread->detached = 1;
	thread_start(thread);

	/* Succeeded: the thread runs. */
	return 0;
}
