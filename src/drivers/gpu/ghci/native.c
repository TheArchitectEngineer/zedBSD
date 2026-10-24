/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The native command of the test kernels: GPU_OP_GHCI_NATIVE, a stream of
 * standard micro-operations a test writes itself, checked (ghci_native_
 * prepare: only blits, fences, waits and invalidations, every rectangle
 * and fence inside the session's memory), given the session's job tag and
 * copied into the ring followed by the driver's fence.  The command
 * returns once the packets are in the ring; the test waits for a fence of
 * its own.  Only a kernel built with CONFIG_GHCI_NATIVE links this file.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>

#include "ghci.h"

/* The most dwords of one native command, well inside the ring with its fence. */
#define NATIVE_DWORDS_MAX		8192U

static int native_owned(void *context, uint32_t address, uint32_t bytes);

/*
 * Checks and submits a GPU_OP_GHCI_NATIVE command from root.
 */
int
ghci_native_command(
	struct ghci_device *device,
	struct ghci_session *session,
	const void *stream,
	uint32_t bytes)
{
	struct ghci_native_check check;
	struct ghci_ticket ticket;
	uint32_t *packets;
	uint32_t dwords;
	int error;

	/* Whole dwords, within the bound. */
	if ((bytes % 4U) != 0U)
		return EINVAL;
	if (bytes / 4U > NATIVE_DWORDS_MAX)
		return EINVAL;

	/* The copy of the checked packets. */
	packets = kern_malloc(bytes);
	if (packets == NULL)
		return ENOMEM;

	/* The check sees the session's allocations as they are while the packets go in. */
	check.owned = native_owned;
	check.context = session;
	check.tag = session->tag;
	mutex_lock(&device->lock);

	error = ghci_native_prepare(stream, bytes / 4U, &check, packets, bytes / 4U, &dwords);
	if (error == 0)
		error = ghci_cp_submit(device, session, packets, dwords, &ticket);

	mutex_unlock(&device->lock);

	/* The copy goes either way. */
	kern_free(packets);

	/* Reports a stream the check refused, or a ring that took nothing. */
	if (error != 0)
		return error;

	/* Succeeded: the packets are in the ring. */
	return 0;
}

/* Reports whether a run of GPU addresses is inside one of the session's allocations. */
static int
native_owned(
	void *context,
	uint32_t address,
	uint32_t bytes)
{
	int owned;

	/* The session's list, under the device's lock the caller holds. */
	owned = ghci_memory_owned(context, address, bytes);

	/* Reports the answer. */
	return owned;
}
