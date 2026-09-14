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

/* Maps a driver errno to the VkResult the reply carries to libvulkan. */
static uint32_t
i915_vk_result(
	int error)
{
	/* Success and the failures this module raises are the results it carries. */
	if (error == 0)
		return 0U;			/* VK_SUCCESS */
	if (error == ENOMEM)
		return (uint32_t)(-2);		/* VK_ERROR_OUT_OF_DEVICE_MEMORY */
	return (uint32_t)(-3);			/* VK_ERROR_INITIALIZATION_FAILED */
}

/* Object destructors, wrapped to a common shape for the shared destroy decode. */
static void
i915_vk_res_free_memory_obj(void *object)
{
	i915_vk_memory_free(object);
}

static void
i915_vk_res_destroy_buffer_obj(void *object)
{
	i915_vk_buffer_destroy(object);
}

static void
i915_vk_res_destroy_image_obj(void *object)
{
	i915_vk_image_destroy(object);
}

/* Buffer and image binders, wrapped to a common shape for the shared bind decode. */
static int
i915_vk_res_bind_buffer_obj(void *resource, struct i915_vk_memory *memory, uint64_t offset)
{
	return i915_vk_buffer_bind(resource, memory, offset);
}

static int
i915_vk_res_bind_image_obj(void *resource, struct i915_vk_memory *memory, uint64_t offset)
{
	return i915_vk_image_bind(resource, memory, offset);
}

/* Skips the one native extension a create/allocate chain may carry (present flag set). */
static void
i915_vk_res_skip_extension(
	struct i915_vk_reader *reader,
	uint64_t present)
{
	if (present != 0U) {
		(void)i915_vk_read_u32(reader);		/* extension sType */
		(void)i915_vk_read_u64(reader);		/* extension pNext present */
		(void)i915_vk_read_u32(reader);		/* extension value */
	}
}

/*
 * vkAllocateMemory: [device][pAllocateInfo present][sType][ext present(+ext)]
 * [allocationSize][memoryTypeIndex][pAllocator present][pMemory present]
 * [memory wire_id].  The reply is result then, on success, present and identity.
 */
static int
i915_vk_res_allocate_memory(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_memory *memory;
	uint64_t allocation_size;
	i915_vk_handle mem_handle;
	uint32_t stype;
	uint32_t memory_type_index;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_u64(reader);			/* pAllocateInfo present */
	stype = i915_vk_read_u32(reader);		/* VkStructureType */
	i915_vk_res_skip_extension(reader, i915_vk_read_u64(reader));
	allocation_size = i915_vk_read_u64(reader);
	memory_type_index = i915_vk_read_u32(reader);
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	(void)i915_vk_read_u64(reader);			/* pMemory present */
	mem_handle = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	/* VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO is the only accepted head. */
	if (stype != 5U)
		return EINVAL;

	/* The memory type index selects host visibility on real parts; not yet used. */
	(void)memory_type_index;

	error = i915_vk_memory_alloc(session, allocation_size, 0U, &memory);
	if (error == 0) {
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_MEMORY, mem_handle, memory);
		if (error != 0)
			i915_vk_memory_free(memory);
	}

	i915_vk_reply_u32(reply, i915_vk_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);		/* present */
		i915_vk_reply_u64(reply, mem_handle);	/* identifier */
	}
	return 0;
}

/*
 * vkCreateBuffer: [device][pCreateInfo present] VkBufferCreateInfo
 * [pAllocator present][pBuffer present][buffer wire_id].  VkBufferCreateInfo is
 * [sType][ext present(+ext)][flags][size][usage][sharingMode][qfiCount][count]
 * [count x qfi].  The reply is result then, on success, present and identity.
 */
static int
i915_vk_res_create_buffer(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_buffer *buffer;
	uint64_t size;
	uint64_t queue_family_count;
	uint64_t index;
	i915_vk_handle buffer_handle;
	uint32_t stype;
	uint32_t usage;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_u64(reader);			/* pCreateInfo present */
	stype = i915_vk_read_u32(reader);		/* VkStructureType */
	i915_vk_res_skip_extension(reader, i915_vk_read_u64(reader));
	(void)i915_vk_read_u32(reader);			/* flags */
	size = i915_vk_read_u64(reader);
	usage = i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);			/* sharingMode */
	(void)i915_vk_read_u32(reader);			/* queueFamilyIndexCount */
	queue_family_count = i915_vk_read_u64(reader);
	for (index = 0U; index < queue_family_count; index++)
		(void)i915_vk_read_u32(reader);		/* pQueueFamilyIndices */
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	(void)i915_vk_read_u64(reader);			/* pBuffer present */
	buffer_handle = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	/* VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO is the only accepted head. */
	if (stype != 12U)
		return EINVAL;

	error = i915_vk_buffer_create(session, size, usage, &buffer);
	if (error == 0) {
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_BUFFER, buffer_handle, buffer);
		if (error != 0)
			i915_vk_buffer_destroy(buffer);
	}

	i915_vk_reply_u32(reply, i915_vk_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);
		i915_vk_reply_u64(reply, buffer_handle);
	}
	return 0;
}

/*
 * vkCreateImage: [device][pCreateInfo present] VkImageCreateInfo
 * [pAllocator present][pImage present][image wire_id].  VkImageCreateInfo is
 * [sType][ext present(+ext)][flags][imageType][format][extent w,h,d][mipLevels]
 * [arrayLayers][samples][tiling][usage][sharingMode][qfiCount][count][count x qfi]
 * [initialLayout].  The reply is result then, on success, present and identity.
 */
static int
i915_vk_res_create_image(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_image_info info;
	struct i915_vk_image *image;
	uint64_t queue_family_count;
	uint64_t index;
	i915_vk_handle image_handle;
	uint32_t stype;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t tiling;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_u64(reader);			/* pCreateInfo present */
	stype = i915_vk_read_u32(reader);		/* VkStructureType */
	i915_vk_res_skip_extension(reader, i915_vk_read_u64(reader));
	(void)i915_vk_read_u32(reader);			/* flags */
	(void)i915_vk_read_u32(reader);			/* imageType */
	format = i915_vk_read_u32(reader);
	width = i915_vk_read_u32(reader);		/* extent.width */
	height = i915_vk_read_u32(reader);		/* extent.height */
	(void)i915_vk_read_u32(reader);			/* extent.depth */
	(void)i915_vk_read_u32(reader);			/* mipLevels */
	(void)i915_vk_read_u32(reader);			/* arrayLayers */
	(void)i915_vk_read_u32(reader);			/* samples */
	tiling = i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);			/* usage */
	(void)i915_vk_read_u32(reader);			/* sharingMode */
	(void)i915_vk_read_u32(reader);			/* queueFamilyIndexCount */
	queue_family_count = i915_vk_read_u64(reader);
	for (index = 0U; index < queue_family_count; index++)
		(void)i915_vk_read_u32(reader);		/* pQueueFamilyIndices */
	(void)i915_vk_read_u32(reader);			/* initialLayout */
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	(void)i915_vk_read_u64(reader);			/* pImage present */
	image_handle = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	/* VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO is the only accepted head. */
	if (stype != 14U)
		return EINVAL;

	info.width = width;
	info.height = height;
	info.format = format;
	info.tiling = tiling;
	error = i915_vk_image_create(session, &info, &image);
	if (error == 0) {
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_IMAGE, image_handle, image);
		if (error != 0)
			i915_vk_image_destroy(image);
	}

	i915_vk_reply_u32(reply, i915_vk_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);
		i915_vk_reply_u64(reply, image_handle);
	}
	return 0;
}

/*
 * vkBindBufferMemory/vkBindImageMemory: [device][resource][memory][offset].
 * The reply is the VkResult alone.
 */
static int
i915_vk_res_bind(
	struct i915_vk_session *session,
	enum i915_vk_object_kind resource_kind,
	int (*bind)(void *, struct i915_vk_memory *, uint64_t),
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_memory *memory;
	void *resource;
	uint64_t offset;
	i915_vk_handle resource_handle;
	i915_vk_handle memory_handle;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	resource_handle = i915_vk_read_handle(reader);
	memory_handle = i915_vk_read_handle(reader);
	offset = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Both objects must already exist in the session's table to bind. */
	resource = i915_vk_obj_lookup(session->vk, resource_kind, resource_handle);
	memory = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_MEMORY, memory_handle);
	if (resource == NULL || memory == NULL)
		error = EINVAL;
	else
		error = bind(resource, memory, offset);

	i915_vk_reply_u32(reply, i915_vk_result(error));
	return 0;
}

/*
 * vkFreeMemory/vkDestroyBuffer/vkDestroyImage: [device][object][pAllocator present].
 * These return void, so the reply is the echoed opcode alone.
 */
static int
i915_vk_res_destroy(
	struct i915_vk_session *session,
	enum i915_vk_object_kind kind,
	void (*destroy)(void *),
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	void *object;
	i915_vk_handle handle;

	(void)i915_vk_read_handle(reader);		/* device */
	handle = i915_vk_read_handle(reader);
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	if (reader->error != 0)
		return EINVAL;

	/* An unknown handle destroys nothing; the wire may retire it more than once. */
	object = i915_vk_obj_lookup(session->vk, kind, handle);
	if (object != NULL) {
		i915_vk_obj_remove(session->vk, kind, handle);
		destroy(object);
	}

	/* A void command has no result or payload beyond the echoed opcode. */
	(void)reply;
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
	 * Memory, buffers and images are wired end to end (p011 increment A);
	 * samplers and descriptors follow and use the module functions above.
	 */
	switch (opcode) {
	case 21U:	/* vkAllocateMemory */
		return i915_vk_res_allocate_memory(session, reader, reply);
	case 22U:	/* vkFreeMemory */
		return i915_vk_res_destroy(session, I915_VK_OBJ_MEMORY,
			i915_vk_res_free_memory_obj, reader, reply);
	case 28U:	/* vkBindBufferMemory */
		return i915_vk_res_bind(session, I915_VK_OBJ_BUFFER,
			i915_vk_res_bind_buffer_obj, reader, reply);
	case 29U:	/* vkBindImageMemory */
		return i915_vk_res_bind(session, I915_VK_OBJ_IMAGE,
			i915_vk_res_bind_image_obj, reader, reply);
	case 50U:	/* vkCreateBuffer */
		return i915_vk_res_create_buffer(session, reader, reply);
	case 51U:	/* vkDestroyBuffer */
		return i915_vk_res_destroy(session, I915_VK_OBJ_BUFFER,
			i915_vk_res_destroy_buffer_obj, reader, reply);
	case 54U:	/* vkCreateImage */
		return i915_vk_res_create_image(session, reader, reply);
	case 55U:	/* vkDestroyImage */
		return i915_vk_res_destroy(session, I915_VK_OBJ_IMAGE,
			i915_vk_res_destroy_image_obj, reader, reply);
	default:
		return EINVAL;
	}
}
