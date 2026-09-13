/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Shares completion payloads between native queue operations and WSI acquisition.
 */

#ifndef VULKAN_SYNC_INTERNAL_H
#define VULKAN_SYNC_INTERNAL_H

#include "internal.h"

struct vulkan_external_fence;

/* A device mutex protects the software payload of one ordinary sync object. */
struct vulkan_sync {
	struct vulkan_object object;
	VkBool32 software_signaled;
	uint64_t notification;
	VkExternalFenceHandleTypeFlags export_types;
	struct vulkan_external_fence *external;
};

/* These helpers never acquire a device mutex; their caller already owns it. */
struct vulkan_sync *vulkan_sync_object(uint64_t handle);
VkResult vulkan_sync_device_status_locked(struct VkDevice_T *device);
VkResult vulkan_sync_status_locked(struct VkDevice_T *device, struct vulkan_sync *sync, uint32_t opcode);
void vulkan_sync_device_error(struct VkDevice_T *device, VkResult status);
VkResult vulkan_sync_capacity_query(struct VkQueue_T *queue, uint64_t *sequence);
VkResult vulkan_sync_capacity_wait(struct VkQueue_T *queue, uint64_t sequence);
VkResult vulkan_sync_job_reserve(struct VkQueue_T *queue, struct vulkan_sync *sync, struct vulkan_notification **reserved, uint64_t *capacity_sequence, VkBool32 *prepared);
VkResult vulkan_sync_job_finish(struct VkQueue_T *queue, struct vulkan_sync *sync, struct vulkan_notification *reserved, VkResult native_status, VkBool32 accepted);
VkResult vulkan_sync_job_status(struct vulkan_sync *sync, uint64_t timeout_ns);
void vulkan_sync_quiesce(struct vulkan_sync *sync);

/* Shared reference payloads retain native completion proof and caller-owned synchronization. */
VkResult vulkan_external_fence_create(struct VkDevice_T *device, struct vulkan_sync *sync, const VkFenceCreateInfo *create);
VkResult vulkan_external_fence_status_locked(struct VkDevice_T *device, struct vulkan_sync *sync);
VkResult vulkan_external_fence_wait(struct VkDevice_T *device, struct vulkan_sync *sync, uint64_t timeout_ns);
VkResult vulkan_external_fence_reset_locked(struct VkDevice_T *device, struct vulkan_sync *sync);
VkResult vulkan_external_fence_prepare_locked(struct VkDevice_T *device, struct vulkan_sync *sync, int *fd, uint64_t *generation, VkBool32 prepare_native);
VkResult vulkan_external_fence_acquire_locked(struct VkDevice_T *device, struct vulkan_sync *sync);
int vulkan_external_fence_descriptor(struct vulkan_sync *sync);
void vulkan_external_fence_finish(struct vulkan_sync *sync);

/* Clock sampling and cooperative pauses are performed without Vulkan locks. */
VkResult vulkan_sync_clock(uint64_t *nanoseconds);
VkResult vulkan_sync_pause(uint64_t nanoseconds);

#endif
