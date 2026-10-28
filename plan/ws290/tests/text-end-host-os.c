/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host's own services the stand-ins of the PC/AT console's end use
 * (pages, their protection, a short pause, one more thread), kept apart so
 * that this file sees the host's headers and none of the kernel's.
 */

#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "text-end-stubs.h"

/* The one extra thread a scene may start; joined before the scene ends. */
static pthread_t extra_thread;

/* Takes fresh zeroed host pages, whole pages for the bytes. */
void *
text_end_host_pages(
	size_t bytes,
	size_t *length)
{
	void *pages;

	/* Whole pages, readable and writable. */
	*length = (bytes + 4095U) & ~(size_t)4095U;
	pages = mmap(NULL, *length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (pages == MAP_FAILED)
		abort();
	return pages;
}

/* Makes pages read-only: a write through them ends the process. */
void
text_end_host_read_only(
	void *pages,
	size_t length)
{
	/* Reads stay allowed, for the checks. */
	if (mprotect(pages, length, PROT_READ) != 0)
		abort();
}

/* Pauses the calling thread. */
void
text_end_host_pause(
	long nanoseconds)
{
	struct timespec pause;

	/* A short sleep. */
	pause.tv_sec = nanoseconds / 1000000000L;
	pause.tv_nsec = nanoseconds % 1000000000L;
	nanosleep(&pause, NULL);
}

/* Starts the scene's extra thread. */
void
text_end_host_thread_start(
	void *(*entry)(void *))
{
	/* A thread of the host. */
	if (pthread_create(&extra_thread, NULL, entry, NULL) != 0)
		abort();
}

/* Waits for the scene's extra thread. */
void
text_end_host_thread_join(void)
{
	/* Until it returned. */
	if (pthread_join(extra_thread, NULL) != 0)
		abort();
}
