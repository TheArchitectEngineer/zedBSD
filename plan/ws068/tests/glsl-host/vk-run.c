/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws068-p018: runs shaders zedBSD's GLSL compiler made on the host's
 * Vulkan (lavapipe) and checks the colour they draw.
 *
 *   vk-run DIRECTORY
 *
 * Each DIRECTORY/NAME.frag is a test: its fragment shader is linked with
 * DIRECTORY/NAME.vert when there is one, or else with a vertex shader
 * that covers the target, then linked the way libGLESv2 links (the
 * reflection in spirv.c finds the uniforms, the vertex shader's
 * gl_Position is rewritten), and drawn into a 4x4 RGBA8 target.  The
 * source says what to expect in comment lines:
 *
 *   // version: 130             the default version of the source (100)
 *   // uniform: NAME v0 v1 ...  values of a uniform (floats, or integers for int, uint and bool)
 *   // expect: R G B A          the colour at every pixel, 0..255 (within 2)
 *
 * Every sampler reads a 2x2 texture, nearest, clamped: red (0,0), green
 * (1,0), blue (0,1), white (1,1) in texture coordinates.  Every uniform
 * block of its own (WS068 p021) reads one buffer whose float at byte
 * offset o is o / 4, so a shader can check its members' std140 offsets.
 */

#include "../../../../userland/base/libglesv2/gles.h"
#include "../../../../userland/base/libglesv2/glsl/glsl.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The target's size and format. */
#define RUN_SIZE		4U
#define RUN_FORMAT		VK_FORMAT_R8G8B8A8_UNORM

/* The size of the uniform buffer, and the most tests in a directory. */
#define RUN_UNIFORM_BYTES	4096U
#define RUN_MAX_TESTS		256U

/*
 * The Vulkan objects every test shares: the device, the target and its
 * readback buffer, the texture, the uniform and vertex buffers.
 */
struct run_context {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkPhysicalDeviceMemoryProperties memory;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	VkCommandPool pool;
	VkCommandBuffer commands;
	VkFence fence;

	/* The target, its view, the pass and framebuffer, and the readback buffer. */
	VkImage target;
	VkDeviceMemory target_memory;
	VkImageView target_view;
	VkRenderPass pass;
	VkFramebuffer framebuffer;
	VkBuffer readback;
	VkDeviceMemory readback_memory;
	void *readback_mapped;

	/* The 2x2 texture and its sampler. */
	VkImage texture;
	VkDeviceMemory texture_memory;
	VkImageView texture_view;
	VkSampler sampler;

	/* The uniform, uniform block and vertex buffers (host visible, mapped). */
	VkBuffer uniforms;
	VkDeviceMemory uniforms_memory;
	void *uniforms_mapped;
	VkBuffer pattern;
	VkDeviceMemory pattern_memory;
	void *pattern_mapped;
	VkBuffer vertices;
	VkDeviceMemory vertices_memory;
	void *vertices_mapped;
};

/*
 * What a test's comments ask for.
 */
struct run_test {
	unsigned version;
	unsigned expect[4];
	int has_expect;
	char uniform_names[16][64];
	float uniform_values[16][16];
	unsigned uniform_counts[16];
	unsigned uniform_count;
};

static int run_setup(struct run_context *context);
static uint32_t run_memory_type(struct run_context *context, uint32_t bits, VkMemoryPropertyFlags flags);
static int run_buffer(struct run_context *context, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory, void **mapped);
static int run_image(struct run_context *context, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *memory, VkImageView *view);
static int run_texture(struct run_context *context);
static void run_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to);
static int run_submit(struct run_context *context);
static char *run_read(const char *path);
static void run_parse(const char *source, struct run_test *test);
static void run_parse_uniform(const char *text, struct run_test *test);
static int run_one(struct run_context *context, const char *directory, const char *name);
static int run_program(struct run_context *context, const struct run_test *test, struct glsl_program *program, const char *name);
static void run_fill_uniforms(struct run_context *context, const struct run_test *test, struct gles_spirv *spirv);
static int run_compare(const char *name, const unsigned char *pixels, const struct run_test *test);

/*
 * Runs every test of a directory.
 */
int
main(
	int argc,
	char **argv)
{
	struct run_context context;
	struct dirent *entry;
	DIR *directory;
	char *names[RUN_MAX_TESTS];
	char *swap;
	unsigned count;
	unsigned index;
	unsigned other;
	unsigned failures;
	size_t length;
	int status;
	int differs;

	/* The directory of tests, and the device. */
	if (argc < 2) {
		fprintf(stderr, "usage: vk-run DIRECTORY\n");
		return 2;
	}

	/* The device. */
	status = run_setup(&context);
	if (status != 0) {
		printf("vk-run: no Vulkan device\n");
		return 1;
	}

	/* The tests' names, sorted. */
	directory = opendir(argv[1]);
	if (directory == NULL) {
		printf("vk-run: cannot read %s\n", argv[1]);
		return 1;
	}

	/* The names ending in .frag. */
	count = 0U;
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL || count == RUN_MAX_TESTS)
			break;
		length = strlen(entry->d_name);
		if (length < 6U)
			continue;
		differs = strcmp(entry->d_name + length - 5U, ".frag");
		if (differs != 0)
			continue;
		names[count] = malloc(length - 4U);
		if (names[count] == NULL)
			break;
		memcpy(names[count], entry->d_name, length - 5U);
		names[count][length - 5U] = '\0';
		count++;
	}

	/* Sorted by name. */
	(void)closedir(directory);
	for (index = 0U; index < count; index++) {
		for (other = index + 1U; other < count; other++) {
			differs = strcmp(names[other], names[index]);
			if (differs < 0) {
				swap = names[index];
				names[index] = names[other];
				names[other] = swap;
			}
		}
	}

	/* Each test. */
	failures = 0U;
	for (index = 0U; index < count; index++) {
		status = run_one(&context, argv[1], names[index]);
		if (status != 0)
			failures++;
		free(names[index]);
	}

	/* The summary. */
	printf("vk-run: %u tests, %u failed\n", count, failures);
	if (failures != 0U)
		return 1;
	return 0;
}

/* Makes the device and the objects every test shares. */
static int
run_setup(
	struct run_context *context)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo instance;
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo device;
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo allocate;
	VkFenceCreateInfo fence;
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass;
	VkFramebufferCreateInfo framebuffer;
	VkQueueFamilyProperties families[16];
	uint32_t count;
	uint32_t index;
	float priority;
	VkResult result;
	int status;

	/* The instance and the first device. */
	memset(context, 0, sizeof(*context));
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	result = vkCreateInstance(&instance, NULL, &context->instance);
	if (result != VK_SUCCESS)
		return -1;
	count = 1U;
	result = vkEnumeratePhysicalDevices(context->instance, &count, &context->physical);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
		return -1;
	if (count == 0U)
		return -1;
	vkGetPhysicalDeviceMemoryProperties(context->physical, &context->memory);

	/* A graphics queue. */
	count = 16U;
	vkGetPhysicalDeviceQueueFamilyProperties(context->physical, &count, families);
	context->family = 0xffffffffU;
	for (index = 0U; index < count; index++) {
		if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
			context->family = index;
			break;
		}
	}

	/* One is needed. */
	if (context->family == 0xffffffffU)
		return -1;

	/* The device with that queue. */
	priority = 1.0f;
	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = context->family;
	queue.queueCount = 1U;
	queue.pQueuePriorities = &priority;
	memset(&device, 0, sizeof(device));
	device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device.queueCreateInfoCount = 1U;
	device.pQueueCreateInfos = &queue;
	result = vkCreateDevice(context->physical, &device, NULL, &context->device);
	if (result != VK_SUCCESS)
		return -1;
	vkGetDeviceQueue(context->device, context->family, 0U, &context->queue);

	/* The command buffer and its fence. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = context->family;
	result = vkCreateCommandPool(context->device, &pool, NULL, &context->pool);
	if (result != VK_SUCCESS)
		return -1;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = context->pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	result = vkAllocateCommandBuffers(context->device, &allocate, &context->commands);
	if (result != VK_SUCCESS)
		return -1;
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	result = vkCreateFence(context->device, &fence, NULL, &context->fence);
	if (result != VK_SUCCESS)
		return -1;

	/* The target and the pass that clears it and leaves it for the copy. */
	status = run_image(context, RUN_SIZE, RUN_SIZE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			   &context->target, &context->target_memory, &context->target_view);
	if (status != 0)
		return -1;
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = RUN_FORMAT;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	reference.attachment = 0U;
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
	result = vkCreateRenderPass(context->device, &pass, NULL, &context->pass);
	if (result != VK_SUCCESS)
		return -1;
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = context->pass;
	framebuffer.attachmentCount = 1U;
	framebuffer.pAttachments = &context->target_view;
	framebuffer.width = RUN_SIZE;
	framebuffer.height = RUN_SIZE;
	framebuffer.layers = 1U;
	result = vkCreateFramebuffer(context->device, &framebuffer, NULL, &context->framebuffer);
	if (result != VK_SUCCESS)
		return -1;

	/* The readback, uniform and vertex buffers. */
	status = run_buffer(context, RUN_SIZE * RUN_SIZE * 4U, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &context->readback,
			    &context->readback_memory, &context->readback_mapped);
	if (status != 0)
		return -1;
	status = run_buffer(context, RUN_UNIFORM_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &context->uniforms,
			    &context->uniforms_memory, &context->uniforms_mapped);
	if (status != 0)
		return -1;
	status = run_buffer(context, 64U, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &context->vertices, &context->vertices_memory,
			    &context->vertices_mapped);
	if (status != 0)
		return -1;

	/* The uniform blocks' buffer: the float at byte offset o is o / 4. */
	status = run_buffer(context, RUN_UNIFORM_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &context->pattern,
			    &context->pattern_memory, &context->pattern_mapped);
	if (status != 0)
		return -1;
	for (index = 0U; index < RUN_UNIFORM_BYTES / 4U; index++)
		((float *)context->pattern_mapped)[index] = (float)index;

	/* The texture. */
	status = run_texture(context);
	return status;
}

/* Returns a memory type index of the bits with the flags. */
static uint32_t
run_memory_type(
	struct run_context *context,
	uint32_t bits,
	VkMemoryPropertyFlags flags)
{
	uint32_t index;

	/* The first type that fits. */
	for (index = 0U; index < context->memory.memoryTypeCount; index++) {
		if ((bits & (1U << index)) == 0U)
			continue;
		if ((context->memory.memoryTypes[index].propertyFlags & flags) == flags)
			return index;
	}

	/* None. */
	return 0U;
}

/* Makes a host-visible buffer, mapped. */
static int
run_buffer(
	struct run_context *context,
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VkBuffer *buffer,
	VkDeviceMemory *memory,
	void **mapped)
{
	VkBufferCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkResult result;

	/* The buffer. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create.size = size;
	create.usage = usage;
	result = vkCreateBuffer(context->device, &create, NULL, buffer);
	if (result != VK_SUCCESS)
		return -1;

	/* Its memory, mapped. */
	vkGetBufferMemoryRequirements(context->device, *buffer, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = run_memory_type(context, requirements.memoryTypeBits,
						   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = vkAllocateMemory(context->device, &allocate, NULL, memory);
	if (result != VK_SUCCESS)
		return -1;
	result = vkBindBufferMemory(context->device, *buffer, *memory, 0U);
	if (result != VK_SUCCESS)
		return -1;
	result = vkMapMemory(context->device, *memory, 0U, VK_WHOLE_SIZE, 0U, mapped);
	if (result != VK_SUCCESS)
		return -1;

	/* Succeeded. */
	memset(*mapped, 0, (size_t)size);
	return 0;
}

/* Makes an RGBA8 image with its view. */
static int
run_image(
	struct run_context *context,
	uint32_t width,
	uint32_t height,
	VkImageUsageFlags usage,
	VkImage *image,
	VkDeviceMemory *memory,
	VkImageView *view)
{
	VkImageCreateInfo create;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkImageViewCreateInfo view_create;
	VkResult result;

	/* The image. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create.imageType = VK_IMAGE_TYPE_2D;
	create.format = RUN_FORMAT;
	create.extent.width = width;
	create.extent.height = height;
	create.extent.depth = 1U;
	create.mipLevels = 1U;
	create.arrayLayers = 1U;
	create.samples = VK_SAMPLE_COUNT_1_BIT;
	create.tiling = VK_IMAGE_TILING_OPTIMAL;
	create.usage = usage;
	create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(context->device, &create, NULL, image);
	if (result != VK_SUCCESS)
		return -1;

	/* Its memory. */
	vkGetImageMemoryRequirements(context->device, *image, &requirements);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = run_memory_type(context, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(context->device, &allocate, NULL, memory);
	if (result != VK_SUCCESS)
		return -1;
	result = vkBindImageMemory(context->device, *image, *memory, 0U);
	if (result != VK_SUCCESS)
		return -1;

	/* Its view. */
	memset(&view_create, 0, sizeof(view_create));
	view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_create.image = *image;
	view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_create.format = RUN_FORMAT;
	view_create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_create.subresourceRange.levelCount = 1U;
	view_create.subresourceRange.layerCount = 1U;
	result = vkCreateImageView(context->device, &view_create, NULL, view);
	if (result != VK_SUCCESS)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Makes the 2x2 texture (red, green / blue, white) and its nearest, clamped sampler. */
static int
run_texture(
	struct run_context *context)
{
	static const unsigned char texels[16] = {
		255, 0, 0, 255, 0, 255, 0, 255,
		0, 0, 255, 255, 255, 255, 255, 255
	};
	VkSamplerCreateInfo sampler;
	VkCommandBufferBeginInfo begin;
	VkBufferImageCopy copy;
	VkBuffer staging;
	VkDeviceMemory staging_memory;
	void *mapped;
	VkResult result;
	int status;

	/* The image, and its texels through a staging buffer. */
	status = run_image(context, 2U, 2U, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &context->texture,
			   &context->texture_memory, &context->texture_view);
	if (status != 0)
		return -1;
	status = run_buffer(context, sizeof(texels), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &staging_memory, &mapped);
	if (status != 0)
		return -1;
	memcpy(mapped, texels, sizeof(texels));

	/* The copy. */
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	(void)vkBeginCommandBuffer(context->commands, &begin);
	run_barrier(context->commands, context->texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = 2U;
	copy.imageExtent.height = 2U;
	copy.imageExtent.depth = 1U;
	vkCmdCopyBufferToImage(context->commands, staging, context->texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
	run_barrier(context->commands, context->texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	(void)vkEndCommandBuffer(context->commands);
	status = run_submit(context);
	if (status != 0)
		return -1;

	/* The sampler. */
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_NEAREST;
	sampler.minFilter = VK_FILTER_NEAREST;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	result = vkCreateSampler(context->device, &sampler, NULL, &context->sampler);
	if (result != VK_SUCCESS)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Records a layout change of an image. */
static void
run_barrier(
	VkCommandBuffer commands,
	VkImage image,
	VkImageLayout from,
	VkImageLayout to)
{
	VkImageMemoryBarrier barrier;

	/* Everything before, everything after. */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.oldLayout = from;
	barrier.newLayout = to;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1U;
	barrier.subresourceRange.layerCount = 1U;
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0U, 0U, NULL, 0U,
			     NULL, 1U, &barrier);
}

/* Submits the command buffer and waits for it. */
static int
run_submit(
	struct run_context *context)
{
	VkSubmitInfo submit;
	VkResult result;

	/* The submission and its fence. */
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &context->commands;
	result = vkQueueSubmit(context->queue, 1U, &submit, context->fence);
	if (result != VK_SUCCESS)
		return -1;
	result = vkWaitForFences(context->device, 1U, &context->fence, VK_TRUE, UINT64_MAX);
	(void)vkResetFences(context->device, 1U, &context->fence);
	if (result != VK_SUCCESS)
		return -1;

	/* Succeeded. */
	return 0;
}

/* Reads a whole file as a string (NULL when there is none). */
static char *
run_read(
	const char *path)
{
	FILE *file;
	char *text;
	long size;
	size_t got;

	/* The file and its size. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0L, SEEK_END);
	size = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	text = malloc((size_t)size + 1U);
	if (text == NULL) {
		(void)fclose(file);
		return NULL;
	}

	/* The bytes, terminated. */
	got = fread(text, 1U, (size_t)size, file);
	(void)fclose(file);
	text[got] = '\0';
	return text;
}

/* Reads a test's comment lines: version, uniforms, expected colour. */
static void
run_parse(
	const char *source,
	struct run_test *test)
{
	const char *line;
	char *end;
	unsigned index;
	int differs;

	/* The defaults. */
	memset(test, 0, sizeof(*test));
	test->version = 100U;

	/* Each "// key:" line. */
	for (line = source; line != NULL; line = strchr(line, '\n')) {
		while (*line == '\n')
			line++;

		/* The version. */
		differs = strncmp(line, "// version:", 11U);
		if (differs == 0)
			test->version = (unsigned)strtoul(line + 11, NULL, 10);

		/* The colour expected. */
		differs = strncmp(line, "// expect:", 10U);
		if (differs == 0) {
			end = (char *)line + 10;
			for (index = 0U; index < 4U; index++)
				test->expect[index] = (unsigned)strtoul(end, &end, 10);
			test->has_expect = 1;
		}

		/* A uniform's values. */
		differs = strncmp(line, "// uniform:", 11U);
		if (differs == 0 && test->uniform_count < 16U)
			run_parse_uniform(line + 11, test);

		/* The end of the source. */
		if (*line == '\0')
			break;
	}
}

/* Reads one "// uniform: NAME v0 v1 ..." line (after the colon). */
static void
run_parse_uniform(
	const char *text,
	struct run_test *test)
{
	char *end;
	unsigned index;
	unsigned slot;

	/* The name. */
	slot = test->uniform_count;
	end = (char *)text;
	while (*end == ' ')
		end++;
	for (index = 0U; index < 63U; index++) {
		if (end[index] == ' ' || end[index] == '\n' || end[index] == '\0')
			break;
		test->uniform_names[slot][index] = end[index];
	}

	/* The name ends there. */
	test->uniform_names[slot][index] = '\0';
	end += index;

	/* The values up to the end of the line. */
	for (index = 0U; index < 16U; index++) {
		while (*end == ' ')
			end++;
		if (*end == '\n' || *end == '\0')
			break;
		test->uniform_values[slot][index] = strtof(end, &end);
	}

	/* The uniform is one more. */
	test->uniform_counts[slot] = index;
	test->uniform_count++;
}

/* Runs one test: compile, link, draw, compare. */
static int
run_one(
	struct run_context *context,
	const char *directory,
	const char *name)
{
	static const char quad_100[] =
		"attribute vec2 a_position;\nvoid main()\n{\n\tgl_Position = vec4(a_position, 0.0, 1.0);\n}\n";
	static const char quad_130[] =
		"#version 130\nin vec2 a_position;\nvoid main()\n{\n\tgl_Position = vec4(a_position, 0.0, 1.0);\n}\n";
	struct glsl_binding binding;
	struct glsl_program program;
	struct glsl_shader *vertex;
	struct glsl_shader *fragment;
	struct run_test test;
	char path[1024];
	char *fragment_source;
	char *vertex_source;
	char *log;
	int status;

	/* The sources. */
	(void)snprintf(path, sizeof(path), "%s/%s.frag", directory, name);
	fragment_source = run_read(path);
	if (fragment_source == NULL)
		return -1;
	run_parse(fragment_source, &test);
	(void)snprintf(path, sizeof(path), "%s/%s.vert", directory, name);
	vertex_source = run_read(path);

	/* The two shaders. */
	if (vertex_source != NULL) {
		vertex = glsl_compile(GLSL_STAGE_VERTEX, vertex_source, test.version, &log);
	} else if (test.version >= 130U) {
		vertex = glsl_compile(GLSL_STAGE_VERTEX, quad_130, test.version, &log);
	} else {
		vertex = glsl_compile(GLSL_STAGE_VERTEX, quad_100, test.version, &log);
	}

	/* The logs, then the fragment shader. */
	if (log != NULL)
		printf("%s.vert: %s", name, log);
	free(log);
	fragment = glsl_compile(GLSL_STAGE_FRAGMENT, fragment_source, test.version, &log);
	if (log != NULL)
		printf("%s.frag: %s", name, log);
	free(log);
	free(fragment_source);
	free(vertex_source);
	if (vertex == NULL || fragment == NULL) {
		printf("%s: FAIL (compile)\n", name);
		glsl_shader_free(vertex);
		glsl_shader_free(fragment);
		return -1;
	}

	/* The link, with the position at location 0. */
	binding.name = "a_position";
	binding.location = 0U;
	status = glsl_link(vertex, fragment, &binding, 1U, &program, &log);
	glsl_shader_free(vertex);
	glsl_shader_free(fragment);
	if (status != 0) {
		printf("%s: FAIL (link) %s", name, log);
		free(log);
		return -1;
	}

	/* The draw and the check. */
	status = run_program(context, &test, &program, name);
	glsl_program_free(&program);
	return status;
}

/* Draws a linked program over the target and compares every pixel. */
static int
run_program(
	struct run_context *context,
	const struct run_test *test,
	struct glsl_program *program,
	const char *name)
{
	static const float quad[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
	struct gles_spirv spirv[2];
	VkShaderModuleCreateInfo module_create;
	VkShaderModule modules[2];
	VkDescriptorSetLayoutBinding bindings[17];
	VkDescriptorSetLayoutCreateInfo set_create;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayoutCreateInfo layout_create;
	VkPipelineLayout layout;
	VkDescriptorPoolSize sizes[2];
	VkDescriptorPoolCreateInfo pool_create;
	VkDescriptorPool pool;
	VkDescriptorSetAllocateInfo set_allocate;
	VkDescriptorSet set;
	VkWriteDescriptorSet writes[17];
	VkDescriptorBufferInfo buffer_info;
	VkDescriptorBufferInfo pattern_info;
	VkDescriptorImageInfo image_info;
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription vertex_binding;
	VkVertexInputAttributeDescription vertex_attribute;
	VkPipelineVertexInputStateCreateInfo vertex_input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkViewport viewport;
	VkRect2D scissor;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkGraphicsPipelineCreateInfo pipeline_create;
	VkPipeline pipeline;
	VkCommandBufferBeginInfo begin;
	VkRenderPassBeginInfo pass_begin;
	VkClearValue clear;
	VkBufferImageCopy copy;
	VkDeviceSize offset;
	uint32_t *patched;
	size_t patched_words;
	char log[256];
	unsigned count;
	unsigned index;
	unsigned stage;
	unsigned position_location;
	int has_block;
	int status;
	int differs;
	VkResult result;

	/* Each stage reflected as libGLESv2 does, the vertex stage's gl_Position rewritten. */
	for (stage = 0U; stage < 2U; stage++) {
		status = gles_spirv_reflect(program->code[stage], program->words[stage], &spirv[stage], log, sizeof(log));
		if (status != 0) {
			printf("%s: FAIL (reflect) %s", name, log);
			return -1;
		}
	}

	/* The vertex stage's gl_Position rewritten. */
	patched = gles_spirv_position(program->code[0], program->words[0], 1, &patched_words);
	if (patched == NULL) {
		printf("%s: FAIL (position)\n", name);
		return -1;
	}

	/* The shader modules. */
	memset(&module_create, 0, sizeof(module_create));
	module_create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_create.codeSize = patched_words * 4U;
	module_create.pCode = patched;
	result = vkCreateShaderModule(context->device, &module_create, NULL, &modules[0]);
	free(patched);
	if (result != VK_SUCCESS)
		return -1;
	module_create.codeSize = program->words[1] * 4U;
	module_create.pCode = program->code[1];
	result = vkCreateShaderModule(context->device, &module_create, NULL, &modules[1]);
	if (result != VK_SUCCESS)
		return -1;

	/* The uniforms' values, and the layout: the block at 0, the samplers where they are. */
	memset(context->uniforms_mapped, 0, RUN_UNIFORM_BYTES);
	run_fill_uniforms(context, test, &spirv[0]);
	run_fill_uniforms(context, test, &spirv[1]);
	count = 0U;
	has_block = spirv[0].has_block || spirv[1].has_block;
	memset(bindings, 0, sizeof(bindings));
	if (has_block) {
		bindings[0].binding = 0U;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1U;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		count = 1U;
	}

	/* Each stage's uniform blocks of their own. */
	for (stage = 0U; stage < 2U; stage++) {
		for (index = 0U; index < spirv[stage].named_count && count < 17U; index++) {
			bindings[count].binding = spirv[stage].named_bindings[index];
			bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[count].descriptorCount = 1U;
			bindings[count].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			count++;
		}
	}

	/* Each stage's samplers. */
	for (stage = 0U; stage < 2U; stage++) {
		for (index = 0U; index < spirv[stage].uniform_count && count < 17U; index++) {
			if (!spirv[stage].uniforms[index].sampler)
				continue;
			bindings[count].binding = spirv[stage].uniforms[index].binding;
			bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[count].descriptorCount = 1U;
			bindings[count].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			count++;
		}
	}

	/* A binding both stages have is kept once. */
	for (index = 1U; index < count; index++) {
		for (stage = 0U; stage < index; stage++) {
			if (bindings[stage].binding == bindings[index].binding)
				bindings[index].stageFlags = 0U;
		}
	}

	/* The kept ones packed. */
	stage = 0U;
	for (index = 0U; index < count; index++) {
		if (bindings[index].stageFlags == 0U)
			continue;
		bindings[stage] = bindings[index];
		stage++;
	}

	/* The count of the kept ones. */
	count = stage;
	memset(&set_create, 0, sizeof(set_create));
	set_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_create.bindingCount = count;
	set_create.pBindings = bindings;
	result = vkCreateDescriptorSetLayout(context->device, &set_create, NULL, &set_layout);
	if (result != VK_SUCCESS)
		return -1;
	memset(&layout_create, 0, sizeof(layout_create));
	layout_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_create.setLayoutCount = 1U;
	layout_create.pSetLayouts = &set_layout;
	result = vkCreatePipelineLayout(context->device, &layout_create, NULL, &layout);
	if (result != VK_SUCCESS)
		return -1;

	/* The descriptor set: the uniform buffer and the texture. */
	sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	sizes[0].descriptorCount = 17U;
	sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[1].descriptorCount = 16U;
	memset(&pool_create, 0, sizeof(pool_create));
	pool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_create.maxSets = 1U;
	pool_create.poolSizeCount = 2U;
	pool_create.pPoolSizes = sizes;
	result = vkCreateDescriptorPool(context->device, &pool_create, NULL, &pool);
	if (result != VK_SUCCESS)
		return -1;
	memset(&set_allocate, 0, sizeof(set_allocate));
	set_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_allocate.descriptorPool = pool;
	set_allocate.descriptorSetCount = 1U;
	set_allocate.pSetLayouts = &set_layout;
	result = vkAllocateDescriptorSets(context->device, &set_allocate, &set);
	if (result != VK_SUCCESS)
		return -1;
	buffer_info.buffer = context->uniforms;
	buffer_info.offset = 0U;
	buffer_info.range = RUN_UNIFORM_BYTES;
	pattern_info.buffer = context->pattern;
	pattern_info.offset = 0U;
	pattern_info.range = RUN_UNIFORM_BYTES;
	image_info.sampler = context->sampler;
	image_info.imageView = context->texture_view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(writes, 0, sizeof(writes));
	for (index = 0U; index < count; index++) {
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = set;
		writes[index].dstBinding = bindings[index].binding;
		writes[index].descriptorCount = 1U;
		writes[index].descriptorType = bindings[index].descriptorType;
		writes[index].pBufferInfo = &buffer_info;
		if (bindings[index].binding != 0U)
			writes[index].pBufferInfo = &pattern_info;
		writes[index].pImageInfo = &image_info;
	}

	/* The set written. */
	vkUpdateDescriptorSets(context->device, count, writes, 0U, NULL);

	/* The pipeline: a strip of the four corners at the position's location. */
	position_location = 0U;
	for (index = 0U; index < spirv[0].input_count; index++) {
		differs = strcmp(spirv[0].inputs[index].name, "a_position");
		if (differs == 0)
			position_location = spirv[0].inputs[index].location;
	}

	/* The stages. */
	memset(stages, 0, sizeof(stages));
	for (stage = 0U; stage < 2U; stage++) {
		stages[stage].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[stage].stage = VK_SHADER_STAGE_VERTEX_BIT;
		if (stage == 1U)
			stages[stage].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[stage].module = modules[stage];
		stages[stage].pName = "main";
	}

	/* The vertices: two floats each at the position's location. */
	memset(&vertex_binding, 0, sizeof(vertex_binding));
	vertex_binding.stride = 8U;
	memset(&vertex_attribute, 0, sizeof(vertex_attribute));
	vertex_attribute.location = position_location;
	vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
	memset(&vertex_input, 0, sizeof(vertex_input));
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	if (spirv[0].input_count != 0U) {
		vertex_input.vertexBindingDescriptionCount = 1U;
		vertex_input.pVertexBindingDescriptions = &vertex_binding;
		vertex_input.vertexAttributeDescriptionCount = 1U;
		vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
	}

	/* The fixed state: a strip, the whole target, no culling, no blending. */
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)RUN_SIZE;
	viewport.height = (float)RUN_SIZE;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = RUN_SIZE;
	scissor.extent.height = RUN_SIZE;
	memset(&viewport_state, 0, sizeof(viewport_state));
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1U;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1U;
	viewport_state.pScissors = &scissor;
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
	blend_attachment.colorWriteMask = 0xfU;
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1U;
	blend.pAttachments = &blend_attachment;
	memset(&pipeline_create, 0, sizeof(pipeline_create));
	pipeline_create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_create.stageCount = 2U;
	pipeline_create.pStages = stages;
	pipeline_create.pVertexInputState = &vertex_input;
	pipeline_create.pInputAssemblyState = &assembly;
	pipeline_create.pViewportState = &viewport_state;
	pipeline_create.pRasterizationState = &raster;
	pipeline_create.pMultisampleState = &multisample;
	pipeline_create.pColorBlendState = &blend;
	pipeline_create.layout = layout;
	pipeline_create.renderPass = context->pass;
	result = vkCreateGraphicsPipelines(context->device, VK_NULL_HANDLE, 1U, &pipeline_create, NULL, &pipeline);
	if (result != VK_SUCCESS) {
		printf("%s: FAIL (pipeline %d)\n", name, (int)result);
		return -1;
	}

	/* The draw, and the copy to the readback buffer. */
	memcpy(context->vertices_mapped, quad, sizeof(quad));
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	(void)vkBeginCommandBuffer(context->commands, &begin);
	memset(&clear, 0, sizeof(clear));
	memset(&pass_begin, 0, sizeof(pass_begin));
	pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass_begin.renderPass = context->pass;
	pass_begin.framebuffer = context->framebuffer;
	pass_begin.renderArea.extent.width = RUN_SIZE;
	pass_begin.renderArea.extent.height = RUN_SIZE;
	pass_begin.clearValueCount = 1U;
	pass_begin.pClearValues = &clear;
	vkCmdBeginRenderPass(context->commands, &pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(context->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(context->commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0U, 1U, &set, 0U, NULL);
	offset = 0U;
	vkCmdBindVertexBuffers(context->commands, 0U, 1U, &context->vertices, &offset);
	vkCmdDraw(context->commands, 4U, 1U, 0U, 0U);
	vkCmdEndRenderPass(context->commands);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = RUN_SIZE;
	copy.imageExtent.height = RUN_SIZE;
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(context->commands, context->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, context->readback, 1U, &copy);
	(void)vkEndCommandBuffer(context->commands);
	status = run_submit(context);

	/* The objects of this test go. */
	vkDestroyPipeline(context->device, pipeline, NULL);
	vkDestroyDescriptorPool(context->device, pool, NULL);
	vkDestroyPipelineLayout(context->device, layout, NULL);
	vkDestroyDescriptorSetLayout(context->device, set_layout, NULL);
	vkDestroyShaderModule(context->device, modules[0], NULL);
	vkDestroyShaderModule(context->device, modules[1], NULL);
	gles_spirv_free(&spirv[0]);
	gles_spirv_free(&spirv[1]);
	if (status != 0)
		return -1;

	/* The comparison. */
	status = run_compare(name, context->readback_mapped, test);
	return status;
}

/* Writes the test's uniform values where a stage's reflection puts them. */
static void
run_fill_uniforms(
	struct run_context *context,
	const struct run_test *test,
	struct gles_spirv *spirv)
{
	const struct gles_uniform *uniform;
	unsigned char *data;
	unsigned index;
	unsigned value;
	unsigned element;
	unsigned column;
	unsigned row;
	unsigned at;
	float number;
	int32_t integer;
	int differs;

	/* Each uniform the test gives a value. */
	data = context->uniforms_mapped;
	for (index = 0U; index < spirv->uniform_count; index++) {
		uniform = &spirv->uniforms[index];
		if (uniform->sampler)
			continue;
		for (value = 0U; value < test->uniform_count; value++) {
			differs = strcmp(test->uniform_names[value], uniform->name);
			if (differs != 0)
				continue;

			/* The values in order: elements, columns, rows. */
			at = 0U;
			for (element = 0U; element < (unsigned)uniform->size; element++) {
				for (column = 0U; column < uniform->columns; column++) {
					for (row = 0U; row < uniform->components; row++) {
						if (at >= test->uniform_counts[value])
							break;
						number = test->uniform_values[value][at];
						integer = (int32_t)number;
						at++;
						if (uniform->base == 0U) {
							memcpy(data + uniform->offset + element * uniform->array_stride + column * uniform->matrix_stride + row * 4U, &number, 4U);
						} else {
							memcpy(data + uniform->offset + element * uniform->array_stride + column * uniform->matrix_stride + row * 4U, &integer, 4U);
						}
					}
				}
			}
		}
	}
}

/* Compares every pixel with the expected colour (within 2); prints the outcome. */
static int
run_compare(
	const char *name,
	const unsigned char *pixels,
	const struct run_test *test)
{
	unsigned index;
	unsigned channel;
	int difference;

	/* Every pixel. */
	if (!test->has_expect) {
		printf("%s: FAIL (no expect line)\n", name);
		return -1;
	}

	/* Each channel of each pixel. */
	for (index = 0U; index < RUN_SIZE * RUN_SIZE; index++) {
		for (channel = 0U; channel < 4U; channel++) {
			difference = (int)pixels[index * 4U + channel] - (int)test->expect[channel];
			if (difference > 2 || difference < -2) {
				printf("%s: FAIL pixel %u is %u %u %u %u, expected %u %u %u %u\n", name, index, pixels[index * 4U],
				       pixels[index * 4U + 1U], pixels[index * 4U + 2U], pixels[index * 4U + 3U], test->expect[0],
				       test->expect[1], test->expect[2], test->expect[3]);
				return -1;
			}
		}
	}

	/* Succeeded. */
	printf("%s: ok\n", name);
	return 0;
}
