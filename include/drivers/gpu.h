/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * GPU backend operations and dynamic device registration.
 */

#ifndef DRIVERS_GPU_H
#define DRIVERS_GPU_H

#include <uapi/gpu.h>
#include <drivers/gpu-display.h>
#include <drivers/gpu-share.h>
#include <drivers/gpu-scanout.h>

#include <stdint.h>

#define DRV_GPU_INTERFACE_VERSION	7U
#define DRV_GPU_MAPPING_DEVICE		1U

struct drv_gpu_device;
struct drv_gpu_completion;

/*
 * Optional queued commands retain a common completion until exactly one finish.
 * Submit failure retains no completion. Drain ends every accepted callback before
 * the backend session closes, including when uncertain DMA remains quarantined.
 */
struct drv_gpu_command_ops {
	int (*submit)(void *, void *, const void *, uint32_t, uint32_t, uint32_t, struct drv_gpu_completion *);
	void (*drain)(void *, void *);
};

/*
 * One supervised reservation retains a callback before userspace submits native
 * work. Reserve owns marker storage and a finite producer deadline. Commit uses
 * that storage without allocation; normal cancel removes the callback before
 * returning success. Fault cancellation terminates uncertain work with ERROR.
 * Each action verifies both the reservation token and original completion.
 * The ordinary command drain also retires every accepted job callback.
 */
struct drv_gpu_job_ops {
	int (*reserve)(void *, void *, uint32_t, struct drv_gpu_completion *, void **);
	int (*commit)(void *, void *, void *, struct drv_gpu_completion *);
	int (*cancel)(void *, void *, void *, struct drv_gpu_completion *, unsigned);
};

/*
 * An immutable CPU view borrowed from one retained resource. The GPU core
 * retains the resource and its open file through every VM mapping and pin.
 * DEVICE distinguishes uncached MMIO from ordinary coherently mapped DMA RAM.
 */
struct drv_gpu_mapping {
	uint64_t physical;
	void *address;
	uint64_t bytes;
	uint32_t attributes;
};

/*
 * Immutable operations shared by instances of one backend.
 *
 *  - No callback runs under a core spinlock.
 *  - Distinct sessions may execute concurrently, a single session
 *    admits one ordinary ioctl at a time; completion/event snapshots bypass
 *    admission and may run concurrently with any ordinary callback.
 *  - Open failure must unwind its own state.
 *  - Resource and blob allocation failures must unwind their own state.
 *  - Optional capabilities require their complete callback pair or operation.
 *  - All buffers passed to optional operations are validated kernel copies.
 *  - Resource read/write finish copying before returning; command receipt is
 *    independent of Vulkan execution completion.
 *  - Present accepts storage resources; the backend owns display arbitration.
 *  - Close and resource_destroy cannot fail and must finish using the state
 *    before returning.
 *  - Unregister preserves private_data until all sessions close; the owner
 *    must retry EBUSY before releasing device state.
 */
struct drv_gpu_ops {
	uint32_t version;
	uint32_t size;
	uint32_t capabilities;
	uint32_t reserved;

	int (*open)(void *, void **);
	void (*close)(void *, void *);
	int (*get_info)(void *, void *, struct gpu_info *);
	int (*resource_create)(void *, void *, const struct gpu_resource_create *, void **);
	void (*resource_destroy)(void *, void *, void *);
	int (*get_capset)(void *, void *, struct gpu_capset *);
	int (*blob_create)(void *, void *, const struct gpu_blob_create *, void **, uint32_t *);
	int (*resource_read)(void *, void *, void *, uint64_t, void *, uint32_t);
	int (*resource_write)(void *, void *, void *, uint64_t, const void *, uint32_t);
	int (*command)(void *, void *, const void *, uint32_t);
	int (*present)(void *, void *, void *, const struct gpu_present *);
	int (*resource_map)(void *, void *, void *, struct drv_gpu_mapping *);

	/*
	 * Optional physical placement allocator validates actual backing before success.
	 * It must satisfy every condition or unwind its own allocations and return
	 * ENOTSUP. Native renderer IDs, BAR addresses and requested flags alone do
	 * not prove physical placement, contiguity or cache coherence.
	 */
	int (*blob_create_placed)(void *, void *, const struct gpu_blob_create_placed *, void **, uint32_t *);

	/* Optional display ownership and immutable mapping views retain the same session lifetime. */
	const struct drv_gpu_display_ops *display;
	const struct drv_gpu_share_ops *share;
	const struct drv_gpu_command_ops *commands;
	const struct drv_gpu_scanout_ops *scanout;
	const struct drv_gpu_job_ops *jobs;
};

/*
 * Retains a wrapper protected by an existing reference or its publisher's lock.
 * The reference preserves the wrapper, not the backend's separate hardware life.
 */
void drv_gpu_retain(struct drv_gpu_device *device);

/*
 * Releases one retained wrapper without invoking backend operations.
 */
void drv_gpu_release(struct drv_gpu_device *device);

/*
 * Finishes one accepted request from an interrupt or ordinary driver context.
 */
void drv_gpu_complete(struct drv_gpu_completion *completion, int error);

/*
 * Publishes a device-wide failure, or zero after a checked fresh-session recovery.
 */
void drv_gpu_report_error(struct drv_gpu_device *device, int error);

/*
 * Tests that only the current opening callback remains, with no exported or mapped owner.
 */
int drv_gpu_recovery_ready(struct drv_gpu_device *device);

/*
 * Registers one initialized device using borrowed operations and private data.
 *
 *  - Every call creates an independent device, even when operations are shared.
 *  - The core owns the returned handle and its /dev/gpuN publication.
 *  - Failure leaves result NULL and does not consume operations or private_data.
 *  - Both borrowed objects must remain valid until unregister succeeds.
 */
int
drv_gpu_register(
	const struct drv_gpu_ops *ops,
	void *private_data,
	struct drv_gpu_device **result);

/*
 * Withdraws a device and releases its registration after all sessions close.
 *
 *  - EBUSY retains the handle and its borrowed state for a later retry.
 *  - Once withdrawal starts, new opens and ioctls fail with ENODEV.
 *  - Success consumes the handle, the caller may then release its private data
 *    and operations.
 *  - Old inode references retain only the offline core wrapper.
 */
int
drv_gpu_unregister(
	struct drv_gpu_device *device);

#endif
