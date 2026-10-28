/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor's threads: each has a fixed name in one register, is made
 * with every signal blocked (the main thread alone takes the signals), and
 * tells the main thread of its results with one byte on the main thread's
 * wake pipe.  The register is what the startup line KWL THREADS reports.
 */

#ifndef KWL_THREADS_H
#define KWL_THREADS_H

#include <pthread.h>

/* The most threads the register holds at once (resident, pool and ad hoc together). */
#define KWL_THREADS_MAX		48U

/* The longest name of a thread, with its terminating zero. */
#define KWL_THREAD_NAME		16U

/*
 * One thread of the compositor's own: its handle, its name in the
 * register, and whether it was started (and so is to be joined).  The
 * record belongs to the thread that started it.
 */
struct kwl_thread {
	pthread_t thread;
	char name[KWL_THREAD_NAME];
	unsigned started;
};

int kwl_thread_start(struct kwl_thread *thread, const char *name, void *(*run)(void *), void *argument);
void kwl_thread_join(struct kwl_thread *thread);
unsigned kwl_threads_count(void);
void kwl_threads_report(void);
unsigned kwl_cpu_count(void);
void kwl_thread_wake(int descriptor);

#endif
