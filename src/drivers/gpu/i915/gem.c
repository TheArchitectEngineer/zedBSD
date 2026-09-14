/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * GEM objects: physically contiguous backing with a CPU view and GPU
 * bindings.
 *
 * Every object is one contiguous run of RAM below the GPU's 39-bit
 * limit, so a binding is a straight run of page table entries. The
 * CPU view is the kernel's direct map; Alder Lake keeps GPU and CPU
 * coherent through the LLC, so no cache maintenance is issued here.
 */

#include "internal.h"

#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>

#include <errno.h>
#include <string.h>

/*
 * Allocates a zeroed contiguous object of the requested size.
 */
int
drv_i915_gem_create(
	struct i915_device *device,
	uint64_t bytes,
	struct i915_gem_object **result)
{
	struct i915_gem_object *object;
	uint64_t rounded;
	int error;

	/* A failed creation transfers nothing. */
	*result = NULL;

	/* Objects are whole pages; the size limit bounds the contiguous allocation. */
	if (bytes == 0U || bytes > I915_MAX_RESOURCE_BYTES)
		return EINVAL;
	rounded = (bytes + I915_PAGE_BYTES - 1U) & ~((uint64_t)I915_PAGE_BYTES - 1U);

	object = kern_calloc(1U, sizeof(*object));
	if (object == NULL)
		return ENOMEM;

	/* The run must be reachable by the GPU's address bits. */
	error = kern_pmem_alloc_limited((size_t)rounded, I915_PAGE_BYTES, I915_DMA_MAX_ADDRESS, 0U, &object->run);
	if (error != 0) {
		kern_free(object);
		return error;
	}

	/* The direct map gives the CPU view; a run outside managed RAM cannot be used. */
	object->address = kern_pmem_to_kernel(object->run.paddr);
	if (object->address == NULL) {
		(void)kern_pmem_free(&object->run);
		kern_free(object);
		return EFAULT;
	}

	/* Zeroing keeps previous owners' data out of a new resource. */
	memset(object->address, 0, (size_t)rounded);
	object->bytes = rounded;
	object->pages = (unsigned)(rounded / I915_PAGE_BYTES);

	/* The device list lets detach and reset find every live object. */
	object->next = device->objects;
	device->objects = object;
	device->object_count++;
	*result = object;

	/* Succeeded: the caller owns an unbound zeroed object. */
	return 0;
}

/*
 * Releases an object after its bindings retired; a quarantined object is kept.
 */
void
drv_i915_gem_destroy(
	struct i915_device *device,
	struct i915_gem_object *object)
{
	struct i915_gem_object **position;

	/* A binding that is still live is a driver bug; the object is retained, not freed. */
	if (object->ggtt_pages != 0U || object->vm != NULL) {
		kern_logf("i915: object destroyed while bound; retained\n");
		object->quarantined = 1U;
	}

	/* Hardware may still address a quarantined object until the next checked reset. */
	if (object->quarantined != 0U) {
		device->quarantined_objects++;
		return;
	}

	/* Unlinks the object from the device list. */
	position = &device->objects;
	while (*position != NULL && *position != object)
		position = &(*position)->next;
	if (*position == object) {
		*position = object->next;
		device->object_count--;
	}

	/* The backing returns to the pool only after no GPU mapping names it. */
	(void)kern_pmem_free(&object->run);
	kern_free(object);
}

/*
 * Maps the object into the global GTT for kernel-owned structures.
 */
int
drv_i915_gem_bind_ggtt(
	struct i915_device *device,
	struct i915_gem_object *object)
{
	uint32_t offset;
	int error;

	/* An object is bound to the GGTT at most once. */
	if (object->ggtt_pages != 0U)
		return EBUSY;

	/* The run of table entries comes from the bitmap allocator. */
	error = drv_i915_ggtt_alloc(device, object->pages, &offset);
	if (error != 0)
		return error;

	/* The entries name the contiguous backing pages. */
	error = drv_i915_ggtt_insert(device, offset, (uint64_t)object->run.paddr, object->pages);
	if (error != 0) {
		drv_i915_ggtt_free(device, offset, object->pages);
		return error;
	}

	object->ggtt_offset = offset;
	object->ggtt_pages = object->pages;

	/* Succeeded: the GPU reaches the object at ggtt_offset. */
	return 0;
}

/*
 * Removes the object's global GTT mapping.
 */
void
drv_i915_gem_unbind_ggtt(
	struct i915_device *device,
	struct i915_gem_object *object)
{
	/* An unbound object has no entries to release. */
	if (object->ggtt_pages == 0U)
		return;

	/* Freeing clears the entries before the run is reused. */
	drv_i915_ggtt_free(device, object->ggtt_offset, object->ggtt_pages);
	object->ggtt_offset = 0U;
	object->ggtt_pages = 0U;
}

/*
 * Maps the object into one session's private address space.
 */
int
drv_i915_gem_bind_vm(
	struct i915_ppgtt *vm,
	struct i915_gem_object *object)
{
	uint64_t va;
	int error;

	/* An object belongs to at most one address space. */
	if (object->vm != NULL)
		return EBUSY;

	/* Each object gets its own aligned range; ranges are never reused in v1. */
	error = drv_i915_ppgtt_va_alloc(vm, object->bytes, &va);
	if (error != 0)
		return error;

	/* The page tables grow as needed to cover the run. */
	error = drv_i915_ppgtt_insert(vm, va, (uint64_t)object->run.paddr, object->pages);
	if (error != 0)
		return error;

	object->vm = vm;
	object->va = va;

	/* Succeeded: GPU commands in this context address the object at va. */
	return 0;
}

/*
 * Removes the object's private address space mapping.
 */
void
drv_i915_gem_unbind_vm(
	struct i915_gem_object *object)
{
	/* An object without a context mapping has nothing to clear. */
	if (object->vm == NULL)
		return;

	/* Scratch entries replace the pages so a stale command reads zeros. */
	drv_i915_ppgtt_clear(object->vm, object->va, object->pages);
	object->vm = NULL;
	object->va = 0U;
}

/*
 * Copies bytes out of the object through its CPU view.
 */
int
drv_i915_gem_read(
	struct i915_gem_object *object,
	uint64_t offset,
	void *buffer,
	uint32_t bytes)
{
	const uint8_t *source;

	/* The range must lie inside the object even though the core checked its own view. */
	if (offset > object->bytes || bytes > object->bytes - offset)
		return EINVAL;

	/* GPU writes are visible through the LLC; the barrier orders against later reads. */
	kern_io_read_barrier();
	source = object->address;
	memcpy(buffer, source + offset, bytes);

	/* Succeeded: the caller's buffer holds a snapshot of the object. */
	return 0;
}

/*
 * Copies bytes into the object through its CPU view.
 */
int
drv_i915_gem_write(
	struct i915_gem_object *object,
	uint64_t offset,
	const void *buffer,
	uint32_t bytes)
{
	uint8_t *destination;

	/* The range must lie inside the object. */
	if (offset > object->bytes || bytes > object->bytes - offset)
		return EINVAL;

	/* The barrier publishes the bytes before a later submission can consume them. */
	destination = object->address;
	memcpy(destination + offset, buffer, bytes);
	kern_io_write_barrier();

	/* Succeeded: the GPU sees the new contents on its next access. */
	return 0;
}
