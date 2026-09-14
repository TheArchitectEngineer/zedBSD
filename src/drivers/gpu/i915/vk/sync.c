/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Synchronization: fences, semaphores and queries tied to the WS029 engine
 * seqno breadcrumbs and the device retire wait queue.
 *
 * A fence is armed by a submission with the engine and the seqno that submission
 * will reach; it is signaled once the engine retired past that seqno.  Waiting
 * blocks on the same retire wait queue the request path wakes.
 */

#include "vk-internal.h"
#include "sync.h"

#include "../internal.h"

#include <kern/kmem.h>

#include <errno.h>

/* One Vulkan fence: an optional armed engine/seqno and a signaled latch. */
struct i915_vk_fence {
	struct i915_vk_session *session;
	unsigned armed;
	uint32_t engine;
	uint32_t target;
	unsigned signaled;
};

/* One Vulkan semaphore; single-queue serial ordering makes it a latch. */
struct i915_vk_semaphore {
	unsigned signaled;
};

/* One Vulkan query pool; results are collected at integration. */
struct i915_vk_query_pool {
	uint32_t type;
	uint32_t count;
};

static int i915_vk_fence_ready_locked(struct i915_vk_fence *fence);

/* Creates a fence, optionally already signaled. */
int
i915_vk_fence_create(
	struct i915_vk_session *session,
	int signaled,
	struct i915_vk_fence **out)
{
	struct i915_vk_fence *fence;

	*out = NULL;

	fence = kern_calloc(1U, sizeof(*fence));
	if (fence == NULL)
		return ENOMEM;
	fence->session = session;
	fence->signaled = signaled != 0 ? 1U : 0U;

	*out = fence;
	return 0;
}

/* Releases a fence. */
void
i915_vk_fence_destroy(
	struct i915_vk_fence *fence)
{
	if (fence != NULL)
		kern_free(fence);
}

/* Returns a fence to the unsignaled, unarmed state. */
int
i915_vk_fence_reset(
	struct i915_vk_fence *fence)
{
	fence->signaled = 0U;
	fence->armed = 0U;
	return 0;
}

/* Binds a fence to the seqno a submission will reach on an engine. */
int
i915_vk_fence_arm(
	struct i915_vk_fence *fence,
	uint32_t engine,
	uint32_t target_seqno)
{
	/* A submission arms the fence; the engine retires past it to signal. */
	fence->armed = 1U;
	fence->engine = engine;
	fence->target = target_seqno;
	fence->signaled = 0U;
	return 0;
}

/* Reports whether a fence is signaled without blocking. */
int
i915_vk_fence_status(
	struct i915_vk_fence *fence)
{
	struct i915_device *device;
	unsigned long irq;
	int ready;

	/* The engine seqno is read under the interrupt lock the handler uses. */
	device = fence->session->vk->i915;
	irq = spin_lock_irqsave(&device->irq_lock);
	ready = i915_vk_fence_ready_locked(fence);
	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* A signaled fence reports success; an unsignaled one is not ready. */
	if (ready != 0)
		return 0;

	return EBUSY;
}

/* Waits until a fence is signaled or the caller only wanted to poll. */
int
i915_vk_fence_wait(
	struct i915_vk_fence *fence,
	uint64_t timeout_ns)
{
	struct i915_device *device;
	uint64_t observed;
	unsigned long irq;

	/* Waiting observes the same retire wait queue the request path wakes. */
	device = fence->session->vk->i915;
	irq = spin_lock_irqsave(&device->irq_lock);

	/* The signaled state is re-checked after every wake to avoid a lost update. */
	while (i915_vk_fence_ready_locked(fence) == 0) {
		/* A zero timeout is a poll: report the timeout without sleeping. */
		if (timeout_ns == 0U) {
			spin_unlock_irqrestore(&device->irq_lock, irq);
			return ETIMEDOUT;
		}

		observed = waitq_sequence(&device->retire_waitq);
		(void)waitq_sleep(&device->retire_waitq, &device->irq_lock, observed, 0U, 0U);
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the engine retired past the fence's seqno. */
	return 0;
}

/* Reports fence readiness with the interrupt lock held. */
static int
i915_vk_fence_ready_locked(
	struct i915_vk_fence *fence)
{
	struct i915_device *device;
	uint32_t completed;

	/* An explicitly signaled fence is always ready. */
	if (fence->signaled != 0U)
		return 1;

	/* An unarmed fence never becomes ready on its own. */
	if (fence->armed == 0U)
		return 0;

	/* Wrap-safe comparison signals once the engine passed the target seqno. */
	device = fence->session->vk->i915;
	completed = device->engines[fence->engine].completed_seqno;
	if ((int32_t)(completed - fence->target) >= 0) {
		fence->signaled = 1U;
		return 1;
	}

	return 0;
}

/* Routes fence/semaphore/event/query opcodes; precise decode lands at integration. */
int
i915_vk_sync_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	(void)session;
	(void)opcode;
	(void)reader;
	(void)reply;
	return EINVAL;
}

/* Creates a semaphore. */
int
i915_vk_semaphore_create(
	struct i915_vk_session *session,
	struct i915_vk_semaphore **out)
{
	struct i915_vk_semaphore *semaphore;

	(void)session;
	*out = NULL;

	semaphore = kern_calloc(1U, sizeof(*semaphore));
	if (semaphore == NULL)
		return ENOMEM;

	*out = semaphore;
	return 0;
}

/* Releases a semaphore. */
void
i915_vk_semaphore_destroy(
	struct i915_vk_semaphore *semaphore)
{
	if (semaphore != NULL)
		kern_free(semaphore);
}

/* Creates a query pool. */
int
i915_vk_query_pool_create(
	struct i915_vk_session *session,
	uint32_t type,
	uint32_t count,
	struct i915_vk_query_pool **out)
{
	struct i915_vk_query_pool *pool;

	(void)session;
	*out = NULL;

	pool = kern_calloc(1U, sizeof(*pool));
	if (pool == NULL)
		return ENOMEM;
	pool->type = type;
	pool->count = count;

	*out = pool;
	return 0;
}

/* Releases a query pool. */
void
i915_vk_query_pool_destroy(
	struct i915_vk_query_pool *pool)
{
	if (pool != NULL)
		kern_free(pool);
}
