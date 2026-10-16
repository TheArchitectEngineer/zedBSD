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
 *
 * With amdrdna.vk.timing=N on the boot line, the executor's work is timed
 * and marked by the first frame after each N milliseconds: the frames,
 * the commands by opcode, the waits for the GPU, the command streams read
 * through the window, the memory made, and the copies moved.  The runs
 * also write the GPU's timestamp counter at their start and end, so the
 * time the GPU spent on them is told apart from the wait.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-node.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk.h"

/* How long an indirect buffer may run before the executor calls the session lost. */
#define VK_NODE_RUN_MS			2000U

/* The opcodes the timing keeps apart; a larger one is counted with the last. */
#define VK_NODE_TIMED_OPCODES		256U

/* The opcodes a mark names, the longest first. */
#define VK_NODE_TIMED_SHOWN		6U

/* How far apart (microseconds) the stamped runs must be for the GPU counter's rate. */
#define VK_NODE_RATE_MIN_US		100000U

/*
 * The kinds of timed work, each a tally: the frames, the waits for the
 * GPU, the command streams read through the window, the memory made, the
 * copies moved; then one per opcode of the commands.
 */
#define VK_NODE_FRAMES			0U
#define VK_NODE_RUNS			1U
#define VK_NODE_STREAMS			2U
#define VK_NODE_MEMORY			3U
#define VK_NODE_MOVES			4U
#define VK_NODE_OPCODE_FIRST		5U
#define VK_NODE_TALLIES			(VK_NODE_OPCODE_FIRST + VK_NODE_TIMED_OPCODES)

/*
 * One run of the executor's own GPU memory: the backing and where the
 * session's address space maps it.
 */
struct vk_node_run {
	struct amdrdna_backing *backing;
	uint64_t va;
};

/* One kind of timed work: how many, their bytes, their microseconds and the longest. */
struct vk_node_tally {
	uint64_t count;
	uint64_t bytes;
	uint64_t us;
	uint64_t most_us;
};

/*
 * The GPU's own time on the runs of a period, in ticks of its timestamp
 * counter: how many runs were stamped, their ticks and the longest; and
 * the counter at the start of the period's first and last stamped run
 * with the CPU's microseconds when each was submitted, which give the
 * counter's rate.
 */
struct vk_node_busy {
	uint64_t runs;
	uint64_t ticks;
	uint64_t most_ticks;
	uint64_t first_stamp;
	uint64_t first_us;
	uint64_t last_stamp;
	uint64_t last_us;
};

/*
 * The executor's timing: the period between marks and when the current
 * one began (microseconds), and the tallies and the GPU's time of the
 * period; lock guards the tallies and the GPU's time (the boot's own
 * trials time outside the node's lock).
 */
struct amdrdna_vk_timing {
	struct spinlock lock;
	uint64_t period_us;
	uint64_t started_us;
	struct vk_node_tally tallies[VK_NODE_TALLIES];
	struct vk_node_busy busy;
};

static int vk_node_execute(struct amdrdna_session *session, const void *wire, uint32_t bytes);
static uint64_t vk_node_timelines(struct amdrdna_session *session);
static int vk_node_dispatch(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static void vk_node_count(struct amdrdna_node *node, uint32_t kind, uint64_t started_us, uint64_t bytes);
static void vk_node_count_busy(struct amdrdna_session *session, uint64_t submitted_us);
static void vk_node_report(struct amdrdna_node *node);
static void vk_node_report_busy(const struct vk_node_busy *busy, uint64_t run_bytes);
static void vk_node_report_clocks(struct amdrdna_node *node);
static void vk_node_report_opcodes(const struct vk_node_tally *tallies);
static void vk_node_tally_add(struct vk_node_tally *tally, uint64_t us, uint64_t bytes);

/*
 * Binds the Vulkan executor to the node before it is registered: the
 * device becomes a renderer whose frames the executor runs.
 */
void
amdrdna_vk_node_bind(
	struct amdrdna_node *node)
{
	struct amdrdna_vk_timing *timing;
	unsigned period_ms;

	/* The executor's frame runner and its record of its queues' timelines. */
	amdrdna_runtime_bind(node, vk_node_execute, vk_node_timelines);

	/* Timed only when the boot line asks for it. */
	period_ms = amdrdna_parameter_number("amdrdna.vk.timing", 0U);
	if (period_ms == 0U)
		return;

	/* The timing, kept for the node's lifetime. */
	timing = kern_calloc(1U, sizeof(*timing));
	if (timing == NULL) {
		amdrdna_mark("vk timing: not timed (no memory)");
		return;
	}
	spin_init(&timing->lock, LOCK_RANK_DEVICE, "amdrdna vk timing");
	timing->period_us = (uint64_t)period_ms * 1000U;
	timing->started_us = amdrdna_display_now_us();

	/* Published before the device is registered, so before any frame. */
	node->vk_timing = timing;
	amdrdna_mark("vk timing: marks every %u ms (at the next frame)", period_ms);

	/* The graphics ring's runs note the GPU's counter from now on. */
	amdrdna_spaces_stamp(node->device, true);
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

	/* Refuses a number the session has no blob for, or a blob the CPU's window does not hold. */
	if (resource == NULL)
		return ENOENT;
	if (!resource->backing->cpu)
		return EINVAL;

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
 * of the CPU's window (cpu true), or the VRAM only the GPU reaches.
 */
uint64_t
amdrdna_vk_heap_bytes(
	void *owner,
	bool cpu)
{
	struct amdrdna_device *device;
	struct amdrdna_session *session;

	/* The node session the executor names. */
	session = owner;
	device = session->node->device;

	/* The window's mapped part, or the pool outside it. */
	if (cpu)
		return device->window.size;
	return device->gpu_pool.end - device->gpu_pool.start;
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
 * Gives a VkDeviceMemory of its own storage: zeroed VRAM outside the CPU's
 * window, mapped into the session's address space at *va; the backing
 * goes back with amdrdna_vk_memory_detach().
 *
 * Returns 0, ENOMEM, or the clear's or the mapping's error.
 */
int
amdrdna_vk_memory_create(
	void *owner,
	uint64_t bytes,
	uint64_t *va,
	void **backing)
{
	struct amdrdna_session *session;
	struct amdrdna_backing *made;
	uint64_t started_us;
	int error;

	/* The node session the executor names, and when the making began. */
	session = owner;
	started_us = amdrdna_display_now_us();

	/* The VRAM, cleared, outside the window. */
	error = amdrdna_backing_create(session->node->device, bytes, false, &made);
	if (error != 0)
		return error;

	/* And in the session's address space. */
	error = amdrdna_space_map(session->space, made, va);
	if (error != 0) {
		amdrdna_backing_put(session->node->device, made);
		return error;
	}

	/* Timed with its bytes. */
	vk_node_count(session->node, VK_NODE_MEMORY, started_us, bytes);

	/* Succeeded: the memory holds the backing's one reference. */
	*backing = made;
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
	uint64_t started_us;
	int error;

	/* The node session the executor names, and when the making began. */
	session = owner;
	started_us = amdrdna_display_now_us();

	/* The record. */
	run = kern_calloc(1U, sizeof(*run));
	if (run == NULL)
		return ENOMEM;

	/* The VRAM, cleared, in the CPU's window. */
	error = amdrdna_backing_create(session->node->device, bytes, true, &run->backing);
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

	/* Timed with its bytes. */
	vk_node_count(session->node, VK_NODE_MEMORY, started_us, bytes);

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
	uint64_t started_us;
	int error;

	/* The node session the executor names. */
	session = owner;

	/* Whole dwords. */
	if (bytes == 0U || (bytes & 3U) != 0U)
		return EINVAL;

	/* Runs the buffer in the session's space and waits for it, timed with its bytes and the GPU's own time. */
	started_us = amdrdna_display_now_us();
	error = amdrdna_space_run(session->space, AMDRDNA_TRACKED_GRAPHICS, va, bytes / 4U, VK_NODE_RUN_MS);
	vk_node_count(session->node, VK_NODE_RUNS, started_us, bytes);
	if (error == 0)
		vk_node_count_busy(session, started_us);
	if (error == EINVAL)
		return EINVAL;
	if (error != 0)
		return EIO;

	/* Succeeded: the GPU has done the work. */
	return 0;
}

/*
 * Runs an indirect buffer of bytes at va on the compute queue of the
 * compute queue family (the first compute queue) in the session's address
 * space and waits until the GPU has done it.  Returns as
 * amdrdna_vk_gpu_run() does; a run that does not finish resets only that
 * queue.
 */
int
amdrdna_vk_gpu_run_kernel(
	void *owner,
	uint64_t va,
	uint32_t bytes)
{
	struct amdrdna_session *session;
	uint64_t started_us;
	int error;

	/* The node session the executor names. */
	session = owner;

	/* Whole dwords. */
	if (bytes == 0U || (bytes & 3U) != 0U)
		return EINVAL;

	/* Runs the buffer in the session's space and waits for it, timed with its bytes. */
	started_us = amdrdna_display_now_us();
	error = amdrdna_space_run(session->space, AMDRDNA_TRACKED_COMPUTE, va, bytes / 4U, VK_NODE_RUN_MS);
	vk_node_count(session->node, VK_NODE_RUNS, started_us, bytes);
	if (error == EINVAL)
		return EINVAL;
	if (error != 0)
		return EIO;

	/* Succeeded: the GPU has done the work. */
	return 0;
}

/* Counts a copy between a buffer and an image the command processor moved. */
void
amdrdna_vk_gpu_copied(
	void *owner,
	uint64_t bytes)
{
	struct amdrdna_session *session;
	uint64_t now_us;

	/* The node session the executor names. */
	session = owner;

	/* One copy more, and its bytes. */
	(void)__atomic_fetch_add(&session->node->copies, 1U, __ATOMIC_RELAXED);
	(void)__atomic_fetch_add(&session->node->copy_bytes, bytes, __ATOMIC_RELAXED);

	/* Counted for the timing too (the moves' time is the GPU's, in its run). */
	now_us = amdrdna_display_now_us();
	vk_node_count(session->node, VK_NODE_MOVES, now_us, bytes);
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
	uint64_t started_us;

	/* The node session the executor names. */
	session = owner;

	/* The copy, timed; the window is read without the CPU's cache. */
	started_us = amdrdna_display_now_us();
	kern_memcpy(to, from, length);
	vk_node_count(session->node, VK_NODE_STREAMS, started_us, length);

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
	struct amdrdna_vk_timing *timing;
	uint64_t started_us;
	int error;

	/* The frame's commands, decoded and dispatched by the executor (each timed when the node is). */
	executor = session->vk;
	timing = session->node->vk_timing;
	if (timing == NULL) {
		error = amdrdna_vk_stream_execute(executor, wire, bytes, amdrdna_vk_dispatch);
	} else {
		started_us = amdrdna_display_now_us();
		error = amdrdna_vk_stream_execute(executor, wire, bytes, vk_node_dispatch);
		vk_node_count(session->node, VK_NODE_FRAMES, started_us, bytes);
		vk_node_report(session->node);
	}
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

/* Runs one command of a timed node's frame through the executor's dispatcher and counts its time under its opcode. */
static int
vk_node_dispatch(
	struct amdrdna_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct amdrdna_session *owner;
	uint64_t started_us;
	uint32_t slot;
	int error;

	/* The command, timed. */
	owner = session->owner;
	started_us = amdrdna_display_now_us();
	error = amdrdna_vk_dispatch(session, opcode, requested, reader, reply);

	/* Counted under its opcode, a large one under the last. */
	slot = opcode;
	if (slot >= VK_NODE_TIMED_OPCODES)
		slot = VK_NODE_TIMED_OPCODES - 1U;
	vk_node_count(owner->node, VK_NODE_OPCODE_FIRST + slot, started_us, 0U);

	/* Reports a command the executor refused. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/* Adds one piece of work of a kind (VK_NODE_*) that began at started_us, of bytes, to a timed node's tallies. */
static void
vk_node_count(
	struct amdrdna_node *node,
	uint32_t kind,
	uint64_t started_us,
	uint64_t bytes)
{
	struct amdrdna_vk_timing *timing;
	unsigned long irq;
	uint64_t now_us;

	/* An untimed node counts nothing. */
	timing = node->vk_timing;
	if (timing == NULL)
		return;

	/* Its time. */
	now_us = amdrdna_display_now_us();

	/* Into the kind's tally, under the timing's lock. */
	irq = spin_lock_irqsave(&timing->lock);

	vk_node_tally_add(&timing->tallies[kind], now_us - started_us, bytes);

	spin_unlock_irqrestore(&timing->lock, irq);
}

/* Adds the GPU's own time on the run a session just ran, submitted at submitted_us, to a timed node's period. */
static void
vk_node_count_busy(
	struct amdrdna_session *session,
	uint64_t submitted_us)
{
	struct amdrdna_vk_timing *timing;
	struct vk_node_busy *busy;
	unsigned long irq;
	uint64_t ticks;
	uint64_t stamp;

	/* An untimed node counts nothing. */
	timing = session->node->vk_timing;
	if (timing == NULL)
		return;

	/* The run's ticks; a run that was not stamped counts nothing. */
	ticks = amdrdna_space_run_ticks(session->space, &stamp);
	if (stamp == 0U)
		return;

	/* Into the period, under the timing's lock. */
	irq = spin_lock_irqsave(&timing->lock);

	busy = &timing->busy;
	busy->runs++;
	busy->ticks += ticks;

	/* The longest run so far. */
	if (ticks > busy->most_ticks)
		busy->most_ticks = ticks;

	/* The period's first and last stamped runs give the counter's rate. */
	if (busy->runs == 1U) {
		busy->first_stamp = stamp;
		busy->first_us = submitted_us;
	}
	busy->last_stamp = stamp;
	busy->last_us = submitted_us;

	spin_unlock_irqrestore(&timing->lock, irq);
}

/*
 * Marks a timed node's period once it has passed: the frames, the waits
 * for the GPU, the command streams read through the window, the memory
 * made and the copies moved, each as how many, kilobytes, milliseconds
 * and the longest; then the opcodes that took longest.  A new period
 * starts.  Only the frame runner calls it (under the node's lock).
 */
static void
vk_node_report(
	struct amdrdna_node *node)
{
	const struct vk_node_tally *frames;
	const struct vk_node_tally *runs;
	const struct vk_node_tally *streams;
	const struct vk_node_tally *memory;
	const struct vk_node_tally *moves;
	struct vk_node_tally *taken;
	struct amdrdna_vk_timing *timing;
	struct vk_node_busy busy;
	unsigned long irq;
	uint64_t now_us;
	uint64_t length_us;

	/* Nothing to mark before the period has passed. */
	timing = node->vk_timing;
	now_us = amdrdna_display_now_us();
	length_us = now_us - timing->started_us;
	if (length_us < timing->period_us)
		return;

	/* The tallies taken aside, for marking outside the lock. */
	taken = kern_malloc(sizeof(timing->tallies));
	if (taken == NULL)
		return;

	/* Takes the period's tallies and starts the next period. */
	irq = spin_lock_irqsave(&timing->lock);

	kern_memcpy(taken, timing->tallies, sizeof(timing->tallies));
	kern_memset(timing->tallies, 0, sizeof(timing->tallies));
	busy = timing->busy;
	kern_memset(&timing->busy, 0, sizeof(timing->busy));
	timing->started_us = now_us;

	spin_unlock_irqrestore(&timing->lock, irq);

	/* The frames and the work in them. */
	frames = &taken[VK_NODE_FRAMES];
	runs = &taken[VK_NODE_RUNS];
	streams = &taken[VK_NODE_STREAMS];
	memory = &taken[VK_NODE_MEMORY];
	moves = &taken[VK_NODE_MOVES];
	amdrdna_mark("vk timing %llums: frames %llu %llums max %lluus | gpu runs %llu %llums max %lluus | moves %llu %lluKiB",
	    (unsigned long long)(length_us / 1000U),
	    (unsigned long long)frames->count, (unsigned long long)(frames->us / 1000U), (unsigned long long)frames->most_us,
	    (unsigned long long)runs->count, (unsigned long long)(runs->us / 1000U), (unsigned long long)runs->most_us,
	    (unsigned long long)moves->count, (unsigned long long)(moves->bytes >> 10));
	amdrdna_mark("vk timing: streams %llu %lluKiB %llums max %lluus | memory %llu %lluKiB %llums max %lluus",
	    (unsigned long long)streams->count, (unsigned long long)(streams->bytes >> 10),
	    (unsigned long long)(streams->us / 1000U), (unsigned long long)streams->most_us,
	    (unsigned long long)memory->count, (unsigned long long)(memory->bytes >> 10),
	    (unsigned long long)(memory->us / 1000U), (unsigned long long)memory->most_us);

	/* The GPU's own time on the runs and its clocks, then the opcodes that took longest. */
	vk_node_report_busy(&busy, runs->bytes);
	vk_node_report_clocks(node);
	vk_node_report_opcodes(taken + VK_NODE_OPCODE_FIRST);
	kern_free(taken);
}

/*
 * Marks the GPU's own time on the runs of a period, from the ticks of its
 * timestamp counter, the counter's rate measured against the CPU's clock
 * over the period, and the kilobytes of the runs' indirect buffers.  The
 * rate needs stamped runs at least VK_NODE_RATE_MIN_US apart; without it
 * the time is 0 and the ticks are marked as they are.
 */
static void
vk_node_report_busy(
	const struct vk_node_busy *busy,
	uint64_t run_bytes)
{
	uint64_t kilohertz;
	uint64_t span_us;
	uint64_t busy_us;
	uint64_t most_us;

	/* The counter's rate in kilohertz, from the first and the last stamped run. */
	kilohertz = 0U;
	span_us = busy->last_us - busy->first_us;
	if (busy->runs >= 2U &&
	    span_us >= VK_NODE_RATE_MIN_US &&
	    busy->last_stamp > busy->first_stamp)
		kilohertz = (busy->last_stamp - busy->first_stamp) * 1000U / span_us;

	/* The ticks as time at that rate. */
	busy_us = 0U;
	most_us = 0U;
	if (kilohertz != 0U) {
		busy_us = busy->ticks * 1000U / kilohertz;
		most_us = busy->most_ticks * 1000U / kilohertz;
	}

	/* Its line. */
	amdrdna_mark("vk timing: gpu busy %llu runs %llums max %lluus (%llu ticks, counter %llukHz) | run buffers %lluKiB",
	    (unsigned long long)busy->runs, (unsigned long long)(busy_us / 1000U), (unsigned long long)most_us,
	    (unsigned long long)busy->ticks, (unsigned long long)kilohertz, (unsigned long long)(run_bytes >> 10));
}

/*
 * Marks the clocks the power side's watch last read from the firmware (at
 * most a second old) and how busy it says the graphics engine was; a GPU
 * whose firmware gives no readings is marked so.
 */
static void
vk_node_report_clocks(
	struct amdrdna_node *node)
{
	struct amdrdna_power_readings speeds_now;
	bool clocked;

	/* The watch's last readings. */
	clocked = amdrdna_power_clocks(node->device, &speeds_now);
	if (!clocked) {
		amdrdna_mark("vk timing: clocks not read (the smu firmware gives no readings)");
		return;
	}

	/* Its line. */
	amdrdna_mark("vk timing: clocks gfx %uMHz average %uMHz soc %uMHz mem %uMHz | gfx busy %u%%",
	    speeds_now.gfx_clock,
	    speeds_now.gfx_average,
	    speeds_now.system_clock,
	    speeds_now.memory_clock,
	    speeds_now.gfx_activity);
}

/* Marks the VK_NODE_TIMED_SHOWN opcodes of a period that took the most time, the longest first. */
static void
vk_node_report_opcodes(
	const struct vk_node_tally *tallies)
{
	bool shown[VK_NODE_TIMED_OPCODES];
	uint32_t best;
	uint32_t slot;
	unsigned place;

	/* None shown yet. */
	kern_memset(shown, 0, sizeof(shown));

	/* Each place: the opcode not yet shown with the most time. */
	for (place = 0U; place < VK_NODE_TIMED_SHOWN; place++) {
		/* The longest of the rest. */
		best = VK_NODE_TIMED_OPCODES;
		for (slot = 0U; slot < VK_NODE_TIMED_OPCODES; slot++) {
			if (shown[slot] || tallies[slot].count == 0U)
				continue;
			if (best == VK_NODE_TIMED_OPCODES || tallies[slot].us > tallies[best].us)
				best = slot;
		}

		/* No more opcodes ran. */
		if (best == VK_NODE_TIMED_OPCODES)
			break;

		/* Its line. */
		shown[best] = true;
		amdrdna_mark("vk timing: op %u: %llu %llums max %lluus",
		    (unsigned)best,
		    (unsigned long long)tallies[best].count,
		    (unsigned long long)(tallies[best].us / 1000U),
		    (unsigned long long)tallies[best].most_us);
	}
}

/* Adds one piece of work of us microseconds and bytes to a tally. */
static void
vk_node_tally_add(
	struct vk_node_tally *tally,
	uint64_t us,
	uint64_t bytes)
{
	/* One more, its bytes and its time. */
	tally->count++;
	tally->bytes += bytes;
	tally->us += us;

	/* The longest so far. */
	if (us > tally->most_us)
		tally->most_us = us;
}
