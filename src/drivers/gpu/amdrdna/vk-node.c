/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the node gives the Vulkan executor: the storage of its memory (a
 * blob's VRAM, mapped in the session's address space), the executor's own
 * GPU memory, and the runs of its indirect buffers on the graphics ring in
 * the session's address space.
 *
 * owner is always the node's session.  The executor runs under the node's
 * lock, and so do these; an address space's lock is taken inside it.
 * Every run of memory is mapped once into the session's space: the
 * address it gets lives as long as the run.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-node.h"

/* How long an indirect buffer may run before the executor calls the session lost. */
#define VK_NODE_RUN_MS			2000U

/*
 * One run of the executor's own GPU memory: the backing and where the
 * session's address space maps it.
 */
struct vk_node_run {
	struct amdrdna_backing *backing;
	uint64_t va;
};

/*
 * Gives the CPU view of a blob of the executor's owner (a node session) by
 * its protocol number: the first byte and the length.
 */
int
amdrdna_vk_blob(
	void *owner,
	uint32_t resource_id,
	uint8_t **cpu,
	uint64_t *bytes)
{
	struct amdrdna_session *session;
	struct amdrdna_resource *resource;

	/* The node session the executor names. */
	session = owner;

	/* Finds the number among the session's blobs. */
	for (resource = session->blobs; resource != NULL; resource = resource->next_blob) {
		if (resource->resource_id == resource_id)
			break;
	}

	/* Refuses a number the session has no blob for. */
	if (resource == NULL)
		return ENOENT;

	/* The CPU reads what the GPU wrote, not what the host path held. */
	amdrdna_host_path_drop(session->node->device);

	/* Succeeded: the blob's bytes in the CPU's window and their count. */
	*cpu = (uint8_t *)(uintptr_t)amdrdna_backing_cpu(session->node->device, resource->backing);
	*bytes = resource->bytes;
	return 0;
}

/* Gives the executor's owner's GPU's PCI product. */
uint32_t
amdrdna_vk_device_id(
	void *owner)
{
	struct amdrdna_session *session;

	/* The node session the executor names. */
	session = owner;

	/* The product the attach read. */
	return session->node->device->product;
}

/*
 * Gives the bytes of VRAM the executor's memory may take: the mapped part
 * of the CPU's window, which every backing comes from.
 */
uint64_t
amdrdna_vk_heap_bytes(
	void *owner)
{
	struct amdrdna_session *session;

	/* The node session the executor names. */
	session = owner;

	/* The window's mapped part. */
	return session->node->device->window.size;
}

/*
 * Gives back the storage of a VkDeviceMemory once nothing holds it: its
 * mapping in the session's address space and the reference to the blob's
 * backing the memory took.  When the session's work may still run (a run
 * that never ended), the VRAM is kept for good.
 */
void
amdrdna_vk_memory_detach(
	void *owner,
	void *backing,
	uint64_t va,
	uint64_t bytes)
{
	struct amdrdna_session *session;
	struct amdrdna_backing *held;
	bool quiet;

	UNUSED_PARAMETER(bytes);

	/* The node session the executor names. */
	session = owner;
	held = backing;

	/* Work that may still read the run keeps its VRAM. */
	quiet = amdrdna_space_quiet(session->space);
	if (!quiet)
		amdrdna_backing_keep(session->node->device, held);

	/* The space no longer maps the run. */
	amdrdna_space_unmap(session->space, va, held->bytes);

	/* The memory's reference goes; the last one gives the VRAM back. */
	amdrdna_backing_put(session->node->device, held);
}

/*
 * Takes a blob of the session's as an imported VkDeviceMemory's storage:
 * maps it into the session's address space and holds its backing.
 *
 * Returns ENOENT for a resource the session has no blob for, EINVAL for
 * one smaller than the memory, or the mapping's error.
 */
int
amdrdna_vk_memory_import(
	void *owner,
	uint32_t resource_id,
	uint64_t bytes,
	uint8_t **cpu,
	uint64_t *va,
	void **backing)
{
	struct amdrdna_session *session;
	struct amdrdna_resource *resource;
	struct amdrdna_backing *held;
	int error;

	/* The node session the executor names. */
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
	if (held->bytes < bytes)
		return EINVAL;

	/* Maps the run into the session's address space. */
	error = amdrdna_space_map(session->space, held, va);
	if (error != 0)
		return error;

	/* The memory holds the backing while it lives. */
	amdrdna_backing_get(session->node->device, held);

	/* Succeeded: the memory reaches the blob's VRAM. */
	*cpu = (uint8_t *)(uintptr_t)amdrdna_backing_cpu(session->node->device, held);
	*backing = held;
	return 0;
}

/*
 * Allocates a run of the executor's own GPU memory: VRAM the CPU writes
 * through its window and the session's address space maps at *va.
 */
int
amdrdna_vk_gpu_alloc(
	void *owner,
	uint64_t bytes,
	void **token,
	uint8_t **cpu,
	uint64_t *va)
{
	struct amdrdna_session *session;
	struct vk_node_run *run;
	int error;

	/* The node session the executor names. */
	session = owner;

	/* The record. */
	run = kern_calloc(1U, sizeof(*run));
	if (run == NULL)
		return ENOMEM;

	/* The VRAM, cleared, in the CPU's window. */
	error = amdrdna_backing_create(session->node->device, bytes, &run->backing);
	if (error != 0) {
		kern_free(run);
		return error;
	}

	/* And in the session's address space. */
	error = amdrdna_space_map(session->space, run->backing, &run->va);
	if (error != 0) {
		amdrdna_backing_put(session->node->device, run->backing);
		kern_free(run);
		return error;
	}

	/* Succeeded: the run is the executor's. */
	*token = run;
	*cpu = (uint8_t *)(uintptr_t)amdrdna_backing_cpu(session->node->device, run->backing);
	*va = run->va;
	return 0;
}

/*
 * Frees a run of the executor's own GPU memory; no submitted work reads it
 * (every run waited for its work), unless the session's work never ended,
 * in which case the VRAM is kept for good.
 */
void
amdrdna_vk_gpu_free(
	void *owner,
	void *token)
{
	struct amdrdna_session *session;
	struct vk_node_run *run;
	bool quiet;

	/* The node session the executor names. */
	session = owner;
	run = token;

	/* Nothing to free. */
	if (run == NULL)
		return;

	/* Work that may still read the run keeps its VRAM. */
	quiet = amdrdna_space_quiet(session->space);
	if (!quiet)
		amdrdna_backing_keep(session->node->device, run->backing);

	/* Unmaps it, then the VRAM and the record. */
	amdrdna_space_unmap(session->space, run->va, run->backing->bytes);
	amdrdna_backing_put(session->node->device, run->backing);
	kern_free(run);
}

/*
 * Runs an indirect buffer of bytes at va on the graphics ring in the
 * session's address space and waits until the GPU has done it.  Returns 0,
 * EINVAL for a buffer that is not whole dwords, or EIO when the work did
 * not finish (the session is then lost) or the session is lost already.
 */
int
amdrdna_vk_gpu_run(
	void *owner,
	uint64_t va,
	uint32_t bytes)
{
	struct amdrdna_session *session;
	int error;

	/* The node session the executor names. */
	session = owner;

	/* Whole dwords. */
	if (bytes == 0U || (bytes & 3U) != 0U)
		return EINVAL;

	/* Runs the buffer in the session's space and waits for it. */
	error = amdrdna_space_run(session->space, AMDRDNA_TRACKED_GRAPHICS, va, bytes / 4U, VK_NODE_RUN_MS);
	if (error == EINVAL)
		return EINVAL;
	if (error != 0)
		return EIO;

	/* Succeeded: the GPU has done the work. */
	return 0;
}

/* Counts a transfer the CPU did through the window instead of the GPU. */
void
amdrdna_vk_gpu_copied(
	void *owner,
	uint64_t bytes)
{
	struct amdrdna_session *session;

	/* The node session the executor names. */
	session = owner;

	/* One copy more, and its bytes. */
	(void)__atomic_fetch_add(&session->node->copies, 1U, __ATOMIC_RELAXED);
	(void)__atomic_fetch_add(&session->node->copy_bytes, bytes, __ATOMIC_RELAXED);
}

/* Counts the draws of a command buffer about to run: the application's and the executor's own quads. */
void
amdrdna_vk_gpu_drawn(
	void *owner,
	uint32_t draws,
	uint32_t quads)
{
	struct amdrdna_session *session;

	/* The node session the executor names. */
	session = owner;

	/* The two kinds of draw. */
	(void)__atomic_fetch_add(&session->node->draws, draws, __ATOMIC_RELAXED);
	(void)__atomic_fetch_add(&session->node->quads, quads, __ATOMIC_RELAXED);
}

/* Copies a command stream out of a blob's bytes in the CPU's window into the executor's memory, counting it. */
void
amdrdna_vk_blob_read(
	void *owner,
	uint8_t *to,
	const uint8_t *from,
	size_t length)
{
	struct amdrdna_session *session;

	/* The node session the executor names. */
	session = owner;

	/* The copy; the window is read without the CPU's cache. */
	kern_memcpy(to, from, length);

	/* One stream more, and its bytes. */
	(void)__atomic_fetch_add(&session->node->streams, 1U, __ATOMIC_RELAXED);
	(void)__atomic_fetch_add(&session->node->stream_bytes, length, __ATOMIC_RELAXED);
}
