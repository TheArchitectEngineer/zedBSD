/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkcompile: what the amdrdna shader compiler's programs do on the GPU,
 * checked pixel by pixel through the public Vulkan 1.0 API with small
 * pixel programs (plan/ws039/tests/vkcompile/): the differences across the
 * pixel's group of four, gathers, the primitive number, the point sprite
 * coordinate, integer division and modulus, discards (and waves that end early), the
 * image size, a chain of texel fetches, and a dual-source blend.
 *
 * Each case draws into a target cleared to an opaque colour, copies the
 * target into a buffer and compares every pixel with what the CPU computes
 * (within 1 per channel).  One line a case:
 *
 *   VKCOMPILE <case> <w>x<h> wrong=<n> [first ...] ok|FAIL
 *   VKCOMPILE <case> refused <what> <status>      (the driver refused the pipeline: the feature is not there)
 *   VKCOMPILE ok cases=<n> refused=<n>   or   VKCOMPILE fail cases=<n> failed=<n> refused=<n>
 *
 * usage: vkcompile [--device-index=N] [--case=NAME]
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "vkcompile-shaders.h"

#define VC_WAIT_NS		UINT64_C(10000000000)
#define VC_TEXTURE_SIDE		16U
#define VC_SIDE_MAX		64U
#define VC_VERTICES_MAX		256U

/* The background every case clears to (R 0.2, G 0.4, B 0.6, A 1.0) as B, G, R, A bytes. */
static const uint8_t vc_background[4] = { 0x99U, 0x66U, 0x33U, 0xffU };

/* The geometry a case draws: the whole target, 32 bands of two triangles, or 64 points 8 pixels apart. */
enum vc_geometry { VC_GEOMETRY_FULL, VC_GEOMETRY_BANDS, VC_GEOMETRY_POINTS };

struct vc;

/* Writes the B, G, R, A bytes a pixel should have; returns 0 when the pixel is not checked. */
typedef int (*vc_expect_fn)(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);

struct vc_case {
	const char *name;
	const uint32_t *code;
	size_t code_bytes;
	enum vc_geometry geometry;
	uint32_t side;
	bool dual;
	uint32_t seed;
	vc_expect_fn expect;
};

struct vc_store {
	VkImage image;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkImageView view;
	uint8_t *mapping;
};

struct vc {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t memory_type;
	bool dual_source;
	VkSampler sampler;
	VkShaderModule vertex_module;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout pipeline_layout;
	VkRenderPass pass;
	VkCommandPool command_pool;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet set;
	struct vc_store texture;
	uint8_t texels[VC_TEXTURE_SIDE * VC_TEXTURE_SIDE * 4U];
	char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

static int vc_expect_derivative(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_gather(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_gather_offset(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_primitive(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_point(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_divide(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_divide_signed(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_discard(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_size(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_chain(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);
static int vc_expect_dual(const struct vc *vc, uint32_t seed, uint32_t x, uint32_t y, uint8_t *bgra);

#define VC_CODE(name)	name, sizeof(name)

static const struct vc_case vc_cases[] = {
	{ "derivative", VC_CODE(vc_derivative_frag), VC_GEOMETRY_FULL, 64U, false, 0U, vc_expect_derivative },
	{ "gather", VC_CODE(vc_gather_frag), VC_GEOMETRY_FULL, 16U, false, 0U, vc_expect_gather },
	{ "gather-offset", VC_CODE(vc_gather_offset_frag), VC_GEOMETRY_FULL, 16U, false, 0U, vc_expect_gather_offset },
	{ "primitive", VC_CODE(vc_primitive_frag), VC_GEOMETRY_BANDS, 64U, false, 0U, vc_expect_primitive },
	{ "point", VC_CODE(vc_point_frag), VC_GEOMETRY_POINTS, 64U, false, 0U, vc_expect_point },
	{ "divide-0", VC_CODE(vc_divide_frag), VC_GEOMETRY_FULL, 64U, false, 0U, vc_expect_divide },
	{ "divide-1", VC_CODE(vc_divide_frag), VC_GEOMETRY_FULL, 64U, false, 1U, vc_expect_divide },
	{ "divide-2", VC_CODE(vc_divide_frag), VC_GEOMETRY_FULL, 64U, false, 2U, vc_expect_divide },
	{ "divide-signed-0", VC_CODE(vc_divide_signed_frag), VC_GEOMETRY_FULL, 64U, false, 0U, vc_expect_divide_signed },
	{ "divide-signed-1", VC_CODE(vc_divide_signed_frag), VC_GEOMETRY_FULL, 64U, false, 1U, vc_expect_divide_signed },
	{ "discard", VC_CODE(vc_discard_frag), VC_GEOMETRY_FULL, 64U, false, 0U, vc_expect_discard },
	{ "size", VC_CODE(vc_size_frag), VC_GEOMETRY_FULL, 16U, false, 0U, vc_expect_size },
	{ "chain", VC_CODE(vc_chain_frag), VC_GEOMETRY_FULL, 64U, false, 0U, vc_expect_chain },
	{ "dual-source", VC_CODE(vc_dual_frag), VC_GEOMETRY_FULL, 64U, true, 0U, vc_expect_dual },
};

static VkResult vc_device(struct vc *vc, int index);
static VkResult vc_layouts(struct vc *vc);
static VkResult vc_buffer(struct vc *vc, VkDeviceSize bytes, VkBufferUsageFlags usage, struct vc_store *store);
static VkResult vc_image(struct vc *vc, uint32_t side, VkImageUsageFlags usage, struct vc_store *store);
static VkResult vc_texture(struct vc *vc);
static VkResult vc_submit(struct vc *vc, VkCommandBuffer command);
static VkResult vc_pipeline(struct vc *vc, const struct vc_case *test, VkShaderModule *module, VkPipeline *pipeline);
static uint32_t vc_geometry(enum vc_geometry geometry, float *corners);
static int vc_run(struct vc *vc, const struct vc_case *test, unsigned *refused);
static uint32_t vc_hash(uint32_t v);
static uint8_t vc_unorm(float value);
static void vc_store_free(struct vc *vc, struct vc_store *store);
static void vc_free(struct vc *vc);

int
main(
	int argc,
	char **argv)
{
	struct vc vc;
	const char *only;
	unsigned count;
	unsigned failed;
	unsigned refused;
	unsigned test;
	int index;
	int argument;

	index = 0;
	only = NULL;
	memset(&vc, 0, sizeof(vc));
	for (argument = 1; argument < argc; argument++) {
		if (strncmp(argv[argument], "--device-index=", 15) == 0) {
			index = atoi(argv[argument] + 15);
		} else if (strncmp(argv[argument], "--case=", 7) == 0) {
			only = argv[argument] + 7;
		} else {
			fprintf(stderr, "usage: vkcompile [--device-index=N] [--case=NAME]\n");
			return 2;
		}
	}

	/* The device, the shared objects and the sampled image. */
	if (vc_device(&vc, index) != VK_SUCCESS || vc_layouts(&vc) != VK_SUCCESS || vc_texture(&vc) != VK_SUCCESS) {
		vc_free(&vc);
		return 1;
	}
	printf("VKCOMPILE device=%s dual-source=%d\n", vc.name, vc.dual_source ? 1 : 0);

	/* Each case. */
	count = 0U;
	failed = 0U;
	refused = 0U;
	for (test = 0U; test < sizeof(vc_cases) / sizeof(vc_cases[0]); test++) {
		if (only != NULL && strcmp(only, vc_cases[test].name) != 0)
			continue;
		count++;
		if (vc_run(&vc, &vc_cases[test], &refused) != 0)
			failed++;
	}
	vc_free(&vc);

	if (failed != 0U) {
		printf("VKCOMPILE fail cases=%u failed=%u refused=%u\n", count, failed, refused);
		return 1;
	}
	printf("VKCOMPILE ok cases=%u refused=%u\n", count, refused);
	return 0;
}

/* Creates the instance and the device (with dual-source blending when it is offered); finds a host-visible coherent memory type. */
static VkResult
vc_device(
	struct vc *vc,
	int index)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkPhysicalDevice physicals[8];
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures offered;
	VkPhysicalDeviceFeatures wanted;
	VkPhysicalDeviceMemoryProperties memory;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	VkSamplerCreateInfo sampler;
	VkCommandPoolCreateInfo pool;
	VkFlags host;
	float priority;
	uint32_t count;
	uint32_t type;
	VkResult status;

	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkcompile";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	status = vkCreateInstance(&instance, NULL, &vc->instance);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail instance %d\n", (int)status);
		return status;
	}
	count = 8U;
	status = vkEnumeratePhysicalDevices(vc->instance, &count, physicals);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || count == 0U || index < 0 || (uint32_t)index >= count) {
		printf("VKCOMPILE fail devices %d count=%u index=%d\n", (int)status, count, index);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	vc->physical = physicals[index];
	vkGetPhysicalDeviceProperties(vc->physical, &properties);
	snprintf(vc->name, sizeof(vc->name), "%s", properties.deviceName);
	vkGetPhysicalDeviceFeatures(vc->physical, &offered);

	host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	vkGetPhysicalDeviceMemoryProperties(vc->physical, &memory);
	for (type = 0U; type < memory.memoryTypeCount; type++) {
		if ((memory.memoryTypes[type].propertyFlags & host) == host)
			break;
	}
	if (type == memory.memoryTypeCount) {
		printf("VKCOMPILE fail memory-type none of %u\n", memory.memoryTypeCount);
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
	vc->memory_type = type;

	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	memset(&wanted, 0, sizeof(wanted));
	wanted.dualSrcBlend = offered.dualSrcBlend;
	vc->dual_source = offered.dualSrcBlend != VK_FALSE;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1U;
	device.pQueueCreateInfos = &queue;
	device.pEnabledFeatures = &wanted;
	status = vkCreateDevice(vc->physical, &device, NULL, &vc->device);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail device %d\n", (int)status);
		return status;
	}
	vkGetDeviceQueue(vc->device, 0U, 0U, &vc->queue);

	/* A nearest sampler that clamps, which the gathers and the fetches read through. */
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	status = vkCreateSampler(vc->device, &sampler, NULL, &vc->sampler);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail sampler %d\n", (int)status);
		return status;
	}

	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	status = vkCreateCommandPool(vc->device, &pool, NULL, &vc->command_pool);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail command-pool %d\n", (int)status);
		return status;
	}
	return VK_SUCCESS;
}

/* Creates the vertex module, the set layout (one sampled image), the pipeline layout (32 bytes of push constants) and the pass. */
static VkResult
vc_layouts(
	struct vc *vc)
{
	VkShaderModuleCreateInfo module;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo pipeline_layout;
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass;
	VkResult status;

	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = sizeof(vc_quad_vert);
	module.pCode = vc_quad_vert;
	status = vkCreateShaderModule(vc->device, &module, NULL, &vc->vertex_module);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail vertex-module %d\n", (int)status);
		return status;
	}

	memset(&binding, 0, sizeof(binding));
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1U;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset(&set_layout, 0, sizeof(set_layout));
	set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout.bindingCount = 1U;
	set_layout.pBindings = &binding;
	status = vkCreateDescriptorSetLayout(vc->device, &set_layout, NULL, &vc->set_layout);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail set-layout %d\n", (int)status);
		return status;
	}
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	range.size = 32U;
	memset(&pipeline_layout, 0, sizeof(pipeline_layout));
	pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout.setLayoutCount = 1U;
	pipeline_layout.pSetLayouts = &vc->set_layout;
	pipeline_layout.pushConstantRangeCount = 1U;
	pipeline_layout.pPushConstantRanges = &range;
	status = vkCreatePipelineLayout(vc->device, &pipeline_layout, NULL, &vc->pipeline_layout);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail pipeline-layout %d\n", (int)status);
		return status;
	}

	/* One B8G8R8A8 attachment, cleared, kept, left in the general layout. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	memset(&reference, 0, sizeof(reference));
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1U;
	subpass.pColorAttachments = &reference;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass.attachmentCount = 1U;
	pass.pAttachments = &attachment;
	pass.subpassCount = 1U;
	pass.pSubpasses = &subpass;
	status = vkCreateRenderPass(vc->device, &pass, NULL, &vc->pass);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail render-pass %d\n", (int)status);
		return status;
	}
	return VK_SUCCESS;
}

/* Creates a buffer with its own memory, mapped. */
static VkResult
vc_buffer(
	struct vc *vc,
	VkDeviceSize bytes,
	VkBufferUsageFlags usage,
	struct vc_store *store)
{
	VkBufferCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	void *mapping;
	VkResult status;

	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = bytes;
	create.usage = usage;
	status = vkCreateBuffer(vc->device, &create, NULL, &store->buffer);
	if (status != VK_SUCCESS)
		return status;
	vkGetBufferMemoryRequirements(vc->device, store->buffer, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = vc->memory_type;
	status = vkAllocateMemory(vc->device, &allocate, NULL, &store->memory);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(vc->device, store->buffer, store->memory, 0U);
	if (status == VK_SUCCESS)
		status = vkMapMemory(vc->device, store->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status == VK_SUCCESS)
		store->mapping = mapping;
	return status;
}

/* Creates a square optimal B8G8R8A8 image with its own memory and its view. */
static VkResult
vc_image(
	struct vc *vc,
	uint32_t side,
	VkImageUsageFlags usage,
	struct vc_store *store)
{
	VkImageCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view;
	VkResult status;

	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = VK_FORMAT_B8G8R8A8_UNORM;
	create.extent.width = side;
	create.extent.height = side;
	create.extent.depth = 1U;
	create.mipLevels = 1U;
	create.arrayLayers = 1U;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = usage;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	status = vkCreateImage(vc->device, &create, NULL, &store->image);
	if (status != VK_SUCCESS)
		return status;
	vkGetImageMemoryRequirements(vc->device, store->image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = vc->memory_type;
	status = vkAllocateMemory(vc->device, &allocate, NULL, &store->memory);
	if (status == VK_SUCCESS)
		status = vkBindImageMemory(vc->device, store->image, store->memory, 0U);
	if (status != VK_SUCCESS)
		return status;
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = store->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	return vkCreateImageView(vc->device, &view, NULL, &store->view);
}

/* Makes the 16x16 sampled image of hashed texels, uploads it, and writes the set that names it. */
static VkResult
vc_texture(
	struct vc *vc)
{
	struct vc_store staging;
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorImageInfo image;
	VkWriteDescriptorSet write;
	VkCommandBufferAllocateInfo command_info;
	VkCommandBufferBeginInfo begin;
	VkCommandBuffer command;
	VkBufferImageCopy region;
	VkMemoryBarrier barrier;
	uint32_t index;
	VkResult status;

	/* The texels: B, G, R, A bytes of a hash. */
	for (index = 0U; index < VC_TEXTURE_SIDE * VC_TEXTURE_SIDE; index++) {
		uint32_t word = vc_hash(index * 2654435761U + 17U);

		memcpy(&vc->texels[index * 4U], &word, 4U);
	}

	memset(&staging, 0, sizeof(staging));
	status = vc_image(vc, VC_TEXTURE_SIDE, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &vc->texture);
	if (status == VK_SUCCESS)
		status = vc_buffer(vc, sizeof(vc->texels), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail texture %d\n", (int)status);
		vc_store_free(vc, &staging);
		return status;
	}
	memcpy(staging.mapping, vc->texels, sizeof(vc->texels));

	/* The upload. */
	memset(&command_info, 0, sizeof(command_info));
	command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_info.commandPool = vc->command_pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 1U;
	status = vkAllocateCommandBuffers(vc->device, &command_info, &command);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (status == VK_SUCCESS)
		status = vkBeginCommandBuffer(command, &begin);
	if (status == VK_SUCCESS) {
		memset(&region, 0, sizeof(region));
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1U;
		region.imageExtent.width = VC_TEXTURE_SIDE;
		region.imageExtent.height = VC_TEXTURE_SIDE;
		region.imageExtent.depth = 1U;
		vkCmdCopyBufferToImage(command, staging.buffer, vc->texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &region);
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 1U, &barrier, 0U, NULL, 0U,
				     NULL);
		status = vkEndCommandBuffer(command);
	}
	if (status == VK_SUCCESS)
		status = vc_submit(vc, command);
	vc_store_free(vc, &staging);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail upload %d\n", (int)status);
		return status;
	}

	/* The set. */
	memset(&size, 0, sizeof(size));
	size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	size.descriptorCount = 1U;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1U;
	pool.poolSizeCount = 1U;
	pool.pPoolSizes = &size;
	status = vkCreateDescriptorPool(vc->device, &pool, NULL, &vc->descriptor_pool);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = vc->descriptor_pool;
	allocate.descriptorSetCount = 1U;
	allocate.pSetLayouts = &vc->set_layout;
	if (status == VK_SUCCESS)
		status = vkAllocateDescriptorSets(vc->device, &allocate, &vc->set);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE fail set %d\n", (int)status);
		return status;
	}
	memset(&image, 0, sizeof(image));
	image.sampler = vc->sampler;
	image.imageView = vc->texture.view;
	image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vc->set;
	write.descriptorCount = 1U;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	vkUpdateDescriptorSets(vc->device, 1U, &write, 0U, NULL);
	return VK_SUCCESS;
}

/* Submits one command buffer and waits for it; the buffer goes back to the pool. */
static VkResult
vc_submit(
	struct vc *vc,
	VkCommandBuffer command)
{
	VkFenceCreateInfo fence_info;
	VkSubmitInfo submit;
	VkFence fence;
	VkResult status;

	memset(&fence_info, 0, sizeof(fence_info));
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	status = vkCreateFence(vc->device, &fence_info, NULL, &fence);
	if (status != VK_SUCCESS)
		return status;
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &command;
	status = vkQueueSubmit(vc->queue, 1U, &submit, fence);
	if (status == VK_SUCCESS)
		status = vkWaitForFences(vc->device, 1U, &fence, VK_TRUE, VC_WAIT_NS);
	vkDestroyFence(vc->device, fence, NULL);
	vkFreeCommandBuffers(vc->device, vc->command_pool, 1U, &command);
	return status;
}

/* Creates the case's pixel module and pipeline (points or triangles; a dual-source blend when asked). */
static VkResult
vc_pipeline(
	struct vc *vc,
	const struct vc_case *test,
	VkShaderModule *module,
	VkPipeline *pipeline)
{
	VkShaderModuleCreateInfo module_info;
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription vertex_binding;
	VkVertexInputAttributeDescription vertex_attribute;
	VkPipelineVertexInputStateCreateInfo vertex_input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo colour_blend;
	VkDynamicState states[2];
	VkPipelineDynamicStateCreateInfo dynamic;
	VkGraphicsPipelineCreateInfo create;
	VkResult status;

	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = test->code_bytes;
	module_info.pCode = test->code;
	status = vkCreateShaderModule(vc->device, &module_info, NULL, module);
	if (status != VK_SUCCESS)
		return status;

	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vc->vertex_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = *module;
	stages[1].pName = "main";
	memset(&vertex_binding, 0, sizeof(vertex_binding));
	vertex_binding.stride = 8U;
	memset(&vertex_attribute, 0, sizeof(vertex_attribute));
	vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
	memset(&vertex_input, 0, sizeof(vertex_input));
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = 1U;
	vertex_input.pVertexBindingDescriptions = &vertex_binding;
	vertex_input.vertexAttributeDescriptionCount = 1U;
	vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	if (test->geometry == VC_GEOMETRY_POINTS)
		assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1U;
	viewport.scissorCount = 1U;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.colorWriteMask = 0xfU;
	if (test->dual) {
		/* colour + target * factor, alpha: colour alpha + target alpha * factor alpha. */
		blend_attachment.blendEnable = VK_TRUE;
		blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
		blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA;
		blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	memset(&colour_blend, 0, sizeof(colour_blend));
	colour_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colour_blend.attachmentCount = 1U;
	colour_blend.pAttachments = &blend_attachment;
	states[0] = VK_DYNAMIC_STATE_VIEWPORT;
	states[1] = VK_DYNAMIC_STATE_SCISSOR;
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2U;
	dynamic.pDynamicStates = states;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create.stageCount = 2U;
	create.pStages = stages;
	create.pVertexInputState = &vertex_input;
	create.pInputAssemblyState = &assembly;
	create.pViewportState = &viewport;
	create.pRasterizationState = &raster;
	create.pMultisampleState = &multisample;
	create.pColorBlendState = &colour_blend;
	create.pDynamicState = &dynamic;
	create.layout = vc->pipeline_layout;
	create.renderPass = vc->pass;
	create.basePipelineIndex = -1;
	return vkCreateGraphicsPipelines(vc->device, VK_NULL_HANDLE, 1U, &create, NULL, pipeline);
}

/* Writes the corners ([0, 1] of the target) of a geometry; returns how many vertices. */
static uint32_t
vc_geometry(
	enum vc_geometry geometry,
	float *corners)
{
	static const float full[12] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	uint32_t band;
	uint32_t point;
	float top;
	float bottom;

	/* The whole target in two triangles. */
	if (geometry == VC_GEOMETRY_FULL) {
		memcpy(corners, full, sizeof(full));
		return 6U;
	}

	/* Points at the centres of the 8x8 cells of 8 pixels. */
	if (geometry == VC_GEOMETRY_POINTS) {
		for (point = 0U; point < 64U; point++) {
			corners[2U * point] = (float)((point & 7U) * 8U + 4U) / 64.0f;
			corners[2U * point + 1U] = (float)((point >> 3) * 8U + 4U) / 64.0f;
		}
		return 64U;
	}

	/* Bands of two rows, each the upper left triangle (0, top) (1, top) (0, bottom), then the lower right one. */
	for (band = 0U; band < 32U; band++) {
		top = (float)(2U * band) / 64.0f;
		bottom = (float)(2U * band + 2U) / 64.0f;
		corners[12U * band + 0U] = 0.0f;
		corners[12U * band + 1U] = top;
		corners[12U * band + 2U] = 1.0f;
		corners[12U * band + 3U] = top;
		corners[12U * band + 4U] = 0.0f;
		corners[12U * band + 5U] = bottom;
		corners[12U * band + 6U] = 1.0f;
		corners[12U * band + 7U] = top;
		corners[12U * band + 8U] = 1.0f;
		corners[12U * band + 9U] = bottom;
		corners[12U * band + 10U] = 0.0f;
		corners[12U * band + 11U] = bottom;
	}
	return 192U;
}

/* Runs one case and prints its line; 0 when every pixel checked matches (or the driver refused the pipeline). */
static int
vc_run(
	struct vc *vc,
	const struct vc_case *test,
	unsigned *refused)
{
	struct vc_store target;
	struct vc_store vertices;
	struct vc_store readback;
	VkShaderModule module;
	VkPipeline pipeline;
	VkFramebuffer framebuffer;
	VkFramebufferCreateInfo framebuffer_info;
	VkCommandBufferAllocateInfo command_info;
	VkCommandBufferBeginInfo begin;
	VkCommandBuffer command;
	VkRenderPassBeginInfo pass;
	VkClearValue clear;
	VkViewport viewport;
	VkRect2D scissor;
	VkDeviceSize offset;
	VkBufferImageCopy region;
	VkMemoryBarrier barrier;
	float corners[2U * VC_VERTICES_MAX];
	uint32_t push[8];
	uint8_t want[4];
	const uint8_t *got;
	uint32_t vertex_count;
	uint32_t wrong;
	uint32_t shown;
	uint32_t x;
	uint32_t y;
	uint32_t channel;
	int difference;
	VkResult status;

	memset(&target, 0, sizeof(target));
	memset(&vertices, 0, sizeof(vertices));
	memset(&readback, 0, sizeof(readback));
	module = VK_NULL_HANDLE;
	pipeline = VK_NULL_HANDLE;
	framebuffer = VK_NULL_HANDLE;

	/* A dual-source blend needs the device feature. */
	if (test->dual && !vc->dual_source) {
		printf("VKCOMPILE %s refused feature dualSrcBlend\n", test->name);
		(*refused)++;
		return 0;
	}

	/* The pipeline: a refusal says the driver does not have the feature. */
	status = vc_pipeline(vc, test, &module, &pipeline);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE %s refused pipeline %d\n", test->name, (int)status);
		(*refused)++;
		if (module != VK_NULL_HANDLE)
			vkDestroyShaderModule(vc->device, module, NULL);
		return 0;
	}

	/* The target, the vertices and the readback. */
	status = vc_image(vc, test->side, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &target);
	if (status == VK_SUCCESS)
		status = vc_buffer(vc, sizeof(corners), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vertices);
	if (status == VK_SUCCESS)
		status = vc_buffer(vc, (VkDeviceSize)test->side * test->side * 4U, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback);
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = vc->pass;
	framebuffer_info.attachmentCount = 1U;
	framebuffer_info.pAttachments = &target.view;
	framebuffer_info.width = test->side;
	framebuffer_info.height = test->side;
	framebuffer_info.layers = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateFramebuffer(vc->device, &framebuffer_info, NULL, &framebuffer);
	memset(&command_info, 0, sizeof(command_info));
	command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_info.commandPool = vc->command_pool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 1U;
	if (status == VK_SUCCESS)
		status = vkAllocateCommandBuffers(vc->device, &command_info, &command);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (status == VK_SUCCESS)
		status = vkBeginCommandBuffer(command, &begin);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE %s setup-failed %d FAIL\n", test->name, (int)status);
		goto out;
	}
	vertex_count = vc_geometry(test->geometry, corners);
	memcpy(vertices.mapping, corners, sizeof(corners));
	memset(readback.mapping, 0xa5, (size_t)test->side * test->side * 4U);

	/* The draw over the cleared target, with the rectangle (-1, -1) .. (1, 1) and the seed in the push constants. */
	memset(&clear, 0, sizeof(clear));
	clear.color.float32[0] = 0.2f;
	clear.color.float32[1] = 0.4f;
	clear.color.float32[2] = 0.6f;
	clear.color.float32[3] = 1.0f;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = vc->pass;
	pass.framebuffer = framebuffer;
	pass.renderArea.extent.width = test->side;
	pass.renderArea.extent.height = test->side;
	pass.clearValueCount = 1U;
	pass.pClearValues = &clear;
	vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)test->side;
	viewport.height = (float)test->side;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command, 0U, 1U, &viewport);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = test->side;
	scissor.extent.height = test->side;
	vkCmdSetScissor(command, 0U, 1U, &scissor);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, vc->pipeline_layout, 0U, 1U, &vc->set, 0U, NULL);
	offset = 0U;
	vkCmdBindVertexBuffers(command, 0U, 1U, &vertices.buffer, &offset);
	{
		float rect[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

		memcpy(push, rect, sizeof(rect));
	}
	push[4] = test->seed;
	push[5] = 0U;
	push[6] = 0U;
	push[7] = 0U;
	vkCmdPushConstants(command, vc->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(push), push);
	vkCmdDraw(command, vertex_count, 1U, 0U, 0U);
	vkCmdEndRenderPass(command);

	/* The target copied out. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 1U, &barrier, 0U, NULL, 0U, NULL);
	memset(&region, 0, sizeof(region));
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1U;
	region.imageExtent.width = test->side;
	region.imageExtent.height = test->side;
	region.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(command, target.image, VK_IMAGE_LAYOUT_GENERAL, readback.buffer, 1U, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &barrier, 0U, NULL, 0U, NULL);
	status = vkEndCommandBuffer(command);
	if (status == VK_SUCCESS)
		status = vc_submit(vc, command);
	if (status != VK_SUCCESS) {
		printf("VKCOMPILE %s submit-failed %d FAIL\n", test->name, (int)status);
		goto out;
	}

	/* Every pixel against what the CPU computes, within 1 per channel. */
	wrong = 0U;
	shown = 0U;
	for (y = 0U; y < test->side; y++) {
		for (x = 0U; x < test->side; x++) {
			if (test->expect(vc, test->seed, x, y, want) == 0)
				continue;
			got = readback.mapping + ((size_t)y * test->side + x) * 4U;
			for (channel = 0U; channel < 4U; channel++) {
				difference = (int)got[channel] - (int)want[channel];
				if (difference > 1 || difference < -1)
					break;
			}
			if (channel == 4U)
				continue;
			wrong++;
			if (shown < 4U) {
				printf("VKCOMPILE %s at %u,%u bgra got %02x%02x%02x%02x want %02x%02x%02x%02x\n", test->name, x, y, got[0], got[1],
				       got[2], got[3], want[0], want[1], want[2], want[3]);
				shown++;
			}
		}
	}
	printf("VKCOMPILE %s %ux%u wrong=%u %s\n", test->name, test->side, test->side, wrong, wrong == 0U ? "ok" : "FAIL");
	status = wrong == 0U ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;

out:
	if (framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(vc->device, framebuffer, NULL);
	vc_store_free(vc, &target);
	vc_store_free(vc, &vertices);
	vc_store_free(vc, &readback);
	vkDestroyPipeline(vc->device, pipeline, NULL);
	vkDestroyShaderModule(vc->device, module, NULL);
	return status == VK_SUCCESS ? 0 : 1;
}

/* f = x y / 256 at pixel centres: the coarse differences from the quad's top left, the fine ones from the pixel's row or column, times 4. */
static int
vc_expect_derivative(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	float qx;
	float qy;
	float px;
	float py;
	float coarse_x;
	float fine_x;
	float coarse_y;
	float fine_y;

	(void)vc;
	(void)seed;
	qx = (float)(x & ~1U) + 0.5f;
	qy = (float)(y & ~1U) + 0.5f;
	px = (float)x + 0.5f;
	py = (float)y + 0.5f;
	coarse_x = (qx + 1.0f) * qy / 256.0f - qx * qy / 256.0f;
	fine_x = (qx + 1.0f) * py / 256.0f - qx * py / 256.0f;
	coarse_y = qx * (qy + 1.0f) / 256.0f - qx * qy / 256.0f;
	fine_y = px * (qy + 1.0f) / 256.0f - px * qy / 256.0f;
	bgra[2] = vc_unorm(coarse_x * 4.0f);
	bgra[1] = vc_unorm(fine_x * 4.0f);
	bgra[0] = vc_unorm(coarse_y * 4.0f);
	bgra[3] = vc_unorm(fine_y * 4.0f);
	return 1;
}

/* One texel's byte (0 B, 1 G, 2 R, 3 A), the coordinates clamped to the image. */
static uint8_t
vc_texel(
	const struct vc *vc,
	int32_t x,
	int32_t y,
	uint32_t byte)
{
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x > (int32_t)VC_TEXTURE_SIDE - 1)
		x = (int32_t)VC_TEXTURE_SIDE - 1;
	if (y > (int32_t)VC_TEXTURE_SIDE - 1)
		y = (int32_t)VC_TEXTURE_SIDE - 1;
	return vc->texels[((uint32_t)y * VC_TEXTURE_SIDE + (uint32_t)x) * 4U + byte];
}

/* A gather's four texels (i0, j1), (i1, j1), (i1, j0), (i0, j0) as R, G, B, A, of one texel byte, from (i0, j0). */
static void
vc_gathered(
	const struct vc *vc,
	int32_t i0,
	int32_t j0,
	uint32_t byte,
	uint8_t *bgra)
{
	bgra[2] = vc_texel(vc, i0, j0 + 1, byte);
	bgra[1] = vc_texel(vc, i0 + 1, j0 + 1, byte);
	bgra[0] = vc_texel(vc, i0 + 1, j0, byte);
	bgra[3] = vc_texel(vc, i0, j0, byte);
}

/* The gather of green at the corner of texels (x, y) and (x + 1, y + 1). */
static int
vc_expect_gather(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	(void)seed;
	vc_gathered(vc, (int32_t)x, (int32_t)y, 1U, bgra);
	return 1;
}

/* The gather of alpha moved by (1, -1). */
static int
vc_expect_gather_offset(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	(void)seed;
	vc_gathered(vc, (int32_t)x + 1, (int32_t)y - 1, 3U, bgra);
	return 1;
}

/* The band's triangle: 2 band in the upper left one, 2 band + 1 in the lower right one. */
static int
vc_expect_primitive(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	uint32_t band;
	uint32_t lower;
	float across;
	float down;

	(void)vc;
	(void)seed;
	band = y / 2U;
	across = ((float)x + 0.5f) / 64.0f;
	down = ((float)y + 0.5f - (float)(2U * band)) / 2.0f;
	lower = 0U;
	if (across + down > 1.0f)
		lower = 1U;
	bgra[2] = (uint8_t)band;
	bgra[1] = lower != 0U ? 0xffU : 0U;
	bgra[0] = 0U;
	bgra[3] = 0xffU;
	return 1;
}

/* Inside the 8x8 point of its cell, the pixel's place in it ((x + 0.5) / 8 from the point's left edge, top first). */
static int
vc_expect_point(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	(void)vc;
	(void)seed;
	bgra[2] = vc_unorm(((float)(x & 7U) + 0.5f) / 8.0f);
	bgra[1] = vc_unorm(((float)(y & 7U) + 0.5f) / 8.0f);
	bgra[0] = 0U;
	bgra[3] = 0xffU;
	return 1;
}

/* The unsigned quotient and remainder of divide.frag, each folded to 16 bits. */
static int
vc_expect_divide(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	uint32_t a;
	uint32_t b;
	uint32_t q;
	uint32_t r;

	(void)vc;
	a = vc_hash(x + y * 64U + seed * 4096U);
	b = (vc_hash(a ^ 0x9e3779b9U) >> ((x + y) & 31U)) | 1U;
	q = a / b;
	r = a % b;
	q ^= q >> 16;
	r ^= r >> 16;
	bgra[2] = (uint8_t)q;
	bgra[1] = (uint8_t)(q >> 8);
	bgra[0] = (uint8_t)r;
	bgra[3] = (uint8_t)(r >> 8);
	return 1;
}

/*
 * The truncated signed quotient and the modulus of divide_signed.frag, each
 * folded to 16 bits.  GLSL's % is OpSMod, whose non-zero result takes the
 * divisor's sign (the SPIR-V specification): the truncated remainder plus
 * the divisor where the remainder is not zero and the operands' signs
 * differ.  (llvmpipe gives the truncated remainder instead, which GLSL
 * allows for negative operands, so this case differs there.)
 */
static int
vc_expect_divide_signed(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	uint32_t h;
	uint32_t g;
	int32_t a;
	int32_t b;
	int32_t remainder;
	uint32_t q;
	uint32_t r;

	(void)vc;
	h = vc_hash(x * 131U + y * 7919U + seed * 65537U);
	a = (int32_t)(h & 0x7fffffffU);
	if ((h & 0x80000000U) != 0U)
		a = -a;
	g = vc_hash(h ^ 0x85ebca6bU);
	b = (int32_t)(((g >> ((x ^ y) & 31U)) | 1U) & 0x7fffffffU);
	if ((g & 1U) != 0U)
		b = -b;
	q = (uint32_t)(a / b);
	remainder = a % b;
	if (remainder != 0 && (a < 0) != (b < 0))
		remainder += b;
	r = (uint32_t)remainder;
	q ^= q >> 16;
	r ^= r >> 16;
	bgra[2] = (uint8_t)q;
	bgra[1] = (uint8_t)(q >> 8);
	bgra[0] = (uint8_t)r;
	bgra[3] = (uint8_t)(r >> 8);
	return 1;
}

/* The background where discard.frag discards, its colour elsewhere. */
static int
vc_expect_discard(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	(void)vc;
	(void)seed;
	if ((x >= 32U && y >= 32U) || (((x >> 2) + (y >> 2)) & 1U) == 1U || ((y & 7U) == 7U && (x & 1U) == 1U)) {
		memcpy(bgra, vc_background, 4U);
		return 1;
	}
	bgra[2] = (uint8_t)x;
	bgra[1] = (uint8_t)y;
	bgra[0] = vc_unorm(0.5f);
	bgra[3] = 0xffU;
	return 1;
}

/* The sampled image's size (16, 16) and level count (1). */
static int
vc_expect_size(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	(void)vc;
	(void)seed;
	(void)x;
	(void)y;
	bgra[2] = VC_TEXTURE_SIDE;
	bgra[1] = VC_TEXTURE_SIDE;
	bgra[0] = 1U;
	bgra[3] = 0U;
	return 1;
}

/* The twelve fetches of chain.frag, each at the texel the red and green bytes of the one before (mixed with the step) choose. */
static int
vc_expect_chain(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	uint32_t sum[4];
	uint32_t at_x;
	uint32_t at_y;
	uint32_t step;
	uint32_t red;
	uint32_t green;

	(void)seed;
	memset(sum, 0, sizeof(sum));
	at_x = x & 15U;
	at_y = y & 15U;
	for (step = 0U; step < 12U; step++) {
		red = vc_texel(vc, (int32_t)at_x, (int32_t)at_y, 2U);
		green = vc_texel(vc, (int32_t)at_x, (int32_t)at_y, 1U);
		sum[0] += red;
		sum[1] += green;
		sum[2] += vc_texel(vc, (int32_t)at_x, (int32_t)at_y, 0U);
		sum[3] += vc_texel(vc, (int32_t)at_x, (int32_t)at_y, 3U);
		at_x = (red ^ step) & 15U;
		at_y = (green ^ (step * 3U)) & 15U;
	}
	bgra[2] = (uint8_t)sum[0];
	bgra[1] = (uint8_t)sum[1];
	bgra[0] = (uint8_t)sum[2];
	bgra[3] = (uint8_t)sum[3];
	return 1;
}

/* colour + background * factor: R x + 0.2 * 0.5, G y + 0.4 * 0.25, B 0.25 + 0.6, A 0.5 + 1.0 * 0 (each clamped to 1). */
static int
vc_expect_dual(
	const struct vc *vc,
	uint32_t seed,
	uint32_t x,
	uint32_t y,
	uint8_t *bgra)
{
	(void)vc;
	(void)seed;
	bgra[2] = vc_unorm((float)x / 255.0f + 0.2f * 0.5f);
	bgra[1] = vc_unorm((float)y / 255.0f + 0.4f * 0.25f);
	bgra[0] = vc_unorm(0.25f + 0.6f);
	bgra[3] = vc_unorm(0.5f);
	return 1;
}

/* The hash of the pixel programs. */
static uint32_t
vc_hash(
	uint32_t v)
{
	v ^= v >> 16;
	v *= 0x7feb352dU;
	v ^= v >> 15;
	v *= 0x846ca68bU;
	v ^= v >> 16;
	return v;
}

/* A float in [0, 1] as an 8-bit unorm, rounded. */
static uint8_t
vc_unorm(
	float value)
{
	if (value <= 0.0f)
		return 0U;
	if (value >= 1.0f)
		return 0xffU;
	return (uint8_t)(value * 255.0f + 0.5f);
}

/* Frees a store's objects. */
static void
vc_store_free(
	struct vc *vc,
	struct vc_store *store)
{
	if (store->view != VK_NULL_HANDLE)
		vkDestroyImageView(vc->device, store->view, NULL);
	if (store->image != VK_NULL_HANDLE)
		vkDestroyImage(vc->device, store->image, NULL);
	if (store->buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(vc->device, store->buffer, NULL);
	if (store->memory != VK_NULL_HANDLE)
		vkFreeMemory(vc->device, store->memory, NULL);
	memset(store, 0, sizeof(*store));
}

/* Frees everything the run made. */
static void
vc_free(
	struct vc *vc)
{
	if (vc->device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(vc->device);
		vc_store_free(vc, &vc->texture);
		vkDestroyDescriptorPool(vc->device, vc->descriptor_pool, NULL);
		vkDestroyRenderPass(vc->device, vc->pass, NULL);
		vkDestroyPipelineLayout(vc->device, vc->pipeline_layout, NULL);
		vkDestroyDescriptorSetLayout(vc->device, vc->set_layout, NULL);
		vkDestroyShaderModule(vc->device, vc->vertex_module, NULL);
		vkDestroySampler(vc->device, vc->sampler, NULL);
		vkDestroyCommandPool(vc->device, vc->command_pool, NULL);
		vkDestroyDevice(vc->device, NULL);
	}
	if (vc->instance != VK_NULL_HANDLE)
		vkDestroyInstance(vc->instance, NULL);
}
