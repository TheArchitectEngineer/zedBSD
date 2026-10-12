/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the joined solid quads (ws211-p003 段 2,
 * userland/desktop/wayland/batch.c): a quad's six vertices carry its place
 * at the corners, its box, its colour with its opacity and its radius; one
 * draw takes at most 62 quads (the kernel's 16 KiB upload); another set or
 * screen draws those waiting first; a flush draws 6n vertices from the
 * first quad's offset and binds the corners again; a full buffer leaves
 * the quad to be drawn alone.
 */

#include "userland/desktop/wayland/compose.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What the fake command buffer took. */
static unsigned draws;
static uint32_t last_vertices;
static unsigned vertex_binds;
static VkBuffer last_buffer;
static VkDeviceSize last_offset;
static unsigned pushes;
static float last_screen[2];

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint p, VkPipeline q) { (void)c; (void)p; (void)q; }
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint p, VkPipelineLayout l, uint32_t f, uint32_t n, const VkDescriptorSet *s, uint32_t d, const uint32_t *o) { (void)c; (void)p; (void)l; (void)f; (void)n; (void)s; (void)d; (void)o; }

/* Takes the push constants and keeps the screen's. */
VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s, uint32_t offset, uint32_t size, const void *values)
{
	const float *floats;

	(void)c;
	(void)l;
	(void)s;
	assert(offset == 0U && size == KWL_PANEL_CONSTANTS * sizeof(float));
	floats = values;
	last_screen[0] = floats[20];
	last_screen[1] = floats[21];
	pushes++;
}

/* Takes a vertex buffer's bind. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t first, uint32_t count, const VkBuffer *buffers, const VkDeviceSize *offsets)
{
	(void)c;
	assert(first == 0U && count == 1U);
	vertex_binds++;
	last_buffer = buffers[0];
	last_offset = offsets[0];
}

/* Takes a draw. */
VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t first_instance)
{
	(void)c;
	assert(instances == 1U && first == 0U && first_instance == 0U);
	draws++;
	last_vertices = vertices;
}

/* The other commands a display list replays, which these cases do not record (ws213-p003). */
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *b, VkSubpassContents s) { (void)c; (void)b; (void)s; }
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) { (void)c; }
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer c, uint32_t f, uint32_t n, const VkViewport *v) { (void)c; (void)f; (void)n; (void)v; }
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer c, uint32_t f, uint32_t n, const VkRect2D *r) { (void)c; (void)f; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d, VkDependencyFlags f, uint32_t mn, const VkMemoryBarrier *m, uint32_t bn, const VkBufferMemoryBarrier *b, uint32_t in, const VkImageMemoryBarrier *i) { (void)c; (void)s; (void)d; (void)f; (void)mn; (void)m; (void)bn; (void)b; (void)in; (void)i; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl, uint32_t n, const VkImageCopy *r) { (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage i, VkImageLayout l, VkBuffer b, uint32_t n, const VkBufferImageCopy *r) { (void)c; (void)i; (void)l; (void)b; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer c, VkBuffer b, VkImage i, VkImageLayout l, uint32_t n, const VkBufferImageCopy *r) { (void)c; (void)b; (void)i; (void)l; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) { (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; (void)f; }
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer c, VkImage i, VkImageLayout l, const VkClearColorValue *v, uint32_t n, const VkImageSubresourceRange *r) { (void)c; (void)i; (void)l; (void)v; (void)n; (void)r; }

/* The making of the pipeline and buffer is not exercised here. */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *m) { (void)d; (void)i; (void)a; (void)m; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d, VkShaderModule m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d; (void)b; (void)a; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *b) { (void)d; (void)i; (void)a; (void)b; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *r) { (void)d; (void)b; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *m) { (void)p; (void)m; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *m) { (void)d; (void)i; (void)a; (void)m; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) { (void)d; (void)b; (void)m; (void)o; return VK_ERROR_INITIALIZATION_FAILED; }
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p) { (void)d; (void)m; (void)o; (void)s; (void)f; (void)p; return VK_ERROR_INITIALIZATION_FAILED; }
VkResult kwl_compose_pipeline_input(struct kwl_compose *c, VkShaderModule v, VkShaderModule f, VkPipelineLayout l, const VkPipelineVertexInputStateCreateInfo *i, VkPipeline *p) { (void)c; (void)v; (void)f; (void)l; (void)i; (void)p; return VK_ERROR_INITIALIZATION_FAILED; }

/* Fills a solid quad's push constants as glass.c does. */
static void
quad(
	float *constants,
	float x0,
	float y0,
	float x1,
	float y1,
	float opacity)
{
	unsigned index;

	/* The corners in normalized device coordinates, the box, the colour, the radius, the screen, the opacity. */
	for (index = 0U; index < KWL_PANEL_CONSTANTS; index++)
		constants[index] = 0.0f;
	constants[0] = x0;
	constants[1] = y0;
	constants[2] = x1;
	constants[3] = y1;
	constants[8] = 10.0f;
	constants[9] = 20.0f;
	constants[10] = 30.0f;
	constants[11] = 1.0f;
	constants[12] = 0.86f;
	constants[13] = 0.90f;
	constants[14] = 1.0f;
	constants[15] = 0.1f;
	constants[16] = 0.0f;
	constants[17] = 3.0f;
	constants[20] = 1920.0f;
	constants[21] = 1080.0f;
	constants[23] = opacity;
}

/* How many of the list's operations were replayed into the fake command buffer. */
static size_t replayed;

/*
 * Replays the operations the list took since the last replay into the
 * fake command buffer (the drawing writes a display list since
 * ws213-p003), so that each case sees the commands it caused.
 */
static void
replay_new(
	const struct kwl_list *list)
{
	struct kwl_list part;

	/* The same list from its first operation not replayed yet. */
	part = *list;
	part.ops = list->ops + replayed;
	part.op_count = list->op_count - replayed;
	kwl_list_replay(&part, (VkCommandBuffer)(uintptr_t)0x10U);
	replayed = list->op_count;
}

/* Joins a quad through kwl_batch_add and replays what it recorded. */
static int
test_add(
	struct kwl_server *server,
	struct kwl_list *list,
	const float *constants,
	VkDescriptorSet set)
{
	int joined;

	/* The quad, then its commands. */
	joined = kwl_batch_add(server, list, constants, set);
	replay_new(list);

	/* Whether it was joined. */
	return joined;
}

/* Draws the quads waiting through kwl_batch_flush and replays what it recorded. */
static void
test_flush(
	struct kwl_server *server,
	struct kwl_list *list)
{
	/* The draw, then its commands. */
	kwl_batch_flush(server, list);
	replay_new(list);
}

/* Runs the cases. */
int
main(
	void)
{
	static struct kwl_server server;
	static struct kwl_compose compose;
	float constants[KWL_PANEL_CONSTANTS];
	const float *vertex;
	static struct kwl_list list;
	VkDescriptorSet set;
	VkDescriptorSet other;
	unsigned index;
	int joined;

	/* A ready batch over a host buffer. */
	memset(&server, 0, sizeof(server));
	memset(&compose, 0, sizeof(compose));
	server.compose = &compose;
	compose.batch.ready = 1U;
	compose.batch.map = calloc(1U, 1024U * 1024U);
	assert(compose.batch.map != NULL);
	compose.batch.buffer = (VkBuffer)(uintptr_t)0x50U;
	compose.corners = (VkBuffer)(uintptr_t)0x51U;
	set = (VkDescriptorSet)(uintptr_t)0x40U;
	other = (VkDescriptorSet)(uintptr_t)0x41U;
	kwl_batch_begin(&compose);

	/* One quad: six vertices at its corners, its values in each, its opacity in its colour's alpha. */
	quad(constants, -1.0f, -0.5f, 0.0f, 0.5f, 0.5f);
	joined = test_add(&server, &list, constants, set);
	assert(joined == 1 && draws == 0U && compose.batch.count == 1U);
	vertex = (const float *)list.vertices;
	assert(vertex[0] == -1.0f && vertex[1] == -0.5f);
	assert(vertex[11 * 1 + 0] == 0.0f && vertex[11 * 1 + 1] == -0.5f);
	assert(vertex[11 * 5 + 0] == 0.0f && vertex[11 * 5 + 1] == 0.5f);
	for (index = 0U; index < 6U; index++) {
		assert(vertex[11 * index + 2] == 10.0f && vertex[11 * index + 5] == 1.0f);
		assert(vertex[11 * index + 6] == 0.86f && vertex[11 * index + 9] == 0.1f * 0.5f && vertex[11 * index + 10] == 0.0f);
	}

	/* The flush: one draw of six vertices from the start, the screen pushed, the corners bound again. */
	test_flush(&server, &list);
	assert(draws == 1U && last_vertices == 6U && pushes == 1U && last_screen[0] == 1920.0f && last_screen[1] == 1080.0f);
	assert(vertex_binds == 2U && last_buffer == compose.corners && last_offset == 0U);

	/* 62 quads fill a draw; the 63rd draws them first. */
	for (index = 0U; index < 63U; index++)
		(void)test_add(&server, &list, constants, set);
	assert(draws == 2U && last_vertices == 6U * 62U && compose.batch.count == 1U);
	assert(62U * 6U * 11U * 4U <= 16384U);

	/* Another set draws those waiting first, from where they start. */
	(void)test_add(&server, &list, constants, other);
	assert(draws == 3U && last_vertices == 6U && compose.batch.count == 1U);

	/* Another screen too. */
	constants[20] = 1280.0f;
	(void)test_add(&server, &list, constants, other);
	assert(draws == 4U && compose.batch.count == 1U);
	test_flush(&server, &list);
	assert(draws == 5U && last_screen[0] == 1280.0f);

	/* A full buffer leaves the quad to be drawn alone. */
	compose.batch.used = 1024U * 1024U - 10U;
	joined = test_add(&server, &list, constants, other);
	assert(joined == 0 && compose.batch.overflowed == 1U);

	/* Nothing waiting draws nothing. */
	test_flush(&server, &list);
	assert(draws == 5U);
	free(compose.batch.map);
	kwl_list_free(&list);
	puts("ws211 joined solid quads: corners, values, 62 a draw, set and screen breaks, overflow: PASS");
	return 0;
}
