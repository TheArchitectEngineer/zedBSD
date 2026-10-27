/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * nggshape: one draw of a chosen shape through the public Vulkan 1.0 API,
 * to find which draws a vertex stage of the driver cannot finish
 * (ws291-p005, the RX 5600 XT): a grid of small triangles over a 256x256
 * B8G8R8A8 target, as a list of its own vertices or indexed (32-bit) over
 * shared ones, with or without a depth target, of one or more instances.
 * The submission is waited for (10 s at most); one line says the end:
 *
 *   NGGSHAPE ok after=<ms> pixel=<B8G8R8A8 word at the centre> drawn=<pixels not the clear colour> <shape>
 *   NGGSHAPE lost after=<ms> <shape>
 *   NGGSHAPE fail <step> <VkResult>
 *
 * usage: nggshape [--triangles=N] [--indexed] [--index16] [--depth] [--instances=N] [--draws=N] [--blit] [--outside] [--cross] [--cull] [--blend] [--heavy] [--mview] [--shuffle] [--tiny] [--allback] [--tail] [--cluster=P] [--mesh=FILE] [--repeat=N] [--texture] [--push=FILE]
 *
 * --index16 reads the indices as 16 bits; --draws splits the triangles into N draws; --blit then
 * blits the target into each level of a 9-level image (256x256 down to 1x1), linearly filtered;
 * --outside makes the grid three times the viewport (the clipper cuts its edge triangles); --cross
 * gives the vertices depths before the near and past the far plane in turn (the clipper cuts them);
 * --cull culls back faces and turns every other list cell around (half of the triangles are culled);
 * --blend blends with the target by the source alpha; --heavy draws with a vertex program of many
 * live values (heavy.vert); --mview draws with mview's own vertex program (its vertex layout of
 * place, normal and texture place, its per-instance offset, its push constants) and a pixel program
 * reading its two outputs; --shuffle (indexed) scatters the triangles' order over the index buffer;
 * --tiny shrinks each cell into a fifth of a pixel off its centre (no pixel drawn), --allback with --cull
 * leaves every cell facing away (all culled, a --tail cell turned to be drawn), --tail makes the last cell cover the whole target; --cluster=P (a list) puts every corner at
 * random within P pixels of the centre at a random depth (small triangles over each other); --mesh=FILE
 * draws the vertices (x y in clip space) and the triangles (a b c) of a file, indexed (32-bit); --repeat=N
 * submits the same command buffer N times more, each waited for; --texture (with --mview) draws with mview's own
 * pixel program, sampling a 9-level checker texture; --push=FILE takes mview's 32-number push block from a file.
 * The --mesh file starts "V T K": K numbers a vertex line, 2 (x y) or 8 (mview's place, normal, texture place).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

/* The programs, from nggshape/shape.vert and shape.frag (made by nggshape-build.sh). */
#include "nggshape-programs.h"

#define SHAPE_TARGET		256U
#define SHAPE_PIXEL		4U
#define SHAPE_WAIT_NS		UINT64_C(10000000000)
#define SHAPE_TRIANGLES_MAX	200000U
#define SHAPE_INSTANCES_MAX	32U

/* mview's vertex: place, normal, texture place (32 bytes), where its copy lies, and its push constants (eight vec4). */
#define SHAPE_MVIEW_STRIDE	32U
#define SHAPE_MVIEW_AT		0x600000U
#define SHAPE_MVIEW_PUSH_BYTES	128U

/* Where each resource lies in the one host-visible allocation. */
#define SHAPE_TARGET_AT		0x000000U
#define SHAPE_READBACK_AT	0x040000U
#define SHAPE_OFFSETS_AT	0x080000U
#define SHAPE_INDICES_AT	0x100000U
#define SHAPE_VERTICES_AT	0x400000U
#define SHAPE_STAGING_AT	0xf00000U
#define SHAPE_MEMORY_BYTES	0x1000000U

/* What one run draws. */
struct shape_options {
	uint32_t triangles;
	uint32_t instances;
	uint32_t draws;
	bool indexed;
	bool index16;
	bool depth;
	bool blit;
	bool outside;
	bool cross;
	bool cull;
	bool blend;
	bool heavy;
	bool mview;
	bool shuffle;
	bool tiny;
	bool allback;
	bool tail;
	uint32_t cluster;
	const char *mesh;
	uint32_t repeat;
	bool texture;
	const char *push;
};

/* The objects of the run, freed together at its end. */
struct shape {
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	VkDeviceMemory memory;
	VkDeviceMemory depth_memory;
	uint8_t *mapping;
	VkImage target;
	VkImageView view;
	VkImage depth;
	VkImage levels;
	VkDeviceMemory levels_memory;
	VkImageView depth_view;
	VkImageView texture_view;
	VkSampler sampler;
	VkBuffer staging;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet set;
	VkBuffer vertices;
	VkBuffer offsets;
	VkBuffer indices;
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
	uint32_t draw_count;
};

static VkResult shape_device(struct shape *shape);
static VkResult shape_texture(struct shape *shape);
static uint32_t shape_memory_type(const VkPhysicalDeviceMemoryProperties *memory, uint32_t mask, VkMemoryPropertyFlags wanted);
static VkResult shape_resources(struct shape *shape, const struct shape_options *options);
static VkResult shape_pipeline(struct shape *shape, const struct shape_options *options);
static VkResult shape_record(struct shape *shape, const struct shape_options *options);
static void shape_geometry(struct shape *shape, const struct shape_options *options);
static void shape_vertex(float *vertex, float x, float y, uint32_t number, const struct shape_options *options);
static void shape_mview_vertices(struct shape *shape, const struct shape_options *options, uint32_t count);
static uint64_t shape_now_ms(void);
static void shape_free(struct shape *shape);

int
main(
	int argc,
	char **argv)
{
	struct shape_options options;
	struct shape shape;
	VkFenceCreateInfo fence;
	VkSubmitInfo submit;
	VkResult status;
	uint64_t started;
	uint32_t repeat;
	uint32_t drawn;
	uint32_t pixel;
	char name[96];
	int argument;

	/* The arguments. */
	memset(&options, 0, sizeof(options));
	options.triangles = 2U;
	options.instances = 1U;
	options.draws = 1U;
	for (argument = 1; argument < argc; argument++) {
		if (strncmp(argv[argument], "--triangles=", 12) == 0) {
			options.triangles = (uint32_t)strtoul(argv[argument] + 12, NULL, 10);
		} else if (strncmp(argv[argument], "--instances=", 12) == 0) {
			options.instances = (uint32_t)strtoul(argv[argument] + 12, NULL, 10);
		} else if (strcmp(argv[argument], "--indexed") == 0) {
			options.indexed = true;
		} else if (strcmp(argv[argument], "--index16") == 0) {
			options.indexed = true;
			options.index16 = true;
		} else if (strncmp(argv[argument], "--draws=", 8) == 0) {
			options.draws = (uint32_t)strtoul(argv[argument] + 8, NULL, 10);
		} else if (strcmp(argv[argument], "--outside") == 0) {
			options.outside = true;
		} else if (strcmp(argv[argument], "--tiny") == 0) {
			options.tiny = true;
		} else if (strcmp(argv[argument], "--allback") == 0) {
			options.allback = true;
		} else if (strncmp(argv[argument], "--push=", 7) == 0) {
			options.push = argv[argument] + 7;
		} else if (strcmp(argv[argument], "--texture") == 0) {
			options.texture = true;
			options.mview = true;
		} else if (strncmp(argv[argument], "--repeat=", 9) == 0) {
			options.repeat = (uint32_t)strtoul(argv[argument] + 9, NULL, 10);
		} else if (strncmp(argv[argument], "--mesh=", 7) == 0) {
			options.mesh = argv[argument] + 7;
			options.indexed = true;
		} else if (strncmp(argv[argument], "--cluster=", 10) == 0) {
			options.cluster = (uint32_t)strtoul(argv[argument] + 10, NULL, 10);
		} else if (strcmp(argv[argument], "--tail") == 0) {
			options.tail = true;
		} else if (strcmp(argv[argument], "--shuffle") == 0) {
			options.indexed = true;
			options.shuffle = true;
		} else if (strcmp(argv[argument], "--mview") == 0) {
			options.mview = true;
		} else if (strcmp(argv[argument], "--heavy") == 0) {
			options.heavy = true;
		} else if (strcmp(argv[argument], "--blend") == 0) {
			options.blend = true;
		} else if (strcmp(argv[argument], "--cull") == 0) {
			options.cull = true;
		} else if (strcmp(argv[argument], "--cross") == 0) {
			options.cross = true;
		} else if (strcmp(argv[argument], "--blit") == 0) {
			options.blit = true;
		} else if (strcmp(argv[argument], "--depth") == 0) {
			options.depth = true;
		} else {
			fprintf(stderr, "usage: nggshape [--triangles=N] [--indexed] [--depth] [--instances=N]\n");
			return 2;
		}
	}

	/* Keeps the shape within the buffers. */
	if (options.triangles < 1U || options.triangles > SHAPE_TRIANGLES_MAX || options.instances < 1U || options.instances > SHAPE_INSTANCES_MAX ||
	    options.draws < 1U) {
		fprintf(stderr, "nggshape: 1 .. %u triangles, 1 .. %u instances\n", SHAPE_TRIANGLES_MAX, SHAPE_INSTANCES_MAX);
		return 2;
	}
	snprintf(name, sizeof(name), "cluster=%u tiny=%d allback=%d tail=%d shuffle=%d triangles=%u instances=%u indexed=%d index16=%d depth=%d draws=%u blit=%d outside=%d cross=%d cull=%d blend=%d heavy=%d mview=%d",
		 options.cluster, (int)options.tiny, (int)options.allback, (int)options.tail, (int)options.shuffle, options.triangles, options.instances, (int)options.indexed, (int)options.index16, (int)options.depth, options.draws,
		 (int)options.blit, (int)options.outside, (int)options.cross, (int)options.cull, (int)options.blend, (int)options.heavy, (int)options.mview);

	/* The device, the resources, the pipeline and the commands. */
	memset(&shape, 0, sizeof(shape));
	status = shape_device(&shape);
	if (status == VK_SUCCESS)
		status = shape_resources(&shape, &options);
	if (status == VK_SUCCESS)
		status = shape_pipeline(&shape, &options);
	if (status == VK_SUCCESS) {
		shape_geometry(&shape, &options);
		status = shape_record(&shape, &options);
	}
	if (status != VK_SUCCESS) {
		shape_free(&shape);
		return 1;
	}

	/* The submission, timed from here, and the wait. */
	memset(&fence, 0, sizeof(fence));
	fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	status = vkCreateFence(shape.device, &fence, NULL, &shape.fence);
	if (status != VK_SUCCESS) {
		printf("NGGSHAPE fail fence %d\n", (int)status);
		shape_free(&shape);
		return 1;
	}
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1U;
	submit.pCommandBuffers = &shape.command;
	started = shape_now_ms();
	status = vkQueueSubmit(shape.queue, 1U, &submit, shape.fence);
	if (status == VK_SUCCESS)
		status = vkWaitForFences(shape.device, 1U, &shape.fence, VK_TRUE, SHAPE_WAIT_NS);

	/* --repeat=N: the same submission N times more, each waited for. */
	for (repeat = 0U; status == VK_SUCCESS && repeat < options.repeat; repeat++) {
		status = vkResetFences(shape.device, 1U, &shape.fence);
		if (status == VK_SUCCESS)
			status = vkQueueSubmit(shape.queue, 1U, &submit, shape.fence);
		if (status == VK_SUCCESS)
			status = vkWaitForFences(shape.device, 1U, &shape.fence, VK_TRUE, SHAPE_WAIT_NS);
	}

	/* The end: lost, or ended with the pixel at the centre. */
	if (status == VK_ERROR_DEVICE_LOST || status == VK_TIMEOUT) {
		printf("NGGSHAPE lost after=%llu result=%d %s\n", (unsigned long long)(shape_now_ms() - started), (int)status, name);
		shape_free(&shape);
		return 0;
	}
	if (status != VK_SUCCESS) {
		printf("NGGSHAPE fail wait %d\n", (int)status);
		shape_free(&shape);
		return 1;
	}
	memcpy(&pixel, shape.mapping + SHAPE_READBACK_AT + ((SHAPE_TARGET / 2U) * SHAPE_TARGET + SHAPE_TARGET / 2U) * SHAPE_PIXEL, sizeof(pixel));
	drawn = 0U;
	for (repeat = 0U; repeat < SHAPE_TARGET * SHAPE_TARGET; repeat++) {
		if (((const uint32_t *)(const void *)(shape.mapping + SHAPE_READBACK_AT))[repeat] != 0xff0000ffU)
			drawn++;
	}
	printf("NGGSHAPE ok after=%llu pixel=%08x drawn=%u %s\n", (unsigned long long)(shape_now_ms() - started), (unsigned)pixel, drawn, name);
	shape_free(&shape);
	return 0;
}

/* Creates the instance and the device with a queue of family 0. */
static VkResult
shape_device(
	struct shape *shape)
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
	application.pApplicationName = "nggshape";
	application.apiVersion = VK_API_VERSION_1_0;
	memset(&instance, 0, sizeof(instance));
	instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance.pApplicationInfo = &application;
	status = vkCreateInstance(&instance, NULL, &shape->instance);
	if (status != VK_SUCCESS) {
		printf("NGGSHAPE fail instance %d\n", (int)status);
		return status;
	}

	/* The first device. */
	count = 8U;
	status = vkEnumeratePhysicalDevices(shape->instance, &count, physicals);
	if ((status != VK_SUCCESS && status != VK_INCOMPLETE) || count == 0U) {
		printf("NGGSHAPE fail devices %d\n", (int)status);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	shape->physical = physicals[0];

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
	status = vkCreateDevice(shape->physical, &device, NULL, &shape->device);
	if (status != VK_SUCCESS) {
		printf("NGGSHAPE fail device %d\n", (int)status);
		return status;
	}
	vkGetDeviceQueue(shape->device, 0U, 0U, &shape->queue);
	return VK_SUCCESS;
}

/* Reports the first memory type of a mask with the properties asked, or the count when there is none. */
static uint32_t
shape_memory_type(
	const VkPhysicalDeviceMemoryProperties *memory,
	uint32_t mask,
	VkMemoryPropertyFlags wanted)
{
	uint32_t type;

	/* Each type of the mask. */
	for (type = 0U; type < memory->memoryTypeCount; type++) {
		if ((mask & (1U << type)) == 0U)
			continue;
		if ((memory->memoryTypes[type].propertyFlags & wanted) == wanted)
			return type;
	}

	/* None. */
	return memory->memoryTypeCount;
}

/* The host-visible allocation with the target, the buffers and the readback; the depth target in memory of its own. */
static VkResult
shape_resources(
	struct shape *shape,
	const struct shape_options *options)
{
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryAllocateInfo allocate;
	VkMemoryRequirements requirements;
	VkImageCreateInfo image;
	VkImageViewCreateInfo view;
	VkBufferCreateInfo buffer;
	void *mapping;
	VkResult status;

	/* A host-visible coherent allocation, mapped. */
	vkGetPhysicalDeviceMemoryProperties(shape->physical, &memory);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = SHAPE_MEMORY_BYTES;
	allocate.memoryTypeIndex = shape_memory_type(&memory, 0xffffffffU, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	status = vkAllocateMemory(shape->device, &allocate, NULL, &shape->memory);
	if (status == VK_SUCCESS)
		status = vkMapMemory(shape->device, shape->memory, 0U, VK_WHOLE_SIZE, 0U, &mapping);
	if (status != VK_SUCCESS) {
		printf("NGGSHAPE fail memory %d\n", (int)status);
		return status;
	}
	shape->mapping = mapping;

	/* The colour target and its view. */
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = VK_FORMAT_B8G8R8A8_UNORM;
	image.extent.width = SHAPE_TARGET;
	image.extent.height = SHAPE_TARGET;
	image.extent.depth = 1U;
	image.mipLevels = 1U;
	image.arrayLayers = 1U;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	status = vkCreateImage(shape->device, &image, NULL, &shape->target);
	if (status == VK_SUCCESS)
		status = vkBindImageMemory(shape->device, shape->target, shape->memory, SHAPE_TARGET_AT);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = shape->target;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1U;
	view.subresourceRange.layerCount = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateImageView(shape->device, &view, NULL, &shape->view);

	/* The depth target in a memory of its own. */
	if (status == VK_SUCCESS && options->depth) {
		image.format = VK_FORMAT_D32_SFLOAT;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		status = vkCreateImage(shape->device, &image, NULL, &shape->depth);
		if (status == VK_SUCCESS) {
			vkGetImageMemoryRequirements(shape->device, shape->depth, &requirements);
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = shape_memory_type(&memory, requirements.memoryTypeBits, 0U);
			status = vkAllocateMemory(shape->device, &allocate, NULL, &shape->depth_memory);
		}
		if (status == VK_SUCCESS)
			status = vkBindImageMemory(shape->device, shape->depth, shape->depth_memory, 0U);
		view.image = shape->depth;
		view.format = VK_FORMAT_D32_SFLOAT;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (status == VK_SUCCESS)
			status = vkCreateImageView(shape->device, &view, NULL, &shape->depth_view);
	}

	/* The 9-level image the target is blitted into, in a memory of its own. */
	if (status == VK_SUCCESS && (options->blit || options->texture)) {
		image.format = VK_FORMAT_B8G8R8A8_UNORM;
		image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image.mipLevels = 9U;
		status = vkCreateImage(shape->device, &image, NULL, &shape->levels);
		if (status == VK_SUCCESS) {
			vkGetImageMemoryRequirements(shape->device, shape->levels, &requirements);
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = shape_memory_type(&memory, requirements.memoryTypeBits, 0U);
			status = vkAllocateMemory(shape->device, &allocate, NULL, &shape->levels_memory);
		}
		if (status == VK_SUCCESS)
			status = vkBindImageMemory(shape->device, shape->levels, shape->levels_memory, 0U);
	}

	/* The vertices, the instance offsets, the indices and the readback. */
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = SHAPE_MEMORY_BYTES - SHAPE_VERTICES_AT;
	buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(shape->device, &buffer, NULL, &shape->vertices);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(shape->device, shape->vertices, shape->memory, SHAPE_VERTICES_AT);
	buffer.size = SHAPE_INSTANCES_MAX * 16U;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(shape->device, &buffer, NULL, &shape->offsets);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(shape->device, shape->offsets, shape->memory, SHAPE_OFFSETS_AT);
	buffer.size = SHAPE_VERTICES_AT - SHAPE_INDICES_AT;
	buffer.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(shape->device, &buffer, NULL, &shape->indices);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(shape->device, shape->indices, shape->memory, SHAPE_INDICES_AT);
	/* With --texture: the levels' texels (one checker for all), the view of all levels, the sampler and the set. */
	if (status == VK_SUCCESS && options->texture)
		status = shape_texture(shape);
	buffer.size = SHAPE_TARGET * SHAPE_TARGET * SHAPE_PIXEL;
	buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (status == VK_SUCCESS)
		status = vkCreateBuffer(shape->device, &buffer, NULL, &shape->readback);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(shape->device, shape->readback, shape->memory, SHAPE_READBACK_AT);
	if (status != VK_SUCCESS)
		printf("NGGSHAPE fail resources %d\n", (int)status);
	return status;
}

/*
 * With --texture: a checker of 8-texel squares in the staging buffer (each
 * level copies its corner of it), the view of the 9 levels, a linear
 * sampler with linear levels, and the set of binding 0 with them.
 */
static VkResult
shape_texture(
	struct shape *shape)
{
	VkBufferCreateInfo buffer;
	VkImageViewCreateInfo view;
	VkSamplerCreateInfo sampler;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorImageInfo image;
	VkWriteDescriptorSet write;
	uint32_t *texels;
	uint32_t x;
	uint32_t y;
	VkResult status;

	/* The checker. */
	texels = (uint32_t *)(shape->mapping + SHAPE_STAGING_AT);
	for (y = 0U; y < SHAPE_TARGET; y++) {
		for (x = 0U; x < SHAPE_TARGET; x++)
			texels[y * SHAPE_TARGET + x] = (((x / 8U) + (y / 8U)) % 2U) != 0U ? 0xffffffffU : 0xff406080U;
	}
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = SHAPE_TARGET * SHAPE_TARGET * SHAPE_PIXEL;
	buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	status = vkCreateBuffer(shape->device, &buffer, NULL, &shape->staging);
	if (status == VK_SUCCESS)
		status = vkBindBufferMemory(shape->device, shape->staging, shape->memory, SHAPE_STAGING_AT);

	/* The view of every level. */
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = shape->levels;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 9U;
	view.subresourceRange.layerCount = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateImageView(shape->device, &view, NULL, &shape->texture_view);

	/* The sampler. */
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.maxLod = 8.0f;
	if (status == VK_SUCCESS)
		status = vkCreateSampler(shape->device, &sampler, NULL, &shape->sampler);

	/* The set's layout, its pool and the set. */
	memset(&binding, 0, sizeof(binding));
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1U;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset(&set_layout, 0, sizeof(set_layout));
	set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout.bindingCount = 1U;
	set_layout.pBindings = &binding;
	if (status == VK_SUCCESS)
		status = vkCreateDescriptorSetLayout(shape->device, &set_layout, NULL, &shape->set_layout);
	memset(&size, 0, sizeof(size));
	size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	size.descriptorCount = 1U;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1U;
	pool.poolSizeCount = 1U;
	pool.pPoolSizes = &size;
	if (status == VK_SUCCESS)
		status = vkCreateDescriptorPool(shape->device, &pool, NULL, &shape->descriptor_pool);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = shape->descriptor_pool;
	allocate.descriptorSetCount = 1U;
	allocate.pSetLayouts = &shape->set_layout;
	if (status == VK_SUCCESS)
		status = vkAllocateDescriptorSets(shape->device, &allocate, &shape->set);
	memset(&image, 0, sizeof(image));
	image.sampler = shape->sampler;
	image.imageView = shape->texture_view;
	image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = shape->set;
	write.descriptorCount = 1U;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	if (status == VK_SUCCESS)
		vkUpdateDescriptorSets(shape->device, 1U, &write, 0U, NULL);
	if (status != VK_SUCCESS)
		printf("NGGSHAPE fail texture %d\n", (int)status);
	return status;
}

/* The pass (cleared, kept), the framebuffer and the pipeline. */
static VkResult
shape_pipeline(
	struct shape *shape,
	const struct shape_options *options)
{
	VkShaderModuleCreateInfo module;
	VkPipelineLayoutCreateInfo layout;
	VkPushConstantRange range;
	VkAttachmentDescription attachments[2];
	VkVertexInputAttributeDescription mview_attributes[4];
	VkAttachmentReference colour_reference;
	VkAttachmentReference depth_reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo pass;
	VkFramebufferCreateInfo framebuffer;
	VkImageView views[2];
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription bindings[2];
	VkVertexInputAttributeDescription attributes[2];
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkViewport viewport_value;
	VkRect2D scissor_value;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkGraphicsPipelineCreateInfo create;
	VkResult status;

	/* The two modules and an empty layout. */
	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = options->heavy ? sizeof(nggshape_heavy_vert) : sizeof(nggshape_shape_vert);
	module.pCode = options->heavy ? nggshape_heavy_vert : nggshape_shape_vert;
	if (options->mview) {
		module.codeSize = sizeof(nggshape_mview_vert);
		module.pCode = nggshape_mview_vert;
	}
	status = vkCreateShaderModule(shape->device, &module, NULL, &shape->vertex_module);
	module.codeSize = options->mview ? sizeof(nggshape_mview_frag) : sizeof(nggshape_shape_frag);
	module.pCode = options->mview ? nggshape_mview_frag : nggshape_shape_frag;
	if (options->texture) {
		module.codeSize = sizeof(nggshape_mview_real_frag);
		module.pCode = nggshape_mview_real_frag;
	}
	if (status == VK_SUCCESS)
		status = vkCreateShaderModule(shape->device, &module, NULL, &shape->fragment_module);
	memset(&layout, 0, sizeof(layout));
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = SHAPE_MVIEW_PUSH_BYTES;
	if (options->mview) {
		layout.pushConstantRangeCount = 1U;
		layout.pPushConstantRanges = &range;
	}
	if (options->texture) {
		range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		layout.setLayoutCount = 1U;
		layout.pSetLayouts = &shape->set_layout;
	}
	if (status == VK_SUCCESS)
		status = vkCreatePipelineLayout(shape->device, &layout, NULL, &shape->layout);

	/* The pass: the colour target, and the depth target when asked. */
	memset(attachments, 0, sizeof(attachments));
	attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	attachments[1] = attachments[0];
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	colour_reference.attachment = 0U;
	colour_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	depth_reference.attachment = 1U;
	depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1U;
	subpass.pColorAttachments = &colour_reference;
	if (options->depth)
		subpass.pDepthStencilAttachment = &depth_reference;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	pass.attachmentCount = options->depth ? 2U : 1U;
	pass.pAttachments = attachments;
	pass.subpassCount = 1U;
	pass.pSubpasses = &subpass;
	if (status == VK_SUCCESS)
		status = vkCreateRenderPass(shape->device, &pass, NULL, &shape->pass);
	views[0] = shape->view;
	views[1] = shape->depth_view;
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = shape->pass;
	framebuffer.attachmentCount = pass.attachmentCount;
	framebuffer.pAttachments = views;
	framebuffer.width = SHAPE_TARGET;
	framebuffer.height = SHAPE_TARGET;
	framebuffer.layers = 1U;
	if (status == VK_SUCCESS)
		status = vkCreateFramebuffer(shape->device, &framebuffer, NULL, &shape->framebuffer);

	/* The stages. */
	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = shape->vertex_module;
	stages[0].pName = "main";
	stages[1] = stages[0];
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = shape->fragment_module;

	/* The vertex input: each vertex's place, each instance's offset. */
	memset(bindings, 0, sizeof(bindings));
	bindings[0].binding = 0U;
	bindings[0].stride = 16U;
	bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	bindings[1].binding = 1U;
	bindings[1].stride = 8U;
	bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
	memset(attributes, 0, sizeof(attributes));
	attributes[0].location = 0U;
	attributes[0].binding = 0U;
	attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[1].location = 1U;
	attributes[1].binding = 1U;
	attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 2U;
	input.pVertexBindingDescriptions = bindings;
	input.vertexAttributeDescriptionCount = 2U;
	input.pVertexAttributeDescriptions = attributes;

	/* mview's layout: place, normal and texture place in one binding of 32 bytes, the instance's offset in a vec4. */
	if (options->mview) {
		bindings[0].stride = SHAPE_MVIEW_STRIDE;
		bindings[1].stride = 16U;
		memset(mview_attributes, 0, sizeof(mview_attributes));
		mview_attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		mview_attributes[1].location = 1U;
		mview_attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		mview_attributes[1].offset = 12U;
		mview_attributes[2].location = 2U;
		mview_attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		mview_attributes[2].offset = 24U;
		mview_attributes[3].location = 3U;
		mview_attributes[3].binding = 1U;
		mview_attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		input.vertexAttributeDescriptionCount = 4U;
		input.pVertexAttributeDescriptions = mview_attributes;
	}

	/* A triangle list over the whole target, no culling, depth less when asked. */
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	memset(&viewport_value, 0, sizeof(viewport_value));
	viewport_value.width = (float)SHAPE_TARGET;
	viewport_value.height = (float)SHAPE_TARGET;
	viewport_value.maxDepth = 1.0f;
	memset(&scissor_value, 0, sizeof(scissor_value));
	scissor_value.extent.width = SHAPE_TARGET;
	scissor_value.extent.height = SHAPE_TARGET;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1U;
	viewport.pViewports = &viewport_value;
	viewport.scissorCount = 1U;
	viewport.pScissors = &scissor_value;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = options->cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	memset(&depth, 0, sizeof(depth));
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = options->depth ? VK_TRUE : VK_FALSE;
	depth.depthWriteEnable = options->depth ? VK_TRUE : VK_FALSE;
	depth.depthCompareOp = VK_COMPARE_OP_LESS;
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.colorWriteMask = 0xfU;
	if (options->blend) {
		blend_attachment.blendEnable = VK_TRUE;
		blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1U;
	blend.pAttachments = &blend_attachment;

	/* The pipeline. */
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
	create.layout = shape->layout;
	create.renderPass = shape->pass;
	if (status == VK_SUCCESS)
		status = vkCreateGraphicsPipelines(shape->device, VK_NULL_HANDLE, 1U, &create, NULL, &shape->pipeline);
	if (status != VK_SUCCESS)
		printf("NGGSHAPE fail pipeline %d\n", (int)status);
	return status;
}

/* The whole of a file, ended by a zero byte (NULL when it cannot be read); the caller frees it. */
static char *
shape_read(
	const char *path)
{
	FILE *file;
	char *text;
	size_t size;
	size_t got;

	file = fopen(path, "r");
	if (file == NULL)
		return NULL;
	size = 1U << 20;
	text = malloc(size + 1U);
	got = 0U;
	if (text != NULL)
		got = fread(text, 1U, size, file);
	fclose(file);
	if (text != NULL)
		text[got] = '\0';
	return text;
}

/* A number from -1 to 1 of a fixed sequence (a linear congruential generator). */
static float
shape_random(void)
{
	static uint32_t state = 12345U;

	state = state * 1664525U + 1013904223U;
	return (float)(state >> 8) / (float)(1U << 23) - 1.0f;
}

/*
 * Where triangle number of count triangles goes with --shuffle: a
 * multiplication by a number prime to the count, so neighbours lie far
 * apart and the vertex reuse inside a subgroup is small.
 */
static uint32_t
shape_shuffled(
	uint32_t number,
	uint32_t count)
{
	uint64_t factor;
	uint64_t a;
	uint64_t b;
	uint64_t rest;

	/* The first factor from 7919 on that is prime to the count. */
	for (factor = 7919U;; factor += 2U) {
		a = factor;
		b = count;
		while (b != 0U) {
			rest = a % b;
			a = b;
			b = rest;
		}
		if (a == 1U)
			break;
	}
	return (uint32_t)(((uint64_t)number * factor) % count);
}

/*
 * Writes the grid: square cells of two triangles each over the target in
 * clip space, as six vertices a cell or, indexed, as six indices into the
 * grid's shared corners; and each instance's offset (a small shift).
 */
static void
shape_geometry(
	struct shape *shape,
	const struct shape_options *options)
{
	float *vertices;
	uint32_t *indices;
	uint16_t *short_indices;
	uint32_t value;
	uint32_t slot;
	float *offsets;
	uint32_t cells;
	uint32_t side;
	uint32_t cell;
	uint32_t row;
	uint32_t column;
	uint32_t corner;
	float x0;
	float y0;
	float size;
	float place[6][2];

	/* The cells needed and the grid's side. */
	cells = (options->triangles + 1U) / 2U;
	side = 1U;
	while (side * side < cells)
		side++;
	size = 2.0f / (float)side;
	vertices = (float *)(shape->mapping + SHAPE_VERTICES_AT);
	indices = (uint32_t *)(shape->mapping + SHAPE_INDICES_AT);
	short_indices = (uint16_t *)(shape->mapping + SHAPE_INDICES_AT);
	offsets = (float *)(shape->mapping + SHAPE_OFFSETS_AT);

	/* The instances' shifts. */
	for (cell = 0U; cell < options->instances; cell++) {
		offsets[2U * cell] = 0.001f * (float)cell;
		offsets[2U * cell + 1U] = 0.0f;
	}

	/*
	 * With --mesh=FILE the vertices and triangles of a file: "V T", then V
	 * lines "x y" (clip space), then T lines "a b c".
	 */
	if (options->mesh != NULL) {
		char *text;
		char *cursor;
		float *full;
		uint32_t count;
		uint32_t triangle_count;
		uint32_t floats;
		uint32_t part;
		float x;
		float y;

		/* The counts: vertices, triangles, and the numbers a vertex line holds (2, or 8 in mview's layout). */
		shape->draw_count = 0U;
		text = shape_read(options->mesh);
		cursor = text;
		if (text != NULL) {
			count = (uint32_t)strtoul(cursor, &cursor, 10);
			triangle_count = (uint32_t)strtoul(cursor, &cursor, 10);
			floats = (uint32_t)strtoul(cursor, &cursor, 10);
		}
		if (text == NULL || count > 100000U || triangle_count > SHAPE_TRIANGLES_MAX || (floats != 2U && floats != 8U)) {
			printf("NGGSHAPE fail mesh %s\n", options->mesh);
			free(text);
			return;
		}
		full = calloc((size_t)count * 8U, sizeof(*full));
		if (full == NULL) {
			free(text);
			return;
		}
		for (cell = 0U; cell < count; cell++) {
			for (part = 0U; part < floats; part++)
				full[8U * cell + part] = strtof(cursor, &cursor);
			x = full[8U * cell];
			y = full[8U * cell + 1U];
			shape_vertex(vertices + 4U * cell, x, y, cell, options);
		}
		for (slot = 0U; slot < 3U * triangle_count; slot++)
			indices[slot] = (uint32_t)strtoul(cursor, &cursor, 10);
		free(text);
		shape->draw_count = slot;
		shape_mview_vertices(shape, options, count);

		/* Eight numbers a vertex: mview's own vertex as the file gives it. */
		if (floats == 8U && options->mview)
			memcpy(shape->mapping + SHAPE_VERTICES_AT + SHAPE_MVIEW_AT, full, (size_t)count * 8U * sizeof(*full));
		free(full);
		return;
	}

	/* Indexed: the shared corners, then six indices a cell. */
	if (options->indexed) {
		for (row = 0U; row <= side; row++) {
			for (column = 0U; column <= side; column++) {
				shape_vertex(vertices + 4U * (row * (side + 1U) + column), -1.0f + size * (float)column, -1.0f + size * (float)row,
					     row * (side + 1U) + column, options);
			}
		}
		for (cell = 0U; cell < cells; cell++) {
			row = cell / side;
			column = cell % side;
			for (corner = 0U; corner < 6U; corner++) {
				value = row * (side + 1U) + column;
				if (corner == 1U || corner == 3U)
					value = row * (side + 1U) + column + 1U;
				if (corner == 2U || corner == 5U)
					value = (row + 1U) * (side + 1U) + column;
				if (corner == 4U)
					value = (row + 1U) * (side + 1U) + column + 1U;
				slot = 6U * cell + corner;
				if (options->shuffle)
					slot = 3U * shape_shuffled(2U * cell + corner / 3U, 2U * cells) + corner % 3U;
				if (options->index16) {
					short_indices[slot] = (uint16_t)value;
				} else {
					indices[slot] = value;
				}
			}
		}
		shape->draw_count = options->triangles * 3U;
		shape_mview_vertices(shape, options, (side + 1U) * (side + 1U));
		return;
	}

	/* A list: six vertices a cell. */
	for (cell = 0U; cell < cells; cell++) {
		x0 = -1.0f + size * (float)(cell % side);
		y0 = -1.0f + size * (float)(cell / side);
		place[0][0] = x0;
		place[0][1] = y0;
		place[1][0] = x0 + size;
		place[1][1] = y0;
		place[2][0] = x0;
		place[2][1] = y0 + size;
		place[3][0] = x0 + size;
		place[3][1] = y0;
		place[4][0] = x0 + size;
		place[4][1] = y0 + size;
		place[5][0] = x0;
		place[5][1] = y0 + size;
		/*
		 * With --tiny each cell lies inside a fifth of one pixel, away from
		 * its centre (no pixel is covered); with --tail the last cell
		 * covers the whole target.
		 */
		if (options->tiny) {
			x0 = -1.0f + (2.0f / (float)SHAPE_TARGET) * ((float)(cell % SHAPE_TARGET) + 0.1f);
			y0 = -1.0f + (2.0f / (float)SHAPE_TARGET) * ((float)((cell / SHAPE_TARGET) % SHAPE_TARGET) + 0.1f);
			for (corner = 0U; corner < 6U; corner++) {
				place[corner][0] = x0 + (place[corner][0] - (-1.0f + size * (float)(cell % side))) / size * (0.4f / (float)SHAPE_TARGET);
				place[corner][1] = y0 + (place[corner][1] - (-1.0f + size * (float)(cell / side))) / size * (0.4f / (float)SHAPE_TARGET);
			}
		}
		if (options->tail && cell + 1U == cells) {
			for (corner = 0U; corner < 6U; corner++) {
				place[corner][0] = (corner == 1U || corner == 3U || corner == 4U) ? 1.0f : -1.0f;
				place[corner][1] = (corner == 2U || corner == 4U || corner == 5U) ? 1.0f : -1.0f;
			}
		}
		/* With --cull every other cell turns around (with --allback none but a --tail, so all others are culled): its corners 1 and 2, 4 and 5 swapped. */
		if (options->cull && (((cell % 2U) != 0U && !options->allback) || (options->allback && options->tail && cell + 1U == cells))) {
			x0 = place[1][0];
			place[1][0] = place[2][0];
			place[2][0] = x0;
			y0 = place[1][1];
			place[1][1] = place[2][1];
			place[2][1] = y0;
			x0 = place[4][0];
			place[4][0] = place[5][0];
			place[5][0] = x0;
			y0 = place[4][1];
			place[4][1] = place[5][1];
			place[5][1] = y0;
		}
		for (corner = 0U; corner < 6U; corner++)
			shape_vertex(vertices + 4U * (6U * cell + corner), place[corner][0], place[corner][1], 6U * cell + corner, options);
		/* With --cluster=P every corner lies at random within P pixels of the centre, at a random depth. */
		if (options->cluster != 0U) {
			for (corner = 0U; corner < 6U; corner++) {
				vertices[4U * (6U * cell + corner)] = shape_random() * (float)options->cluster * (2.0f / (float)SHAPE_TARGET);
				vertices[4U * (6U * cell + corner) + 1U] = shape_random() * (float)options->cluster * (2.0f / (float)SHAPE_TARGET);
				vertices[4U * (6U * cell + corner) + 2U] = 0.5f + 0.2f * shape_random();
			}
		}
	}
	shape->draw_count = options->triangles * 3U;
	shape_mview_vertices(shape, options, 6U * cells);
}

/*
 * With --mview, writes the vertices again in mview's layout after the
 * others: each place with the normal (0, 0, 1) and the place as the
 * texture place; and each instance's offset as a vec4.
 */
static void
shape_mview_vertices(
	struct shape *shape,
	const struct shape_options *options,
	uint32_t count)
{
	const float *source;
	float *target;
	float *offsets;
	uint32_t index;

	/* Only for --mview. */
	if (!options->mview)
		return;

	/* Each vertex in mview's layout. */
	source = (const float *)(shape->mapping + SHAPE_VERTICES_AT);
	target = (float *)(shape->mapping + SHAPE_VERTICES_AT + SHAPE_MVIEW_AT);
	for (index = 0U; index < count; index++) {
		target[8U * index] = source[4U * index];
		target[8U * index + 1U] = source[4U * index + 1U];
		target[8U * index + 2U] = source[4U * index + 2U];
		target[8U * index + 3U] = 0.0f;
		target[8U * index + 4U] = 0.0f;
		target[8U * index + 5U] = 1.0f;
		target[8U * index + 6U] = source[4U * index];
		target[8U * index + 7U] = source[4U * index + 1U];
	}

	/* Each instance's offset as a vec4. */
	offsets = (float *)(shape->mapping + SHAPE_OFFSETS_AT);
	for (index = 0U; index < options->instances; index++) {
		offsets[4U * index] = 0.001f * (float)index;
		offsets[4U * index + 1U] = 0.0f;
		offsets[4U * index + 2U] = 0.0f;
		offsets[4U * index + 3U] = 0.0f;
	}
}

/* Records the pass with the draw, then the copy of the target into the readback. */
static VkResult
shape_record(
	struct shape *shape,
	const struct shape_options *options)
{
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo allocate;
	VkCommandBufferBeginInfo begin;
	VkClearValue clears[2];
	VkRenderPassBeginInfo pass;
	VkBufferImageCopy copy;
	VkBuffer buffers[2];
	VkDeviceSize offsets[2];
	uint32_t per_draw;
	uint32_t first;
	uint32_t count;
	uint32_t level;
	VkImageMemoryBarrier barrier;
	VkImageBlit blit;
	float push[SHAPE_MVIEW_PUSH_BYTES / 4U];
	VkResult status;

	/* The pool and the command buffer. */
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	status = vkCreateCommandPool(shape->device, &pool, NULL, &shape->pool);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate.commandPool = shape->pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1U;
	if (status == VK_SUCCESS)
		status = vkAllocateCommandBuffers(shape->device, &allocate, &shape->command);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (status == VK_SUCCESS)
		status = vkBeginCommandBuffer(shape->command, &begin);
	if (status != VK_SUCCESS) {
		printf("NGGSHAPE fail commands %d\n", (int)status);
		return status;
	}

	/* With --texture: the checker into each level, then the levels ready for sampling. */
	if (options->texture) {
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = shape->levels;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 9U;
		barrier.subresourceRange.layerCount = 1U;
		vkCmdPipelineBarrier(shape->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL,
				     1U, &barrier);
		for (level = 0U; level < 9U; level++) {
			memset(&copy, 0, sizeof(copy));
			copy.bufferRowLength = SHAPE_TARGET;
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.mipLevel = level;
			copy.imageSubresource.layerCount = 1U;
			copy.imageExtent.width = SHAPE_TARGET >> level;
			copy.imageExtent.height = SHAPE_TARGET >> level;
			copy.imageExtent.depth = 1U;
			vkCmdCopyBufferToImage(shape->command, shape->staging, shape->levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &copy);
		}
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(shape->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, 0U, NULL, 0U,
				     NULL, 1U, &barrier);
	}

	/* The pass, cleared to blue and the far depth, with the draw. */
	memset(clears, 0, sizeof(clears));
	clears[0].color.float32[2] = 1.0f;
	clears[0].color.float32[3] = 1.0f;
	clears[1].depthStencil.depth = 1.0f;
	memset(&pass, 0, sizeof(pass));
	pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	pass.renderPass = shape->pass;
	pass.framebuffer = shape->framebuffer;
	pass.renderArea.extent.width = SHAPE_TARGET;
	pass.renderArea.extent.height = SHAPE_TARGET;
	pass.clearValueCount = options->depth ? 2U : 1U;
	pass.pClearValues = clears;
	vkCmdBeginRenderPass(shape->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(shape->command, VK_PIPELINE_BIND_POINT_GRAPHICS, shape->pipeline);
	buffers[0] = shape->vertices;
	buffers[1] = shape->offsets;
	offsets[0] = options->mview ? SHAPE_MVIEW_AT : 0U;
	offsets[1] = 0U;
	vkCmdBindVertexBuffers(shape->command, 0U, 2U, buffers, offsets);

	/* mview's view: the clip transform the identity, the normal rotation the identity, white. */
	if (options->mview) {
		memset(push, 0, sizeof(push));
		push[0] = 1.0f;
		push[5] = 1.0f;
		push[10] = 1.0f;
		push[15] = 1.0f;
		push[16] = 1.0f;
		push[21] = 1.0f;
		push[26] = 1.0f;
		push[28] = 1.0f;
		push[29] = 1.0f;
		push[30] = 1.0f;
		push[31] = 1.0f;
		/* --push=FILE: the 32 numbers of the block from a file. */
		if (options->push != NULL) {
			char *text;
			char *cursor;

			text = shape_read(options->push);
			cursor = text;
			for (count = 0U; text != NULL && count < SHAPE_MVIEW_PUSH_BYTES / 4U; count++)
				push[count] = strtof(cursor, &cursor);
			free(text);
		}
		vkCmdPushConstants(shape->command, shape->layout,
				   options->texture ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT, 0U,
				   SHAPE_MVIEW_PUSH_BYTES, push);
		if (options->texture)
			vkCmdBindDescriptorSets(shape->command, VK_PIPELINE_BIND_POINT_GRAPHICS, shape->layout, 0U, 1U, &shape->set, 0U, NULL);
	}
	if (options->indexed)
		vkCmdBindIndexBuffer(shape->command, shape->indices, 0U, options->index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
	per_draw = (shape->draw_count / 3U + options->draws - 1U) / options->draws * 3U;
	for (first = 0U; first < shape->draw_count; first += per_draw) {
		count = shape->draw_count - first;
		if (count > per_draw)
			count = per_draw;
		if (options->indexed) {
			vkCmdDrawIndexed(shape->command, count, options->instances, first, 0, 0U);
		} else {
			vkCmdDraw(shape->command, count, options->instances, first, 0U);
		}
	}
	vkCmdEndRenderPass(shape->command);

	/* The target blitted into each level. */
	if (options->blit) {
		memset(&barrier, 0, sizeof(barrier));
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = shape->levels;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 9U;
		barrier.subresourceRange.layerCount = 1U;
		vkCmdPipelineBarrier(shape->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, NULL, 0U, NULL,
				     1U, &barrier);
		for (level = 0U; level < 9U; level++) {
			memset(&blit, 0, sizeof(blit));
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.layerCount = 1U;
			blit.srcOffsets[1].x = (int32_t)SHAPE_TARGET;
			blit.srcOffsets[1].y = (int32_t)SHAPE_TARGET;
			blit.srcOffsets[1].z = 1;
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = level;
			blit.dstSubresource.layerCount = 1U;
			blit.dstOffsets[1].x = (int32_t)(SHAPE_TARGET >> level);
			blit.dstOffsets[1].y = (int32_t)(SHAPE_TARGET >> level);
			blit.dstOffsets[1].z = 1;
			vkCmdBlitImage(shape->command, shape->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shape->levels,
				       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &blit, VK_FILTER_LINEAR);
		}
	}

	/* The target into the readback. */
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1U;
	copy.imageExtent.width = SHAPE_TARGET;
	copy.imageExtent.height = SHAPE_TARGET;
	copy.imageExtent.depth = 1U;
	vkCmdCopyImageToBuffer(shape->command, shape->target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shape->readback, 1U, &copy);
	status = vkEndCommandBuffer(shape->command);
	if (status != VK_SUCCESS)
		printf("NGGSHAPE fail end %d\n", (int)status);
	return status;
}

/* Writes one vertex: x, y (three times as far out with --outside), its depth and w 1. */
static void
shape_vertex(
	float *vertex,
	float x,
	float y,
	uint32_t number,
	const struct shape_options *options)
{
	float scale;

	/* The place, the depth (before the near or past the far plane in turn with --cross), w. */
	scale = options->outside ? 3.0f : 1.0f;
	vertex[0] = x * scale;
	vertex[1] = y * scale;
	vertex[2] = 0.5f;
	if (options->cross)
		vertex[2] = (number % 2U) != 0U ? -0.5f : 1.5f;
	vertex[3] = 1.0f;
}

/* Reports the monotonic clock in milliseconds. */
static uint64_t
shape_now_ms(void)
{
	struct timespec now;

	/* The clock, in milliseconds. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Frees the run's objects (the device's first, the instance last). */
static void
shape_free(
	struct shape *shape)
{
	/* Nothing more when there is no device. */
	if (shape->device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(shape->device);
		if (shape->fence != VK_NULL_HANDLE)
			vkDestroyFence(shape->device, shape->fence, NULL);
		if (shape->pool != VK_NULL_HANDLE)
			vkDestroyCommandPool(shape->device, shape->pool, NULL);
		if (shape->pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(shape->device, shape->pipeline, NULL);
		if (shape->framebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(shape->device, shape->framebuffer, NULL);
		if (shape->pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(shape->device, shape->pass, NULL);
		if (shape->layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(shape->device, shape->layout, NULL);
		if (shape->fragment_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(shape->device, shape->fragment_module, NULL);
		if (shape->vertex_module != VK_NULL_HANDLE)
			vkDestroyShaderModule(shape->device, shape->vertex_module, NULL);
		if (shape->readback != VK_NULL_HANDLE)
			vkDestroyBuffer(shape->device, shape->readback, NULL);
		if (shape->indices != VK_NULL_HANDLE)
			vkDestroyBuffer(shape->device, shape->indices, NULL);
		if (shape->offsets != VK_NULL_HANDLE)
			vkDestroyBuffer(shape->device, shape->offsets, NULL);
		if (shape->vertices != VK_NULL_HANDLE)
			vkDestroyBuffer(shape->device, shape->vertices, NULL);
		if (shape->descriptor_pool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(shape->device, shape->descriptor_pool, NULL);
		if (shape->set_layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(shape->device, shape->set_layout, NULL);
		if (shape->sampler != VK_NULL_HANDLE)
			vkDestroySampler(shape->device, shape->sampler, NULL);
		if (shape->texture_view != VK_NULL_HANDLE)
			vkDestroyImageView(shape->device, shape->texture_view, NULL);
		if (shape->staging != VK_NULL_HANDLE)
			vkDestroyBuffer(shape->device, shape->staging, NULL);
		if (shape->depth_view != VK_NULL_HANDLE)
			vkDestroyImageView(shape->device, shape->depth_view, NULL);
		if (shape->depth != VK_NULL_HANDLE)
			vkDestroyImage(shape->device, shape->depth, NULL);
		if (shape->levels != VK_NULL_HANDLE)
			vkDestroyImage(shape->device, shape->levels, NULL);
		if (shape->levels_memory != VK_NULL_HANDLE)
			vkFreeMemory(shape->device, shape->levels_memory, NULL);
		if (shape->view != VK_NULL_HANDLE)
			vkDestroyImageView(shape->device, shape->view, NULL);
		if (shape->target != VK_NULL_HANDLE)
			vkDestroyImage(shape->device, shape->target, NULL);
		if (shape->depth_memory != VK_NULL_HANDLE)
			vkFreeMemory(shape->device, shape->depth_memory, NULL);
		if (shape->memory != VK_NULL_HANDLE)
			vkFreeMemory(shape->device, shape->memory, NULL);
		vkDestroyDevice(shape->device, NULL);
	}

	/* The instance. */
	if (shape->instance != VK_NULL_HANDLE)
		vkDestroyInstance(shape->instance, NULL);
}
