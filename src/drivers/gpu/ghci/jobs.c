/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The job markers of a GHCI session.
 *
 * A marker is reserved with the core's completion before userspace
 * submits, committed to the fence value of the session's last submission,
 * and completed when that fence passes.  The fence is the device's: one
 * 32-bit value that every submission of every session advances, so a
 * marker committed after a submission completes no earlier than it.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "ghci-words.h"

/*
 * Empties a session's table of job markers.
 */
void
ghci_jobs_init(
	struct ghci_job_table *table)
{
	unsigned index;

	/* Every marker is free and holds nothing. */
	for (index = 0U; index < GHCI_JOB_MARKERS; index++) {
		table->markers[index].state = GHCI_JOB_FREE;
		table->markers[index].completion = NULL;
		table->markers[index].fence = 0U;
	}

	/* No marker is reserved or waiting for a fence. */
	table->reserved = 0U;
	table->committed = 0U;
}

/*
 * Takes a free marker for a completion.
 */
int
ghci_jobs_reserve(
	struct ghci_job_table *table,
	void *completion,
	unsigned *index)
{
	unsigned at;

	/* The first free marker becomes the reservation. */
	for (at = 0U; at < GHCI_JOB_MARKERS; at++) {
		if (table->markers[at].state == GHCI_JOB_FREE) {
			table->markers[at].state = GHCI_JOB_RESERVED;
			table->markers[at].completion = completion;
			table->reserved++;
			*index = at;
			return 0;
		}
	}

	/* Every marker is in use: the producer waits for capacity. */
	return EAGAIN;
}

/*
 * Binds a reserved marker to a fence value.
 */
int
ghci_jobs_commit(
	struct ghci_job_table *table,
	unsigned index,
	void *completion,
	uint32_t fence,
	uint32_t completed,
	int *passed)
{
	struct ghci_job_marker *marker;
	int reached;

	/* Refuses an index outside the table. */
	*passed = 0;
	if (index >= GHCI_JOB_MARKERS)
		return ESTALE;

	/* Refuses a marker that is not this completion's reservation. */
	marker = &table->markers[index];
	if (marker->state != GHCI_JOB_RESERVED)
		return ESTALE;
	if (marker->completion != completion)
		return ESTALE;

	/* The reservation ends either way. */
	table->reserved--;

	/* A fence that has already passed completes the marker at once: the caller does it. */
	reached = ghci_fence_passed(completed, fence);
	if (reached != 0) {
		marker->state = GHCI_JOB_FREE;
		marker->completion = NULL;
		*passed = 1;
		return 0;
	}

	/* Otherwise the marker waits for the fence; it now counts as committed. */
	marker->state = GHCI_JOB_COMMITTED;
	marker->fence = fence;
	table->committed++;

	/* Succeeded: the marker completes when the fence passes. */
	return 0;
}

/*
 * Withdraws a reserved marker or ends it with an error.
 */
int
ghci_jobs_cancel(
	struct ghci_job_table *table,
	unsigned index,
	void *completion,
	unsigned fault)
{
	struct ghci_job_marker *marker;

	/* Only a normal and a fault cancel exist. */
	if (fault > 1U)
		return EINVAL;

	/* Refuses an index outside the table. */
	if (index >= GHCI_JOB_MARKERS)
		return ESTALE;

	/* Refuses a marker that does not hold this completion. */
	marker = &table->markers[index];
	if (marker->state == GHCI_JOB_FREE)
		return ESTALE;
	if (marker->completion != completion)
		return ESTALE;

	/* A normal cancel withdraws a reservation only; nothing completes. */
	if (fault == 0U) {
		/* A committed marker is not withdrawn: it completes with its fence. */
		if (marker->state != GHCI_JOB_RESERVED)
			return ESTALE;

		marker->state = GHCI_JOB_FREE;
		marker->completion = NULL;
		table->reserved--;
		return 0;
	}

	/* A fault cancel of a reservation keeps the completion, which ends with an error. */
	if (marker->state == GHCI_JOB_RESERVED) {
		marker->state = GHCI_JOB_FAILED;
		table->reserved--;
		table->committed++;
		return 0;
	}

	/* A fault cancel of a committed marker also ends it with an error. */
	if (marker->state == GHCI_JOB_COMMITTED)
		marker->state = GHCI_JOB_FAILED;

	/* Succeeded: the marker ends with an error at the next retirement. */
	return 0;
}

/*
 * Reports how many markers a reservation could take now.
 */
unsigned
ghci_jobs_capacity(
	const struct ghci_job_table *table)
{
	/* Reports the markers neither reserved nor waiting. */
	return GHCI_JOB_MARKERS - table->reserved - table->committed;
}

/*
 * Reports the index of the marker a token points at.
 */
int
ghci_jobs_index(
	const struct ghci_job_table *table,
	const void *token,
	unsigned *index)
{
	unsigned at;

	/* Compares the token with each marker's address before it is used. */
	for (at = 0U; at < GHCI_JOB_MARKERS; at++) {
		if (token == (const void *)&table->markers[at]) {
			*index = at;
			return 0;
		}
	}

	/* The token is not one of the session's markers. */
	return ESTALE;
}

/*
 * Frees every marker that is done and hands its completion back.
 */
unsigned
ghci_jobs_retire(
	struct ghci_job_table *table,
	uint32_t completed,
	void **done,
	int *errors,
	unsigned capacity)
{
	struct ghci_job_marker *marker;
	unsigned count;
	unsigned at;
	int reached;

	/* Looks at every marker that waits, while the caller has room for it. */
	count = 0U;
	for (at = 0U; at < GHCI_JOB_MARKERS; at++) {
		/* Stops once the caller's arrays are full; the rest wait for the next call. */
		if (count == capacity)
			break;

		/* A failed marker completes with an error. */
		marker = &table->markers[at];
		if (marker->state == GHCI_JOB_FAILED) {
			done[count] = marker->completion;
			errors[count] = EIO;
			count++;
			marker->state = GHCI_JOB_FREE;
			marker->completion = NULL;
			table->committed--;
			continue;
		}

		/* Markers that are free or still reserved wait for nothing here. */
		if (marker->state != GHCI_JOB_COMMITTED)
			continue;

		/* A committed marker completes once its fence has passed. */
		reached = ghci_fence_passed(completed, marker->fence);
		if (reached != 0) {
			done[count] = marker->completion;
			errors[count] = 0;
			count++;
			marker->state = GHCI_JOB_FREE;
			marker->completion = NULL;
			table->committed--;
		}
	}

	/* Reports the number of completions handed back. */
	return count;
}

/*
 * Turns every committed marker into a failed one.
 */
void
ghci_jobs_fail(
	struct ghci_job_table *table)
{
	unsigned at;

	/* The work the markers wait for will never complete. */
	for (at = 0U; at < GHCI_JOB_MARKERS; at++) {
		if (table->markers[at].state == GHCI_JOB_COMMITTED)
			table->markers[at].state = GHCI_JOB_FAILED;
	}
}
