/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Entering and leaving the interpreter.
 *
 * One lock (aml-os.h) serializes every use of the namespace and every run
 * of AML.  The first entry of an operating system thread takes the lock
 * and becomes the active interpreter thread; entries nested inside it
 * (a driver callback that evaluates again) join it.  AML that sleeps --
 * Sleep, and the waits of Acquire and Wait -- lets the lock go for the
 * duration, so another thread may run AML meanwhile, and takes it back.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The interpreter thread that holds the lock, or NULL when nobody does.
 *
 * drv_acpi_enter() sets it and drv_acpi_leave() clears it; drv_acpi_sleep()
 * puts it aside while the lock is let go.  Only the holder of the lock
 * reads or writes it.
 */
static struct drv_acpi_thread *active_thread;

/*
 * Enters the interpreter: takes the lock, or joins the entry of this
 * thread that already holds it.
 *
 * storage is the caller's thread record, used when this is the outermost
 * entry; frame is the caller's frame address, from which the stack budget
 * is measured.  The thread returned is the one to leave with.
 */
struct drv_acpi_thread *
drv_acpi_enter(
	struct drv_acpi_thread *storage,
	const void *frame)
{
	bool owned;

	/* An entry inside one that holds the lock joins it. */
	owned = drv_acpi_os_lock_owned();
	if (owned && active_thread != NULL) {
		active_thread->nesting++;
		return active_thread;
	}

	/* Takes the lock for a new outermost entry. */
	drv_acpi_os_lock();

	/*
	 * The record becomes the active thread: it holds no mutexes, it is
	 * entered once, and its stack is measured from the caller's frame.
	 */
	kern_memset(storage, 0, sizeof(*storage));
	storage->stack_base = (uintptr_t)frame;
	storage->nesting = 1;
	active_thread = storage;

	/* Succeeded. */
	return storage;
}

/*
 * Leaves the interpreter; the outermost leave releases what the thread
 * still holds and lets the lock go.
 */
void
drv_acpi_leave(
	struct drv_acpi_thread *thread)
{
	/* A nested entry only counts down. */
	thread->nesting--;
	if (thread->nesting != 0)
		return;

	/* Releases the mutexes AML left held, then the lock. */
	drv_acpi_thread_end(thread);
	active_thread = NULL;
	drv_acpi_os_unlock();
}

/*
 * Reports the interpreter thread that holds the lock, or NULL.
 */
struct drv_acpi_thread *
drv_acpi_active_thread(void)
{
	/* Reports it. */
	return active_thread;
}

/*
 * Reports the thread an evaluation belongs to.
 *
 * An evaluation carries its thread; one that does not (the arguments of a
 * region evaluated on first use) belongs to the active thread.
 */
struct drv_acpi_thread *
drv_acpi_eval_thread(
	struct drv_acpi_eval *eval)
{
	/*
	 * The thread of AML run with no entry at all, which only the host
	 * tests do.  It holds nothing between runs.
	 */
	static struct drv_acpi_thread fallback;

	/* Reports the evaluation's own thread when it has one. */
	if (eval != NULL && eval->thread != NULL)
		return eval->thread;

	/* Reports the active thread, or the fallback outside any entry. */
	if (active_thread != NULL)
		return active_thread;
	return &fallback;
}

/*
 * Sleeps with the lock let go, so that other threads can run AML.
 */
void
drv_acpi_sleep(
	uint64_t milliseconds)
{
	struct drv_acpi_thread *sleeper;

	/* Puts the active thread aside and lets the lock go. */
	sleeper = active_thread;
	active_thread = NULL;
	drv_acpi_os_unlock();

	/* Sleeps. */
	drv_acpi_os_sleep(milliseconds);

	/* Takes the lock back and becomes the active thread again. */
	drv_acpi_os_lock();
	active_thread = sleeper;
}
