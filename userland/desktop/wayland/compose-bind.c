/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The binds of the frame being recorded (ws211-p003 段 1): a pipeline and a
 * descriptor set are bound into the display list only when they differ
 * from what it has bound since its recording or its last pass began, and
 * the commands recorded are counted for KWL DRAWS (review-1 M2).  A set is
 * the same binding only with the same pipeline layout (review-1 M1).
 */

#include "compose.h"

/*
 * Binds a pipeline and a descriptor set (set 0, with the pipeline layout it
 * is used with) into the display list, each only when it differs from what
 * is bound (ws211-p003 段 1); the commands are counted for the log.
 */
void
kwl_compose_bind(
	struct kwl_server *server,
	struct kwl_list *command,
	VkPipeline pipeline,
	VkPipelineLayout layout,
	VkDescriptorSet set)
{
	struct kwl_compose *compose;
	unsigned pipelines;
	unsigned sets;

	/* The pipeline, when another is bound. */
	compose = server->compose;
	pipelines = 0U;
	if (pipeline != compose->bound_pipeline) {
		kwl_list_bind_pipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		compose->bound_pipeline = pipeline;
		pipelines = 1U;
	}

	/* The set, when another is bound or it was bound with another layout (review-1 M1). */
	sets = 0U;
	if (set != compose->bound_set || layout != compose->bound_layout) {
		kwl_list_bind_descriptor_sets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0U, 1U, &set, 0U, NULL);
		compose->bound_set = set;
		compose->bound_layout = layout;
		sets = 1U;
	}

	/* Counted for the log. */
	if (server->log_frames) {
		server->draw_counts.cmd_pipelines += pipelines;
		server->draw_counts.cmd_sets += sets;
	}
}

/*
 * Forgets what the display list has bound (its drawing or a pass began,
 * or a set was given back): the next draw binds its pipeline and set.
 */
void
kwl_compose_bind_forget(
	struct kwl_compose *compose)
{
	/* Nothing is known as bound. */
	compose->bound_pipeline = VK_NULL_HANDLE;
	compose->bound_layout = VK_NULL_HANDLE;
	compose->bound_set = VK_NULL_HANDLE;
}

/* Counts push constants and draws recorded, for the log (KWL DRAWS cmds). */
void
kwl_compose_count_commands(
	struct kwl_server *server,
	unsigned pushes,
	unsigned draws)
{
	/* Only with --log-frames. */
	if (!server->log_frames)
		return;
	server->draw_counts.cmd_pushes += pushes;
	server->draw_counts.cmd_draws += draws;
}
