/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the compositor's binds on change (ws211-p003 段 1,
 * userland/desktop/wayland/compose-bind.c): a pipeline or a set is bound
 * only when it differs; the same set with another pipeline layout is bound
 * again (review-1 M1); forgetting (a recording or a pass began, a set
 * given back) binds again; the commands are counted for the log.
 */

#include "userland/desktop/wayland/compose.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The binds the fake command buffer took. */
static unsigned bound_pipelines;
static unsigned bound_sets;
static VkPipelineLayout last_layout;
static VkDescriptorSet last_set;

/* Takes a pipeline bind. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(
	VkCommandBuffer command,
	VkPipelineBindPoint point,
	VkPipeline pipeline)
{
	(void)command;
	(void)point;
	(void)pipeline;
	bound_pipelines++;
}

/* Takes a set bind and the layout it was bound with. */
VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(
	VkCommandBuffer command,
	VkPipelineBindPoint point,
	VkPipelineLayout layout,
	uint32_t first,
	uint32_t count,
	const VkDescriptorSet *sets,
	uint32_t dynamic_count,
	const uint32_t *dynamic)
{
	(void)command;
	(void)point;
	(void)first;
	(void)count;
	(void)dynamic_count;
	(void)dynamic;
	bound_sets++;
	last_layout = layout;
	last_set = sets[0];
}

/* The other commands a display list replays, which these cases do not record (ws213-p003). */
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *b, VkSubpassContents s) { (void)c; (void)b; (void)s; }
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) { (void)c; }
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer c, uint32_t f, uint32_t n, const VkViewport *v) { (void)c; (void)f; (void)n; (void)v; }
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer c, uint32_t f, uint32_t n, const VkRect2D *r) { (void)c; (void)f; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s, uint32_t o, uint32_t n, const void *v) { (void)c; (void)l; (void)s; (void)o; (void)n; (void)v; }
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n, const VkBuffer *b, const VkDeviceSize *o) { (void)c; (void)f; (void)n; (void)b; (void)o; }
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i, uint32_t f, uint32_t g) { (void)c; (void)v; (void)i; (void)f; (void)g; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d, VkDependencyFlags f, uint32_t mn, const VkMemoryBarrier *m, uint32_t bn, const VkBufferMemoryBarrier *b, uint32_t in, const VkImageMemoryBarrier *i) { (void)c; (void)s; (void)d; (void)f; (void)mn; (void)m; (void)bn; (void)b; (void)in; (void)i; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl, uint32_t n, const VkImageCopy *r) { (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage i, VkImageLayout l, VkBuffer b, uint32_t n, const VkBufferImageCopy *r) { (void)c; (void)i; (void)l; (void)b; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer c, VkBuffer b, VkImage i, VkImageLayout l, uint32_t n, const VkBufferImageCopy *r) { (void)c; (void)b; (void)i; (void)l; (void)n; (void)r; }
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d, VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) { (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; (void)f; }
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer c, VkImage i, VkImageLayout l, const VkClearColorValue *v, uint32_t n, const VkImageSubresourceRange *r) { (void)c; (void)i; (void)l; (void)v; (void)n; (void)r; }

/*
 * Binds through kwl_compose_bind into a display list and replays what it
 * wrote into the fake command buffer (the drawing writes a list since
 * ws213-p003).
 */
static void
test_bind(
	struct kwl_server *server,
	struct kwl_list *list,
	VkPipeline pipeline,
	VkPipelineLayout layout,
	VkDescriptorSet set)
{
	/* The binds written, replayed, and the list emptied for the next. */
	kwl_compose_bind(server, list, pipeline, layout, set);
	kwl_list_replay(list, (VkCommandBuffer)(uintptr_t)0x10U);
	kwl_list_reset(list);
}

/* Runs the cases. */
int
main(
	void)
{
	static struct kwl_server server;
	static struct kwl_compose compose;
	static struct kwl_list list;
	VkPipeline quad_pipeline;
	VkPipeline glass_pipeline;
	VkPipelineLayout quad_layout;
	VkPipelineLayout panel_layout;
	VkDescriptorSet set;
	VkDescriptorSet other;

	/* Handles that only need to differ. */
	memset(&server, 0, sizeof(server));
	memset(&compose, 0, sizeof(compose));
	server.compose = &compose;
	server.log_frames = 1U;
	quad_pipeline = (VkPipeline)(uintptr_t)0x20U;
	glass_pipeline = (VkPipeline)(uintptr_t)0x21U;
	quad_layout = (VkPipelineLayout)(uintptr_t)0x30U;
	panel_layout = (VkPipelineLayout)(uintptr_t)0x31U;
	set = (VkDescriptorSet)(uintptr_t)0x40U;
	other = (VkDescriptorSet)(uintptr_t)0x41U;

	/* The first draw binds both; the same again binds nothing. */
	kwl_compose_bind_forget(&compose);
	test_bind(&server, &list, glass_pipeline, panel_layout, set);
	assert(bound_pipelines == 1U && bound_sets == 1U);
	test_bind(&server, &list, glass_pipeline, panel_layout, set);
	assert(bound_pipelines == 1U && bound_sets == 1U);

	/* Another set binds the set only. */
	test_bind(&server, &list, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 1U && bound_sets == 2U && last_set == other);

	/* The same set with the quad's layout (a window's quad after its glass) binds it again with that layout. */
	test_bind(&server, &list, quad_pipeline, quad_layout, other);
	assert(bound_pipelines == 2U && bound_sets == 3U && last_layout == quad_layout);
	test_bind(&server, &list, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 3U && bound_sets == 4U && last_layout == panel_layout);

	/* A pass or a recording begun: both are bound again. */
	kwl_compose_bind_forget(&compose);
	test_bind(&server, &list, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 4U && bound_sets == 5U);

	/* The counts for the log. */
	kwl_compose_count_commands(&server, 1U, 1U);
	assert(server.draw_counts.cmd_pipelines == 4U && server.draw_counts.cmd_sets == 5U);
	assert(server.draw_counts.cmd_pushes == 1U && server.draw_counts.cmd_draws == 1U);

	/* Without --log-frames nothing is counted, and the binds still are skipped. */
	server.log_frames = 0U;
	test_bind(&server, &list, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 4U && bound_sets == 5U && server.draw_counts.cmd_pipelines == 4U);
	kwl_list_free(&list);
	puts("ws211 compose binds on change, layout-keyed sets, forget, counts: PASS");
	return 0;
}
