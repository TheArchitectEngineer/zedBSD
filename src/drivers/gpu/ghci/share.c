/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sharing of a GHCI device's memory between its sessions: an
 * exported allocation (an image for a compositor, or any allocation) and
 * its imports into other sessions of the same device.
 *
 * An export is a record that holds the allocation's memory (the memory a
 * blob of a Vulkan memory object shows, when the resource is such a
 * blob), so the memory stays while the export lives, even after the
 * exporting session gave it back.  An import is a blob of the importing
 * session that shows the same memory and holds it too.  The memory goes
 * back once its owner, every export and every import have let it go,
 * after the newest fence then issued.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-share.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "ghci.h"

/*
 * One exported allocation: the device and the memory it holds.  It lives
 * from the export to its release, whatever happens to the sessions.
 */
struct ghci_share {
	struct ghci_device *device;
	struct ghci_allocation *memory;
};

static int share_export(void *private_data, void *opaque, void *object, const struct gpu_image_descriptor *image, void **result);
static void share_release(void *private_data, void *shared);
static int share_import(void *private_data, void *opaque, void *shared, void **result, uint32_t *resource_id);

/* The sharing operations; the table lives as long as the kernel. */
const struct drv_gpu_share_ops ghci_share_ops = {
	share_export,
	share_release,
	share_import,
	NULL
};

/*
 * Exports a session's allocation: the record holds its memory.  The
 * image, when there is one, was checked by the core against the
 * resource; every GHCI allocation is linear memory, so it adds nothing.
 */
static int
share_export(
	void *private_data,
	void *opaque,
	void *object,
	const struct gpu_image_descriptor *image,
	void **result)
{
	struct ghci_device *device;
	struct ghci_allocation *allocation;
	struct ghci_share *share;

	(void)opaque;
	(void)image;

	/* The record. */
	device = private_data;
	allocation = object;
	share = kern_calloc(1U, sizeof(*share));
	if (share == NULL)
		return ENOMEM;

	/* It holds the memory itself: a blob of a Vulkan memory object holds the memory it shows. */
	mutex_lock(&device->lock);

	share->device = device;
	share->memory = allocation;
	if (allocation->backing != NULL)
		share->memory = allocation->backing;
	ghci_memory_hold(share->memory);

	mutex_unlock(&device->lock);

	/* Succeeded: the export is the core's. */
	*result = share;
	return 0;
}

/* Releases an export: its memory goes back if nothing else has it any more. */
static void
share_release(
	void *private_data,
	void *shared)
{
	struct ghci_device *device;
	struct ghci_share *share;

	/* The memory is let go under the device's lock. */
	device = private_data;
	share = shared;
	mutex_lock(&device->lock);

	ghci_memory_unhold(device, share->memory);

	mutex_unlock(&device->lock);

	/* The memory may now be on the deferred list; the record goes. */
	ghci_service_wake(device, GHCI_WORK_RECLAIM);
	kern_free(share);
}

/*
 * Imports an export into a session: a blob of the session that shows the
 * same memory and holds it, named by its GPU address.
 */
static int
share_import(
	void *private_data,
	void *opaque,
	void *shared,
	void **result,
	uint32_t *resource_id)
{
	struct ghci_device *device;
	struct ghci_session *session;
	struct ghci_share *share;
	struct ghci_allocation *alias;
	int error;

	/* An export of this device only. */
	device = private_data;
	session = opaque;
	share = shared;
	if (share->device != device)
		return EINVAL;

	/* The blob's record. */
	alias = kern_calloc(1U, sizeof(*alias));
	if (alias == NULL)
		return ENOMEM;

	/* The blob shows the memory, holds it, and is listed as the session's. */
	mutex_lock(&device->lock);

	error = 0;
	if (session->allocation_count >= GHCI_SESSION_RESOURCES) {
		error = ENOSPC;
	} else {
		alias->session = session;
		alias->kind = share->memory->kind;
		alias->gpu_address = share->memory->gpu_address;
		alias->bytes = share->memory->bytes;
		alias->requested = share->memory->requested;
		alias->view = share->memory->view;
		alias->physical = share->memory->physical;
		alias->blob = 1;
		alias->backing = share->memory;
		ghci_memory_hold(share->memory);
		alias->next = session->allocations;
		session->allocations = alias;
		session->allocation_count++;
	}

	mutex_unlock(&device->lock);

	/* A session that holds as many resources as it may. */
	if (error != 0) {
		kern_free(alias);
		return error;
	}

	/* Succeeded: the import is the session's resource. */
	*result = alias;
	*resource_id = alias->gpu_address;
	return 0;
}
