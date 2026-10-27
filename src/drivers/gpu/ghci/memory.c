/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI device's memory: runs of host RAM the GPU reaches by DMA, and
 * the quarantine of a lost device.
 *
 * Host memory is reached by the GPU at its bus address, which on the PC
 * and on the VisionFive 2's root complexes is its physical address (the
 * host bridge maps bus addresses one to one).  The driver takes every run
 * below 4 GiB (CP_HEAP, the framebuffer and a flip's record are 32-bit
 * physical addresses, and the rest follows for simplicity), below the
 * GPU's HOST_LIMIT when it reports one, and outside its local memory,
 * where the same addresses name the GPU's own memory.  Where the GPU
 * does not snoop the processor's caches (host_uncached), the processor's
 * view of a run is an uncached second mapping of it, made when the run is
 * taken and removed before it goes back.
 *
 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 11.3,
 * the MEM block's HOST_LIMIT and the routing of physical addresses).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include "ghci.h"

/* The end of the 32-bit physical addresses every run of the driver lies below. */
#define ADDRESS_SPACE_END		0x100000000ULL

/* The tries at a run of host memory that does not fall into the local range. */
#define HOST_TRIES			8U

/*
 * The uncached views of RAM.
 *
 * A HAL without uncached views leaves them out of the kernel; they are
 * weak so that the kernel links, and a null test refuses host memory to
 * a device that needs the view (host_uncached) on such a kernel.
 */
extern int kern_pmem_map_uncached(const struct kern_pmem *run, void **mapped) __attribute__((weak));
extern int kern_pmem_unmap_uncached(void *mapped, size_t size) __attribute__((weak));

static int host_usable(const struct ghci_device *device, const struct kern_pmem *run);
static int host_view(struct ghci_device *device, struct ghci_allocation *allocation);

/*
 * Takes a run of host memory the GPU reaches, cleared, below limit
 * (0: the driver's own limit).
 *
 * Returns 0 with the allocation, or ENOMEM.
 */
int
ghci_memory_alloc(
	struct ghci_device *device,
	uint64_t bytes,
	uint64_t limit,
	struct ghci_allocation **result)
{
	struct ghci_allocation *allocation;
	struct kern_pmem held[HOST_TRIES];
	struct kern_pmem run;
	uint64_t end;
	unsigned held_count;
	unsigned index;
	int usable;
	int error;

	/* The record of the allocation, in whole pages. */
	allocation = kern_calloc(1U, sizeof(*allocation));
	if (allocation == NULL)
		return ENOMEM;
	allocation->kind = GHCI_MEMORY_HOST;
	allocation->bytes = (bytes + GHCI_PAGE_BYTES - 1U) & ~(uint64_t)(GHCI_PAGE_BYTES - 1U);

	/* The end of the addresses the run may take. */
	end = ghci_memory_limit(device);
	if (limit != 0U && limit < end)
		end = limit;

	/*
	 * Tries a bounded number of runs; a run that meets the local range is
	 * held so that the next try finds a different one, and every held
	 * run goes back at the end.
	 */
	held_count = 0U;
	error = ENOMEM;
	for (index = 0U; index < HOST_TRIES; index++) {
		/* A run the GPU can name. */
		error = kern_pmem_alloc_limited((size_t)allocation->bytes, GHCI_PAGE_BYTES, end - 1U, 0U, &run);
		if (error != 0) {
			error = ENOMEM;
			break;
		}

		/* A run outside the local range is taken. */
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
	if (error != 0) {
		kern_free(allocation);
		return ENOMEM;
	}

	/* The GPU's address of it is its bus address; the processor's view of it. */
	allocation->bus = allocation->run.paddr;
	error = host_view(device, allocation);
	if (error != 0) {
		(void)kern_pmem_free(&allocation->run);
		kern_free(allocation);
		return error;
	}

	/* Cleared, so that the GPU never reads what the run held before. */
	kern_memset(allocation->view, 0, (size_t)allocation->bytes);

	/* Succeeded: the run is the caller's. */
	*result = allocation;
	return 0;
}

/*
 * Gives a run back once the GPU no longer reaches it.
 *
 * Returns 0, or the error of a view that could not be removed; the run is
 * then kept, as memory that may still be reached is never handed out again.
 */
int
ghci_memory_free(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	int error;

	/* Nothing to give back. */
	if (allocation == NULL)
		return 0;

	/* The uncached view goes first. */
	if (device->host_uncached && kern_pmem_unmap_uncached != NULL) {
		error = kern_pmem_unmap_uncached(allocation->view, allocation->run.size);
		if (error != 0) {
			kern_logf("ghci: %s: host view not removed (%d); run kept\n", device->name, error);
			return error;
		}
	}

	/* The RAM goes back. */
	error = kern_pmem_free(&allocation->run);
	if (error != 0) {
		kern_logf("ghci: %s: host run not freed (%d)\n", device->name, error);
		return error;
	}

	/* Succeeded: the record goes too. */
	kern_free(allocation);
	return 0;
}

/*
 * Keeps a run the GPU of a lost device may still reach until bus
 * mastering is off.  The caller holds the device's lock or owns the
 * device alone.
 */
void
ghci_memory_quarantine(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	/* Nothing to keep. */
	if (allocation == NULL)
		return;

	/* The run joins the quarantine. */
	allocation->next = device->quarantined;
	device->quarantined = allocation;
}

/*
 * Gives back every run of the quarantine; only after bus mastering is
 * off, or the GPU reset and stopped, so that no DMA reaches them.
 */
void
ghci_memory_release_quarantine(
	struct ghci_device *device)
{
	struct ghci_allocation *allocation;
	int error;

	/* Each run in turn; one whose view cannot be removed stays. */
	while (device->quarantined != NULL) {
		allocation = device->quarantined;
		device->quarantined = allocation->next;
		error = ghci_memory_free(device, allocation);
		if (error != 0)
			break;
	}
}

/*
 * Adds one owner to an allocation (a resource, an export or a mapping).
 * The caller holds the device's lock.
 */
void
ghci_memory_hold(
	struct ghci_allocation *allocation)
{
	/* One more owner. */
	allocation->holds++;
}

/*
 * Lets one owner of an allocation go; the last one gives it back.  On a
 * lost device an allocation the GPU was ever given joins the quarantine
 * instead, as the GPU may still reach it.  The caller holds the device's
 * lock.
 */
void
ghci_memory_unhold(
	struct ghci_device *device,
	struct ghci_allocation *allocation)
{
	enum ghci_device_state state;
	unsigned long irq;
	int error;

	/* An allocation nobody holds is a caller's error; it is kept rather than given back twice. */
	if (allocation->holds == 0U) {
		kern_logf("ghci: %s: allocation let go more often than held\n", device->name);
		return;
	}

	/* Owners remain. */
	allocation->holds--;
	if (allocation->holds != 0U)
		return;

	/* The device's life, as the recovery last set it. */
	irq = spin_lock_irqsave(&device->irq_lock);

	state = device->state;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Memory a lost GPU may still reach waits for bus mastering to go. */
	if (state == GHCI_STATE_LOST && allocation->reached) {
		ghci_memory_quarantine(device, allocation);
		return;
	}

	/* Succeeded: the run goes back, or stays in the quarantine when its view cannot go. */
	error = ghci_memory_free(device, allocation);
	if (error != 0)
		ghci_memory_quarantine(device, allocation);
}

/*
 * Reports the end of the host addresses the driver gives the GPU: 4 GiB,
 * or HOST_LIMIT when the GPU reports a lower one.
 */
uint64_t
ghci_memory_limit(
	const struct ghci_device *device)
{
	/* A GPU that reports no limit is given the whole 32-bit range. */
	if (device->caps.host_limit == 0U)
		return ADDRESS_SPACE_END;

	/* Succeeded: the limit the GPU reported. */
	return device->caps.host_limit;
}

/* Reports whether a run of RAM lies outside the GPU's local range: 1 when it does. */
static int
host_usable(
	const struct ghci_device *device,
	const struct kern_pmem *run)
{
	uint64_t start;
	uint64_t end;
	uint64_t limit;
	uint64_t local_end;

	/* The run's bus addresses must lie below the limit. */
	start = run->paddr;
	end = start + run->size;
	limit = ghci_memory_limit(device);
	if (end > limit)
		return 0;

	/* A GPU without local memory has no range to avoid. */
	if (device->caps.local_bytes == 0U)
		return 1;

	/* The run must not meet the local range. */
	local_end = device->caps.local_base + device->caps.local_bytes;
	if (end <= device->caps.local_base)
		return 1;
	if (start >= local_end)
		return 1;

	/* Succeeded: the run meets the local range and is not usable. */
	return 0;
}

/*
 * Gives the processor its view of a host run: the direct map where the
 * GPU snoops the caches, else an uncached mapping, so that neither side
 * needs cache maintenance.
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
