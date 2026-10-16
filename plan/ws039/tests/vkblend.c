/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkblend: what a render leaves in memory, and how a blend combines,
 * checked pixel by pixel through the public Vulkan 1.0 API with the
 * compositor's quad shaders (ws039-p009, the RX 5600 XT).
 *
 * Each case uploads a B8G8R8A8 picture, draws it 1:1 over a target
 * cleared to an opaque colour (no blend, premultiplied ONE /
 * ONE_MINUS_SRC_ALPHA, or straight SRC_ALPHA / ONE_MINUS_SRC_ALPHA), and
 * reads the target three ways: (copy) an image-to-buffer copy, (cpu) the
 * target's own memory through the CPU mapping (the linear layout's row
 * pitch, or for an optimal image the executor's 256-byte pitch), and
 * (sample) a second draw sampling the target into another target, then
 * copied.  One line a case:
 *
 *   VKBLEND <case> <w>x<h> <tiling> <blend> copy=<wrong> cpu=<wrong> sample=<wrong> max=<diff> [first ...]
 *   VKBLEND ok cases=<n>   or   VKBLEND fail cases=<n> failed=<n>
 *
 * The coordinate pictures are compared exactly; the blends within 1.
 *
 * usage: vkblend [--device-index=N] [--case=NAME] [--dump=PREFIX]
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "userland/desktop/wayland/shaders.h"

#define BLEND_PIXEL		4U
#define BLEND_WAIT_NS		UINT64_C(10000000000)
#define BLEND_PITCH_ALIGN	256U

/* The background every case clears to (R 0.2, G 0.4, B 0.6, A 1.0) as B, G, R, A bytes. */
#define BLEND_BACK_B		0x99U
#define BLEND_BACK_G		0x66U
#define BLEND_BACK_R		0x33U
#define BLEND_BACK_A		0xffU

/* The source colour of the blend pictures (straight): R 40, G 200, B 120. */
#define BLEND_SOURCE_R		40U
#define BLEND_SOURCE_G		200U
#define BLEND_SOURCE_B		120U

enum blend_mode { BLEND_NONE, BLEND_PREMULTIPLIED, BLEND_STRAIGHT, BLEND_MODES };
enum blend_picture { PICTURE_COORDINATES, PICTURE_ALPHA_BANDS };

struct blend_case {
	const char *name;
	uint32_t width;
	uint32_t height;
	VkImageTiling tiling;
	enum blend_mode mode;
	enum blend_picture picture;
	bool device;
	uint32_t skew;
};

static const struct blend_case blend_cases[] = {
	{ "read-240x135-optimal", 240U, 135U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-240x135-linear", 240U, 135U, VK_IMAGE_TILING_LINEAR, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-256x64-optimal", 256U, 64U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-256x64-linear", 256U, 64U, VK_IMAGE_TILING_LINEAR, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-1920x64-optimal", 1920U, 64U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-1920x64-linear", 1920U, 64U, VK_IMAGE_TILING_LINEAR, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-1180x40-optimal", 1180U, 40U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "read-100x30-optimal", 100U, 30U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, false, 0U },
	{ "premul-240x135-optimal", 240U, 135U, VK_IMAGE_TILING_OPTIMAL, BLEND_PREMULTIPLIED, PICTURE_ALPHA_BANDS, false, 0U },
	{ "straight-240x135-optimal", 240U, 135U, VK_IMAGE_TILING_OPTIMAL, BLEND_STRAIGHT, PICTURE_ALPHA_BANDS, false, 0U },
	{ "premul-256x64-linear", 256U, 64U, VK_IMAGE_TILING_LINEAR, BLEND_PREMULTIPLIED, PICTURE_ALPHA_BANDS, false, 0U },
	{ "straight-256x64-linear", 256U, 64U, VK_IMAGE_TILING_LINEAR, BLEND_STRAIGHT, PICTURE_ALPHA_BANDS, false, 0U },
	{ "premul-1920x64-optimal", 1920U, 64U, VK_IMAGE_TILING_OPTIMAL, BLEND_PREMULTIPLIED, PICTURE_ALPHA_BANDS, false, 0U },
	{ "straight-1920x64-optimal", 1920U, 64U, VK_IMAGE_TILING_OPTIMAL, BLEND_STRAIGHT, PICTURE_ALPHA_BANDS, false, 0U },
	{ "skew-read-100x30-optimal", 100U, 30U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, false, 4U },
	{ "device-read-240x135-optimal", 240U, 135U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, true, 0U },
	{ "device-read-1920x64-optimal", 1920U, 64U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, true, 0U },
	{ "device-premul-240x135-optimal", 240U, 135U, VK_IMAGE_TILING_OPTIMAL, BLEND_PREMULTIPLIED, PICTURE_ALPHA_BANDS, true, 0U },
	{ "device-skew-read-1180x40-optimal", 1180U, 40U, VK_IMAGE_TILING_OPTIMAL, BLEND_NONE, PICTURE_COORDINATES, true, 4U },
	{ "device-skew-premul-100x30-optimal", 100U, 30U, VK_IMAGE_TILING_OPTIMAL, BLEND_PREMULTIPLIED, PICTURE_ALPHA_BANDS, true, 4U },
};

/* One image or buffer with its own memory: host-visible and mapped, or device-local only (mapping NULL). */
struct blend_store {
	VkImage image;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkImageView view;
	uint8_t *mapping;
	uint32_t pitch;
};

struct blend {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t memory_type;
	uint32_t device_type;
	VkSampler sampler;
	VkShaderModule vertex_module;
	VkShaderModule fragment_module;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout pipeline_layout;
	VkRenderPass pass;
	VkPipeline pipelines[BLEND_MODES];
	VkCommandPool command_pool;
	const char *dump;
	char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

/* The per case objects. */
struct blend_run {
	struct blend_store target;
	struct blend_store copy;
	struct blend_store vertices;
	struct blend_store staging;
	struct blend_store texture;
	struct blend_store readback;
	struct blend_store readback2;
	VkFramebuffer framebuffer;
	VkFramebuffer copy_framebuffer;
	VkDescriptorPool pool;
	VkDescriptorSet sets[2];
	VkCommandBuffer command;
	VkFence fence;
};

static VkResult blend_device(struct blend *blend, int index);
static VkResult blend_pipelines(struct blend *blend);
static VkResult blend_image(struct blend *blend, uint32_t width, uint32_t height, VkImageTiling tiling, VkImageUsageFlags usage, bool device,
			    struct blend_store *store);
static VkResult blend_buffer(struct blend *blend, VkDeviceSize bytes, VkBufferUsageFlags usage, struct blend_store *store);
static int blend_case_run(struct blend *blend, const struct blend_case *test);
static void blend_draw(struct blend *blend, VkCommandBuffer command, VkFramebuffer framebuffer, VkPipeline pipeline, VkDescriptorSet set,
		       VkBuffer vertices, uint32_t width, uint32_t height);
static uint32_t blend_band_alpha(uint32_t x);
static void blend_source(const struct blend_case *test, uint32_t x, uint32_t y, uint8_t *texel);
static void blend_expected(const struct blend_case *test, uint32_t x, uint32_t y, uint8_t *pixel);
static uint32_t blend_compare(const struct blend_case *test, const char *what, const uint8_t *rows, uint32_t pitch, uint32_t *largest);
static void blend_write(const struct blend *blend, const struct blend_case *test, const char *what, const uint8_t *rows, uint32_t pitch);
static void blend_run_free(struct blend *blend, struct blend_run *run);
static void blend_store_free(struct blend *blend, struct blend_store *store);
static void blend_free(struct blend *blend);

int
main(
	int argc,
	char **argv)
{
	struct blend blend;
	const char *only;
	unsigned count;
	unsigned failed;
	unsigned test;
	int index;
	int argument;

	/* The arguments. */
	index = 0;
	only = NULL;
	memset(&blend, 0, sizeof(blend));
	for (argument = 1; argument < argc; argument++) {
		if (strncmp(argv[argument], "--device-index=", 15) == 0) {
			index = atoi(argv[argument] + 15);
		} else if (strncmp(argv[argument], "--case=", 7) == 0) {
			only = argv[argument] + 7;
		} else if (strncmp(argv[argument], "--dump=", 7) == 0) {
			blend.dump = argv[argument] + 7;
		} else {
			fprintf(stderr, "usage: vkblend [--device-index=N] [--case=NAME] [--dump=PREFIX]\n");
			return 2;
		}
	}

	/* The device and the pipelines. */
	if (blend_device(&blend, index) != VK_SUCCESS || blend_pipelines(&blend) != VK_SUCCESS) {
		blend_free(&blend);
		return 1;
	}
	printf("VKBLEND device=%s\n", blend.name);

	/* Each case. */
	count = 0U;
	failed = 0U;
	for (test = 0U; test < sizeof(blend_cases) / sizeof(blend_cases[0]); test++) {
		if (only != NULL && strcmp(only, blend_cases[test].name) != 0)
			continue;
		count++;
		if (blend_case_run(&blend, &blend_cases[test]) != 0)
			failed++;
	}
	blend_free(&blend);

	/* The result. */
	if (failed != 0U) {
		printf("VKBLEND fail cases=%u failed=%u\n", count, failed);
		return 1;
	}
	printf("VKBLEND ok cases=%u\n", count);
	return 0;
}

/* Creates the instance and the device with its queue; finds a host-visible coherent memory type. */
static VkResult
blend_device(
	struct blend *blend,
	int index)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkPhysicalDevice physicals[8];
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceMemoryProperties memory;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	VkSamplerCreateInfo sampler;
	VkCommandPoolCreateInfo pool;
	float priority;
	uint32_t count;
	uint32_t type;
	VkResult status;

	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkblend";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	status = vkCreateInstance(&instance, NULL, &blend->instance);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail instance %d\n", (int)status);
		return status;
	}
	count = 8U;
	status = vkEnumeratePhysicalDevices(blend->instance, &count, physicals);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || count == 0U || index < 0 || (uint32_t)index >= count) {
		printf("VKBLEND fail devices %d count=%u index=%d\n", (int)status, count, index);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	blend->physical = physicals[index];
	vkGetPhysicalDeviceProperties(blend->physical, &properties);
	snprintf(blend->name, sizeof(blend->name), "%s", properties.deviceName);

	vkGetPhysicalDeviceMemoryProperties(blend->physical, &memory);
	for (type = 0U; type < memory.memoryTypeCount; type++) {
		if ((memory.memoryTypes[type].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
		    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			break;
	}
	if (type == memory.memoryTypeCount) {
		printf("VKBLEND fail memory-type none of %u\n", memory.memoryTypeCount);
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
	blend->memory_type = type;

	/* A device-local type the CPU does not reach, when there is one. */
	blend->device_type = UINT32_MAX;
	for (type = 0U; type < memory.memoryTypeCount; type++) {
		if ((memory.memoryTypes[type].propertyFlags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ==
		    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
			blend->device_type = type;
			break;
		}
	}

	/* The types found. */
	printf("VKBLEND memory types=%u host=%u device=%d\n", memory.memoryTypeCount, blend->memory_type,
	       blend->device_type == UINT32_MAX ? -1 : (int)blend->device_type);

	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1U;
	device.pQueueCreateInfos = &queue;
	status = vkCreateDevice(blend->physical, &device, NULL, &blend->device);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail device %d\n", (int)status);
		return status;
	}
	vkGetDeviceQueue(blend->device, 0U, 0U, &blend->queue);

	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	status = vkCreateSampler(blend->device, &sampler, NULL, &blend->sampler);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail sampler %d\n", (int)status);
		return status;
	}

	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	status = vkCreateCommandPool(blend->device, &pool, NULL, &blend->command_pool);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail command-pool %d\n", (int)status);
		return status;
	}
	return VK_SUCCESS;
}

/* Creates the modules, the layouts, the clearing pass and a pipeline for each blend. */
static VkResult
blend_pipelines(
	struct blend *blend)
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
	unsigned mode;
	VkResult status;

	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = sizeof(kwl_quad_vert);
	module.pCode = kwl_quad_vert;
	status = vkCreateShaderModule(blend->device, &module, NULL, &blend->vertex_module);
	if (status == VK_SUCCESS) {
		module.codeSize = sizeof(kwl_quad_frag);
		module.pCode = kwl_quad_frag;
		status = vkCreateShaderModule(blend->device, &module, NULL, &blend->fragment_module);
	}
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail module %d\n", (int)status);
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
	status = vkCreateDescriptorSetLayout(blend->device, &set_layout, NULL, &blend->set_layout);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail set-layout %d\n", (int)status);
		return status;
	}
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = 32U;
	memset(&pipeline_layout, 0, sizeof(pipeline_layout));
	pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout.setLayoutCount = 1U;
	pipeline_layout.pSetLayouts = &blend->set_layout;
	pipeline_layout.pushConstantRangeCount = 1U;
	pipeline_layout.pPushConstantRanges = &range;
	status = vkCreatePipelineLayout(blend->device, &pipeline_layout, NULL, &blend->pipeline_layout);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail pipeline-layout %d\n", (int)status);
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
	status = vkCreateRenderPass(blend->device, &pass, NULL, &blend->pass);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail render-pass %d\n", (int)status);
		return status;
	}

	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = blend->vertex_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = blend->fragment_module;
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
	create.layout = blend->pipeline_layout;
	create.renderPass = blend->pass;
	create.basePipelineIndex = -1;

	for (mode = 0U; mode < BLEND_MODES; mode++) {
		memset(&blend_attachment, 0, sizeof(blend_attachment));
		blend_attachment.colorWriteMask = 0xfU;
		if (mode != BLEND_NONE) {
			blend_attachment.blendEnable = VK_TRUE;
			blend_attachment.srcColorBlendFactor = mode == BLEND_PREMULTIPLIED ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			blend_attachment.srcAlphaBlendFactor = blend_attachment.srcColorBlendFactor;
			blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		status = vkCreateGraphicsPipelines(blend->device, VK_NULL_HANDLE, 1U, &create, NULL, &blend->pipelines[mode]);
		if (status != VK_SUCCESS) {
			printf("VKBLEND fail pipeline %u %d\n", mode, (int)status);
			return status;
		}
	}
	return VK_SUCCESS;
}

/* Creates a 2D B8G8R8A8 image with its own memory (mapped unless device-local only), and its view. */
static VkResult
blend_image(
	struct blend *blend,
	uint32_t width,
	uint32_t height,
	VkImageTiling tiling,
	VkImageUsageFlags usage,
	bool device,
	struct blend_store *store)
{
	VkImageCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	VkImageViewCreateInfo view;
	void *mapping;
	VkResult status;

	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = VK_FORMAT_B8G8R8A8_UNORM;
	create.extent.width = width;
	create.extent.height = height;
	create.extent.depth = 1U;
	create.mipLevels = 1U;
	create.arrayLayers = 1U;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = tiling;
	create.usage = usage;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	status = vkCreateImage(blend->device, &create, NULL, &store->image);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail image %ux%u tiling=%d %d\n", width, height, (int)tiling, (int)status);
		return status;
	}
	vkGetImageMemoryRequirements(blend->device, store->image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = blend->memory_type;
	if (device)
		allocate.memoryTypeIndex = blend->device_type;
	mapping = NULL;
	status = vkAllocateMemory(blend->device, &allocate, NULL, &store->memory);
	if (status == VK_SUCCESS)
		status = vkBindImageMemory(blend->device, store->image, store->memory, 0U);
	if (status == VK_SUCCESS && !device)
		status = vkMapMemory(blend->device, store->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail image-memory %ux%u size=%llu %d\n", width, height, (unsigned long long)requirements.size, (int)status);
		return status;
	}
	store->mapping = mapping;

	/* The row pitch: the linear layout's, or the executor's for an optimal image. */
	store->pitch = (width * BLEND_PIXEL + BLEND_PITCH_ALIGN - 1U) & ~(BLEND_PITCH_ALIGN - 1U);
	if (tiling == VK_IMAGE_TILING_LINEAR) {
		memset(&subresource, 0, sizeof(subresource));
		subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkGetImageSubresourceLayout(blend->device, store->image, &subresource, &layout);
		store->pitch = (uint32_t)layout.rowPitch;
	}

	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = store->image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	status = vkCreateImageView(blend->device, &view, NULL, &store->view);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail view %d\n", (int)status);
		return status;
	}
	return VK_SUCCESS;
}

/* Creates a buffer with its own memory, mapped. */
static VkResult
blend_buffer(
	struct blend *blend,
	VkDeviceSize bytes,
	VkBufferUsageFlags usage,
	struct blend_store *store)
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
	status = vkCreateBuffer(blend->device, &create, NULL, &store->buffer);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail buffer %d\n", (int)status);
		return status;
	}
	vkGetBufferMemoryRequirements(blend->device, store->buffer, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = blend->memory_type;
	status = vkAllocateMemory(blend->device, &allocate, NULL, &store->memory);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(blend->device, store->buffer, store->memory, 0U);
	if (status == VK_SUCCESS)
		status = vkMapMemory(blend->device, store->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status != VK_SUCCESS) {
		printf("VKBLEND fail buffer-memory %llu %d\n", (unsigned long long)bytes, (int)status);
		return status;
	}
	store->mapping = mapping;
	return VK_SUCCESS;
}

/* Runs one case and prints its line; 0 when every way of reading it matches. */
static int
blend_case_run(
	struct blend *blend,
	const struct blend_case *test)
{
	static const float corners[12] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	struct blend_run run;
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorSetLayout layouts[2];
	VkDescriptorImageInfo images[2];
	VkWriteDescriptorSet writes[2];
	VkFramebufferCreateInfo framebuffer;
	VkCommandBufferAllocateInfo command;
	VkCommandBufferBeginInfo begin;
	VkBufferImageCopy region;
	VkMemoryBarrier barrier;
	VkFenceCreateInfo fence;
	VkSubmitInfo submit;
	VkResult status;
	uint32_t bytes;
	uint32_t x;
	uint32_t y;
	uint32_t wrong_copy;
	uint32_t wrong_cpu;
	uint32_t wrong_sample;
	uint32_t largest;
	uint32_t largest_cpu;
	uint32_t largest_sample;
	const char *tiling;
	static const char *const modes[BLEND_MODES] = { "none", "premultiplied", "straight" };

	memset(&run, 0, sizeof(run));
	tiling = test->tiling == VK_IMAGE_TILING_LINEAR ? "linear" : "optimal";
	bytes = test->width * test->height * BLEND_PIXEL;
	if (test->device && blend->device_type == UINT32_MAX) {
		printf("VKBLEND %s no device-local type\n", test->name);
		return 1;
	}

	/* The images and buffers. */
	status = blend_image(blend, test->width, test->height, test->tiling,
			     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, test->device,
			     &run.target);
	if (status == VK_SUCCESS)
		status = blend_image(blend, test->width, test->height, VK_IMAGE_TILING_OPTIMAL,
				     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, test->device, &run.copy);
	if (status == VK_SUCCESS)
		status = blend_image(blend, test->width, test->height, VK_IMAGE_TILING_OPTIMAL,
				     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, test->device, &run.texture);
	if (status == VK_SUCCESS)
		status = blend_buffer(blend, sizeof(corners), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &run.vertices);
	if (status == VK_SUCCESS)
		status = blend_buffer(blend, bytes + test->skew, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &run.staging);
	if (status == VK_SUCCESS)
		status = blend_buffer(blend, bytes + test->skew, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run.readback);
	if (status == VK_SUCCESS)
		status = blend_buffer(blend, bytes + test->skew, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &run.readback2);
	if (status != VK_SUCCESS) {
		printf("VKBLEND %s %ux%u %s %s setup-failed\n", test->name, test->width, test->height, tiling, modes[test->mode]);
		blend_run_free(blend, &run);
		return 1;
	}

	/* The picture, the corners, and a target poisoned so that unwritten pixels show. */
	memcpy(run.vertices.mapping, corners, sizeof(corners));
	for (y = 0U; y < test->height; y++)
		for (x = 0U; x < test->width; x++)
			blend_source(test, x, y, run.staging.mapping + test->skew + (y * test->width + x) * BLEND_PIXEL);
	memset(run.readback.mapping, 0xa5, bytes + test->skew);
	memset(run.readback2.mapping, 0xa5, bytes + test->skew);

	/* The two sets: the picture for the first draw, the target for the second. */
	memset(&size, 0, sizeof(size));
	size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	size.descriptorCount = 2U;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 2U;
	pool.poolSizeCount = 1U;
	pool.pPoolSizes = &size;
	status = vkCreateDescriptorPool(blend->device, &pool, NULL, &run.pool);
	layouts[0] = blend->set_layout;
	layouts[1] = blend->set_layout;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = run.pool;
	allocate.descriptorSetCount = 2U;
	allocate.pSetLayouts = layouts;
	if (status == VK_SUCCESS)
		status = vkAllocateDescriptorSets(blend->device, &allocate, run.sets);
	if (status != VK_SUCCESS) {
		printf("VKBLEND %s sets-failed %d\n", test->name, (int)status);
		blend_run_free(blend, &run);
		return 1;
	}
	memset(images, 0, sizeof(images));
	images[0].sampler = blend->sampler;
	images[0].imageView = run.texture.view;
	images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	images[1].sampler = blend->sampler;
	images[1].imageView = run.target.view;
	images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	memset(writes, 0, sizeof(writes));
	for (x = 0U; x < 2U; x++) {
		writes[x].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[x].dstSet = run.sets[x];
		writes[x].descriptorCount = 1U;
		writes[x].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[x].pImageInfo = &images[x];
	}
	vkUpdateDescriptorSets(blend->device, 2U, writes, 0U, NULL);

	/* The framebuffers. */
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = blend->pass;
	framebuffer.attachmentCount = 1U;
	framebuffer.pAttachments = &run.target.view;
	framebuffer.width = test->width;
	framebuffer.height = test->height;
	framebuffer.layers = 1U;
	status = vkCreateFramebuffer(blend->device, &framebuffer, NULL, &run.framebuffer);
	framebuffer.pAttachments = &run.copy.view;
	if (status == VK_SUCCESS)
		status = vkCreateFramebuffer(blend->device, &framebuffer, NULL, &run.copy_framebuffer);
	if (status != VK_SUCCESS) {
		printf("VKBLEND %s framebuffer-failed %d\n", test->name, (int)status);
		blend_run_free(blend, &run);
		return 1;
	}

	/* The commands. */
	memset(&command, 0, sizeof(command));
	command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command.commandPool = blend->command_pool;
	command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command.commandBufferCount = 1U;
	status = vkAllocateCommandBuffers(blend->device, &command, &run.command);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (status == VK_SUCCESS)
		status = vkBeginCommandBuffer(run.command, &begin);
	if (status != VK_SUCCESS) {
		printf("VKBLEND %s command-failed %d\n", test->name, (int)status);
		blend_run_free(blend, &run);
		return 1;
	}
	memset(&region, 0, sizeof(region));
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1U;
	region.imageExtent.width = test->width;
	region.imageExtent.height = test->height;
	region.imageExtent.depth = 1U;
	region.bufferOffset = test->skew;
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

	/* The upload, then the draw of the picture over the cleared target. */
	vkCmdCopyBufferToImage(run.command, run.staging.buffer, run.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(run.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 1U, &barrier, 0U, NULL, 0U,
			     NULL);
	blend_draw(blend, run.command, run.framebuffer, blend->pipelines[test->mode], run.sets[0], run.vertices.buffer, test->width,
		   test->height);

	/* The target copied out, and sampled into the second target, which is copied out. */
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(run.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			     VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &barrier, 0U,
			     NULL, 0U, NULL);
	vkCmdCopyImageToBuffer(run.command, run.target.image, VK_IMAGE_LAYOUT_GENERAL, run.readback.buffer, 1U, &region);
	blend_draw(blend, run.command, run.copy_framebuffer, blend->pipelines[BLEND_NONE], run.sets[1], run.vertices.buffer, test->width,
		   test->height);
	vkCmdPipelineBarrier(run.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 1U, &barrier, 0U, NULL,
			     0U, NULL);
	vkCmdCopyImageToBuffer(run.command, run.copy.image, VK_IMAGE_LAYOUT_GENERAL, run.readback2.buffer, 1U, &region);
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(run.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0U, 1U, &barrier, 0U, NULL, 0U, NULL);
	status = vkEndCommandBuffer(run.command);

	/* Submitted and waited for. */
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (status == VK_SUCCESS)
		status = vkCreateFence(blend->device, &fence, NULL, &run.fence);
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &run.command;
	if (status == VK_SUCCESS)
		status = vkQueueSubmit(blend->queue, 1U, &submit, run.fence);
	if (status == VK_SUCCESS)
		status = vkWaitForFences(blend->device, 1U, &run.fence, VK_TRUE, BLEND_WAIT_NS);
	if (status != VK_SUCCESS) {
		printf("VKBLEND %s submit-failed %d\n", test->name, (int)status);
		blend_run_free(blend, &run);
		return 1;
	}

	/* The three readings. */
	largest = 0U;
	largest_cpu = 0U;
	largest_sample = 0U;
	wrong_copy = blend_compare(test, "copy", run.readback.mapping + test->skew, test->width * BLEND_PIXEL, &largest);
	wrong_cpu = 0U;
	if (run.target.mapping != NULL)
		wrong_cpu = blend_compare(test, "cpu", run.target.mapping, run.target.pitch, &largest_cpu);
	wrong_sample = blend_compare(test, "sample", run.readback2.mapping + test->skew, test->width * BLEND_PIXEL, &largest_sample);
	if (largest_cpu > largest)
		largest = largest_cpu;
	if (largest_sample > largest)
		largest = largest_sample;
	printf("VKBLEND %s %ux%u %s %s %s skew=%u pitch=%u copy=%u cpu=%u%s sample=%u max=%u %s\n", test->name, test->width, test->height,
	       tiling, modes[test->mode], test->device ? "device" : "host", test->skew, run.target.pitch, wrong_copy, wrong_cpu,
	       run.target.mapping == NULL ? "(none)" : "", wrong_sample, largest, wrong_copy + wrong_cpu + wrong_sample == 0U ? "ok" : "FAIL");
	blend_write(blend, test, "copy", run.readback.mapping + test->skew, test->width * BLEND_PIXEL);
	if (run.target.mapping != NULL)
		blend_write(blend, test, "cpu", run.target.mapping, run.target.pitch);
	blend_write(blend, test, "sample", run.readback2.mapping + test->skew, test->width * BLEND_PIXEL);
	blend_run_free(blend, &run);
	return wrong_copy + wrong_cpu + wrong_sample == 0U ? 0 : 1;
}

/* Records a pass over the framebuffer, cleared to the background, drawing its set's image 1:1 with a pipeline. */
static void
blend_draw(
	struct blend *blend,
	VkCommandBuffer command,
	VkFramebuffer framebuffer,
	VkPipeline pipeline,
	VkDescriptorSet set,
	VkBuffer vertices,
	uint32_t width,
	uint32_t height)
{
	VkRenderPassBeginInfo pass;
	VkClearValue clear;
	VkViewport viewport;
	VkRect2D scissor;
	VkDeviceSize offset;
	float push[8];

	memset(&clear, 0, sizeof(clear));
	clear.color.float32[0] = 0.2f;
	clear.color.float32[1] = 0.4f;
	clear.color.float32[2] = 0.6f;
	clear.color.float32[3] = 1.0f;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = blend->pass;
	pass.framebuffer = framebuffer;
	pass.renderArea.extent.width = width;
	pass.renderArea.extent.height = height;
	pass.clearValueCount = 1U;
	pass.pClearValues = &clear;
	vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)width;
	viewport.height = (float)height;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command, 0U, 1U, &viewport);
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = width;
	scissor.extent.height = height;
	vkCmdSetScissor(command, 0U, 1U, &scissor);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, blend->pipeline_layout, 0U, 1U, &set, 0U, NULL);
	offset = 0U;
	vkCmdBindVertexBuffers(command, 0U, 1U, &vertices, &offset);
	push[0] = -1.0f;
	push[1] = -1.0f;
	push[2] = 1.0f;
	push[3] = 1.0f;
	push[4] = 0.0f;
	push[5] = 0.0f;
	push[6] = 1.0f;
	push[7] = 1.0f;
	vkCmdPushConstants(command, blend->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	vkCmdDraw(command, 6U, 1U, 0U, 0U);
	vkCmdEndRenderPass(command);
}

/* The alpha of the column x in the band picture: 0, 64, 128, 191, 255, then alpha-0 stripes every other and every fourth column. */
static uint32_t
blend_band_alpha(
	uint32_t x)
{
	static const uint32_t alphas[5] = { 0U, 64U, 128U, 191U, 255U };
	uint32_t band;

	band = (x / 8U) % 7U;
	if (band < 5U)
		return alphas[band];
	if (band == 5U)
		return (x % 2U) == 0U ? 0U : 255U;
	return (x % 4U) == 0U ? 255U : 0U;
}

/* The picture's texel at (x, y) as B, G, R, A bytes. */
static void
blend_source(
	const struct blend_case *test,
	uint32_t x,
	uint32_t y,
	uint8_t *texel)
{
	uint32_t alpha;

	if (test->picture == PICTURE_COORDINATES) {
		texel[0] = (uint8_t)x;
		texel[1] = (uint8_t)y;
		texel[2] = (uint8_t)((x >> 8) | ((y >> 8) << 4));
		texel[3] = (uint8_t)(x * 3U + y * 5U);
		return;
	}
	alpha = blend_band_alpha(x);
	if (test->mode == BLEND_PREMULTIPLIED) {
		texel[0] = (uint8_t)((BLEND_SOURCE_B * alpha + 127U) / 255U);
		texel[1] = (uint8_t)((BLEND_SOURCE_G * alpha + 127U) / 255U);
		texel[2] = (uint8_t)((BLEND_SOURCE_R * alpha + 127U) / 255U);
	} else {
		texel[0] = (uint8_t)BLEND_SOURCE_B;
		texel[1] = (uint8_t)BLEND_SOURCE_G;
		texel[2] = (uint8_t)BLEND_SOURCE_R;
	}
	texel[3] = (uint8_t)alpha;
}

/* The pixel the target should hold at (x, y), as B, G, R, A bytes. */
static void
blend_expected(
	const struct blend_case *test,
	uint32_t x,
	uint32_t y,
	uint8_t *pixel)
{
	static const uint32_t back[4] = { BLEND_BACK_B, BLEND_BACK_G, BLEND_BACK_R, BLEND_BACK_A };
	uint8_t texel[4];
	uint32_t alpha;
	uint32_t channel;
	double value;

	blend_source(test, x, y, texel);
	if (test->mode == BLEND_NONE) {
		memcpy(pixel, texel, sizeof(texel));
		return;
	}
	alpha = texel[3];
	for (channel = 0U; channel < 4U; channel++) {
		if (test->mode == BLEND_PREMULTIPLIED)
			value = (double)texel[channel] + (double)back[channel] * (255.0 - alpha) / 255.0;
		else
			value = (double)texel[channel] * alpha / 255.0 + (double)back[channel] * (255.0 - alpha) / 255.0;
		if (value > 255.0)
			value = 255.0;
		pixel[channel] = (uint8_t)(value + 0.5);
	}
}

/* Counts the pixels of rows (with a pitch) that differ from the expectation by more than the case allows; says the first. */
static uint32_t
blend_compare(
	const struct blend_case *test,
	const char *what,
	const uint8_t *rows,
	uint32_t pitch,
	uint32_t *largest)
{
	const uint8_t *pixel;
	uint8_t want[4];
	uint32_t allowed;
	uint32_t wrong;
	uint32_t channel;
	uint32_t difference;
	uint32_t worst;
	uint32_t x;
	uint32_t y;

	allowed = test->mode == BLEND_NONE ? 0U : 1U;
	wrong = 0U;
	for (y = 0U; y < test->height; y++) {
		for (x = 0U; x < test->width; x++) {
			pixel = rows + (size_t)y * pitch + (size_t)x * BLEND_PIXEL;
			blend_expected(test, x, y, want);
			worst = 0U;
			for (channel = 0U; channel < 4U; channel++) {
				difference = pixel[channel] > want[channel] ? pixel[channel] - want[channel] : want[channel] - pixel[channel];
				if (difference > worst)
					worst = difference;
			}
			if (worst > *largest)
				*largest = worst;
			if (worst <= allowed)
				continue;
			if (wrong < 4U)
				printf("VKBLEND   %s %s (%u,%u) alpha=%u got BGRA %02x%02x%02x%02x want %02x%02x%02x%02x\n", test->name, what, x, y,
				       test->picture == PICTURE_ALPHA_BANDS ? blend_band_alpha(x) : 255U, pixel[0], pixel[1], pixel[2], pixel[3],
				       want[0], want[1], want[2], want[3]);
			wrong++;
		}
	}
	return wrong;
}

/* Writes rows as a P6 PPM (R, G, B) and the alpha as a P5 PGM when a dump prefix was given. */
static void
blend_write(
	const struct blend *blend,
	const struct blend_case *test,
	const char *what,
	const uint8_t *rows,
	uint32_t pitch)
{
	char path[256];
	FILE *stream;
	const uint8_t *pixel;
	uint32_t x;
	uint32_t y;

	if (blend->dump == NULL)
		return;
	snprintf(path, sizeof(path), "%s-%s-%s.ppm", blend->dump, test->name, what);
	stream = fopen(path, "wb");
	if (stream == NULL)
		return;
	fprintf(stream, "P6\n%u %u\n255\n", test->width, test->height);
	for (y = 0U; y < test->height; y++) {
		for (x = 0U; x < test->width; x++) {
			pixel = rows + (size_t)y * pitch + (size_t)x * BLEND_PIXEL;
			fputc(pixel[2], stream);
			fputc(pixel[1], stream);
			fputc(pixel[0], stream);
		}
	}
	fclose(stream);
}

static void
blend_store_free(
	struct blend *blend,
	struct blend_store *store)
{
	if (store->view != VK_NULL_HANDLE)
		vkDestroyImageView(blend->device, store->view, NULL);
	if (store->image != VK_NULL_HANDLE)
		vkDestroyImage(blend->device, store->image, NULL);
	if (store->buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(blend->device, store->buffer, NULL);
	if (store->memory != VK_NULL_HANDLE)
		vkFreeMemory(blend->device, store->memory, NULL);
	memset(store, 0, sizeof(*store));
}

static void
blend_run_free(
	struct blend *blend,
	struct blend_run *run)
{
	(void)vkDeviceWaitIdle(blend->device);
	if (run->fence != VK_NULL_HANDLE)
		vkDestroyFence(blend->device, run->fence, NULL);
	if (run->command != VK_NULL_HANDLE)
		vkFreeCommandBuffers(blend->device, blend->command_pool, 1U, &run->command);
	if (run->framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(blend->device, run->framebuffer, NULL);
	if (run->copy_framebuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(blend->device, run->copy_framebuffer, NULL);
	if (run->pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(blend->device, run->pool, NULL);
	blend_store_free(blend, &run->target);
	blend_store_free(blend, &run->copy);
	blend_store_free(blend, &run->texture);
	blend_store_free(blend, &run->vertices);
	blend_store_free(blend, &run->staging);
	blend_store_free(blend, &run->readback);
	blend_store_free(blend, &run->readback2);
}

static void
blend_free(
	struct blend *blend)
{
	unsigned mode;

	if (blend->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(blend->device);
		if (blend->command_pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(blend->device, blend->command_pool, NULL);
		for (mode = 0U; mode < BLEND_MODES; mode++)
			if (blend->pipelines[mode] != VK_NULL_HANDLE)
				vkDestroyPipeline(blend->device, blend->pipelines[mode], NULL);
		if (blend->pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(blend->device, blend->pass, NULL);
		if (blend->pipeline_layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(blend->device, blend->pipeline_layout, NULL);
		if (blend->set_layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(blend->device, blend->set_layout, NULL);
		if (blend->fragment_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(blend->device, blend->fragment_module, NULL);
		if (blend->vertex_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(blend->device, blend->vertex_module, NULL);
		if (blend->sampler != VK_NULL_HANDLE)
			vkDestroySampler(blend->device, blend->sampler, NULL);
		vkDestroyDevice(blend->device, NULL);
	}
	if (blend->instance != VK_NULL_HANDLE)
		vkDestroyInstance(blend->instance, NULL);
}
