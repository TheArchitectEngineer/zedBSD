/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the node gives the Vulkan executor (ws037-p007 stage D3): the
 * storage of its memory (a blob's VRAM, mapped in the work channel's
 * space), the executor's own GPU memory, and the running of its push
 * buffers on the work channel.
 *
 * The executor runs under the node's lock, and so do these, except
 * nvrtx_vk_node_blob_attach(), which the node calls from a blob's
 * creation and which takes the lock itself.  Every run is mapped once for
 * the work channel: the VA it gets lives as long as the run.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-node.h"
#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/* How long a push buffer may run before the executor calls the GPU lost. */
#define VK_NODE_RUN_US			2000000U

/* The executor's own runs are on this boundary (the page tables' big page). */
#define VK_NODE_RUN_ALIGN		0x10000ULL

/*
 * One run of the executor's own GPU memory: the VRAM and where the work
 * channel reads it.
 */
struct vk_node_run {
	struct nvrtx_bo bo;
	uint64_t va;
};

static void vk_node_backing_drop(struct nvrtx_node *node, struct nvrtx_backing *backing);

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
	cpu = (uint8_t *)(uintptr_t)nvrtx_bo_cpu(node->gpu, &backing->bo);

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

	/* The work channel no longer reads the run (every submission waited for its work). */
	error = nvrtx_work_unmap(session->node->gpu, va, held->bo.size);
	if (error != 0)
		nvrtx_stage_mark("D3: a memory's run at %llx stayed mapped (%d)", (unsigned long long)va, error);

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
	*cpu = (uint8_t *)(uintptr_t)nvrtx_bo_cpu(session->node->gpu, &held->bo);
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
 * Frees a run of the executor's own GPU memory; no submitted work reads it
 * (every submission waited for its work).
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

	/* Unmaps it; a run that stays mapped keeps its VRAM, so nothing else is mapped over it. */
	error = nvrtx_work_unmap(session->node->gpu, run->va, run->bo.size);
	if (error != 0) {
		nvrtx_stage_mark("D3: an executor run at %llx stayed mapped (%d)", (unsigned long long)run->va, error);
		kern_free(run);
		return;
	}

	/* The VRAM and the record. */
	nvrtx_bo_free(session->node->gpu, &run->bo);
	kern_free(run);
}

/*
 * Runs a push buffer on the work channel and waits until the GPU has done
 * it.  Returns 0, the submission's error, or EIO when the work did not
 * finish in time.
 */
int
nvrtx_vk_gpu_run(
	void *owner,
	uint64_t va,
	uint32_t bytes)
{
	struct nvrtx_session *session;
	uint32_t fence;
	int error;

	session = owner;

	/* Puts the push buffer on the ring with the driver's fence after it. */
	error = nvrtx_work_submit(session->node->gpu, va, bytes, &fence);
	if (error != 0)
		return error;

	/* Waits for the fence. */
	error = nvrtx_work_wait(session->node->gpu, fence, VK_NODE_RUN_US);
	if (error != 0) {
		nvrtx_stage_mark("D3 fail: the work of fence %u did not finish (%d)", (unsigned)fence, error);
		return EIO;
	}

	/* Succeeded: the GPU has done the work. */
	return 0;
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
