/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's instance, physical-device, device and queue commands,
 * which the driver answers itself (D).
 *
 * The device is one physical device with one queue family of one queue
 * that draws, computes and copies: a GHCI GPU has one ring.  Its limits
 * and features come from the GPU's capability registers (CAPS, CAPS2 and
 * the FMT capability) and from what the shader compiler and the firmware
 * provide; the rest are the smallest values Vulkan 1.0 allows.  The
 * memory is two types: device local, and device local, host visible and
 * coherent, over the GPU's memory the processor reaches through the
 * window (and the rest of it, which only the GPU reaches).
 *
 * vkDestroyDevice is answered here and relayed too: the firmware gives
 * back the objects it still keeps for the device.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "../hcr.h"
#include "vk.h"
#include "vk-engine.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

/*
 * The bits of the float values of the limits.
 *
 * Values referenced: IEEE 754-2008 binary32 (sign 31, biased exponent
 * 30:23 with a bias of 127, fraction 22:0).
 */
#define F32_ONE				0x3f800000U
#define F32_TWO				0x40000000U
#define F32_HALF_NEGATIVE		0xbf000000U
#define F32_0_4375			0x3ee00000U
#define F32_8192_NEGATIVE		0xc6000000U
#define F32_8191			0x45fff800U

/* The version the device reports (libvulkan needs a native 1.1) and the driver's version. */
#define DEVICE_API_VERSION		VK_MAKE_VERSION(1, 1, 0)
#define DEVICE_DRIVER_VERSION		1U

/* The one queue family and the timelines a queue may complete jobs on (1 to 63). */
#define DEVICE_FAMILIES			1U

/* The largest edge of an image and its most layers. */
#define IMAGE_EDGE			4096U
#define IMAGE_LAYERS			256U
#define IMAGE_EDGE_3D			256U
#define IMAGE_LEVELS			13U

/* The varying words a GPU without the Vulkan 1.0 core rendering of CAPS[30] keeps per vertex. */
#define VARYING_WORDS_BASIC		16U
#define VARYING_WORDS_CORE		64U

/* The most invocations of one workgroup. */
#define WORKGROUP_INVOCATIONS		128U

/* The bit of CAPS that says the GPU renders the whole of Vulkan 1.0 core (draft). */
#define CAPS_VULKAN_CORE		(1U << 30)

static int create_instance(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int destroy_instance(struct i915_wire_reader *reader);
static int enumerate(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int properties(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int identity(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static VkPhysicalDeviceProperties *device_record(struct ghci_vk_build *build);
static void fill_limits(struct ghci_device *device, VkPhysicalDeviceLimits *limit);
static void fill_features(struct ghci_device *device, VkPhysicalDeviceFeatures *features);
static int features(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int memory_properties(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int families(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int format(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int image_format(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static uint32_t image_format_limits(struct ghci_device *device, uint32_t vk_format, uint32_t type, uint32_t tiling, uint32_t usage, uint32_t create_flags, VkImageFormatProperties *record);
static int image_format2(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int buffer_external(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int sparse_formats(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int create_device(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int destroy_device(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int device_queue(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int usage_allowed(uint32_t usage, uint32_t features);
static int features_within(const VkPhysicalDeviceFeatures *asked, const VkPhysicalDeviceFeatures *offered);
static void set_float(float *destination, uint32_t bits);
static uint32_t sample_counts(const struct ghci_device *device);

/*
 * Runs an instance, physical-device, device or queue command; *handled
 * stays clear for another route's number.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/powervr/vk/vk-device.c
 * (the wire of each command).
 */
int
ghci_vk_device_route(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another route. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_INSTANCE:
		error = create_instance(build, flags, reader);
		break;
	case GPU_OP_DESTROY_INSTANCE:
		error = destroy_instance(reader);
		break;
	case GPU_OP_ENUMERATE_PHYSICAL_DEVICES:
		error = enumerate(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_FEATURES:
		error = features(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES:
		error = format(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES:
		error = image_format(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES:
		error = properties(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2:
		error = identity(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES:
		error = families(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES:
		error = memory_properties(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES2:
		error = image_format2(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES:
		error = buffer_external(build, flags, reader);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES:
		error = sparse_formats(build, flags, reader);
		break;
	case GPU_OP_CREATE_DEVICE:
		error = create_device(build, flags, reader);
		break;
	case GPU_OP_DESTROY_DEVICE:
		error = destroy_device(build, flags, reader);
		break;
	case GPU_OP_GET_DEVICE_QUEUE2:
		error = device_queue(build, flags, reader);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/* vkCreateInstance: [u64 1][create info][u64 0][u64 1][instance] -> [result][u64 1][instance]. */
static int
create_instance(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkInstanceCreateInfo info;
	uint64_t present;
	uint64_t instance;

	/* The create info when present; nothing of it is kept. */
	kern_memset(&info, 0, sizeof(info));
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkInstanceCreateInfo(reader, &build->session->arena, &info);
	instance = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Success and the identity: the instance is the session's alone. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, instance);
	}

	/* Succeeded: the instance exists. */
	return 0;
}

/* vkDestroyInstance: [instance][u64 0], no reply body. */
static int
destroy_instance(
	struct i915_wire_reader *reader)
{
	/* The instance, then the allocator. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the instance is forgotten. */
	return 0;
}

/*
 * vkEnumeratePhysicalDevices: [instance][u64 1][u32 count][u64 count]
 * [ids] -> [result][u64 1][u32 count][u64 count][ids].  There is one
 * device; the library chooses its identity.
 */
static int
enumerate(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	uint64_t array_count;
	uint64_t physical;
	uint32_t count;

	/* The instance and the count's presence word, then the counts. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	count = drv_i915_wire_read_u32(reader);
	array_count = drv_i915_wire_read_u64(reader);
	physical = 0U;
	if (array_count != 0U)
		physical = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* At most one device, asked for with room for one. */
	if (array_count > 1U)
		return EINVAL;
	if (array_count != 0U && count != 1U)
		return EINVAL;

	/* Success, one device and its identity when the array holds it. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, array_count);
		if (array_count != 0U)
			drv_i915_wire_reply_u64(&build->reply, physical);
	}

	/* Succeeded: the device is reported. */
	return 0;
}

/* vkGetPhysicalDeviceProperties: [physical][u64 1] -> [u64 1][properties]. */
static int
properties(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkPhysicalDeviceProperties *record;

	/* The physical device and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The device's record. */
	record = device_record(build);
	if (record == NULL)
		return ENOMEM;

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkPhysicalDeviceProperties(&build->reply, record);
	}

	/* Succeeded: the properties are reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceProperties2, asked for the device's ID properties
 * only: [physical][u64 1][PROPERTIES_2 sType][u64 1][ID_PROPERTIES sType]
 * [u64 0] -> [u64 1], the two headers, the device and driver UUIDs, the
 * LUID (none), the node mask, the LUID-valid word and the properties.
 * Every session of the device reports the same UUIDs.
 */
static int
identity(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	static const uint8_t driver_uuid[VK_UUID_SIZE] = {
		'z', 'e', 'd', 'B', 'S', 'D', '-', 'g', 'h', 'c', 'i', '-', 'v', 'k', '0', '1'
	};
	static const uint8_t luid[VK_LUID_SIZE] = { 0 };
	VkPhysicalDeviceProperties *record;
	uint8_t device_uuid[VK_UUID_SIZE];
	uint64_t present;
	uint64_t chained;
	uint64_t last;
	uint32_t outer;
	uint32_t inner;

	/* The physical device and the output chain: properties2, then the ID properties, then nothing. */
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	outer = drv_i915_wire_read_u32(reader);
	chained = drv_i915_wire_read_u64(reader);
	inner = drv_i915_wire_read_u32(reader);
	last = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (present != 1U)
		return EINVAL;

	/* Only the ID properties are known. */
	if (outer != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2)
		return ENOTSUP;
	if (chained != 1U)
		return ENOTSUP;
	if (inner != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES)
		return ENOTSUP;
	if (last != 0U)
		return ENOTSUP;

	/* The ordinary properties, as the plain query gives them. */
	record = device_record(build);
	if (record == NULL)
		return ENOMEM;

	/* The device's UUID: the driver's name and the GPU's vendor and device FourCCs. */
	kern_memset(device_uuid, 0, sizeof(device_uuid));
	kern_memcpy(device_uuid, "ghci", 4U);
	kern_memcpy(device_uuid + 4U, &build->session->device->vendor_id, 4U);
	kern_memcpy(device_uuid + 8U, &build->session->device->device_id, 4U);
	kern_memcpy(device_uuid + 12U, &build->session->device->device_version, 4U);

	/* The two headers, the identity (no Windows adapter: no LUID), then the properties. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
		drv_i915_wire_reply_u64(&build->reply, 0U);
		drv_i915_wire_reply_u64(&build->reply, VK_UUID_SIZE);
		i915_vkc_reply_bytes(&build->reply, device_uuid, VK_UUID_SIZE);
		drv_i915_wire_reply_u64(&build->reply, VK_UUID_SIZE);
		i915_vkc_reply_bytes(&build->reply, driver_uuid, VK_UUID_SIZE);
		drv_i915_wire_reply_u64(&build->reply, VK_LUID_SIZE);
		i915_vkc_reply_bytes(&build->reply, luid, VK_LUID_SIZE);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, VK_FALSE);
		i915_vkc_enc_VkPhysicalDeviceProperties(&build->reply, record);
	}

	/* Succeeded: the identity every session of the device reports. */
	return 0;
}

/* Fills a cleared record from the scratch with the device's properties; NULL when none is left. */
static VkPhysicalDeviceProperties *
device_record(
	struct ghci_vk_build *build)
{
	struct ghci_device *device;
	VkPhysicalDeviceProperties *record;
	size_t length;

	/* The cleared record. */
	device = build->session->device;
	record = ghci_vk_arena_take(build->session, sizeof(*record));
	if (record == NULL)
		return NULL;

	/*
	 * The device: its GHCI vendor and device FourCCs as the numbers, a
	 * discrete GPU when it has memory of its own, its name and its
	 * limits.
	 */
	record->apiVersion = DEVICE_API_VERSION;
	record->driverVersion = DEVICE_DRIVER_VERSION;
	record->vendorID = device->vendor_id;
	record->deviceID = device->device_id;
	record->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	if (device->local_size != 0U)
		record->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

	/* The name: the node's, "ghci:<vendor>:<device>". */
	length = kern_strlen(device->name);
	if (length >= VK_MAX_PHYSICAL_DEVICE_NAME_SIZE)
		length = VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1U;
	kern_memcpy(record->deviceName, device->name, length);

	/* The pipeline cache's UUID: the cache is always empty, so any constant does. */
	kern_memcpy(record->pipelineCacheUUID, "zedBSD-ghci-pc01", VK_UUID_SIZE);
	fill_limits(device, &record->limits);

	/* Succeeded: the record. */
	return record;
}

/*
 * Fills the limits: what the GPU's capability registers say, what the
 * compiler and the firmware provide, and Vulkan 1.0's smallest values for
 * the rest.
 *
 * Values referenced: the Required Limits table of the Vulkan 1.0
 * specification.
 */
static void
fill_limits(
	struct ghci_device *device,
	VkPhysicalDeviceLimits *limit)
{
	uint32_t varying_words;
	uint32_t attributes;
	uint32_t texture_slots;
	uint32_t shared_bytes;
	uint32_t attachments;
	uint32_t samples;

	/* The sizes of CAPS2, and what the render output and the varyings take. */
	attributes = GHCI_CAPS2_ATTRS(device->caps2);
	texture_slots = GHCI_CAPS2_TEX_SLOTS(device->caps2);
	shared_bytes = GHCI_CAPS2_LDS_KIB(device->caps2) * 1024U;
	attachments = device->vk->color_attachments;
	varying_words = VARYING_WORDS_BASIC;
	if ((device->caps & CAPS_VULKAN_CORE) != 0U)
		varying_words = VARYING_WORDS_CORE;
	samples = sample_counts(device);

	/* Images, buffers and allocations. */
	limit->maxImageDimension1D = IMAGE_EDGE;
	limit->maxImageDimension2D = IMAGE_EDGE;
	limit->maxImageDimension3D = IMAGE_EDGE_3D;
	limit->maxImageDimensionCube = IMAGE_EDGE;
	limit->maxImageArrayLayers = IMAGE_LAYERS;
	limit->maxTexelBufferElements = 65536U;
	limit->maxUniformBufferRange = 16384U;
	limit->maxStorageBufferRange = 1U << 27;
	limit->maxPushConstantsSize = 128U;
	limit->maxMemoryAllocationCount = GHCI_SESSION_RESOURCES;
	limit->maxSamplerAllocationCount = 4000U;
	limit->bufferImageGranularity = GHCI_LAYOUT_ALIGN;
	limit->sparseAddressSpaceSize = 0U;

	/* Descriptors: the texture slots are the sampled images and samplers a stage takes. */
	limit->maxBoundDescriptorSets = 4U;
	limit->maxPerStageDescriptorSamplers = texture_slots;
	limit->maxPerStageDescriptorUniformBuffers = 12U;
	limit->maxPerStageDescriptorStorageBuffers = 4U;
	limit->maxPerStageDescriptorSampledImages = texture_slots;
	limit->maxPerStageDescriptorStorageImages = 4U;
	limit->maxPerStageDescriptorInputAttachments = 4U;
	limit->maxPerStageResources = 128U;
	limit->maxDescriptorSetSamplers = 96U;
	limit->maxDescriptorSetUniformBuffers = 72U;
	limit->maxDescriptorSetUniformBuffersDynamic = 8U;
	limit->maxDescriptorSetStorageBuffers = 24U;
	limit->maxDescriptorSetStorageBuffersDynamic = 4U;
	limit->maxDescriptorSetSampledImages = 96U;
	limit->maxDescriptorSetStorageImages = 24U;
	limit->maxDescriptorSetInputAttachments = 4U;

	/* Vertex input, the varyings and the fragment outputs; no tessellation, no geometry. */
	limit->maxVertexInputAttributes = attributes;
	limit->maxVertexInputBindings = 16U;
	limit->maxVertexInputAttributeOffset = 2047U;
	limit->maxVertexInputBindingStride = 2048U;
	limit->maxVertexOutputComponents = varying_words;
	limit->maxFragmentInputComponents = varying_words;
	limit->maxFragmentOutputAttachments = attachments;
	limit->maxFragmentDualSrcAttachments = 0U;
	limit->maxFragmentCombinedOutputResources = attachments + 8U;

	/* Compute: the shared memory of CAPS2, and a workgroup the size of a shader unit's warps. */
	limit->maxComputeSharedMemorySize = shared_bytes;
	limit->maxComputeWorkGroupCount[0] = 65535U;
	limit->maxComputeWorkGroupCount[1] = 65535U;
	limit->maxComputeWorkGroupCount[2] = 65535U;
	limit->maxComputeWorkGroupInvocations = WORKGROUP_INVOCATIONS;
	limit->maxComputeWorkGroupSize[0] = WORKGROUP_INVOCATIONS;
	limit->maxComputeWorkGroupSize[1] = WORKGROUP_INVOCATIONS;
	limit->maxComputeWorkGroupSize[2] = 64U;

	/* Precision, draws and sampling. */
	limit->subPixelPrecisionBits = 4U;
	limit->subTexelPrecisionBits = 4U;
	limit->mipmapPrecisionBits = 4U;
	limit->maxDrawIndexedIndexValue = 0xffffffffU;
	limit->maxDrawIndirectCount = 1U;
	set_float(&limit->maxSamplerLodBias, F32_TWO);
	set_float(&limit->maxSamplerAnisotropy, F32_ONE);

	/* One viewport of the largest framebuffer, within the guard band. */
	limit->maxViewports = 1U;
	limit->maxViewportDimensions[0] = IMAGE_EDGE;
	limit->maxViewportDimensions[1] = IMAGE_EDGE;
	set_float(&limit->viewportBoundsRange[0], F32_8192_NEGATIVE);
	set_float(&limit->viewportBoundsRange[1], F32_8191);
	limit->viewportSubPixelBits = 0U;

	/* Alignments. */
	limit->minMemoryMapAlignment = 64U;
	limit->minTexelBufferOffsetAlignment = 16U;
	limit->minUniformBufferOffsetAlignment = 256U;
	limit->minStorageBufferOffsetAlignment = 16U;

	/* Texel offsets, gathers and interpolation offsets. */
	limit->minTexelOffset = -8;
	limit->maxTexelOffset = 7U;
	limit->minTexelGatherOffset = -8;
	limit->maxTexelGatherOffset = 7U;
	set_float(&limit->minInterpolationOffset, F32_HALF_NEGATIVE);
	set_float(&limit->maxInterpolationOffset, F32_0_4375);
	limit->subPixelInterpolationOffsetBits = 4U;

	/* Framebuffers and their samples. */
	limit->maxFramebufferWidth = IMAGE_EDGE;
	limit->maxFramebufferHeight = IMAGE_EDGE;
	limit->maxFramebufferLayers = IMAGE_LAYERS;
	limit->framebufferColorSampleCounts = samples;
	limit->framebufferDepthSampleCounts = samples;
	limit->framebufferStencilSampleCounts = samples;
	limit->framebufferNoAttachmentsSampleCounts = samples;
	limit->maxColorAttachments = attachments;
	limit->sampledImageColorSampleCounts = samples;
	limit->sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->sampledImageDepthSampleCounts = samples;
	limit->sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->maxSampleMaskWords = 1U;

	/* No timestamps, no clip or cull distances. */
	limit->timestampComputeAndGraphics = VK_FALSE;
	set_float(&limit->timestampPeriod, F32_ONE);
	limit->maxClipDistances = 0U;
	limit->maxCullDistances = 0U;
	limit->maxCombinedClipAndCullDistances = 0U;

	/* Queue priorities, points and lines of width 1, the standard sample places. */
	limit->discreteQueuePriorities = 2U;
	set_float(&limit->pointSizeRange[0], F32_ONE);
	set_float(&limit->pointSizeRange[1], F32_ONE);
	set_float(&limit->lineWidthRange[0], F32_ONE);
	set_float(&limit->lineWidthRange[1], F32_ONE);
	limit->strictLines = VK_FALSE;
	limit->standardSampleLocations = VK_TRUE;

	/* Copies: whole lines of memory. */
	limit->optimalBufferCopyOffsetAlignment = GHCI_LAYOUT_ALIGN;
	limit->optimalBufferCopyRowPitchAlignment = GHCI_LAYOUT_ALIGN;
	limit->nonCoherentAtomSize = GHCI_LAYOUT_ALIGN;
}

/*
 * Fills the features: the optional ones the GPU's units, the compiler and
 * the firmware provide.
 */
static void
fill_features(
	struct ghci_device *device,
	VkPhysicalDeviceFeatures *record)
{
	int core;

	/* The Vulkan 1.0 core rendering of CAPS[30] carries most of the optional state. */
	core = 0;
	if ((device->caps & CAPS_VULKAN_CORE) != 0U)
		core = 1;

	/* Required, or provided by the compiler and the firmware on every GHCI GPU. */
	record->robustBufferAccess = VK_TRUE;
	record->fullDrawIndexUint32 = VK_TRUE;
	record->drawIndirectFirstInstance = VK_TRUE;
	record->vertexPipelineStoresAndAtomics = VK_TRUE;
	record->fragmentStoresAndAtomics = VK_TRUE;

	/* The render output's and the rasterizer's optional state. */
	if (core) {
		record->independentBlend = VK_TRUE;
		record->depthClamp = VK_TRUE;
		record->depthBiasClamp = VK_TRUE;
		record->depthBounds = VK_TRUE;
		record->occlusionQueryPrecise = VK_TRUE;
	}

	/* Alpha to one comes with multisampling. */
	if ((device->caps & GHCI_CAPS_MS4) != 0U)
		record->alphaToOne = VK_TRUE;

	/* Gathers with offsets come with the texture unit of Vulkan 1.0 core sampling. */
	if ((device->caps & GHCI_CAPS_TU_FULL) != 0U)
		record->shaderImageGatherExtended = VK_TRUE;
}

/* vkGetPhysicalDeviceFeatures: [physical][u64 1] -> [u64 1][features]. */
static int
features(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkPhysicalDeviceFeatures *record;

	/* The physical device and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A cleared record with the device's features. */
	record = ghci_vk_arena_take(build->session, sizeof(*record));
	if (record == NULL)
		return ENOMEM;
	fill_features(build->session->device, record);

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkPhysicalDeviceFeatures(&build->reply, record);
	}

	/* Succeeded: the features are reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceMemoryProperties: [physical][u64 1][u64 32][u64 16]
 * -> [u64 1][properties].  Two types: device local (type 0), and device
 * local, host visible and coherent (type 1).  A discrete GPU with memory
 * past its window has two heaps, the window's and the rest; otherwise
 * one heap holds both types.
 */
static int
memory_properties(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkPhysicalDeviceMemoryProperties *record;
	struct ghci_device *device;
	uint64_t visible_bytes;
	uint64_t hidden_bytes;

	/* The physical device, the presence word and the two extents. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A cleared record from the scratch. */
	device = build->session->device;
	record = ghci_vk_arena_take(build->session, sizeof(*record));
	if (record == NULL)
		return ENOMEM;

	/* The memory the processor reaches, and the memory only the GPU reaches. */
	visible_bytes = device->visible.limit - device->visible.base;
	hidden_bytes = device->hidden.limit - device->hidden.base;
	if (device->local_size == 0U || device->local_window == 0U) {
		visible_bytes = GHCI_RESOURCE_MAX_BYTES;
		hidden_bytes = 0U;
	}

	/* The types: device local first, as the specification orders them by fewer properties. */
	record->memoryTypeCount = GHCI_VK_MEMORY_TYPES;
	record->memoryTypes[GHCI_VK_MEMORY_DEVICE].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	record->memoryTypes[GHCI_VK_MEMORY_DEVICE].heapIndex = 0U;
	record->memoryTypes[GHCI_VK_MEMORY_HOST].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
	    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	record->memoryTypes[GHCI_VK_MEMORY_HOST].heapIndex = 0U;

	/* The heaps: the window's, and the rest of the GPU's memory when there is any. */
	record->memoryHeapCount = 1U;
	record->memoryHeaps[0].size = visible_bytes;
	record->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
	if (hidden_bytes != 0U) {
		record->memoryHeapCount = 2U;
		record->memoryHeaps[1].size = hidden_bytes;
		record->memoryHeaps[1].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
		record->memoryTypes[GHCI_VK_MEMORY_DEVICE].heapIndex = 1U;
	}

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkPhysicalDeviceMemoryProperties(&build->reply, record);
	}

	/* Succeeded: the memory is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceQueueFamilyProperties: [physical][u64 1][u32 count]
 * [u64 count] -> [u64 1][u32 count][u64 count][families].  One family of
 * one queue that draws, computes and copies.
 */
static int
families(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkQueueFamilyProperties family;
	uint64_t array_count;
	uint64_t index;

	/* The physical device, the presence word and the count, then the array's count. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	array_count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (array_count > DEVICE_FAMILIES)
		return EINVAL;

	/* The presence word, the count, then the family when the array holds it. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, DEVICE_FAMILIES);
		drv_i915_wire_reply_u64(&build->reply, array_count);
		for (index = 0U; index < array_count; index++) {
			kern_memset(&family, 0, sizeof(family));
			family.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
			family.queueCount = 1U;
			family.timestampValidBits = 0U;
			family.minImageTransferGranularity.width = 1U;
			family.minImageTransferGranularity.height = 1U;
			family.minImageTransferGranularity.depth = 1U;
			i915_vkc_enc_VkQueueFamilyProperties(&build->reply, &family);
		}
	}

	/* Succeeded: the family is reported. */
	return 0;
}

/* vkGetPhysicalDeviceFormatProperties: [physical][u32 format][u64 1] -> [u64 1][properties]. */
static int
format(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkFormatProperties record;
	uint32_t optimal;
	uint32_t linear;
	uint32_t buffer;
	uint32_t vk_format;

	/* The physical device, the format and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	vk_format = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The format's features on each tiling and on buffers. */
	ghci_vk_format_features(build->session->device, vk_format, &optimal, &linear, &buffer);
	kern_memset(&record, 0, sizeof(record));
	record.linearTilingFeatures = linear;
	record.optimalTilingFeatures = optimal;
	record.bufferFeatures = buffer;

	/* The presence word and the record. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkFormatProperties(&build->reply, &record);
	}

	/* Succeeded: the format is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceImageFormatProperties: [physical][u32 format]
 * [u32 type][u32 tiling][u32 usage][u32 flags][u64 1] -> [result][u64 1]
 * [properties].
 */
static int
image_format(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkImageFormatProperties record;
	uint32_t vk_format;
	uint32_t type;
	uint32_t tiling;
	uint32_t usage;
	uint32_t create_flags;
	uint32_t result;

	/* The physical device, the format, type, tiling, usage and flags, and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	vk_format = drv_i915_wire_read_u32(reader);
	type = drv_i915_wire_read_u32(reader);
	tiling = drv_i915_wire_read_u32(reader);
	usage = drv_i915_wire_read_u32(reader);
	create_flags = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* What such an image may be. */
	result = image_format_limits(build->session->device, vk_format, type, tiling, usage, create_flags, &record);

	/* The result, the presence word and the limits. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, result);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		i915_vkc_enc_VkImageFormatProperties(&build->reply, &record);
	}

	/* Succeeded: the format is reported. */
	return 0;
}

/*
 * Fills the limits of an image of a format, type, tiling, usage and
 * flags, and reports VK_SUCCESS or VK_ERROR_FORMAT_NOT_SUPPORTED.  Every
 * image is laid out in linear rows, so the tilings take the same images;
 * 4x multisampling takes 2D images of one level that may be attachments.
 */
static uint32_t
image_format_limits(
	struct ghci_device *device,
	uint32_t vk_format,
	uint32_t type,
	uint32_t tiling,
	uint32_t usage,
	uint32_t create_flags,
	VkImageFormatProperties *record)
{
	uint32_t optimal;
	uint32_t linear;
	uint32_t buffer;
	uint32_t features;
	uint32_t known_flags;
	int allowed;

	/* The format's features on the tiling. */
	kern_memset(record, 0, sizeof(*record));
	ghci_vk_format_features(device, vk_format, &optimal, &linear, &buffer);
	features = optimal;
	if (tiling == VK_IMAGE_TILING_LINEAR)
		features = linear;

	/* Usages the features allow, and the flags of Vulkan 1.0 the device takes (no sparse). */
	allowed = usage_allowed(usage, features);
	if (!allowed)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	known_flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	if ((create_flags & ~known_flags) != 0U)
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;

	/* The extent and the layers of each type. */
	record->maxExtent.width = IMAGE_EDGE;
	record->maxExtent.height = IMAGE_EDGE;
	record->maxExtent.depth = 1U;
	record->maxArrayLayers = IMAGE_LAYERS;
	record->maxMipLevels = IMAGE_LEVELS;
	if (type == VK_IMAGE_TYPE_1D) {
		record->maxExtent.height = 1U;
	} else if (type == VK_IMAGE_TYPE_3D) {
		record->maxExtent.width = IMAGE_EDGE_3D;
		record->maxExtent.height = IMAGE_EDGE_3D;
		record->maxExtent.depth = IMAGE_EDGE_3D;
		record->maxArrayLayers = 1U;
		record->maxMipLevels = 9U;
	} else if (type != VK_IMAGE_TYPE_2D) {
		kern_memset(record, 0, sizeof(*record));
		return (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	/* One sample, and four for a plain 2D image that may be an attachment. */
	record->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
	if (type == VK_IMAGE_TYPE_2D &&
	    (create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) == 0U &&
	    (features & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0U)
		record->sampleCounts = sample_counts(device);

	/* The largest image is the largest allocation. */
	record->maxResourceSize = GHCI_RESOURCE_MAX_BYTES;

	/* Succeeded: such an image may be made. */
	return (uint32_t)VK_SUCCESS;
}

/*
 * vkGetPhysicalDeviceImageFormatProperties2 with an external memory
 * handle type: [physical][u64 1][IMAGE_FORMAT_INFO_2 sType][u64 1]
 * [EXTERNAL_IMAGE_FORMAT_INFO sType][u64 0][u32 handle type][u32 format]
 * [u32 type][u32 tiling][u32 usage][u32 flags][u64 1]
 * [IMAGE_FORMAT_PROPERTIES_2 sType][u64 1][EXTERNAL_IMAGE_FORMAT_PROPERTIES
 * sType][u64 0] -> [result][u64 1], the two headers, the external memory
 * properties and the image's limits.  The device shares no memory, so
 * the external properties are empty.
 */
static int
image_format2(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkImageFormatProperties record;
	uint32_t vk_format;
	uint32_t type;
	uint32_t tiling;
	uint32_t usage;
	uint32_t create_flags;
	uint32_t result;

	/* The physical device and the input chain up to the image's description. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);

	/* The image, then the output chain's headers. */
	vk_format = drv_i915_wire_read_u32(reader);
	type = drv_i915_wire_read_u32(reader);
	tiling = drv_i915_wire_read_u32(reader);
	usage = drv_i915_wire_read_u32(reader);
	create_flags = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The image's limits. */
	result = image_format_limits(build->session->device, vk_format, type, tiling, usage, create_flags, &record);

	/* The result, the two headers, no external memory feature, then the limits. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, result);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES);
		drv_i915_wire_reply_u64(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		i915_vkc_enc_VkImageFormatProperties(&build->reply, &record);
	}

	/* Succeeded: the format is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceExternalBufferProperties: [physical][u64 1]
 * [EXTERNAL_BUFFER_INFO sType][u64 0][u32 flags][u32 usage][u32 handle
 * type][u64 1][EXTERNAL_BUFFER_PROPERTIES sType][u64 0] -> [u64 1], the
 * header and the external memory properties: none, the device shares no
 * memory.
 */
static int
buffer_external(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	/* The physical device and the two chains. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The presence word, the header and three empty words. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES);
		drv_i915_wire_reply_u64(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
	}

	/* Succeeded: no external memory is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceSparseImageFormatProperties: [physical][u32 format]
 * [u32 type][u32 samples][u32 usage][u32 tiling][u64 1][u32 capacity]
 * [u64 capacity] -> [u64 1][u32 0][u64 0]: no sparse image format.
 */
static int
sparse_formats(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	/* The physical device, the five words, the presence word and the capacities. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* None. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u32(&build->reply, 0U);
		drv_i915_wire_reply_u64(&build->reply, 0U);
	}

	/* Succeeded: the device has no sparse images. */
	return 0;
}

/*
 * vkCreateDevice: [physical][u64 1][create info][u64 0][u64 1][device] ->
 * [result][u64 1][device].  Queues of the one family, one queue, and only
 * the features the device reports.
 */
static int
create_device(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkDeviceCreateInfo info;
	VkPhysicalDeviceFeatures offered;
	struct ghci_vk_object *object;
	uint64_t present;
	uint64_t device;
	uint32_t result;
	uint32_t index;
	int within;
	int error;

	/* The physical device, then the create info when present. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkDeviceCreateInfo(reader, &build->session->arena, &info);
	device = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Queues of the one family, one queue each. */
	result = (uint32_t)VK_SUCCESS;
	for (index = 0U; index < info.queueCreateInfoCount; index++) {
		/* The create info names its queues. */
		if (info.pQueueCreateInfos == NULL) {
			result = (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
			break;
		}

		/* A family the device does not have, or more queues than it has. */
		if (info.pQueueCreateInfos[index].queueFamilyIndex >= DEVICE_FAMILIES) {
			result = (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
		} else if (info.pQueueCreateInfos[index].queueCount != 1U) {
			result = (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
		}
	}

	/* Only the features the device reports. */
	if (result == (uint32_t)VK_SUCCESS && info.pEnabledFeatures != NULL) {
		kern_memset(&offered, 0, sizeof(offered));
		fill_features(build->session->device, &offered);
		within = features_within(info.pEnabledFeatures, &offered);
		if (!within)
			result = (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT;
	}

	/* The device's record: whether it checks buffer bounds, which changes how programs are built. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(build->session, GHCI_VK_DEVICE, device, &object);
		if (error == ENOMEM)
			return error;
		if (error != 0)
			result = (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
	}

	/* A device made with robustBufferAccess has its programs check their buffer offsets. */
	if (result == (uint32_t)VK_SUCCESS &&
	    info.pEnabledFeatures != NULL &&
	    info.pEnabledFeatures->robustBufferAccess != VK_FALSE)
		object->as.device.robust = 1;

	/* The result, and the identity when it succeeded. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, result);
		if (result == (uint32_t)VK_SUCCESS) {
			drv_i915_wire_reply_u64(&build->reply, 1U);
			drv_i915_wire_reply_u64(&build->reply, device);
		}
	}

	/* Succeeded: the device exists, or the reply says why not. */
	return 0;
}

/*
 * vkDestroyDevice: [device][u64 0], no reply body; relayed so the
 * firmware gives back what it still keeps of the device.
 */
static int
destroy_device(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t device;
	uint32_t index;
	size_t start;
	int error;

	/* The device, then the allocator. */
	start = reader->offset;
	device = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The firmware's objects of the device go. */
	error = ghci_vk_relay(build, GPU_OP_DESTROY_DEVICE, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The driver's record of it. */
	object = ghci_vk_object_find(build->session, GHCI_VK_DEVICE, device);
	if (object != NULL)
		ghci_vk_object_remove(build->session, object);

	/* Succeeded: the device is destroyed. */
	return 0;
}

/*
 * vkGetDeviceQueue2 as libvulkan sends it: [device][u64 1][sType]
 * [u64 1][timeline sType][u64 0][u32 timeline][u32 flags][u32 family]
 * [u32 index][u64 1][queue] -> [u64 1][queue].
 */
static int
device_queue(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	uint64_t queue;
	uint32_t timeline;
	uint32_t family;
	uint32_t index;

	/* Up to the timeline, the timeline, the flags, the family, the index, the presence word, the identity. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	timeline = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	family = drv_i915_wire_read_u32(reader);
	index = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	queue = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The one queue of the one family, completing jobs on a timeline of 1 to 63. */
	if (family >= DEVICE_FAMILIES)
		return EINVAL;
	if (index != 0U)
		return EINVAL;
	if (timeline == 0U)
		return EINVAL;
	if (timeline >= GHCI_VK_TIMELINES)
		return EINVAL;

	/* The session's jobs may name the queue's timeline from now on (the device has one ring). */
	build->session->timelines |= (uint64_t)1U << timeline;

	/* The presence word and the identity. */
	if (flags != 0U) {
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, queue);
	}

	/* Succeeded: the queue is known. */
	return 0;
}

/* Reports whether every usage of an image needs only features the format has: 1 when it does. */
static int
usage_allowed(
	uint32_t usage,
	uint32_t features)
{
	uint32_t known;

	/* Some usage, all of them of Vulkan 1.0, and a format with features. */
	known = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	    VK_IMAGE_USAGE_SAMPLED_BIT |
	    VK_IMAGE_USAGE_STORAGE_BIT |
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
	    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	if (usage == 0U)
		return 0;
	if ((usage & ~known) != 0U)
		return 0;
	if (features == 0U)
		return 0;

	/* Each usage against the feature it needs. */
	if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0U && (features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) == 0U)
		return 0;
	if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0U && (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0U)
		return 0;
	if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0U && (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0U)
		return 0;
	if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0U && (features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
		return 0;
	if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0U && (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0U)
		return 0;
	if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U && (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0U)
		return 0;

	/* Succeeded: every usage is allowed. */
	return 1;
}

/*
 * Reports whether every feature asked for is one offered: 1 when it is.
 * VkPhysicalDeviceFeatures is an array of VkBool32.
 */
static int
features_within(
	const VkPhysicalDeviceFeatures *asked,
	const VkPhysicalDeviceFeatures *offered)
{
	const VkBool32 *asked_words;
	const VkBool32 *offered_words;
	size_t count;
	size_t index;

	/* The two records as their Boolean words. */
	asked_words = (const VkBool32 *)(const void *)asked;
	offered_words = (const VkBool32 *)(const void *)offered;
	count = sizeof(*asked) / sizeof(VkBool32);

	/* A feature asked for that the device does not offer. */
	for (index = 0U; index < count; index++) {
		if (asked_words[index] != VK_FALSE && offered_words[index] == VK_FALSE)
			return 0;
	}

	/* Succeeded: every feature asked for is offered. */
	return 1;
}

/* Fills a float field by its bits, so this file never touches a floating-point register. */
static void
set_float(
	float *destination,
	uint32_t bits)
{
	/* Stores the bits unchanged. */
	kern_memcpy(destination, &bits, sizeof(bits));
}

/* Reports the sample counts of attachments and sampled images: 1, and 4 with 4x multisampling. */
static uint32_t
sample_counts(
	const struct ghci_device *device)
{
	/* 4x multisampling of CAPS[29]. */
	if ((device->caps & GHCI_CAPS_MS4) != 0U)
		return VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

	/* One sample. */
	return VK_SAMPLE_COUNT_1_BIT;
}
