/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The work channel (stage D1, ws037-p007, nvrtx.work=1).
 *
 * After the golden context (P5g) and the VRAM allocator (P4m) the driver
 * makes the graphics channel the GPU device keeps for the work it is given,
 * the way the first compute launch (compute.c) makes its channel and NVK
 * keeps one channel with both classes (both MIT):
 *
 *   1. an arena of the VRAM allocator for the space's page tables, the
 *      channel's instance block and USERD, and its own context buffers;
 *   2. an address space the driver owns over the 512 MiB of
 *      NVRTX_WORK_VA_BASE: the context buffers privileged at the golden
 *      context's addresses, the driver's pages (ring, semaphore, the
 *      fences' push buffers), and later the runs mapped for users;
 *   3. the channel on the graphics engine, the promote of its context
 *      buffers, the 3D and compute objects, the bind and the schedule;
 *   4. a first submission that binds both classes to their subchannels,
 *      and its fence.
 *
 * Afterwards nvrtx_work_submit() hands a push buffer segment to the
 * channel followed by a fence of the driver's, nvrtx_work_wait() polls for
 * a fence, and the map calls write the space's page entries for a run and
 * drop the stale translations.  The stop undoes 3 and 2 in nouveau's order
 * before the graphics context's stop.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/atomic.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-compute.h"
#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The arena: its size and alignment in the VRAM allocator. */
#define WORK_ARENA_BYTES		0x2000000ULL
#define WORK_ARENA_ALIGN		0x200000ULL

/*
 * Where the timed runs (nvrtx.vk.perf=1) leave their times in the
 * semaphore's page: the fence's 16 bytes at its start (value, then the
 * timer at byte 8), and the start stamp's 16 bytes after them.
 */
#define WORK_FENCE_TIMER_AT		8U
#define WORK_START_AT			0x10U
#define WORK_START_TIMER_AT		0x18U

/* The arena's parts: the tables, the instance block and USERD, then the channel's own context buffers. */
#define WORK_TABLES_AT			0x000000ULL
#define WORK_INSTANCE_AT		0x200000ULL
#define WORK_USERD_AT			0x201000ULL
#define WORK_BUFFERS_AT			0x400000ULL

/* The driver's pages in the space, in order: the ring, the semaphore, the fences' push buffers. */
#define WORK_RING_VA			(NVRTX_WORK_VA_DRIVER + 0x0000ULL)
#define WORK_SEMAPHORE_VA		(NVRTX_WORK_VA_DRIVER + 0x1000ULL)
#define WORK_FENCES_VA			(NVRTX_WORK_VA_DRIVER + 0x2000ULL)
#define WORK_FENCES_BYTES		(NVRTX_WORK_RING_ENTRIES * NVRTX_WORK_FENCE_BYTES)
#define WORK_DRIVER_PAGES		(2U + WORK_FENCES_BYTES / 0x1000U)

/* The address-space limit written into the instance block (47-bit virtual addresses). */
#define WORK_VA_LIMIT			(1ULL << 47)

/* The channel number on the graphics runlist (the golden context 1, the scrubber 2, the compute launch 3). */
#define WORK_CHID			4U

/* USERD's get and put of the ring, and the doorbell: runlist in bits 22:16, channel in bits 11:0. */
#define WORK_USERD_RING_GET		0x88U
#define WORK_USERD_RING_PUT		0x8cU
#define WORK_REG_DOORBELL		0xbb0090U

/* The promote's number of the unrestricted access map, which a channel sharing the global buffers leaves out. */
#define WORK_BUFFER_UNRESTRICTED_MAP	11U

/* How long the first fence and a free ring entry are waited for, and how often the CPU looks. */
#define WORK_FIRST_FENCE_US		2000000U
#define WORK_ENTRY_WAIT_US		2000000U
#define WORK_POLL_US			50U

/*
 * How long a wait looks at the semaphore without sleeping before it sleeps
 * between looks: a sleep lasts at least a scheduler tick (1 ms), many
 * times the few tens of microseconds the GPU takes for most work, so the
 * work that ends soon is seen as it ends (ws037-p007 9, F2).
 */
#define WORK_SPIN_US			2000U

static int work_arena(struct nvrtx_gpu *gpu);
static int work_buffers(struct nvrtx_gpu *gpu);
static int work_tables(struct nvrtx_gpu *gpu);
static int work_space(struct nvrtx_gpu *gpu);
static int work_channel(struct nvrtx_gpu *gpu);
static int work_objects(struct nvrtx_gpu *gpu);
static int work_first(struct nvrtx_gpu *gpu);
static int work_entries_write(struct nvrtx_gpu *gpu, uint64_t va, const uint8_t *entries, uint64_t pages);
static int work_ring_wait(struct nvrtx_gpu *gpu, uint32_t entry);
static int work_ring_put(struct nvrtx_gpu *gpu, uint32_t entry, uint64_t va, size_t length);
static int work_doorbell(struct nvrtx_gpu *gpu);
static uint32_t work_semaphore(const struct nvrtx_gpu *gpu);
static uint64_t work_semaphore_timer(const struct nvrtx_gpu *gpu, size_t at);
static uint64_t work_align(uint64_t value, uint32_t shift);

/*
 * Runs stage D1: the arena, the space, the channel with both classes and
 * its first fence.
 *
 * Needs the golden context of P5g and the VRAM allocator of P4m.  A failure
 * is returned; the finish's stop frees whatever objects were made.
 */
int
nvrtx_work_start(
	struct nvrtx_gpu *gpu)
{
	bool timed;
	int error;

	nvrtx_stage_mark("P9w begin");

	/* Each run is timed on the GPU when the boot asks for the counts (nvrtx.vk.perf=1). */
	timed = nvrtx_stage_vk_perf_wanted();
	gpu->work.timed = 0;
	if (timed)
		gpu->work.timed = 1;

	/* The channel's context comes from the golden context. */
	if (gpu->gr.buffer_count == 0U) {
		nvrtx_stage_mark("P9w fail: no golden context");
		return EINVAL;
	}

	/* The arena comes from the VRAM allocator. */
	if (gpu->memory_state.ready == 0) {
		nvrtx_stage_mark("P9w fail: no VRAM allocator (P4m)");
		return ENODEV;
	}

	/* The arena, the system-memory buffers and the tables. */
	error = work_arena(gpu);
	if (error != 0)
		return error;

	error = work_buffers(gpu);
	if (error != 0)
		return error;

	error = work_tables(gpu);
	if (error != 0)
		return error;

	/* The space and the channel with its two objects. */
	error = work_space(gpu);
	if (error != 0)
		return error;

	error = work_channel(gpu);
	if (error != 0)
		return error;

	error = work_objects(gpu);
	if (error != 0)
		return error;

	nvrtx_stage_mark("P9w ok: work channel %u on runlist %u", (unsigned)WORK_CHID, (unsigned)gpu->engine.pick.graphics_runlist);

	/* The classes on their subchannels, and the first fence. */
	error = work_first(gpu);
	if (error != 0)
		return error;

	/* Reports the GPU's performance level, and raises it when the boot asks (nvrtx.boost=1). */
	nvrtx_perf_start(gpu);

	/* Succeeded: the channel takes work. */
	return 0;
}

/*
 * Undoes D1's objects in nouveau's order before the graphics context's
 * stop: the channel off the scheduler, the 3D and compute objects, the
 * channel, the root, the space.  Failures are counted and shown; the
 * unload removes the rest.
 */
void
nvrtx_work_stop(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	uint8_t params[NVRTX_UNPDIR_PARAMS_BYTES];
	const uint8_t *reply;
	size_t reply_length;
	int failures;
	int error;

	work = &gpu->work;
	failures = 0;

	/* Nothing is submitted from now on. */
	work->ready = 0;

	/* Takes the channel off the scheduler. */
	if (work->scheduled != 0) {
		kern_memset(params, 0, sizeof(params));
		error = nvrtx_rm_control(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_WORK_CHANNEL, NVRTX_CTL_SCHEDULE, params, NVRTX_SCHEDULE_PARAMS_BYTES, &reply, &reply_length);
		if (error != 0)
			failures++;
		work->scheduled = 0;
	}

	/* Frees the 3D object, then the compute object. */
	if (work->threed_made != 0) {
		error = nvrtx_rm_free(gpu, NVRTX_HANDLE_WORK_THREED);
		if (error != 0)
			failures++;
		work->threed_made = 0;
	}

	if (work->compute_made != 0) {
		error = nvrtx_rm_free(gpu, NVRTX_HANDLE_WORK_COMPUTE);
		if (error != 0)
			failures++;
		work->compute_made = 0;
	}

	/* Frees the channel. */
	if (work->channel_made != 0) {
		error = nvrtx_rm_free(gpu, NVRTX_HANDLE_WORK_CHANNEL);
		if (error != 0)
			failures++;
		work->channel_made = 0;
	}

	/* Takes the root back from the space, then frees it. */
	if (work->root_set != 0) {
		kern_memset(params, 0, sizeof(params));
		nvrtx_put32(params, 0U, NVRTX_HANDLE_WORK_SPACE);
		error = nvrtx_rm_control(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_DEVICE, NVRTX_CTL_UNSET_PAGE_DIRECTORY, params, sizeof(params), &reply, &reply_length);
		if (error != 0)
			failures++;
		work->root_set = 0;
	}

	if (work->space_made != 0) {
		error = nvrtx_rm_free(gpu, NVRTX_HANDLE_WORK_SPACE);
		if (error != 0)
			failures++;
		work->space_made = 0;
	}

	/* Says how the stop went, when anything failed. */
	if (failures != 0)
		nvrtx_stage_mark("P6: %d work frees failed; the unload removes the rest", failures);
}

/*
 * Gives back D1's system-memory buffers and its arena.
 *
 * Only once the GPU no longer reads them: after the GSP is stopped.
 */
void
nvrtx_work_release(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;

	work = &gpu->work;

	/* Frees each buffer that was allocated. */
	nvrtx_buffer_free(gpu, &work->ring);
	nvrtx_buffer_free(gpu, &work->semaphore);
	nvrtx_buffer_free(gpu, &work->fences);
	nvrtx_buffer_free(gpu, &work->methods);

	/* Returns the arena to the VRAM allocator. */
	if (work->arena_made != 0) {
		nvrtx_bo_free(gpu, &work->arena);
		work->arena_made = 0;
	}
}

/*
 * Maps a run of VRAM into the work channel's space for its users.
 *
 * The run gets the next free addresses and VRAM entries that are neither
 * privileged nor read-only; stale translations are dropped before the
 * call returns.  Returns the address in *va, ENOSPC when the space is
 * full, EINVAL for a run not on page boundaries, or ENODEV before the
 * channel runs.
 */
int
nvrtx_work_map_vram(
	struct nvrtx_gpu *gpu,
	uint64_t physical,
	uint64_t size,
	uint64_t *va)
{
	struct nvrtx_work_state *work;
	uint8_t *entries;
	uint64_t pages;
	uint64_t address;
	int error;

	work = &gpu->work;

	/* Whole pages only. */
	if (size == 0U || (size & 0xfffU) != 0U || (physical & 0xfffU) != 0U)
		return EINVAL;

	/* Builds the run's entries. */
	pages = size / NVRTX_GSP_PAGE_SIZE;
	entries = kern_malloc((size_t)pages * 8U);
	if (entries == NULL)
		return ENOMEM;

	nvrtx_mmu_gr_entries_vram(entries, physical, pages);

	/* Takes addresses and writes the entries there. */
	mutex_lock(&work->lock);

	error = ENODEV;
	if (work->ready != 0)
		error = nvrtx_heap_alloc(&work->va, size, NVRTX_GSP_PAGE_SIZE, &address);
	if (error == 0) {
		error = work_entries_write(gpu, address, entries, pages);
		if (error != 0)
			(void)nvrtx_heap_free(&work->va, address, size);
	}

	mutex_unlock(&work->lock);

	kern_free(entries);

	/* Reports a full space or entries that could not be written. */
	if (error != 0)
		return error;

	/* Succeeded: the run is reachable at the address. */
	*va = address;
	return 0;
}

/*
 * Maps system-memory pages into the work channel's space for its users.
 *
 * The pages get the next free addresses in order, with coherent, volatile
 * entries that are not privileged.  Returns as nvrtx_work_map_vram() does.
 */
int
nvrtx_work_map_system(
	struct nvrtx_gpu *gpu,
	const uint64_t *pages,
	size_t count,
	uint64_t *va)
{
	struct nvrtx_work_state *work;
	uint8_t *entries;
	uint64_t size;
	uint64_t address;
	int error;

	work = &gpu->work;

	/* At least one page. */
	if (count == 0U)
		return EINVAL;

	/* Builds the pages' entries. */
	size = (uint64_t)count * NVRTX_GSP_PAGE_SIZE;
	entries = kern_malloc(count * 8U);
	if (entries == NULL)
		return ENOMEM;

	nvrtx_mmu_gr_entries_system(entries, pages, count);

	/* Takes addresses and writes the entries there. */
	mutex_lock(&work->lock);

	error = ENODEV;
	if (work->ready != 0)
		error = nvrtx_heap_alloc(&work->va, size, NVRTX_GSP_PAGE_SIZE, &address);
	if (error == 0) {
		error = work_entries_write(gpu, address, entries, count);
		if (error != 0)
			(void)nvrtx_heap_free(&work->va, address, size);
	}

	mutex_unlock(&work->lock);

	kern_free(entries);

	/* Reports a full space or entries that could not be written. */
	if (error != 0)
		return error;

	/* Succeeded: the pages are reachable at the address. */
	*va = address;
	return 0;
}

/*
 * Unmaps a run mapped by one of the map calls and gives its addresses back.
 *
 * The caller has made sure no submitted work still reads the run (its
 * fence has passed).  The entries are cleared and stale translations
 * dropped before the addresses can be handed out again.  Returns EIO when
 * the entries could not be cleared; the addresses then stay taken.
 */
int
nvrtx_work_unmap(
	struct nvrtx_gpu *gpu,
	uint64_t va,
	uint64_t size)
{
	struct nvrtx_work_state *work;
	uint8_t *entries;
	uint64_t pages;
	int error;

	work = &gpu->work;

	/* Whole pages only. */
	if (size == 0U || (size & 0xfffU) != 0U || (va & 0xfffU) != 0U)
		return EINVAL;

	/* Invalid entries for the run. */
	pages = size / NVRTX_GSP_PAGE_SIZE;
	entries = kern_calloc((size_t)pages, 8U);
	if (entries == NULL)
		return ENOMEM;

	/* Clears the entries, then gives the addresses back. */
	mutex_lock(&work->lock);

	error = work_entries_write(gpu, va, entries, pages);
	if (error == 0)
		(void)nvrtx_heap_free(&work->va, va, size);

	mutex_unlock(&work->lock);

	kern_free(entries);

	/* Reports entries that could not be cleared. */
	if (error != 0)
		return EIO;

	/* Succeeded: the addresses are free again. */
	return 0;
}

/*
 * Submits a push buffer segment, followed by a fence of the driver's.
 *
 * The segment is length bytes at push_va in the channel's space; the GPU
 * reads it after the call returns, so it stays intact until the fence
 * returned in *fence has passed.  A timed channel (nvrtx.vk.perf=1) puts a
 * start stamp before the segment and times the fence, for
 * nvrtx_work_run_time().  Waits up to 2 s for free ring entries.  Returns
 * ENODEV before the channel runs, ETIMEDOUT when the ring stays full,
 * EINVAL for an empty or misaligned segment.
 */
int
nvrtx_work_submit(
	struct nvrtx_gpu *gpu,
	uint64_t push_va,
	size_t length,
	uint32_t *fence)
{
	struct nvrtx_work_state *work;
	uint8_t *slot;
	uint32_t stamp_entry;
	uint32_t entry;
	uint32_t fence_entry;
	uint32_t value;
	size_t stamp_length;
	size_t fence_length;
	int error;

	work = &gpu->work;

	/* Whole words, within what one ring entry carries. */
	if (length == 0U || (length & 3U) != 0U || length / 4U > NVRTX_WORK_SEGMENT_WORDS_MAX)
		return EINVAL;

	/* Puts the segment and the fence on the ring under the lock. */
	mutex_lock(&work->lock);

	/* The channel must be running. */
	if (work->ready == 0) {
		mutex_unlock(&work->lock);
		return ENODEV;
	}

	/* The entries: without timing the segment's entry also stands for the stamp's, which is not written. */
	stamp_entry = work->put;
	entry = work->put;

	/* A timed run's start stamp takes the first entry, and the segment the next. */
	if (work->timed != 0)
		entry = (stamp_entry + 1U) & (NVRTX_WORK_RING_ENTRIES - 1U);

	/* The fence follows the segment. */
	fence_entry = (entry + 1U) & (NVRTX_WORK_RING_ENTRIES - 1U);

	/* Waits until the entries' earlier work is done. */
	error = work_ring_wait(gpu, stamp_entry);
	if (error == 0)
		error = work_ring_wait(gpu, entry);
	if (error == 0)
		error = work_ring_wait(gpu, fence_entry);
	if (error != 0) {
		mutex_unlock(&work->lock);
		return error;
	}

	/* Writes the fence's push buffer into the fence entry's slot, timed for a timed run. */
	value = work->fence + 1U;
	slot = (uint8_t *)work->fences.dma.address + (size_t)fence_entry * NVRTX_WORK_FENCE_BYTES;
	if (work->timed != 0) {
		fence_length = nvrtx_push_fence_timed(slot, WORK_SEMAPHORE_VA, value);
	} else {
		fence_length = nvrtx_push_fence(slot, WORK_SEMAPHORE_VA, value);
	}

	/* A timed run's start stamp, in its own entry's slot, on the ring before the segment. */
	error = 0;
	if (work->timed != 0) {
		slot = (uint8_t *)work->fences.dma.address + (size_t)stamp_entry * NVRTX_WORK_FENCE_BYTES;
		stamp_length = nvrtx_push_stamp(slot, WORK_SEMAPHORE_VA + WORK_START_AT, value);
		error = work_ring_put(gpu, stamp_entry, WORK_FENCES_VA + (uint64_t)stamp_entry * NVRTX_WORK_FENCE_BYTES, stamp_length);
	}

	/* The segment, then the fence; the doorbell after the put moved past them. */
	if (error == 0)
		error = work_ring_put(gpu, entry, push_va, length);
	if (error == 0)
		error = work_ring_put(gpu, fence_entry, WORK_FENCES_VA + (uint64_t)fence_entry * NVRTX_WORK_FENCE_BYTES, fence_length);
	if (error == 0)
		error = work_doorbell(gpu);
	if (error != 0) {
		mutex_unlock(&work->lock);
		return error;
	}

	/*
	 * Every entry now belongs to this fence: they are written again only
	 * once it has passed.  fence is the newest value the semaphore will
	 * reach.
	 */
	work->entry_fence[stamp_entry] = value;
	work->entry_fence[entry] = value;
	work->entry_fence[fence_entry] = value;
	work->fence = value;

	mutex_unlock(&work->lock);

	/* Succeeded: the work runs and will release the fence. */
	*fence = value;
	return 0;
}

/*
 * Reports how long the GPU took for a timed run whose fence has passed:
 * the nanoseconds from its start stamp to its fence, by the GPU's timer.
 *
 * Returns ENOENT when the channel is not timed or the stamps are another
 * run's (a later submission wrote them), else 0.
 */
int
nvrtx_work_run_time(
	struct nvrtx_gpu *gpu,
	uint32_t fence,
	uint64_t *gpu_ns)
{
	const volatile uint32_t *semaphore;
	uint32_t started;
	uint32_t ended;
	uint64_t start;
	uint64_t end;

	/* Only a timed channel leaves stamps. */
	if (gpu->work.timed == 0)
		return ENOENT;

	/* Both values must be this run's; each was written after its timer. */
	semaphore = (const volatile uint32_t *)gpu->work.semaphore.dma.address;
	started = semaphore[WORK_START_AT / 4U];
	ended = semaphore[0];
	if (started != fence || ended != fence)
		return ENOENT;

	/* The timers are read only after the values that follow them. */
	__atomic_thread_fence(__ATOMIC_ACQUIRE);

	/* The timers at the start and at the fence. */
	start = work_semaphore_timer(gpu, WORK_START_TIMER_AT);
	end = work_semaphore_timer(gpu, WORK_FENCE_TIMER_AT);

	/* A fence timed before its own start is not a time. */
	if (end < start)
		return ENOENT;

	/* Succeeded: the GPU's time for the run. */
	*gpu_ns = end - start;
	return 0;
}

/*
 * Waits until a fence has passed, up to a time measured in microseconds.
 *
 * The semaphore lies in coherent system memory, so looking at it costs
 * the CPU and the bus nothing: the first 2 ms it is looked at without a
 * pause longer than the processor's spin hint, after that between sleeps.
 * Returns 0, or ETIMEDOUT when the semaphore did not reach it in time.
 */
int
nvrtx_work_wait(
	struct nvrtx_gpu *gpu,
	uint32_t fence,
	uint32_t timeout_us)
{
	uint64_t start;
	uint64_t spent;
	uint32_t seen;
	bool passed;

	/* Looks until the fence has passed or the time, measured, is spent. */
	start = nvrtx_now_us();
	for (;;) {
		seen = work_semaphore(gpu);
		passed = nvrtx_work_fence_passed(seen, fence);
		if (passed)
			break;

		spent = nvrtx_now_us() - start;
		if (spent > timeout_us)
			return ETIMEDOUT;

		/* Spins at first; work that takes longer is looked at again after a sleep. */
		if (spent < WORK_SPIN_US) {
			atomic_spin_hint();
		} else {
			kern_usleep_range(WORK_POLL_US, WORK_POLL_US * 2U);
		}
	}

	/* Succeeded: the work before the fence is done. */
	return 0;
}

/*
 * Takes the arena from the VRAM allocator and lays the channel's own
 * context buffers out in it; the global ones are the golden context's.
 */
static int
work_arena(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	struct nvrtx_gr_state *gr;
	struct nvrtx_gr_buffer *buffer;
	uint64_t at;
	size_t index;
	int error;

	work = &gpu->work;
	gr = &gpu->gr;

	/* Takes the arena. */
	error = nvrtx_bo_alloc(gpu, WORK_ARENA_BYTES, WORK_ARENA_ALIGN, &work->arena);
	if (error != 0) {
		nvrtx_stage_mark("P9w fail: no VRAM for the arena (%d)", error);
		return error;
	}

	work->arena_made = 1;

	/* Each buffer: the golden context's when global, else one of the channel's own. */
	at = WORK_BUFFERS_AT;
	for (index = 0U; index < gr->buffer_count; index++) {
		buffer = &gr->buffers[index];
		if (buffer->global) {
			work->physical[index] = gr->golden_physical[index];
			continue;
		}

		at = work_align(at, buffer->page_shift);
		work->physical[index] = work->arena.vram + at;
		at += work_align(buffer->size, buffer->page_shift);
	}

	/* The channel's own buffers must fit the arena. */
	if (at > WORK_ARENA_BYTES) {
		nvrtx_stage_mark("P9w fail: the context buffers need %llx of VRAM", (unsigned long long)at);
		return ENOSPC;
	}

	/* Succeeded: the arena is laid out. */
	nvrtx_stage_mark("P9w arena at %llx, own buffers %llx bytes", (unsigned long long)work->arena.vram, (unsigned long long)(at - WORK_BUFFERS_AT));
	return 0;
}

/* Allocates the ring, the semaphore, the fences' push buffers and the channel's method buffer. */
static int
work_buffers(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	int error;

	work = &gpu->work;

	/* One page each for the ring and the semaphore. */
	error = nvrtx_buffer_alloc(gpu, NVRTX_WORK_RING_ENTRIES * 8U, &work->ring);
	if (error != 0)
		return error;

	error = nvrtx_buffer_alloc(gpu, NVRTX_GSP_PAGE_SIZE, &work->semaphore);
	if (error != 0)
		return error;

	/* One fence slot per ring entry. */
	error = nvrtx_buffer_alloc(gpu, WORK_FENCES_BYTES, &work->fences);
	if (error != 0)
		return error;

	/* The method buffer, as large as the resource manager asked for P5's. */
	error = nvrtx_buffer_alloc(gpu, gpu->engine.methods_size, &work->methods);
	if (error != 0)
		return error;

	/* Succeeded: the buffers are clear. */
	return 0;
}

/*
 * Builds the space's tables and writes them into the arena: the context
 * buffers privileged at the golden context's addresses (the access map
 * read-only, the unrestricted one left out), the driver's pages after
 * them.  The rest of the range is free for the runs mapped for users.
 */
static int
work_tables(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	struct nvrtx_gr_state *gr;
	struct nvrtx_gr_buffer *buffer;
	uint64_t pages[WORK_DRIVER_PAGES];
	uint64_t size;
	uint8_t *image;
	size_t length;
	size_t index;
	unsigned page;
	int error;

	work = &gpu->work;
	gr = &gpu->gr;

	/* The context buffers must lie below the driver's pages. */
	for (index = 0U; index < gr->buffer_count; index++) {
		buffer = &gr->buffers[index];
		size = work_align(buffer->size, 12U);
		if (gr->virtual_address[index] < NVRTX_WORK_VA_BASE || gr->virtual_address[index] + size > NVRTX_WORK_VA_DRIVER) {
			nvrtx_stage_mark("P9w fail: context buffer %u at %llx is outside the space", (unsigned)index, (unsigned long long)gr->virtual_address[index]);
			return ERANGE;
		}
	}

	/* The driver's pages in order. */
	pages[0] = work->ring.dma.device_address;
	pages[1] = work->semaphore.dma.device_address;
	for (page = 0U; page < WORK_FENCES_BYTES / NVRTX_GSP_PAGE_SIZE; page++)
		pages[2U + page] = work->fences.dma.device_address + (uint64_t)page * NVRTX_GSP_PAGE_SIZE;

	/* The directories in memory. */
	length = nvrtx_mmu_gr_tables_bytes(NVRTX_WORK_VA_BYTES);
	image = kern_malloc(length);
	if (image == NULL)
		return ENOMEM;

	error = nvrtx_mmu_gr_tables(image, work->arena.vram + WORK_TABLES_AT, NVRTX_WORK_VA_BASE, NVRTX_WORK_VA_BYTES);

	/* Each context buffer the promote names by address. */
	for (index = 0U; error == 0 && index < gr->buffer_count; index++) {
		buffer = &gr->buffers[index];
		if (buffer->buffer_id == WORK_BUFFER_UNRESTRICTED_MAP)
			continue;
		error = nvrtx_mmu_gr_map(image,
		    NVRTX_WORK_VA_BASE,
		    NVRTX_WORK_VA_BYTES,
		    gr->virtual_address[index],
		    work->physical[index],
		    work_align(buffer->size, 12U),
		    buffer->read_only);
	}

	/* The driver's pages. */
	if (error == 0)
		error = nvrtx_mmu_gr_map_system(image, NVRTX_WORK_VA_BASE, NVRTX_WORK_VA_BYTES, NVRTX_WORK_VA_DRIVER, pages, WORK_DRIVER_PAGES);

	/* Copies the tables into the arena, and clears the instance block and USERD. */
	if (error == 0)
		error = nvrtx_vram_write(gpu, work->arena.vram + WORK_TABLES_AT, image, length);
	kern_free(image);
	if (error == 0)
		error = nvrtx_vram_zero(gpu, work->arena.vram + WORK_INSTANCE_AT, 2U * NVRTX_INSTANCE_BYTES);
	if (error != 0) {
		nvrtx_stage_mark("P9w fail: the tables could not be written (%d)", error);
		return error;
	}

	/* Hands out the rest of the range to the runs mapped for users. */
	nvrtx_heap_init(&work->va, NVRTX_WORK_VA_USER, NVRTX_WORK_VA_BASE + NVRTX_WORK_VA_BYTES);

	/* Succeeded: the tables are in the VRAM. */
	nvrtx_stage_mark("P9w page tables at %llx", (unsigned long long)(work->arena.vram + WORK_TABLES_AT));
	return 0;
}

/* Makes the address space the driver owns over the tables, gives it the root and drops stale translations. */
static int
work_space(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	uint8_t params[NVRTX_SPACE_PARAMS_BYTES];
	const uint8_t *reply;
	size_t reply_length;
	uint64_t root;
	int error;

	work = &gpu->work;
	root = work->arena.vram + WORK_TABLES_AT;

	/* The space, owned by the driver (as P4's). */
	nvrtx_space_params(params);
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_DEVICE, NVRTX_HANDLE_WORK_SPACE, NVRTX_CLASS_SPACE, params, sizeof(params));
	if (error != 0)
		return error;

	work->space_made = 1;

	/* The root. */
	nvrtx_pdir_params(params, root, NVRTX_HANDLE_WORK_SPACE);
	error = nvrtx_rm_control(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_DEVICE, NVRTX_CTL_SET_PAGE_DIRECTORY, params, NVRTX_PDIR_PARAMS_BYTES, &reply, &reply_length);
	if (error != 0)
		return error;

	work->root_set = 1;

	/* Drops any stale translation under the root. */
	error = nvrtx_flush_tlb(gpu, root);
	if (error != 0)
		return error;

	/* Succeeded: the space maps the range. */
	nvrtx_stage_mark("P9w ok: vaspace");
	return 0;
}

/*
 * Makes the graphics channel: its instance block naming the tables, the
 * channel on the graphics engine and the promote of its context buffers.
 */
static int
work_channel(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	struct nvrtx_channel_setup setup;
	uint8_t *params;
	uint8_t *instance;
	int error;

	work = &gpu->work;

	/* The instance block names the tables. */
	instance = kern_calloc(1U, NVRTX_INSTANCE_BYTES);
	if (instance == NULL)
		return ENOMEM;

	nvrtx_mmu_instance(instance, work->arena.vram + WORK_TABLES_AT, WORK_VA_LIMIT);
	error = nvrtx_vram_write(gpu, work->arena.vram + WORK_INSTANCE_AT, instance, NVRTX_INSTANCE_BYTES);
	kern_free(instance);
	if (error != 0)
		return error;

	/* The channel on the graphics engine, its ring at the driver's first page. */
	setup.ring_va = WORK_RING_VA;
	setup.ring_entries = NVRTX_WORK_RING_ENTRIES;
	setup.chid = WORK_CHID;
	setup.space = NVRTX_HANDLE_WORK_SPACE;
	setup.engine = NVRTX_ENGINE_GRAPHICS;
	setup.privileged = 0U;
	setup.instance = work->arena.vram + WORK_INSTANCE_AT;
	setup.userd = work->arena.vram + WORK_USERD_AT;
	setup.methods = work->methods.dma.device_address;
	setup.methods_size = gpu->engine.methods_size;

	params = kern_malloc(NVRTX_CHANNEL_PARAMS_BYTES);
	if (params == NULL)
		return ENOMEM;

	nvrtx_channel_params(params, &setup);
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_DEVICE, NVRTX_HANDLE_WORK_CHANNEL, gpu->identity.chip.profile->class_channel, params, NVRTX_CHANNEL_PARAMS_BYTES);
	kern_free(params);
	if (error != 0)
		return error;

	work->channel_made = 1;

	/* Hands it its context buffers: its own main and patch, the golden context's global ones. */
	error = nvrtx_gr_promote(gpu, NVRTX_HANDLE_WORK_CHANNEL, work->physical, false);
	if (error != 0)
		return error;

	/* Succeeded: the channel exists with its context. */
	return 0;
}

/* Makes the 3D and compute objects on the channel, binds it to the graphics engine and lets the scheduler run it. */
static int
work_objects(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	uint8_t params[NVRTX_BIND_PARAMS_BYTES + NVRTX_SCHEDULE_PARAMS_BYTES];
	const uint8_t *reply;
	size_t reply_length;
	int error;

	work = &gpu->work;

	/* The 3D object. */
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_WORK_CHANNEL, NVRTX_HANDLE_WORK_THREED, gpu->identity.chip.profile->class_threed, NULL, 0U);
	if (error != 0)
		return error;

	work->threed_made = 1;

	/* The compute object. */
	error = nvrtx_rm_allocate(gpu, NVRTX_HANDLE_WORK_CHANNEL, NVRTX_HANDLE_WORK_COMPUTE, gpu->identity.chip.profile->class_compute, NULL, 0U);
	if (error != 0)
		return error;

	work->compute_made = 1;

	/* Binds the channel to the graphics engine. */
	kern_memset(params, 0, sizeof(params));
	nvrtx_put32(params, 0U, NVRTX_ENGINE_GRAPHICS);
	error = nvrtx_rm_control(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_WORK_CHANNEL, NVRTX_CTL_BIND, params, NVRTX_BIND_PARAMS_BYTES, &reply, &reply_length);
	if (error != 0)
		return error;

	/* Lets the scheduler run it. */
	kern_memset(params, 0, sizeof(params));
	params[0] = 1U;
	error = nvrtx_rm_control(gpu, NVRTX_HANDLE_CLIENT, NVRTX_HANDLE_WORK_CHANNEL, NVRTX_CTL_SCHEDULE, params, NVRTX_SCHEDULE_PARAMS_BYTES, &reply, &reply_length);
	if (error != 0)
		return error;

	/* Succeeded: the stop takes the channel off the scheduler before it frees it. */
	work->scheduled = 1;
	return 0;
}

/*
 * Runs the channel's first submission: both classes on their subchannels,
 * then its fence.
 */
static int
work_first(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_work_state *work;
	uint8_t *push;
	uint64_t push_va;
	uint64_t start;
	uint64_t waited;
	size_t length;
	uint32_t fence;
	int error;

	work = &gpu->work;

	/*
	 * The objects' push buffer goes in the last fence slot, which the
	 * ring reaches only after NVRTX_WORK_RING_ENTRIES - 1 more entries,
	 * long after this submission's fence.
	 */
	push = (uint8_t *)work->fences.dma.address + (size_t)(NVRTX_WORK_RING_ENTRIES - 1U) * NVRTX_WORK_FENCE_BYTES;
	push_va = WORK_FENCES_VA + (uint64_t)(NVRTX_WORK_RING_ENTRIES - 1U) * NVRTX_WORK_FENCE_BYTES;
	length = nvrtx_work_push_objects(push, gpu->identity.chip.profile->class_threed, gpu->identity.chip.profile->class_compute);

	/* Opens the channel to submissions; the semaphore starts at 0. */
	kern_memset(work->semaphore.dma.address, 0, NVRTX_GSP_PAGE_SIZE);
	work->put = 0U;
	work->fence = 0U;
	kern_memset(work->entry_fence, 0, sizeof(work->entry_fence));
	work->ready = 1;

	/* Submits it and waits for its fence. */
	start = nvrtx_now_us();
	fence = 0U;
	error = nvrtx_work_submit(gpu, push_va, length, &fence);
	if (error == 0)
		error = nvrtx_work_wait(gpu, fence, WORK_FIRST_FENCE_US);
	if (error != 0) {
		work->ready = 0;
		nvrtx_stage_mark("P9w fail: the first fence %u did not come (%d, semaphore %08x)", (unsigned)fence, error, (unsigned)work_semaphore(gpu));
		return error;
	}

	/* Succeeded: the channel ran its first work. */
	waited = nvrtx_now_us() - start;
	nvrtx_stage_mark("P9w ok: fence %u after %u us", (unsigned)fence, (unsigned)waited);
	return 0;
}

/* Writes a run of page entries into the tables and drops the stale translations. */
static int
work_entries_write(
	struct nvrtx_gpu *gpu,
	uint64_t va,
	const uint8_t *entries,
	uint64_t pages)
{
	struct nvrtx_work_state *work;
	size_t offset;
	int error;

	work = &gpu->work;

	/* The run's first entry; the rest follow it (the tables lie in page order). */
	offset = nvrtx_mmu_gr_pte_offset(NVRTX_WORK_VA_BASE, NVRTX_WORK_VA_BYTES, va);
	if (offset == 0U)
		return EINVAL;

	/* Writes the entries. */
	error = nvrtx_vram_write(gpu, work->arena.vram + WORK_TABLES_AT + offset, entries, (size_t)pages * 8U);
	if (error != 0)
		return error;

	/* Drops what the GPU may have kept of the old entries. */
	error = nvrtx_flush_tlb(gpu, work->arena.vram + WORK_TABLES_AT);
	if (error != 0)
		return error;

	/* Succeeded: the run maps as written. */
	return 0;
}

/* Waits until a ring entry's earlier submission is done, so the entry and its fence slot may be written again. */
static int
work_ring_wait(
	struct nvrtx_gpu *gpu,
	uint32_t entry)
{
	uint32_t fence;
	int error;

	/* An entry not used yet is free. */
	fence = gpu->work.entry_fence[entry];
	if (fence == 0U)
		return 0;

	/* Waits for the fence that ended its submission. */
	error = nvrtx_work_wait(gpu, fence, WORK_ENTRY_WAIT_US);
	if (error != 0)
		return error;

	/* Succeeded: the entry is free. */
	return 0;
}

/* Writes one ring entry and moves the put in USERD past it. */
static int
work_ring_put(
	struct nvrtx_gpu *gpu,
	uint32_t entry,
	uint64_t va,
	size_t length)
{
	struct nvrtx_work_state *work;
	uint8_t put[4];
	uint32_t next;
	int error;

	work = &gpu->work;

	/* The entry, written before the put that hands it to the GPU. */
	nvrtx_ring_entry((uint8_t *)work->ring.dma.address + (size_t)entry * 8U, va, (uint32_t)(length / 4U));
	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	/* The ring's put in USERD, wrapping at the ring's end. */
	next = (entry + 1U) & (NVRTX_WORK_RING_ENTRIES - 1U);
	nvrtx_put32(put, 0U, next);
	error = nvrtx_vram_write(gpu, work->arena.vram + WORK_USERD_AT + WORK_USERD_RING_PUT, put, sizeof(put));
	if (error != 0)
		return error;

	/* The next submission starts after this entry. */
	work->put = next;

	/* Succeeded: the entry is the GPU's once the doorbell rings. */
	return 0;
}

/* Rings the channel's doorbell: the graphics runlist and the channel. */
static int
work_doorbell(
	struct nvrtx_gpu *gpu)
{
	/* The GPU fetches the entries up to the put. */
	nvrtx_wr32(gpu, WORK_REG_DOORBELL, (gpu->engine.pick.graphics_runlist << 16) | WORK_CHID);

	/* Succeeded: the doorbell was written. */
	return 0;
}

/* Reads the value the fences released last. */
static uint32_t
work_semaphore(
	const struct nvrtx_gpu *gpu)
{
	const volatile uint32_t *semaphore;

	/* The semaphore's first word in the coherent page. */
	semaphore = (const volatile uint32_t *)gpu->work.semaphore.dma.address;

	/* Succeeded: what the GPU last released. */
	return *semaphore;
}

/* Reads a 64-bit GPU timer a timed release wrote into the semaphore's page at a byte offset. */
static uint64_t
work_semaphore_timer(
	const struct nvrtx_gpu *gpu,
	size_t at)
{
	const volatile uint32_t *words;
	uint64_t low;
	uint64_t high;

	/* The timer's two words in the coherent page, low first. */
	words = (const volatile uint32_t *)((const uint8_t *)gpu->work.semaphore.dma.address + at);
	low = words[0];
	high = words[1];

	/* Succeeded: the timer in nanoseconds. */
	return (high << 32) | low;
}

/* Rounds a value up to a power-of-two boundary given as its log 2. */
static uint64_t
work_align(
	uint64_t value,
	uint32_t shift)
{
	uint64_t mask;

	/* The boundary's mask. */
	mask = (1ULL << shift) - 1U;

	/* Succeeded: the rounded value. */
	return (value + mask) & ~mask;
}
