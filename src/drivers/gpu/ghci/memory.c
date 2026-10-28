/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI device's memory: runs of host RAM the GPU reaches by DMA, runs
 * of a discrete GPU's local memory, and the quarantine of a lost device.
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
 * Local memory.  A discrete GPU (UMA clear, local memory in the MEM block
 * and a page-table walker) has memory of its own at [LOCAL_BASE,
 * LOCAL_BASE + LOCAL_SIZE) of its physical addresses.  The driver hands
 * out runs of it by their offsets from LOCAL_BASE with a first-fit
 * allocator; a run's GPU address is its local address, and the processor
 * sees it only through the local window (BAR2), where the window shows
 * it.  A boot framebuffer the firmware left scanned out in local memory
 * is kept out of the allocator, and the window kept open, until the
 * driver's own first flip has been taken.  Nothing of local memory is
 * cleared here: the caller clears a new run with the GPU before any
 * client sees it.
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

/* The runs of local memory the allocator's table holds at once. */
#define LOCAL_RUNS			1024U

/* The largest run of local memory one allocation takes (the allocator counts in 32 bits). */
#define LOCAL_RUN_MAX			0x80000000ULL

/*
 * The registers of the display controller (DISP or DISP2) the start reads
 * to find a boot framebuffer, at offsets from the block: CTRL with its
 * ENABLE bit, the framebuffer being scanned out, its row pitch, and its
 * size ([15:0] width, [31:16] height).
 *
 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (sections 13.1
 * and 13.2, the register tables; section 13.3, the handover).
 */
#define DISPLAY_CTRL			0x10U
#define DISPLAY_CTRL_ENABLE		(1U << 0)
#define DISPLAY_FB_BASE			0x20U
#define DISPLAY_FB_STRIDE		0x28U
#define DISPLAY_FB_SIZE			0x2CU
#define DISPLAY_HEIGHT_SHIFT		16U

/*
 * WINDOW_CTRL's ENABLE bit in the MEM block: the local window is open.
 *
 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 11.3,
 * the MEM capability's registers).
 */
#define WINDOW_CTRL_ENABLE		(1U << 0)

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
static int boot_framebuffer(struct ghci_device *device, uint32_t *offset, uint32_t *bytes);
static int local_reserve(struct ghci_device *device, uint32_t offset, uint32_t bytes);
static void window_update(struct ghci_device *device);

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

	/*
	 * A run of local memory goes back to the allocator; the caller holds
	 * the device's lock or owns the device alone.
	 */
	if (allocation->kind == GHCI_MEMORY_LOCAL) {
		error = 0;
		if (device->local_extents != NULL)
			error = ghci_range_free(&device->local_map, (uint32_t)(allocation->bus - device->caps.local_base));
		if (error != 0)
			kern_logf("ghci: %s: local run at %llx not in the allocator (%d)\n",
			    device->name,
			    (unsigned long long)allocation->bus,
			    error);
		kern_free(allocation);
		return 0;
	}

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
 * Takes a run of the GPU's local memory, not cleared.
 *
 * The caller clears the run with the GPU before a client can see it, as
 * the run may hold what an earlier client left.  Returns 0 with the
 * allocation, ENOTSUP on a device whose local memory the driver does not
 * hand out, or ENOMEM.
 */
int
ghci_memory_alloc_local(
	struct ghci_device *device,
	uint64_t bytes,
	struct ghci_allocation **result)
{
	struct ghci_allocation *allocation;
	uint64_t shown;
	uint32_t offset;
	int error;

	/* A device without local memory in use. */
	if (!device->local_ready)
		return ENOTSUP;

	/* A run the allocator can count, in whole pages. */
	bytes = (bytes + GHCI_PAGE_BYTES - 1U) & ~(uint64_t)(GHCI_PAGE_BYTES - 1U);
	if (bytes == 0U || bytes > LOCAL_RUN_MAX)
		return ENOMEM;

	/* The record of the allocation. */
	allocation = kern_calloc(1U, sizeof(*allocation));
	if (allocation == NULL)
		return ENOMEM;

	/* The run, first fit on a page. */
	mutex_lock(&device->lock);

	error = ghci_range_alloc(&device->local_map, (uint32_t)bytes, GHCI_PAGE_BYTES, &offset);

	mutex_unlock(&device->lock);

	/* No room, or no room in the allocator's table. */
	if (error != 0) {
		kern_free(allocation);
		return ENOMEM;
	}

	/* The GPU's address of it is its local address. */
	allocation->kind = GHCI_MEMORY_LOCAL;
	allocation->bus = device->caps.local_base + offset;
	allocation->bytes = bytes;

	/* The processor sees it through the local window, when the window shows all of it. */
	shown = device->caps.window_bytes;
	if (device->window_bytes < shown)
		shown = device->window_bytes;
	if (device->window != NULL && (uint64_t)offset + bytes <= shown)
		allocation->view = (uint8_t *)(uintptr_t)(device->window + offset);

	/* Succeeded: the run is the caller's. */
	*result = allocation;
	return 0;
}

/*
 * Starts the use of the GPU's local memory and handles the boot
 * framebuffer, before the ring runs.
 *
 * On a discrete device the allocator of local memory starts, with the
 * boot framebuffer the firmware left scanned out kept out of it.  A
 * device that is not discrete, or whose allocator cannot be made, keeps
 * everything in host memory.  The local window stays open while the boot
 * console may write through it or the driver reads and writes local
 * memory through it, and is closed otherwise.  Returns 0; a failure only
 * leaves the device unified.
 */
int
ghci_memory_local_start(
	struct ghci_device *device)
{
	uint32_t offset;
	uint32_t bytes;
	int discrete;
	int walker;
	int active;
	int error;

	/* Nothing in use yet. */
	device->local_ready = 0;
	device->boot_reserved = 0;
	device->boot_offset = 0U;
	device->boot_bytes = 0U;

	/* A boot framebuffer in local memory, which the boot console may still write. */
	active = boot_framebuffer(device, &offset, &bytes);
	if (active) {
		device->boot_reserved = 1;
		device->boot_offset = offset;
		device->boot_bytes = bytes;
	}

	/* Discrete needs UMA clear, local memory and a walker (the flat GPU names memory by bus addresses). */
	discrete = ghci_caps_has(&device->caps, GHCI_HAS_DISCRETE);
	walker = ghci_caps_has(&device->caps, GHCI_HAS_HW_WALKER);
	if (discrete && !walker)
		kern_logf("ghci: %s: local memory not used without a page-table walker\n", device->name);

	/* The allocator's table, on a discrete device. */
	if (discrete && walker) {
		device->local_extents = kern_calloc(LOCAL_RUNS, sizeof(*device->local_extents));
		if (device->local_extents == NULL)
			kern_logf("ghci: %s: no room for the local allocator; local memory not used\n", device->name);
	}

	/* The allocator over the offsets of local memory, without the boot framebuffer. */
	if (device->local_extents != NULL) {
		ghci_range_init(&device->local_map, 0U, device->caps.local_bytes, device->local_extents, LOCAL_RUNS);
		error = 0;
		if (device->boot_reserved)
			error = local_reserve(device, device->boot_offset, device->boot_bytes);
		if (error == 0) {
			device->local_ready = 1;
			kern_logf("ghci: %s: discrete, %llu MiB of local memory at %llx\n",
			    device->name,
			    (unsigned long long)(device->caps.local_bytes >> 20),
			    (unsigned long long)device->caps.local_base);
		} else {
			kern_logf("ghci: %s: boot framebuffer not kept out (%d); local memory not used\n", device->name, error);
			kern_free(device->local_extents);
			device->local_extents = NULL;
		}
	}

	/* The window: open while the boot console or the driver uses it, closed otherwise. */
	window_update(device);

	/* Succeeded: local memory is in use, or everything stays in host memory. */
	return 0;
}

/*
 * Ends the use of local memory at the detach, after the quarantine went:
 * the allocator goes when nothing but the boot framebuffer is in it.
 * The window keeps what it was (a boot console may still use it).
 */
void
ghci_memory_local_stop(
	struct ghci_device *device)
{
	unsigned kept;

	/* No allocator. */
	device->local_ready = 0;
	if (device->local_extents == NULL)
		return;

	/* A run still out (one the quarantine kept) keeps the table, so that its free finds it. */
	kept = 0U;
	if (device->boot_reserved)
		kept = 1U;
	if (device->local_map.count > kept) {
		kern_logf("ghci: %s: %u local runs still out at the detach\n",
		    device->name,
		    device->local_map.count - kept);
		return;
	}

	/* Succeeded: the table goes. */
	kern_free(device->local_extents);
	device->local_extents = NULL;
}

/*
 * Reports the bytes of local memory the driver hands out: LOCAL_SIZE on a
 * discrete device whose local memory is in use, else 0.
 */
uint64_t
ghci_memory_local_bytes(
	const struct ghci_device *device)
{
	/* A device that keeps everything in host memory. */
	if (!device->local_ready)
		return 0U;

	/* Succeeded: the local memory. */
	return device->caps.local_bytes;
}

/*
 * Gives the boot framebuffer's range back to the allocator once the
 * driver's own first flip has been taken, and closes the local window
 * when the driver does not use it.  Takes the device's lock.
 */
void
ghci_memory_boot_release(
	struct ghci_device *device)
{
	int error;

	/* The range, once. */
	mutex_lock(&device->lock);

	if (device->boot_reserved) {
		error = 0;
		if (device->local_ready)
			error = ghci_range_free(&device->local_map, device->boot_offset);
		if (error != 0)
			kern_logf("ghci: %s: boot framebuffer not in the allocator (%d)\n", device->name, error);
		device->boot_reserved = 0;
		window_update(device);
	}

	mutex_unlock(&device->lock);
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

/*
 * Finds a boot framebuffer the firmware left scanned out in local memory:
 * a display controller (DISP2, or DISP) found running.  Its range, in
 * whole pages from LOCAL_BASE and cut to local memory, goes to offset and
 * bytes.  A boot framebuffer in host memory needs nothing of the driver:
 * the boot memory map already keeps it from the kernel's allocator.
 * Returns 1 when there is one in local memory, else 0.
 */
static int
boot_framebuffer(
	struct ghci_device *device,
	uint32_t *offset,
	uint32_t *bytes)
{
	uint32_t block;
	uint32_t ctrl;
	uint64_t base;
	uint64_t stride;
	uint64_t height;
	uint64_t start;
	uint64_t end;
	uint64_t local_end;

	/* The display controller: DISP2, else DISP. */
	block = device->caps.block_offset[GHCI_EXTCAP_ID_DISP2];
	if (block == 0U)
		block = device->caps.block_offset[GHCI_EXTCAP_ID_DISP];
	if (block == 0U)
		return 0;

	/* A controller found disabled scans nothing out. */
	ctrl = ghci_read(device, block + DISPLAY_CTRL);
	if ((ctrl & DISPLAY_CTRL_ENABLE) == 0U)
		return 0;

	/* The framebuffer being scanned out: rows of the pitch, as many as the height. */
	base = ghci_read(device, block + DISPLAY_FB_BASE);
	stride = ghci_read(device, block + DISPLAY_FB_STRIDE);
	height = ghci_read(device, block + DISPLAY_FB_SIZE) >> DISPLAY_HEIGHT_SHIFT;
	start = base;
	end = base + stride * height;

	/* Only a framebuffer in local memory is the driver's to keep. */
	local_end = device->caps.local_base + device->caps.local_bytes;
	if (device->caps.local_bytes == 0U || end <= start)
		return 0;
	if (end <= device->caps.local_base || start >= local_end) {
		kern_logf("ghci: %s: boot framebuffer at %llx in host memory\n", device->name, (unsigned long long)base);
		return 0;
	}

	/* Its pages, cut to local memory. */
	if (start < device->caps.local_base)
		start = device->caps.local_base;
	if (end > local_end)
		end = local_end;
	start &= ~(uint64_t)(GHCI_PAGE_BYTES - 1U);
	end = (end + GHCI_PAGE_BYTES - 1U) & ~(uint64_t)(GHCI_PAGE_BYTES - 1U);
	if (end > local_end)
		end = local_end;

	/* Succeeded: the boot framebuffer is in local memory. */
	*offset = (uint32_t)(start - device->caps.local_base);
	*bytes = (uint32_t)(end - start);
	kern_logf("ghci: %s: boot framebuffer at %llx (%u bytes) kept\n",
	    device->name,
	    (unsigned long long)base,
	    (unsigned)*bytes);
	return 1;
}

/*
 * Takes the run [offset, offset + bytes) out of the empty local
 * allocator: first fit on an empty table takes the gap before it, then
 * the run itself, and the gap goes back.  Returns 0, or the allocator's
 * error.
 */
static int
local_reserve(
	struct ghci_device *device,
	uint32_t offset,
	uint32_t bytes)
{
	uint32_t gap;
	uint32_t taken;
	int error;

	/* The gap before the run, which an empty table gives from offset 0. */
	gap = 0U;
	if (offset != 0U) {
		error = ghci_range_alloc(&device->local_map, offset, GHCI_PAGE_BYTES, &gap);
		if (error != 0)
			return error;
	}

	/* The run, which first fit now puts right after the gap. */
	error = ghci_range_alloc(&device->local_map, bytes, GHCI_PAGE_BYTES, &taken);
	if (error == 0 && taken != offset) {
		(void)ghci_range_free(&device->local_map, taken);
		error = EINVAL;
	}

	/* The gap goes back either way. */
	if (offset != 0U)
		(void)ghci_range_free(&device->local_map, gap);
	if (error != 0)
		return error;

	/* Succeeded: the run is out of the allocator. */
	return 0;
}

/*
 * Opens or closes the local window: open while a boot framebuffer is kept
 * (the boot console may write through it) or the driver reads and writes
 * local memory through it, closed otherwise.  A device without a window
 * has nothing to write.  The caller holds the device's lock or owns the
 * device alone.
 */
static void
window_update(
	struct ghci_device *device)
{
	uint32_t block;
	int open;

	/* The MEM block, with a window. */
	block = device->caps.block_offset[GHCI_EXTCAP_ID_MEM];
	if (block == 0U || device->caps.window_bytes == 0U)
		return;

	/* Whether anything uses the window. */
	open = 0;
	if (device->boot_reserved)
		open = 1;
	if (device->local_ready && device->window != NULL)
		open = 1;

	/* Succeeded: WINDOW_CTRL.ENABLE says so. */
	ghci_write(device, block + GHCI_MEM_WINDOW_CTRL, open ? WINDOW_CTRL_ENABLE : 0U);
	device->window_open = open;
}
