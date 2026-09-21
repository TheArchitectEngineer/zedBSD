/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Synchronization: fences, semaphores and queries tied to the WS029 seqno
 * breadcrumbs and completion callback.  Contract for p009; see
 * external-design.md section 4.9.
 */

#ifndef I915_VK_SYNC_H
#define I915_VK_SYNC_H

#include "vk-internal.h"

/* Routes fence/semaphore/event/query opcodes and the wait-idle calls. */
int
i915_vk_sync_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/* Fence lifetime and waiting. */
int
i915_vk_fence_create(
	struct i915_vk_session *session,
	int signaled,
	struct i915_vk_fence **out);

void
i915_vk_fence_destroy(
	struct i915_vk_fence *fence);

int
i915_vk_fence_reset(
	struct i915_vk_fence *fence);

int
i915_vk_fence_wait(
	struct i915_vk_fence *fence,
	uint64_t timeout_ns);

int
i915_vk_fence_status(
	struct i915_vk_fence *fence);

/* Signals a fence whose submission has run to its end (E-127: vkQueueSubmit is synchronous). */
void
i915_vk_fence_signal(
	struct i915_vk_fence *fence);

/* Binds a fence to the target seqno a submit produced on an engine. */
int
i915_vk_fence_arm(
	struct i915_vk_fence *fence,
	uint32_t engine,
	uint32_t target_seqno);

/* Semaphore lifetime (single-queue serial ordering for now). */
int
i915_vk_semaphore_create(
	struct i915_vk_session *session,
	struct i915_vk_semaphore **out);

void
i915_vk_semaphore_destroy(
	struct i915_vk_semaphore *semaphore);

/* Query pool lifetime. */
int
i915_vk_query_pool_create(
	struct i915_vk_session *session,
	uint32_t type,
	uint32_t count,
	struct i915_vk_query_pool **out);

void
i915_vk_query_pool_destroy(
	struct i915_vk_query_pool *pool);

#endif /* I915_VK_SYNC_H */
