/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkload: a long, steady load on the graphics engine through the public
 * Vulkan 1.0 API (ws039-p019, the RX 5600 XT), to see whether the
 * firmware raises the graphics clock under a load that lasts, and how long
 * one draw takes at the clock it gets.
 *
 * A square B8G8R8A8 target (--side, 1024 by default) is cleared and a quad
 * over it is drawn with the compositor's quad vertex program and a pixel
 * program that runs a loop of 16384 steps (vkload/spin.frag).  The command
 * buffer is recorded anew, submitted and waited for again and again for
 * --seconds (10 by default), with --gap milliseconds of sleep between
 * draws (0 by default: a steady load; 20 is a desktop's short bursts).
 * One line a second, then one line at the end, with the time from the
 * submission to the fence of each draw:
 *
 *   VKLOAD second=<s> draws=<n> mean=<us> min=<us> max=<us>
 *   VKLOAD ok seconds=<s> side=<n> gap=<ms> draws=<n> mean=<us> first=<us> last=<us>
 *   VKLOAD fail <step> <VkResult>
 *
 * first and last are the mean of the first and the last second.
 *
 * usage: vkload [--seconds=N] [--side=N] [--gap=MS] [--device-index=N]
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#include "userland/desktop/wayland/shaders.h"

/* The pixel program, from vkload/spin.frag (made by vkload-build.sh). */
#include "vkload-programs.h"

/* The defaults and limits of the arguments, and the fence's wait. */
#define LOAD_SECONDS		10U
#define LOAD_SECONDS_MAX	600U
#define LOAD_SIDE		1024U
#define LOAD_SIDE_MIN		16U
#define LOAD_SIDE_MAX		2048U
#define LOAD_GAP_MAX		1000U
#define LOAD_WAIT_NS		UINT64_C(20000000000)

/* The vertex buffer: the quad's six corners, two floats each. */
#define LOAD_CORNERS_BYTES	48U

/* The objects of the run, freed together at its end. */
struct load {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	VkDeviceMemory memory;
	uint8_t *mapping;
	VkImage target;
	VkImageView view;
	VkBuffer vertices;
	VkShaderModule vertex_module;
	VkShaderModule fragment_module;
	VkPipelineLayout layout;
	VkRenderPass pass;
	VkFramebuffer framebuffer;
	VkPipeline pipeline;
	VkCommandPool pool;
	VkCommandBuffer command;
	VkFence fence;
	uint32_t side;
};

/* What one second (or the whole run) of draws took, in microseconds. */
struct load_tally {
	uint64_t draws;
	uint64_t total_us;
	uint64_t min_us;
	uint64_t max_us;
};

static int load_parse(int argc, char **argv, unsigned *seconds, unsigned *side, unsigned *gap, int *index);
static VkResult load_device(struct load *load, int index);
static VkResult load_resources(struct load *load);
static VkResult load_pipeline(struct load *load);
static VkResult load_commands(struct load *load);
static VkResult load_record(struct load *load);
static VkResult load_draw(struct load *load, uint64_t *took_us);
static void load_count(struct load_tally *tally, uint64_t took_us);
static uint64_t load_mean(const struct load_tally *tally);
static void load_sleep_ms(unsigned milliseconds);
static uint64_t load_now_us(void);
static void load_free(struct load *load);

int
main(
	int argc,
	char **argv)
{
	static const float corners[12] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	struct load load;
	struct load_tally second;
	struct load_tally whole;
	uint64_t started_us;
	uint64_t second_end_us;
	uint64_t end_us;
	uint64_t took_us;
	uint64_t first_mean;
	uint64_t last_mean;
	unsigned seconds;
	unsigned side;
	unsigned gap;
	unsigned counted;
	VkResult status;
	int index;

	/* The arguments. */
	if (load_parse(argc, argv, &seconds, &side, &gap, &index) != 0) {
		fprintf(stderr, "usage: vkload [--seconds=N] [--side=N] [--gap=MS] [--device-index=N]\n");
		return 2;
	}

	/* The device, the resources, the pipeline and the command buffer. */
	memset(&load, 0, sizeof(load));
	load.side = side;
	status = load_device(&load, index);
	if (status == VK_SUCCESS)
		status = load_resources(&load);
	if (status == VK_SUCCESS) {
		memcpy(load.mapping, corners, sizeof(corners));
		status = load_pipeline(&load);
	}
	if (status == VK_SUCCESS)
		status = load_commands(&load);
	if (status != VK_SUCCESS) {
		load_free(&load);
		return 1;
	}

	/* Draws until the time is up, one line a second. */
	memset(&second, 0, sizeof(second));
	memset(&whole, 0, sizeof(whole));
	first_mean = 0U;
	last_mean = 0U;
	counted = 0U;
	started_us = load_now_us();
	second_end_us = started_us + 1000000U;
	end_us = started_us + (uint64_t)seconds * 1000000U;
	for (;;) {
		/* One draw, timed from its submission to its fence. */
		status = load_draw(&load, &took_us);
		if (status != VK_SUCCESS) {
			load_free(&load);
			return 1;
		}
		load_count(&second, took_us);
		load_count(&whole, took_us);

		/* A second is over: its line, and the first and the last second's mean kept. */
		if (load_now_us() >= second_end_us) {
			counted++;
			printf("VKLOAD second=%u draws=%llu mean=%llu min=%llu max=%llu\n",
			    counted,
			    (unsigned long long)second.draws,
			    (unsigned long long)load_mean(&second),
			    (unsigned long long)second.min_us,
			    (unsigned long long)second.max_us);
			fflush(stdout);
			if (counted == 1U)
				first_mean = load_mean(&second);
			last_mean = load_mean(&second);
			memset(&second, 0, sizeof(second));
			second_end_us += 1000000U;
		}

		/* Ends when the time is up, else sleeps the gap. */
		if (load_now_us() >= end_us)
			break;
		if (gap != 0U)
			load_sleep_ms(gap);
	}

	/* The whole run. */
	printf("VKLOAD ok seconds=%u side=%u gap=%u draws=%llu mean=%llu first=%llu last=%llu\n",
	    seconds,
	    side,
	    gap,
	    (unsigned long long)whole.draws,
	    (unsigned long long)load_mean(&whole),
	    (unsigned long long)first_mean,
	    (unsigned long long)last_mean);
	load_free(&load);
	return 0;
}

/* Reads the arguments into their values, the defaults where none is given; returns -1 for a bad one. */
static int
load_parse(
	int argc,
	char **argv,
	unsigned *seconds,
	unsigned *side,
	unsigned *gap,
	int *index)
{
	int argument;
	long value;

	/* The defaults. */
	*seconds = LOAD_SECONDS;
	*side = LOAD_SIDE;
	*gap = 0U;
	*index = 0;

	/* Each argument. */
	for (argument = 1; argument < argc; argument++) {
		if (strncmp(argv[argument], "--seconds=", 10) == 0) {
			value = atol(argv[argument] + 10);
			if (value < 1 || value > (long)LOAD_SECONDS_MAX)
				return -1;
			*seconds = (unsigned)value;
		} else if (strncmp(argv[argument], "--side=", 7) == 0) {
			value = atol(argv[argument] + 7);
			if (value < (long)LOAD_SIDE_MIN || value > (long)LOAD_SIDE_MAX)
				return -1;
			*side = (unsigned)value;
		} else if (strncmp(argv[argument], "--gap=", 6) == 0) {
			value = atol(argv[argument] + 6);
			if (value < 0 || value > (long)LOAD_GAP_MAX)
				return -1;
			*gap = (unsigned)value;
		} else if (strncmp(argv[argument], "--device-index=", 15) == 0) {
			*index = atoi(argv[argument] + 15);
		} else {
			return -1;
		}
	}

	/* Succeeded: every argument was understood. */
	return 0;
}

/* Creates the instance and the device with one queue of family 0. */
static VkResult
load_device(
	struct load *load,
	int index)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkPhysicalDevice physicals[8];
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	float priority;
	uint32_t count;
	VkResult status;

	/* The instance. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkload";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	status = vkCreateInstance(&instance, NULL, &load->instance);
	if (status != VK_SUCCESS) {
		printf("VKLOAD fail instance %d\n", (int)status);
		return status;
	}

	/* The device asked for. */
	count = 8U;
	status = vkEnumeratePhysicalDevices(load->instance, &count, physicals);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || index < 0 || (uint32_t)index >= count) {
		printf("VKLOAD fail devices %d count=%u\n", (int)status, count);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	load->physical = physicals[index];

	/* The device with one queue of family 0. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = 0U;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1U;
	device.pQueueCreateInfos = &queue;
	status = vkCreateDevice(load->physical, &device, NULL, &load->device);
	if (status != VK_SUCCESS) {
		printf("VKLOAD fail device %d\n", (int)status);
		return status;
	}
	vkGetDeviceQueue(load->device, 0U, 0U, &load->queue);
	return VK_SUCCESS;
}

/*
 * The one host-visible allocation: the corners at its start, the target
 * after them on the target's alignment.
 */
static VkResult
load_resources(
	struct load *load)
{
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryRequirements needs;
	VkMemoryAllocateInfo allocate;
	VkImageCreateInfo image;
	VkImageViewCreateInfo view;
	VkBufferCreateInfo buffer;
	VkDeviceSize target_at;
	void *mapping;
	uint32_t type;
	VkResult status;

	/* The target and the corners' buffer, not bound yet. */
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = VK_FORMAT_B8G8R8A8_UNORM;
	image.extent.width = load->side;
	image.extent.height = load->side;
	image.extent.depth = 1U;
	image.mipLevels = 1U;
	image.arrayLayers = 1U;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	status = vkCreateImage(load->device, &image, NULL, &load->target);
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = LOAD_CORNERS_BYTES;
	buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(load->device, &buffer, NULL, &load->vertices);
	if (status != VK_SUCCESS) {
		printf("VKLOAD fail resources %d\n", (int)status);
		return status;
	}

	/* The target's size and alignment; it starts after the corners on its alignment. */
	vkGetImageMemoryRequirements(load->device, load->target, &needs);
	target_at = needs.alignment;
	if (target_at < LOAD_CORNERS_BYTES)
		target_at = (LOAD_CORNERS_BYTES + needs.alignment - 1U) / needs.alignment * needs.alignment;

	/* A host-visible coherent type, the allocation, mapped. */
	vkGetPhysicalDeviceMemoryProperties(load->physical, &memory);
	for (type = 0U; type < memory.memoryTypeCount; type++) {
		if ((memory.memoryTypes[type].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
		    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
		    (needs.memoryTypeBits & (1U << type)) != 0U)
			break;
	}
	if (type >= memory.memoryTypeCount) {
		printf("VKLOAD fail memory-type %d\n", (int)VK_ERROR_FEATURE_NOT_PRESENT);
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = target_at + needs.size;
	allocate.memoryTypeIndex = type;
	status = vkAllocateMemory(load->device, &allocate, NULL, &load->memory);
	if (status == VK_SUCCESS)
		status = vkMapMemory(load->device, load->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status != VK_SUCCESS) {
		printf("VKLOAD fail memory %d\n", (int)status);
		return status;
	}
	load->mapping = mapping;

	/* Both bound, and the target's view. */
	status = vkBindBufferMemory(load->device, load->vertices, load->memory, 0U);
	if (status == VK_SUCCESS)
		status = vkBindImageMemory(load->device, load->target, load->memory, target_at);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = load->target;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateImageView(load->device, &view, NULL, &load->view);
	if (status != VK_SUCCESS)
		printf("VKLOAD fail bind %d\n", (int)status);
	return status;
}

/* The pass (cleared, kept), the framebuffer and the pipeline of the quad with the long pixel program. */
static VkResult
load_pipeline(
	struct load *load)
{
	VkShaderModuleCreateInfo module;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo layout;
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass;
	VkFramebufferCreateInfo framebuffer;
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attribute;
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkViewport viewport_value;
	VkRect2D scissor_value;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkGraphicsPipelineCreateInfo create;
	VkResult status;

	/* The two modules. */
	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = sizeof(kwl_quad_vert);
	module.pCode = kwl_quad_vert;
	status = vkCreateShaderModule(load->device, &module, NULL, &load->vertex_module);
	module.codeSize = sizeof(vkload_spin_frag);
	module.pCode = vkload_spin_frag;
	if (status == VK_SUCCESS)
		status = vkCreateShaderModule(load->device, &module, NULL, &load->fragment_module);

	/* The layout: the vertex program's rectangle and texture corners. */
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = 32U;
	memset(&layout, 0, sizeof(layout));
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.pushConstantRangeCount = 1U;
	layout.pPushConstantRanges = &range;
	if (status == VK_SUCCESS)
		status = vkCreatePipelineLayout(load->device, &layout, NULL, &load->layout);

	/* The pass and the framebuffer. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
	if (status == VK_SUCCESS)
		status = vkCreateRenderPass(load->device, &pass, NULL, &load->pass);
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = load->pass;
	framebuffer.attachmentCount = 1U;
	framebuffer.pAttachments = &load->view;
	framebuffer.width = load->side;
	framebuffer.height = load->side;
	framebuffer.layers = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateFramebuffer(load->device, &framebuffer, NULL, &load->framebuffer);

	/* The pipeline: two triangles from the corners, a fixed viewport, no blend. */
	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = load->vertex_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = load->fragment_module;
	stages[1].pName = "main";
	memset(&binding, 0, sizeof(binding));
	binding.stride = 8U;
	memset(&attribute, 0, sizeof(attribute));
	attribute.format = VK_FORMAT_R32G32_SFLOAT;
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 1U;
	input.pVertexBindingDescriptions = &binding;
	input.vertexAttributeDescriptionCount = 1U;
	input.pVertexAttributeDescriptions = &attribute;
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	memset(&viewport_value, 0, sizeof(viewport_value));
	viewport_value.width = (float)load->side;
	viewport_value.height = (float)load->side;
	viewport_value.maxDepth = 1.0f;
	memset(&scissor_value, 0, sizeof(scissor_value));
	scissor_value.extent.width = load->side;
	scissor_value.extent.height = load->side;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1U;
	viewport.pViewports = &viewport_value;
	viewport.scissorCount = 1U;
	viewport.pScissors = &scissor_value;
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
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1U;
	blend.pAttachments = &blend_attachment;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create.stageCount = 2U;
	create.pStages = stages;
	create.pVertexInputState = &input;
	create.pInputAssemblyState = &assembly;
	create.pViewportState = &viewport;
	create.pRasterizationState = &raster;
	create.pMultisampleState = &multisample;
	create.pColorBlendState = &blend;
	create.layout = load->layout;
	create.renderPass = load->pass;
	create.basePipelineIndex = -1;
	if (status == VK_SUCCESS)
		status = vkCreateGraphicsPipelines(load->device, VK_NULL_HANDLE, 1U, &create, NULL, &load->pipeline);
	if (status != VK_SUCCESS)
		printf("VKLOAD fail pipeline %d\n", (int)status);
	return status;
}

/* The pool, the command buffer and the fence the draws reuse. */
static VkResult
load_commands(
	struct load *load)
{
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo allocate;
	VkFenceCreateInfo fence;
	VkResult status;

	/* The pool, its one command buffer, and the fence. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	status = vkCreateCommandPool(load->device, &pool, NULL, &load->pool);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = load->pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	if (status == VK_SUCCESS)
		status = vkAllocateCommandBuffers(load->device, &allocate, &load->command);
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (status == VK_SUCCESS)
		status = vkCreateFence(load->device, &fence, NULL, &load->fence);
	if (status != VK_SUCCESS)
		printf("VKLOAD fail command %d\n", (int)status);
	return status;
}

/* Records the pass drawing the quad over the whole target, once for one submission. */
static VkResult
load_record(
	struct load *load)
{
	VkCommandBufferBeginInfo begin;
	VkRenderPassBeginInfo pass;
	VkClearValue clear;
	VkDeviceSize offset;
	float push[8];
	VkResult status;

	/* The pool reset, the buffer begun for one submission. */
	status = vkResetCommandPool(load->device, load->pool, 0U);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (status == VK_SUCCESS)
		status = vkBeginCommandBuffer(load->command, &begin);
	if (status != VK_SUCCESS) {
		printf("VKLOAD fail record %d\n", (int)status);
		return status;
	}

	/* The pass, the quad over the whole target. */
	memset(&clear, 0, sizeof(clear));
	clear.color.float32[3] = 1.0f;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = load->pass;
	pass.framebuffer = load->framebuffer;
	pass.renderArea.extent.width = load->side;
	pass.renderArea.extent.height = load->side;
	pass.clearValueCount = 1U;
	pass.pClearValues = &clear;
	vkCmdBeginRenderPass(load->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(load->command, VK_PIPELINE_BIND_POINT_GRAPHICS, load->pipeline);
	offset = 0U;
	vkCmdBindVertexBuffers(load->command, 0U, 1U, &load->vertices, &offset);
	push[0] = -1.0f;
	push[1] = -1.0f;
	push[2] = 1.0f;
	push[3] = 1.0f;
	push[4] = 0.0f;
	push[5] = 0.0f;
	push[6] = 1.0f;
	push[7] = 1.0f;
	vkCmdPushConstants(load->command, load->layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	vkCmdDraw(load->command, 6U, 1U, 0U, 0U);
	vkCmdEndRenderPass(load->command);
	status = vkEndCommandBuffer(load->command);
	if (status != VK_SUCCESS)
		printf("VKLOAD fail end %d\n", (int)status);
	return status;
}

/* Records, submits and waits for one draw; took_us is the time from the submission to the fence. */
static VkResult
load_draw(
	struct load *load,
	uint64_t *took_us)
{
	VkSubmitInfo submit;
	uint64_t started_us;
	VkResult status;

	/* The commands of this draw. */
	status = load_record(load);
	if (status != VK_SUCCESS)
		return status;

	/* Submitted, waited for, timed. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &load->command;
	started_us = load_now_us();
	status = vkQueueSubmit(load->queue, 1U, &submit, load->fence);
	if (status == VK_SUCCESS)
		status = vkWaitForFences(load->device, 1U, &load->fence, VK_TRUE, LOAD_WAIT_NS);
	*took_us = load_now_us() - started_us;
	if (status == VK_SUCCESS)
		status = vkResetFences(load->device, 1U, &load->fence);
	if (status != VK_SUCCESS)
		printf("VKLOAD fail draw %d after=%llu\n", (int)status, (unsigned long long)*took_us);
	return status;
}

/* Counts one draw's time in a tally. */
static void
load_count(
	struct load_tally *tally,
	uint64_t took_us)
{
	if (tally->draws == 0U || took_us < tally->min_us)
		tally->min_us = took_us;
	if (took_us > tally->max_us)
		tally->max_us = took_us;
	tally->draws++;
	tally->total_us += took_us;
}

/* The mean time of a tally's draws, 0 without draws. */
static uint64_t
load_mean(
	const struct load_tally *tally)
{
	if (tally->draws == 0U)
		return 0U;
	return tally->total_us / tally->draws;
}

/* Sleeps milliseconds. */
static void
load_sleep_ms(
	unsigned milliseconds)
{
	struct timespec wait;

	wait.tv_sec = (time_t)(milliseconds / 1000U);
	wait.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	(void)nanosleep(&wait, NULL);
}

/* The monotonic clock in microseconds. */
static uint64_t
load_now_us(
	void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Destroys what was made. */
static void
load_free(
	struct load *load)
{
	if (load->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(load->device);
		if (load->fence != VK_NULL_HANDLE)
			vkDestroyFence(load->device, load->fence, NULL);
		if (load->pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(load->device, load->pool, NULL);
		if (load->pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(load->device, load->pipeline, NULL);
		if (load->framebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(load->device, load->framebuffer, NULL);
		if (load->pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(load->device, load->pass, NULL);
		if (load->layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(load->device, load->layout, NULL);
		if (load->fragment_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(load->device, load->fragment_module, NULL);
		if (load->vertex_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(load->device, load->vertex_module, NULL);
		if (load->vertices != VK_NULL_HANDLE)
			vkDestroyBuffer(load->device, load->vertices, NULL);
		if (load->view != VK_NULL_HANDLE)
			vkDestroyImageView(load->device, load->view, NULL);
		if (load->target != VK_NULL_HANDLE)
			vkDestroyImage(load->device, load->target, NULL);
		if (load->memory != VK_NULL_HANDLE)
			vkFreeMemory(load->device, load->memory, NULL);
		vkDestroyDevice(load->device, NULL);
	}
	if (load->instance != VK_NULL_HANDLE)
		vkDestroyInstance(load->instance, NULL);
}
