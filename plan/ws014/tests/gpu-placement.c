/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises production blob allocation with independently described device backing pages. */
#define main gpu_framework_unused_main
#include "gpu-framework.c"
#undef main

/* The modeled DMA allocation has real retained storage and independently chosen address metadata. */
struct placement_backing {
	uint64_t pages[2];
	unsigned coherent;
};

/* Device-owned backing facts remain independent from every incoming requested condition. */
static struct placement_backing actual_backing;

/* These counters distinguish actual placed allocation and checked physical rejection from early core rejection. */
static unsigned placed_calls;
static unsigned placed_rejections;

static int placed_blob(void *opaque, void *session, const struct gpu_blob_create_placed *request, void **result, uint32_t *resource_id);
static int placed_backing_validate(const struct gpu_placement *placement);
static void placed_request(struct gpu_blob_create_placed *request);
static int placed_call(struct test_file *opened, struct gpu_blob_create_placed *request);

/*
 * Verifies actual backing conditions, allocation rollback and exact old/new ioctl boundaries.
 */
int
main(
	void)
{
	struct drv_gpu_ops operations;
	struct drv_gpu_ops ordinary;
	struct drv_gpu_device *device;
	struct drv_gpu_device *legacy_device;
	struct test_backend backend;
	struct test_backend legacy_backend;
	struct test_file opened;
	struct test_file legacy;
	struct gpu_blob_create_placed request;
	struct gpu_blob_create original;
	unsigned live;
	unsigned calls;
	int error;

	/* Both drivers provide the same ordinary lifecycle; one additionally understands physical placement. */
	memset(&backend, 0, sizeof(backend));
	memset(&legacy_backend, 0, sizeof(legacy_backend));
	backend.max_bytes = 8192U;
	legacy_backend.max_bytes = 8192U;
	memset(&operations, 0, sizeof(operations));
	operations.version = DRV_GPU_INTERFACE_VERSION;
	operations.size = sizeof(operations);
	operations.capabilities = GPU_CAP_BLOB;
	operations.open = backend_open;
	operations.close = backend_close;
	operations.get_info = backend_get_info;
	operations.blob_create = backend_blob;
	operations.resource_destroy = backend_destroy;
	ordinary = operations;
	operations.blob_create_placed = placed_blob;
	error = drv_gpu_register(&operations, &backend, &device);
	assert(error == 0);
	error = drv_gpu_register(&ordinary, &legacy_backend, &legacy_device);
	assert(error == 0);
	error = open_file(&opened, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&legacy, "gpu1", O_RDWR);
	assert(error == 0);

	/* Independently selected contiguous coherent pages fit DMA32 and a page-aligned base. */
	actual_backing.pages[0] = 0x01000000U;
	actual_backing.pages[1] = 0x01001000U;
	actual_backing.coherent = 1U;
	placed_request(&request);
	error = placed_call(&opened, &request);
	assert(error == 0 && request.blob.handle != 0U && placed_calls == 1U);
	error = destroy_handle(&opened, request.blob.handle);
	assert(error == 0);

	/* A real address above the caller's inclusive DMA limit fails after allocation and is released once. */
	placed_request(&request);
	request.placement.max_dma_address = actual_backing.pages[1] + 4094U;
	error = placed_call(&opened, &request);
	assert(error == ENOTSUP && backend.live == 0U && placed_rejections == 1U);

	/* Neither discontiguous pages nor noncoherent backing may satisfy their requested property flags. */
	actual_backing.pages[1] += 4096U;
	placed_request(&request);
	error = placed_call(&opened, &request);
	assert(error == ENOTSUP && backend.live == 0U && placed_rejections == 2U);
	actual_backing.pages[1] = actual_backing.pages[0] + 4096U;
	actual_backing.coherent = 0U;
	placed_request(&request);
	error = placed_call(&opened, &request);
	assert(error == ENOTSUP && backend.live == 0U && placed_rejections == 3U);
	actual_backing.coherent = 1U;

	/* DMA32 checks all page endpoints rather than trusting an allocation's first address alone. */
	actual_backing.pages[0] = (uint64_t)UINT32_MAX - 4095U;
	actual_backing.pages[1] = (uint64_t)UINT32_MAX + 1U;
	placed_request(&request);
	error = placed_call(&opened, &request);
	assert(error == ENOTSUP && backend.live == 0U && placed_rejections == 4U);
	actual_backing.pages[0] = 0x01001000U;
	actual_backing.pages[1] = 0x01002000U;
	placed_request(&request);
	request.placement.alignment = 8192U;
	error = placed_call(&opened, &request);
	assert(error == ENOTSUP && backend.live == 0U && placed_rejections == 5U);

	/* Contiguity cannot interpret address-space wraparound as adjacent physical backing. */
	actual_backing.pages[0] = UINT64_MAX - 4095U;
	actual_backing.pages[1] = 0U;
	placed_request(&request);
	request.placement.flags = GPU_PLACEMENT_CONTIGUOUS;
	error = placed_call(&opened, &request);
	assert(error == ENOTSUP && backend.live == 0U && placed_rejections == 6U);

	/* A kernel copyout failure destroys a successful placed object before publishing any handle. */
	actual_backing.pages[0] = 0x01000000U;
	actual_backing.pages[1] = 0x01001000U;
	placed_request(&request);
	reject_copyout = 1U;
	error = placed_call(&opened, &request);
	assert(error == EFAULT && backend.live == 0U);

	/* An old backend honestly rejects nonzero physical requirements before creating any object. */
	live = allocations;
	placed_request(&request);
	error = placed_call(&legacy, &request);
	assert(error == ENOTSUP && allocations == live && legacy_backend.created == 0U);

	/* Zero conditions use the old callback, retaining its real cleanup and 64-byte publication framing. */
	placed_request(&request);
	memset(&request.placement, 0, sizeof(request.placement));
	calls = placed_calls;
	error = placed_call(&legacy, &request);
	assert(error == 0 && request.blob.size == sizeof(request) && placed_calls == calls);
	error = destroy_handle(&legacy, request.blob.handle);
	assert(error == 0);

	/* The original request still uses exactly 40 bytes and cannot acquire a hidden placement extension. */
	memset(&original, 0, sizeof(original));
	original.version = GPU_ABI_VERSION;
	original.size = sizeof(original);
	original.bytes = 8192U;
	original.flags = GPU_BLOB_MAPPABLE;
	error = cdev_file_ops.ioctl(&opened.file, GPU_BLOB_CREATE, (uintptr_t)&original);
	assert(error == 0 && original.size == 40U && placed_calls == calls);
	error = destroy_handle(&opened, original.handle);
	assert(error == 0);

	/* Unsupported flags and non-power-of-two alignment are malformed input, not backing rejection. */
	placed_request(&request);
	request.placement.flags |= 8U;
	error = placed_call(&opened, &request);
	assert(error == EINVAL && placed_calls == calls);
	placed_request(&request);
	request.placement.alignment = 3U;
	error = placed_call(&opened, &request);
	assert(error == EINVAL && placed_calls == calls);

	/* Every success and rejected candidate releases its actual ordinary backing before driver retirement. */
	close_file(&legacy);
	close_file(&opened);
	error = drv_gpu_unregister(legacy_device);
	assert(error == 0);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	assert(backend.created == backend.destroyed);
	assert(legacy_backend.created == legacy_backend.destroyed);
	assert(allocations == 0U && held_spinlocks == 0U);
	puts("GPU placement: actual page endpoints, contiguity, coherence, base alignment, failed-candidate and copyout cleanup, unsupported backend, legacy40/new64 PASS");

	/* Succeeded: no successful or rejected physical candidate survived final driver teardown. */
	return 0;
}

/* Allocates first, then checks independent backing facts before publishing a placed blob. */
static int
placed_blob(
	void *opaque,
	void *session,
	const struct gpu_blob_create_placed *request,
	void **result,
	uint32_t *resource_id)
{
	struct gpu_blob_create ordinary;
	struct test_resource *storage;
	void *candidate;
	uint32_t identifier;
	int error;

	/* Failure never leaves an object or protocol ID for the common core to guess how to release. */
	*result = NULL;
	*resource_id = 0U;
	assert(held_spinlocks == 0U);
	assert(request->blob.size == sizeof(*request));
	assert(request->blob.handle != 0U);
	placed_calls++;

	/* The actual owned storage is separate from the requested physical properties. */
	ordinary = request->blob;
	ordinary.size = sizeof(ordinary);
	error = backend_blob(opaque, session, &ordinary, &candidate, &identifier);
	if (error != 0)
		return error;

	/* Actual retained bytes cover both independently described DMA pages. */
	storage = candidate;
	storage->mapped_data = calloc(1U, 8192U);
	if (storage->mapped_data == NULL) {
		backend_destroy(opaque, session, candidate);
		return ENOMEM;
	}

	/* Any unsatisfied requirement retires the candidate exactly once within the failed callback. */
	error = placed_backing_validate(&request->placement);
	if (error != 0) {
		placed_rejections++;
		backend_destroy(opaque, session, candidate);
		return error;
	}

	/* Succeeded: this concrete backing satisfies every caller-supplied condition. */
	*result = candidate;
	*resource_id = identifier;
	return 0;
}

/* Checks the independent two-page DMA description rather than echoing requested flags. */
static int
placed_backing_validate(
	const struct gpu_placement *placement)
{
	uint64_t end;
	unsigned index;

	/* Physical base alignment applies to the first actual DMA address, not a CPU virtual pointer. */
	if (placement->alignment > 1U &&
	    (actual_backing.pages[0] & (placement->alignment - 1U)) != 0U)
		return ENOTSUP;

	/* Coherence is an independently supplied allocation property. */
	if ((placement->flags & GPU_PLACEMENT_COHERENT) != 0U && actual_backing.coherent == 0U)
		return ENOTSUP;

	/* Every page endpoint must fit the inclusive address restriction and DMA32 requirement. */
	for (index = 0U; index < 2U; index++) {
		/* Addition cannot wrap a physical endpoint into the allowed low-address interval. */
		if (actual_backing.pages[index] > UINT64_MAX - 4095U)
			return ENOTSUP;

		/* The optional general address bound is independent from the conventional DMA32 flag. */
		end = actual_backing.pages[index] + 4095U;
		if (placement->max_dma_address != 0U && end > placement->max_dma_address)
			return ENOTSUP;

		/* All pages, including the tail of the final page, must remain below four GiB. */
		if ((placement->flags & GPU_PLACEMENT_DMA32) != 0U && end > UINT32_MAX)
			return ENOTSUP;

		/* Physical contiguity cannot wrap the address space into an apparently adjacent page. */
		if ((placement->flags & GPU_PLACEMENT_CONTIGUOUS) != 0U && index != 0U) {
			/* A previous page at the final address-space boundary has no representable successor. */
			if (actual_backing.pages[index - 1U] > UINT64_MAX - 4096U)
				return ENOTSUP;

			/* Only adjacent actual page bases establish the requested contiguity. */
			if (actual_backing.pages[index] != actual_backing.pages[index - 1U] + 4096U)
				return ENOTSUP;
		}
	}

	/* Succeeded: the independently described backing meets all requested conditions. */
	return 0;
}

/* Initializes one complete placed request with independently testable physical requirements. */
static void
placed_request(
	struct gpu_blob_create_placed *request)
{
	/* The new command identifies its full 64-byte extent through the unchanged header prefix. */
	memset(request, 0, sizeof(*request));
	request->blob.version = GPU_ABI_VERSION;
	request->blob.size = sizeof(*request);
	request->blob.bytes = 8192U;
	request->blob.flags = GPU_BLOB_MAPPABLE;
	request->placement.flags = GPU_PLACEMENT_DMA32 | GPU_PLACEMENT_CONTIGUOUS | GPU_PLACEMENT_COHERENT;
	request->placement.alignment = 4096U;

	/* Succeeded: the core receives no stale output identity or implicit user pointer. */
	return;
}

/* Invokes actual cdev dispatch so validation and rollback stay in the production core. */
static int
placed_call(
	struct test_file *opened,
	struct gpu_blob_create_placed *request)
{
	int error;

	/* No test helper substitutes for the real GPU request boundary. */
	error = cdev_file_ops.ioctl(&opened->file, GPU_BLOB_CREATE_PLACED, (uintptr_t)request);
	if (error != 0)
		return error;

	/* Succeeded: the new blob handle is published through the real session table. */
	return 0;
}
