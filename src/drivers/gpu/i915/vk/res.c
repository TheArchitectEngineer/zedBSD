/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Resource model: Vulkan memory, buffers, images, samplers and descriptors
 * mapped onto WS029 GEM objects and the session PPGTT.
 *
 * The object lifetime and address-space binding are exercised by the host
 * fixture.  The Gen12 RENDER_SURFACE_STATE / SAMPLER_STATE dword layouts are
 * hardware encodings: this module fills them best-effort from the public Intel
 * PRM and marks them for verification on real hardware (WS031 p011) and for a
 * source-audited transcription once Mesa genxml is available.  See the surface
 * and sampler state helpers below.
 */

#include "vk-internal.h"
#include "res.h"
#include "cmd.h"

#include "../internal.h"

#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>

#include <errno.h>
#include <string.h>

#include "linux/surface-state-gen12.inc"

/* One Vulkan device memory allocation backed by a GEM object. */
struct i915_vk_memory {
	struct i915_vk_session *session;
	struct i915_gem_object *object;
	void *cpu;
	uint64_t size;
};

/* A buffer names a memory range the GPU addresses through the PPGTT. */
struct i915_vk_buffer {
	struct i915_vk_memory *memory;
	uint64_t offset;
	uint64_t size;
	uint32_t usage;
};

/* An image carries its dimensions, format and a surface state. */
struct i915_vk_image {
	struct i915_vk_memory *memory;
	uint64_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t tiling;
	uint32_t surface_state[16];
};

/* An image view selects a format over an image and holds its surface state. */
struct i915_vk_image_view {
	struct i915_vk_image *image;
	uint32_t format;
	uint32_t surface_state[16];
};

/* A sampler holds a Gen12 sampler state. */
struct i915_vk_sampler {
	uint32_t sampler_state[4];
};

/* The most bindings a baseline descriptor set carries. */
#define I915_VK_MAX_BINDINGS 16U

/* A descriptor set layout records its bindings and their types. */
struct i915_vk_dsl {
	uint32_t count;
	struct i915_vk_dsl_binding bindings[I915_VK_MAX_BINDINGS];
};

/* A descriptor pool bounds how many sets it hands out. */
struct i915_vk_dpool {
	uint32_t max_sets;
	uint32_t used;
};

/* A descriptor set holds the view and sampler bound to each binding. */
struct i915_vk_dset {
	struct i915_vk_dpool *pool;
	struct i915_vk_dsl *layout;
	struct i915_vk_write_dset entries[I915_VK_MAX_BINDINGS];
	uint32_t count;
};

static void i915_vk_surface_state(struct i915_vk_image *image, uint32_t format, uint32_t *state);

/* Allocates device memory as a GEM object bound into the session address space. */
int
i915_vk_memory_alloc(
	struct i915_vk_session *session,
	uint64_t size,
	uint32_t flags,
	struct i915_vk_memory **out)
{
	struct i915_device *device;
	struct i915_vk_memory *memory;
	int error;

	/* The caller receives nothing on failure. */
	(void)flags;
	*out = NULL;
	device = session->vk->i915;

	memory = kern_calloc(1U, sizeof(*memory));
	if (memory == NULL)
		return ENOMEM;
	memory->session = session;
	memory->size = size;

	/* GEM allocation and address-space binding share the controller mutex. */
	mutex_lock(&device->mutex);

	error = drv_i915_gem_create(device, size, &memory->object);
	if (error != 0) {
		mutex_unlock(&device->mutex);
		kern_free(memory);
		return error;
	}

	error = drv_i915_gem_bind_vm(session->gpu->vm, memory->object);
	if (error != 0) {
		drv_i915_gem_destroy(device, memory->object);
		mutex_unlock(&device->mutex);
		kern_free(memory);
		return error;
	}

	mutex_unlock(&device->mutex);

	/* Managed RAM is direct-mapped, so a host-visible view is always available. */
	memory->cpu = kern_pmem_to_kernel(memory->object->run.paddr);

	/* Succeeded: the memory can back buffers and images. */
	*out = memory;
	return 0;
}

/* Releases device memory and its address-space binding. */
void
i915_vk_memory_free(
	struct i915_vk_memory *memory)
{
	struct i915_device *device;

	/* Memory that never allocated is nothing to release. */
	if (memory == NULL)
		return;
	device = memory->session->vk->i915;

	mutex_lock(&device->mutex);
	drv_i915_gem_unbind_vm(memory->object);
	drv_i915_gem_destroy(device, memory->object);
	mutex_unlock(&device->mutex);

	kern_free(memory);
}

/* Reports the host-visible CPU mapping of a memory allocation. */
int
i915_vk_memory_map(
	struct i915_vk_memory *memory,
	void **cpu)
{
	/* Only managed, direct-mapped memory has a CPU view. */
	if (memory->cpu == NULL)
		return ENOTSUP;

	*cpu = memory->cpu;
	return 0;
}

/* Creates a buffer that will name a memory range. */
int
i915_vk_buffer_create(
	struct i915_vk_session *session,
	uint64_t size,
	uint32_t usage,
	struct i915_vk_buffer **out)
{
	struct i915_vk_buffer *buffer;

	/* The buffer object is empty until it binds a memory range. */
	(void)session;
	*out = NULL;

	buffer = kern_calloc(1U, sizeof(*buffer));
	if (buffer == NULL)
		return ENOMEM;
	buffer->size = size;
	buffer->usage = usage;

	/* Succeeded: the buffer can bind memory. */
	*out = buffer;
	return 0;
}

/* Binds a buffer to a memory range. */
int
i915_vk_buffer_bind(
	struct i915_vk_buffer *buffer,
	struct i915_vk_memory *memory,
	uint64_t offset)
{
	buffer->memory = memory;
	buffer->offset = offset;
	return 0;
}

/* Releases a buffer; its memory is owned separately. */
void
i915_vk_buffer_destroy(
	struct i915_vk_buffer *buffer)
{
	if (buffer != NULL)
		kern_free(buffer);
}

/* Creates an image with its dimensions, format and tiling. */
int
i915_vk_image_create(
	struct i915_vk_session *session,
	const struct i915_vk_image_info *info,
	struct i915_vk_image **out)
{
	struct i915_vk_image *image;

	/* The caller receives nothing on failure. */
	(void)session;
	*out = NULL;

	image = kern_calloc(1U, sizeof(*image));
	if (image == NULL)
		return ENOMEM;
	image->width = info->width;
	image->height = info->height;
	image->format = info->format;
	image->tiling = info->tiling;

	/* The surface state describes the image to the sampler and render target. */
	i915_vk_surface_state(image, image->format, image->surface_state);

	/* Succeeded: the image can bind memory and be viewed. */
	*out = image;
	return 0;
}

/* Binds an image to a memory range. */
int
i915_vk_image_bind(
	struct i915_vk_image *image,
	struct i915_vk_memory *memory,
	uint64_t offset)
{
	uint64_t base;

	image->memory = memory;
	image->offset = offset;

	/* The surface base address names the image's page in the session address space. */
	base = memory->object->va + offset;
	image->surface_state[GEN12_SURFACE_BASE_LO_DWORD] = (uint32_t)base;
	image->surface_state[GEN12_SURFACE_BASE_HI_DWORD] = (uint32_t)(base >> 32);
	return 0;
}

/* Releases an image; its memory is owned separately. */
void
i915_vk_image_destroy(
	struct i915_vk_image *image)
{
	if (image != NULL)
		kern_free(image);
}

/* Creates a view that selects a format over an image. */
int
i915_vk_image_view_create(
	struct i915_vk_session *session,
	struct i915_vk_image *image,
	uint32_t format,
	struct i915_vk_image_view **out)
{
	struct i915_vk_image_view *view;

	(void)session;
	*out = NULL;

	view = kern_calloc(1U, sizeof(*view));
	if (view == NULL)
		return ENOMEM;
	view->image = image;
	view->format = format;

	/* The view inherits the image's surface state, including its base address. */
	memcpy(view->surface_state, image->surface_state, sizeof(view->surface_state));

	/* The view selects its own format in dword 0 without disturbing the rest. */
	view->surface_state[0] &= ~(GEN12_SURFACE_FORMAT_MASK << GEN12_SURFACE_FORMAT_SHIFT);
	view->surface_state[0] |= (format & GEN12_SURFACE_FORMAT_MASK) << GEN12_SURFACE_FORMAT_SHIFT;

	*out = view;
	return 0;
}

/* Releases an image view. */
void
i915_vk_image_view_destroy(
	struct i915_vk_image_view *view)
{
	if (view != NULL)
		kern_free(view);
}

/* Returns an image view's Gen12 surface state dwords. */
const uint32_t *
i915_vk_image_surface_state(
	const struct i915_vk_image_view *view)
{
	return view->surface_state;
}

/*
 * Fills a Gen12 RENDER_SURFACE_STATE for an image.  The exact bit layout is a
 * hardware encoding from the Intel PRM (Volume: Render Engine, RENDER_SURFACE_
 * STATE).  This best-effort fill records the fields the driver knows (type,
 * format, extent, tiling, base address at bind time) and is marked for real
 * hardware verification and a source-audited transcription.
 */
static void
i915_vk_surface_state(
	struct i915_vk_image *image,
	uint32_t format,
	uint32_t *state)
{
	uint32_t tile;
	uint32_t pitch;

	/* Unwritten fields stay zero; the base address is filled at bind. */
	memset(state, 0, GEN12_SURFACE_STATE_DWORDS * sizeof(uint32_t));

	/* Dword 0: a 2D surface of the given hardware format and tiling. */
	tile = image->tiling != 0U ? GEN12_TILE_YMAJOR : GEN12_TILE_LINEAR;
	state[0] = (GEN12_SURFTYPE_2D << GEN12_SURFACE_TYPE_SHIFT)
		| ((format & GEN12_SURFACE_FORMAT_MASK) << GEN12_SURFACE_FORMAT_SHIFT)
		| (tile << GEN12_SURFACE_TILE_MODE_SHIFT);

	/* Dword 2: the extent, stored as one less than each dimension. */
	state[2] = ((image->width - 1U) & GEN12_SURFACE_WIDTH_MASK)
		| (((image->height - 1U) & GEN12_SURFACE_HEIGHT_MASK) << GEN12_SURFACE_HEIGHT_SHIFT);

	/* Dword 3: the row pitch in bytes minus one, four bytes per pixel. */
	pitch = image->width * 4U;
	state[3] = (pitch - 1U) & GEN12_SURFACE_PITCH_MASK;
}

/* Creates a sampler with a Gen12 sampler state. */
int
i915_vk_sampler_create(
	struct i915_vk_session *session,
	const struct i915_vk_sampler_info *info,
	struct i915_vk_sampler **out)
{
	struct i915_vk_sampler *sampler;

	(void)session;
	(void)info;
	*out = NULL;

	sampler = kern_calloc(1U, sizeof(*sampler));
	if (sampler == NULL)
		return ENOMEM;

	/*
	 * The SAMPLER_STATE dword layout is a hardware encoding (Intel PRM,
	 * SAMPLER_STATE).  vkdemo samples nearest, so the fields stay at their
	 * nearest-filter defaults pending a PRM-audited transcription.
	 */
	memset(sampler->sampler_state, 0, sizeof(sampler->sampler_state));

	*out = sampler;
	return 0;
}

/* Releases a sampler. */
void
i915_vk_sampler_destroy(
	struct i915_vk_sampler *sampler)
{
	if (sampler != NULL)
		kern_free(sampler);
}

/* Returns a sampler's Gen12 sampler state dwords. */
const uint32_t *
i915_vk_sampler_state(
	const struct i915_vk_sampler *sampler)
{
	return sampler->sampler_state;
}

/* Creates a descriptor set layout from its bindings. */
int
i915_vk_dsl_create(
	struct i915_vk_session *session,
	const void *bindings,
	uint32_t count,
	struct i915_vk_dsl **out)
{
	const struct i915_vk_dsl_binding *source;
	struct i915_vk_dsl *dsl;
	uint32_t index;

	/* The caller receives nothing on failure. */
	(void)session;
	*out = NULL;

	/* A baseline layout is bounded so a set fits a fixed binding table. */
	if (count > I915_VK_MAX_BINDINGS)
		return EINVAL;

	dsl = kern_calloc(1U, sizeof(*dsl));
	if (dsl == NULL)
		return ENOMEM;

	/* The layout copies each binding number and its descriptor type. */
	source = bindings;
	dsl->count = count;
	for (index = 0U; index < count; index++)
		dsl->bindings[index] = source[index];

	*out = dsl;
	return 0;
}

/* Releases a descriptor set layout. */
void
i915_vk_dsl_destroy(
	struct i915_vk_dsl *dsl)
{
	if (dsl != NULL)
		kern_free(dsl);
}

/* Creates a descriptor pool that hands out a bounded number of sets. */
int
i915_vk_dpool_create(
	struct i915_vk_session *session,
	uint32_t max_sets,
	struct i915_vk_dpool **out)
{
	struct i915_vk_dpool *dpool;

	(void)session;
	*out = NULL;

	dpool = kern_calloc(1U, sizeof(*dpool));
	if (dpool == NULL)
		return ENOMEM;
	dpool->max_sets = max_sets;

	*out = dpool;
	return 0;
}

/* Releases a descriptor pool. */
void
i915_vk_dpool_destroy(
	struct i915_vk_dpool *dpool)
{
	if (dpool != NULL)
		kern_free(dpool);
}

/* Allocates a descriptor set from a pool for a layout. */
int
i915_vk_dset_alloc(
	struct i915_vk_dpool *dpool,
	struct i915_vk_dsl *dsl,
	struct i915_vk_dset **out)
{
	struct i915_vk_dset *dset;

	/* The pool refuses a set beyond the count it was created for. */
	*out = NULL;
	if (dpool->used >= dpool->max_sets)
		return ENOSPC;

	dset = kern_calloc(1U, sizeof(*dset));
	if (dset == NULL)
		return ENOMEM;
	dset->pool = dpool;
	dset->layout = dsl;
	dpool->used++;

	*out = dset;
	return 0;
}

/* Returns a descriptor set to its pool. */
void
i915_vk_dset_free(
	struct i915_vk_dset *dset)
{
	/* A freed set returns its slot so the pool can hand out another. */
	if (dset == NULL)
		return;
	if (dset->pool->used != 0U)
		dset->pool->used--;
	kern_free(dset);
}

/* Records the view and sampler bound at each written binding. */
int
i915_vk_dset_update(
	struct i915_vk_dset *dset,
	const struct i915_vk_write_dset *writes,
	uint32_t count)
{
	uint32_t index;

	/* A set holds at most one entry per binding table slot. */
	if (count > I915_VK_MAX_BINDINGS)
		return EINVAL;

	/* Each write replaces the binding's view and sampler in the set. */
	for (index = 0U; index < count; index++)
		dset->entries[index] = writes[index];
	if (count > dset->count)
		dset->count = count;

	return 0;
}

/* Routes the resource opcodes; the command decode grows with integration. */
int
i915_vk_res_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	/*
	 * The resource creation commands carry full Venus-encoded VkXxxCreateInfo
	 * structures; their precise decode is completed against libvulkan at
	 * integration (p011).  Until then the object lifetime is exercised through
	 * the module functions above and the drv_gpu blob path.
	 */
	(void)session;
	(void)opcode;
	(void)reader;
	(void)reply;
	return EINVAL;
}
