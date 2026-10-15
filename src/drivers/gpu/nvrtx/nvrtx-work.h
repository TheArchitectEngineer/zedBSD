/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The work channel of the nvrtx driver: the parts that touch no hardware.
 *
 * The work channel (work.c) is the graphics channel
 * the GPU device keeps for the work it is given: one address space the
 * driver owns over the 512 MiB at NVRTX_WORK_VA_BASE, whose low part holds
 * the graphics context's buffers at the golden context's addresses, then
 * the driver's own pages (the ring, the semaphore and the fences' push
 * buffers), then the runs the device maps for its users.  Each submission
 * takes two ring entries: the caller's push buffer segment, then a fence
 * of the driver's that releases the next value of the semaphore.
 */

#ifndef DRIVERS_GPU_NVRTX_WORK_H
#define DRIVERS_GPU_NVRTX_WORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The address space: one first-level directory entry's 512 MiB.
 *
 * Value referenced: Linux v6.19
 * drivers/gpu/drm/nouveau/nvkm/subdev/mmu/vmmtu102.c (tu102_vmm, the
 * 29-bit level).
 */
#define NVRTX_WORK_VA_BASE		0x20000000ULL
#define NVRTX_WORK_VA_BYTES		0x20000000ULL

/* The driver's own pages, after the graphics context's buffers, and the first address a mapping gets. */
#define NVRTX_WORK_VA_DRIVER		0x28000000ULL
#define NVRTX_WORK_VA_USER		0x28100000ULL

/* The ring's entries (a power of two; one page of 8-byte entries) and the push buffer bytes of one fence. */
#define NVRTX_WORK_RING_ENTRIES		512U
#define NVRTX_WORK_FENCE_BYTES		64U

/*
 * The most words one ring entry carries (its length field, bits 30:10 of
 * its upper word).
 *
 * Value referenced: NVIDIA open-gpu-doc classes/host/clc46f.h
 * (NVC46F_GP_ENTRY1_LENGTH).
 */
#define NVRTX_WORK_SEGMENT_WORDS_MAX	0x1fffffU

/*
 * The two classes the channel has, and their subchannels.
 *
 * Values referenced: Mesa 25.3.6 src/nouveau/vulkan/nvk_queue.c
 * (nvk_queue_init_context_state).
 */
#define NVRTX_WORK_SUBCHANNEL_THREED	0U
#define NVRTX_WORK_SUBCHANNEL_COMPUTE	1U

bool nvrtx_work_fence_passed(uint32_t seen, uint32_t value);
size_t nvrtx_work_push_objects(uint8_t *push, uint32_t threed_class, uint32_t compute_class);

#endif
