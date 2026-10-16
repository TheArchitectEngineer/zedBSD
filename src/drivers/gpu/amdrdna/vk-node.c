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
 * the session's address space.  And what binds the executor to the node:
 * the renderer's frame runner and timelines, an executor session for each
 * open of the device, the storage a blob made for a VkDeviceMemory gives
 * it, and stage A9, the executor's first draws on the GPU.
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
#include "drivers/gpu/amdrdna/vk/amdrdna-vk.h"

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

static int vk_node_execute(struct amdrdna_session *session, const void *wire, uint32_t bytes);
static uint64_t vk_node_timelines(struct amdrdna_session *session);

/*
 * Binds the Vulkan executor to the node before it is registered: the
 * device becomes a renderer whose frames the executor runs.
 */
void
amdrdna_vk_node_bind(
	struct amdrdna_node *node)
{
	/* The executor's frame runner and its record of its queues' timelines. */
	amdrdna_runtime_bind(node, vk_node_execute, vk_node_timelines);
}

/*
 * Opens the executor session of an open of the device when the device is
 * a renderer.  Returns 0 or ENOMEM.
 */
int
amdrdna_vk_node_open(
	struct amdrdna_session *session)
{
	struct amdrdna_vk_session *opened;
	int error;

	/* A device without an executor opens none. */
	if (session->node->renderer == 0)
		return 0;

	/* The executor session reads the open's blobs through it. */
	error = amdrdna_vk_session_open(session, &opened);
	if (error != 0)
		return error;

	/* Succeeded: the open has its executor session. */
	session->vk = opened;
	return 0;
}

/*
 * Closes the executor session of an open of the device, under the node's
 * lock, before the open's address space goes.  Returns 0, or EBUSY when
 * the executor keeps the session (a payload is still held): the open's
 * record and address space must then stay, since the executor's memory
 * still names them.
 */
int
amdrdna_vk_node_close(
	struct amdrdna_session *session)
{
	struct amdrdna_vk_session *closing;
	int error;

	/* An open without an executor session has nothing to close. */
	closing = session->vk;
	if (closing == NULL)
		return 0;

	/* Closes it with everything the executor holds. */
	mutex_lock(&session->node->lock);

	error = amdrdna_vk_session_close(&closing);

	mutex_unlock(&session->node->lock);

	/* A session the executor keeps keeps the open too. */
	if (error == EBUSY) {
		session->vk = closing;
		amdrdna_mark("gpu device: an executor session is kept (a payload is still held)");
		return EBUSY;
	}

	/* A destructor's error is only shown; the session is gone. */
	session->vk = NULL;
	if (error != 0)
		amdrdna_mark("gpu device: an executor session closed with %d", error);

	/* Succeeded: the executor session is gone. */
	return 0;
}

/*
 * Attaches a new blob's VRAM to the VkDeviceMemory its blob_id names: maps
 * it into the session's address space and gives the memory a reference to
 * the backing.  Called by the blob's creation before the blob is listed.
 *
 * Returns EINVAL when the session's executor has no such memory (the
 * mapping is then undone), or the mapping's error.
 */
int
amdrdna_vk_node_blob_attach(
	struct amdrdna_session *session,
	uint64_t blob_id,
	struct amdrdna_backing *backing,
	uint64_t bytes)
{
	struct amdrdna_node *node;
	uint8_t *cpu;
	uint64_t va;
	int error;

	/* The node of the session. */
	node = session->node;

	/* Without an executor session no memory takes the blob. */
	if (session->vk == NULL)
		return EINVAL;

	/* Maps the run into the session's address space. */
	error = amdrdna_space_map(session->space, backing, &va);
	if (error != 0)
		return error;

	/* The memory takes the storage under the node's lock, as everything the executor holds. */
	cpu = (uint8_t *)(uintptr_t)amdrdna_backing_cpu(node->device, backing);

	mutex_lock(&node->lock);

	error = amdrdna_vk_memory_attach(session->vk, blob_id, bytes, cpu, va, backing);

	mutex_unlock(&node->lock);

	/* A blob no memory takes keeps no mapping. */
	if (error != 0) {
		amdrdna_space_unmap(session->space, va, backing->bytes);
		return error;
	}

	/* The memory holds the backing until its detach. */
	amdrdna_backing_get(node->device, backing);

	/* Succeeded: the memory's buffers and images reach the blob's VRAM. */
	return 0;
}

/*
 * Runs stage A9: the executor's first draws on the GPU, in a session of
 * the driver's own: a quad of one colour, one of another inside it, and a
 * quad that samples the drawn image into a second one, every pixel of
 * both compared.  The executor must have been bound (not amdrdna.vk=0) before
 * stage A5s registered the device.
 *
 * Returns 0 when every pixel is as drawn, ENOTSUP without an executor,
 * EIO for a wrong pixel, or the error of the step that failed.
 */
int
amdrdna_a9_draw(
	struct amdrdna_device *device)
{
	struct amdrdna_node *node;
	struct amdrdna_session *session;
	struct amdrdna_vk_session *executor;
	struct amdrdna_vk_trial trial;
	int closed;
	int error;

	/* Says the stage starts. */
	amdrdna_mark("A9 begin");
	node = device->node;

	/* The device registered by stage A5s, a renderer. */
	if (node == NULL || node->gpu == NULL) {
		amdrdna_mark("A9 fail: no gpu device (stage A5s)");
		return ENODEV;
	}
	if (node->renderer == 0) {
		amdrdna_mark("A9 fail: no executor bound (the boot said amdrdna.vk=0)");
		return ENOTSUP;
	}

	/* A session of the driver's own, with its address space. */
	error = amdrdna_node_session_open(node, &session);
	if (error != 0) {
		amdrdna_mark("A9 fail: no session (%d)", error);
		return error;
	}

	/* The executor's session and its trial, under the node's lock as every executor call. */
	mutex_lock(&node->lock);

	error = amdrdna_vk_session_open(session, &executor);
	if (error == 0) {
		error = amdrdna_vk_execute_trial(executor, &trial);
		closed = amdrdna_vk_session_close(&executor);
		if (error == 0 && closed != 0)
			error = closed;
	}

	mutex_unlock(&node->lock);

	/* The session and its address space go (or are kept when the work did not end). */
	amdrdna_node_session_close(node, session);

	/* Reports a trial that did not run to its end. */
	if (error != 0) {
		amdrdna_mark("A9 fail: the executor's draws did not run (%d)", error);
		return error;
	}

	/* Reports a pixel the draws did not leave as expected. */
	if (trial.drawn != trial.pixels || trial.copied != trial.pixels) {
		amdrdna_mark("A9 fail: draw pixels=%u/%u copy=%u/%u first wrong at %u: 0x%08x",
		    (unsigned)trial.drawn, (unsigned)trial.pixels, (unsigned)trial.copied, (unsigned)trial.pixels,
		    (unsigned)trial.bad_at, (unsigned)trial.bad_value);
		return EIO;
	}

	/* Marks the result. */
	amdrdna_mark("A9 ok draw pixels=%u copy=%u quads=%u words=%u renderer bound",
	    (unsigned)trial.drawn, (unsigned)trial.copied, (unsigned)trial.quads, (unsigned)trial.words);

	/* Succeeded: the executor draws on the GPU. */
	return 0;
}

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

/* Runs one command frame of an open through its executor session (the node's lock is held). */
static int
vk_node_execute(
	struct amdrdna_session *session,
	const void *wire,
	uint32_t bytes)
{
	struct amdrdna_vk_session *executor;
	int error;

	/* The frame's commands, decoded and dispatched by the executor. */
	executor = session->vk;
	error = amdrdna_vk_stream_execute(executor, wire, bytes, amdrdna_vk_dispatch);
	if (error != 0) {
		amdrdna_mark("vk: a frame failed (%d): command %u refused, %u the last taken",
		    error, (unsigned)executor->failed_opcode, (unsigned)executor->last_opcode);
		return error;
	}

	/* Succeeded: the frame ran. */
	return 0;
}

/* Reports the timelines the queues of an open's executor complete jobs on. */
static uint64_t
vk_node_timelines(
	struct amdrdna_session *session)
{
	struct amdrdna_vk_session *executor;
	uint64_t timelines;

	/* An open without an executor session has no queue. */
	executor = session->vk;
	if (executor == NULL)
		return 0U;

	/* The executor records them as its queues are made. */
	timelines = __atomic_load_n(&executor->timelines, __ATOMIC_ACQUIRE);

	/* Succeeded: the timelines. */
	return timelines;
}
