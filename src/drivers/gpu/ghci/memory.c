/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI device's memory: the allocators of local memory (the range the
 * local window shows and the part past it), host RAM, the aperture that
 * bounds every access of the GPU, the local window's switch, and the
 * allocations that wait for the GPU before they go back.
 *
 * The placement follows GHCI section 11.3: what the processor writes or
 * reads (the ring, the fences, the bounce buffer, mapped memory) is host
 * memory, and memory only the GPU uses is local on a discrete GPU.  The
 * processor never reaches local memory itself; copies by the GPU move
 * data between the two.
 *
 * Host memory is reached by the GPU at its bus address, which on the PC
 * host and on the VisionFive 2's root complexes is its physical address
 * (the host bridge maps bus addresses one to one); it must lie outside
 * the GPU's local range, below the GPU's HOST_LIMIT and inside the
 * aperture, and so below 4 GiB.  Where the GPU does not snoop the
 * processor's caches (host_uncached), the processor's view of host
 * memory is an uncached second mapping of it, made when the memory is
 * taken and removed before it goes back.
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

/*
 * The uncached views of RAM.
 *
 * A HAL without uncached views leaves them out of the kernel; they are
 * weak so that the kernel links, and a null test refuses host memory to
 * a device that needs the view (host_uncached) on such a kernel.
 */
extern int kern_pmem_map_uncached(const struct kern_pmem *run, void **mapped) __attribute__((weak));
extern int kern_pmem_unmap_uncached(void *mapped, size_t size) __attribute__((weak));

static int range_make(struct ghci_range_map *map, struct ghci_range_extent **extents, uint64_t base, uint64_t limit);
static enum ghci_memory_kind kind_for(const struct ghci_device *device, enum ghci_memory_use use);
static int local_take(struct ghci_device *device, struct ghci_allocation *allocation, enum ghci_memory_kind kind);
static int host_take(struct ghci_device *device, struct ghci_allocation *allocation);
static int host_usable(const struct ghci_device *device, const struct kern_pmem *run);
static int host_view(struct ghci_device *device, struct ghci_allocation *allocation);
static int host_give(struct ghci_device *device, struct ghci_allocation *allocation);
static uint64_t host_end(const struct ghci_device *device);

/*
 * Sets up the aperture and the allocators of local memory.
 */
int
ghci_memory_init(
	struct ghci_device *device)
{
	uint64_t line_mask;
	uint64_t local_end;
	uint64_t aperture_end;
	uint64_t window_end;
	int error;

	/*
	 * The aperture is aligned to the memory line (to the 4 KiB granule of
	 * session protection when the GPU has it: the table's granules are the
	 * pages); its limit register cannot say 2^32.
	 */
	line_mask = (uint64_t)device->line_bytes - 1U;
	if (device->protection && line_mask < GHCI_PAGE_BYTES - 1U)
		line_mask = GHCI_PAGE_BYTES - 1U;
	local_end = (uint64_t)device->local_base + device->local_size;

	/*
	 * The aperture covers the host addresses and the local memory: from
	 * the first line (address 0 is never reached) to whichever of the two
	 * ends higher.
	 */
	aperture_end = host_end(device);
	if (local_end > aperture_end)
		aperture_end = local_end;
	if (aperture_end > ADDRESS_SPACE_END - (line_mask + 1U))
		aperture_end = ADDRESS_SPACE_END - (line_mask + 1U);
	device->aperture_base = (uint32_t)(line_mask + 1U);
	device->aperture_limit = (uint32_t)(aperture_end & ~line_mask);

	/* Local memory past the aperture's end is not used. */
	if (local_end > device->aperture_limit)
		local_end = device->aperture_limit;

	/* The range of local memory the window shows. */
	window_end = (uint64_t)device->local_base + device->local_window;
	if (window_end > local_end)
		window_end = local_end;
	if (device->local_window != 0U) {
		error = range_make(&device->windowed, &device->windowed_extents, device->local_base, window_end);
		if (error != 0)
			return error;
	}

	/* The rest of local memory. */
	if (device->local_size != 0U && window_end < local_end) {
		error = range_make(&device->unwindowed, &device->unwindowed_extents, window_end, local_end);
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
	/* The table of the window range's allocator. */
	if (device->windowed_extents != NULL) {
		kern_free(device->windowed_extents);
		device->windowed_extents = NULL;
	}

	/* The table of the allocator of the rest of local memory. */
	if (device->unwindowed_extents != NULL) {
		kern_free(device->unwindowed_extents);
		device->unwindowed_extents = NULL;
	}
}

/*
 * Opens or closes the local window to the host.
 *
 * The driver does not use the window: it closes it while it drives the
 * device, so that a stray access through the window cannot change local
 * memory, and opens it again (as after a reset) when it lets the device go.
 * The switch follows the GPU's window, also when the bus left it unmapped.
 */
void
ghci_memory_window_set(
	struct ghci_device *device,
	int open)
{
	uint32_t control;

	/* A GPU without a window has nothing to switch. */
	if (device->mem_registers == 0U || device->hardware_window == 0U)
		return;

	/* WINDOW_CTRL.ENABLE; CTRL.RESET does not change it. */
	control = 0U;
	if (open)
		control = GHCI_MEM_WINDOW_ENABLE;
	ghci_write(device, device->mem_registers + GHCI_MEM_WINDOW_CTRL, control);
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

	/* The memory of the use's kind; local memory comes from the window's range when the rest is full. */
	kind = kind_for(device, use);
	if (kind == GHCI_MEMORY_HOST) {
		error = host_take(device, allocation);
	} else {
		error = local_take(device, allocation, GHCI_MEMORY_UNWINDOWED);
		if (error == ENOMEM)
			error = local_take(device, allocation, GHCI_MEMORY_WINDOWED);
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
	if (allocation->kind == GHCI_MEMORY_WINDOWED) {
		error = ghci_range_free(&device->windowed, allocation->gpu_address);
	} else if (allocation->kind == GHCI_MEMORY_UNWINDOWED) {
		error = ghci_range_free(&device->unwindowed, allocation->gpu_address);
	} else {
		error = host_give(device, allocation);
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
	struct ghci_allocation *backing;
	struct ghci_ticket ticket;
	int done;

	/* A blob that shows a Vulkan memory object has no memory of its own: it lets that memory go. */
	if (allocation->backing != NULL) {
		backing = allocation->backing;
		kern_free(allocation);
		ghci_memory_unhold(device, backing);
		return;
	}

	/*
	 * An allocation that is still held stays until its last user lets it
	 * go, and then waits for every fence issued now: the newest fence is
	 * at least the owner's last one.
	 */
	if (allocation->holds != 0U) {
		ghci_cp_last(device, &ticket);
		allocation->released = 1;
		allocation->session = NULL;
		allocation->fence = ticket.fence;
		allocation->generation = ticket.generation;
		return;
	}

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
 * Keeps an allocation from going back while its user still writes it.
 */
void
ghci_memory_hold(
	struct ghci_allocation *allocation)
{
	/* One more user keeps the memory. */
	allocation->holds++;
}

/*
 * Lets a held allocation go.
 */
void
ghci_memory_unhold(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	struct ghci_ticket ticket;

	/* One user fewer; the others keep it. */
	allocation->holds--;
	if (allocation->holds != 0U)
		return;

	/* An allocation its owner still has stays with the owner. */
	if (!allocation->released)
		return;

	/*
	 * The owner gave it back meanwhile: it waits for the newest fence now,
	 * since the users that held it (another session's work among them) may
	 * have submitted work on it after the owner let it go.
	 */
	allocation->released = 0;
	ghci_cp_last(device, &ticket);
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
	/* Memory only the GPU uses is local on a discrete GPU (past the window first, see ghci_memory_alloc). */
	if (use == GHCI_USE_DEVICE && device->local_size != 0U)
		return GHCI_MEMORY_UNWINDOWED;

	/* What the processor writes or reads, and the driver's own buffers, are host memory. */
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
	int error;

	/* The allocator of the kind; a GPU may have no memory of it. */
	map = &device->unwindowed;
	if (kind == GHCI_MEMORY_WINDOWED)
		map = &device->windowed;
	if (map->extents == NULL)
		return ENOMEM;

	/* The run, aligned to a page. */
	error = ghci_range_alloc(map, allocation->bytes, GHCI_PAGE_BYTES, &address);
	if (error == ENOSPC)
		return ENOMEM;
	if (error != 0)
		return error;

	/* The GPU's address of it; the processor has no view of local memory. */
	allocation->kind = kind;
	allocation->gpu_address = address;
	allocation->view = NULL;
	allocation->physical = 0U;

	/* Succeeded: the run is the allocation's. */
	return 0;
}

/*
 * Takes host RAM for an allocation: one contiguous run below the GPU's
 * HOST_LIMIT that lies inside the aperture and outside the local range.
 * A run that does not is held while another is tried, so it is not
 * handed out again, and every held run goes back at the end.
 */
static int
host_take(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	struct kern_pmem held[GHCI_HOST_TRIES];
	struct kern_pmem run;
	uint64_t limit;
	unsigned held_count;
	unsigned index;
	int usable;
	int error;

	/* The end of the host addresses the GPU reaches inside its aperture. */
	limit = host_end(device);
	if (limit > device->aperture_limit)
		limit = device->aperture_limit;

	/* Tries a bounded number of runs. */
	held_count = 0U;
	error = ENOMEM;
	for (index = 0U; index < GHCI_HOST_TRIES; index++) {
		/* A run the GPU can name. */
		error = kern_pmem_alloc_limited(allocation->bytes, GHCI_PAGE_BYTES, limit - 1U, 0U, &run);
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

	/* The GPU's address of it is its bus address. */
	allocation->kind = GHCI_MEMORY_HOST;
	allocation->gpu_address = (uint32_t)allocation->run.paddr;
	allocation->physical = allocation->run.paddr;

	/* The processor's view of it: the direct map, or an uncached one. */
	error = host_view(device, allocation);
	if (error != 0) {
		(void)kern_pmem_free(&allocation->run);
		return error;
	}

	/* The bytes of host memory the device holds. */
	device->host_bytes += allocation->bytes;

	/* Succeeded: the run is the allocation's. */
	return 0;
}

/* Reports whether a run of RAM is a host address range inside the aperture, outside the local range: 1 when it is. */
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

	/* It must lie inside the aperture and below HOST_LIMIT. */
	if (start < device->aperture_base)
		return 0;
	if (end > device->aperture_limit)
		return 0;
	if (end > host_end(device))
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

/*
 * Gives the processor its view of a host allocation: the direct map where
 * the GPU snoops the caches, else an uncached mapping, so that neither
 * side needs cache maintenance.
 */
static int
host_view(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	void *mapped;
	int error;

	/* A GPU that snoops the caches shares the direct map with the processor. */
	if (!device->host_uncached) {
		allocation->view = kern_pmem_to_kernel(allocation->run.paddr);
		return 0;
	}

	/* A kernel without uncached views cannot give one. */
	if (kern_pmem_map_uncached == NULL) {
		kern_logf("ghci: %s: no uncached view of host memory on this kernel\n", device->name);
		return ENOTSUP;
	}

	/* The uncached view; the cached lines of the run are written back and dropped first. */
	error = kern_pmem_map_uncached(&allocation->run, &mapped);
	if (error != 0) {
		kern_logf("ghci: %s: no uncached view of host memory (%d)\n", device->name, error);
		return error;
	}

	/* Succeeded: the processor reaches the run through the uncached view. */
	allocation->view = (uint8_t *)mapped;
	return 0;
}

/* Removes the processor's view of a host allocation and gives its RAM back. */
static int
host_give(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	int error;

	/*
	 * The uncached view goes first; a view that cannot be removed keeps
	 * its RAM, which is not handed out again while it may be reached.
	 */
	if (device->host_uncached && kern_pmem_unmap_uncached != NULL) {
		error = kern_pmem_unmap_uncached(allocation->view, allocation->run.size);
		if (error != 0)
			return error;
	}

	/* The RAM goes back. */
	error = kern_pmem_free(&allocation->run);
	if (error != 0)
		return error;

	/* Succeeded: the device holds the bytes no more. */
	device->host_bytes -= allocation->bytes;
	return 0;
}

/* Reports the end of the GPU's host addresses: HOST_LIMIT, or 2^32 when it says all. */
static uint64_t
host_end(
	const struct ghci_device *device)
{
	/* A GPU that reports no limit reaches the host up to the end of its address space. */
	if (device->host_limit == 0U)
		return ADDRESS_SPACE_END;

	/* Succeeded: the limit the GPU reported. */
	return device->host_limit;
}
