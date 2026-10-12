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

/* Runs the cases. */
int
main(
	void)
{
	static struct kwl_server server;
	static struct kwl_compose compose;
	VkCommandBuffer command;
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
	command = (VkCommandBuffer)(uintptr_t)0x10U;
	quad_pipeline = (VkPipeline)(uintptr_t)0x20U;
	glass_pipeline = (VkPipeline)(uintptr_t)0x21U;
	quad_layout = (VkPipelineLayout)(uintptr_t)0x30U;
	panel_layout = (VkPipelineLayout)(uintptr_t)0x31U;
	set = (VkDescriptorSet)(uintptr_t)0x40U;
	other = (VkDescriptorSet)(uintptr_t)0x41U;

	/* The first draw binds both; the same again binds nothing. */
	kwl_compose_bind_forget(&compose);
	kwl_compose_bind(&server, command, glass_pipeline, panel_layout, set);
	assert(bound_pipelines == 1U && bound_sets == 1U);
	kwl_compose_bind(&server, command, glass_pipeline, panel_layout, set);
	assert(bound_pipelines == 1U && bound_sets == 1U);

	/* Another set binds the set only. */
	kwl_compose_bind(&server, command, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 1U && bound_sets == 2U && last_set == other);

	/* The same set with the quad's layout (a window's quad after its glass) binds it again with that layout. */
	kwl_compose_bind(&server, command, quad_pipeline, quad_layout, other);
	assert(bound_pipelines == 2U && bound_sets == 3U && last_layout == quad_layout);
	kwl_compose_bind(&server, command, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 3U && bound_sets == 4U && last_layout == panel_layout);

	/* A pass or a recording begun: both are bound again. */
	kwl_compose_bind_forget(&compose);
	kwl_compose_bind(&server, command, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 4U && bound_sets == 5U);

	/* The counts for the log. */
	kwl_compose_count_commands(&server, 1U, 1U);
	assert(server.draw_counts.cmd_pipelines == 4U && server.draw_counts.cmd_sets == 5U);
	assert(server.draw_counts.cmd_pushes == 1U && server.draw_counts.cmd_draws == 1U);

	/* Without --log-frames nothing is counted, and the binds still are skipped. */
	server.log_frames = 0U;
	kwl_compose_bind(&server, command, glass_pipeline, panel_layout, other);
	assert(bound_pipelines == 4U && bound_sets == 5U && server.draw_counts.cmd_pipelines == 4U);
	puts("ws211 compose binds on change, layout-keyed sets, forget, counts: PASS");
	return 0;
}
