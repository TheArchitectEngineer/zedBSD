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
 *
 * An import for the display (GPU_IMPORT_SCANOUT) is refused with ENOTSUP
 * unless the display controller can scan the exported image where it
 * is: an image of one of the controller's modes, its first row and its
 * stride on the memory line, in local memory on a GPU that has it.  The
 * caller then copies its frames instead.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-share.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-scanout.h>

#include "ghci.h"
#include "vk/vk-node.h"

/*
 * One exported allocation: the device, the memory it holds, and the
 * image the export described (has_image 0 for an allocation-only
 * export), whose first row lies at gpu_address.  It lives from the
 * export to its release, whatever happens to the sessions.
 */
struct ghci_share {
	struct ghci_device *device;
	struct ghci_allocation *memory;
	int has_image;
	struct gpu_image_descriptor image;
	uint32_t gpu_address;
};

static int share_export(void *private_data, void *opaque, void *object, const struct gpu_image_descriptor *image, void **result);
static void share_release(void *private_data, void *shared);
static int share_import(void *private_data, void *opaque, void *shared, uint32_t flags, void **result, uint32_t *resource_id);
static int share_scannable(struct ghci_device *device, const struct ghci_share *share);

/* The sharing operations; the table lives as long as the kernel. */
const struct drv_gpu_share_ops ghci_share_ops = {
	share_export,
	share_release,
	share_import,
	NULL
};

/*
 * Exports a session's allocation: the record holds its memory and keeps
 * the image, when there is one, for an import for the display.  The core
 * checked the image against the resource; every GHCI allocation is
 * linear memory.
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
	share->gpu_address = allocation->gpu_address;
	if (image != NULL) {
		share->has_image = 1;
		share->image = *image;
		share->gpu_address = allocation->gpu_address + (uint32_t)image->offset;
	}
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
 * same memory and holds it, named by its GPU address.  An import for the
 * display needs an image the controller scans where it is.
 */
static int
share_import(
	void *private_data,
	void *opaque,
	void *shared,
	uint32_t flags,
	void **result,
	uint32_t *resource_id)
{
	struct ghci_device *device;
	struct ghci_session *session;
	struct ghci_share *share;
	struct ghci_allocation *alias;
	int scannable;
	int error;

	/* An export of this device only. */
	device = private_data;
	session = opaque;
	share = shared;
	if (share->device != device)
		return EINVAL;

	/* An import for the display only of an image the controller scans; the caller copies otherwise. */
	if ((flags & GPU_IMPORT_SCANOUT) != 0U) {
		scannable = share_scannable(device, share);
		if (!scannable)
			return ENOTSUP;
	}

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

	/*
	 * A protected importer is granted the memory (it is another
	 * session's, or nobody's after its owner let it go), before any frame
	 * of its that uses the import; without a grant entry there is no
	 * import.
	 */
	if (error == 0) {
		error = ghci_vk_prot_grant(device, session, alias);
		if (error != 0) {
			ghci_allocation_drop(device, session, alias);
			alias = NULL;
		}
	}

	mutex_unlock(&device->lock);

	/* A session that holds as many resources as it may, or no grant. */
	if (error != 0) {
		if (alias != NULL)
			kern_free(alias);
		return error;
	}

	/* Succeeded: the import is the session's resource. */
	*result = alias;
	*resource_id = alias->gpu_address;
	return 0;
}

/*
 * Reports whether the display controller can scan an export's image
 * where it is: 1 when it can.
 */
static int
share_scannable(
	struct ghci_device *device,
	const struct ghci_share *share)
{
	const struct ghci_display_mode *mode;
	uint32_t line;
	unsigned index;
	int sized;

	/* A device without a display scans nothing; an allocation-only export has no image. */
	if (!device->display.present)
		return 0;
	if (!share->has_image)
		return 0;

	/* The controller scans local memory only, on a GPU that has it. */
	if (device->local_size != 0U && share->memory->kind == GHCI_MEMORY_HOST)
		return 0;

	/* The first row and the stride lie on the memory line. */
	line = device->line_bytes;
	if (line == 0U)
		return 0;
	if ((share->gpu_address % line) != 0U)
		return 0;
	if ((share->image.stride % line) != 0U)
		return 0;

	/* The image is the size of one of the controller's modes (it has no scaler). */
	sized = 0;
	for (index = 0U; index < device->display.mode_count; index++) {
		mode = &device->display.modes[index];
		if (mode->width == share->image.width && mode->height == share->image.height)
			sized = 1;
	}
	if (!sized)
		return 0;

	/* Succeeded: the controller scans the image where it is. */
	return 1;
}
