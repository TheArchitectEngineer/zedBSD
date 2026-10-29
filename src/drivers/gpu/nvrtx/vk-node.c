/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the node gives the Vulkan executor: the storage of its memory (a blob's
 * VRAM, mapped in the work channel's space), the executor's own GPU memory, and
 * the running of its push buffers on the work channel.
 *
 * The executor runs under the node's lock, and so do these, except
 * nvrtx_vk_node_blob_attach(), which the node calls from a blob's
 * creation and which takes the lock itself, and nvrtx_vk_node_run_wait(),
 * which the renderer's completer calls without it.  Every run is mapped
 * once for the work channel: the VA it gets lives as long as the run.
 *
 * A push buffer is put on the session's channel (its own, or the shared
 * work channel) without waiting for it: the session keeps its newest
 * run's fence, and whatever needs the GPU done
 * with the session's work first (a run of memory given back, a copy by the
 * CPU, vkQueueWaitIdle) waits for it.  A run may take VK_NODE_RUN_US from
 * its submission; one that takes longer loses the session's runs.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-node.h"
#include "drivers/gpu/nvrtx/vk/nvrtx-vk.h"

/* How long a push buffer may run before the executor calls the GPU lost. */
#define VK_NODE_RUN_US			2000000U

/*
 * How long before a run is expected to end (as the session's last run
 * took) a wait starts to look at the fence without sleeping.
 */
#define VK_NODE_SPIN_EARLY_US		1500U

/*
 * The executor's own runs are on this boundary (the page tables' big page).
 *
 * Value referenced: Linux v6.19
 * drivers/gpu/drm/nouveau/nvkm/subdev/mmu/vmmtu102.c (tu102_vmm, the
 * 16-bit page).
 */
#define VK_NODE_RUN_ALIGN		0x10000ULL

/*
 * The 3D engine's pending interrupt, trap and GPC exception registers, read
 * after a run to tell a run the engine trapped in without stopping the
 * channel; the first few such runs are told.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/nouveau/nvkm/engine/gr/gf100.c (gf100_gr_intr 0x400100,
 * gf100_gr_trap_intr 0x400108 and 0x400118).
 */
#define VK_NODE_GR_INTR			0x400100U
#define VK_NODE_GR_TRAP			0x400108U
#define VK_NODE_GR_GPC_EXCEPTION	0x400118U
#define VK_NODE_GR_TOLD_MAX		4U

/*
 * One run of the executor's own GPU memory: the VRAM, or the coherent
 * system memory when system is set, and where the work channel reads it.
 */
struct vk_node_run {
	struct nvrtx_bo bo;
	struct nvrtx_buffer pages;
	bool system;
	uint64_t va;
};

static uint8_t *vk_node_backing_view(struct nvrtx_node *node, const struct nvrtx_backing *backing);
static void vk_node_backing_drop(struct nvrtx_node *node, struct nvrtx_backing *backing);
static int vk_node_wait(struct nvrtx_session *session, uint32_t fence, uint64_t submitted_us, uint64_t *waited_us);
static bool vk_node_passed(struct nvrtx_session *session, uint32_t fence);
static void vk_node_gr_check(struct nvrtx_node *node);

/*
 * Attaches a new blob's VRAM to the VkDeviceMemory its blob_id names: maps
 * it for the work channel and gives the memory a reference to the backing.
 * Called by the blob's creation before the blob is published.
 *
 * Returns EINVAL when the session's executor has no such memory (the
 * mapping is then undone), ENODEV without the work channel.
 */
int
nvrtx_vk_node_blob_attach(
	struct nvrtx_node *node,
	struct nvrtx_session *session,
	uint64_t blob_id,
	struct nvrtx_backing *backing,
	uint64_t bytes)
{
	uint8_t *cpu;
	uint64_t va;
	int error;

	/* Maps the run for the work channel. */
	error = nvrtx_work_map_vram(node->gpu, backing->bo.vram, backing->bo.size, &va);
	if (error != 0)
		return error;

	/* The memory takes its reference under the node's lock, as everything the executor holds. */
	cpu = vk_node_backing_view(node, backing);

	mutex_lock(&node->lock);

	error = nvrtx_vk_memory_attach(session->vk, blob_id, bytes, cpu, va, backing);
	if (error == 0)
		backing->refs++;

	mutex_unlock(&node->lock);

	/* A blob no memory takes keeps no mapping. */
	if (error != 0) {
		(void)nvrtx_work_unmap(node->gpu, va, backing->bo.size);
		return error;
	}

	/* Succeeded: the memory's buffers and images reach the blob's VRAM. */
	return 0;
}

/*
 * Gives back a VkDeviceMemory's storage: unmaps its run from the work
 * channel and lets the memory's reference to the backing go.  The caller
 * (the executor) holds the node's lock.
 */
void
nvrtx_vk_memory_detach(
	void *owner,
	void *backing,
	uint64_t va,
	uint64_t bytes)
{
	struct nvrtx_session *session;
	struct nvrtx_backing *held;
	int error;

	UNUSED_PARAMETER(bytes);

	session = owner;
	held = backing;

	/* Runs that may read it keep the mapping and the reference for good when they do not end. */
	error = nvrtx_vk_gpu_idle(owner);
	if (error != 0) {
		nvrtx_stage_mark("executor: a memory's run at %llx stays, its work did not end", (unsigned long long)va);
		return;
	}

	/* The work channel no longer reads the run (the session's runs have ended). */
	error = nvrtx_work_unmap(session->node->gpu, va, held->bo.size);
	if (error != 0)
		nvrtx_stage_mark("executor: a memory's run at %llx stayed mapped (%d)", (unsigned long long)va, error);

	/* The memory's reference goes; the last one gives the VRAM back. */
	vk_node_backing_drop(session->node, held);
}

/*
 * Takes a blob of the session's as an imported VkDeviceMemory's storage:
 * maps it for the work channel and holds its backing.  The caller holds
 * the node's lock.
 *
 * Returns ENOENT for a resource the session has no blob for, EINVAL for
 * one smaller than the memory.
 */
int
nvrtx_vk_memory_import(
	void *owner,
	uint32_t resource_id,
	uint64_t bytes,
	uint8_t **cpu,
	uint64_t *va,
	void **backing)
{
	struct nvrtx_session *session;
	struct nvrtx_resource *resource;
	struct nvrtx_backing *held;
	int error;

	session = owner;

	/* Finds the number among the session's blobs. */
	for (resource = session->blobs; resource != NULL; resource = resource->next_blob) {
		if (resource->resource_id == resource_id)
			break;
	}

	/* A blob of the session's that holds the whole memory. */
	if (resource == NULL)
		return ENOENT;
	held = resource->backing;
	if (held->bo.size < bytes)
		return EINVAL;

	/* Maps the run for the work channel. */
	error = nvrtx_work_map_vram(session->node->gpu, held->bo.vram, held->bo.size, va);
	if (error != 0)
		return error;

	/* The memory holds the backing while it lives. */
	held->refs++;
	*cpu = vk_node_backing_view(session->node, held);
	*backing = held;

	/* Succeeded: the memory reaches the blob's VRAM. */
	return 0;
}

/*
 * Allocates a run of the executor's own GPU memory: VRAM the CPU writes
 * through its window and the work channel reads at *va.
 */
int
nvrtx_vk_gpu_alloc(
	void *owner,
	uint64_t bytes,
	void **token,
	uint8_t **cpu,
	uint64_t *va)
{
	struct nvrtx_session *session;
	struct vk_node_run *run;
	int error;

	session = owner;

	/* The record. */
	run = kern_calloc(1U, sizeof(*run));
	if (run == NULL)
		return ENOMEM;

	/* The VRAM, in the CPU's window. */
	error = nvrtx_bo_alloc(session->node->gpu, bytes, VK_NODE_RUN_ALIGN, &run->bo);
	if (error == 0)
		error = nvrtx_bo_map(session->node->gpu, &run->bo);
	if (error != 0) {
		nvrtx_bo_free(session->node->gpu, &run->bo);
		kern_free(run);
		return error;
	}

	/* And in the work channel's space. */
	error = nvrtx_work_map_vram(session->node->gpu, run->bo.vram, run->bo.size, &run->va);
	if (error != 0) {
		nvrtx_bo_free(session->node->gpu, &run->bo);
		kern_free(run);
		return error;
	}

	/* Succeeded: the run is the executor's. */
	*token = run;
	*cpu = (uint8_t *)(uintptr_t)nvrtx_bo_cpu(session->node->gpu, &run->bo);
	*va = run->va;
	return 0;
}

/*
 * Allocates a run of the executor's own GPU memory in coherent system
 * memory: the CPU writes it through its cache, without the window's
 * uncached writes, and the work channel reads it at *va (the push
 * buffers, constants and tables the CPU writes for every submission).
 */
int
nvrtx_vk_gpu_alloc_system(
	void *owner,
	uint64_t bytes,
	void **token,
	uint8_t **cpu,
	uint64_t *va)
{
	struct nvrtx_session *session;
	struct vk_node_run *run;
	uint64_t *pages;
	size_t count;
	size_t index;
	int error;

	session = owner;
	count = (size_t)((bytes + NVRTX_GSP_PAGE_SIZE - 1U) / NVRTX_GSP_PAGE_SIZE);

	/* The record and the list of its pages. */
	run = kern_calloc(1U, sizeof(*run));
	if (run == NULL)
		return ENOMEM;
	pages = kern_malloc(count * sizeof(*pages));
	if (pages == NULL) {
		kern_free(run);
		return ENOMEM;
	}

	/* The memory, page by page in the work channel's space. */
	error = nvrtx_buffer_alloc(session->node->gpu, count * NVRTX_GSP_PAGE_SIZE, &run->pages);
	if (error == 0) {
		for (index = 0U; index < count; index++)
			pages[index] = run->pages.dma.device_address + (uint64_t)index * NVRTX_GSP_PAGE_SIZE;
		error = nvrtx_work_map_system(session->node->gpu, pages, count, &run->va);
		if (error != 0)
			nvrtx_buffer_free(session->node->gpu, &run->pages);
	}
	kern_free(pages);
	if (error != 0) {
		kern_free(run);
		return error;
	}

	/* Succeeded: the run is the executor's. */
	run->system = true;
	*token = run;
	*cpu = (uint8_t *)run->pages.dma.address;
	*va = run->va;
	return 0;
}

/*
 * Frees a run of the executor's own GPU memory once the session's runs,
 * which may read it, have ended; when they do not end, it is kept for good.
 */
void
nvrtx_vk_gpu_free(
	void *owner,
	void *token)
{
	struct nvrtx_session *session;
	struct vk_node_run *run;
	int error;

	session = owner;
	run = token;

	/* Nothing to free. */
	if (run == NULL)
		return;

	/* Runs that may read it keep it when they do not end. */
	error = nvrtx_vk_gpu_idle(owner);
	if (error != 0) {
		nvrtx_stage_mark("executor: an executor run at %llx stays, its work did not end", (unsigned long long)run->va);
		return;
	}

	/* Unmaps it; a run that stays mapped keeps its memory, so nothing else is mapped over it. */
	if (run->system) {
		error = nvrtx_work_unmap(session->node->gpu, run->va, run->pages.dma.size);
	} else {
		error = nvrtx_work_unmap(session->node->gpu, run->va, run->bo.size);
	}
	if (error != 0) {
		nvrtx_stage_mark("executor: an executor run at %llx stayed mapped (%d)", (unsigned long long)run->va, error);
		kern_free(run);
		return;
	}

	/* The memory and the record. */
	if (run->system) {
		nvrtx_buffer_free(session->node->gpu, &run->pages);
	} else {
		nvrtx_bo_free(session->node->gpu, &run->bo);
	}
	kern_free(run);
}

/*
 * Puts a push buffer on the session's channel with the driver's fence after
 * it, without waiting for it; the session's newest run is this one from now
 * on.  Returns 0, EIO once the session's runs are lost, ENODEV for a
 * session without its channel, or the submission's error.
 */
int
nvrtx_vk_gpu_submit(
	void *owner,
	uint64_t va,
	uint32_t bytes,
	struct nvrtx_vk_ticket *ticket)
{
	struct nvrtx_session *session;
	struct nvrtx_node *node;
	uint64_t submitted;
	uint32_t fence;
	int error;

	session = owner;
	node = session->node;
	ticket->fence = 0U;
	ticket->submitted_us = 0U;

	/* A session whose run did not end puts nothing more on the GPU. */
	if (__atomic_load_n(&session->run_lost, __ATOMIC_ACQUIRE) != 0)
		return EIO;

	/* The renderer gives the session its channel before its first frame runs. */
	if (session->channel == NULL)
		return ENODEV;

	/* Puts the push buffer on the session's channel's ring with the driver's fence after it. */
	submitted = nvrtx_now_us();
	error = nvrtx_work_channel_submit(node->gpu, session->channel, va, bytes, &fence);
	if (error != 0) {
		nvrtx_stage_mark("executor fail: a push buffer was not submitted (%d)", error);
		return error;
	}

	/* Counts the run and its push buffer's bytes. */
	nvrtx_node_count(node, &node->counts.runs, 1U);
	nvrtx_node_count(node, &node->counts.push_bytes, bytes);

	/* Succeeded: the run is the session's newest. */
	session->run_fence = fence;
	session->run_us = submitted;
	ticket->fence = fence;
	ticket->submitted_us = submitted;
	return 0;
}

/*
 * Waits for a run of the session's to end, up to its time from the
 * submission; the wait under the node's lock is counted.  Returns 0, or
 * EIO when it did not end in time or the session's runs are lost.
 */
int
nvrtx_vk_gpu_wait(
	void *owner,
	const struct nvrtx_vk_ticket *ticket)
{
	struct nvrtx_session *session;
	struct nvrtx_node *node;
	uint64_t waited;
	bool passed;
	int error;

	session = owner;
	node = session->node;

	/* A run that ended is no wait. */
	passed = vk_node_passed(session, ticket->fence);
	if (passed)
		return 0;

	/* Waits, and counts the wait. */
	error = vk_node_wait(session, ticket->fence, ticket->submitted_us, &waited);
	nvrtx_node_count(node, &node->counts.blocked, 1U);
	nvrtx_node_count(node, &node->counts.blocked_us, waited);
	if (error != 0)
		return error;

	/* Succeeded: the GPU has done the run. */
	return 0;
}

/*
 * Waits without the node's lock (the renderer's completer) for a run of
 * the session's to end, up to its time from the submission.  Returns 0, or
 * EIO when it did not end in time or the session's runs are lost.
 */
int
nvrtx_vk_node_run_wait(
	struct nvrtx_session *session,
	uint32_t fence,
	uint64_t submitted_us)
{
	uint64_t waited;
	bool passed;
	int error;

	/* A run that ended is no wait. */
	passed = vk_node_passed(session, fence);
	if (passed)
		return 0;

	/* Waits. */
	error = vk_node_wait(session, fence, submitted_us, &waited);
	if (error != 0)
		return error;

	/* Succeeded: the GPU has done the run. */
	return 0;
}

/* Reports, without the node's lock (the renderer), whether a run of the session's has ended. */
bool
nvrtx_vk_node_run_passed(
	struct nvrtx_session *session,
	uint32_t fence)
{
	bool passed;

	/* The session's channel's semaphore at or past the fence. */
	passed = vk_node_passed(session, fence);

	/* Succeeded: whether it ended. */
	return passed;
}

/* Reports whether a run of the session's has ended. */
bool
nvrtx_vk_gpu_passed(
	void *owner,
	const struct nvrtx_vk_ticket *ticket)
{
	struct nvrtx_session *session;
	bool passed;

	session = owner;

	/* The channel's semaphore at or past the run's fence. */
	passed = vk_node_passed(session, ticket->fence);

	/* Succeeded: whether it ended. */
	return passed;
}

/*
 * Counts an ended run's own time on the GPU when the channel times its runs
 * and tells a trap the 3D engine holds, then forgets the run.
 */
void
nvrtx_vk_gpu_retire(
	void *owner,
	struct nvrtx_vk_ticket *ticket)
{
	struct nvrtx_session *session;
	struct nvrtx_node *node;
	uint64_t gpu_ns;
	bool passed;
	int timed;

	session = owner;
	node = session->node;

	/* No run, or one that has not ended. */
	if (ticket->fence == 0U)
		return;
	passed = vk_node_passed(session, ticket->fence);
	if (!passed)
		return;

	/* The GPU's own time for the run. */
	if (node->perf != 0) {
		timed = nvrtx_work_channel_run_time(session->channel, ticket->fence, &gpu_ns);
		if (timed == 0) {
			nvrtx_node_count(node, &node->counts.timed, 1U);
			nvrtx_node_count(node, &node->counts.gpu_us, gpu_ns / 1000U);
			nvrtx_node_count_max(node, &node->counts.gpu_max_us, gpu_ns / 1000U);
		}
	}

	/* A run the engine trapped in is told, the first few times. */
	vk_node_gr_check(node);

	/* Succeeded: the run is forgotten. */
	ticket->fence = 0U;
	ticket->submitted_us = 0U;
}

/* Gives the session's newest run (no run before its first submission). */
void
nvrtx_vk_gpu_last(
	void *owner,
	struct nvrtx_vk_ticket *ticket)
{
	struct nvrtx_session *session;

	session = owner;

	/* The fence and the time the last submission left. */
	ticket->fence = session->run_fence;
	ticket->submitted_us = session->run_us;
}

/*
 * Waits until the session's runs have ended (its newest one).  Returns 0,
 * or EIO when it did not end in time or the session's runs are lost.
 */
int
nvrtx_vk_gpu_idle(
	void *owner)
{
	struct nvrtx_vk_ticket last;
	int error;

	/* The newest run. */
	nvrtx_vk_gpu_last(owner, &last);
	error = nvrtx_vk_gpu_wait(owner, &last);
	if (error != 0)
		return error;

	/* Succeeded: the GPU has done the session's work. */
	return 0;
}

/*
 * Waits for a run's fence until VK_NODE_RUN_US after its submission and
 * counts the run's time from its submission to its end; a run that did not
 * end loses the session's runs.  *waited_us is the wait's own time.
 * Returns 0, or EIO.
 */
static int
vk_node_wait(
	struct nvrtx_session *session,
	uint32_t fence,
	uint64_t submitted_us,
	uint64_t *waited_us)
{
	struct nvrtx_node *node;
	uint64_t start;
	uint64_t age;
	uint64_t spent;
	uint64_t took;
	uint32_t left;
	uint32_t quiet;
	int error;

	node = session->node;
	start = nvrtx_now_us();
	*waited_us = 0U;

	/* The session's runs are lost already: nothing is waited for again. */
	if (__atomic_load_n(&session->run_lost, __ATOMIC_ACQUIRE) != 0)
		return EIO;

	/* What is left of the run's time. */
	left = 0U;
	age = start - submitted_us;
	if (age < VK_NODE_RUN_US)
		left = (uint32_t)(VK_NODE_RUN_US - age);

	/* The run is expected to take as long as the session's last one: the wait sleeps until shortly before. */
	quiet = 0U;
	took = __atomic_load_n(&session->run_took_us, __ATOMIC_RELAXED);
	if (took > age + VK_NODE_SPIN_EARLY_US && took - age - VK_NODE_SPIN_EARLY_US < left)
		quiet = (uint32_t)(took - age - VK_NODE_SPIN_EARLY_US);

	/* Waits for the fence on the session's channel. */
	error = nvrtx_work_channel_wait_late(session->channel, fence, left, quiet);
	spent = nvrtx_now_us();
	*waited_us = spent - start;
	if (error != 0) {
		__atomic_store_n(&session->run_lost, 1, __ATOMIC_RELEASE);
		nvrtx_stage_mark("executor fail: the work of fence %u did not finish (%d); the session's runs are lost", (unsigned)fence, error);
		return EIO;
	}

	/* The next wait expects as long; counts the run's time from its submission to the end the wait saw. */
	__atomic_store_n(&session->run_took_us, spent - submitted_us, __ATOMIC_RELAXED);
	nvrtx_node_count(node, &node->counts.ended, 1U);
	nvrtx_node_count(node, &node->counts.run_us, spent - submitted_us);
	nvrtx_node_count_max(node, &node->counts.run_max_us, spent - submitted_us);

	/* Succeeded: the run ended. */
	return 0;
}

/*
 * Reports whether a run of the session's has ended: no run (fence 0, or a
 * session that has no channel yet: it never submitted), or its channel's
 * semaphore at or past the fence.
 */
static bool
vk_node_passed(
	struct nvrtx_session *session,
	uint32_t fence)
{
	struct nvrtx_work_channel *channel;
	bool passed;

	/* No run at all. */
	channel = session->channel;
	if (fence == 0U || channel == NULL)
		return true;

	/* The channel's semaphore. */
	passed = nvrtx_work_channel_passed(channel, fence);

	/* Succeeded: whether it ended. */
	return passed;
}

/* Tells a trap or exception the 3D engine holds after a run, the first few times. */
static void
vk_node_gr_check(
	struct nvrtx_node *node)
{
	static uint32_t told;
	uint32_t intr;
	uint32_t trap;
	uint32_t gpc;

	if (told >= VK_NODE_GR_TOLD_MAX)
		return;
	intr = nvrtx_rd32(node->gpu, VK_NODE_GR_INTR);
	trap = nvrtx_rd32(node->gpu, VK_NODE_GR_TRAP);
	gpc = nvrtx_rd32(node->gpu, VK_NODE_GR_GPC_EXCEPTION);
	if (intr == 0U && trap == 0U && gpc == 0U)
		return;
	told++;
	nvrtx_stage_mark("vk: gr after a run: intr %08x trap %08x gpc %08x", (unsigned)intr, (unsigned)trap, (unsigned)gpc);
}

/*
 * Counts the draws a push buffer holds before it runs, for the node's
 * counts (nvrtx.vk.perf=1): the application's draws and the executor's
 * own quads.  The caller holds the node's lock.
 */
void
nvrtx_vk_gpu_drawn(
	void *owner,
	uint32_t draws,
	uint32_t quads)
{
	struct nvrtx_session *session;

	session = owner;

	/* The two kinds of draw. */
	nvrtx_node_count(session->node, &session->node->counts.draws, draws);
	nvrtx_node_count(session->node, &session->node->counts.quads, quads);
}

/*
 * Copies a command stream out of a blob's bytes in the CPU's window into
 * the executor's memory, counting the copy and its time for the node's
 * counts (nvrtx.vk.perf=1).  The caller holds the node's lock.
 */
void
nvrtx_vk_blob_read(
	void *owner,
	uint8_t *to,
	const uint8_t *from,
	size_t length)
{
	struct nvrtx_session *session;
	struct nvrtx_node *node;
	uint64_t start;
	uint64_t spent;

	session = owner;
	node = session->node;

	/* The copy, timed: the window is read without the CPU's cache. */
	start = 0U;
	if (node->perf != 0)
		start = nvrtx_now_us();
	kern_memcpy(to, from, length);

	/* Counts the copy, its bytes and its time. */
	if (node->perf != 0) {
		spent = nvrtx_now_us() - start;
		nvrtx_node_count(node, &node->counts.streams, 1U);
		nvrtx_node_count(node, &node->counts.stream_bytes, length);
		nvrtx_node_count(node, &node->counts.stream_us, spent);
	}
}

/*
 * Counts a transfer the CPU did through the window, for the node's counts
 * (nvrtx.vk.perf=1).  The caller holds the node's lock.
 */
void
nvrtx_vk_gpu_copied(
	void *owner,
	uint64_t bytes)
{
	struct nvrtx_session *session;

	session = owner;

	/* One copy more, and its bytes. */
	nvrtx_node_count(session->node, &session->node->counts.copies, 1U);
	nvrtx_node_count(session->node, &session->node->counts.copy_bytes, bytes);
}

/*
 * Gives the CPU's view of a memory's storage for the executor, mapping it
 * into the window the first time the executor's CPU reaches it (a copy the
 * GPU cannot draw).  The caller holds the node's lock.
 *
 * Returns ENODEV without a working window, or ENOSPC when the window is full.
 */
int
nvrtx_vk_memory_cpu(
	void *owner,
	void *backing,
	uint8_t **cpu)
{
	struct nvrtx_session *session;
	volatile uint8_t *view;
	int error;

	session = owner;

	/* The backing's place in the window. */
	error = nvrtx_node_backing_cpu(session->node, backing, &view);
	if (error != 0)
		return error;

	/* Succeeded: the storage's first byte. */
	*cpu = (uint8_t *)(uintptr_t)view;
	return 0;
}

/*
 * Reports where the CPU reaches a backing in the window, or NULL while it has
 * no place there yet (nvrtx_vk_memory_cpu() gives it one).
 */
static uint8_t *
vk_node_backing_view(
	struct nvrtx_node *node,
	const struct nvrtx_backing *backing)
{
	/* Not yet in the window. */
	if (backing->bo.bar1_mapped == 0)
		return NULL;

	/* Succeeded: its first byte there. */
	return (uint8_t *)(uintptr_t)nvrtx_bo_cpu(node->gpu, &backing->bo);
}

/*
 * Lets one reference to a backing go with the node's lock held; the last
 * one gives the VRAM back unless the display may still read it.
 */
static void
vk_node_backing_drop(
	struct nvrtx_node *node,
	struct nvrtx_backing *backing)
{
	/* Others still name the VRAM. */
	backing->refs--;
	if (backing->refs != 0U)
		return;

	/* The VRAM goes back (the memory lock is taken after the node's), unless the display may still read it. */
	if (backing->kept == 0)
		nvrtx_bo_free(node->gpu, &backing->bo);

	/* The record. */
	kern_free(backing);
}
