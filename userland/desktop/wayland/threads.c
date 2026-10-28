/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register of the compositor's threads and the way they are made.
 *
 * A thread is made with every signal blocked, so that SIGINT, SIGTERM and
 * SIGCHLD reach the main thread alone, and its name is kept in the
 * register until it is joined, or for a detached thread (one made for a
 * single job that may hang, which nobody waits for) until it ends.  The register is only read for the log, so
 * a thread that cannot be registered (the register full) still runs.
 */

#include "threads.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * The names of the threads running now, one slot each (an empty name is
 * a free slot).  register_lock guards them; a thread is put in at its
 * start and taken out at its join, both on the thread that starts it, or
 * by a detached thread itself as it ends.
 */
static pthread_mutex_t register_lock = PTHREAD_MUTEX_INITIALIZER;
static char register_names[KWL_THREADS_MAX][KWL_THREAD_NAME];

static void register_add(const char *name);
static void register_remove(const char *name);

/*
 * Starts a thread of the compositor's own under a name, with every signal
 * blocked.  Returns 0, or the errno value of pthread_create (the thread is
 * then not started and needs no join).
 */
int
kwl_thread_start(
	struct kwl_thread *thread,
	const char *name,
	void *(*run)(void *),
	void *argument)
{
	sigset_t all;
	sigset_t previous;
	int error;

	/* The record names the thread before it runs. */
	memset(thread, 0, sizeof(*thread));
	(void)snprintf(thread->name, sizeof(thread->name), "%s", name);

	/* The new thread inherits a mask that blocks every signal; this thread's own mask is put back after. */
	(void)sigfillset(&all);
	(void)pthread_sigmask(SIG_SETMASK, &all, &previous);
	error = pthread_create(&thread->thread, NULL, run, argument);
	(void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
	if (error != 0)
		return error;

	/* started tells the join that a thread is to be waited for; the register shows it until then. */
	thread->started = 1U;
	register_add(thread->name);

	/* Succeeded: the thread runs. */
	return 0;
}

/*
 * Starts a detached thread of the compositor's own under a name, with
 * every signal blocked: nobody joins it, and it calls kwl_thread_ended
 * with the same name as it ends.  Returns 0, or the errno value of
 * pthread_create (the thread is then not started).
 */
int
kwl_thread_start_detached(
	const char *name,
	void *(*run)(void *),
	void *argument)
{
	pthread_attr_t attributes;
	pthread_t thread;
	sigset_t all;
	sigset_t previous;
	int error;

	/* Detached: its end frees it. */
	error = pthread_attr_init(&attributes);
	if (error != 0)
		return error;
	(void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

	/* Registered before it runs, so that its own end cannot come first. */
	register_add(name);

	/* The new thread inherits a mask that blocks every signal; this thread's own mask is put back after. */
	(void)sigfillset(&all);
	(void)pthread_sigmask(SIG_SETMASK, &all, &previous);
	error = pthread_create(&thread, &attributes, run, argument);
	(void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
	(void)pthread_attr_destroy(&attributes);
	if (error != 0) {
		register_remove(name);
		return error;
	}

	/* Succeeded: the thread runs until its job ends. */
	return 0;
}

/*
 * Takes a detached thread out of the register, on that thread as it ends.
 */
void
kwl_thread_ended(
	const char *name)
{
	/* Its slot. */
	register_remove(name);
}

/*
 * Waits for a thread that was started to end and takes it out of the
 * register.  A thread never started needs nothing.
 */
void
kwl_thread_join(
	struct kwl_thread *thread)
{
	/* Nothing was started. */
	if (!thread->started)
		return;

	/* The thread has ended once this returns. */
	(void)pthread_join(thread->thread, NULL);
	thread->started = 0U;
	register_remove(thread->name);
}

/*
 * Counts the threads in the register.
 */
unsigned
kwl_threads_count(void)
{
	unsigned count;
	unsigned slot;

	/* Every slot that holds a name. */
	pthread_mutex_lock(&register_lock);

	count = 0U;
	for (slot = 0U; slot < KWL_THREADS_MAX; slot++) {
		if (register_names[slot][0] != '\0')
			count++;
	}

	pthread_mutex_unlock(&register_lock);

	/* Reports the threads running. */
	return count;
}

/*
 * Prints the threads in the register on one line (KWL THREADS), in the
 * order of their slots.
 */
void
kwl_threads_report(void)
{
	char line[KWL_THREADS_MAX * KWL_THREAD_NAME];
	size_t used;
	unsigned count;
	unsigned slot;
	int written;

	/* The names, with a comma between two. */
	pthread_mutex_lock(&register_lock);

	line[0] = '\0';
	used = 0U;
	count = 0U;
	for (slot = 0U; slot < KWL_THREADS_MAX; slot++) {
		if (register_names[slot][0] == '\0')
			continue;

		/* A comma before every name but the first. */
		written = snprintf(line + used, sizeof(line) - used, "%s%s", count == 0U ? "" : ",", register_names[slot]);
		if (written < 0 || (size_t)written >= sizeof(line) - used)
			break;
		used += (size_t)written;
		count++;
	}

	pthread_mutex_unlock(&register_lock);

	/* The line the tests read. */
	printf("KWL THREADS count=%u names=%s\n", count, line);
}

/*
 * Counts the processors online, for the sizes of the pools: at least 1,
 * also when the system does not say.
 */
unsigned
kwl_cpu_count(void)
{
	long online;

	/* The processors the system runs now. */
	online = sysconf(_SC_NPROCESSORS_ONLN);
	if (online < 1L)
		return 1U;

	/* A count too large for the pools is as many as they could use anyway. */
	if (online > 1024L)
		return 1024U;

	/* Succeeded: the processors online. */
	return (unsigned)online;
}

/*
 * Writes one byte to the main thread's wake pipe, whose end does not
 * block: a full pipe already wakes the main thread, so the byte is not
 * needed then.  A descriptor below 0 (no pipe) is passed by.
 */
void
kwl_thread_wake(
	int descriptor)
{
	unsigned char byte;
	ssize_t written;

	/* No pipe to wake. */
	if (descriptor < 0)
		return;

	/* The byte, again only when a signal cut the write short. */
	byte = 1U;
	do {
		written = write(descriptor, &byte, 1U);
	} while (written < 0 && errno == EINTR);
}

/* Puts a thread's name in a free slot of the register (a full register leaves it out). */
static void
register_add(
	const char *name)
{
	unsigned slot;

	/* The first free slot. */
	pthread_mutex_lock(&register_lock);

	for (slot = 0U; slot < KWL_THREADS_MAX; slot++) {
		if (register_names[slot][0] == '\0') {
			(void)snprintf(register_names[slot], sizeof(register_names[slot]), "%s", name);
			break;
		}
	}

	pthread_mutex_unlock(&register_lock);
}

/* Takes one slot with a thread's name out of the register. */
static void
register_remove(
	const char *name)
{
	unsigned slot;
	int same;

	/* The first slot of that name (threads of a pool have names of their own). */
	pthread_mutex_lock(&register_lock);

	for (slot = 0U; slot < KWL_THREADS_MAX; slot++) {
		same = strcmp(register_names[slot], name);
		if (same == 0) {
			register_names[slot][0] = '\0';
			break;
		}
	}

	pthread_mutex_unlock(&register_lock);
}
