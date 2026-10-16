/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU's address spaces: the page tables of each session, the VRAM its
 * work uses, and the runs of that work on the rings.
 *
 * Every session (and every check of the driver's own) has an address
 * space of four levels of page tables in VRAM the CPU writes through its
 * window, and an identifier the interrupt controller attaches to the page
 * faults of the space's work.  Buffers of VRAM (backings) are mapped into
 * a space at addresses from 4 GiB on.  A run hands the space one of the
 * graphics hub's contexts 1 to 15 for as long as it keeps it
 * (context-pool.c), drops the context's cached translations on the ring
 * when needed, has the ring run the space's indirect buffer in that
 * context and waits for the fence after it.  A page fault of a space's
 * work, or a run that does not end in time, loses the space: it runs
 * nothing more, and its session is told.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-private.h"
#include "drivers/gpu/amdrdna/amdrdna-regs.h"

/* A page, a table's entries and the bytes of one entry. */
#define SPACE_PAGE_BYTES		4096U
#define SPACE_TABLE_ENTRIES		512U
#define SPACE_ENTRY_BYTES		8U

/* The level of the leaf tables (the root is level 0). */
#define SPACE_LEAF_LEVEL		(AMDRDNA_VM_LEVELS - 1U)

/*
 * The addresses a space hands out: from 4 GiB (nothing is mapped below,
 * so a small bad address faults) to 64 TiB, below the hole that starts at
 * 2^47; and the boundary of every mapping and backing (64 KiB, the hubs'
 * larger page).
 */
#define SPACE_FIRST_ADDRESS		(4ULL << 30)
#define SPACE_LAST_ADDRESS		(64ULL << 40)
#define SPACE_MAPPING_ALIGN		(64ULL << 10)

/*
 * The invalidation engine each command ring drops a context's
 * translations on: the graphics ring engine 0, the compute queues of the
 * first compute engine's pipes 0 to 3 engines 4 to 7 (the scheduler's
 * queue has 1, the first copy engine 8).  Engines 2 and 3 are the
 * firmware's and 17 the CPU's.
 *
 * Values referenced: Linux v6.19 drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.c
 * (amdgpu_gmc_allocate_vm_inv_eng: the engines handed to the rings in
 * order from the mask 0x1fff3).
 */
#define SPACE_ENGINE_GRAPHICS		0U
#define SPACE_ENGINE_COMPUTE_FIRST	4U

/* The most words a run puts on its ring: a sync, a drop, the indirect buffer and a timed run's two timestamps. */
#define SPACE_RUN_COMMANDS		64U

/* How often a run waits for a context before giving up. */
#define SPACE_GRANT_TRIES		32U

static struct amdrdna_ring *space_ring(struct amdrdna_device *device, unsigned tracked);
static void space_passed(struct amdrdna_device *device, uint32_t *passed);
static unsigned space_commands(struct amdrdna_space *space, struct amdrdna_ring *ring, unsigned tracked, const struct amdrdna_context_grant *grant, uint64_t address, uint32_t words, uint32_t *commands);
static int space_table_create(struct amdrdna_device *device, unsigned level, struct amdrdna_table **result);
static void space_table_free(struct amdrdna_device *device, struct amdrdna_table *table, unsigned level);
static void space_table_write(struct amdrdna_device *device, struct amdrdna_table *table, unsigned index, uint64_t entry);
static int space_leaf(struct amdrdna_space *space, uint64_t address, struct amdrdna_table **leaf);
static void space_clear(struct amdrdna_space *space, uint64_t address, uint64_t bytes);
static void space_changed(struct amdrdna_space *space);
static void space_lose(struct amdrdna_space *space, int error);
static void space_reset(struct amdrdna_device *device, unsigned context);
static uint64_t space_table_word(struct amdrdna_device *device, uint64_t vram);
static bool space_quiet_locked(struct amdrdna_space *space);
static uint32_t space_hub_read(struct amdrdna_device *device, enum amdrdna_hub_register which, unsigned instance);

/*
 * Prepares the address spaces' lock, the context pool and the
 * identifiers at the attach, before any interrupt can name a space.
 */
void
amdrdna_spaces_init(
	struct amdrdna_device *device)
{
	struct amdrdna_spaces *spaces;

	/* The lock, the pool and the identifiers. */
	spaces = &device->spaces;
	spin_init(&spaces->lock, LOCK_RANK_DEVICE, "amdrdna spaces");
	amdrdna_context_pool_init(&spaces->pool);
	amdrdna_space_ids_init(&spaces->ids);

	/* No space, nothing running yet. */
	spaces->list = NULL;
	spaces->count = 0U;
	spaces->held = 0U;
	spaces->ready = false;
	spaces->faulted = false;
	spaces->runs = 0U;
	spaces->lost = 0U;
}

/* Lets the address spaces run work once the rings run (stage A5s). */
void
amdrdna_spaces_start(
	struct amdrdna_device *device)
{
	unsigned long irq;

	/* ready lets runs be submitted from now on. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	device->spaces.ready = true;

	spin_unlock_irqrestore(&device->spaces.lock, irq);
}

/*
 * Stops every new run after a fault of the device or before its stop:
 * a run asked for from now on is refused with EIO.
 */
void
amdrdna_spaces_fault(
	struct amdrdna_device *device)
{
	unsigned long irq;

	/* faulted refuses every later run. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	device->spaces.faulted = true;

	spin_unlock_irqrestore(&device->spaces.lock, irq);
}

/*
 * Creates an address space for a session (session names it for the
 * report of a loss; NULL for a check of the driver's own): an identifier,
 * an empty root table and the free addresses.
 *
 * Returns 0, ENOSPC when no identifier is free, ENOMEM.
 */
int
amdrdna_space_create(
	struct amdrdna_device *device,
	void *session,
	struct amdrdna_space **result)
{
	struct amdrdna_space *space;
	unsigned long irq;
	uint32_t id;
	int error;

	/* The record. */
	*result = NULL;
	space = kern_calloc(1U, sizeof(*space));
	if (space == NULL)
		return ENOMEM;

	/* The free addresses. */
	space->addresses = kern_malloc(sizeof(*space->addresses));
	if (space->addresses == NULL) {
		kern_free(space);
		return ENOMEM;
	}

	/* Every address from 4 GiB to 64 TiB is free. */
	amdrdna_pool_init(space->addresses, SPACE_FIRST_ADDRESS, SPACE_LAST_ADDRESS);
	(void)amdrdna_pool_give(space->addresses, SPACE_FIRST_ADDRESS, SPACE_LAST_ADDRESS - SPACE_FIRST_ADDRESS);

	/* The root table, empty. */
	error = space_table_create(device, 0U, &space->root);
	if (error != 0) {
		kern_free(space->addresses);
		kern_free(space);
		return error;
	}

	/* Describes the space: no context yet, its tables at their first generation. */
	(void)mutex_init(&space->lock, LOCK_RANK_DEVICE, "amdrdna space");
	space->device = device;
	space->root_word = space_table_word(device, space->root->vram);
	space->generation = 1U;
	space->context = 0U;
	space->session = session;

	/* Takes an identifier and lists the space, so its page faults find it. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	error = amdrdna_space_id_take(&device->spaces.ids, &id);
	if (error == 0) {
		space->id = id;
		space->next = device->spaces.list;
		device->spaces.list = space;
		device->spaces.count++;
	}

	spin_unlock_irqrestore(&device->spaces.lock, irq);

	/* No identifier is free. */
	if (error != 0) {
		space_table_free(device, space->root, 0U);
		kern_free(space->addresses);
		kern_free(space);
		return error;
	}

	/* Succeeded: the space is the caller's. */
	*result = space;
	return 0;
}

/*
 * Destroys an address space once its session is done with it: its
 * context goes back to the pool, its identifier is free again, and its
 * tables and record are freed.
 *
 * A space whose context may still run work (it was lost and its work
 * never ended) keeps its context out of the pool and its tables in VRAM
 * for good, since the GPU may still read them; true is then returned and
 * the record stays.
 */
bool
amdrdna_space_destroy(
	struct amdrdna_space *space)
{
	struct amdrdna_device *device;
	struct amdrdna_space **link;
	unsigned long irq;
	bool quiet;

	/* Takes the space off the list and gives its context back, or holds it. */
	device = space->device;
	irq = spin_lock_irqsave(&device->spaces.lock);

	for (link = &device->spaces.list; *link != NULL; link = &(*link)->next) {
		if (*link == space) {
			*link = space->next;
			device->spaces.count--;
			break;
		}
	}

	/* A space whose work ended gives its context and identifier back; another holds its context for good. */
	quiet = space_quiet_locked(space);
	if (quiet) {
		amdrdna_context_release(&device->spaces.pool, space->context, space->id);
		amdrdna_space_id_give(&device->spaces.ids, space->id);
	} else {
		amdrdna_context_hold(&device->spaces.pool, space->context);
		device->spaces.held++;
	}

	spin_unlock_irqrestore(&device->spaces.lock, irq);

	/* A space that may still run keeps everything the GPU may read. */
	if (!quiet) {
		amdrdna_mark("space %u kept: its work in context %u did not end", (unsigned)space->id, space->context);
		return true;
	}

	/* The tables, the free addresses and the record. */
	space_table_free(device, space->root, 0U);
	kern_free(space->addresses);
	kern_free(space);

	/* Succeeded: the space is gone. */
	return false;
}

/*
 * Maps a backing into an address space at a free address on a 64 KiB
 * boundary, readable, writable and executable, and reports the address.
 *
 * Returns 0, ENOSPC when the space has no free addresses of the size, or
 * ENOMEM when a table could not be had (nothing stays mapped).
 */
int
amdrdna_space_map(
	struct amdrdna_space *space,
	const struct amdrdna_backing *backing,
	uint64_t *address)
{
	struct amdrdna_device *device;
	struct amdrdna_table *leaf;
	uint64_t access;
	uint64_t start;
	uint64_t page;
	uint64_t entry;
	unsigned index[AMDRDNA_VM_LEVELS];
	int error;

	/* The bits of every page: present, readable, writable, executable, non-coherent VRAM. */
	device = space->device;
	access = AMDRDNA_PAGE_PRESENT | AMDRDNA_PAGE_READ | AMDRDNA_PAGE_WRITE | AMDRDNA_PAGE_EXECUTE;
	access |= AMDRDNA_PAGE_TYPE_NONCOHERENT << AMDRDNA_PAGE_TYPE_SHIFT;

	/* Takes the addresses and writes one entry per page, under the space's lock. */
	mutex_lock(&space->lock);

	error = amdrdna_pool_take(space->addresses, backing->bytes, SPACE_MAPPING_ALIGN, &start);
	if (error != 0) {
		mutex_unlock(&space->lock);
		return ENOSPC;
	}

	/* One entry per page of the backing. */
	for (page = 0U; page < backing->bytes; page += SPACE_PAGE_BYTES) {
		/* The leaf table of the page, made when missing. */
		error = space_leaf(space, start + page, &leaf);
		if (error != 0)
			break;

		/* The page's entry: its VRAM's physical address and the bits. */
		amdrdna_vm_split(start + page, index);
		entry = amdrdna_vm_leaf_entry(backing->vram + page + device->frame_buffer_physical, access);
		space_table_write(device, leaf, index[SPACE_LEAF_LEVEL], entry);
		leaf->count++;
	}

	/* A table that could not be had undoes the pages mapped so far. */
	if (error != 0) {
		space_clear(space, start, page);
		(void)amdrdna_pool_give(space->addresses, start, backing->bytes);
		mutex_unlock(&space->lock);
		return ENOMEM;
	}

	mutex_unlock(&space->lock);

	/* The GPU reads the tables as written; the next run drops the context's translations. */
	amdrdna_host_path_flush(device);
	space_changed(space);

	/* Succeeded: the backing is at the address. */
	*address = start;
	return 0;
}

/*
 * Unmaps a run of addresses of an address space and frees them.  The
 * caller has made sure no work of the space reads them any more; the
 * next run drops the context's stale translations.
 */
void
amdrdna_space_unmap(
	struct amdrdna_space *space,
	uint64_t address,
	uint64_t bytes)
{
	uint64_t whole;
	int error;

	/* The run in whole 64 KiB steps, as it was mapped. */
	whole = (bytes + SPACE_MAPPING_ALIGN - 1U) & ~(SPACE_MAPPING_ALIGN - 1U);

	/* Clears the entries and frees the addresses, under the space's lock. */
	mutex_lock(&space->lock);

	space_clear(space, address, whole);
	error = amdrdna_pool_give(space->addresses, address, whole);

	mutex_unlock(&space->lock);

	/* Addresses the list cannot take back stay unused for good. */
	if (error != 0)
		amdrdna_mark("space %u: addresses at 0x%llx stay unused (%d)", (unsigned)space->id, (unsigned long long)address, error);

	/* The GPU reads the cleared tables; the next run drops the stale translations. */
	amdrdna_host_path_flush(space->device);
	space_changed(space);
}

/*
 * Runs words dwords of commands at address of an address space on a
 * tracked ring (AMDRDNA_TRACKED_*) and waits up to milliseconds for them
 * to end.
 *
 * The CPU's writes to VRAM are made visible first.  The space gets a hub
 * context; when the context's translations are stale they are dropped on
 * the ring before the commands (the context pointed at the space's root
 * table, the drop waited for, the interrupt controller told which space
 * the context's faults belong to).  The commands run as an indirect
 * buffer in the context, and the ring's fence after them is waited for.
 * A run that does not end in time loses the space.
 *
 * Returns 0, EINVAL for a bad buffer, ENOTSUP for the copy engine, ENODEV
 * for a ring that does not run,
 * EIO for a space that is lost or a device that failed (or a run that did
 * not end), EBUSY when the ring is full, ETIMEDOUT when no context became
 * free.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_vm.c (amdgpu_vm_flush: the pipeline
 * sync, the drop, then the space's identifier for the context) and
 * amdgpu_ib.c (amdgpu_ib_schedule).
 */
int
amdrdna_space_run(
	struct amdrdna_space *space,
	unsigned tracked,
	uint64_t address,
	uint32_t words,
	unsigned milliseconds)
{
	uint32_t commands[SPACE_RUN_COMMANDS];
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	struct amdrdna_context_request request;
	struct amdrdna_context_grant grant;
	struct amdrdna_device *device;
	struct amdrdna_ring *ring;
	struct amdrdna_ring *waited;
	unsigned long irq;
	uint32_t sequence;
	unsigned count;
	unsigned tries;
	bool submitted;
	int error;

	/* A command ring that runs (the copy engine reads other packets), and a buffer of whole dwords. */
	device = space->device;
	if (tracked >= AMDRDNA_TRACKED_COPY)
		return ENOTSUP;
	ring = space_ring(device, tracked);
	if (ring == NULL)
		return ENODEV;
	if (words == 0U || (address & 3U) != 0U)
		return EINVAL;

	/* The GPU must read what the CPU wrote to VRAM. */
	amdrdna_host_path_flush(device);

	/* Gets a context and submits the run, waiting for a context when none is free. */
	submitted = false;
	sequence = 0U;
	error = 0;
	for (tries = 0U; tries < SPACE_GRANT_TRIES; tries++) {
		irq = spin_lock_irqsave(&device->spaces.lock);

		/* A failed device or a lost space runs nothing. */
		if (!device->spaces.ready ||
		    device->spaces.faulted ||
		    space->lost) {
			spin_unlock_irqrestore(&device->spaces.lock, irq);
			return EIO;
		}

		/* What the run asks of the pool, and the rings' fences. */
		request.owner = space->id;
		request.held = space->context;
		request.table = space->root_word;
		request.generation = space->generation;
		request.ring = tracked;
		space_passed(device, passed);
		error = amdrdna_context_grant(&device->spaces.pool, &request, passed, &grant);
		if (error != 0) {
			spin_unlock_irqrestore(&device->spaces.lock, irq);
			return error;
		}

		/* No context is free: waits for one without the lock, then asks again. */
		if (grant.wait) {
			spin_unlock_irqrestore(&device->spaces.lock, irq);
			waited = space_ring(device, grant.wait_ring);
			if (waited != NULL)
				(void)amdrdna_ring_wait(device, waited, grant.wait_sequence, milliseconds);
			continue;
		}

		/* Builds the commands and puts them on the ring. */
		count = space_commands(space, ring, tracked, &grant, address, words, commands);
		error = amdrdna_ring_submit(device, ring, commands, count, true, &sequence);
		if (error == 0) {
			amdrdna_context_submitted(&device->spaces.pool, grant.context, &request, sequence, grant.flush);
			space->context = grant.context;
			device->spaces.runs++;
			submitted = true;
		}

		spin_unlock_irqrestore(&device->spaces.lock, irq);
		break;
	}

	/* Reports a ring that was full. */
	if (error != 0)
		return error;

	/* Reports a run that never got a context. */
	if (!submitted)
		return ETIMEDOUT;

	/*
	 * Waits for the fence after the run.  A run that does not end loses
	 * the space; on the graphics queue, which every session shares, the
	 * queue is reset so the other sessions go on, and a queue that cannot
	 * be reset fails the device.
	 */
	error = amdrdna_ring_wait(device, ring, sequence, milliseconds);
	if (error != 0) {
		space_lose(space, ETIMEDOUT);
		if (tracked == AMDRDNA_TRACKED_GRAPHICS)
			space_reset(device, grant.context);
		return EIO;
	}

	/* The CPU reads what the GPU wrote to VRAM, not what the host path held. */
	amdrdna_host_path_drop(device);

	/* Succeeded: the run ended. */
	return 0;
}

/*
 * Reports whether no work of an address space can still run: it holds
 * no context, or every ring has passed the work of its context.
 */
bool
amdrdna_space_quiet(
	struct amdrdna_space *space)
{
	unsigned long irq;
	bool quiet;

	/* Read under the spaces' lock. */
	irq = spin_lock_irqsave(&space->device->spaces.lock);

	quiet = space_quiet_locked(space);

	spin_unlock_irqrestore(&space->device->spaces.lock, irq);

	/* Succeeded: the answer. */
	return quiet;
}

/* Reports whether an address space was lost. */
bool
amdrdna_space_lost(
	struct amdrdna_space *space)
{
	unsigned long irq;
	bool lost;

	/* Read under the spaces' lock. */
	irq = spin_lock_irqsave(&space->device->spaces.lock);

	lost = space->lost;

	spin_unlock_irqrestore(&space->device->spaces.lock, irq);

	/* Succeeded: the answer. */
	return lost;
}

/*
 * Makes every later run on the graphics ring write the GPU's timestamp
 * counter at its start and at its end (on), or not, for the executor's
 * timing.
 */
void
amdrdna_spaces_stamp(
	struct amdrdna_device *device,
	bool on)
{
	unsigned long irq;

	/* Set under the spaces' lock, which every run's commands are built under. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	device->spaces.stamped = on;

	spin_unlock_irqrestore(&device->spaces.lock, irq);
}

/*
 * Reports how many ticks of the GPU's timestamp counter the last run on
 * the graphics ring took, from the command processor's start to its end,
 * and the counter at its start; 0 and 0 when runs are not stamped.  The
 * caller ran that run and nothing has run on the ring since (the node's
 * lock keeps the executor's runs in turn).
 */
uint64_t
amdrdna_space_run_ticks(
	struct amdrdna_space *space,
	uint64_t *started)
{
	struct amdrdna_device *device;
	struct amdrdna_ring *ring;
	uint64_t start;
	uint64_t end;

	/* Nothing was stamped without the timing. */
	device = space->device;
	*started = 0U;
	if (!device->spaces.stamped)
		return 0U;

	/* Nor without the graphics ring. */
	ring = space_ring(device, AMDRDNA_TRACKED_GRAPHICS);
	if (ring == NULL)
		return 0U;

	/* The counter at the run's start and at its end. */
	amdrdna_ring_stamps(device, ring, &start, &end);
	*started = start;

	/* A pair that does not go forward was not this run's. */
	if (end < start)
		return 0U;

	/* Succeeded: the ticks between. */
	return end - start;
}

/*
 * Takes a page fault a hub reported to the address space it belongs to:
 * the space the identifier names, or, without one, the space the faulting
 * context is handed to.  The space is lost and its session told; a fault
 * of the kernel's context or of no known space is left to the log.
 */
void
amdrdna_space_fault(
	struct amdrdna_device *device,
	const struct amdrdna_vm_fault *fault,
	unsigned context)
{
	struct amdrdna_space *space;
	struct amdrdna_space *found;
	unsigned long irq;
	void *session;
	bool first;

	/* Finds the space and marks it lost, once. */
	found = NULL;
	first = false;
	session = NULL;
	irq = spin_lock_irqsave(&device->spaces.lock);

	for (space = device->spaces.list; space != NULL; space = space->next) {
		/* The identifier names the space. */
		if (fault->space_id != 0U && space->id == fault->space_id) {
			found = space;
			break;
		}

		/* Without one, the context's owner is the space. */
		if (fault->space_id == 0U &&
		    context != 0U &&
		    space->context == context &&
		    device->spaces.pool.slots[context].owner == space->id) {
			found = space;
			break;
		}
	}

	/* The space counts the fault and is lost, the first time with its session noted. */
	if (found != NULL) {
		found->faults++;
		if (!found->lost) {
			found->lost = true;
			found->lost_error = EFAULT;
			device->spaces.lost++;
			session = found->session;
			first = true;
		}
	}

	spin_unlock_irqrestore(&device->spaces.lock, irq);

	/* Says which space lost its work, the first time. */
	if (!first)
		return;
	amdrdna_mark("space %u lost: page fault at 0x%llx in context %u",
	    (unsigned)found->id,
	    (unsigned long long)fault->fault_address,
	    context);

	/* Tells the session; its later work is refused. */
	if (session != NULL)
		amdrdna_node_session_lost(device, session, EIO);
}

/*
 * Marks what the hub and the tables say about an address of a space, for
 * a check that failed: the context the space holds, that context's table
 * words and control word as the hub reads them, the done word of the
 * graphics ring's invalidation engine, and the entry of each level the
 * address goes through, read back through the CPU's window.
 */
void
amdrdna_space_describe(
	struct amdrdna_space *space,
	uint64_t address)
{
	struct amdrdna_device *device;
	struct amdrdna_table *table;
	unsigned index[AMDRDNA_VM_LEVELS];
	uint64_t entries[AMDRDNA_VM_LEVELS];
	uint32_t base_low;
	uint32_t base_high;
	uint32_t control;
	volatile uint8_t *cpu;
	uint32_t done;
	unsigned context;
	unsigned level;

	/* The context and the hub's words for it. */
	device = space->device;
	context = space->context;
	base_low = space_hub_read(device, AMDRDNA_HUB_TABLE_BASE_LOW, context);
	base_high = space_hub_read(device, AMDRDNA_HUB_TABLE_BASE_HIGH, context);
	control = space_hub_read(device, AMDRDNA_HUB_CONTEXT_CONTROL, context);
	done = space_hub_read(device, AMDRDNA_HUB_INVALIDATE_DONE, SPACE_ENGINE_GRAPHICS);

	/* The entry of each level, as far as the tables go. */
	amdrdna_vm_split(address, index);
	table = space->root;
	for (level = 0U; level < AMDRDNA_VM_LEVELS; level++) {
		/* A level the tables do not reach reads as no entry. */
		entries[level] = 0U;
		if (table == NULL)
			continue;

		/* The entry, then the table below it. */
		cpu = amdrdna_vram_cpu(device, table->vram);
		entries[level] = kern_mmio_read64(cpu + (size_t)index[level] * SPACE_ENTRY_BYTES);
		if (table->below == NULL) {
			table = NULL;
		} else {
			table = table->below[index[level]];
		}
	}

	/* One line for the hub, one for the tables. */
	amdrdna_mark("space %u ctx=%u root=0x%llx hub base=0x%08x%08x control=0x%08x done=0x%08x",
	    (unsigned)space->id,
	    context,
	    (unsigned long long)space->root_word,
	    (unsigned)base_high,
	    (unsigned)base_low,
	    (unsigned)control,
	    (unsigned)done);
	amdrdna_mark("space %u at 0x%llx: 0x%llx 0x%llx 0x%llx 0x%llx",
	    (unsigned)space->id,
	    (unsigned long long)address,
	    (unsigned long long)entries[0],
	    (unsigned long long)entries[1],
	    (unsigned long long)entries[2],
	    (unsigned long long)entries[3]);
}

/*
 * Creates a backing: zeroed VRAM in the CPU's window (cpu true) or in the
 * VRAM only the GPU reaches, in whole 64 KiB steps, held once by the
 * caller.
 *
 * Returns 0, EINVAL for no bytes, ENOMEM, or the copy engine's error for
 * VRAM outside the window.
 */
int
amdrdna_backing_create(
	struct amdrdna_device *device,
	uint64_t bytes,
	bool cpu,
	struct amdrdna_backing **result)
{
	struct amdrdna_backing *backing;
	volatile uint8_t *window;
	uint64_t whole;
	uint64_t at;
	int error;

	/* Some bytes, in whole steps. */
	*result = NULL;
	if (bytes == 0U)
		return EINVAL;
	whole = (bytes + SPACE_MAPPING_ALIGN - 1U) & ~(SPACE_MAPPING_ALIGN - 1U);

	/* The record. */
	backing = kern_calloc(1U, sizeof(*backing));
	if (backing == NULL)
		return ENOMEM;

	/* The VRAM, from the pool asked for. */
	error = amdrdna_vram_take(device, cpu, whole, SPACE_MAPPING_ALIGN, &backing->vram);
	if (error != 0) {
		kern_free(backing);
		return ENOMEM;
	}

	/* Cleared, so no earlier contents show: through the window, or by the copy engine outside it. */
	if (cpu) {
		window = amdrdna_vram_cpu(device, backing->vram);
		for (at = 0U; at < whole; at += 8U)
			kern_mmio_write64(window + at, 0U);
	} else {
		error = amdrdna_engines_clear(device, backing->vram, whole);
		if (error != 0) {
			amdrdna_vram_give(device, false, backing->vram, whole);
			kern_free(backing);
			return error;
		}
	}

	/* One holder: the caller. */
	backing->bytes = whole;
	backing->asked = bytes;
	backing->refs = 1U;
	backing->cpu = cpu;
	backing->kept = false;

	/* Succeeded: the backing is the caller's. */
	*result = backing;
	return 0;
}

/* Takes one more reference to a backing. */
void
amdrdna_backing_get(
	struct amdrdna_device *device,
	struct amdrdna_backing *backing)
{
	unsigned long irq;

	/* refs counts the holders; the last put gives the VRAM back. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	backing->refs++;

	spin_unlock_irqrestore(&device->spaces.lock, irq);
}

/*
 * Drops one reference to a backing: the last one gives the VRAM back,
 * unless work nobody could stop may still touch it, and frees the record.
 */
void
amdrdna_backing_put(
	struct amdrdna_device *device,
	struct amdrdna_backing *backing)
{
	unsigned long irq;
	uint32_t refs;
	bool kept;

	/* Counts the reference off. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	backing->refs--;
	refs = backing->refs;
	kept = backing->kept;

	spin_unlock_irqrestore(&device->spaces.lock, irq);

	/* Others still hold it. */
	if (refs != 0U)
		return;

	/* The VRAM goes back unless it is kept for good. */
	if (!kept)
		amdrdna_vram_give(device, backing->cpu, backing->vram, backing->bytes);

	/* The record. */
	kern_free(backing);
}

/* Keeps a backing's VRAM for good: GPU work nobody could stop may still touch it. */
void
amdrdna_backing_keep(
	struct amdrdna_device *device,
	struct amdrdna_backing *backing)
{
	unsigned long irq;

	/* kept makes the last put leave the VRAM taken. */
	irq = spin_lock_irqsave(&device->spaces.lock);

	backing->kept = true;

	spin_unlock_irqrestore(&device->spaces.lock, irq);
}

/* Reports where the CPU reads and writes a backing in its window; NULL for one outside the window. */
volatile uint8_t *
amdrdna_backing_cpu(
	struct amdrdna_device *device,
	const struct amdrdna_backing *backing)
{
	volatile uint8_t *cpu;

	/* Outside the window the CPU does not reach it. */
	if (!backing->cpu)
		return NULL;

	/* The window's view of the VRAM. */
	cpu = amdrdna_vram_cpu(device, backing->vram);

	/* Succeeded: the CPU's address. */
	return cpu;
}

/* Reports the ring a tracked ring stands for, or NULL when it does not run. */
static struct amdrdna_ring *
space_ring(
	struct amdrdna_device *device,
	unsigned tracked)
{
	struct amdrdna_engines *engines;
	unsigned compute;

	/* The graphics ring. */
	engines = &device->engines;
	if (tracked == AMDRDNA_TRACKED_GRAPHICS) {
		if (!engines->graphics_running)
			return NULL;
		return &engines->graphics;
	}

	/* A compute queue the stage attached. */
	if (tracked >= AMDRDNA_TRACKED_COMPUTE && tracked < AMDRDNA_TRACKED_COPY) {
		compute = tracked - AMDRDNA_TRACKED_COMPUTE;
		if (!engines->compute_running || compute >= engines->compute_count)
			return NULL;
		return &engines->compute[compute];
	}

	/* The first copy engine. */
	if (tracked == AMDRDNA_TRACKED_COPY) {
		if (!engines->copy_running)
			return NULL;
		return &engines->copy;
	}

	/* No such ring. */
	return NULL;
}

/* Reads the fence each tracked ring last wrote (0 for one that does not run). */
static void
space_passed(
	struct amdrdna_device *device,
	uint32_t *passed)
{
	struct amdrdna_ring *ring;
	unsigned tracked;

	/* Each tracked ring's fence word. */
	for (tracked = 0U; tracked < AMDRDNA_TRACKED_RINGS; tracked++) {
		ring = space_ring(device, tracked);
		passed[tracked] = 0U;
		if (ring != NULL)
			passed[tracked] = amdrdna_ring_fence(device, ring);
	}
}

/*
 * Builds a run's commands: a wait for the ring's own earlier work when the
 * context still ran some (sync), the drop of the context's translations
 * when they are stale (the context pointed at the space's root table, the
 * hub asked to drop them and waited for, the interrupt controller told the
 * context's space, the graphics ring's fetcher held until the engine
 * caught up), the caches synced, then the indirect buffer in the context.
 * Reports the words.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_ib.c (amdgpu_ib_schedule: the memory
 * sync before a job's indirect buffers),
 * gmc_v10_0.c (gmc_v10_0_emit_flush_gpu_tlb,
 * gmc_v10_0_emit_pasid_mapping) and gfx_v10_0.c
 * (gfx_v10_0_ring_emit_pipeline_sync, gfx_v10_0_ring_emit_vm_flush,
 * gfx_v10_0_ring_emit_ib_gfx, gfx_v10_0_ring_emit_ib_compute).
 */
static unsigned
space_commands(
	struct amdrdna_space *space,
	struct amdrdna_ring *ring,
	unsigned tracked,
	const struct amdrdna_context_grant *grant,
	uint64_t address,
	uint32_t words,
	uint32_t *commands)
{
	struct amdrdna_device *device;
	uint64_t fence_address;
	uint32_t fence_sequence;
	uint32_t table_low;
	uint32_t table_high;
	uint32_t request;
	uint32_t done;
	uint32_t identifier;
	uint32_t invalidate;
	uint64_t stamp_address;
	unsigned engine;
	unsigned count;
	bool graphics;
	bool compute;
	bool stamped;
	int error;

	/* The ring's kind and its invalidation engine: the graphics ring or a compute queue. */
	device = space->device;
	if (tracked == AMDRDNA_TRACKED_GRAPHICS) {
		graphics = true;
		compute = false;
		engine = SPACE_ENGINE_GRAPHICS;
	} else {
		graphics = false;
		compute = true;
		engine = SPACE_ENGINE_COMPUTE_FIRST + (tracked - AMDRDNA_TRACKED_COMPUTE);
	}

	/* The ring waits for its own earlier work in the context first: its fence reaching its newest sequence. */
	count = 0U;
	if (grant->sync) {
		fence_address = amdrdna_ring_fence_address(device, ring);
		fence_sequence = amdrdna_ring_sequence(ring);
		count += amdrdna_pm4_wait_memory(commands + count, fence_address, fence_sequence, graphics);
	}

	/* The drop of the context's translations, when they are stale. */
	if (grant->flush) {
		/* The registers of the context and of the ring's invalidation engine. */
		table_low = amdrdna_hub_register(device, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_TABLE_BASE_LOW, grant->context);
		table_high = amdrdna_hub_register(device, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_TABLE_BASE_HIGH, grant->context);
		request = amdrdna_hub_register(device, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_INVALIDATE_REQUEST, engine);
		done = amdrdna_hub_register(device, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_INVALIDATE_DONE, engine);
		identifier = 0U;
		error = amdrdna_block_index(device, AMDRDNA_BLOCK_IH, AMDRDNA_IH_WINDOW_MAIN, AMDRDNA_IH_REG_SPACE_TO_PROCESS + grant->context, &identifier);
		if (error != 0)
			identifier = 0U;

		/* The context points at the space's root table. */
		if (graphics) {
			count += amdrdna_pm4_fetcher_write_register(commands + count, table_low, (uint32_t)space->root_word);
			count += amdrdna_pm4_fetcher_write_register(commands + count, table_high, (uint32_t)(space->root_word >> 32));
		} else {
			count += amdrdna_pm4_write_register(commands + count, table_low, (uint32_t)space->root_word, false);
			count += amdrdna_pm4_write_register(commands + count, table_high, (uint32_t)(space->root_word >> 32), false);
		}

		/* The hub drops the context's translations, and the ring waits for it. */
		invalidate = amdrdna_invalidate_word(grant->context);
		count += amdrdna_pm4_write_and_wait(commands + count, request, done, invalidate, 1U << grant->context, graphics);

		/* The interrupt controller names the context's faults after the space. */
		if (identifier != 0U) {
			if (graphics) {
				count += amdrdna_pm4_fetcher_write_register(commands + count, identifier, space->id);
			} else {
				count += amdrdna_pm4_write_register(commands + count, identifier, space->id, false);
			}
		}

		/* The graphics ring's fetcher waits for the engine before it reads on. */
		if (graphics)
			count += amdrdna_pm4_fetcher_sync(commands + count);
	}

	/*
	 * The GPU's caches dropped and written back, so the indirect buffer and
	 * what it reads come from memory as the CPU last wrote them through the
	 * window (which the level-2 cache does not see).
	 */
	count += amdrdna_pm4_cache_sync(commands + count, false);

	/* Only the graphics ring's runs are timed, and only while the executor's timing asks. */
	stamped = false;
	if (graphics && device->spaces.stamped)
		stamped = true;

	/* A timed run notes the GPU's counter as the command processor starts it. */
	if (stamped) {
		stamp_address = amdrdna_ring_stamp_address(device, ring, false);
		count += amdrdna_pm4_stamp(commands + count, stamp_address, compute);
	}

	/* The indirect buffer, in the context. */
	count += amdrdna_pm4_indirect(commands + count, address, words, grant->context, compute);

	/* And again once everything the buffer started has finished. */
	if (stamped) {
		stamp_address = amdrdna_ring_stamp_address(device, ring, true);
		count += amdrdna_pm4_stamp_done(commands + count, stamp_address, compute);
	}

	/* Succeeded: the words. */
	return count;
}

/*
 * Makes one page table of a level: a page of the CPU's VRAM cleared to no
 * entries, and for a directory the list of the tables below it.
 *
 * Returns 0 or ENOMEM.
 */
static int
space_table_create(
	struct amdrdna_device *device,
	unsigned level,
	struct amdrdna_table **result)
{
	struct amdrdna_table *table;
	volatile uint8_t *cpu;
	unsigned index;
	int error;

	/* The record. */
	*result = NULL;
	table = kern_calloc(1U, sizeof(*table));
	if (table == NULL)
		return ENOMEM;

	/* A directory lists the tables below it. */
	if (level < SPACE_LEAF_LEVEL) {
		table->below = kern_calloc(SPACE_TABLE_ENTRIES, sizeof(*table->below));
		if (table->below == NULL) {
			kern_free(table);
			return ENOMEM;
		}
	}

	/* The table's page of VRAM. */
	error = amdrdna_vram_take(device, true, SPACE_PAGE_BYTES, SPACE_PAGE_BYTES, &table->vram);
	if (error != 0) {
		kern_free(table->below);
		kern_free(table);
		return ENOMEM;
	}

	/* No entry yet. */
	cpu = amdrdna_vram_cpu(device, table->vram);
	for (index = 0U; index < SPACE_TABLE_ENTRIES; index++)
		kern_mmio_write64(cpu + (size_t)index * SPACE_ENTRY_BYTES, 0U);

	/* Succeeded: the table is the caller's. */
	*result = table;
	return 0;
}

/* Frees a page table and every table below it, with their VRAM. */
static void
space_table_free(
	struct amdrdna_device *device,
	struct amdrdna_table *table,
	unsigned level)
{
	unsigned index;

	/* The tables below a directory first. */
	if (table->below != NULL) {
		for (index = 0U; index < SPACE_TABLE_ENTRIES; index++) {
			if (table->below[index] != NULL)
				space_table_free(device, table->below[index], level + 1U);
		}

		/* The list of the tables below. */
		kern_free(table->below);
	}

	/* The table's VRAM and record. */
	amdrdna_vram_give(device, true, table->vram, SPACE_PAGE_BYTES);
	kern_free(table);
}

/* Writes one entry of a page table through the CPU's window. */
static void
space_table_write(
	struct amdrdna_device *device,
	struct amdrdna_table *table,
	unsigned index,
	uint64_t entry)
{
	volatile uint8_t *cpu;

	/* The entry's place in the table's page. */
	cpu = amdrdna_vram_cpu(device, table->vram);
	kern_mmio_write64(cpu + (size_t)index * SPACE_ENTRY_BYTES, entry);
}

/*
 * Finds the leaf table that maps an address of a space, making the
 * missing directories and the leaf on the way.  The caller holds the
 * space's lock.  Returns 0 or ENOMEM.
 */
static int
space_leaf(
	struct amdrdna_space *space,
	uint64_t address,
	struct amdrdna_table **leaf)
{
	struct amdrdna_table *table;
	struct amdrdna_table *below;
	unsigned index[AMDRDNA_VM_LEVELS];
	unsigned level;
	uint64_t entry;
	int error;

	/* The address's index at each level. */
	amdrdna_vm_split(address, index);

	/* Walks down from the root, making each missing table. */
	table = space->root;
	for (level = 0U; level < SPACE_LEAF_LEVEL; level++) {
		below = table->below[index[level]];
		if (below == NULL) {
			/* The missing table, and the directory's entry pointing at it. */
			error = space_table_create(space->device, level + 1U, &below);
			if (error != 0)
				return error;
			table->below[index[level]] = below;
			table->count++;
			entry = space_table_word(space->device, below->vram);
			space_table_write(space->device, table, index[level], entry);
		}

		/* One level down. */
		table = below;
	}

	/* Succeeded: the leaf table. */
	*leaf = table;
	return 0;
}

/*
 * Clears the page entries of a run of a space's addresses (whole pages);
 * the tables stay.  The caller holds the space's lock.
 */
static void
space_clear(
	struct amdrdna_space *space,
	uint64_t address,
	uint64_t bytes)
{
	struct amdrdna_table *table;
	unsigned index[AMDRDNA_VM_LEVELS];
	unsigned level;
	uint64_t page;

	/* Each page of the run. */
	for (page = 0U; page < bytes; page += SPACE_PAGE_BYTES) {
		/* Walks down to the page's leaf; a missing table maps nothing. */
		amdrdna_vm_split(address + page, index);
		table = space->root;
		for (level = 0U; level < SPACE_LEAF_LEVEL; level++) {
			if (table == NULL)
				break;
			table = table->below[index[level]];
		}

		/* No leaf: nothing to clear. */
		if (table == NULL)
			continue;

		/* The entry cleared. */
		space_table_write(space->device, table, index[SPACE_LEAF_LEVEL], 0U);
		if (table->count != 0U)
			table->count--;
	}
}

/* Moves a space's table generation on, so its next run drops the context's translations. */
static void
space_changed(
	struct amdrdna_space *space)
{
	unsigned long irq;

	/* The generation the context pool compares with the context's last drop. */
	irq = spin_lock_irqsave(&space->device->spaces.lock);

	space->generation++;

	spin_unlock_irqrestore(&space->device->spaces.lock, irq);
}

/* Loses a space whose run did not end: it runs nothing more, and its session is told. */
static void
space_lose(
	struct amdrdna_space *space,
	int error)
{
	struct amdrdna_device *device;
	unsigned long irq;
	void *session;
	bool first;

	/* Marks it lost, once. */
	device = space->device;
	first = false;
	session = NULL;
	irq = spin_lock_irqsave(&device->spaces.lock);

	if (!space->lost) {
		space->lost = true;
		space->lost_error = error;
		device->spaces.lost++;
		session = space->session;
		first = true;
	}

	spin_unlock_irqrestore(&device->spaces.lock, irq);

	/* Says so the first time, and tells the session. */
	if (!first)
		return;
	amdrdna_mark("space %u lost: its run did not end (%d)", (unsigned)space->id, error);
	if (session != NULL)
		amdrdna_node_session_lost(device, session, EIO);
}

/*
 * Resets the graphics queue after a run of a context did not end, and
 * says how long it took; a queue that cannot be reset fails the device
 * (every session's work then fails).
 */
static void
space_reset(
	struct amdrdna_device *device,
	unsigned context)
{
	uint64_t started;
	uint64_t taken;
	int error;

	/* The reset, timed. */
	started = clock_milliseconds(NULL);
	error = amdrdna_cp_graphics_reset(device, context);
	taken = clock_milliseconds(NULL) - started;

	/* Says how it went; a failed one ends the device's work. */
	if (error != 0) {
		amdrdna_mark("graphics queue reset for context %u failed (%d) after %llums; the device takes no more work", context, error,
		    (unsigned long long)taken);
		amdrdna_node_device_failed(device, EIO);
		return;
	}

	/* Counted, and said. */
	device->spaces.resets++;
	amdrdna_mark("graphics queue reset for context %u in %llums, the queue runs again", context, (unsigned long long)taken);
}

/*
 * The word that points a hub context, or a directory entry, at a table:
 * its physical VRAM address, present.
 *
 * Procedure referenced: Linux v6.19
 * drivers/gpu/drm/amd/amdgpu/amdgpu_gmc.c (amdgpu_gmc_pd_addr,
 * amdgpu_gmc_get_pde_for_bo) and gmc_v10_0.c (gmc_v10_0_get_vm_pde: a
 * VRAM table's address is its offset plus the frame buffer's physical
 * offset).
 */
static uint64_t
space_table_word(
	struct amdrdna_device *device,
	uint64_t vram)
{
	uint64_t word;

	/* The table's physical address with the present bit. */
	word = amdrdna_vm_table_entry(vram + device->frame_buffer_physical, false);

	/* Succeeded: the word. */
	return word;
}

/*
 * Reports whether no work of a space can still run; the caller holds the
 * spaces' lock.  A space whose context changed hands ran nothing more in
 * it: the context was handed on only once its work had ended.
 */
static bool
space_quiet_locked(
	struct amdrdna_space *space)
{
	uint32_t passed[AMDRDNA_TRACKED_RINGS];
	struct amdrdna_device *device;
	bool idle;

	/* A space that never held a context ran nothing. */
	device = space->device;
	if (space->context == 0U)
		return true;

	/* A context handed on carries nothing of the space's. */
	if (device->spaces.pool.slots[space->context].owner != space->id)
		return true;

	/* Every ring passed the context's work. */
	space_passed(device, passed);
	idle = amdrdna_context_idle(&device->spaces.pool, space->context, passed);
	if (!idle)
		return false;

	/* Succeeded: nothing of the space runs. */
	return true;
}

/* Reads one instance of a named register of the graphics hub. */
static uint32_t
space_hub_read(
	struct amdrdna_device *device,
	enum amdrdna_hub_register which,
	unsigned instance)
{
	uint32_t index;
	uint32_t value;

	/* The register's index, then its value. */
	index = amdrdna_hub_register(device, AMDRDNA_HUB_GRAPHICS, which, instance);
	value = amdrdna_read(device, index);

	/* Succeeded: the value. */
	return value;
}
