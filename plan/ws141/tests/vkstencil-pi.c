/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * vkstencil-pi: the Raspberry Pi's depth/stencil attachment (ws141-p017),
 * drawn offscreen through the public Vulkan 1.0 API with Notes' shaders and
 * Notes' stencil, fringe and cover pipelines, and read back.
 *
 * Three 64x64 B8G8R8A8 targets share one 64x64 D24S8 attachment:
 *
 *   winding  rectangle A (8..40) counts up and rectangle B (24..56), drawn
 *            the other way round, counts down, with no colour written; the
 *            fringe paints green where the stencil is zero (outside both and
 *            where they overlap), the cover red where it is not and zeroes
 *            it, and a second cover in black then finds nothing to paint;
 *   stored   the same count stored with the attachment, then loaded by a
 *            second pass that loads the colour and paints the cover;
 *   depth    a less-than depth test that writes: green at depth 0.5, red
 *            at 0.75 (behind, not drawn), white over the middle at 0.25.
 *
 * Every layout change is a render pass's own, and the copies read the
 * targets in GENERAL.  One line says the result:
 *
 *   VKSTENCIL ok device=<name> pixels=12288
 *   VKSTENCIL fail <step> <VkResult or what differs>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "userland/desktop/notes/shaders.h"

/* The targets' side, a pixel's bytes and the fence's wait. */
#define STENCIL_SIDE		64U
#define STENCIL_PIXEL		4U
#define STENCIL_WAIT_NS		UINT64_C(10000000000)

/* A vertex: the corner (x, y in pixels, then the fringe's distance and an unused value) and the colour. */
#define STENCIL_VERTEX_FLOATS	8U
#define STENCIL_QUAD_VERTICES	6U

/* The colours as a B8G8R8A8 pixel read as a little-endian word. */
#define STENCIL_BLUE		0xff0000ffU
#define STENCIL_RED		0xffff0000U
#define STENCIL_GREEN		0xff00ff00U
#define STENCIL_WHITE		0xffffffffU

/* Where each resource lies in the one allocation. */
#define STENCIL_TARGET_AT	0x00000U
#define STENCIL_DEPTH_AT	0x0c000U
#define STENCIL_VERTICES_AT	0x10000U
#define STENCIL_READBACK_AT	0x14000U
#define STENCIL_SLOT		0x04000U
#define STENCIL_MEMORY_BYTES	0x20000U

/* The three targets and their passes. */
#define STENCIL_TARGETS		3U

/*
 * The quads of the vertex buffer, in order.  B's corners run the other way
 * round from A's, so its triangles face the other way.
 */
enum stencil_quad {
	STENCIL_QUAD_A = 0,
	STENCIL_QUAD_B,
	STENCIL_QUAD_GREEN,
	STENCIL_QUAD_RED,
	STENCIL_QUAD_BLACK,
	STENCIL_QUAD_MIDDLE,
	STENCIL_QUADS
};

/* The pipelines: Notes' three stencil roles and a depth test. */
enum stencil_pipe {
	STENCIL_PIPE_COUNT = 0,
	STENCIL_PIPE_FRINGE,
	STENCIL_PIPE_COVER,
	STENCIL_PIPE_DEPTH,
	STENCIL_PIPES
};

/* The objects of the run, freed together at its end. */
struct stencil {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	VkDeviceMemory memory;
	uint8_t *mapping;
	VkImage targets[STENCIL_TARGETS];
	VkImageView target_views[STENCIL_TARGETS];
	VkImage depth;
	VkImageView depth_view;
	VkBuffer vertices;
	VkBuffer readbacks[STENCIL_TARGETS];
	VkShaderModule vertex_module;
	VkShaderModule fragment_module;
	VkPipelineLayout pipeline_layout;
	VkRenderPass clear_pass;
	VkRenderPass store_pass;
	VkRenderPass load_pass;
	VkFramebuffer framebuffers[STENCIL_TARGETS];
	VkPipeline pipes[STENCIL_PIPES];
	VkCommandPool command_pool;
	VkCommandBuffer command;
	VkFence fence;
	char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

static VkResult stencil_device(struct stencil *stencil);
static VkResult stencil_resources(struct stencil *stencil);
static VkResult stencil_image(struct stencil *stencil, VkFormat format, VkImageUsageFlags usage, VkDeviceSize at, VkImage *image);
static VkResult stencil_buffer(struct stencil *stencil, VkDeviceSize bytes, VkBufferUsageFlags usage, VkDeviceSize at, VkBuffer *buffer);
static VkResult stencil_view(struct stencil *stencil, VkImage image, VkFormat format, VkImageAspectFlags aspects, VkImageView *view);
static VkResult stencil_passes(struct stencil *stencil);
static VkResult stencil_pass(struct stencil *stencil, VkAttachmentLoadOp load, VkAttachmentStoreOp store, VkRenderPass *pass);
static VkResult stencil_pipelines(struct stencil *stencil);
static VkResult stencil_pipeline(struct stencil *stencil, enum stencil_pipe pipe);
static VkResult stencil_record(struct stencil *stencil);
static void stencil_begin(struct stencil *stencil, VkRenderPass pass, uint32_t target);
static void stencil_draw(struct stencil *stencil, enum stencil_pipe pipe, enum stencil_quad quad, float depth);
static VkResult stencil_submit(struct stencil *stencil);
static int stencil_check(const struct stencil *stencil);
static uint32_t stencil_expected(uint32_t target, uint32_t x, uint32_t y);
static void stencil_fill(struct stencil *stencil);
static void stencil_free(struct stencil *stencil);

/* Runs the three passes and says how they went. */
int
main(
	void)
{
	struct stencil stencil;
	VkResult status;
	int wrong;

	/* The device, the resources, the passes, the pipelines, the recording and its submission. */
	memset(&stencil, 0, sizeof(stencil));
	status = stencil_device(&stencil);
	if (status == VK_SUCCESS)
		status = stencil_resources(&stencil);
	if (status == VK_SUCCESS)
		status = stencil_passes(&stencil);
	if (status == VK_SUCCESS)
		status = stencil_pipelines(&stencil);
	if (status == VK_SUCCESS)
		status = stencil_record(&stencil);
	if (status == VK_SUCCESS)
		status = stencil_submit(&stencil);
	if (status != VK_SUCCESS) {
		stencil_free(&stencil);
		return 1;
	}

	/* The pixels read back. */
	wrong = stencil_check(&stencil);
	stencil_free(&stencil);
	if (wrong != 0)
		return 1;

	/* Succeeded: the tests drew what they should. */
	return 0;
}

/* Creates the instance, takes the first device and creates it with its queue. */
static VkResult
stencil_device(
	struct stencil *stencil)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkPhysicalDevice physicals[8];
	VkPhysicalDeviceProperties properties;
	VkFormatProperties format;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	float priority;
	uint32_t count;
	VkResult status;

	/* The instance. */
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "vkstencil";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	status = vkCreateInstance(&instance, NULL, &stencil->instance);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail instance %d\n", (int)status);
		return status;
	}

	/* The first device. */
	count = 8U;
	status = vkEnumeratePhysicalDevices(stencil->instance, &count, physicals);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || count == 0U) {
		printf("VKSTENCIL fail devices %d count=%u\n", (int)status, count);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	stencil->physical = physicals[0];
	vkGetPhysicalDeviceProperties(stencil->physical, &properties);
	snprintf(stencil->name, sizeof(stencil->name), "%s", properties.deviceName);

	/* The format Notes asks for first renders depth and stencil. */
	vkGetPhysicalDeviceFormatProperties(stencil->physical, VK_FORMAT_D24_UNORM_S8_UINT, &format);
	if ((format.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0U) {
		printf("VKSTENCIL fail format D24S8 optimal=%08x\n", (unsigned)format.optimalTilingFeatures);
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

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
	status = vkCreateDevice(stencil->physical, &device, NULL, &stencil->device);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail device %d\n", (int)status);
		return status;
	}
	vkGetDeviceQueue(stencil->device, 0U, 0U, &stencil->queue);

	/* Succeeded: the device is ready. */
	return VK_SUCCESS;
}

/* Allocates the one host-visible allocation and binds the images and buffers into it. */
static VkResult
stencil_resources(
	struct stencil *stencil)
{
	VkMemoryAllocateInfo allocate;
	VkPhysicalDeviceMemoryProperties memory;
	void *mapping;
	uint32_t type;
	uint32_t target;
	VkResult status;

	/* A memory type that is host visible and coherent. */
	vkGetPhysicalDeviceMemoryProperties(stencil->physical, &memory);
	for (type = 0U; type < memory.memoryTypeCount; type++) {
		if ((memory.memoryTypes[type].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
		    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			break;
	}
	if (type == memory.memoryTypeCount) {
		printf("VKSTENCIL fail memory-type none of %u\n", memory.memoryTypeCount);
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	/* The allocation, mapped. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = STENCIL_MEMORY_BYTES;
	allocate.memoryTypeIndex = type;
	status = vkAllocateMemory(stencil->device, &allocate, NULL, &stencil->memory);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail allocate %d\n", (int)status);
		return status;
	}
	status = vkMapMemory(stencil->device, stencil->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail map %d\n", (int)status);
		return status;
	}
	stencil->mapping = mapping;

	/* The three targets with their views and readbacks. */
	for (target = 0U; target < STENCIL_TARGETS; target++) {
		status = stencil_image(stencil, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				       STENCIL_TARGET_AT + target * STENCIL_SLOT, &stencil->targets[target]);
		if (status == VK_SUCCESS)
			status = stencil_view(stencil, stencil->targets[target], VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT,
					      &stencil->target_views[target]);
		if (status == VK_SUCCESS)
			status = stencil_buffer(stencil, STENCIL_SIDE * STENCIL_SIDE * STENCIL_PIXEL, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						STENCIL_READBACK_AT + target * STENCIL_SLOT, &stencil->readbacks[target]);
		if (status != VK_SUCCESS)
			return status;
	}

	/* The shared depth/stencil attachment and the vertex buffer. */
	status = stencil_image(stencil, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, STENCIL_DEPTH_AT,
			       &stencil->depth);
	if (status == VK_SUCCESS)
		status = stencil_view(stencil, stencil->depth, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				      &stencil->depth_view);
	if (status == VK_SUCCESS)
		status = stencil_buffer(stencil, STENCIL_QUADS * STENCIL_QUAD_VERTICES * STENCIL_VERTEX_FLOATS * sizeof(float),
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, STENCIL_VERTICES_AT, &stencil->vertices);
	if (status != VK_SUCCESS)
		return status;

	/* The quads' corners and colours. */
	stencil_fill(stencil);

	/* Succeeded: the resources are bound. */
	return VK_SUCCESS;
}

/* Creates a 64x64 image of one level and binds it at its place. */
static VkResult
stencil_image(
	struct stencil *stencil,
	VkFormat format,
	VkImageUsageFlags usage,
	VkDeviceSize at,
	VkImage *image)
{
	VkImageCreateInfo create;
	VkMemoryRequirements requirements;
	VkResult status;

	/* The image. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = format;
	create.extent.width = STENCIL_SIDE;
	create.extent.height = STENCIL_SIDE;
	create.extent.depth = 1U;
	create.mipLevels = 1U;
	create.arrayLayers = 1U;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = usage;
	status = vkCreateImage(stencil->device, &create, NULL, image);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail image format=%d %d\n", (int)format, (int)status);
		return status;
	}

	/* Bound at its place, which its size must fit. */
	vkGetImageMemoryRequirements(stencil->device, *image, &requirements);
	if (requirements.size > STENCIL_SLOT) {
		printf("VKSTENCIL fail image-size %llu\n", (unsigned long long)requirements.size);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	status = vkBindImageMemory(stencil->device, *image, stencil->memory, at);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail bind-image %d\n", (int)status);
		return status;
	}

	/* Succeeded: the image has storage. */
	return VK_SUCCESS;
}

/* Creates a buffer and binds it at its place. */
static VkResult
stencil_buffer(
	struct stencil *stencil,
	VkDeviceSize bytes,
	VkBufferUsageFlags usage,
	VkDeviceSize at,
	VkBuffer *buffer)
{
	VkBufferCreateInfo create;
	VkMemoryRequirements requirements;
	VkResult status;

	/* The buffer. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = bytes;
	create.usage = usage;
	status = vkCreateBuffer(stencil->device, &create, NULL, buffer);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail buffer %d\n", (int)status);
		return status;
	}

	/* Bound at its place. */
	vkGetBufferMemoryRequirements(stencil->device, *buffer, &requirements);
	status = vkBindBufferMemory(stencil->device, *buffer, stencil->memory, at);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail bind-buffer %d\n", (int)status);
		return status;
	}

	/* Succeeded: the buffer has storage. */
	return VK_SUCCESS;
}

/* Creates a 2D view of an image's one level with the given aspects. */
static VkResult
stencil_view(
	struct stencil *stencil,
	VkImage image,
	VkFormat format,
	VkImageAspectFlags aspects,
	VkImageView *view)
{
	VkImageViewCreateInfo create;
	VkResult status;

	/* The view. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	create.image = image;
	create.viewType = VK_IMAGE_VIEW_TYPE_2D;
	create.format = format;
	create.subresourceRange.aspectMask = aspects;
	create.subresourceRange.levelCount = 1U;
	create.subresourceRange.layerCount = 1U;
	status = vkCreateImageView(stencil->device, &create, NULL, view);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail view format=%d %d\n", (int)format, (int)status);
		return status;
	}

	/* Succeeded: the view exists. */
	return VK_SUCCESS;
}

/*
 * Creates the three passes (Notes' clearing one, one that stores the
 * attachment and one that loads both attachments) and a framebuffer of
 * each target with the shared attachment.
 */
static VkResult
stencil_passes(
	struct stencil *stencil)
{
	VkFramebufferCreateInfo framebuffer;
	VkImageView attachments[2];
	uint32_t target;
	VkResult status;

	/* The passes. */
	status = stencil_pass(stencil, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, &stencil->clear_pass);
	if (status == VK_SUCCESS)
		status = stencil_pass(stencil, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, &stencil->store_pass);
	if (status == VK_SUCCESS)
		status = stencil_pass(stencil, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE, &stencil->load_pass);
	if (status != VK_SUCCESS)
		return status;

	/* One framebuffer of each target, every one compatible with every pass. */
	for (target = 0U; target < STENCIL_TARGETS; target++) {
		attachments[0] = stencil->target_views[target];
		attachments[1] = stencil->depth_view;
		memset(&framebuffer, 0, sizeof(framebuffer));
		framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer.renderPass = stencil->clear_pass;
		framebuffer.attachmentCount = 2U;
		framebuffer.pAttachments = attachments;
		framebuffer.width = STENCIL_SIDE;
		framebuffer.height = STENCIL_SIDE;
		framebuffer.layers = 1U;
		status = vkCreateFramebuffer(stencil->device, &framebuffer, NULL, &stencil->framebuffers[target]);
		if (status != VK_SUCCESS) {
			printf("VKSTENCIL fail framebuffer %d\n", (int)status);
			return status;
		}
	}

	/* Succeeded: the passes and framebuffers exist. */
	return VK_SUCCESS;
}

/*
 * Creates one pass of a B8G8R8A8 colour attachment, left in GENERAL, and a
 * D24S8 attachment left in its attachment layout.  load LOAD loads both
 * from where the last pass left them; CLEAR clears both.  store applies to
 * both of the attachment's aspects.
 */
static VkResult
stencil_pass(
	struct stencil *stencil,
	VkAttachmentLoadOp load,
	VkAttachmentStoreOp store,
	VkRenderPass *pass)
{
	VkAttachmentDescription attachments[2];
	VkAttachmentReference colour;
	VkAttachmentReference depth;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo create;
	VkResult status;

	/* The colour, cleared or loaded, and kept for the readback. */
	memset(attachments, 0, sizeof(attachments));
	attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = load;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	if (load == VK_ATTACHMENT_LOAD_OP_LOAD)
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;

	/* The depth/stencil attachment, its two aspects alike. */
	attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = load;
	attachments[1].storeOp = store;
	attachments[1].stencilLoadOp = load;
	attachments[1].stencilStoreOp = store;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	if (load == VK_ATTACHMENT_LOAD_OP_LOAD)
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	/* The single subpass that draws into both. */
	memset(&colour, 0, sizeof(colour));
	colour.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&depth, 0, sizeof(depth));
	depth.attachment = 1U;
	depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1U;
	subpass.pColorAttachments = &colour;
	subpass.pDepthStencilAttachment = &depth;
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	create.attachmentCount = 2U;
	create.pAttachments = attachments;
	create.subpassCount = 1U;
	create.pSubpasses = &subpass;
	status = vkCreateRenderPass(stencil->device, &create, NULL, pass);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail render-pass load=%d store=%d %d\n", (int)load, (int)store, (int)status);
		return status;
	}

	/* Succeeded: the pass exists. */
	return VK_SUCCESS;
}

/* Creates the modules, the layout (Notes' vertex push constants) and the four pipelines. */
static VkResult
stencil_pipelines(
	struct stencil *stencil)
{
	VkShaderModuleCreateInfo module;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo layout;
	uint32_t pipe;
	VkResult status;

	/* Notes' vertex and fill shaders. */
	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = sizeof(notes_draw_vert);
	module.pCode = notes_draw_vert;
	status = vkCreateShaderModule(stencil->device, &module, NULL, &stencil->vertex_module);
	if (status == VK_SUCCESS) {
		module.codeSize = sizeof(notes_fill_frag);
		module.pCode = notes_fill_frag;
		status = vkCreateShaderModule(stencil->device, &module, NULL, &stencil->fragment_module);
	}
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail module %d\n", (int)status);
		return status;
	}

	/* The target's size in the first vec4 of Notes' 32 bytes of push constants. */
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = 32U;
	memset(&layout, 0, sizeof(layout));
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.pushConstantRangeCount = 1U;
	layout.pPushConstantRanges = &range;
	status = vkCreatePipelineLayout(stencil->device, &layout, NULL, &stencil->pipeline_layout);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail pipeline-layout %d\n", (int)status);
		return status;
	}

	/* Each pipeline. */
	for (pipe = 0U; pipe < STENCIL_PIPES; pipe++) {
		status = stencil_pipeline(stencil, (enum stencil_pipe)pipe);
		if (status != VK_SUCCESS)
			return status;
	}

	/* Succeeded: the pipelines are compiled. */
	return VK_SUCCESS;
}

/*
 * Creates one pipeline as Notes' render_pipeline does: two vec4 attributes,
 * triangles, both faces, premultiplied blending, and its stencil role; or
 * the depth pipeline, a less-than test that writes.
 */
static VkResult
stencil_pipeline(
	struct stencil *stencil,
	enum stencil_pipe pipe)
{
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attributes[2];
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkDynamicState states[2];
	VkPipelineDynamicStateCreateInfo dynamic;
	VkGraphicsPipelineCreateInfo create;
	VkResult status;

	/* The two stages. */
	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = stencil->vertex_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = stencil->fragment_module;
	stages[1].pName = "main";

	/* One vertex buffer of two vec4s a vertex: the corner and the colour. */
	memset(&binding, 0, sizeof(binding));
	binding.stride = STENCIL_VERTEX_FLOATS * sizeof(float);
	memset(attributes, 0, sizeof(attributes));
	attributes[0].location = 0U;
	attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[1].location = 1U;
	attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[1].offset = 4U * sizeof(float);
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 1U;
	input.pVertexBindingDescriptions = &binding;
	input.vertexAttributeDescriptionCount = 2U;
	input.pVertexAttributeDescriptions = attributes;
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	/* The viewport and scissor are set per draw; both faces are drawn. */
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1U;
	viewport.scissorCount = 1U;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	/* Notes' stencil state: compare masks and write masks full, reference zero. */
	memset(&depth, 0, sizeof(depth));
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.front.compareMask = 0xffU;
	depth.front.writeMask = 0xffU;
	depth.front.failOp = VK_STENCIL_OP_KEEP;
	depth.front.depthFailOp = VK_STENCIL_OP_KEEP;
	depth.front.passOp = VK_STENCIL_OP_KEEP;
	depth.front.compareOp = VK_COMPARE_OP_ALWAYS;
	depth.back = depth.front;

	/* The pipeline's role. */
	switch (pipe) {
	case STENCIL_PIPE_COUNT:
		/* Front faces count up, back faces down: the winding number, modulo 256. */
		depth.stencilTestEnable = VK_TRUE;
		depth.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
		depth.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
		break;
	case STENCIL_PIPE_FRINGE:
		/* Only where the stencil is zero, leaving it. */
		depth.stencilTestEnable = VK_TRUE;
		depth.front.compareOp = VK_COMPARE_OP_EQUAL;
		depth.front.writeMask = 0U;
		depth.back = depth.front;
		break;
	case STENCIL_PIPE_COVER:
		/* Only where it is not, setting it back to zero. */
		depth.stencilTestEnable = VK_TRUE;
		depth.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
		depth.front.passOp = VK_STENCIL_OP_ZERO;
		depth.back = depth.front;
		break;
	default:
		/* A nearer fragment passes and writes its depth. */
		depth.depthTestEnable = VK_TRUE;
		depth.depthWriteEnable = VK_TRUE;
		depth.depthCompareOp = VK_COMPARE_OP_LESS;
		break;
	}

	/* Premultiplied blending, and no colour while counting. */
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.blendEnable = VK_TRUE;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
	    VK_COLOR_COMPONENT_A_BIT;
	if (pipe == STENCIL_PIPE_COUNT)
		blend_attachment.colorWriteMask = 0U;
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1U;
	blend.pAttachments = &blend_attachment;
	states[0] = VK_DYNAMIC_STATE_VIEWPORT;
	states[1] = VK_DYNAMIC_STATE_SCISSOR;
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2U;
	dynamic.pDynamicStates = states;

	/* The pipeline, made for the clearing pass and compatible with the others. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create.stageCount = 2U;
	create.pStages = stages;
	create.pVertexInputState = &input;
	create.pInputAssemblyState = &assembly;
	create.pViewportState = &viewport;
	create.pRasterizationState = &raster;
	create.pMultisampleState = &multisample;
	create.pDepthStencilState = &depth;
	create.pColorBlendState = &blend;
	create.pDynamicState = &dynamic;
	create.layout = stencil->pipeline_layout;
	create.renderPass = stencil->clear_pass;
	create.basePipelineIndex = -1;
	status = vkCreateGraphicsPipelines(stencil->device, VK_NULL_HANDLE, 1U, &create, NULL, &stencil->pipes[pipe]);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail pipeline %u %d\n", (unsigned)pipe, (int)status);
		return status;
	}

	/* Succeeded: the pipeline is compiled. */
	return VK_SUCCESS;
}

/* Records the three tests and the readbacks. */
static VkResult
stencil_record(
	struct stencil *stencil)
{
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo allocate;
	VkCommandBufferBeginInfo begin;
	VkBufferImageCopy readback;
	VkDeviceSize offset;
	float push[8];
	uint32_t target;
	VkResult status;

	/* The pool and the command buffer. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	status = vkCreateCommandPool(stencil->device, &pool, NULL, &stencil->command_pool);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail command-pool %d\n", (int)status);
		return status;
	}
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = stencil->command_pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	status = vkAllocateCommandBuffers(stencil->device, &allocate, &stencil->command);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail command-buffer %d\n", (int)status);
		return status;
	}
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	status = vkBeginCommandBuffer(stencil->command, &begin);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail begin %d\n", (int)status);
		return status;
	}

	/* The push constants: the target's size, which every pass shares. */
	memset(push, 0, sizeof(push));
	push[0] = (float)STENCIL_SIDE;
	push[1] = (float)STENCIL_SIDE;

	/* The winding: count, fringe green, cover red, a second cover in black that finds nothing. */
	stencil_begin(stencil, stencil->clear_pass, 0U);
	vkCmdPushConstants(stencil->command, stencil->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	offset = 0U;
	vkCmdBindVertexBuffers(stencil->command, 0U, 1U, &stencil->vertices, &offset);
	stencil_draw(stencil, STENCIL_PIPE_COUNT, STENCIL_QUAD_A, 0.0f);
	stencil_draw(stencil, STENCIL_PIPE_COUNT, STENCIL_QUAD_B, 0.0f);
	stencil_draw(stencil, STENCIL_PIPE_FRINGE, STENCIL_QUAD_GREEN, 0.0f);
	stencil_draw(stencil, STENCIL_PIPE_COVER, STENCIL_QUAD_RED, 0.0f);
	stencil_draw(stencil, STENCIL_PIPE_COVER, STENCIL_QUAD_BLACK, 0.0f);
	vkCmdEndRenderPass(stencil->command);

	/* The stored count, then the cover in a pass that loads it. */
	stencil_begin(stencil, stencil->store_pass, 1U);
	vkCmdPushConstants(stencil->command, stencil->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	vkCmdBindVertexBuffers(stencil->command, 0U, 1U, &stencil->vertices, &offset);
	stencil_draw(stencil, STENCIL_PIPE_COUNT, STENCIL_QUAD_A, 0.0f);
	stencil_draw(stencil, STENCIL_PIPE_COUNT, STENCIL_QUAD_B, 0.0f);
	vkCmdEndRenderPass(stencil->command);
	stencil_begin(stencil, stencil->load_pass, 1U);
	vkCmdPushConstants(stencil->command, stencil->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	vkCmdBindVertexBuffers(stencil->command, 0U, 1U, &stencil->vertices, &offset);
	stencil_draw(stencil, STENCIL_PIPE_COVER, STENCIL_QUAD_RED, 0.0f);
	vkCmdEndRenderPass(stencil->command);

	/* The depth test: green at 0.5, red behind it at 0.75, white over the middle at 0.25. */
	stencil_begin(stencil, stencil->clear_pass, 2U);
	vkCmdPushConstants(stencil->command, stencil->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(push), push);
	vkCmdBindVertexBuffers(stencil->command, 0U, 1U, &stencil->vertices, &offset);
	stencil_draw(stencil, STENCIL_PIPE_DEPTH, STENCIL_QUAD_GREEN, 0.5f);
	stencil_draw(stencil, STENCIL_PIPE_DEPTH, STENCIL_QUAD_RED, 0.75f);
	stencil_draw(stencil, STENCIL_PIPE_DEPTH, STENCIL_QUAD_MIDDLE, 0.25f);
	vkCmdEndRenderPass(stencil->command);

	/* Each target read back, rows of 64 pixels. */
	memset(&readback, 0, sizeof(readback));
	readback.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	readback.imageSubresource.layerCount = 1U;
	readback.imageExtent.width = STENCIL_SIDE;
	readback.imageExtent.height = STENCIL_SIDE;
	readback.imageExtent.depth = 1U;
	for (target = 0U; target < STENCIL_TARGETS; target++)
		vkCmdCopyImageToBuffer(stencil->command, stencil->targets[target], VK_IMAGE_LAYOUT_GENERAL, stencil->readbacks[target], 1U, &readback);

	/* The end. */
	status = vkEndCommandBuffer(stencil->command);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail end %d\n", (int)status);
		return status;
	}

	/* Succeeded: the command buffer is recorded. */
	return VK_SUCCESS;
}

/* Begins a pass on a target: the colour cleared to blue, depth to 1.0 and stencil to 0 where the pass clears. */
static void
stencil_begin(
	struct stencil *stencil,
	VkRenderPass pass,
	uint32_t target)
{
	VkRenderPassBeginInfo begin;
	VkClearValue clears[2];

	/* The clear values; a loading pass ignores them. */
	memset(clears, 0, sizeof(clears));
	clears[0].color.float32[2] = 1.0f;
	clears[0].color.float32[3] = 1.0f;
	clears[1].depthStencil.depth = 1.0f;
	clears[1].depthStencil.stencil = 0U;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = pass;
	begin.framebuffer = stencil->framebuffers[target];
	begin.renderArea.extent.width = STENCIL_SIDE;
	begin.renderArea.extent.height = STENCIL_SIDE;
	begin.clearValueCount = 2U;
	begin.pClearValues = clears;
	vkCmdBeginRenderPass(stencil->command, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

/* Draws one quad with one pipeline, at a depth given as the viewport's near plane. */
static void
stencil_draw(
	struct stencil *stencil,
	enum stencil_pipe pipe,
	enum stencil_quad quad,
	float depth)
{
	VkViewport viewport;
	VkRect2D scissor;

	/* The whole target, its depth range starting at the quad's depth (the shader's z is 0). */
	memset(&viewport, 0, sizeof(viewport));
	viewport.width = (float)STENCIL_SIDE;
	viewport.height = (float)STENCIL_SIDE;
	viewport.minDepth = depth;
	viewport.maxDepth = depth + 0.125f;
	memset(&scissor, 0, sizeof(scissor));
	scissor.extent.width = STENCIL_SIDE;
	scissor.extent.height = STENCIL_SIDE;

	/* The pipeline, the dynamic state and the quad's six vertices. */
	vkCmdBindPipeline(stencil->command, VK_PIPELINE_BIND_POINT_GRAPHICS, stencil->pipes[pipe]);
	vkCmdSetViewport(stencil->command, 0U, 1U, &viewport);
	vkCmdSetScissor(stencil->command, 0U, 1U, &scissor);
	vkCmdDraw(stencil->command, STENCIL_QUAD_VERTICES, 1U, (uint32_t)quad * STENCIL_QUAD_VERTICES, 0U);
}

/* Submits the command buffer with a fence and waits for it. */
static VkResult
stencil_submit(
	struct stencil *stencil)
{
	VkFenceCreateInfo fence;
	VkSubmitInfo submit;
	VkResult status;

	/* The fence. */
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	status = vkCreateFence(stencil->device, &fence, NULL, &stencil->fence);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail fence %d\n", (int)status);
		return status;
	}

	/* The submission and the wait. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &stencil->command;
	status = vkQueueSubmit(stencil->queue, 1U, &submit, stencil->fence);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail submit %d\n", (int)status);
		return status;
	}
	status = vkWaitForFences(stencil->device, 1U, &stencil->fence, VK_TRUE, STENCIL_WAIT_NS);
	if (status != VK_SUCCESS) {
		printf("VKSTENCIL fail wait %d\n", (int)status);
		return status;
	}

	/* Succeeded: the GPU has done the work. */
	return VK_SUCCESS;
}

/* Checks every read-back pixel of the three targets; says the first wrong one of each. */
static int
stencil_check(
	const struct stencil *stencil)
{
	static const char *const names[STENCIL_TARGETS] = { "winding", "stored", "depth" };
	const uint8_t *readback;
	uint32_t pixel;
	uint32_t want;
	uint32_t wrong;
	uint32_t total;
	uint32_t target;
	uint32_t x;
	uint32_t y;

	/* Each target against its picture. */
	total = 0U;
	for (target = 0U; target < STENCIL_TARGETS; target++) {
		readback = stencil->mapping + STENCIL_READBACK_AT + target * STENCIL_SLOT;
		wrong = 0U;
		for (y = 0U; y < STENCIL_SIDE; y++) {
			for (x = 0U; x < STENCIL_SIDE; x++) {
				memcpy(&pixel, readback + (y * STENCIL_SIDE + x) * STENCIL_PIXEL, sizeof(pixel));
				want = stencil_expected(target, x, y);
				if (pixel != want) {
					if (wrong == 0U)
						printf("VKSTENCIL fail %s pixel (%u,%u) is %08x want %08x\n", names[target], x, y, pixel, want);
					wrong++;
				}
			}
		}
		if (wrong != 0U)
			printf("VKSTENCIL fail %s wrong=%u of %u\n", names[target], wrong, STENCIL_SIDE * STENCIL_SIDE);
		total += wrong;
	}

	/* Reports the wrong pixels. */
	if (total != 0U)
		return 1;

	/* Succeeded: every pixel is right. */
	printf("VKSTENCIL ok device=%s pixels=%u\n", stencil->name, STENCIL_TARGETS * STENCIL_SIDE * STENCIL_SIDE);
	return 0;
}

/*
 * Reports the pixel a target should hold at (x, y): red inside exactly one
 * of A (8..40) and B (24..56) and green elsewhere for the winding, red there
 * and the blue clear elsewhere for the stored count, and white in the
 * middle (16..48) on green for the depth test.
 */
static uint32_t
stencil_expected(
	uint32_t target,
	uint32_t x,
	uint32_t y)
{
	uint32_t inside;

	/* The depth test's picture. */
	if (target == 2U) {
		if (x >= 16U && x < 48U && y >= 16U && y < 48U)
			return STENCIL_WHITE;
		return STENCIL_GREEN;
	}

	/* How many of the two rectangles hold the pixel. */
	inside = 0U;
	if (x >= 8U && x < 40U && y >= 8U && y < 40U)
		inside++;
	if (x >= 24U && x < 56U && y >= 24U && y < 56U)
		inside++;

	/* An odd winding is covered; the rest is the fringe's green or the clear's blue. */
	if (inside == 1U)
		return STENCIL_RED;
	if (target == 0U)
		return STENCIL_GREEN;

	/* Succeeded: the stored count's uncovered pixels keep the clear. */
	return STENCIL_BLUE;
}

/*
 * Fills the vertex buffer with the quads: each two triangles of a
 * rectangle in pixels, in one colour.  B's corners run the other way.
 */
static void
stencil_fill(
	struct stencil *stencil)
{
	static const float rectangles[STENCIL_QUADS][4] = {
		{ 8.0f, 8.0f, 40.0f, 40.0f },
		{ 24.0f, 24.0f, 56.0f, 56.0f },
		{ 0.0f, 0.0f, 64.0f, 64.0f },
		{ 0.0f, 0.0f, 64.0f, 64.0f },
		{ 0.0f, 0.0f, 64.0f, 64.0f },
		{ 16.0f, 16.0f, 48.0f, 48.0f }
	};
	static const float colours[STENCIL_QUADS][4] = {
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 0.0f, 1.0f, 0.0f, 1.0f },
		{ 1.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f }
	};
	static const unsigned corners[STENCIL_QUAD_VERTICES][2] = {
		{ 0U, 1U }, { 2U, 1U }, { 0U, 3U }, { 2U, 1U }, { 2U, 3U }, { 0U, 3U }
	};
	static const unsigned reversed[STENCIL_QUAD_VERTICES] = { 0U, 2U, 1U, 3U, 5U, 4U };
	float *vertex;
	unsigned quad;
	unsigned index;
	unsigned corner;

	/* Each quad's six vertices: the corner, a zero fringe distance, and the colour. */
	for (quad = 0U; quad < STENCIL_QUADS; quad++) {
		for (index = 0U; index < STENCIL_QUAD_VERTICES; index++) {
			corner = index;
			if (quad == STENCIL_QUAD_B)
				corner = reversed[index];
			vertex = (float *)(stencil->mapping + STENCIL_VERTICES_AT) + (quad * STENCIL_QUAD_VERTICES + index) * STENCIL_VERTEX_FLOATS;
			vertex[0] = rectangles[quad][corners[corner][0]];
			vertex[1] = rectangles[quad][corners[corner][1]];
			vertex[2] = 0.0f;
			vertex[3] = 0.0f;
			memcpy(vertex + 4, colours[quad], sizeof(colours[quad]));
		}
	}
}

/* Destroys what was made, in reverse. */
static void
stencil_free(
	struct stencil *stencil)
{
	uint32_t index;

	/* Waits for the device before anything goes. */
	if (stencil->device != VK_NULL_HANDLE)
		(void)vkDeviceWaitIdle(stencil->device);

	/* The recording, the pipelines and the passes. */
	if (stencil->fence != VK_NULL_HANDLE)
		vkDestroyFence(stencil->device, stencil->fence, NULL);
	if (stencil->command_pool != VK_NULL_HANDLE)
		vkDestroyCommandPool(stencil->device, stencil->command_pool, NULL);
	for (index = 0U; index < STENCIL_PIPES; index++) {
		if (stencil->pipes[index] != VK_NULL_HANDLE)
			vkDestroyPipeline(stencil->device, stencil->pipes[index], NULL);
	}
	for (index = 0U; index < STENCIL_TARGETS; index++) {
		if (stencil->framebuffers[index] != VK_NULL_HANDLE)
			vkDestroyFramebuffer(stencil->device, stencil->framebuffers[index], NULL);
	}
	if (stencil->load_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(stencil->device, stencil->load_pass, NULL);
	if (stencil->store_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(stencil->device, stencil->store_pass, NULL);
	if (stencil->clear_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(stencil->device, stencil->clear_pass, NULL);
	if (stencil->pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(stencil->device, stencil->pipeline_layout, NULL);
	if (stencil->fragment_module != VK_NULL_HANDLE)
		vkDestroyShaderModule(stencil->device, stencil->fragment_module, NULL);
	if (stencil->vertex_module != VK_NULL_HANDLE)
		vkDestroyShaderModule(stencil->device, stencil->vertex_module, NULL);

	/* The views, buffers and images, then the memory, the device and the instance. */
	if (stencil->depth_view != VK_NULL_HANDLE)
		vkDestroyImageView(stencil->device, stencil->depth_view, NULL);
	if (stencil->depth != VK_NULL_HANDLE)
		vkDestroyImage(stencil->device, stencil->depth, NULL);
	if (stencil->vertices != VK_NULL_HANDLE)
		vkDestroyBuffer(stencil->device, stencil->vertices, NULL);
	for (index = 0U; index < STENCIL_TARGETS; index++) {
		if (stencil->readbacks[index] != VK_NULL_HANDLE)
			vkDestroyBuffer(stencil->device, stencil->readbacks[index], NULL);
		if (stencil->target_views[index] != VK_NULL_HANDLE)
			vkDestroyImageView(stencil->device, stencil->target_views[index], NULL);
		if (stencil->targets[index] != VK_NULL_HANDLE)
			vkDestroyImage(stencil->device, stencil->targets[index], NULL);
	}
	if (stencil->memory != VK_NULL_HANDLE)
		vkFreeMemory(stencil->device, stencil->memory, NULL);
	if (stencil->device != VK_NULL_HANDLE)
		vkDestroyDevice(stencil->device, NULL);
	if (stencil->instance != VK_NULL_HANDLE)
		vkDestroyInstance(stencil->instance, NULL);
}
