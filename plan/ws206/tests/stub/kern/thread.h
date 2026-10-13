/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The part of the kernel's thread interface rpi4-wifi.c uses, for the host
 * test (ws206-p002b).
 *
 * The kernel's own kern/thread.h pulls in the HAL and the user signal
 * types, which clash with the host's C library, so the host test puts this
 * directory first on the include path.  The declarations match
 * include/kern/thread.h.
 */

#ifndef KERN_KERN_THREAD_H
#define KERN_KERN_THREAD_H

/* A kernel thread, as far as the driver touches it. */
struct thread {
	unsigned detached;
};

int
kthread_create(
	void (*entry)(void *),
	void *arg,
	int priority,
	struct thread **result);

void
thread_start(
	struct thread *thread);

#endif
