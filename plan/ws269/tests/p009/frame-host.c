/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The client side of the executor host check: command frames written
 * with libvulkan's own wire and record encoders (userland/desktop/
 * libvulkan wire.c and codec.c, linked unchanged), in the order and with
 * the framing libvulkan's instance.c, device.c, memory.c, resources*.c,
 * descriptors.c, pipeline.c, commands*.c, queue.c and context.c use, and
 * the checks of the replies the executor wrote, read with libvulkan's
 * reader and record decoders.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "userland/desktop/libkeiland/ui/shaders.h"
#include "userland/desktop/wayland/shaders.h"
#include "frame-host.h"

/* The places of the reply region: the replies from 0, the trailer in the last 20 bytes. */
#define REPLY_TRAILER_BYTES	20U

/* The wire identities the frames use. */
#define ID_INSTANCE		1U
#define ID_PHYSICAL		2U
#define ID_DEVICE		3U
#define ID_QUEUE		4U
#define ID_MEMORY		10U
#define ID_BUFFER		11U
#define ID_SHADER		12U
#define ID_SET_LAYOUT		13U
#define ID_PIPELINE_LAYOUT	14U
#define ID_PIPELINE		15U
#define ID_DESCRIPTOR_POOL	16U
#define ID_SET			17U
#define ID_COMMAND_POOL		18U
#define ID_COMMAND		19U
#define ID_FENCE		20U
#define ID_BAD_SHADER		21U
#define ID_BAD_PIPELINE		22U
#define ID_SHARED_IMAGE		23U
#define ID_EXPORT_MEMORY	24U
#define ID_IMPORT_MEMORY	25U
#define ID_LINEAR_IMAGE		26U
#define ID_IMAGE_MEMORY		27U
#define ID_IMAGE		28U
#define ID_VIEW			29U
#define ID_SAMPLER		30U
#define ID_UNKNOWN_IMPORT	31U
#define ID_OTHER_HANDLE_IMAGE	32U

/* The Keiland-like frame's objects (ws269-p011). */
#define ID_K_MEMORY		40U
#define ID_K_TARGET		41U
#define ID_K_VERTEX		42U
#define ID_K_TARGET_VIEW	43U
#define ID_K_PASS		44U
#define ID_K_FRAMEBUFFER	45U
#define ID_K_QUAD_VERT		46U
#define ID_K_QUAD_FRAG		47U
#define ID_K_CANVAS_VERT	48U
#define ID_K_CANVAS_FRAG	49U
#define ID_K_SET_LAYOUT		50U
#define ID_K_QUAD_LAYOUT	51U
#define ID_K_CANVAS_LAYOUT	52U
#define ID_K_QUAD_PIPELINE	53U
#define ID_K_CANVAS_PIPELINE	54U
#define ID_K_POOL		55U
#define ID_K_SET		56U
#define ID_K_COMMAND_A		57U
#define ID_K_COMMAND_B		58U
#define ID_K_FENCE_A		59U
#define ID_K_FENCE_B		60U

/* The Keiland-like frame's target: 64x64 BGRA8. */
#define K_EDGE			64U

/* The shared path's switch the executor is built with (image-state.h). */
#ifndef POWERVR_FORMAT_LINEAR_SAMPLED
#define POWERVR_FORMAT_LINEAR_SAMPLED	0
#endif

/* Local objects whose pointers are the handles libvulkan's encoders turn into wire identities. */
static struct vulkan_object objects[64];

static int failures;
static int passes;

/* The libvulkan symbols the linked wire and codec files name but the check does not use. */
void vulkan_context_error(struct vulkan_context *context, VkResult error) { (void)context; (void)error; }
VkResult vulkan_context_execute(struct vulkan_context *context, const struct vulkan_writer *writer, size_t reply_capacity, struct vulkan_reader *reader) { (void)context; (void)writer; (void)reply_capacity; (void)reader; return VK_ERROR_DEVICE_LOST; }
struct vulkan_object *vulkan_nondispatchable_object(uint64_t handle) { return (struct vulkan_object *)(uintptr_t)handle; }
uint64_t vulkan_object_wire_id(const struct vulkan_object *object) { return object == NULL ? 0 : object->wire_id; }

/* The handle of a wire identity. */
static uint64_t
handle(
	uint64_t identity)
{
	objects[identity].wire_id = identity;
	return (uint64_t)(uintptr_t)&objects[identity];
}

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: frame: %s\n", what);
}

/* Appends one command's bytes to the frame. */
static void
append(
	struct vulkan_writer *frame,
	struct vulkan_writer *command)
{
	vulkan_write_bytes(frame, command->data, command->bytes);
	vulkan_writer_finish(command);
}

/* Starts a frame with its reply region. */
static void
frame_begin(
	struct vulkan_writer *frame,
	uint32_t reply_blob)
{
	vulkan_writer_init(frame);
	vulkan_write_u32(frame, GPU_OP_SET_REPLY_STREAM);
	vulkan_write_u32(frame, 0);
	vulkan_write_u64(frame, 1);
	vulkan_write_u32(frame, reply_blob);
	vulkan_write_u64(frame, 0);
	vulkan_write_u64(frame, FRAME_REPLY_BYTES);
}

/* Ends a frame with the decoder's trailer, and hands its bytes over. */
static size_t
frame_end(
	struct vulkan_writer *frame,
	uint8_t **bytes)
{
	vulkan_write_u32(frame, GPU_OP_SEEK_REPLY_STREAM);
	vulkan_write_u32(frame, 0);
	vulkan_write_u64(frame, FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES);
	vulkan_write_u32(frame, GPU_OP_ENUMERATE_INSTANCE_VERSION);
	vulkan_write_u32(frame, 1);
	vulkan_write_u64(frame, 1);
	check(frame->error == VK_SUCCESS, "frame encodes");
	*bytes = frame->data;
	return frame->bytes;
}

/* A create's tail: [allocator][present][identity]. */
static void
create_tail(
	struct vulkan_writer *writer,
	uint64_t identity)
{
	vulkan_write_u64(writer, 0);
	vulkan_write_u64(writer, 1);
	vulkan_write_u64(writer, identity);
}

/*
 * The first frame: the instance, the physical device and its queries,
 * the device and its queue.
 */
size_t
frame_setup(
	uint32_t reply_blob,
	uint8_t **bytes)
{
	static const char *const none[1] = { NULL };
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	struct vulkan_writer frame;
	struct vulkan_writer w;
	float priority;

	frame_begin(&frame, reply_blob);

	/* vkCreateInstance. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.apiVersion = VK_MAKE_VERSION(1, 1, 0);
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	instance.ppEnabledLayerNames = none;
	instance.ppEnabledExtensionNames = none;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_INSTANCE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkInstanceCreateInfo(&w, &instance);
	create_tail(&w, ID_INSTANCE);
	append(&frame, &w);

	/* vkEnumeratePhysicalDevices: the count, then the array of one. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ENUMERATE_PHYSICAL_DEVICES);
	vulkan_write_u64(&w, ID_INSTANCE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ENUMERATE_PHYSICAL_DEVICES);
	vulkan_write_u64(&w, ID_INSTANCE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, ID_PHYSICAL);
	append(&frame, &w);

	/* The physical device's properties, features and memory. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u64(&w, 1);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_FEATURES);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u64(&w, 1);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, VK_MAX_MEMORY_TYPES);
	vulkan_write_u64(&w, VK_MAX_MEMORY_HEAPS);
	append(&frame, &w);

	/* The queue families: the count. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);

	/* vkCreateDevice with one queue of family 0. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = 0;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1;
	device.pQueueCreateInfos = &queue;
	device.ppEnabledLayerNames = none;
	device.ppEnabledExtensionNames = none;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_DEVICE);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkDeviceCreateInfo(&w, &device);
	create_tail(&w, ID_DEVICE);
	append(&frame, &w);

	/* vkGetDeviceQueue2 with its timeline record (timeline 1). */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_DEVICE_QUEUE2);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, 1000145003U);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, GPU_OP_RECORD_QUEUE_TIMELINE);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, ID_QUEUE);
	append(&frame, &w);

	return frame_end(&frame, bytes);
}

/* The second frame's first part: the memory (host visible) the blob is made for. */
size_t
frame_memory(
	uint32_t reply_blob,
	uint64_t memory_bytes,
	uint8_t **bytes)
{
	struct vulkan_writer frame;
	struct vulkan_writer w;

	frame_begin(&frame, reply_blob);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ALLOCATE_MEMORY);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, memory_bytes);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, ID_MEMORY);
	append(&frame, &w);
	return frame_end(&frame, bytes);
}

/* Appends a vkAllocateMemory of bytes; chain_type 0 for none, else one record of that sType and value. */
static void
allocate_memory(
	struct vulkan_writer *frame,
	uint64_t identity,
	uint64_t bytes,
	uint32_t chain_type,
	uint32_t chain_value)
{
	struct vulkan_writer w;

	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ALLOCATE_MEMORY);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	vulkan_write_u64(&w, chain_type != 0 ? 1 : 0);
	if (chain_type != 0) {
		vulkan_write_u32(&w, chain_type);
		vulkan_write_u64(&w, 0);
		vulkan_write_u32(&w, chain_value);
	}
	vulkan_write_u64(&w, bytes);
	vulkan_write_u32(&w, 0);
	create_tail(&w, identity);
	append(frame, &w);
}

/* Appends a vkCreateImage of a 2D colour image, with libvulkan's external declaration of these handle types when external is nonzero. */
static void
create_image(
	struct vulkan_writer *frame,
	uint64_t identity,
	VkFormat format,
	uint32_t width,
	uint32_t height,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	int external)
{
	VkExternalMemoryImageCreateInfo declaration;
	VkImageCreateInfo image;
	struct vulkan_writer w;

	memset(&declaration, 0, sizeof(declaration));
	declaration.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
	declaration.handleTypes = external > 1 ? (VkExternalMemoryHandleTypeFlags)external : VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	if (external)
		image.pNext = &declaration;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = format;
	image.extent.width = width;
	image.extent.height = height;
	image.extent.depth = 1;
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = tiling;
	image.usage = usage;
	image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_IMAGE);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkImageCreateInfo(&w, &image);
	create_tail(&w, identity);
	append(frame, &w);
}

/*
 * The image frame: the format queries, the shared path (an image
 * declaring external memory, an export, an import of the session's blob
 * import_blob, the device's identity, and the refusals of an unknown blob
 * and another handle type), a linear image and its layout, and an
 * optimal image bound to memory with a view and a sampler.
 */
size_t
frame_image(
	uint32_t reply_blob,
	uint32_t import_blob,
	uint8_t **bytes)
{
	VkImageSubresource subresource;
	VkImageViewCreateInfo view;
	VkSamplerCreateInfo sampler;
	struct vulkan_writer frame;
	struct vulkan_writer w;
	int pass;

	frame_begin(&frame, reply_blob);

	/* The format's features, then the image queries of each tiling. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u32(&w, VK_FORMAT_R8G8B8A8_UNORM);
	vulkan_write_u64(&w, 1);
	append(&frame, &w);
	for (pass = 0; pass < 2; pass++) {
		vulkan_writer_init(&w);
		vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES);
		vulkan_write_u64(&w, ID_PHYSICAL);
		vulkan_write_u32(&w, VK_FORMAT_B8G8R8A8_UNORM);
		vulkan_write_u32(&w, VK_IMAGE_TYPE_2D);
		vulkan_write_u32(&w, pass == 0 ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL);
		vulkan_write_u32(&w, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		vulkan_write_u32(&w, 0);
		vulkan_write_u64(&w, 1);
		append(&frame, &w);
	}

	/* The shared path: an external image, an export, an import, then the refusals. */
	create_image(&frame, ID_SHARED_IMAGE, VK_FORMAT_B8G8R8A8_UNORM, 64, 64, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 1);
	allocate_memory(&frame, ID_EXPORT_MEMORY, 16384, VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
	allocate_memory(&frame, ID_IMPORT_MEMORY, 16384, GPU_OP_RECORD_IMPORT_RESOURCE, import_blob);
	allocate_memory(&frame, ID_UNKNOWN_IMPORT, 16384, GPU_OP_RECORD_IMPORT_RESOURCE, 999);
	create_image(&frame, ID_OTHER_HANDLE_IMAGE, VK_FORMAT_B8G8R8A8_UNORM, 64, 64, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);

	/* The device's identity: properties2 with the ID properties. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2);
	vulkan_write_u64(&w, ID_PHYSICAL);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);

	/* A linear image of 100 texels a row and its layout. */
	create_image(&frame, ID_LINEAR_IMAGE, VK_FORMAT_R8G8B8A8_UNORM, 100, 10, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
	memset(&subresource, 0, sizeof(subresource));
	subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_LINEAR_IMAGE));
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkImageSubresource(&w, &subresource);
	vulkan_write_u64(&w, 1);
	append(&frame, &w);

	/* An optimal image, its requirements, its memory and binding. */
	allocate_memory(&frame, ID_IMAGE_MEMORY, 8192, 0, 0);
	create_image(&frame, ID_IMAGE, VK_FORMAT_R8G8B8A8_UNORM, 30, 20, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_IMAGE));
	vulkan_write_u64(&w, 1);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_BIND_IMAGE_MEMORY);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_IMAGE));
	vulkan_encode_handle(&w, handle(ID_IMAGE_MEMORY));
	vulkan_write_u64(&w, 4096);
	append(&frame, &w);

	/* Its view and a sampler. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = (VkImage)(uintptr_t)handle(ID_IMAGE);
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_R8G8B8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	view.subresourceRange.layerCount = 1;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkImageViewCreateInfo(&w, &view);
	create_tail(&w, ID_VIEW);
	append(&frame, &w);
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.maxLod = 0.25f;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_SAMPLER);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkSamplerCreateInfo(&w, &sampler);
	create_tail(&w, ID_SAMPLER);
	append(&frame, &w);

	return frame_end(&frame, bytes);
}

/* Appends one recorded command's header: [opcode][0][command buffer]. */
static void
record_begin(
	struct vulkan_writer *w,
	uint32_t opcode)
{
	vulkan_writer_init(w);
	w->bytes = 0;
	vulkan_write_u32(w, opcode);
	vulkan_write_u32(w, 0);
	vulkan_write_u64(w, ID_COMMAND);
}

/*
 * The compute frame: a buffer bound to the memory, the shader module,
 * the layouts, the pipeline, the descriptor set, a command buffer that
 * binds and dispatches groups workgroups, and its submission; also a
 * shader outside the subset and its refused pipeline.
 */
size_t
frame_compute(
	uint32_t reply_blob,
	const uint32_t *spirv,
	size_t spirv_words,
	const uint32_t *bad_spirv,
	size_t bad_words,
	uint32_t groups,
	uint8_t **bytes)
{
	VkBufferCreateInfo buffer;
	VkShaderModuleCreateInfo module;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkDescriptorSetLayout set_layout_handle;
	VkPipelineLayoutCreateInfo pipeline_layout;
	VkComputePipelineCreateInfo pipeline;
	VkDescriptorPoolSize pool_size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorBufferInfo descriptor;
	VkCommandPoolCreateInfo command_pool;
	VkCommandBufferAllocateInfo command_allocate;
	VkCommandBufferBeginInfo begin;
	VkMemoryBarrier barrier;
	struct vulkan_writer frame;
	struct vulkan_writer w;
	int pass;

	frame_begin(&frame, reply_blob);

	/* vkCreateBuffer, its requirements, and its binding at offset 0. */
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = FRAME_BUFFER_BYTES;
	buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_BUFFER);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkBufferCreateInfo(&w, &buffer);
	create_tail(&w, ID_BUFFER);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_BUFFER));
	vulkan_write_u64(&w, 1);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_BIND_BUFFER_MEMORY);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_BUFFER));
	vulkan_encode_handle(&w, handle(ID_MEMORY));
	vulkan_write_u64(&w, 0);
	append(&frame, &w);

	/* The two shader modules. */
	for (pass = 0; pass < 2; pass++) {
		memset(&module, 0, sizeof(module));
		module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		module.codeSize = (pass == 0 ? spirv_words : bad_words) * 4U;
		module.pCode = pass == 0 ? spirv : bad_spirv;
		vulkan_writer_init(&w);
		vulkan_command_begin(&w, GPU_OP_CREATE_SHADER_MODULE);
		vulkan_write_u64(&w, ID_DEVICE);
		vulkan_write_u64(&w, 1);
		vulkan_encode_VkShaderModuleCreateInfo(&w, &module);
		create_tail(&w, pass == 0 ? ID_SHADER : ID_BAD_SHADER);
		append(&frame, &w);
	}

	/* The set layout of one storage buffer, and the pipeline layout of that one set. */
	memset(&binding, 0, sizeof(binding));
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	memset(&set_layout, 0, sizeof(set_layout));
	set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout.bindingCount = 1;
	set_layout.pBindings = &binding;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_pointer(&w, &set_layout);
	vulkan_encode_VkDescriptorSetLayoutCreateInfo(&w, &set_layout);
	create_tail(&w, ID_SET_LAYOUT);
	append(&frame, &w);
	set_layout_handle = (VkDescriptorSetLayout)(uintptr_t)handle(ID_SET_LAYOUT);
	memset(&pipeline_layout, 0, sizeof(pipeline_layout));
	pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout.setLayoutCount = 1;
	pipeline_layout.pSetLayouts = &set_layout_handle;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_PIPELINE_LAYOUT);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkPipelineLayoutCreateInfo(&w, &pipeline_layout);
	create_tail(&w, ID_PIPELINE_LAYOUT);
	append(&frame, &w);

	/* The two compute pipelines, one per module. */
	for (pass = 0; pass < 2; pass++) {
		memset(&pipeline, 0, sizeof(pipeline));
		pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipeline.stage.module = (VkShaderModule)(uintptr_t)handle(pass == 0 ? ID_SHADER : ID_BAD_SHADER);
		pipeline.stage.pName = "main";
		pipeline.layout = (VkPipelineLayout)(uintptr_t)handle(ID_PIPELINE_LAYOUT);
		pipeline.basePipelineIndex = -1;
		vulkan_writer_init(&w);
		vulkan_command_begin(&w, GPU_OP_CREATE_COMPUTE_PIPELINES);
		vulkan_write_u64(&w, ID_DEVICE);
		vulkan_write_u64(&w, 0);
		vulkan_write_u32(&w, 1);
		vulkan_write_u64(&w, 1);
		vulkan_encode_VkComputePipelineCreateInfo(&w, &pipeline);
		vulkan_write_u64(&w, 0);
		vulkan_write_u64(&w, 1);
		vulkan_write_u64(&w, pass == 0 ? ID_PIPELINE : ID_BAD_PIPELINE);
		append(&frame, &w);
	}

	/* The descriptor pool, the set, and its binding 0 pointing at the whole buffer. */
	memset(&pool_size, 0, sizeof(pool_size));
	pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_size.descriptorCount = 1;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &pool_size;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_DESCRIPTOR_POOL);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_pointer(&w, &pool);
	vulkan_encode_VkDescriptorPoolCreateInfo(&w, &pool);
	create_tail(&w, ID_DESCRIPTOR_POOL);
	append(&frame, &w);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = (VkDescriptorPool)(uintptr_t)handle(ID_DESCRIPTOR_POOL);
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &set_layout_handle;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ALLOCATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_pointer(&w, &allocate);
	vulkan_encode_VkDescriptorSetAllocateInfo(&w, &allocate);
	vulkan_write_u64(&w, 1);
	vulkan_encode_handle(&w, handle(ID_SET));
	append(&frame, &w);
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.buffer = (VkBuffer)(uintptr_t)handle(ID_BUFFER);
	descriptor.offset = 0;
	descriptor.range = VK_WHOLE_SIZE;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_UPDATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, ID_SET);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u32(&w, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkDescriptorBufferInfo(&w, &descriptor);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);

	/* The command pool, one command buffer, and its recording. */
	memset(&command_pool, 0, sizeof(command_pool));
	command_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_COMMAND_POOL);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkCommandPoolCreateInfo(&w, &command_pool);
	create_tail(&w, ID_COMMAND_POOL);
	append(&frame, &w);
	memset(&command_allocate, 0, sizeof(command_allocate));
	command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_allocate.commandPool = (VkCommandPool)(uintptr_t)handle(ID_COMMAND_POOL);
	command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_allocate.commandBufferCount = 1;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ALLOCATE_COMMAND_BUFFERS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkCommandBufferAllocateInfo(&w, &command_allocate);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, ID_COMMAND);
	append(&frame, &w);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_BEGIN_COMMAND_BUFFER);
	vulkan_write_u64(&w, ID_COMMAND);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkCommandBufferBeginInfo(&w, &begin);
	append(&frame, &w);

	/* The recorded commands, as commands-generated.inc writes them. */
	record_begin(&w, GPU_OP_CMD_BIND_PIPELINE);
	vulkan_write_u32(&w, VK_PIPELINE_BIND_POINT_COMPUTE);
	vulkan_encode_handle(&w, handle(ID_PIPELINE));
	append(&frame, &w);
	record_begin(&w, GPU_OP_CMD_BIND_DESCRIPTOR_SETS);
	vulkan_write_u32(&w, VK_PIPELINE_BIND_POINT_COMPUTE);
	vulkan_encode_handle(&w, handle(ID_PIPELINE_LAYOUT));
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_handle(&w, handle(ID_SET));
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);
	record_begin(&w, GPU_OP_CMD_DISPATCH);
	vulkan_write_u32(&w, groups);
	vulkan_write_u32(&w, 1);
	vulkan_write_u32(&w, 1);
	append(&frame, &w);
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	record_begin(&w, GPU_OP_CMD_PIPELINE_BARRIER);
	vulkan_write_u32(&w, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	vulkan_write_u32(&w, VK_PIPELINE_STAGE_HOST_BIT);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkMemoryBarrier(&w, &barrier);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);

	/* vkEndCommandBuffer. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_END_COMMAND_BUFFER);
	vulkan_write_u64(&w, ID_COMMAND);
	append(&frame, &w);

	/* A fence, and vkQueueSubmit of the command buffer with it. */
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_FENCE);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	create_tail(&w, ID_FENCE);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_QUEUE_SUBMIT);
	vulkan_write_u64(&w, ID_QUEUE);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_SUBMIT_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, ID_COMMAND);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, ID_FENCE);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_FENCE_STATUS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, ID_FENCE);
	append(&frame, &w);

	return frame_end(&frame, bytes);
}

/* Reads a reply's echoed opcode and checks it. */
static void
expect_opcode(
	struct vulkan_reader *reader,
	uint32_t opcode,
	const char *what)
{
	uint32_t echoed;

	echoed = vulkan_read_u32(reader);
	check(echoed == opcode, what);
}

/* Checks a create's reply: [opcode][result][present][identity]. */
static void
expect_created(
	struct vulkan_reader *reader,
	uint32_t opcode,
	uint64_t identity,
	const char *what)
{
	uint32_t result;
	uint64_t present;
	uint64_t answered;

	expect_opcode(reader, opcode, what);
	result = vulkan_read_u32(reader);
	present = vulkan_read_u64(reader);
	answered = vulkan_read_u64(reader);
	check(result == VK_SUCCESS && present == 1 && answered == identity, what);
}

/* Checks a result-only reply: [opcode][result]. */
static void
expect_result(
	struct vulkan_reader *reader,
	uint32_t opcode,
	uint32_t expected,
	const char *what)
{
	uint32_t result;

	expect_opcode(reader, opcode, what);
	result = vulkan_read_u32(reader);
	check(result == expected, what);
}

/* Checks the decoder's trailer in the last 20 bytes. */
static void
expect_trailer(
	uint8_t *reply)
{
	struct vulkan_reader reader;
	uint32_t version;

	vulkan_reader_init(&reader, NULL, 0);
	reader.data = reply + FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES;
	reader.bytes = REPLY_TRAILER_BYTES;
	expect_opcode(&reader, GPU_OP_ENUMERATE_INSTANCE_VERSION, "trailer opcode");
	check(vulkan_read_u32(&reader) == VK_SUCCESS, "trailer result");
	check(vulkan_read_u64(&reader) == 1, "trailer presence");
	version = vulkan_read_u32(&reader);
	check(version >= VK_MAKE_VERSION(1, 1, 0), "trailer version");
}

/* Checks the replies of the first frame. */
void
replies_setup(
	uint8_t *reply)
{
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;
	VkPhysicalDeviceMemoryProperties memory;
	struct vulkan_reader reader;
	uint32_t count;
	uint64_t array;

	vulkan_reader_init(&reader, NULL, 0);
	reader.data = reply;
	reader.bytes = FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES;

	expect_created(&reader, GPU_OP_CREATE_INSTANCE, ID_INSTANCE, "instance");

	expect_opcode(&reader, GPU_OP_ENUMERATE_PHYSICAL_DEVICES, "enumerate count");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1, "enumerate count result");
	count = vulkan_read_u32(&reader);
	array = vulkan_read_u64(&reader);
	check(count == 1 && array == 0, "one physical device, no array");
	expect_opcode(&reader, GPU_OP_ENUMERATE_PHYSICAL_DEVICES, "enumerate array");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1, "enumerate array result");
	count = vulkan_read_u32(&reader);
	array = vulkan_read_u64(&reader);
	check(count == 1 && array == 1 && vulkan_read_u64(&reader) == ID_PHYSICAL, "the physical device's identity");

	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES, "properties");
	check(vulkan_reply_pointer(&reader), "properties present");
	memset(&properties, 0, sizeof(properties));
	vulkan_decode_VkPhysicalDeviceProperties(&reader, &properties);
	check(properties.apiVersion >= VK_MAKE_VERSION(1, 1, 0), "native version 1.1");
	check(strcmp(properties.deviceName, "zedBSD powervr (PowerVR BXE-4-32)") == 0, "device name");
	check(properties.limits.maxComputeWorkGroupSize[0] == 32, "workgroup size limit");

	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_FEATURES, "features");
	check(vulkan_reply_pointer(&reader), "features present");
	vulkan_decode_VkPhysicalDeviceFeatures(&reader, &features);

	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES, "memory");
	check(vulkan_reply_pointer(&reader), "memory present");
	memset(&memory, 0, sizeof(memory));
	vulkan_decode_VkPhysicalDeviceMemoryProperties(&reader, &memory);
	check(memory.memoryTypeCount == 2 && memory.memoryHeapCount == 1, "two types over one heap");
	check(memory.memoryTypes[0].propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "type 0 device local");
	check(memory.memoryTypes[1].propertyFlags == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), "type 1 host visible and coherent, not cached");

	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES, "families");
	check(vulkan_read_u64(&reader) == 1 && vulkan_read_u32(&reader) == 1 && vulkan_read_u64(&reader) == 0, "one family");

	expect_created(&reader, GPU_OP_CREATE_DEVICE, ID_DEVICE, "device");
	expect_opcode(&reader, GPU_OP_GET_DEVICE_QUEUE2, "queue");
	check(vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_QUEUE, "queue identity");
	check(reader.error == VK_SUCCESS, "setup replies read");
	expect_trailer(reply);
}

/* Checks the reply of the memory frame. */
void
replies_memory(
	uint8_t *reply)
{
	struct vulkan_reader reader;

	vulkan_reader_init(&reader, NULL, 0);
	reader.data = reply;
	reader.bytes = FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES;
	expect_created(&reader, GPU_OP_ALLOCATE_MEMORY, ID_MEMORY, "memory");
	expect_trailer(reply);
}

/* Checks the replies of the compute frame. */
void
replies_compute(
	uint8_t *reply)
{
	VkMemoryRequirements requirements;
	struct vulkan_reader reader;
	uint32_t result;

	vulkan_reader_init(&reader, NULL, 0);
	reader.data = reply;
	reader.bytes = FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES;

	expect_created(&reader, GPU_OP_CREATE_BUFFER, ID_BUFFER, "buffer");
	expect_opcode(&reader, GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS, "requirements");
	check(vulkan_reply_pointer(&reader), "requirements present");
	vulkan_decode_VkMemoryRequirements(&reader, &requirements);
	check(requirements.size == FRAME_BUFFER_BYTES && requirements.memoryTypeBits == 3U, "requirements of both types");
	expect_result(&reader, GPU_OP_BIND_BUFFER_MEMORY, VK_SUCCESS, "bind");
	expect_created(&reader, GPU_OP_CREATE_SHADER_MODULE, ID_SHADER, "shader module");
	expect_created(&reader, GPU_OP_CREATE_SHADER_MODULE, ID_BAD_SHADER, "a module outside the subset is still created");
	expect_created(&reader, GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT, ID_SET_LAYOUT, "set layout");
	expect_created(&reader, GPU_OP_CREATE_PIPELINE_LAYOUT, ID_PIPELINE_LAYOUT, "pipeline layout");
	expect_opcode(&reader, GPU_OP_CREATE_COMPUTE_PIPELINES, "pipeline");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_PIPELINE, "pipeline created");
	expect_opcode(&reader, GPU_OP_CREATE_COMPUTE_PIPELINES, "bad pipeline");
	result = vulkan_read_u32(&reader);
	check(result == (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == 0, "a pipeline outside the subset is refused with a null identity");
	expect_created(&reader, GPU_OP_CREATE_DESCRIPTOR_POOL, ID_DESCRIPTOR_POOL, "descriptor pool");
	expect_opcode(&reader, GPU_OP_ALLOCATE_DESCRIPTOR_SETS, "sets");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_SET, "set allocated");
	expect_opcode(&reader, GPU_OP_UPDATE_DESCRIPTOR_SETS, "update");
	expect_created(&reader, GPU_OP_CREATE_COMMAND_POOL, ID_COMMAND_POOL, "command pool");
	expect_opcode(&reader, GPU_OP_ALLOCATE_COMMAND_BUFFERS, "command buffers");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_COMMAND, "command buffer allocated");
	expect_result(&reader, GPU_OP_BEGIN_COMMAND_BUFFER, VK_SUCCESS, "begin");
	expect_result(&reader, GPU_OP_END_COMMAND_BUFFER, VK_SUCCESS, "end");
	expect_created(&reader, GPU_OP_CREATE_FENCE, ID_FENCE, "fence");
	expect_result(&reader, GPU_OP_QUEUE_SUBMIT, VK_SUCCESS, "submit");
	expect_result(&reader, GPU_OP_GET_FENCE_STATUS, VK_SUCCESS, "the fence is signaled after the submission");
	check(reader.error == VK_SUCCESS, "compute replies read");
	expect_trailer(reply);
}

/* Checks the identity reply: the two headers, the UUIDs, no LUID, then the properties. */
static void
expect_identity(
	struct vulkan_reader *reader)
{
	static const uint8_t device_uuid[VK_UUID_SIZE] = {
		'z', 'e', 'd', 'B', 'S', 'D', '-', 'p', 'o', 'w', 'e', 'r', 'v', 'r', 'b', 'x'
	};
	VkPhysicalDeviceProperties properties;
	uint8_t uuid[VK_UUID_SIZE];
	uint8_t driver[VK_UUID_SIZE];
	uint8_t luid[VK_LUID_SIZE];
	int ok;

	expect_opcode(reader, GPU_OP_GET_PHYSICAL_DEVICE_PROPERTIES2, "identity");
	ok = vulkan_read_u64(reader) == 1;
	ok = ok && vulkan_read_u32(reader) == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 && vulkan_read_u64(reader) == 1;
	ok = ok && vulkan_read_u32(reader) == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES && vulkan_read_u64(reader) == 0;
	ok = ok && vulkan_read_u64(reader) == VK_UUID_SIZE;
	vulkan_read_bytes(reader, uuid, VK_UUID_SIZE);
	ok = ok && vulkan_read_u64(reader) == VK_UUID_SIZE;
	vulkan_read_bytes(reader, driver, VK_UUID_SIZE);
	ok = ok && vulkan_read_u64(reader) == VK_LUID_SIZE;
	vulkan_read_bytes(reader, luid, VK_LUID_SIZE);
	ok = ok && vulkan_read_u32(reader) == 0 && vulkan_read_u32(reader) == VK_FALSE;
	memset(&properties, 0, sizeof(properties));
	vulkan_decode_VkPhysicalDeviceProperties(reader, &properties);
	check(ok && memcmp(uuid, device_uuid, VK_UUID_SIZE) == 0 && properties.vendorID != 0, "the identity: headers, the device UUID, no LUID, the properties");
}

/* Checks the replies of the image frame. */
void
replies_image(
	uint8_t *reply)
{
	VkFormatProperties format;
	VkImageFormatProperties limits;
	VkSubresourceLayout layout;
	VkMemoryRequirements requirements;
	struct vulkan_reader reader;
	uint32_t result;

	vulkan_reader_init(&reader, NULL, 0);
	reader.data = reply;
	reader.bytes = FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES;

	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES, "format");
	check(vulkan_reply_pointer(&reader), "format present");
	memset(&format, 0, sizeof(format));
	vulkan_decode_VkFormatProperties(&reader, &format);
	if (POWERVR_FORMAT_LINEAR_SAMPLED)
		check((format.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 && (format.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0 && (format.linearTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0, "switch on: RGBA8 linear samples and filters (the shared path)");
	else
		check((format.linearTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) == 0 && (format.linearTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0, "switch off: RGBA8 linear copies but does not sample (libvulkan stays off the shared path)");
	check((format.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0 && (format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0, "RGBA8 optimal draws and samples");
	check(format.bufferFeatures == VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, "RGBA8 is a vertex format");
	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES, "linear image query");
	result = vulkan_read_u32(&reader);
	if (POWERVR_FORMAT_LINEAR_SAMPLED)
		check(result == VK_SUCCESS, "switch on: a linear sampled BGRA8 image query succeeds");
	else
		check(result == (uint32_t)VK_ERROR_FORMAT_NOT_SUPPORTED, "switch off: a linear sampled BGRA8 image query is FORMAT_NOT_SUPPORTED");
	check(vulkan_reply_pointer(&reader), "linear limits present");
	vulkan_decode_VkImageFormatProperties(&reader, &limits);
	expect_opcode(&reader, GPU_OP_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES, "optimal image query");
	result = vulkan_read_u32(&reader);
	check(result == VK_SUCCESS, "an optimal BGRA8 image query succeeds");
	check(vulkan_reply_pointer(&reader), "optimal limits present");
	memset(&limits, 0, sizeof(limits));
	vulkan_decode_VkImageFormatProperties(&reader, &limits);
	check(limits.maxExtent.width == 4096 && limits.maxArrayLayers == 256 && limits.sampleCounts == VK_SAMPLE_COUNT_1_BIT, "optimal limits");

	expect_created(&reader, GPU_OP_CREATE_IMAGE, ID_SHARED_IMAGE, "an image declaring dma-buf external memory is made");
	expect_created(&reader, GPU_OP_ALLOCATE_MEMORY, ID_EXPORT_MEMORY, "an export is allocated");
	expect_created(&reader, GPU_OP_ALLOCATE_MEMORY, ID_IMPORT_MEMORY, "an import of the session's blob is allocated");
	expect_opcode(&reader, GPU_OP_ALLOCATE_MEMORY, "unknown import");
	result = vulkan_read_u32(&reader);
	check(result != VK_SUCCESS, "an import of an unknown blob is refused");
	expect_result(&reader, GPU_OP_CREATE_IMAGE, (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT, "an image declaring a host allocation is FEATURE_NOT_PRESENT");
	expect_identity(&reader);

	expect_created(&reader, GPU_OP_CREATE_IMAGE, ID_LINEAR_IMAGE, "linear image");
	expect_opcode(&reader, GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT, "subresource layout");
	check(vulkan_reply_pointer(&reader), "layout present");
	memset(&layout, 0, sizeof(layout));
	vulkan_decode_VkSubresourceLayout(&reader, &layout);
	check(layout.offset == 0 && layout.rowPitch == 416 && layout.size == 4160 && layout.rowPitch % 32U == 0, "100 RGBA8 texels: rows of 416 bytes");

	expect_created(&reader, GPU_OP_ALLOCATE_MEMORY, ID_IMAGE_MEMORY, "image memory");
	expect_created(&reader, GPU_OP_CREATE_IMAGE, ID_IMAGE, "optimal image");
	expect_opcode(&reader, GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS, "image requirements");
	check(vulkan_reply_pointer(&reader), "image requirements present");
	vulkan_decode_VkMemoryRequirements(&reader, &requirements);
	check(requirements.size == 4096 && requirements.alignment == 4096 && requirements.memoryTypeBits == 3U, "30x20 twiddled is 32x32: one page on the page");
	expect_result(&reader, GPU_OP_BIND_IMAGE_MEMORY, VK_SUCCESS, "image bind");
	expect_created(&reader, GPU_OP_CREATE_IMAGE_VIEW, ID_VIEW, "image view");
	expect_created(&reader, GPU_OP_CREATE_SAMPLER, ID_SAMPLER, "sampler");
	check(reader.error == VK_SUCCESS, "image replies read");
	expect_trailer(reply);
}

/* Reports this side's counts. */
void
frame_counts(
	int *passed,
	int *failed)
{
	*passed = passes;
	*failed = failures;
}

/* Starts a recorded command of a command buffer. */
static void
record_for(
	struct vulkan_writer *w,
	uint32_t opcode,
	uint64_t command)
{
	vulkan_writer_init(w);
	w->bytes = 0;
	vulkan_write_u32(w, opcode);
	vulkan_write_u32(w, 0);
	vulkan_write_u64(w, command);
}

/* Appends a vkCreateShaderModule of SPIR-V words. */
static void
k_shader(
	struct vulkan_writer *frame,
	uint64_t identity,
	const uint32_t *words,
	size_t bytes)
{
	VkShaderModuleCreateInfo module;
	struct vulkan_writer w;

	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = bytes;
	module.pCode = words;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_SHADER_MODULE);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkShaderModuleCreateInfo(&w, &module);
	create_tail(&w, identity);
	append(frame, &w);
}

/* Appends a vkCreatePipelineLayout of the one set and a vertex push constant range of these bytes. */
static void
k_pipeline_layout(
	struct vulkan_writer *frame,
	uint64_t identity,
	uint32_t push_bytes)
{
	VkPipelineLayoutCreateInfo info;
	VkPushConstantRange range;
	VkDescriptorSetLayout set_layout;
	struct vulkan_writer w;

	set_layout = (VkDescriptorSetLayout)(uintptr_t)handle(ID_K_SET_LAYOUT);
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = push_bytes;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.setLayoutCount = 1;
	info.pSetLayouts = &set_layout;
	info.pushConstantRangeCount = 1;
	info.pPushConstantRanges = &range;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_PIPELINE_LAYOUT);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkPipelineLayoutCreateInfo(&w, &info);
	create_tail(&w, identity);
	append(frame, &w);
}

/*
 * Appends a vkCreateGraphicsPipelines of one pipeline as
 * userland/desktop/wayland/compose.c makes it (one vertex binding of the
 * attribute's format, triangle lists, dynamic viewport and scissor, the
 * premultiplied alpha blend), encoded as libvulkan/pipeline.c:
 * pipeline_encode_graphics writes it.
 */
static void
k_pipeline(
	struct vulkan_writer *frame,
	uint64_t identity,
	uint64_t vertex,
	uint64_t fragment,
	uint64_t layout,
	VkFormat attribute_format,
	uint32_t stride)
{
	static const VkDynamicState dynamic[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attribute;
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDynamicStateCreateInfo dynamic_state;
	struct vulkan_writer w;
	unsigned stage;

	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = (VkShaderModule)(uintptr_t)handle(vertex);
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = (VkShaderModule)(uintptr_t)handle(fragment);
	stages[1].pName = "main";
	memset(&binding, 0, sizeof(binding));
	binding.stride = stride;
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	memset(&attribute, 0, sizeof(attribute));
	attribute.format = attribute_format;
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 1;
	input.pVertexBindingDescriptions = &binding;
	input.vertexAttributeDescriptionCount = 1;
	input.pVertexAttributeDescriptions = &attribute;
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blend_attachment.blendEnable = VK_TRUE;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.logicOp = VK_LOGIC_OP_COPY;
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;
	memset(&dynamic_state, 0, sizeof(dynamic_state));
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.dynamicStateCount = 2;
	dynamic_state.pDynamicStates = dynamic;

	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_GRAPHICS_PIPELINES);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 2);
	vulkan_write_u64(&w, 2);
	for (stage = 0; stage < 2; stage++)
		vulkan_encode_VkPipelineShaderStageCreateInfo(&w, &stages[stage]);
	vulkan_write_pointer(&w, &input);
	vulkan_encode_VkPipelineVertexInputStateCreateInfo(&w, &input);
	vulkan_write_pointer(&w, &assembly);
	vulkan_encode_VkPipelineInputAssemblyStateCreateInfo(&w, &assembly);
	vulkan_write_u64(&w, 0);
	vulkan_write_pointer(&w, &viewport);
	vulkan_encode_VkPipelineViewportStateCreateInfo(&w, &viewport);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkPipelineRasterizationStateCreateInfo(&w, &raster);
	vulkan_write_pointer(&w, &multisample);
	vulkan_encode_VkPipelineMultisampleStateCreateInfo(&w, &multisample);
	vulkan_write_u64(&w, 0);
	vulkan_write_pointer(&w, &blend);
	vulkan_encode_VkPipelineColorBlendStateCreateInfo(&w, &blend);
	vulkan_write_pointer(&w, &dynamic_state);
	vulkan_encode_VkPipelineDynamicStateCreateInfo(&w, &dynamic_state);
	vulkan_encode_handle(&w, handle(layout));
	vulkan_encode_handle(&w, handle(ID_K_PASS));
	vulkan_write_u32(&w, 0);
	vulkan_encode_handle(&w, 0);
	vulkan_write_u32(&w, (uint32_t)-1);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, identity);
	append(frame, &w);
}

/* Appends vkAllocateCommandBuffers of one primary command buffer and its vkBeginCommandBuffer. */
static void
k_command_begin(
	struct vulkan_writer *frame,
	uint64_t command)
{
	VkCommandBufferAllocateInfo allocate;
	VkCommandBufferBeginInfo begin;
	struct vulkan_writer w;

	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = (VkCommandPool)(uintptr_t)handle(ID_COMMAND_POOL);
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ALLOCATE_COMMAND_BUFFERS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkCommandBufferAllocateInfo(&w, &allocate);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, command);
	append(frame, &w);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_BEGIN_COMMAND_BUFFER);
	vulkan_write_u64(&w, command);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkCommandBufferBeginInfo(&w, &begin);
	append(frame, &w);
}

/*
 * Records one render pass instance as compose.c records a frame: begin
 * with the clear, the pipeline, the set, the vertex buffer, the
 * viewport and scissor, then per quad its push constants and a draw of
 * six corners, then the end.
 */
static void
k_pass(
	struct vulkan_writer *frame,
	uint64_t command,
	uint64_t pipeline,
	uint64_t layout,
	uint32_t push_bytes,
	uint32_t draws)
{
	VkViewport viewport;
	VkRect2D scissor;
	VkRect2D area;
	struct vulkan_writer w;
	float push[8];
	uint32_t index;

	record_for(&w, GPU_OP_CMD_BEGIN_RENDER_PASS, command);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_encode_handle(&w, handle(ID_K_PASS));
	vulkan_encode_handle(&w, handle(ID_K_FRAMEBUFFER));
	memset(&area, 0, sizeof(area));
	area.extent.width = K_EDGE;
	area.extent.height = K_EDGE;
	vulkan_encode_VkRect2D(&w, &area);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 2);
	vulkan_write_u64(&w, 4);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0x3f800000U);
	vulkan_write_u32(&w, VK_SUBPASS_CONTENTS_INLINE);
	append(frame, &w);

	record_for(&w, GPU_OP_CMD_BIND_PIPELINE, command);
	vulkan_write_u32(&w, VK_PIPELINE_BIND_POINT_GRAPHICS);
	vulkan_encode_handle(&w, handle(pipeline));
	append(frame, &w);
	record_for(&w, GPU_OP_CMD_BIND_DESCRIPTOR_SETS, command);
	vulkan_write_u32(&w, VK_PIPELINE_BIND_POINT_GRAPHICS);
	vulkan_encode_handle(&w, handle(layout));
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_handle(&w, handle(ID_K_SET));
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(frame, &w);
	record_for(&w, GPU_OP_CMD_BIND_VERTEX_BUFFERS, command);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_handle(&w, handle(ID_K_VERTEX));
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, 0);
	append(frame, &w);
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)K_EDGE;
	viewport.height = (float)K_EDGE;
	viewport.maxDepth = 1.0f;
	record_for(&w, GPU_OP_CMD_SET_VIEWPORT, command);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkViewport(&w, &viewport);
	append(frame, &w);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = K_EDGE;
	scissor.extent.height = K_EDGE;
	record_for(&w, GPU_OP_CMD_SET_SCISSOR, command);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkRect2D(&w, &scissor);
	append(frame, &w);

	for (index = 0; index < draws; index++) {
		memset(push, 0, sizeof(push));
		push[0] = -1.0f + (float)index / (float)draws;
		push[2] = push[0] + 0.5f;
		push[3] = 1.0f;
		record_for(&w, GPU_OP_CMD_PUSH_CONSTANTS, command);
		vulkan_encode_handle(&w, handle(layout));
		vulkan_write_u32(&w, VK_SHADER_STAGE_VERTEX_BIT);
		vulkan_write_u32(&w, 0);
		vulkan_write_u32(&w, push_bytes);
		vulkan_write_u64(&w, push_bytes);
		vulkan_write_bytes(&w, push, push_bytes);
		append(frame, &w);
		record_for(&w, GPU_OP_CMD_DRAW, command);
		vulkan_write_u32(&w, 6);
		vulkan_write_u32(&w, 1);
		vulkan_write_u32(&w, 0);
		vulkan_write_u32(&w, 0);
		append(frame, &w);
	}

	record_for(&w, GPU_OP_CMD_END_RENDER_PASS, command);
	append(frame, &w);
}

/* Appends vkEndCommandBuffer, a fence and vkQueueSubmit of the command buffer with it, then its status. */
static void
k_submit(
	struct vulkan_writer *frame,
	uint64_t command,
	uint64_t fence)
{
	struct vulkan_writer w;

	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_END_COMMAND_BUFFER);
	vulkan_write_u64(&w, command);
	append(frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_FENCE);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	create_tail(&w, fence);
	append(frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_QUEUE_SUBMIT);
	vulkan_write_u64(&w, ID_QUEUE);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_SUBMIT_INFO);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, command);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, fence);
	append(frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_GET_FENCE_STATUS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, fence);
	append(frame, &w);
}

/*
 * The Keiland-like frame (ws269-p011): the compositor's quad pipeline
 * and libkeiland's canvas pipeline over one set of the image frame's
 * texture and sampler, a 64x64 BGRA8 target with its pass and
 * framebuffer, then command buffer A (a copy of the vertex buffer into
 * the texture, render passes of 20 and 17 draws, the clear before the
 * present) and command buffer B (a render pass of 200 draws), each
 * submitted with its fence.
 */
size_t
frame_keiland(
	uint32_t reply_blob,
	uint8_t **bytes)
{
	VkBufferCreateInfo buffer;
	VkImageViewCreateInfo view;
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass;
	VkFramebufferCreateInfo framebuffer;
	VkImageView view_handle;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkDescriptorSetLayout set_layout_handle;
	VkDescriptorPoolSize pool_size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkBufferImageCopy copy;
	VkImageSubresourceRange range;
	struct vulkan_writer frame;
	struct vulkan_writer w;

	frame_begin(&frame, reply_blob);

	/* The memory, the target image and the vertex buffer bound in it. */
	allocate_memory(&frame, ID_K_MEMORY, 65536, 0, 0);
	create_image(&frame, ID_K_TARGET, VK_FORMAT_B8G8R8A8_UNORM, K_EDGE, K_EDGE, VK_IMAGE_TILING_OPTIMAL,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_BIND_IMAGE_MEMORY);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_K_TARGET));
	vulkan_encode_handle(&w, handle(ID_K_MEMORY));
	vulkan_write_u64(&w, 0);
	append(&frame, &w);
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = 4096;
	buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_BUFFER);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkBufferCreateInfo(&w, &buffer);
	create_tail(&w, ID_K_VERTEX);
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_BIND_BUFFER_MEMORY);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_encode_handle(&w, handle(ID_K_VERTEX));
	vulkan_encode_handle(&w, handle(ID_K_MEMORY));
	vulkan_write_u64(&w, 32768);
	append(&frame, &w);

	/* The target's view, the pass of one cleared and stored colour, and the framebuffer. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = (VkImage)(uintptr_t)handle(ID_K_TARGET);
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkImageViewCreateInfo(&w, &view);
	create_tail(&w, ID_K_TARGET_VIEW);
	append(&frame, &w);
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	memset(&reference, 0, sizeof(reference));
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &reference;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass.attachmentCount = 1;
	pass.pAttachments = &attachment;
	pass.subpassCount = 1;
	pass.pSubpasses = &subpass;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_RENDER_PASS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkRenderPassCreateInfo(&w, &pass);
	create_tail(&w, ID_K_PASS);
	append(&frame, &w);
	view_handle = (VkImageView)(uintptr_t)handle(ID_K_TARGET_VIEW);
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = (VkRenderPass)(uintptr_t)handle(ID_K_PASS);
	framebuffer.attachmentCount = 1;
	framebuffer.pAttachments = &view_handle;
	framebuffer.width = K_EDGE;
	framebuffer.height = K_EDGE;
	framebuffer.layers = 1;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_FRAMEBUFFER);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkFramebufferCreateInfo(&w, &framebuffer);
	create_tail(&w, ID_K_FRAMEBUFFER);
	append(&frame, &w);

	/* The four shader modules: the compositor's quad and libkeiland's canvas. */
	k_shader(&frame, ID_K_QUAD_VERT, kwl_quad_vert, sizeof(kwl_quad_vert));
	k_shader(&frame, ID_K_QUAD_FRAG, kwl_quad_frag, sizeof(kwl_quad_frag));
	k_shader(&frame, ID_K_CANVAS_VERT, keiui_canvas_vert, sizeof(keiui_canvas_vert));
	k_shader(&frame, ID_K_CANVAS_FRAG, keiui_canvas_frag, sizeof(keiui_canvas_frag));

	/* The set layout of one combined image sampler for the fragment stage, and the two pipeline layouts. */
	memset(&binding, 0, sizeof(binding));
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset(&set_layout, 0, sizeof(set_layout));
	set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout.bindingCount = 1;
	set_layout.pBindings = &binding;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_pointer(&w, &set_layout);
	vulkan_encode_VkDescriptorSetLayoutCreateInfo(&w, &set_layout);
	create_tail(&w, ID_K_SET_LAYOUT);
	append(&frame, &w);
	k_pipeline_layout(&frame, ID_K_QUAD_LAYOUT, 32);
	k_pipeline_layout(&frame, ID_K_CANVAS_LAYOUT, 16);

	/* The two pipelines: a vec2 corner a vertex, and a vec4 corner a vertex. */
	k_pipeline(&frame, ID_K_QUAD_PIPELINE, ID_K_QUAD_VERT, ID_K_QUAD_FRAG, ID_K_QUAD_LAYOUT, VK_FORMAT_R32G32_SFLOAT, 8);
	k_pipeline(&frame, ID_K_CANVAS_PIPELINE, ID_K_CANVAS_VERT, ID_K_CANVAS_FRAG, ID_K_CANVAS_LAYOUT, VK_FORMAT_R32G32B32A32_SFLOAT, 16);

	/* The pool, the set and its sampler and texture (the image frame's view and sampler). */
	memset(&pool_size, 0, sizeof(pool_size));
	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_size.descriptorCount = 1;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &pool_size;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_CREATE_DESCRIPTOR_POOL);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_pointer(&w, &pool);
	vulkan_encode_VkDescriptorPoolCreateInfo(&w, &pool);
	create_tail(&w, ID_K_POOL);
	append(&frame, &w);
	set_layout_handle = (VkDescriptorSetLayout)(uintptr_t)handle(ID_K_SET_LAYOUT);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = (VkDescriptorPool)(uintptr_t)handle(ID_K_POOL);
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &set_layout_handle;
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_ALLOCATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_pointer(&w, &allocate);
	vulkan_encode_VkDescriptorSetAllocateInfo(&w, &allocate);
	vulkan_write_u64(&w, 1);
	vulkan_encode_handle(&w, handle(ID_K_SET));
	append(&frame, &w);
	vulkan_writer_init(&w);
	vulkan_command_begin(&w, GPU_OP_UPDATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&w, ID_DEVICE);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, ID_K_SET);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u32(&w, 1);
	vulkan_write_u32(&w, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	vulkan_write_u64(&w, 1);
	vulkan_write_u64(&w, ID_SAMPLER);
	vulkan_write_u64(&w, ID_VIEW);
	vulkan_write_u32(&w, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vulkan_write_u64(&w, 0);
	vulkan_write_u64(&w, 0);
	vulkan_write_u32(&w, 0);
	vulkan_write_u64(&w, 0);
	append(&frame, &w);

	/* Command buffer A: the copy into the texture, the passes of 20 and 17 draws, the clear before the present. */
	k_command_begin(&frame, ID_K_COMMAND_A);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = 30;
	copy.imageExtent.height = 20;
	copy.imageExtent.depth = 1;
	record_for(&w, GPU_OP_CMD_COPY_BUFFER_TO_IMAGE, ID_K_COMMAND_A);
	vulkan_encode_handle(&w, handle(ID_K_VERTEX));
	vulkan_encode_handle(&w, handle(ID_IMAGE));
	vulkan_write_u32(&w, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkBufferImageCopy(&w, &copy);
	append(&frame, &w);
	k_pass(&frame, ID_K_COMMAND_A, ID_K_QUAD_PIPELINE, ID_K_QUAD_LAYOUT, 32, 20);
	k_pass(&frame, ID_K_COMMAND_A, ID_K_CANVAS_PIPELINE, ID_K_CANVAS_LAYOUT, 16, 17);
	memset(&range, 0, sizeof(range));
	range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	range.levelCount = 1;
	range.layerCount = 1;
	record_for(&w, GPU_OP_CMD_CLEAR_COLOR_IMAGE, ID_K_COMMAND_A);
	vulkan_encode_handle(&w, handle(ID_K_TARGET));
	vulkan_write_u32(&w, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vulkan_write_u64(&w, 1);
	vulkan_write_u32(&w, 2);
	vulkan_write_u64(&w, 4);
	vulkan_write_u32(&w, 0x3e800000U);
	vulkan_write_u32(&w, 0x3f000000U);
	vulkan_write_u32(&w, 0x3f400000U);
	vulkan_write_u32(&w, 0x3f800000U);
	vulkan_write_u32(&w, 1);
	vulkan_write_u64(&w, 1);
	vulkan_encode_VkImageSubresourceRange(&w, &range);
	append(&frame, &w);
	k_submit(&frame, ID_K_COMMAND_A, ID_K_FENCE_A);

	/* Command buffer B: one pass of 200 draws, more jobs than a command buffer has room for. */
	k_command_begin(&frame, ID_K_COMMAND_B);
	k_pass(&frame, ID_K_COMMAND_B, ID_K_QUAD_PIPELINE, ID_K_QUAD_LAYOUT, 32, 200);
	k_submit(&frame, ID_K_COMMAND_B, ID_K_FENCE_B);

	return frame_end(&frame, bytes);
}

/* Checks the replies of the Keiland-like frame up to and with the two submissions' results; *submit_b gets B's. */
void
replies_keiland(
	uint8_t *reply,
	uint32_t *submit_b)
{
	struct vulkan_reader reader;
	uint32_t result;

	vulkan_reader_init(&reader, NULL, 0);
	reader.data = reply;
	reader.bytes = FRAME_REPLY_BYTES - REPLY_TRAILER_BYTES;

	expect_created(&reader, GPU_OP_ALLOCATE_MEMORY, ID_K_MEMORY, "keiland memory");
	expect_created(&reader, GPU_OP_CREATE_IMAGE, ID_K_TARGET, "keiland target");
	expect_result(&reader, GPU_OP_BIND_IMAGE_MEMORY, VK_SUCCESS, "keiland target bind");
	expect_created(&reader, GPU_OP_CREATE_BUFFER, ID_K_VERTEX, "keiland vertex buffer");
	expect_result(&reader, GPU_OP_BIND_BUFFER_MEMORY, VK_SUCCESS, "keiland vertex bind");
	expect_created(&reader, GPU_OP_CREATE_IMAGE_VIEW, ID_K_TARGET_VIEW, "keiland target view");
	expect_created(&reader, GPU_OP_CREATE_RENDER_PASS, ID_K_PASS, "keiland render pass");
	expect_created(&reader, GPU_OP_CREATE_FRAMEBUFFER, ID_K_FRAMEBUFFER, "keiland framebuffer");
	expect_created(&reader, GPU_OP_CREATE_SHADER_MODULE, ID_K_QUAD_VERT, "kwl quad.vert module");
	expect_created(&reader, GPU_OP_CREATE_SHADER_MODULE, ID_K_QUAD_FRAG, "kwl quad.frag module");
	expect_created(&reader, GPU_OP_CREATE_SHADER_MODULE, ID_K_CANVAS_VERT, "canvas.vert module");
	expect_created(&reader, GPU_OP_CREATE_SHADER_MODULE, ID_K_CANVAS_FRAG, "canvas.frag module");
	expect_created(&reader, GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT, ID_K_SET_LAYOUT, "sampler set layout");
	expect_created(&reader, GPU_OP_CREATE_PIPELINE_LAYOUT, ID_K_QUAD_LAYOUT, "quad pipeline layout");
	expect_created(&reader, GPU_OP_CREATE_PIPELINE_LAYOUT, ID_K_CANVAS_LAYOUT, "canvas pipeline layout");
	expect_opcode(&reader, GPU_OP_CREATE_GRAPHICS_PIPELINES, "quad pipeline");
	result = vulkan_read_u32(&reader);
	check(result == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_K_QUAD_PIPELINE, "the compositor's quad pipeline is created");
	expect_opcode(&reader, GPU_OP_CREATE_GRAPHICS_PIPELINES, "canvas pipeline");
	result = vulkan_read_u32(&reader);
	check(result == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_K_CANVAS_PIPELINE, "libkeiland's canvas pipeline is created");
	expect_created(&reader, GPU_OP_CREATE_DESCRIPTOR_POOL, ID_K_POOL, "sampler pool");
	expect_opcode(&reader, GPU_OP_ALLOCATE_DESCRIPTOR_SETS, "sampler set");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_K_SET, "sampler set allocated");
	expect_opcode(&reader, GPU_OP_UPDATE_DESCRIPTOR_SETS, "sampler update");
	expect_opcode(&reader, GPU_OP_ALLOCATE_COMMAND_BUFFERS, "command buffer A");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_K_COMMAND_A, "command buffer A allocated");
	expect_result(&reader, GPU_OP_BEGIN_COMMAND_BUFFER, VK_SUCCESS, "begin A");
	expect_result(&reader, GPU_OP_END_COMMAND_BUFFER, VK_SUCCESS, "end A");
	expect_created(&reader, GPU_OP_CREATE_FENCE, ID_K_FENCE_A, "fence A");
	expect_result(&reader, GPU_OP_QUEUE_SUBMIT, VK_SUCCESS, "submit A (copy, 20 and 17 draws, clear)");
	expect_result(&reader, GPU_OP_GET_FENCE_STATUS, VK_SUCCESS, "fence A signaled");
	expect_opcode(&reader, GPU_OP_ALLOCATE_COMMAND_BUFFERS, "command buffer B");
	check(vulkan_read_u32(&reader) == VK_SUCCESS && vulkan_read_u64(&reader) == 1 && vulkan_read_u64(&reader) == ID_K_COMMAND_B, "command buffer B allocated");
	expect_result(&reader, GPU_OP_BEGIN_COMMAND_BUFFER, VK_SUCCESS, "begin B");
	expect_result(&reader, GPU_OP_END_COMMAND_BUFFER, VK_SUCCESS, "end B");
	expect_created(&reader, GPU_OP_CREATE_FENCE, ID_K_FENCE_B, "fence B");
	expect_opcode(&reader, GPU_OP_QUEUE_SUBMIT, "submit B");
	*submit_b = vulkan_read_u32(&reader);
	check(reader.error == VK_SUCCESS, "keiland replies read");
}
