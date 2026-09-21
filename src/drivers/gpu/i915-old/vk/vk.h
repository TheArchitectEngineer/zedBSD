/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Native Vulkan executor entry: attach to an i915 device, per-open session
 * lifetime, and the drv_gpu command entry point.  Implemented by module top
 * (vk.c).  Contract for p002; see plan/ws031/external-design.md section 4.1.
 */

#ifndef I915_VK_H
#define I915_VK_H

#include "vk-internal.h"

/* Attaches the executor to an i915 device and advertises its capset. */
int
drv_i915_vk_attach(
	struct i915_device *device,
	struct i915_vk_device **out);

/* Releases the executor and every object still held for the device. */
void
drv_i915_vk_detach(
	struct i915_vk_device *vk);

/* Opens a session over an i915 drv_gpu session (its PPGTT and lifetime). */
int
drv_i915_vk_open(
	struct i915_vk_device *vk,
	struct i915_session *gpu_session,
	struct i915_vk_session **out);

/* Closes a session and retires its objects; cannot fail. */
void
drv_i915_vk_close(
	struct i915_vk_session *session);

/* Decodes and executes one submitted command stream; writes any reply. */
int
drv_i915_vk_command(
	struct i915_vk_session *session,
	const void *wire,
	size_t bytes,
	void *reply,
	size_t *reply_bytes);

/*
 * E-127: the blob libvulkan exports for a VkDeviceMemory names it by blob_id; that blob is the
 * storage of the allocation.  ENOENT: no allocation has that identity.
 */
struct i915_gem_object;
int
drv_i915_vk_blob_attach(
	struct i915_vk_device *vk,
	uint64_t blob_id,
	struct i915_gem_object *object);

void
drv_i915_vk_blob_detach(
	struct i915_vk_device *vk,
	struct i915_gem_object *object);

#endif /* I915_VK_H */
