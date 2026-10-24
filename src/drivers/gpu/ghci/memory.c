/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI device's memory: the allocators of local memory (the part in
 * the local window and the part past it), host RAM below 4 GiB, the
 * aperture that bounds every access of the GPU, and the allocations that
 * wait for the GPU before they go back.
 *
 * Host memory is reached by the GPU at its bus address, which on the PC
 * host is its physical address (the host bridge maps bus addresses one
 * to one); it must lie outside the GPU's local range, inside the
 * aperture and below 4 GiB.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include "ghci.h"
#include "hcr.h"

/* The end of the 32-bit GPU address space. */
#define ADDRESS_SPACE_END		0x100000000ULL

static int range_make(struct ghci_range_map *map, struct ghci_range_extent **extents, uint64_t base, uint64_t limit);
static enum ghci_memory_kind kind_for(const struct ghci_device *device, enum ghci_memory_use use);
static int local_take(struct ghci_device *device, struct ghci_allocation *allocation, enum ghci_memory_kind kind);
static int host_take(struct ghci_device *device, struct ghci_allocation *allocation);
static int host_usable(const struct ghci_device *device, const struct kern_pmem *run);

/*
 * Sets up the allocators from the local memory and the window.
 */
int
ghci_memory_init(
	struct ghci_device *device)
{
	uint64_t line_mask;
	uint64_t local_end;
	uint64_t usable_end;
	uint64_t window_end;
	int error;

	/* The aperture is aligned to the memory line; its limit register cannot say 2^32. */
	line_mask = (uint64_t)device->line_bytes - 1U;
	local_end = (uint64_t)device->local_base + device->local_size;
	usable_end = local_end;
	if (usable_end > ADDRESS_SPACE_END - device->line_bytes)
		usable_end = ADDRESS_SPACE_END - device->line_bytes;

	/*
	 * A discrete GPU with a window keeps everything in local memory, so
	 * its aperture is exactly the local memory.  Otherwise the GPU also
	 * reaches host memory, and the aperture opens the 32-bit space but
	 * its first line, so that address 0 is never reached.
	 */
	if (device->local_window != 0U) {
		device->aperture_base = (uint32_t)(device->local_base & ~line_mask);
		device->aperture_limit = (uint32_t)(usable_end & ~line_mask);
	} else {
		device->aperture_base = device->line_bytes;
		device->aperture_limit = (uint32_t)(ADDRESS_SPACE_END - device->line_bytes);
	}

	/* The part of local memory the window shows. */
	window_end = (uint64_t)device->local_base + device->local_window;
	if (window_end > usable_end)
		window_end = usable_end;
	if (device->local_window != 0U) {
		error = range_make(&device->visible, &device->visible_extents, device->local_base, window_end);
		if (error != 0)
			return error;
	}

	/* The rest of local memory, which only the GPU reaches. */
	if (device->local_size != 0U && window_end < usable_end) {
		error = range_make(&device->hidden, &device->hidden_extents, window_end, usable_end);
		if (error != 0) {
			ghci_memory_fini(device);
			return error;
		}
	}

	/* Succeeded: the allocators are ready. */
	return 0;
}

/*
 * Gives back the allocators' tables at the detach.
 */
void
ghci_memory_fini(
	struct ghci_device *device)
{
	/* The table of the window's allocator. */
	if (device->visible_extents != NULL) {
		kern_free(device->visible_extents);
		device->visible_extents = NULL;
	}

	/* The table of the hidden part's allocator. */
	if (device->hidden_extents != NULL) {
		kern_free(device->hidden_extents);
		device->hidden_extents = NULL;
	}
}

/*
 * Takes memory for a use.
 */
int
ghci_memory_alloc(
	struct ghci_device *device,
	struct ghci_session *session,
	enum ghci_memory_use use,
	uint64_t bytes,
	struct ghci_allocation **result)
{
	struct ghci_allocation *allocation;
	enum ghci_memory_kind kind;
	uint64_t pages_bytes;
	int error;

	/* Whole pages, inside what one 32-bit run can be. */
	pages_bytes = (bytes + GHCI_PAGE_BYTES - 1U) & ~((uint64_t)GHCI_PAGE_BYTES - 1U);
	if (pages_bytes == 0U || pages_bytes > 0x80000000ULL)
		return EINVAL;

	/* The allocation's record. */
	allocation = kern_calloc(1U, sizeof(*allocation));
	if (allocation == NULL)
		return ENOMEM;

	/* The record before the memory is found. */
	allocation->session = session;
	allocation->bytes = (uint32_t)pages_bytes;
	allocation->requested = bytes;

	/* The memory of the use's kind; device-local memory may also come from the window. */
	kind = kind_for(device, use);
	if (kind == GHCI_MEMORY_HOST) {
		error = host_take(device, allocation);
	} else {
		error = local_take(device, allocation, kind);
		if (error == ENOMEM &&
		    kind == GHCI_MEMORY_HIDDEN &&
		    device->local_window != 0U)
			error = local_take(device, allocation, GHCI_MEMORY_VISIBLE);
	}

	/* No memory: the record goes too. */
	if (error != 0) {
		kern_free(allocation);
		return error;
	}

	/* Succeeded: the memory is the caller's. */
	*result = allocation;
	return 0;
}

/*
 * Gives memory back now.
 */
void
ghci_memory_free(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	int error;

	/* Gives the memory back to where it came from. */
	error = 0;
	if (allocation->kind == GHCI_MEMORY_VISIBLE) {
		error = ghci_range_free(&device->visible, allocation->gpu_address);
	} else if (allocation->kind == GHCI_MEMORY_HIDDEN) {
		error = ghci_range_free(&device->hidden, allocation->gpu_address);
	} else {
		error = kern_pmem_free(&allocation->run);
		device->host_bytes -= allocation->bytes;
	}

	/* A run the allocator does not know is reported, not given back twice. */
	if (error != 0)
		kern_logf("ghci: %s: memory at %08x not given back (%d)\n", device->name, (unsigned)allocation->gpu_address, error);

	/* The record goes. */
	kern_free(allocation);
}

/*
 * Gives a session's allocation back now, or once the session's last fence has passed.
 */
void
ghci_memory_release(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	struct ghci_session *session;
	struct ghci_ticket ticket;
	int done;

	/* An allocation of a session that has submitted nothing since its last fence passed goes now. */
	session = allocation->session;
	if (session == NULL || !session->outstanding) {
		ghci_memory_free(device, allocation);
		return;
	}

	/* The session's work may still use it: it waits for the session's last fence. */
	done = ghci_cp_done(device, session->last_fence, session->last_generation);
	if (done) {
		session->outstanding = 0;
		ghci_memory_free(device, allocation);
		return;
	}

	/* The deferred list keeps it, with the fence that frees it. */
	ticket.fence = session->last_fence;
	ticket.generation = session->last_generation;
	ghci_memory_defer(device, allocation, &ticket);
}

/*
 * Keeps an allocation on the deferred list until a ticket is done.
 */
void
ghci_memory_defer(
	struct ghci_device *device,
	struct ghci_allocation *allocation,
	const struct ghci_ticket *ticket)
{
	/* The allocation belongs to no session any more; the ticket frees it. */
	allocation->session = NULL;
	allocation->fence = ticket->fence;
	allocation->generation = ticket->generation;
	allocation->next = device->deferred;
	device->deferred = allocation;
}

/*
 * Gives back every deferred allocation whose fence is done.
 */
uint64_t
ghci_memory_reclaim(
	struct ghci_device *device)
{
	struct ghci_allocation **position;
	struct ghci_allocation *allocation;
	uint64_t given;
	int done;

	/* Walks the deferred list, freeing each allocation the GPU is done with. */
	given = 0U;
	position = &device->deferred;
	while (*position != NULL) {
		allocation = *position;
		done = ghci_cp_done(device, allocation->fence, allocation->generation);
		if (!done) {
			position = &allocation->next;
			continue;
		}

		/* Unlinks and frees it. */
		*position = allocation->next;
		given += allocation->bytes;
		ghci_memory_free(device, allocation);
	}

	/* Reports the bytes given back. */
	return given;
}

/*
 * Reports whether a run of GPU addresses lies inside one of a session's allocations.
 */
int
ghci_memory_owned(
	struct ghci_session *session,
	uint32_t address,
	uint32_t bytes)
{
	struct ghci_allocation *allocation;
	uint64_t end;
	uint64_t allocation_end;

	/* The run asked about, which may end at 2^32. */
	end = (uint64_t)address + bytes;

	/* Looks at each of the session's allocations. */
	for (allocation = session->allocations;
	     allocation != NULL;
	     allocation = allocation->next) {
		/* The run must start inside the allocation... */
		if (address < allocation->gpu_address)
			continue;

		/* ...and end inside it. */
		allocation_end = (uint64_t)allocation->gpu_address + allocation->bytes;
		if (end <= allocation_end)
			return 1;
	}

	/* Succeeded: no allocation of the session holds the run. */
	return 0;
}

/* Starts one allocator of local memory with a table of its own. */
static int
range_make(
	struct ghci_range_map *map,
	struct ghci_range_extent **extents,
	uint64_t base,
	uint64_t limit)
{
	/* The table of the runs in use. */
	*extents = kern_calloc(GHCI_EXTENTS_MAX, sizeof(**extents));
	if (*extents == NULL)
		return ENOMEM;

	/* The allocator over the range. */
	ghci_range_init(map, base, limit, *extents, GHCI_EXTENTS_MAX);

	/* Succeeded: the allocator is empty. */
	return 0;
}

/* Chooses the kind of memory for a use (see ghci.h). */
static enum ghci_memory_kind
kind_for(
	const struct ghci_device *device,
	enum ghci_memory_use use)
{
	/* Memory only the GPU uses is local when the GPU has a part past the window. */
	if (use == GHCI_USE_DEVICE && device->hidden_extents != NULL)
		return GHCI_MEMORY_HIDDEN;

	/* Everything else is in the window when there is one. */
	if (device->local_window != 0U)
		return GHCI_MEMORY_VISIBLE;

	/* A GPU without a window into its local memory keeps the rest in host memory. */
	return GHCI_MEMORY_HOST;
}

/* Takes local memory of a kind for an allocation. */
static int
local_take(
	struct ghci_device *device,
	struct ghci_allocation *allocation,
	enum ghci_memory_kind kind)
{
	struct ghci_range_map *map;
	uint32_t address;
	uint64_t offset;
	int error;

	/* The allocator of the kind; a GPU may have no memory of it. */
	map = &device->hidden;
	if (kind == GHCI_MEMORY_VISIBLE)
		map = &device->visible;
	if (map->extents == NULL)
		return ENOMEM;

	/* The run, aligned to a page. */
	error = ghci_range_alloc(map, allocation->bytes, GHCI_PAGE_BYTES, &address);
	if (error == ENOSPC)
		return ENOMEM;
	if (error != 0)
		return error;

	/* The GPU's address of it, and for the window the processor's view. */
	allocation->kind = kind;
	allocation->gpu_address = address;
	allocation->view = NULL;
	allocation->physical = 0U;
	if (kind == GHCI_MEMORY_VISIBLE) {
		offset = (uint64_t)address - device->local_base;
		allocation->view = (uint8_t *)device->window + offset;
		allocation->physical = device->window_physical + offset;
	}

	/* Succeeded: the run is the allocation's. */
	return 0;
}

/*
 * Takes host RAM for an allocation: one contiguous run below 4 GiB that
 * lies inside the aperture and outside the local range.  A run that does
 * not is held while another is tried, so it is not handed out again, and
 * every held run goes back at the end.
 */
static int
host_take(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	struct kern_pmem held[GHCI_HOST_TRIES];
	struct kern_pmem run;
	unsigned held_count;
	unsigned index;
	int usable;
	int error;

	/* Tries a bounded number of runs. */
	held_count = 0U;
	error = ENOMEM;
	for (index = 0U; index < GHCI_HOST_TRIES; index++) {
		/* A run the GPU can name. */
		error = kern_pmem_alloc_limited(allocation->bytes, GHCI_PAGE_BYTES, (uint64_t)device->aperture_limit - 1U, 0U, &run);
		if (error != 0)
			break;

		/* A run the GPU reaches as host memory is taken. */
		usable = host_usable(device, &run);
		if (usable) {
			allocation->run = run;
			break;
		}

		/* Another run is held so that the next try finds a different one. */
		held[held_count] = run;
		held_count++;
		error = ENOMEM;
	}

	/* The held runs go back. */
	for (index = 0U; index < held_count; index++)
		(void)kern_pmem_free(&held[index]);

	/* No usable run. */
	if (error != 0)
		return ENOMEM;

	/* The GPU's address of it is its bus address; the processor uses the direct map. */
	allocation->kind = GHCI_MEMORY_HOST;
	allocation->gpu_address = (uint32_t)allocation->run.paddr;
	allocation->physical = allocation->run.paddr;
	allocation->view = kern_pmem_to_kernel(allocation->run.paddr);
	device->host_bytes += allocation->bytes;

	/* Succeeded: the run is the allocation's. */
	return 0;
}

/* Reports whether a run of RAM is inside the aperture and outside the local range: 1 when it is. */
static int
host_usable(
	const struct ghci_device *device,
	const struct kern_pmem *run)
{
	uint64_t start;
	uint64_t end;
	uint64_t local_end;

	/* The run's bus addresses. */
	start = run->paddr;
	end = start + run->size;

	/* It must lie inside the aperture. */
	if (start < device->aperture_base)
		return 0;
	if (end > device->aperture_limit)
		return 0;

	/* A GPU without local memory has no range to avoid. */
	if (device->local_size == 0U)
		return 1;

	/* It must not meet the local range, where the same addresses name the GPU's memory. */
	local_end = (uint64_t)device->local_base + device->local_size;
	if (end <= device->local_base)
		return 1;
	if (start >= local_end)
		return 1;

	/* Succeeded: the run meets the local range and is not usable. */
	return 0;
}
