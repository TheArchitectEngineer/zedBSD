/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the executor's engine (vk-engine.c) the frames and the
 * sessions use: the control packets the driver waits for, the staging
 * ring and the list of frames in flight.  The device's lock is held for
 * every call.
 */

#ifndef DRIVERS_GPU_GHCI_VK_VK_ENGINE_H
#define DRIVERS_GPU_GHCI_VK_VK_ENGINE_H

#include <stdint.h>

#include "vk.h"

/*
 * Sends one GHCI gpu-op as a class 2 packet of a session (NULL: the
 * driver's own), asks for its result and waits for the fence after it.
 * Returns 0 with the firmware's VkResult in *status, or why the packet
 * did not run.
 */
int ghci_vk_engine_control(struct ghci_device *device, struct ghci_session *owner, uint32_t opcode, const uint32_t *arguments, uint32_t count, int32_t *status);

/*
 * Sends one GHCI gpu-op as a class 2 packet of a session (NULL: the
 * driver's own) without a result, and does not wait: the work after it
 * in the ring is ordered after it.  Returns 0, or why it is not in the
 * ring.
 */
int ghci_vk_engine_post(struct ghci_device *device, struct ghci_session *owner, uint32_t opcode, const uint32_t *arguments, uint32_t count);

/*
 * Takes bytes of the staging ring for a frame.  Returns 0 with the
 * offset of the run and the bytes it uses up (the run and the end of the
 * ring it skipped), EAGAIN while frames in flight hold the room, or E2BIG
 * for a frame no ring holds.
 */
int ghci_vk_staging_take(struct ghci_device *device, uint32_t bytes, uint32_t *offset, uint32_t *consumed);

/* Puts a submitted frame at the end of the device's list. */
void ghci_vk_frame_queue(struct ghci_device *device, struct ghci_vk_frame *frame);

/* Reads every format's features from the GPU's FMT capability into the engine's table. */
void ghci_vk_format_load(struct ghci_device *device);

/* Reports whether the device's executor takes no new frame: 1 after a fault. */
int ghci_vk_engine_faulted(struct ghci_device *device);

#endif /* DRIVERS_GPU_GHCI_VK_VK_ENGINE_H */
