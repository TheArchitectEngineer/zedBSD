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
 * the VRAM, transfers, CPU mappings, sharing of blobs between its opens and
 * one display: the head the firmware lit, in the mode it lit it with
 * (plan/ws037/phase006), with its refresh boundaries, its cursor plane and
 * its scanout constraints.  node.c holds the sessions and resources,
 * present.c the display's operations, cursor.c the cursor plane, share.c
 * the sharing and the scanout operations.
 */

#ifndef DRIVERS_GPU_NVRTX_NODE_H
#define DRIVERS_GPU_NVRTX_NODE_H

#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-scanout.h>
#include <drivers/gpu/gpu-share.h>
#include <kern/lock.h>
#include <kern/pmem.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

struct nvrtx_vk_session;

/* The one display's identity and generation. */
#define NVRTX_NODE_DISPLAY_ID		1U
#define NVRTX_NODE_GENERATION		1U

/* The display's topology sequence: its first value, which nothing moves yet (no hotplug events). */
#define NVRTX_NODE_TOPOLOGY_FIRST	1U

/* The two kinds of resource: ordinary storage in RAM, a blob in the VRAM. */
#define NVRTX_RESOURCE_STORAGE		1U
#define NVRTX_RESOURCE_BLOB		2U

/*
 * The VRAM of a blob: a buffer object mapped in the CPU's window.
 *
 * Shared by the blob, every import of it into another open and every
 * export the GPU core holds; refs counts them, under the node's lock, and
 * the last one gives the VRAM back.  bytes is the size the blob was asked
 * for.  kept is set when the display could not be moved off the VRAM: it
 * is then never given back (until power-off), though the record goes.
 */
struct nvrtx_backing {
	struct nvrtx_bo bo;
	uint64_t bytes;
	uint32_t refs;
	int kept;
};

/*
 * One resource of a session.
 *
 * Storage is a contiguous run of RAM the CPU reads and writes directly; a
 * presentation copies its rows into a surface of the node.  A blob names a
 * backing in the VRAM, which the display shows as it is; an import is a
 * blob naming another open's backing.  Created by the resource, blob or
 * import operation and freed by the destroy, which first takes a blob off
 * the screen and then drops its reference to the backing.
 */
struct nvrtx_resource {
	uint32_t kind;
	uint64_t bytes;
	struct kern_pmem ram;
	uint8_t *cpu;
	struct nvrtx_backing *backing;
	/* A blob's protocol number and its place in its session's list of blobs (NULL session: not listed). */
	uint32_t resource_id;
	struct nvrtx_session *session;
	struct nvrtx_resource *next_blob;
};

/*
 * One open of the GPU device; it only names its node.
 */
struct nvrtx_session {
	struct nvrtx_node *node;
	/* The session's blobs by protocol number (the executor reads streams and writes replies there), and its executor session. */
	struct nvrtx_resource *blobs;
	struct nvrtx_vk_session *vk;
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
 *
 * refresh_lock (IRQ-safe, never held across a wait) guards the refresh
 * count: the head's 16-bit frame counter extended to a count that never
 * goes back (refresh_frame is the last reading, refresh_time_ns the time the
 * count last moved); readers do not take the node's lock, which a
 * presentation holds while it waits.
 *
 * The cursor plane, under the node's lock: cursor_object holds two images
 * of 64 by 64 pixels (slots), cursor_ready says the cursor channel and the
 * slots exist, cursor_slot is the slot last written (the one a shown cursor
 * reads), cursor_has_image that the lease set one, cursor_visible that the
 * core shows it, cursor_point the position word last sent.
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
	struct spinlock refresh_lock;
	uint64_t refresh_count;
	uint64_t refresh_time_ns;
	uint32_t refresh_frame;
	int refresh_frame_valid;
	struct nvrtx_bo cursor_object;
	int cursor_ready;
	unsigned cursor_slot;
	int cursor_has_image;
	int cursor_visible;
	uint32_t cursor_point;
};

extern const struct drv_gpu_display_ops nvrtx_display_ops;
extern const struct drv_gpu_share_ops nvrtx_share_ops;
extern const struct drv_gpu_scanout_ops nvrtx_scanout_ops;

int nvrtx_present_start(struct nvrtx_node *node);
void nvrtx_present_session_close(struct nvrtx_node *node, void *session);
void nvrtx_present_resource_gone(struct nvrtx_node *node, struct nvrtx_resource *resource);
void nvrtx_present_refresh_sample(struct nvrtx_node *node, uint64_t *count, uint64_t *time_ns);

void nvrtx_cursor_start(struct nvrtx_node *node);
void nvrtx_cursor_lease_end(struct nvrtx_node *node);
int nvrtx_cursor_set(void *private_data, void *session, void *object, const struct gpu_display_cursor_image *request);
int nvrtx_cursor_move(void *private_data, void *session, const struct gpu_display_cursor_move *request);
int nvrtx_cursor_hide(void *private_data, void *session, const struct gpu_display_cursor_hide *request);

void nvrtx_node_backing_put(struct nvrtx_node *node, struct nvrtx_backing *backing);
uint32_t nvrtx_node_resource_id(struct nvrtx_node *node);
void nvrtx_node_blob_listed(struct nvrtx_node *node, struct nvrtx_session *session, struct nvrtx_resource *resource, uint32_t resource_id);

#endif
