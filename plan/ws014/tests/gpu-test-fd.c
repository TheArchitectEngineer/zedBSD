/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Process and scheduler collaborators for GPU tests linked to real descriptor code.
 */

#include <kern/process.h>
#include <kern/thread.h>
#include <kern/filedesc.h>
#include <kern/record-lock.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One host caller selects the process namespace used by actual handle_fd functions. */
static struct thread gpu_test_thread;

/*
 * Selects a process between completed test operations.
 */
void
gpu_test_set_process(
	struct process *process)
{
	/* No production descriptor operation is replaced by this caller selection. */
	gpu_test_thread.proc = process;

	/* Succeeded: subsequent real syscalls use the selected process's descriptor table. */
	return;
}

/*
 * Supplies the kernel caller without borrowing host thread ABI declarations.
 */
struct thread *
thread_current(void)
{
	/* Succeeded: the fixture controls this thread throughout each operation. */
	return &gpu_test_thread;
}

/*
 * Reports assertions made through the target libc declarations.
 */
void
__libc_assert_fail(
	const char *expression,
	const char *file,
	int line)
{
	/* Preserve the actual failed invariant instead of masking it in the peer. */
	fprintf(stderr, "ASSERT %s:%d: %s\n", file, line, expression);
	abort();
}

/*
 * Initializes descriptor reservation wait channels.
 */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* Real descriptor code owns every subsequent generation transition. */
	memset(queue, 0, sizeof(*queue));
	queue->name = name;

	/* Succeeded: the channel begins with no published reservation change. */
	return;
}

/*
 * Observes the reservation wake generation.
 */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* Succeeded: the scalar snapshot is stable in this bounded single-threaded peer. */
	return queue->sequence;
}

/*
 * Publishes reservation completion to the real descriptor transaction logic.
 */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* A changed sequence invalidates an earlier reservation wait observation. */
	queue->sequence++;

	/* Succeeded: a waiter can now distinguish the completed reservation. */
	return;
}

/*
 * Refuses an unexpected blocking path in this sequential ownership fixture.
 */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	/* The GPU test does not use sleeping descriptor operations to fake progress. */
	(void)queue;
	(void)lock;
	(void)observed;
	(void)deadline;
	(void)flags;
	abort();
}

/*
 * Detects accidental treatment of a kernel capability as an inode-backed file.
 */
void
record_lock_release_process_inode(
	struct process *process,
	struct inode *inode)
{
	/* GPU capability descriptors have no inode or POSIX record-lock cleanup. */
	(void)process;
	(void)inode;
	abort();
}
