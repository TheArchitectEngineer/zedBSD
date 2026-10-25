/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the rest of the GHCI driver calls of its Vulkan executor: the
 * executor's start and stop with the device, its part of every open and
 * close, the blob of a Vulkan memory object, the operations it adds to
 * the node, and the work the service thread does for it.
 */

#ifndef DRIVERS_GPU_GHCI_VK_VK_NODE_H
#define DRIVERS_GPU_GHCI_VK_VK_NODE_H

#include <stdint.h>

#include "../ghci.h"

/*
 * Makes the executor of a device whose command processor has firmware
 * and whose GPU has a shader unit: the firmware's heap and the staging
 * ring.  Returns 0 also when the device has no executor (device->vk stays
 * NULL), or why one could not be made.
 */
int ghci_vk_engine_start(struct ghci_device *device);

/* Gives the executor's memory back at the detach, after the ring stopped. */
void ghci_vk_engine_stop(struct ghci_device *device);

/* Adds the capset, the queued frames, the job markers and the recovery to the node's operations. */
void ghci_vk_node_bind(struct ghci_device *device);

/* Makes a session's executor and opens the firmware's session of its tag; the device's lock is held. */
int ghci_vk_session_open(struct ghci_device *device, struct ghci_session *owner);

/* Closes the firmware's session and forgets every object; the device's lock is held. */
void ghci_vk_session_close(struct ghci_device *device, struct ghci_session *owner);

/*
 * Finds the allocation of a session's VkDeviceMemory, for the blob that
 * maps it: EINVAL when the session has no memory of that identity, or
 * when the memory is not host visible.  The device's lock is held.
 */
int ghci_vk_memory_blob(struct ghci_device *device, struct ghci_session *owner, uint64_t identity, struct ghci_allocation **allocation);

/*
 * Does the executor's part of a round of the service thread: completes
 * the frames whose results are in and the job markers whose fences
 * passed.  No lock is held.
 */
void ghci_vk_service(struct ghci_device *device);

/*
 * Ends everything of the firmware's after a reset of the command
 * processor: every frame in flight fails, every session's firmware
 * objects are gone, and the heap is lent again.  No lock is held.
 */
void ghci_vk_reset(struct ghci_device *device);

#endif /* DRIVERS_GPU_GHCI_VK_VK_NODE_H */
