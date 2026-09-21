/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Instance, physical-device, device and queue commands of the native Vulkan executor (WS031 E-127).
 *
 * MINIMAL CONNECTION, HAPPY PATH ONLY.  The wire of every command here was read from the library
 * that sends it (userland/base/libvulkan: instance.c, device.c, objects.c) and the records travel
 * through the generated codec, so the two ends cannot drift.  What is reported is one Gen12 device:
 * one memory type that is device-local and host-coherent (the GPU is UMA), one queue family of one
 * graphics queue on RCS0.
 *
 * XXX: the limits and the format table are what the connectivity check needs, not a survey of the
 * hardware; each is marked where it is filled.
 */

#include "vkc.h"

#include "../internal.h"

#include <kern/klog.h>
#include <kern/kmem.h>

#include <errno.h>

#include "codec-generated.inc"

/* Identities are the library's: the executor only remembers that they were created. */
static int inst_token;

static void
set_float(float *destination, uint32_t bits)
{
	/* A float field is filled by its bits: this translation unit never touches an FP register. */
	memcpy(destination, &bits, sizeof(bits));
}

#define F32_ONE      0x3f800000U   /* 1.0f */
#define F32_16       0x41800000U   /* 16.0f */
#define F32_EIGHTH   0x3e000000U   /* 0.125f */
#define F32_MINUS_32768 0xc7000000U /* -32768.0f */
#define F32_32767    0x46fffe00U   /* 32767.0f */
#define F32_2048     0x45000000U   /* 2048.0f */

/* [pCreateInfo][allocator = 0][pInstance: id] -> [result][present][id] */
static int
inst_create_instance(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkInstanceCreateInfo info;
	uint64_t present;
	uint64_t identity;
	int error;

	memset(&info, 0, sizeof(info));
	present = i915_vk_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkInstanceCreateInfo(reader, &session->arena, &info);
	(void)i915_vk_read_u64(reader);			/* pAllocator */
	(void)i915_vk_read_u64(reader);			/* pInstance present */
	identity = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_INSTANCE, identity, &inst_token);
	if (error != 0)
		return error;

	i915_vk_reply_u32(reply, 0U);			/* VK_SUCCESS */
	i915_vk_reply_u64(reply, 1U);
	i915_vk_reply_u64(reply, identity);
	return 0;
}

/* [instance][pCount present][count][array count][ids] -> [result][present][count][array count][ids] */
static int
inst_enumerate_physical_devices(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	uint64_t array_count;
	uint64_t identity;
	uint64_t index;
	uint32_t count;

	(void)i915_vk_read_u64(reader);			/* instance */
	(void)i915_vk_read_u64(reader);			/* pPhysicalDeviceCount present */
	count = i915_vk_read_u32(reader);
	array_count = i915_vk_read_u64(reader);
	if (reader->error != 0 || array_count > 1U || (array_count != 0U && count != 1U))
		return EINVAL;

	/* This executor is one physical device. */
	i915_vk_reply_u32(reply, 0U);
	i915_vk_reply_u64(reply, 1U);
	i915_vk_reply_u32(reply, 1U);
	i915_vk_reply_u64(reply, array_count);
	for (index = 0U; index < array_count; index++) {
		identity = i915_vk_read_u64(reader);
		if (reader->error != 0)
			return EINVAL;
		(void)i915_vk_obj_insert(session->vk, I915_VK_OBJ_PHYSICAL_DEVICE, identity, &inst_token);
		i915_vk_reply_u64(reply, identity);
	}
	return 0;
}

/* [physical][present] -> [present][VkPhysicalDeviceProperties] */
static int
inst_properties(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	static const char name[] = "zedBSD i915 (Gen12 Xe)";
	VkPhysicalDeviceProperties *p;
	VkPhysicalDeviceLimits *l;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	p = i915_vkc_array(reader, &session->arena, 1U, sizeof(*p));
	if (p == NULL)
		return ENOMEM;
	p->apiVersion = VK_MAKE_VERSION(1, 1, 0);	/* the library requires a native 1.1 */
	p->driverVersion = 1U;
	p->vendorID = 0x8086U;
	p->deviceID = session->vk->i915 != NULL ? session->vk->i915->product : 0U;
	p->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
	memcpy(p->deviceName, name, sizeof(name));

	/*
	 * XXX: limits for the connectivity check -- the values a Gen12 part comfortably meets and the
	 * standard application reads, not a transcription of the hardware's real maxima.
	 */
	l = &p->limits;
	l->maxImageDimension1D = 16384U;
	l->maxImageDimension2D = 16384U;
	l->maxImageDimension3D = 2048U;
	l->maxImageDimensionCube = 16384U;
	l->maxImageArrayLayers = 2048U;
	l->maxTexelBufferElements = 1U << 27;
	l->maxUniformBufferRange = 1U << 27;
	l->maxStorageBufferRange = 1U << 30;
	l->maxPushConstantsSize = 128U;
	l->maxMemoryAllocationCount = 4096U;
	l->maxSamplerAllocationCount = 4000U;
	l->bufferImageGranularity = 1U;
	l->maxBoundDescriptorSets = 4U;
	l->maxPerStageDescriptorSamplers = 16U;
	l->maxPerStageDescriptorUniformBuffers = 12U;
	l->maxPerStageDescriptorStorageBuffers = 4U;
	l->maxPerStageDescriptorSampledImages = 16U;
	l->maxPerStageDescriptorStorageImages = 4U;
	l->maxPerStageDescriptorInputAttachments = 4U;
	l->maxPerStageResources = 128U;
	l->maxDescriptorSetSamplers = 96U;
	l->maxDescriptorSetUniformBuffers = 72U;
	l->maxDescriptorSetUniformBuffersDynamic = 8U;
	l->maxDescriptorSetStorageBuffers = 24U;
	l->maxDescriptorSetStorageBuffersDynamic = 4U;
	l->maxDescriptorSetSampledImages = 96U;
	l->maxDescriptorSetStorageImages = 24U;
	l->maxDescriptorSetInputAttachments = 4U;
	l->maxVertexInputAttributes = 16U;
	l->maxVertexInputBindings = 16U;
	l->maxVertexInputAttributeOffset = 2047U;
	l->maxVertexInputBindingStride = 2048U;
	l->maxVertexOutputComponents = 64U;
	l->maxFragmentInputComponents = 64U;
	l->maxFragmentOutputAttachments = 4U;
	l->maxFragmentCombinedOutputResources = 4U;
	l->maxComputeSharedMemorySize = 16384U;
	l->maxComputeWorkGroupCount[0] = 65535U;
	l->maxComputeWorkGroupCount[1] = 65535U;
	l->maxComputeWorkGroupCount[2] = 65535U;
	l->maxComputeWorkGroupInvocations = 128U;
	l->maxComputeWorkGroupSize[0] = 128U;
	l->maxComputeWorkGroupSize[1] = 128U;
	l->maxComputeWorkGroupSize[2] = 64U;
	l->subPixelPrecisionBits = 4U;
	l->subTexelPrecisionBits = 4U;
	l->mipmapPrecisionBits = 4U;
	l->maxDrawIndexedIndexValue = 0xffffffffU;
	l->maxDrawIndirectCount = 1U;
	set_float(&l->maxSamplerLodBias, F32_16);
	set_float(&l->maxSamplerAnisotropy, F32_ONE);
	l->maxViewports = 1U;
	l->maxViewportDimensions[0] = 16384U;
	l->maxViewportDimensions[1] = 16384U;
	set_float(&l->viewportBoundsRange[0], F32_MINUS_32768);
	set_float(&l->viewportBoundsRange[1], F32_32767);
	l->minMemoryMapAlignment = 4096U;
	l->minTexelBufferOffsetAlignment = 16U;
	l->minUniformBufferOffsetAlignment = 64U;
	l->minStorageBufferOffsetAlignment = 64U;
	l->minTexelOffset = -8;
	l->maxTexelOffset = 7U;
	l->minTexelGatherOffset = -8;
	l->maxTexelGatherOffset = 7U;
	l->subPixelInterpolationOffsetBits = 4U;
	l->maxFramebufferWidth = 16384U;
	l->maxFramebufferHeight = 16384U;
	l->maxFramebufferLayers = 1U;
	l->framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->maxColorAttachments = 4U;
	l->sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
	l->maxSampleMaskWords = 1U;
	l->maxClipDistances = 8U;
	l->maxCullDistances = 8U;
	l->maxCombinedClipAndCullDistances = 8U;
	l->discreteQueuePriorities = 2U;
	set_float(&l->pointSizeRange[0], F32_EIGHTH);
	set_float(&l->pointSizeRange[1], F32_2048);
	set_float(&l->lineWidthRange[0], F32_ONE);
	set_float(&l->lineWidthRange[1], F32_ONE);
	l->strictLines = VK_FALSE;
	l->standardSampleLocations = VK_TRUE;
	l->optimalBufferCopyOffsetAlignment = 64U;
	l->optimalBufferCopyRowPitchAlignment = 64U;
	l->nonCoherentAtomSize = 64U;

	i915_vk_reply_u64(reply, 1U);
	i915_vkc_enc_VkPhysicalDeviceProperties(reply, p);
	return 0;
}

/* [physical][present] -> [present][VkPhysicalDeviceFeatures]: no optional feature is claimed. */
static int
inst_features(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkPhysicalDeviceFeatures *features;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	features = i915_vkc_array(reader, &session->arena, 1U, sizeof(*features));
	if (features == NULL)
		return ENOMEM;

	i915_vk_reply_u64(reply, 1U);
	i915_vkc_enc_VkPhysicalDeviceFeatures(reply, features);
	return 0;
}

/* [physical][present][type extent][heap extent] -> [present][VkPhysicalDeviceMemoryProperties] */
static int
inst_memory_properties(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkPhysicalDeviceMemoryProperties *memory;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	memory = i915_vkc_array(reader, &session->arena, 1U, sizeof(*memory));
	if (memory == NULL)
		return ENOMEM;

	/* One type: the GPU shares the system's memory, so it is device-local AND host-coherent. */
	memory->memoryTypeCount = 1U;
	memory->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
		VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	memory->memoryTypes[0].heapIndex = 0U;
	memory->memoryHeapCount = 1U;
	memory->memoryHeaps[0].size = 1024ULL * 1024ULL * 1024ULL;	/* XXX: a budget, not a measurement */
	memory->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

	i915_vk_reply_u64(reply, 1U);
	i915_vkc_enc_VkPhysicalDeviceMemoryProperties(reply, memory);
	return 0;
}

/* [physical][present][count][array count] -> [present][count][array count][families] */
static int
inst_queue_families(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkQueueFamilyProperties family;
	uint64_t array_count;

	(void)session;
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	array_count = i915_vk_read_u64(reader);
	if (reader->error != 0 || array_count > 1U)
		return EINVAL;

	/* One family, one queue: graphics (with the compute and transfer it implies) on RCS0. */
	memset(&family, 0, sizeof(family));
	family.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	family.queueCount = 1U;
	family.minImageTransferGranularity.width = 1U;
	family.minImageTransferGranularity.height = 1U;
	family.minImageTransferGranularity.depth = 1U;

	i915_vk_reply_u64(reply, 1U);
	i915_vk_reply_u32(reply, 1U);
	i915_vk_reply_u64(reply, array_count);
	if (array_count != 0U)
		i915_vkc_enc_VkQueueFamilyProperties(reply, &family);
	return 0;
}

/* The formats this executor renders to, samples from and copies. */
static void
inst_format_features(
	uint32_t format,
	VkFormatProperties *properties)
{
	memset(properties, 0, sizeof(*properties));

	/* XXX: the three formats the connectivity check uses; nothing else is claimed. */
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
		properties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
			VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
			VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
			VK_FORMAT_FEATURE_BLIT_DST_BIT;	/* E-130: GPU rectangles (gfx-draw.c) */
		properties->linearTilingFeatures = properties->optimalTilingFeatures;
		break;
	case VK_FORMAT_D32_SFLOAT:
		properties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
		break;
	default:
		break;
	}
}

/* [physical][format][present] -> [present][VkFormatProperties] */
static int
inst_format_properties(
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkFormatProperties properties;
	uint32_t format;

	(void)i915_vk_read_u64(reader);
	format = i915_vk_read_u32(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	inst_format_features(format, &properties);
	i915_vk_reply_u64(reply, 1U);
	i915_vkc_enc_VkFormatProperties(reply, &properties);
	return 0;
}

/* [physical][format][type][tiling][usage][flags][present] -> [result][present][VkImageFormatProperties] */
static int
inst_image_format_properties(
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkImageFormatProperties image;
	VkFormatProperties properties;
	uint32_t format;
	uint32_t type;

	(void)i915_vk_read_u64(reader);
	format = i915_vk_read_u32(reader);
	type = i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	inst_format_features(format, &properties);
	memset(&image, 0, sizeof(image));
	if (properties.optimalTilingFeatures == 0U || type != VK_IMAGE_TYPE_2D) {
		i915_vk_reply_u32(reply, (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED);
		i915_vk_reply_u64(reply, 1U);
		i915_vkc_enc_VkImageFormatProperties(reply, &image);
		return 0;
	}

	image.maxExtent.width = 16384U;
	image.maxExtent.height = 16384U;
	image.maxExtent.depth = 1U;
	image.maxMipLevels = 1U;			/* XXX: one level is what the executor lays out */
	image.maxArrayLayers = 1U;
	image.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
	image.maxResourceSize = 1ULL << 30;
	i915_vk_reply_u32(reply, 0U);
	i915_vk_reply_u64(reply, 1U);
	i915_vkc_enc_VkImageFormatProperties(reply, &image);
	return 0;
}

/* [physical][present][VkDeviceCreateInfo][allocator][present][id] -> [result][present][id] */
static int
inst_create_device(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	VkDeviceCreateInfo info;
	uint64_t present;
	uint64_t identity;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	present = i915_vk_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkDeviceCreateInfo(reader, &session->arena, &info);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	identity = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_DEVICE, identity, &inst_token);
	if (error != 0)
		return error;

	i915_vk_reply_u32(reply, 0U);
	i915_vk_reply_u64(reply, 1U);
	i915_vk_reply_u64(reply, identity);
	return 0;
}

/*
 * vkGetDeviceQueue2 as device.c sends it: [device][present][sType DEVICE_QUEUE_INFO_2][pNext present]
 * [sType of the timeline record][pNext = 0][timeline index][flags][family][index][present][id]
 * -> [present][id].  The command has no result.
 */
static int
inst_get_device_queue2(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	uint64_t identity;

	(void)i915_vk_read_u64(reader);			/* device */
	(void)i915_vk_read_u64(reader);			/* pQueueInfo present */
	(void)i915_vk_read_u32(reader);			/* sType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_u32(reader);			/* chained sType */
	(void)i915_vk_read_u64(reader);			/* its pNext */
	(void)i915_vk_read_u32(reader);			/* timeline index: XXX one timeline, RCS0 */
	(void)i915_vk_read_u32(reader);			/* flags */
	(void)i915_vk_read_u32(reader);			/* family */
	(void)i915_vk_read_u32(reader);			/* index */
	(void)i915_vk_read_u64(reader);			/* pQueue present */
	identity = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	(void)i915_vk_obj_insert(session->vk, I915_VK_OBJ_QUEUE, identity, &inst_token);
	i915_vk_reply_u64(reply, 1U);
	i915_vk_reply_u64(reply, identity);
	return 0;
}

/* vkDestroyInstance / vkDestroyDevice: [handle][allocator], no reply body. */
static int
inst_destroy(
	struct i915_vk_session *session,
	enum i915_vk_object_kind kind,
	struct i915_vk_reader *reader)
{
	uint64_t identity;

	identity = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	i915_vk_obj_remove(session->vk, kind, identity);
	return 0;
}

/* vkQueueWaitIdle / vkDeviceWaitIdle: [handle] -> [result]. */
static int
inst_wait_idle(
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/*
	 * XXX: happy path -- the resident shim runs every request to its end before it takes the next,
	 * and this command travels the same ordered stream, so by the time it is decoded the queue is
	 * idle.  A real wait belongs here once submission is asynchronous.
	 */
	i915_vk_reply_u32(reply, 0U);
	return 0;
}

/*
 * vkExecuteCommandStreamsMESA as context.c sends it: [stream count = 1][present][resource][offset]
 * [bytes][pReplyOffsets = 0][dependent count = 0][pDependents = 0][flags = 0].  The stream is a
 * session blob; its commands are decoded in place of this one and reply to the same writer.
 */
static int
inst_execute_streams(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_gem_object *object;
	struct i915_vk_reader nested;
	uint64_t offset;
	uint64_t bytes;
	uint32_t resource;
	uint8_t *copy;
	int error;

	(void)i915_vk_read_u32(reader);			/* stream count: XXX one stream */
	(void)i915_vk_read_u64(reader);			/* pStreams present */
	resource = i915_vk_read_u32(reader);
	offset = i915_vk_read_u64(reader);
	bytes = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);			/* pReplyOffsets */
	(void)i915_vk_read_u32(reader);			/* dependent count */
	(void)i915_vk_read_u64(reader);			/* pDependents */
	(void)i915_vk_read_u32(reader);			/* flags */
	if (reader->error != 0)
		return EINVAL;

	for (object = session->gpu->objects; object != NULL; object = object->session_next) {
		if (object->slot == resource)
			break;
	}
	if (object == NULL || offset > object->bytes || bytes > object->bytes - offset ||
	    bytes == 0U || bytes > (64U << 20))
		return EINVAL;

	/* The sender keeps its mapping: decode a private copy, not memory that can change underfoot. */
	copy = kern_malloc((size_t)bytes);
	if (copy == NULL)
		return ENOMEM;
	memcpy(copy, (const uint8_t *)kern_pmem_to_kernel(object->run.paddr) + offset, (size_t)bytes);

	nested.base = copy;
	nested.size = (size_t)bytes;
	nested.offset = 0U;
	nested.error = 0;
	error = 0;
	while (error == 0 && nested.offset < nested.size) {
		session->arena.used = 0U;
		error = i915_vk_cmd_dispatch(session, &nested, reply);
	}

	kern_free(copy);
	return error;
}

int
i915_vk_inst_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply,
	int *handled)
{
	*handled = 1;
	switch (opcode) {
	case 0U:	/* vkCreateInstance */
		return inst_create_instance(session, reader, reply);
	case 1U:	/* vkDestroyInstance */
		return inst_destroy(session, I915_VK_OBJ_INSTANCE, reader);
	case 2U:	/* vkEnumeratePhysicalDevices */
		return inst_enumerate_physical_devices(session, reader, reply);
	case 3U:	/* vkGetPhysicalDeviceFeatures */
		return inst_features(session, reader, reply);
	case 4U:	/* vkGetPhysicalDeviceFormatProperties */
		return inst_format_properties(reader, reply);
	case 5U:	/* vkGetPhysicalDeviceImageFormatProperties */
		return inst_image_format_properties(reader, reply);
	case 6U:	/* vkGetPhysicalDeviceProperties */
		return inst_properties(session, reader, reply);
	case 7U:	/* vkGetPhysicalDeviceQueueFamilyProperties */
		return inst_queue_families(session, reader, reply);
	case 8U:	/* vkGetPhysicalDeviceMemoryProperties */
		return inst_memory_properties(session, reader, reply);
	case 11U:	/* vkCreateDevice */
		return inst_create_device(session, reader, reply);
	case 12U:	/* vkDestroyDevice */
		return inst_destroy(session, I915_VK_OBJ_DEVICE, reader);
	case 19U:	/* vkQueueWaitIdle */
	case 20U:	/* vkDeviceWaitIdle */
		return inst_wait_idle(reader, reply);
	case 155U:	/* vkGetDeviceQueue2 */
		return inst_get_device_queue2(session, reader, reply);
	case 180U:	/* vkExecuteCommandStreamsMESA */
		return inst_execute_streams(session, reader, reply);
	default:
		*handled = 0;
		return 0;
	}
}
