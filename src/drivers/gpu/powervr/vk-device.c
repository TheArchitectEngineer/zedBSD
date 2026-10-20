/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's instance, physical-device, device and queue commands.
 *
 * The device is one physical device with one queue family of one queue
 * that computes (graphics comes with plan/ws269 p010), and two memory
 * types over one heap of uncached system memory: device local, and host
 * visible and coherent (design D7 (a)).  No image format has features
 * yet.  The limits are the ones the compute path keeps.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/bcm2711/vulkan-device.c
 * and vulkan-query.c (the wire of each command).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/vk.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

/*
 * The bits of the float constants of the limits: 1 and 2048.
 *
 * Values referenced: IEEE 754-2008 binary32 (sign 31, biased exponent
 * 30:23 with a bias of 127, fraction 22:0).
 */
#define DEVICE_F32_ONE			0x3f800000U
#define DEVICE_F32_2048			0x45000000U

/* The version the device reports (libvulkan needs a native 1.1) and the driver's version. */
#define DEVICE_API_VERSION		VK_MAKE_VERSION(1, 1, 0)
#define DEVICE_DRIVER_VERSION		1U

/*
 * The core's vendor and device numbers as Vulkan names them: Imagination
 * Technologies' PCI vendor id, and the BXE-4-32's BVNC packed as
 * B << 48 | V << 32 | N << 16 | C, of which the low 32 bits are taken.
 *
 * Values referenced: PCI-SIG vendor id 0x1010 (Imagination Technologies);
 * plan/ws269/design.md section 0.1 (BVNC 36.50.54.182).
 */
#define DEVICE_VENDOR_ID		0x1010U
#define DEVICE_ID			((54U << 16) | 182U)

/* The one queue family and the timelines a queue may complete jobs on (1 to 63). */
#define DEVICE_FAMILIES			1U
#define DEVICE_TIMELINES		64U

/* The heap the memory types share. */
#define DEVICE_HEAP_BYTES		POWERVR_RENDER_MAX_BYTES

/*
 * What the registry holds for an instance, a physical device, a device
 * and a queue: the executor only remembers that they exist, so each of
 * them stands for this token, which lives as long as the kernel.
 */
static int device_token;

static int create_instance(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int enumerate(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int properties(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int identity(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static VkPhysicalDeviceProperties *device_record(struct powervr_vk_session *session);
static void limits(VkPhysicalDeviceLimits *limit);
static int features(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int memory_properties(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int families(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int format(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int image_format(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static bool usage_allowed(uint32_t usage, uint32_t features);
static int create_device(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int device_queue(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int destroy(struct powervr_vk_session *session, enum i915_vk_object_kind kind, struct i915_wire_reader *reader);
static int wait_idle(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int token_destroy(struct powervr_vk_session *session, void *payload);
static int remember(struct powervr_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
static void set_float(float *destination, uint32_t bits);

/*
 * Runs an instance, physical-device, device or queue command; *handled
 * stays clear for another handler's opcode.
 */
int
powervr_vk_device_route(
	struct powervr_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another handler. */
	(void)requested;
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_INSTANCE:
		error = create_instance(session, reader, reply);
		break;
	case GPU_OP_DESTROY_INSTANCE:
		error = destroy(session, I915_VK_OBJ_INSTANCE, reader);
		break;
	case GPU_OP_ENUMERATE_PHYSICAL_DEVICES:
		error = enumerate(session, reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_FEATURES:
		error = features(session, reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES:
		error = format(reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES:
		error = image_format(reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES:
		error = properties(session, reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2:
		error = identity(session, reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES:
		error = families(reader, reply);
		break;
	case GPU_OP_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES:
		error = memory_properties(session, reader, reply);
		break;
	case GPU_OP_CREATE_DEVICE:
		error = create_device(session, reader, reply);
		break;
	case GPU_OP_DESTROY_DEVICE:
		error = destroy(session, I915_VK_OBJ_DEVICE, reader);
		break;
	case GPU_OP_QUEUE_WAIT_IDLE:
	case GPU_OP_DEVICE_WAIT_IDLE:
		error = wait_idle(reader, reply);
		break;
	case GPU_OP_GET_DEVICE_QUEUE2:
		error = device_queue(session, reader, reply);
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

/* vkCreateInstance: [present][create info][allocator][present][id] -> [result][present][id]. */
static int
create_instance(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkInstanceCreateInfo info;
	uint64_t present;
	uint64_t identity;
	int error;

	/* The create info when present; nothing of it is kept. */
	kern_memset(&info, 0, sizeof(info));
	present = drv_i915_wire_read_u64(reader);
	if (present != 0)
		i915_vkc_dec_VkInstanceCreateInfo(reader, &session->arena, &info);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Remembers the instance. */
	error = remember(session, I915_VK_OBJ_INSTANCE, identity);
	if (error != 0)
		return error;

	/* Success and the identity. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the instance exists. */
	return 0;
}

/*
 * vkEnumeratePhysicalDevices: [instance][present][count][array count][ids]
 * -> [result][present][count][array count][ids].  There is one device.
 */
static int
enumerate(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint64_t array_count;
	uint64_t identity;
	uint64_t index;
	uint32_t count;

	/* The instance and the count's presence word, then the counts. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	count = drv_i915_wire_read_u32(reader);
	array_count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (array_count > 1 || (array_count != 0 && count != 1))
		return EINVAL;

	/* Success and one device. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u32(reply, 1);
	drv_i915_wire_reply_u64(reply, array_count);

	/* Remembers and echoes the identity the library chose; one enumerated again is already remembered. */
	for (index = 0; index < array_count; index++) {
		identity = drv_i915_wire_read_u64(reader);
		if (reader->error != 0)
			return EINVAL;
		(void)remember(session, I915_VK_OBJ_PHYSICAL_DEVICE, identity);
		drv_i915_wire_reply_u64(reply, identity);
	}

	/* Succeeded: the device is reported. */
	return 0;
}

/* vkGetPhysicalDeviceProperties: [physical][present] -> [present][properties]. */
static int
properties(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkPhysicalDeviceProperties *record;

	/* The physical device and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The device's record. */
	record = device_record(session);
	if (record == NULL)
		return ENOMEM;

	/* The presence word and the record. */
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkPhysicalDeviceProperties(reply, record);

	/* Succeeded: the properties are reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceProperties2, asked for the device's ID properties
 * only: [physical][u64 1][PROPERTIES_2 sType][u64 1][ID_PROPERTIES sType]
 * [u64 0] -> [u64 1], the two headers, the device and driver UUIDs, the
 * LUID (none), the node mask, the LUID-valid word and the properties.
 * The UUIDs pair an exported allocation with its importer: every session
 * of the device reports the same ones.  ENOTSUP for another chain.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/bcm2711/vulkan-query.c
 * (identity_query); include/uapi/gpu-op.h
 * (GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2).
 */
static int
identity(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	static const uint8_t device_uuid[VK_UUID_SIZE] = {
		'z', 'e', 'd', 'B', 'S', 'D', '-', 'p', 'o', 'w', 'e', 'r', 'v', 'r', 'b', 'x'
	};
	static const uint8_t driver_uuid[VK_UUID_SIZE] = {
		'z', 'e', 'd', 'B', 'S', 'D', '-', 'p', 'v', 'r', '-', 'v', 'k', '-', '0', '1'
	};
	static const uint8_t luid[VK_LUID_SIZE] = { 0 };
	VkPhysicalDeviceProperties *record;
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
	if (reader->error != 0 || present != 1)
		return EINVAL;
	if (outer != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 || chained != 1 || inner != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES || last != 0)
		return ENOTSUP;

	/* The ordinary properties, as the plain query gives them. */
	record = device_record(session);
	if (record == NULL)
		return ENOMEM;

	/* The two headers, the identity (no Windows adapter: no LUID), then the properties. */
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u32(reply, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u32(reply, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
	drv_i915_wire_reply_u64(reply, 0);
	drv_i915_wire_reply_u64(reply, VK_UUID_SIZE);
	i915_vkc_reply_bytes(reply, device_uuid, VK_UUID_SIZE);
	drv_i915_wire_reply_u64(reply, VK_UUID_SIZE);
	i915_vkc_reply_bytes(reply, driver_uuid, VK_UUID_SIZE);
	drv_i915_wire_reply_u64(reply, VK_LUID_SIZE);
	i915_vkc_reply_bytes(reply, luid, VK_LUID_SIZE);
	drv_i915_wire_reply_u32(reply, 0);
	drv_i915_wire_reply_u32(reply, VK_FALSE);
	i915_vkc_enc_VkPhysicalDeviceProperties(reply, record);

	/* Succeeded: the identity every session of the device reports. */
	return 0;
}

/* Fills a cleared record from the scratch (too large for the stack) with the device's properties; NULL when none is left. */
static VkPhysicalDeviceProperties *
device_record(
	struct powervr_vk_session *session)
{
	static const char name[] = "zedBSD powervr (PowerVR BXE-4-32)";
	VkPhysicalDeviceProperties *record;

	/* The cleared record. */
	record = powervr_vk_arena_take(session, sizeof(*record));
	if (record == NULL)
		return NULL;

	/* The device: an integrated GPU, its names and its limits. */
	record->apiVersion = DEVICE_API_VERSION;
	record->driverVersion = DEVICE_DRIVER_VERSION;
	record->vendorID = DEVICE_VENDOR_ID;
	record->deviceID = DEVICE_ID;
	record->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	kern_memcpy(record->deviceName, name, sizeof(name));
	limits(&record->limits);

	/* Succeeded: the record. */
	return record;
}

/* Fills the limits the compute path keeps; the rest stay at the smallest. */
static void
limits(
	VkPhysicalDeviceLimits *limit)
{
	/* Allocations, buffers and descriptors. */
	limit->maxImageDimension1D = 4096;
	limit->maxImageDimension2D = POWERVR_VK_IMAGE_EDGE;
	limit->maxImageDimension3D = 256;
	limit->maxImageDimensionCube = 4096;
	limit->maxImageArrayLayers = POWERVR_VK_IMAGE_LAYERS;
	limit->maxTexelBufferElements = 65536;
	limit->maxUniformBufferRange = 16384;
	limit->maxStorageBufferRange = 1U << 27;
	limit->maxPushConstantsSize = 128;
	limit->maxMemoryAllocationCount = 256;
	limit->maxSamplerAllocationCount = 4000;
	limit->bufferImageGranularity = 1;
	limit->maxBoundDescriptorSets = POWERVR_VK_SETS_MAX;
	limit->maxPerStageDescriptorSamplers = 16;
	limit->maxPerStageDescriptorUniformBuffers = 12;
	limit->maxPerStageDescriptorStorageBuffers = 4;
	limit->maxPerStageDescriptorSampledImages = 16;
	limit->maxPerStageDescriptorStorageImages = 4;
	limit->maxPerStageDescriptorInputAttachments = 4;
	limit->maxPerStageResources = 128;
	limit->maxDescriptorSetSamplers = 96;
	limit->maxDescriptorSetUniformBuffers = 72;
	limit->maxDescriptorSetUniformBuffersDynamic = 8;
	limit->maxDescriptorSetStorageBuffers = 24;
	limit->maxDescriptorSetStorageBuffersDynamic = 4;
	limit->maxDescriptorSetSampledImages = 96;
	limit->maxDescriptorSetStorageImages = 24;
	limit->maxDescriptorSetInputAttachments = 4;

	/* Vertex input and the stage interfaces, the specification's smallest. */
	limit->maxVertexInputAttributes = 16;
	limit->maxVertexInputBindings = 16;
	limit->maxVertexInputAttributeOffset = 2047;
	limit->maxVertexInputBindingStride = 2048;
	limit->maxVertexOutputComponents = 64;
	limit->maxFragmentInputComponents = 64;
	limit->maxFragmentOutputAttachments = 4;
	limit->maxFragmentCombinedOutputResources = 4;

	/* Compute: a workgroup of one task in x, as many groups in x as one submission runs. */
	limit->maxComputeSharedMemorySize = 16384;
	limit->maxComputeWorkGroupCount[0] = 128;
	limit->maxComputeWorkGroupCount[1] = 1;
	limit->maxComputeWorkGroupCount[2] = 1;
	limit->maxComputeWorkGroupInvocations = 32;
	limit->maxComputeWorkGroupSize[0] = 32;
	limit->maxComputeWorkGroupSize[1] = 1;
	limit->maxComputeWorkGroupSize[2] = 1;

	/* Precision, draws, sampling and viewports. */
	limit->subPixelPrecisionBits = 4;
	limit->subTexelPrecisionBits = 4;
	limit->mipmapPrecisionBits = 4;
	limit->maxDrawIndexedIndexValue = 0xffffffU;
	limit->maxDrawIndirectCount = 1;
	set_float(&limit->maxSamplerLodBias, DEVICE_F32_ONE);
	set_float(&limit->maxSamplerAnisotropy, DEVICE_F32_ONE);
	limit->maxViewports = 1;
	limit->maxViewportDimensions[0] = 4096;
	limit->maxViewportDimensions[1] = 4096;
	set_float(&limit->viewportBoundsRange[0], DEVICE_F32_2048 | 0x80000000U);
	set_float(&limit->viewportBoundsRange[1], DEVICE_F32_2048);

	/* Alignments. */
	limit->minMemoryMapAlignment = 4096;
	limit->minTexelBufferOffsetAlignment = 16;
	limit->minUniformBufferOffsetAlignment = POWERVR_VK_RESOURCE_ALIGN;
	limit->minStorageBufferOffsetAlignment = 4;
	limit->minTexelOffset = -8;
	limit->maxTexelOffset = 7;
	limit->minTexelGatherOffset = -8;
	limit->maxTexelGatherOffset = 7;
	limit->subPixelInterpolationOffsetBits = 4;

	/* Framebuffers and samples: one sample. */
	limit->maxFramebufferWidth = 4096;
	limit->maxFramebufferHeight = 4096;
	limit->maxFramebufferLayers = 256;
	limit->framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->maxColorAttachments = 4;
	limit->sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	limit->maxSampleMaskWords = 1;

	/* Queue priorities, points, lines and copies. */
	limit->discreteQueuePriorities = 2;
	set_float(&limit->pointSizeRange[0], DEVICE_F32_ONE);
	set_float(&limit->pointSizeRange[1], DEVICE_F32_ONE);
	set_float(&limit->lineWidthRange[0], DEVICE_F32_ONE);
	set_float(&limit->lineWidthRange[1], DEVICE_F32_ONE);
	limit->standardSampleLocations = VK_TRUE;
	limit->optimalBufferCopyOffsetAlignment = 64;
	limit->optimalBufferCopyRowPitchAlignment = 64;
	limit->nonCoherentAtomSize = 64;
}

/* vkGetPhysicalDeviceFeatures: [physical][present] -> [present][features].  No optional feature. */
static int
features(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkPhysicalDeviceFeatures *record;

	/* The physical device and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A cleared record: every optional feature off. */
	record = powervr_vk_arena_take(session, sizeof(*record));
	if (record == NULL)
		return ENOMEM;

	/* The presence word and the record. */
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkPhysicalDeviceFeatures(reply, record);

	/* Succeeded: the features are reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceMemoryProperties: [physical][present][type extent]
 * [heap extent] -> [present][properties].  Two types over one heap.
 */
static int
memory_properties(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkPhysicalDeviceMemoryProperties *record;

	/* The physical device, the presence word and the two extents. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A cleared record from the scratch. */
	record = powervr_vk_arena_take(session, sizeof(*record));
	if (record == NULL)
		return ENOMEM;

	/*
	 * Device local, then host visible and coherent (uncached): the fewer
	 * properties first, as the specification orders them.  Host cached is
	 * not offered (design D7 (a)).
	 */
	record->memoryTypeCount = POWERVR_VK_MEMORY_TYPES;
	record->memoryTypes[POWERVR_VK_MEMORY_DEVICE].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	record->memoryTypes[POWERVR_VK_MEMORY_DEVICE].heapIndex = 0;
	record->memoryTypes[POWERVR_VK_MEMORY_HOST].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	record->memoryTypes[POWERVR_VK_MEMORY_HOST].heapIndex = 0;

	/* The one heap of system memory the core reaches. */
	record->memoryHeapCount = 1;
	record->memoryHeaps[0].size = DEVICE_HEAP_BYTES;
	record->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

	/* The presence word and the record. */
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkPhysicalDeviceMemoryProperties(reply, record);

	/* Succeeded: the memory is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceQueueFamilyProperties: [physical][present][count]
 * [array count] -> [present][count][array count][families].  One family
 * of one queue that computes.
 */
static int
families(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkQueueFamilyProperties family;
	uint64_t array_count;
	uint64_t index;

	/* The physical device, the presence word and the count, then the array's count. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	array_count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || array_count > DEVICE_FAMILIES)
		return EINVAL;

	/* The presence word, the count, then the family when the array holds it. */
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u32(reply, DEVICE_FAMILIES);
	drv_i915_wire_reply_u64(reply, array_count);
	for (index = 0; index < array_count; index++) {
		kern_memset(&family, 0, sizeof(family));
		family.queueFlags = VK_QUEUE_COMPUTE_BIT;
		family.queueCount = 1;
		family.minImageTransferGranularity.width = 1;
		family.minImageTransferGranularity.height = 1;
		family.minImageTransferGranularity.depth = 1;
		i915_vkc_enc_VkQueueFamilyProperties(reply, &family);
	}

	/* Succeeded: the family is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceFormatProperties: [physical][format][present] ->
 * [present][properties]; the features of the format table, none for a
 * format outside it.
 */
static int
format(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkFormatProperties record;
	const struct powervr_format *entry;
	uint32_t vk_format;

	/* The physical device, the format and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	vk_format = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The format's features on each tiling and on buffers. */
	kern_memset(&record, 0, sizeof(record));
	entry = powervr_format_find(vk_format);
	if (entry != NULL) {
		record.optimalTilingFeatures = entry->optimal_features;
		record.linearTilingFeatures = powervr_format_linear_features(entry);
		record.bufferFeatures = entry->buffer_features;
	}
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkFormatProperties(reply, &record);

	/* Succeeded: the format is reported. */
	return 0;
}

/*
 * vkGetPhysicalDeviceImageFormatProperties: [physical][format][type]
 * [tiling][usage][flags][present] -> [result][present][properties].  A
 * plain two-dimensional image of one sample whose every usage the
 * format's features on the tiling allow.
 */
static int
image_format(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageFormatProperties record;
	const struct powervr_format *entry;
	uint32_t vk_format;
	uint32_t type;
	uint32_t tiling;
	uint32_t usage;
	uint32_t flags;
	uint32_t features;
	uint32_t result;

	/* The physical device, the format, type, tiling, usage and flags, and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	vk_format = drv_i915_wire_read_u32(reader);
	type = drv_i915_wire_read_u32(reader);
	tiling = drv_i915_wire_read_u32(reader);
	usage = drv_i915_wire_read_u32(reader);
	flags = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The format's features on the tiling. */
	features = 0;
	entry = powervr_format_find(vk_format);
	if (entry != NULL && tiling == VK_IMAGE_TILING_OPTIMAL)
		features = entry->optimal_features;
	else if (entry != NULL && tiling == VK_IMAGE_TILING_LINEAR)
		features = powervr_format_linear_features(entry);

	/* A plain image whose usages its features allow. */
	result = (uint32_t)VK_SUCCESS;
	if (type != VK_IMAGE_TYPE_2D || flags != 0 || !usage_allowed(usage, features))
		result = (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED;

	/* The limits of such an image: a linear one is one level and one layer. */
	kern_memset(&record, 0, sizeof(record));
	if (result == (uint32_t)VK_SUCCESS) {
		record.maxExtent.width = POWERVR_VK_IMAGE_EDGE;
		record.maxExtent.height = POWERVR_VK_IMAGE_EDGE;
		record.maxExtent.depth = 1;
		record.maxMipLevels = 13;
		record.maxArrayLayers = POWERVR_VK_IMAGE_LAYERS;
		if (tiling == VK_IMAGE_TILING_LINEAR) {
			record.maxMipLevels = 1;
			record.maxArrayLayers = 1;
		}
		record.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
		record.maxResourceSize = POWERVR_RENDER_MAX_BYTES;
	}
	drv_i915_wire_reply_u32(reply, result);
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkImageFormatProperties(reply, &record);

	/* Succeeded: the format is reported. */
	return 0;
}

/* Says whether every usage of an image needs only features the format has; none at all is refused. */
static bool
usage_allowed(
	uint32_t usage,
	uint32_t features)
{
	uint32_t known;

	/* Some usage, all of them known, and a format with features. */
	known = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (usage == 0 || (usage & ~known) != 0 || features == 0)
		return false;

	/* Each usage against the feature it needs. */
	if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 && (features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) == 0) {
		/* A copy from the image. */
		return false;
	} else if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0 && (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0) {
		/* A copy or clear into the image. */
		return false;
	} else if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
		/* A shader's sampling. */
		return false;
	} else if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0 && (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0) {
		/* A render pass's colour. */
		return false;
	} else if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 && (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		/* A render pass's depth. */
		return false;
	}

	/* Succeeded: every usage is allowed. */
	return true;
}

/*
 * vkCreateDevice: [physical][present][create info][allocator][present]
 * [id] -> [result][present][id].  Queues of the one family only, no
 * optional feature.
 */
static int
create_device(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkDeviceCreateInfo info;
	VkPhysicalDeviceFeatures none;
	uint64_t present;
	uint64_t identity;
	uint32_t index;
	int different;
	int error;

	/* The physical device, then the create info when present. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0)
		i915_vkc_dec_VkDeviceCreateInfo(reader, &session->arena, &info);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Queues of the one family, one queue each. */
	for (index = 0; index < info.queueCreateInfoCount && info.pQueueCreateInfos != NULL; index++) {
		if (info.pQueueCreateInfos[index].queueFamilyIndex >= DEVICE_FAMILIES || info.pQueueCreateInfos[index].queueCount != 1) {
			drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_INITIALIZATION_FAILED);
			return 0;
		}
	}

	/* No optional feature. */
	if (info.pEnabledFeatures != NULL) {
		kern_memset(&none, 0, sizeof(none));
		different = kern_memcmp(&none, info.pEnabledFeatures, sizeof(none));
		if (different != 0) {
			drv_i915_wire_reply_u32(reply, (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT);
			return 0;
		}
	}

	/* Remembers the device. */
	error = remember(session, I915_VK_OBJ_DEVICE, identity);
	if (error != 0)
		return error;

	/* Success and the identity. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the device exists. */
	return 0;
}

/*
 * vkGetDeviceQueue2 as libvulkan sends it: [device][present][sType]
 * [pNext present][timeline sType][pNext][timeline][flags][family][index]
 * [present][id] -> [present][id].
 */
static int
device_queue(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint64_t identity;
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
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The one queue of the one family, completing jobs on a timeline of 1 to 63. */
	if (family >= DEVICE_FAMILIES || index != 0 || timeline == 0 || timeline >= DEVICE_TIMELINES)
		return EINVAL;

	/* Remembers the queue; one asked for again is already remembered. */
	(void)remember(session, I915_VK_OBJ_QUEUE, identity);

	/* The session's jobs may name the queue's timeline from now on. */
	__atomic_fetch_or(&session->timelines, 1ULL << timeline, __ATOMIC_RELEASE);

	/* The presence word and the identity. */
	drv_i915_wire_reply_u64(reply, 1);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the queue is known. */
	return 0;
}

/* vkDestroyInstance and vkDestroyDevice: [handle][allocator], no reply body. */
static int
destroy(
	struct powervr_vk_session *session,
	enum i915_vk_object_kind kind,
	struct i915_wire_reader *reader)
{
	uint64_t identity;

	/* The identity, then the allocator. */
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Forgets it; one never remembered is no error. */
	(void)powervr_vk_object_remove(session, kind, identity);

	/* Succeeded: the identity is forgotten. */
	return 0;
}

/*
 * vkQueueWaitIdle and vkDeviceWaitIdle: [handle] -> [result].  A
 * submission's work ends before its frame's later commands run, so the
 * queue is idle here.
 */
static int
wait_idle(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	/* The queue or the device. */
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Success. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);

	/* Succeeded: the queue is idle. */
	return 0;
}

/* The token's destructor: the token is static, nothing is freed. */
static int
token_destroy(
	struct powervr_vk_session *session,
	void *payload)
{
	/* Nothing to free. */
	(void)session;
	(void)payload;

	/* Succeeded: the token stays. */
	return 0;
}

/* Publishes an identity that stands for the token. */
static int
remember(
	struct powervr_vk_session *session,
	enum i915_vk_object_kind kind,
	uint64_t identity)
{
	int error;

	/* Publishes it. */
	error = powervr_vk_object_publish(session, kind, identity, &device_token, token_destroy);
	if (error != 0)
		return error;

	/* Succeeded: the identity is remembered. */
	return 0;
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
