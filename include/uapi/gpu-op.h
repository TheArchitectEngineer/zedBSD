/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Kei GPU command protocol, version 1 (ws167-p002): the numbers of
 * the Vulkan commands in the stream libvulkan submits to a GPU node
 * (GPU_COMMAND, GPU_COMMAND_SUBMIT in uapi/gpu.h), which the i915 Vulkan
 * executor reads and a virtual machine's host renders.
 *
 * The numbers of version 1 reuse those of the Venus protocol's wire
 * format 1 (virglrenderer 1.1.0) for the same Vulkan commands, so that a
 * Venus renderer on a virtual machine's host accepts the stream as it
 * is.  The names, this file and every later number are zedBSD's own; a
 * command of zedBSD's alone is numbered from GPU_OP_OWN_FIRST, which a
 * Venus host is never sent.
 */

#ifndef UAPI_GPU_OP_H
#define UAPI_GPU_OP_H

/*
 * The protocol's version, and the first number of zedBSD's own commands.
 * Version 2 adds the video commands (ws083); version 1's numbers are
 * unchanged, and only a backend whose capset declares video is sent the
 * new ones.
 */
#define GPU_OP_PROTOCOL_VERSION	2U
#define GPU_OP_OWN_FIRST	0x10000U

/* Each command, the Vulkan command it carries named beside it. */
enum gpu_op {
	GPU_OP_CREATE_INSTANCE = 0,	/* vkCreateInstance */
	GPU_OP_DESTROY_INSTANCE = 1,	/* vkDestroyInstance */
	GPU_OP_ENUMERATE_PHYSICAL_DEVICES = 2,	/* vkEnumeratePhysicalDevices */
	GPU_OP_GET_PHYSICAL_DEVICE_FEATURES = 3,	/* vkGetPhysicalDeviceFeatures */
	GPU_OP_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES = 4,	/* vkGetPhysicalDeviceFormatProperties */
	GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES = 5,	/* vkGetPhysicalDeviceImageFormatProperties */
	GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES = 6,	/* vkGetPhysicalDeviceProperties */
	GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES = 7,	/* vkGetPhysicalDeviceQueueFamilyProperties */
	GPU_OP_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES = 8,	/* vkGetPhysicalDeviceMemoryProperties */
	GPU_OP_GET_INSTANCE_PROC_ADDR = 9,	/* vkGetInstanceProcAddr */
	GPU_OP_GET_DEVICE_PROC_ADDR = 10,	/* vkGetDeviceProcAddr */
	GPU_OP_CREATE_DEVICE = 11,	/* vkCreateDevice */
	GPU_OP_DESTROY_DEVICE = 12,	/* vkDestroyDevice */
	GPU_OP_ENUMERATE_INSTANCE_EXTENSION_PROPERTIES = 13,	/* vkEnumerateInstanceExtensionProperties */
	GPU_OP_ENUMERATE_DEVICE_EXTENSION_PROPERTIES = 14,	/* vkEnumerateDeviceExtensionProperties */
	GPU_OP_ENUMERATE_INSTANCE_LAYER_PROPERTIES = 15,	/* vkEnumerateInstanceLayerProperties */
	GPU_OP_ENUMERATE_DEVICE_LAYER_PROPERTIES = 16,	/* vkEnumerateDeviceLayerProperties */
	GPU_OP_GET_DEVICE_QUEUE = 17,	/* vkGetDeviceQueue */
	GPU_OP_QUEUE_SUBMIT = 18,	/* vkQueueSubmit */
	GPU_OP_QUEUE_WAIT_IDLE = 19,	/* vkQueueWaitIdle */
	GPU_OP_DEVICE_WAIT_IDLE = 20,	/* vkDeviceWaitIdle */
	GPU_OP_ALLOCATE_MEMORY = 21,	/* vkAllocateMemory */
	GPU_OP_FREE_MEMORY = 22,	/* vkFreeMemory */
	GPU_OP_MAP_MEMORY = 23,	/* vkMapMemory */
	GPU_OP_UNMAP_MEMORY = 24,	/* vkUnmapMemory */
	GPU_OP_FLUSH_MAPPED_MEMORY_RANGES = 25,	/* vkFlushMappedMemoryRanges */
	GPU_OP_INVALIDATE_MAPPED_MEMORY_RANGES = 26,	/* vkInvalidateMappedMemoryRanges */
	GPU_OP_GET_DEVICE_MEMORY_COMMITMENT = 27,	/* vkGetDeviceMemoryCommitment */
	GPU_OP_BIND_BUFFER_MEMORY = 28,	/* vkBindBufferMemory */
	GPU_OP_BIND_IMAGE_MEMORY = 29,	/* vkBindImageMemory */
	GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS = 30,	/* vkGetBufferMemoryRequirements */
	GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS = 31,	/* vkGetImageMemoryRequirements */
	GPU_OP_GET_IMAGE_SPARSE_MEMORY_REQUIREMENTS = 32,	/* vkGetImageSparseMemoryRequirements */
	GPU_OP_GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES = 33,	/* vkGetPhysicalDeviceSparseImageFormatProperties */
	GPU_OP_QUEUE_BIND_SPARSE = 34,	/* vkQueueBindSparse */
	GPU_OP_CREATE_FENCE = 35,	/* vkCreateFence */
	GPU_OP_DESTROY_FENCE = 36,	/* vkDestroyFence */
	GPU_OP_RESET_FENCES = 37,	/* vkResetFences */
	GPU_OP_GET_FENCE_STATUS = 38,	/* vkGetFenceStatus */
	GPU_OP_WAIT_FOR_FENCES = 39,	/* vkWaitForFences */
	GPU_OP_CREATE_SEMAPHORE = 40,	/* vkCreateSemaphore */
	GPU_OP_DESTROY_SEMAPHORE = 41,	/* vkDestroySemaphore */
	GPU_OP_CREATE_EVENT = 42,	/* vkCreateEvent */
	GPU_OP_DESTROY_EVENT = 43,	/* vkDestroyEvent */
	GPU_OP_GET_EVENT_STATUS = 44,	/* vkGetEventStatus */
	GPU_OP_SET_EVENT = 45,	/* vkSetEvent */
	GPU_OP_RESET_EVENT = 46,	/* vkResetEvent */
	GPU_OP_CREATE_QUERY_POOL = 47,	/* vkCreateQueryPool */
	GPU_OP_DESTROY_QUERY_POOL = 48,	/* vkDestroyQueryPool */
	GPU_OP_GET_QUERY_POOL_RESULTS = 49,	/* vkGetQueryPoolResults */
	GPU_OP_CREATE_BUFFER = 50,	/* vkCreateBuffer */
	GPU_OP_DESTROY_BUFFER = 51,	/* vkDestroyBuffer */
	GPU_OP_CREATE_BUFFER_VIEW = 52,	/* vkCreateBufferView */
	GPU_OP_DESTROY_BUFFER_VIEW = 53,	/* vkDestroyBufferView */
	GPU_OP_CREATE_IMAGE = 54,	/* vkCreateImage */
	GPU_OP_DESTROY_IMAGE = 55,	/* vkDestroyImage */
	GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT = 56,	/* vkGetImageSubresourceLayout */
	GPU_OP_CREATE_IMAGE_VIEW = 57,	/* vkCreateImageView */
	GPU_OP_DESTROY_IMAGE_VIEW = 58,	/* vkDestroyImageView */
	GPU_OP_CREATE_SHADER_MODULE = 59,	/* vkCreateShaderModule */
	GPU_OP_DESTROY_SHADER_MODULE = 60,	/* vkDestroyShaderModule */
	GPU_OP_CREATE_PIPELINE_CACHE = 61,	/* vkCreatePipelineCache */
	GPU_OP_DESTROY_PIPELINE_CACHE = 62,	/* vkDestroyPipelineCache */
	GPU_OP_GET_PIPELINE_CACHE_DATA = 63,	/* vkGetPipelineCacheData */
	GPU_OP_MERGE_PIPELINE_CACHES = 64,	/* vkMergePipelineCaches */
	GPU_OP_CREATE_GRAPHICS_PIPELINES = 65,	/* vkCreateGraphicsPipelines */
	GPU_OP_CREATE_COMPUTE_PIPELINES = 66,	/* vkCreateComputePipelines */
	GPU_OP_DESTROY_PIPELINE = 67,	/* vkDestroyPipeline */
	GPU_OP_CREATE_PIPELINE_LAYOUT = 68,	/* vkCreatePipelineLayout */
	GPU_OP_DESTROY_PIPELINE_LAYOUT = 69,	/* vkDestroyPipelineLayout */
	GPU_OP_CREATE_SAMPLER = 70,	/* vkCreateSampler */
	GPU_OP_DESTROY_SAMPLER = 71,	/* vkDestroySampler */
	GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT = 72,	/* vkCreateDescriptorSetLayout */
	GPU_OP_DESTROY_DESCRIPTOR_SET_LAYOUT = 73,	/* vkDestroyDescriptorSetLayout */
	GPU_OP_CREATE_DESCRIPTOR_POOL = 74,	/* vkCreateDescriptorPool */
	GPU_OP_DESTROY_DESCRIPTOR_POOL = 75,	/* vkDestroyDescriptorPool */
	GPU_OP_RESET_DESCRIPTOR_POOL = 76,	/* vkResetDescriptorPool */
	GPU_OP_ALLOCATE_DESCRIPTOR_SETS = 77,	/* vkAllocateDescriptorSets */
	GPU_OP_FREE_DESCRIPTOR_SETS = 78,	/* vkFreeDescriptorSets */
	GPU_OP_UPDATE_DESCRIPTOR_SETS = 79,	/* vkUpdateDescriptorSets */
	GPU_OP_CREATE_FRAMEBUFFER = 80,	/* vkCreateFramebuffer */
	GPU_OP_DESTROY_FRAMEBUFFER = 81,	/* vkDestroyFramebuffer */
	GPU_OP_CREATE_RENDER_PASS = 82,	/* vkCreateRenderPass */
	GPU_OP_DESTROY_RENDER_PASS = 83,	/* vkDestroyRenderPass */
	GPU_OP_GET_RENDER_AREA_GRANULARITY = 84,	/* vkGetRenderAreaGranularity */
	GPU_OP_CREATE_COMMAND_POOL = 85,	/* vkCreateCommandPool */
	GPU_OP_DESTROY_COMMAND_POOL = 86,	/* vkDestroyCommandPool */
	GPU_OP_RESET_COMMAND_POOL = 87,	/* vkResetCommandPool */
	GPU_OP_ALLOCATE_COMMAND_BUFFERS = 88,	/* vkAllocateCommandBuffers */
	GPU_OP_FREE_COMMAND_BUFFERS = 89,	/* vkFreeCommandBuffers */
	GPU_OP_BEGIN_COMMAND_BUFFER = 90,	/* vkBeginCommandBuffer */
	GPU_OP_END_COMMAND_BUFFER = 91,	/* vkEndCommandBuffer */
	GPU_OP_RESET_COMMAND_BUFFER = 92,	/* vkResetCommandBuffer */
	GPU_OP_CMD_BIND_PIPELINE = 93,	/* vkCmdBindPipeline */
	GPU_OP_CMD_SET_VIEWPORT = 94,	/* vkCmdSetViewport */
	GPU_OP_CMD_SET_SCISSOR = 95,	/* vkCmdSetScissor */
	GPU_OP_CMD_SET_LINE_WIDTH = 96,	/* vkCmdSetLineWidth */
	GPU_OP_CMD_SET_DEPTH_BIAS = 97,	/* vkCmdSetDepthBias */
	GPU_OP_CMD_SET_BLEND_CONSTANTS = 98,	/* vkCmdSetBlendConstants */
	GPU_OP_CMD_SET_DEPTH_BOUNDS = 99,	/* vkCmdSetDepthBounds */
	GPU_OP_CMD_SET_STENCIL_COMPARE_MASK = 100,	/* vkCmdSetStencilCompareMask */
	GPU_OP_CMD_SET_STENCIL_WRITE_MASK = 101,	/* vkCmdSetStencilWriteMask */
	GPU_OP_CMD_SET_STENCIL_REFERENCE = 102,	/* vkCmdSetStencilReference */
	GPU_OP_CMD_BIND_DESCRIPTOR_SETS = 103,	/* vkCmdBindDescriptorSets */
	GPU_OP_CMD_BIND_INDEX_BUFFER = 104,	/* vkCmdBindIndexBuffer */
	GPU_OP_CMD_BIND_VERTEX_BUFFERS = 105,	/* vkCmdBindVertexBuffers */
	GPU_OP_CMD_DRAW = 106,	/* vkCmdDraw */
	GPU_OP_CMD_DRAW_INDEXED = 107,	/* vkCmdDrawIndexed */
	GPU_OP_CMD_DRAW_INDIRECT = 108,	/* vkCmdDrawIndirect */
	GPU_OP_CMD_DRAW_INDEXED_INDIRECT = 109,	/* vkCmdDrawIndexedIndirect */
	GPU_OP_CMD_DISPATCH = 110,	/* vkCmdDispatch */
	GPU_OP_CMD_DISPATCH_INDIRECT = 111,	/* vkCmdDispatchIndirect */
	GPU_OP_CMD_COPY_BUFFER = 112,	/* vkCmdCopyBuffer */
	GPU_OP_CMD_COPY_IMAGE = 113,	/* vkCmdCopyImage */
	GPU_OP_CMD_BLIT_IMAGE = 114,	/* vkCmdBlitImage */
	GPU_OP_CMD_COPY_BUFFER_TO_IMAGE = 115,	/* vkCmdCopyBufferToImage */
	GPU_OP_CMD_COPY_IMAGE_TO_BUFFER = 116,	/* vkCmdCopyImageToBuffer */
	GPU_OP_CMD_UPDATE_BUFFER = 117,	/* vkCmdUpdateBuffer */
	GPU_OP_CMD_FILL_BUFFER = 118,	/* vkCmdFillBuffer */
	GPU_OP_CMD_CLEAR_COLOR_IMAGE = 119,	/* vkCmdClearColorImage */
	GPU_OP_CMD_CLEAR_DEPTH_STENCIL_IMAGE = 120,	/* vkCmdClearDepthStencilImage */
	GPU_OP_CMD_CLEAR_ATTACHMENTS = 121,	/* vkCmdClearAttachments */
	GPU_OP_CMD_RESOLVE_IMAGE = 122,	/* vkCmdResolveImage */
	GPU_OP_CMD_SET_EVENT = 123,	/* vkCmdSetEvent */
	GPU_OP_CMD_RESET_EVENT = 124,	/* vkCmdResetEvent */
	GPU_OP_CMD_WAIT_EVENTS = 125,	/* vkCmdWaitEvents */
	GPU_OP_CMD_PIPELINE_BARRIER = 126,	/* vkCmdPipelineBarrier */
	GPU_OP_CMD_BEGIN_QUERY = 127,	/* vkCmdBeginQuery */
	GPU_OP_CMD_END_QUERY = 128,	/* vkCmdEndQuery */
	GPU_OP_CMD_RESET_QUERY_POOL = 129,	/* vkCmdResetQueryPool */
	GPU_OP_CMD_WRITE_TIMESTAMP = 130,	/* vkCmdWriteTimestamp */
	GPU_OP_CMD_COPY_QUERY_POOL_RESULTS = 131,	/* vkCmdCopyQueryPoolResults */
	GPU_OP_CMD_PUSH_CONSTANTS = 132,	/* vkCmdPushConstants */
	GPU_OP_CMD_BEGIN_RENDER_PASS = 133,	/* vkCmdBeginRenderPass */
	GPU_OP_CMD_NEXT_SUBPASS = 134,	/* vkCmdNextSubpass */
	GPU_OP_CMD_END_RENDER_PASS = 135,	/* vkCmdEndRenderPass */
	GPU_OP_CMD_EXECUTE_COMMANDS = 136,	/* vkCmdExecuteCommands */
	GPU_OP_ENUMERATE_INSTANCE_VERSION = 137,	/* vkEnumerateInstanceVersion */
	GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2 = 148,	/* vkGetPhysicalDeviceProperties2 */
	GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES2 = 150,	/* vkGetPhysicalDeviceImageFormatProperties2 */
	GPU_OP_GET_DEVICE_QUEUE2 = 155,	/* vkGetDeviceQueue2 */
	GPU_OP_GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES = 161,	/* vkGetPhysicalDeviceExternalBufferProperties */
	GPU_OP_SET_REPLY_STREAM = 178,	/* the stream's replies go to a resource */
	GPU_OP_SEEK_REPLY_STREAM = 179,	/* moves the reply position */
	GPU_OP_EXECUTE_STREAMS = 180,	/* runs command streams held in resources */

	/*
	 * zedBSD's own commands (ws083): Vulkan Video, H.264 decode.  Only a
	 * backend whose capset declares video takes them (the native i915);
	 * they are never sent to a Venus host.
	 */
	GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_CAPABILITIES = 0x10000,	/* vkGetPhysicalDeviceVideoCapabilitiesKHR */
	GPU_OP_GET_PHYSICAL_DEVICE_VIDEO_FORMAT_PROPERTIES = 0x10001,	/* vkGetPhysicalDeviceVideoFormatPropertiesKHR */
	GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_VIDEO_PROPERTIES = 0x10002,	/* the codec operations of each queue family */
	GPU_OP_CREATE_VIDEO_SESSION = 0x10003,	/* vkCreateVideoSessionKHR */
	GPU_OP_DESTROY_VIDEO_SESSION = 0x10004,	/* vkDestroyVideoSessionKHR */
	GPU_OP_GET_VIDEO_SESSION_MEMORY_REQUIREMENTS = 0x10005,	/* vkGetVideoSessionMemoryRequirementsKHR */
	GPU_OP_BIND_VIDEO_SESSION_MEMORY = 0x10006,	/* vkBindVideoSessionMemoryKHR */
	GPU_OP_CREATE_VIDEO_SESSION_PARAMETERS = 0x10007,	/* vkCreateVideoSessionParametersKHR */
	GPU_OP_UPDATE_VIDEO_SESSION_PARAMETERS = 0x10008,	/* vkUpdateVideoSessionParametersKHR */
	GPU_OP_DESTROY_VIDEO_SESSION_PARAMETERS = 0x10009,	/* vkDestroyVideoSessionParametersKHR */
	GPU_OP_CMD_BEGIN_VIDEO_CODING = 0x1000a,	/* vkCmdBeginVideoCodingKHR */
	GPU_OP_CMD_END_VIDEO_CODING = 0x1000b,	/* vkCmdEndVideoCodingKHR */
	GPU_OP_CMD_CONTROL_VIDEO_CODING = 0x1000c,	/* vkCmdControlVideoCodingKHR */
	GPU_OP_CMD_DECODE_VIDEO = 0x1000d	/* vkCmdDecodeVideoKHR */
};

#endif
