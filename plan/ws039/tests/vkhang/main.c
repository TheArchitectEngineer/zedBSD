/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkhang: one draw whose pixel program never ends, through the public
 * Vulkan 1.0 API (ws039-p013, the RX 5600 XT): the driver must give up on
 * it, report the device lost to this app only, and keep the other apps and
 * the desktop going.
 *
 * A 64x64 B8G8R8A8 target is cleared and a quad over it is drawn with the
 * compositor's quad vertex program and a pixel program walking 3x + 1 from
 * 0 (never ending) or, with --loops, from 27 (111 steps, the control).
 * The submission is waited for (20 s at most) and one line says the end:
 *
 *   VKHANG lost after=<ms> result=<VkResult>    (the draw was given up)
 *   VKHANG ok after=<ms> pixel=<B8G8R8A8 word>  (the draw ended)
 *   VKHANG fail <step> <VkResult>
 *
 * usage: vkhang [--loops] [--device-index=N]
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#include "userland/desktop/wayland/shaders.h"

/* The pixel programs, from vkhang/hang.frag and vkhang/loops.frag (made by vkhang-build.sh). */
#include "vkhang-programs.h"

#define HANG_TARGET		64U
#define HANG_PIXEL		4U
#define HANG_WAIT_NS		UINT64_C(20000000000)

/* The objects of the run, freed together at its end. */
struct hang {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	VkDeviceMemory memory;
	uint8_t *mapping;
	VkImage target;
	VkImageView view;
	VkBuffer vertices;
	VkBuffer readback;
	VkShaderModule vertex_module;
	VkShaderModule fragment_module;
	VkPipelineLayout layout;
	VkRenderPass pass;
	VkFramebuffer framebuffer;
	VkPipeline pipeline;
	VkCommandPool pool;
	VkCommandBuffer command;
	VkFence fence;
};

/* Where each resource lies in the one allocation. */
#define HANG_TARGET_AT		0x00000U
#define HANG_VERTICES_AT	0x10000U
#define HANG_READBACK_AT	0x11000U
#define HANG_MEMORY_BYTES	0x20000U

static VkResult hang_device(struct hang *hang, int index);
static VkResult hang_resources(struct hang *hang);
static VkResult hang_pipeline(struct hang *hang, bool loops);
static VkResult hang_record(struct hang *hang);
static uint64_t hang_now_ms(void);
static void hang_free(struct hang *hang);

int
main(
	int argc,
	char **argv)
{
	static const float corners[12] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
	struct hang hang;
	VkFenceCreateInfo fence;
	VkSubmitInfo submit;
	VkResult status;
	uint64_t started;
	uint32_t pixel;
	bool loops;
	int index;
	int argument;

	/* The arguments. */
	loops = false;
	index = 0;
	for (argument = 1; argument < argc; argument++) {
		if (strcmp(argv[argument], "--loops") == 0) {
			loops = true;
		} else if (strncmp(argv[argument], "--device-index=", 15) == 0) {
			index = atoi(argv[argument] + 15);
		} else {
			fprintf(stderr, "usage: vkhang [--loops] [--device-index=N]\n");
			return 2;
		}
	}

	/* The device, the resources, the pipeline and the commands. */
	memset(&hang, 0, sizeof(hang));
	status = hang_device(&hang, index);
	if (status == VK_SUCCESS)
		status = hang_resources(&hang);
	if (status == VK_SUCCESS)
		status = hang_pipeline(&hang, loops);
	if (status == VK_SUCCESS) {
		memcpy(hang.mapping + HANG_VERTICES_AT, corners, sizeof(corners));
		status = hang_record(&hang);
	}
	if (status != VK_SUCCESS) {
		hang_free(&hang);
		return 1;
	}

	/* The submission, timed from here, and the wait. */
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	status = vkCreateFence(hang.device, &fence, NULL, &hang.fence);
	if (status != VK_SUCCESS) {
		printf("VKHANG fail fence %d\n", (int)status);
		hang_free(&hang);
		return 1;
	}
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &hang.command;
	started = hang_now_ms();
	status = vkQueueSubmit(hang.queue, 1U, &submit, hang.fence);
	if (status == VK_SUCCESS)
		status = vkWaitForFences(hang.device, 1U, &hang.fence, VK_TRUE, HANG_WAIT_NS);

	/* The end: lost (the driver gave up), or ended with the pixel it drew. */
	if (status == VK_ERROR_DEVICE_LOST) {
		printf("VKHANG lost after=%llu result=%d\n", (unsigned long long)(hang_now_ms() - started), (int)status);
		hang_free(&hang);
		return 0;
	}
	if (status != VK_SUCCESS) {
		printf("VKHANG fail wait %d after=%llu\n", (int)status, (unsigned long long)(hang_now_ms() - started));
		hang_free(&hang);
		return 1;
	}
	memcpy(&pixel, hang.mapping + HANG_READBACK_AT, sizeof(pixel));
	printf("VKHANG ok after=%llu pixel=%08x\n", (unsigned long long)(hang_now_ms() - started), (unsigned)pixel);
	hang_free(&hang);
	return 0;
}

/* Creates the instance and the device with its queue. */
static VkResult
hang_device(
	struct hang *hang,
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
	application.pApplicationName = "vkhang";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	status = vkCreateInstance(&instance, NULL, &hang->instance);
	if (status != VK_SUCCESS) {
		printf("VKHANG fail instance %d\n", (int)status);
		return status;
	}

	/* The device asked for. */
	count = 8U;
	status = vkEnumeratePhysicalDevices(hang->instance, &count, physicals);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || index < 0 || (uint32_t)index >= count) {
		printf("VKHANG fail devices %d count=%u\n", (int)status, count);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	hang->physical = physicals[index];

	/* The device with one queue of family 0. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1U;
	device.pQueueCreateInfos = &queue;
	status = vkCreateDevice(hang->physical, &device, NULL, &hang->device);
	if (status != VK_SUCCESS) {
		printf("VKHANG fail device %d\n", (int)status);
		return status;
	}
	vkGetDeviceQueue(hang->device, 0U, 0U, &hang->queue);
	return VK_SUCCESS;
}

/* The one host-visible allocation with the target, the corners and the readback bound in it. */
static VkResult
hang_resources(
	struct hang *hang)
{
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryAllocateInfo allocate;
	VkImageCreateInfo image;
	VkImageViewCreateInfo view;
	VkBufferCreateInfo buffer;
	void *mapping;
	uint32_t type;
	VkResult status;

	/* A host-visible coherent type, the allocation, mapped. */
	vkGetPhysicalDeviceMemoryProperties(hang->physical, &memory);
	for (type = 0U; type < memory.memoryTypeCount; type++) {
		if ((memory.memoryTypes[type].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
		    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			break;
	}
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = HANG_MEMORY_BYTES;
	allocate.memoryTypeIndex = type;
	status = vkAllocateMemory(hang->device, &allocate, NULL, &hang->memory);
	if (status == VK_SUCCESS)
		status = vkMapMemory(hang->device, hang->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status != VK_SUCCESS) {
		printf("VKHANG fail memory %d\n", (int)status);
		return status;
	}
	hang->mapping = mapping;

	/* The target and its view. */
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = VK_FORMAT_B8G8R8A8_UNORM;
	image.extent.width = HANG_TARGET;
	image.extent.height = HANG_TARGET;
	image.extent.depth = 1U;
	image.mipLevels = 1U;
	image.arrayLayers = 1U;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	status = vkCreateImage(hang->device, &image, NULL, &hang->target);
	if (status == VK_SUCCESS)
		status = vkBindImageMemory(hang->device, hang->target, hang->memory, HANG_TARGET_AT);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = hang->target;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateImageView(hang->device, &view, NULL, &hang->view);

	/* The corners and the readback. */
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = 48U;
	buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(hang->device, &buffer, NULL, &hang->vertices);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(hang->device, hang->vertices, hang->memory, HANG_VERTICES_AT);
	buffer.size = HANG_TARGET * HANG_TARGET * HANG_PIXEL;
	buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(hang->device, &buffer, NULL, &hang->readback);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(hang->device, hang->readback, hang->memory, HANG_READBACK_AT);
	if (status != VK_SUCCESS)
		printf("VKHANG fail resources %d\n", (int)status);
	return status;
}

/* The pass (cleared, kept), the framebuffer and the pipeline of the quad with the chosen pixel program. */
static VkResult
hang_pipeline(
	struct hang *hang,
	bool loops)
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
	status = vkCreateShaderModule(hang->device, &module, NULL, &hang->vertex_module);
	module.codeSize = loops ? sizeof(vkhang_loops_frag) : sizeof(vkhang_hang_frag);
	module.pCode = loops ? vkhang_loops_frag : vkhang_hang_frag;
	if (status == VK_SUCCESS)
		status = vkCreateShaderModule(hang->device, &module, NULL, &hang->fragment_module);

	/* The layout: the vertex program's rectangle and texture corners. */
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = 32U;
	memset(&layout, 0, sizeof(layout));
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.pushConstantRangeCount = 1U;
	layout.pPushConstantRanges = &range;
	if (status == VK_SUCCESS)
		status = vkCreatePipelineLayout(hang->device, &layout, NULL, &hang->layout);

	/* The pass and the framebuffer. */
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
	subpass.colorAttachmentCount = 1U;
	subpass.pColorAttachments = &reference;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass.attachmentCount = 1U;
	pass.pAttachments = &attachment;
	pass.subpassCount = 1U;
	pass.pSubpasses = &subpass;
	if (status == VK_SUCCESS)
		status = vkCreateRenderPass(hang->device, &pass, NULL, &hang->pass);
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = hang->pass;
	framebuffer.attachmentCount = 1U;
	framebuffer.pAttachments = &hang->view;
	framebuffer.width = HANG_TARGET;
	framebuffer.height = HANG_TARGET;
	framebuffer.layers = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateFramebuffer(hang->device, &framebuffer, NULL, &hang->framebuffer);

	/* The pipeline: two triangles from the corners, a fixed viewport, no blend. */
	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = hang->vertex_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = hang->fragment_module;
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
	viewport_value.width = (float)HANG_TARGET;
	viewport_value.height = (float)HANG_TARGET;
	viewport_value.maxDepth = 1.0f;
	memset(&scissor_value, 0, sizeof(scissor_value));
	scissor_value.extent.width = HANG_TARGET;
	scissor_value.extent.height = HANG_TARGET;
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
	create.layout = hang->layout;
	create.renderPass = hang->pass;
	create.basePipelineIndex = -1;
	if (status == VK_SUCCESS)
		status = vkCreateGraphicsPipelines(hang->device, VK_NULL_HANDLE, 1U, &create, NULL, &hang->pipeline);
	if (status != VK_SUCCESS)
		printf("VKHANG fail pipeline %d\n", (int)status);
	return status;
}

/* Records the pass drawing the quad over the whole target, then the target read back. */
static VkResult
hang_record(
	struct hang *hang)
{
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo allocate;
	VkCommandBufferBeginInfo begin;
	VkRenderPassBeginInfo pass;
	VkClearValue clear;
	VkBufferImageCopy copy;
	VkDeviceSize offset;
	float push[8];
	VkResult status;

	/* The pool and the command buffer. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	status = vkCreateCommandPool(hang->device, &pool, NULL, &hang->pool);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = hang->pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	if (status == VK_SUCCESS)
		status = vkAllocateCommandBuffers(hang->device, &allocate, &hang->command);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (status == VK_SUCCESS)
		status = vkBeginCommandBuffer(hang->command, &begin);
	if (status != VK_SUCCESS) {
		printf("VKHANG fail command %d\n", (int)status);
		return status;
	}

	/* The pass, the quad over the whole target. */
	memset(&clear, 0, sizeof(clear));
	clear.color.float32[3] = 1.0f;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = hang->pass;
	pass.framebuffer = hang->framebuffer;
	pass.renderArea.extent.width = HANG_TARGET;
	pass.renderArea.extent.height = HANG_TARGET;
	pass.clearValueCount = 1U;
	pass.pClearValues = &clear;
	vkCmdBeginRenderPass(hang->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(hang->command, VK_PIPELINE_BIND_POINT_GRAPHICS, hang->pipeline);
	offset = 0U;
	vkCmdBindVertexBuffers(hang->command, 0U, 1U, &hang->vertices, &offset);
	push[0] = -1.0f;
	push[1] = -1.0f;
	push[2] = 1.0f;
	push[3] = 1.0f;
	push[4] = 0.0f;
	push[5] = 0.0f;
	push[6] = 1.0f;
	push[7] = 1.0f;
	vkCmdPushConstants(hang->command, hang->layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	vkCmdDraw(hang->command, 6U, 1U, 0U, 0U);
	vkCmdEndRenderPass(hang->command);

	/* The target read back. */
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = HANG_TARGET;
	copy.imageExtent.height = HANG_TARGET;
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(hang->command, hang->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, hang->readback, 1U, &copy);
	status = vkEndCommandBuffer(hang->command);
	if (status != VK_SUCCESS)
		printf("VKHANG fail end %d\n", (int)status);
	return status;
}

/* The monotonic clock in milliseconds. */
static uint64_t
hang_now_ms(
	void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Destroys what was made (after a lost device the destroys only free the app's records). */
static void
hang_free(
	struct hang *hang)
{
	if (hang->device != VK_NULL_HANDLE) {
		(void)vkDeviceWaitIdle(hang->device);
		if (hang->fence != VK_NULL_HANDLE)
			vkDestroyFence(hang->device, hang->fence, NULL);
		if (hang->pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(hang->device, hang->pool, NULL);
		if (hang->pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(hang->device, hang->pipeline, NULL);
		if (hang->framebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(hang->device, hang->framebuffer, NULL);
		if (hang->pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(hang->device, hang->pass, NULL);
		if (hang->layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(hang->device, hang->layout, NULL);
		if (hang->fragment_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(hang->device, hang->fragment_module, NULL);
		if (hang->vertex_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(hang->device, hang->vertex_module, NULL);
		if (hang->readback != VK_NULL_HANDLE)
			vkDestroyBuffer(hang->device, hang->readback, NULL);
		if (hang->vertices != VK_NULL_HANDLE)
			vkDestroyBuffer(hang->device, hang->vertices, NULL);
		if (hang->view != VK_NULL_HANDLE)
			vkDestroyImageView(hang->device, hang->view, NULL);
		if (hang->target != VK_NULL_HANDLE)
			vkDestroyImage(hang->device, hang->target, NULL);
		if (hang->memory != VK_NULL_HANDLE)
			vkFreeMemory(hang->device, hang->memory, NULL);
		vkDestroyDevice(hang->device, NULL);
	}
	if (hang->instance != VK_NULL_HANDLE)
		vkDestroyInstance(hang->instance, NULL);
}
