/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The operations the Vulkan executor adds to a GHCI node: the capset
 * libvulkan reads, the queued frames and their drain, the job markers
 * libvulkan completes its fences with, the stop of a closing session's
 * work, and the device's fault.
 *
 * A job marker is reserved with the core's completion before libvulkan
 * submits, committed to the session's last fence (the fence after the
 * frame of the vkQueueSubmit before it) and completed by the service
 * thread when that fence passes.  The device has one ring, so every
 * timeline of a session's queues is that ring's order.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-job.h>

#include "vk.h"
#include "vk-engine.h"

/* The capset libvulkan asks for: the native renderer's protocol record. */
#define CAPSET_ID			4U

static int node_capset(void *private_data, void *opaque, struct gpu_capset *capset);
static int node_submit(void *private_data, void *opaque, const void *command, uint32_t bytes, uint32_t flags, uint32_t timeline, struct drv_gpu_completion *completion);
static void node_drain(void *private_data, void *opaque);
static int node_reserve(void *private_data, void *opaque, uint32_t timeline, struct drv_gpu_completion *completion, void **token);
static int node_commit(void *private_data, void *opaque, void *token, struct drv_gpu_completion *completion);
static int node_cancel(void *private_data, void *opaque, void *token, struct drv_gpu_completion *completion, unsigned fault);
static int node_capacity(void *private_data, void *opaque, uint32_t timeline, unsigned *available);
static int node_stop_begin(void *private_data, void *opaque, int error);
static int node_stop_poll(void *private_data, void *opaque);
static void node_fault(void *private_data, int error);
static int timeline_owned(const struct ghci_session *owner, uint32_t timeline);

/* The queued frames; the table lives as long as the kernel. */
static const struct drv_gpu_command_ops command_operations = {
	node_submit,
	node_drain
};

/* The job markers; the table lives as long as the kernel. */
static const struct drv_gpu_job_ops job_operations = {
	node_reserve,
	node_commit,
	node_cancel,
	node_capacity
};

/*
 * The recovery: the stop of a session's work, which the core asks for at
 * every close (without it the core takes each close for a device that
 * cannot stop, and faults the whole device), and the device's fault.  The
 * command processor's own reset after a GPU fault or a hang is the
 * service thread's (cp.c).
 */
static const struct drv_gpu_recovery_ops recovery_operations = {
	node_stop_begin,
	node_stop_poll,
	node_fault,
	NULL,
	NULL
};

/*
 * Adds the capset, the queued frames, the job markers and the recovery
 * to the node's operations.
 */
void
ghci_vk_node_bind(
	struct ghci_device *device)
{
	/* The operations and the capabilities they make good. */
	device->ops.get_capset = node_capset;
	device->ops.commands = &command_operations;
	device->ops.jobs = &job_operations;
	device->ops.recovery = &recovery_operations;
	device->ops.capabilities |= GPU_CAP_CAPSET | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY;
}

/* Reports the native renderer's capset: id 4, version 0. */
static int
node_capset(
	void *private_data,
	void *opaque,
	struct gpu_capset *capset)
{
	(void)private_data;
	(void)opaque;

	/* Only the protocol's capset, version 0, in a reply that holds it. */
	if (capset->capset_id != CAPSET_ID)
		return ENOTSUP;
	if (capset->capset_version != 0U)
		return ENOTSUP;
	if (capset->capacity < GHCI_CAPSET_BYTES)
		return EINVAL;
	if (capset->capacity > GPU_CAPSET_MAX)
		return EINVAL;

	/* The record, and nothing after it. */
	kern_memset(capset->data, 0, sizeof(capset->data));
	ghci_capset_fill(capset->data);
	capset->bytes = GHCI_CAPSET_BYTES;

	/* Succeeded: the capset is reported. */
	return 0;
}

/* Takes one frame of commands, or an empty frame of a context fence. */
static int
node_submit(
	void *private_data,
	void *opaque,
	const void *command,
	uint32_t bytes,
	uint32_t flags,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct ghci_device *device;
	int error;

	/* Every frame is on the context's own timeline. */
	if (timeline != 0U)
		return EINVAL;

	/* The frame is decoded and submitted under the device's lock. */
	device = private_data;
	mutex_lock(&device->lock);

	error = ghci_vk_submit(device, opaque, command, bytes, flags, completion);

	mutex_unlock(&device->lock);

	/* A frame that was refused retains no callback. */
	if (error != 0)
		return error;

	/* Succeeded: the callback completes once the replies are written. */
	return 0;
}

/* Waits for every frame and fence of a closing session. */
static void
node_drain(
	void *private_data,
	void *opaque)
{
	/* The frames complete on the service thread meanwhile. */
	ghci_vk_drain(private_data, opaque);
}

/* Reserves a job marker on a timeline of the session's queues: EAGAIN when all are in use. */
static int
node_reserve(
	void *private_data,
	void *opaque,
	uint32_t timeline,
	struct drv_gpu_completion *completion,
	void **token)
{
	struct ghci_device *device;
	struct ghci_session *owner;
	unsigned index;
	int owned;
	int error;

	/* A timeline of the session's queues. */
	device = private_data;
	owner = opaque;
	*token = NULL;
	mutex_lock(&device->lock);

	owned = timeline_owned(owner, timeline);
	error = EINVAL;
	if (owned && owner->vk->lost) {
		error = EIO;
	} else if (owned) {
		error = ghci_jobs_reserve(&owner->vk->jobs, completion, &index);
	}

	/* The token names the marker. */
	if (error == 0)
		*token = &owner->vk->jobs.markers[index];

	mutex_unlock(&device->lock);

	/* No marker. */
	if (error != 0)
		return error;

	/* Succeeded: the marker is reserved. */
	return 0;
}

/*
 * Commits a reserved marker to the session's last fence: it completes
 * when the work the session submitted before it has finished, at once
 * when that has already happened.
 */
static int
node_commit(
	void *private_data,
	void *opaque,
	void *token,
	struct drv_gpu_completion *completion)
{
	struct ghci_device *device;
	struct ghci_session *owner;
	uint32_t completed;
	uint32_t fence;
	unsigned index;
	int passed;
	int error;

	/* The marker the token names, bound under the device's lock. */
	device = private_data;
	owner = opaque;
	passed = 0;
	mutex_lock(&device->lock);

	error = ghci_jobs_index(&owner->vk->jobs, token, &index);

	/* The fence to wait for: the session's last, or the completed one when nothing is outstanding. */
	completed = ghci_cp_completed(device);
	fence = completed;
	if (owner->outstanding)
		fence = owner->last_fence;

	/* Binds the marker to that fence. */
	if (error == 0)
		error = ghci_jobs_commit(&owner->vk->jobs, index, completion, fence, completed, &passed);

	mutex_unlock(&device->lock);

	/* A token or a completion that is not this reservation's. */
	if (error != 0)
		return error;

	/* Work that has already finished completes the marker now. */
	if (passed)
		drv_gpu_complete(completion, 0);

	/* Succeeded: the marker completes in order. */
	return 0;
}

/*
 * Withdraws a reservation (normal) or ends an uncertain one with an
 * error at the next retirement (fault).
 */
static int
node_cancel(
	void *private_data,
	void *opaque,
	void *token,
	struct drv_gpu_completion *completion,
	unsigned fault)
{
	struct ghci_device *device;
	struct ghci_session *owner;
	unsigned index;
	int error;

	/* The marker the token names, changed under the device's lock. */
	device = private_data;
	owner = opaque;
	mutex_lock(&device->lock);

	error = ghci_jobs_index(&owner->vk->jobs, token, &index);
	if (error == 0)
		error = ghci_jobs_cancel(&owner->vk->jobs, index, completion, fault);

	mutex_unlock(&device->lock);

	/* A cancel the marker's state does not allow. */
	if (error != 0)
		return error;

	/* A failed marker completes with the next round of the service. */
	if (fault != 0U)
		ghci_service_wake(device, GHCI_WORK_REPLY);

	/* Succeeded: the reservation is withdrawn or ended. */
	return 0;
}

/* Reports how many markers a reservation on the timeline could take now. */
static int
node_capacity(
	void *private_data,
	void *opaque,
	uint32_t timeline,
	unsigned *available)
{
	struct ghci_device *device;
	struct ghci_session *owner;
	int owned;
	int error;

	/* A timeline of the session's queues on a device that runs. */
	device = private_data;
	owner = opaque;
	*available = 0U;
	mutex_lock(&device->lock);

	owned = timeline_owned(owner, timeline);
	error = EINVAL;
	if (owned && device->vk->faulted) {
		error = EIO;
	} else if (owned) {
		*available = ghci_jobs_capacity(&owner->vk->jobs);
		error = 0;
	}

	mutex_unlock(&device->lock);

	/* A timeline the session's queues do not complete on. */
	if (error != 0)
		return error;

	/* Succeeded: the count is a snapshot, not a promise. */
	return 0;
}

/*
 * Begins the stop of a session's work.  The core has stopped taking the
 * session's commands; its frames and fences in the ring end by
 * themselves (or with the next reset), so nothing more is to be done.
 */
static int
node_stop_begin(
	void *private_data,
	void *opaque,
	int error)
{
	(void)private_data;
	(void)opaque;
	(void)error;

	/* Succeeded: the stop has begun; the poll says when it is done. */
	return 0;
}

/*
 * Reports whether a stopping session's work is done: 0 when no frame of
 * it is in flight, no job marker waits and its last fence passed (or a
 * reset ended it), EAGAIN before.
 */
static int
node_stop_poll(
	void *private_data,
	void *opaque)
{
	struct ghci_device *device;
	struct ghci_session *owner;
	struct ghci_ticket ticket;
	unsigned pending;
	unsigned committed;
	int outstanding;
	int done;

	/* What of the session is still in flight. */
	device = private_data;
	owner = opaque;
	mutex_lock(&device->lock);

	pending = 0U;
	committed = 0U;
	if (owner->vk != NULL) {
		pending = owner->vk->pending;
		committed = owner->vk->jobs.committed;
	}
	outstanding = owner->outstanding;
	ticket.fence = owner->last_fence;
	ticket.generation = owner->last_generation;

	mutex_unlock(&device->lock);

	/* The last fence, when one is outstanding. */
	done = 1;
	if (outstanding)
		done = ghci_cp_done(device, ticket.fence, ticket.generation);

	/* Work still in flight: the service thread completes it meanwhile. */
	if (!done || pending != 0U || committed != 0U) {
		ghci_service_wake(device, GHCI_WORK_REPLY);
		return EAGAIN;
	}

	/* Succeeded: the GPU no longer reaches the session's memory for it. */
	return 0;
}

/*
 * Takes the core's fault: the device takes no new frame.  The work in
 * flight still ends on the GPU or with the next reset, so no memory goes
 * back before the GPU is done with it.
 */
static void
node_fault(
	void *private_data,
	int error)
{
	struct ghci_device *device;

	(void)error;

	/* No frame is submitted from here. */
	device = private_data;
	mutex_lock(&device->lock);

	device->vk->faulted = 1;

	mutex_unlock(&device->lock);
}

/* Reports whether a session's queues complete jobs on a timeline (1 to 63): 1 when they do. */
static int
timeline_owned(
	const struct ghci_session *owner,
	uint32_t timeline)
{
	/* An open without an executor has no queue. */
	if (owner->vk == NULL)
		return 0;

	/* Timelines 1 to 63, as vkGetDeviceQueue2 gave them. */
	if (timeline == 0U)
		return 0;
	if (timeline >= GHCI_VK_TIMELINES)
		return 0;
	if ((owner->vk->timelines & ((uint64_t)1U << timeline)) == 0U)
		return 0;

	/* Succeeded: the timeline is the session's. */
	return 1;
}
