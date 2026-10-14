/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU device of the nvrtx driver (stage P8c, nvrtx.resident=1).
 *
 * After the takeover of the firmware's head (P8b) the GSP stays up and the
 * GPU is registered with the GPU core as a device with storage, blobs in
 * the VRAM, transfers, CPU mappings and one display: the head the firmware
 * lit, in the mode it lit it with (plan/ws037/phase006).  node.c holds the
 * sessions and resources, present.c the display's operations.
 */

#ifndef DRIVERS_GPU_NVRTX_NODE_H
#define DRIVERS_GPU_NVRTX_NODE_H

#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/lock.h>
#include <kern/pmem.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The one display's identity and generation. */
#define NVRTX_NODE_DISPLAY_ID		1U
#define NVRTX_NODE_GENERATION		1U

/* The two kinds of resource: ordinary storage in RAM, a blob in the VRAM. */
#define NVRTX_RESOURCE_STORAGE		1U
#define NVRTX_RESOURCE_BLOB		2U

/*
 * One resource of a session.
 *
 * Storage is a contiguous run of RAM the CPU reads and writes directly; a
 * presentation copies its rows into a surface of the node.  A blob is a
 * buffer object in the VRAM, mapped in the CPU's window, which the display
 * shows as it is.  Created by the resource or blob operation and freed by
 * the destroy, which first takes a blob off the screen.  kept is set when a
 * blob could not be taken off the screen: its VRAM is then never freed.
 */
struct nvrtx_resource {
	uint32_t kind;
	uint64_t bytes;
	struct kern_pmem ram;
	uint8_t *cpu;
	struct nvrtx_bo bo;
	int kept;
};

/*
 * One open of the GPU device; it only names its node.
 */
struct nvrtx_session {
	struct nvrtx_node *node;
};

/*
 * The registered GPU device and its display.
 *
 * Made once by nvrtx_node_start() and kept until power-off, as the GSP is.
 * lock serializes the lease, the frames and the screen's state; it is taken
 * before the memory lock.  owner is the session holding the lease (NULL when
 * free) and lease its number; sequence counts the frames shown under the
 * lease.  shown is the blob on the screen, NULL while a surface of the node
 * is.  frames are the node's two surfaces storage frames are copied into,
 * next the one the next copy uses; the console surface is P8b's first.
 * broken is set once a flip did not begin: nothing is flipped after it.
 */
struct nvrtx_node {
	struct nvrtx_gpu *gpu;
	struct drv_gpu_device *device;
	struct drv_gpu_ops ops;
	struct mutex lock;
	void *owner;
	uint64_t lease;
	uint64_t next_lease;
	uint64_t sequence;
	uint64_t shown_time_ns;
	struct nvrtx_resource *shown;
	struct nvrtx_bo frame_objects[2];
	struct nvrtx_disp_surface frames[2];
	unsigned next;
	uint32_t next_resource_id;
	uint32_t width;
	uint32_t height;
	uint32_t refresh_millihz;
	int broken;
};

extern const struct drv_gpu_display_ops nvrtx_display_ops;
int nvrtx_present_start(struct nvrtx_node *node);
void nvrtx_present_session_close(struct nvrtx_node *node, void *session);
void nvrtx_present_resource_gone(struct nvrtx_node *node, struct nvrtx_resource *resource);

#endif
