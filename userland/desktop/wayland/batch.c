/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The glass look's small solid quads drawn together (ws211-p003 段 2,
 * plan/ws211/phase003/phase.md): instead of one draw a quad, each with its
 * own push constants, the quads that follow one another are written as
 * vertices (six a quad, each carrying the quad's place, rounded rectangle,
 * colour and radius) and drawn with one draw.  The order of drawing is
 * kept: only quads drawn one after another are joined, and anything else
 * recorded first draws the quads waiting (kwl_batch_flush).
 *
 * This first step joins only App Home's floor lines (glass.c joins a quad
 * only while server->batch_floors is set), so that its cost on the
 * Raspberry Pi 4 can be measured before more is joined: the kernel packs
 * each draw's vertices on the CPU, which more vertices make dearer, while
 * fewer draws make its per-draw preparation cheaper (review-1 H2).  A draw
 * is kept to BATCH_QUADS quads, whose packed vertices fit the kernel's
 * 16 KiB upload (review-1 N12, M8).  KWL_BATCH=0 in the environment, or a
 * device that cannot make the pipeline, draws every quad alone.
 */

#include "compose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders.h"

/* A vertex's floats: its place (2), its quad's rounded rectangle (4), colour (4) and radius (1). */
#define BATCH_FLOATS		11U
#define BATCH_STRIDE		(BATCH_FLOATS * (uint32_t)sizeof(float))

/* The quads one draw takes: 6 vertices of 11 floats each fit the kernel's 16 KiB upload 62 times. */
#define BATCH_QUADS		62U

/* The vertex buffer's size: the vertices of a frame's joined quads. */
#define BATCH_BYTES		(1024U * 1024U)

/* The output's push constants' byte offset of screen (panel.vert's sixth vec4). */
#define BATCH_SCREEN_OFFSET	80U

static VkResult batch_buffer(struct kwl_compose *compose);
static VkResult batch_pipeline(struct kwl_compose *compose);

/*
 * Makes the pipeline and the vertex buffer of the joined quads, after the
 * glass look's pipelines; without them every quad is drawn alone.
 */
void
kwl_batch_open(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	const char *setting;
	VkResult result;

	/* Off when asked for (the reference for the tests). */
	compose = server->compose;
	memset(&compose->batch, 0, sizeof(compose->batch));
	setting = getenv("KWL_BATCH");
	if (setting != NULL && strcmp(setting, "0") == 0) {
		printf("KWL BATCH off\n");
		return;
	}

	/* The pipeline, then the buffer. */
	result = batch_pipeline(compose);
	if (result == VK_SUCCESS)
		result = batch_buffer(compose);
	if (result != VK_SUCCESS) {
		kwl_batch_close(compose);
		printf("KWL BATCH unavailable result=%d\n", (int)result);
		return;
	}

	/* Succeeded. */
	compose->batch.ready = 1U;
	printf("KWL BATCH ready quads=%u bytes=%u\n", BATCH_QUADS, BATCH_BYTES);
}

/* Destroys the joined quads' pipeline and buffer (the device is idle). */
void
kwl_batch_close(
	struct kwl_compose *compose)
{
	/* Whatever was made. */
	if (compose->batch.pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(compose->device, compose->batch.pipeline, NULL);
	if (compose->batch.buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(compose->device, compose->batch.buffer, NULL);
	if (compose->batch.memory != VK_NULL_HANDLE)
		vkFreeMemory(compose->device, compose->batch.memory, NULL);
	memset(&compose->batch, 0, sizeof(compose->batch));
}

/* Starts a recording of compose->command: its joined quads' vertices from the buffer's start. */
void
kwl_batch_begin(
	struct kwl_compose *compose)
{
	/* The last recording completed before this one began. */
	compose->batch.used = 0U;
	compose->batch.first = 0U;
	compose->batch.count = 0U;
	compose->batch.overflowed = 0U;
}

/*
 * Joins a solid quad to the ones waiting, from its push constants
 * (glass.c's 24 floats, with the dark appearance's colour and the layer's
 * place already in them).  A quad of another screen or set, or one past
 * the draw's room, draws those waiting first.  Returns 1 when it was
 * joined, 0 when the caller draws it alone (no room in the buffer).
 */
int
kwl_batch_add(
	struct kwl_server *server,
	VkCommandBuffer command,
	const float *constants,
	VkDescriptorSet set)
{
	static const float corners[6][2] = {
		{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
		{ 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }
	};
	struct kwl_compose *compose;
	float vertex[BATCH_FLOATS];
	uint32_t bytes;
	unsigned index;

	/* Only with the batch, and room for the quad's six vertices. */
	compose = server->compose;
	if (!compose->batch.ready)
		return 0;
	bytes = 6U * BATCH_STRIDE;
	if (compose->batch.used + bytes > BATCH_BYTES) {
		kwl_batch_flush(server, command);
		if (!compose->batch.overflowed)
			printf("KWL BATCH overflow bytes=%u\n", compose->batch.used);
		compose->batch.overflowed = 1U;
		return 0;
	}

	/* Another screen or set, or a full draw: the quads waiting are drawn first. */
	if (compose->batch.count != 0U &&
	    (compose->batch.count == BATCH_QUADS ||
	     compose->batch.set != set ||
	     compose->batch.screen[0] != constants[20] ||
	     compose->batch.screen[1] != constants[21]))
		kwl_batch_flush(server, command);

	/* The first quad of a draw names its set and screen and where its vertices start. */
	if (compose->batch.count == 0U) {
		compose->batch.first = compose->batch.used;
		compose->batch.set = set;
		compose->batch.screen[0] = constants[20];
		compose->batch.screen[1] = constants[21];
	}

	/* The quad's values: its box, its colour with its opacity, its radius. */
	memcpy(&vertex[2], &constants[8], 4U * sizeof(float));
	memcpy(&vertex[6], &constants[12], 4U * sizeof(float));
	vertex[9] = constants[15] * constants[23];
	vertex[10] = constants[16];

	/* Six vertices at the quad's corners, written in order to the mapped buffer. */
	for (index = 0U; index < 6U; index++) {
		vertex[0] = constants[0] + (constants[2] - constants[0]) * corners[index][0];
		vertex[1] = constants[1] + (constants[3] - constants[1]) * corners[index][1];
		memcpy(compose->batch.map + compose->batch.used, vertex, BATCH_STRIDE);
		compose->batch.used += BATCH_STRIDE;
	}
	compose->batch.count++;

	/* Succeeded: the quad waits for the draw. */
	return 1;
}

/*
 * Draws the quads waiting with one draw: the batch's pipeline, a set the
 * layout needs (unused by its shaders), the screen, its vertices; then the
 * corners are bound again for every other draw (review-1 M5).
 */
void
kwl_batch_flush(
	struct kwl_server *server,
	VkCommandBuffer command)
{
	struct kwl_compose *compose;
	float constants[KWL_PANEL_CONSTANTS];
	VkDeviceSize offset;
	uint32_t count;

	/* Nothing waits. */
	compose = server->compose;
	count = compose->batch.count;
	if (count == 0U)
		return;
	compose->batch.count = 0U;

	/* The pipeline and the set with the glass look's layout, the screen in its push constants (all of them pushed, as every draw does). */
	kwl_compose_bind(server, command, compose->batch.pipeline, compose->panel_layout, compose->batch.set);
	memset(constants, 0, sizeof(constants));
	memcpy(&constants[BATCH_SCREEN_OFFSET / sizeof(float)], compose->batch.screen, sizeof(compose->batch.screen));
	vkCmdPushConstants(command, compose->panel_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, sizeof(constants), constants);

	/* The quads' vertices, then the corners again. */
	offset = compose->batch.first;
	vkCmdBindVertexBuffers(command, 0U, 1U, &compose->batch.buffer, &offset);
	vkCmdDraw(command, 6U * count, 1U, 0U, 0U);
	offset = 0U;
	vkCmdBindVertexBuffers(command, 0U, 1U, &compose->corners, &offset);
	kwl_compose_count_commands(server, 1U, 1U);
}

/* Makes the joined quads' pipeline: the glass look's layout and blending, the vertices batch.c writes. */
static VkResult
batch_pipeline(
	struct kwl_compose *compose)
{
	VkShaderModuleCreateInfo module;
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attributes[4];
	VkPipelineVertexInputStateCreateInfo input;
	VkShaderModule vertex;
	VkShaderModule fragment;
	VkResult result;

	/* The two shaders. */
	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = sizeof(kwl_batch_vert);
	module.pCode = kwl_batch_vert;
	result = vkCreateShaderModule(compose->device, &module, NULL, &vertex);
	if (result != VK_SUCCESS)
		return result;
	module.codeSize = sizeof(kwl_batch_frag);
	module.pCode = kwl_batch_frag;
	result = vkCreateShaderModule(compose->device, &module, NULL, &fragment);
	if (result != VK_SUCCESS) {
		vkDestroyShaderModule(compose->device, vertex, NULL);
		return result;
	}

	/* One buffer of vertices: the place, the box, the colour and the radius. */
	memset(&binding, 0, sizeof(binding));
	binding.binding = 0U;
	binding.stride = BATCH_STRIDE;
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	memset(attributes, 0, sizeof(attributes));
	attributes[0].location = 0U;
	attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
	attributes[0].offset = 0U;
	attributes[1].location = 1U;
	attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[1].offset = 2U * sizeof(float);
	attributes[2].location = 2U;
	attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	attributes[2].offset = 6U * sizeof(float);
	attributes[3].location = 3U;
	attributes[3].format = VK_FORMAT_R32_SFLOAT;
	attributes[3].offset = 10U * sizeof(float);
	memset(&input, 0, sizeof(input));
	input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	input.vertexBindingDescriptionCount = 1U;
	input.pVertexBindingDescriptions = &binding;
	input.vertexAttributeDescriptionCount = 4U;
	input.pVertexAttributeDescriptions = attributes;

	/* The pipeline; the modules only while it is made. */
	result = kwl_compose_pipeline_input(compose, vertex, fragment, compose->panel_layout, &input, &compose->batch.pipeline);
	vkDestroyShaderModule(compose->device, vertex, NULL);
	vkDestroyShaderModule(compose->device, fragment, NULL);
	return result;
}

/* Makes the vertex buffer: host-visible and coherent, mapped while the compositor runs (as the corners', review-1 L3). */
static VkResult
batch_buffer(
	struct kwl_compose *compose)
{
	VkPhysicalDeviceMemoryProperties memory;
	VkMemoryRequirements requirements;
	VkMemoryAllocateInfo allocate;
	VkBufferCreateInfo buffer;
	VkMemoryPropertyFlags wanted;
	uint32_t index;
	VkResult result;
	void *map;

	/* The buffer. */
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer.size = BATCH_BYTES;
	buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(compose->device, &buffer, NULL, &compose->batch.buffer);
	if (result != VK_SUCCESS)
		return result;

	/* Host-visible, coherent memory for it. */
	vkGetBufferMemoryRequirements(compose->device, compose->batch.buffer, &requirements);
	vkGetPhysicalDeviceMemoryProperties(compose->physical, &memory);
	wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (index = 0; index < memory.memoryTypeCount; index++) {
		if ((requirements.memoryTypeBits & (1U << index)) != 0U &&
		    (memory.memoryTypes[index].propertyFlags & wanted) == wanted)
			break;
	}
	if (index == memory.memoryTypeCount)
		return VK_ERROR_FORMAT_NOT_SUPPORTED;

	/* The memory, bound and mapped. */
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = index;
	result = vkAllocateMemory(compose->device, &allocate, NULL, &compose->batch.memory);
	if (result != VK_SUCCESS)
		return result;
	result = vkBindBufferMemory(compose->device, compose->batch.buffer, compose->batch.memory, 0U);
	if (result != VK_SUCCESS)
		return result;
	result = vkMapMemory(compose->device, compose->batch.memory, 0U, BATCH_BYTES, 0U, &map);
	if (result != VK_SUCCESS)
		return result;

	/* Succeeded. */
	compose->batch.map = map;
	compose->batch.capacity = BATCH_BYTES;
	return VK_SUCCESS;
}
