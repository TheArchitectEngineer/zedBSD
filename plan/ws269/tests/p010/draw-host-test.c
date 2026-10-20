/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269-p010i (render-draw.c): vkdemo's and vkstage's
 * pipelines through the fixed state and keys, their shaders compiled by
 * the executor's compiler with those keys, the coefficient program and
 * output select between them, the viewport and scissor words, a draw's
 * primitive state block and its vertex control stream with and without
 * an index buffer, against golden words the one-time M9 comparison with
 * Mesa 26.2.5's csbgen packing confirmed (phase010i/phase.md), and the
 * refusals.  -v prints the words.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/powervr/render-draw.h"
#include "drivers/gpu/powervr/render-pass.h"
#include "drivers/gpu/powervr/shader.h"
#include "userland/tests/vkdemo/shaders.h"
#include "userland/tests/vkstage/shader.h"

static unsigned passed;
static unsigned failed;
static int verbose;

static void
check(
	int ok,
	const char *name)
{
	if (ok) {
		passed++;
	} else {
		failed++;
		printf("FAIL %s\n", name);
	}
}

static void
dump(
	const char *name,
	const uint32_t *words,
	uint32_t count)
{
	uint32_t index;

	if (!verbose)
		return;
	printf("%s:", name);
	for (index = 0; index < count; index++)
		printf(" 0x%08x", words[index]);
	printf("\n");
}

/* One pipeline: the create info's parts. */
struct pipeline {
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attributes[2];
	VkPipelineVertexInputStateCreateInfo input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkViewport viewport;
	VkRect2D scissor;
	VkPipelineViewportStateCreateInfo viewport_state;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkGraphicsPipelineCreateInfo info;
};

/* A pipeline of two float attributes (sizes a and b) of a vertex of stride bytes, one colour, and a depth test when depth. */
static void
pipeline_init(
	struct pipeline *pipeline,
	uint32_t format_a,
	uint32_t format_b,
	uint32_t offset_b,
	uint32_t stride,
	int depth)
{
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	pipeline->stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	pipeline->binding.stride = stride;
	pipeline->attributes[0].format = format_a;
	pipeline->attributes[1].location = 1;
	pipeline->attributes[1].format = format_b;
	pipeline->attributes[1].offset = offset_b;
	pipeline->input.vertexBindingDescriptionCount = 1;
	pipeline->input.pVertexBindingDescriptions = &pipeline->binding;
	pipeline->input.vertexAttributeDescriptionCount = 2;
	pipeline->input.pVertexAttributeDescriptions = pipeline->attributes;
	pipeline->assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pipeline->viewport.width = 64.0f;
	pipeline->viewport.height = 64.0f;
	pipeline->viewport.maxDepth = 1.0f;
	pipeline->scissor.extent.width = 64;
	pipeline->scissor.extent.height = 64;
	pipeline->viewport_state.viewportCount = 1;
	pipeline->viewport_state.pViewports = &pipeline->viewport;
	pipeline->viewport_state.scissorCount = 1;
	pipeline->viewport_state.pScissors = &pipeline->scissor;
	pipeline->raster.polygonMode = VK_POLYGON_MODE_FILL;
	pipeline->raster.cullMode = VK_CULL_MODE_NONE;
	pipeline->raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	pipeline->raster.lineWidth = 1.0f;
	pipeline->multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	pipeline->depth.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
	pipeline->depth.depthWriteEnable = depth ? VK_TRUE : VK_FALSE;
	pipeline->depth.depthCompareOp = VK_COMPARE_OP_LESS;
	pipeline->blend_attachment.colorWriteMask = 0xf;
	pipeline->blend.attachmentCount = 1;
	pipeline->blend.pAttachments = &pipeline->blend_attachment;
	pipeline->info.stageCount = 2;
	pipeline->info.pStages = pipeline->stages;
	pipeline->info.pVertexInputState = &pipeline->input;
	pipeline->info.pInputAssemblyState = &pipeline->assembly;
	pipeline->info.pViewportState = &pipeline->viewport_state;
	pipeline->info.pRasterizationState = &pipeline->raster;
	pipeline->info.pMultisampleState = &pipeline->multisample;
	pipeline->info.pDepthStencilState = &pipeline->depth;
	pipeline->info.pColorBlendState = &pipeline->blend;
}

static struct powervr_shader_workspace *workspace;
static uint8_t code[8192];

/* Compiles a shader with a key; reports 0 on success. */
static int
compile(
	const uint32_t *words,
	size_t bytes,
	uint32_t stage,
	const struct powervr_shader_key *key,
	struct powervr_shader_program *program)
{
	return powervr_shader_compile(words, bytes / 4U, stage, key, workspace, program, code, sizeof(code));
}

/* One pipeline end to end: fixed state, compiles, varyings, block and streams; returns 1 when all worked. */
static int
run(
	const char *name,
	struct pipeline *pipeline,
	const struct powervr_pass *pass,
	const uint32_t *vertex_words,
	size_t vertex_bytes,
	const uint32_t *fragment_words,
	size_t fragment_bytes,
	struct powervr_draw_varyings *varyings,
	uint32_t ppp[POWERVR_DRAW_PPP_WORDS],
	uint32_t *ppp_count,
	uint32_t vdm[POWERVR_DRAW_VDM_WORDS],
	uint32_t *vdm_count,
	int indexed)
{
	static struct powervr_shader_program vertex;
	static struct powervr_shader_program fragment;
	struct powervr_draw_fixed fixed;
	struct powervr_draw_ppp block;
	struct powervr_draw_vdm stream;
	char label[96];
	int error;

	/* The fixed state and the keys. */
	error = powervr_draw_fixed_build(&pipeline->info, pass, &fixed);
	snprintf(label, sizeof(label), "%s fixed state", name);
	check(error == 0, label);
	if (error != 0)
		return 0;

	/* The two shaders with their keys. */
	error = compile(vertex_words, vertex_bytes, POWERVR_SHADER_VERTEX, &fixed.vertex_key, &vertex);
	snprintf(label, sizeof(label), "%s vertex compiles with its key", name);
	check(error == 0, label);
	if (error != 0)
		return 0;
	error = compile(fragment_words, fragment_bytes, POWERVR_SHADER_FRAGMENT, &fixed.fragment_key, &fragment);
	snprintf(label, sizeof(label), "%s fragment compiles with its key", name);
	check(error == 0, label);
	if (error != 0)
		return 0;

	/* The varyings between them. */
	error = powervr_draw_varyings_build(&vertex, &fragment, varyings);
	snprintf(label, sizeof(label), "%s varyings", name);
	check(error == 0, label);
	if (error != 0)
		return 0;
	dump("coefficient data", varyings->data, varyings->data_words);
	dump("coefficient code", varyings->code, varyings->code_words);

	/* The block. */
	memset(&block, 0, sizeof(block));
	block.fixed = &fixed;
	block.varyings = varyings;
	block.has_depth = pass->depth != POWERVR_PASS_UNUSED;
	block.fragment_kick_offset = 0x4000;
	block.shared_code_offset = 0x4100;
	block.shared_data_offset = 0x4200;
	block.shared_data_words = 6;
	block.fragment_shareds = fragment.shareds;
	block.coefficient_offset = 0x4300;
	error = powervr_draw_viewport_words(&fixed.viewport, block.viewport);
	if (error == 0)
		error = powervr_draw_scissor_words(&fixed.viewport, &fixed.scissor, (uint32_t[2]){ 0, 0 }, block.region_clip);
	if (error == 0)
		error = powervr_draw_ppp_build(&block, ppp, ppp_count);
	snprintf(label, sizeof(label), "%s primitive state", name);
	check(error == 0, label);
	if (error != 0)
		return 0;
	dump("ppp", ppp, *ppp_count);

	/* The stream. */
	memset(&stream, 0, sizeof(stream));
	stream.ppp_address = 0x81000000ULL;
	stream.ppp_words = *ppp_count;
	stream.vertex_data_offset = 0x5000;
	stream.vertex_code_offset = 0x5100;
	stream.vertex_data_words = 16;
	stream.vertex_temps = 2;
	stream.shared_code_offset = 0x5200;
	stream.shared_data_offset = 0x5300;
	stream.shared_data_words = 3;
	stream.vertex_shareds = vertex.shareds;
	stream.vertex_inputs = vertex.vertex_inputs;
	stream.vertex_outputs = vertex.vertex_outputs;
	stream.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	stream.count = 36;
	stream.instances = 1;
	if (indexed) {
		stream.indexed = 1;
		stream.index_address = 0x82000000ULL;
		stream.index_bytes = 2;
		stream.vertex_offset = 4;
	}
	error = powervr_draw_vdm_build(&stream, vdm, vdm_count);
	snprintf(label, sizeof(label), "%s vertex stream", name);
	check(error == 0, label);
	if (error != 0)
		return 0;
	dump("vdm", vdm, *vdm_count);
	return 1;
}

int
main(
	int argc,
	char **argv)
{
	static const uint32_t demo_ppp[] = {
		0x007f0ee3U, 0x00010000U, 0x00020f00U, 0x00000000U, 0x00004000U, 0x00004100U, 0x01000411U, 0x00800000U,
		0x00004300U, 0x00004200U, 0x80000003U, 0x00000003U, 0x42000000U, 0x42000000U, 0x42000000U, 0x42000000U,
		0x00000000U, 0x3f800000U, 0x29e12e13U, 0x06010000U, 0x00000002U, 0x00000000U, 0x00000000U, 0x000006a0U,
		0x00000000U
	};
	static const uint32_t demo_vdm[] = {
		0x00001900U, 0x81000000U, 0x20010001U, 0x00005300U, 0x00005200U, 0x4c000f80U, 0x00005000U, 0x00005100U,
		0x00000006U, 0x20000844U, 0x68000600U, 0x00000024U
	};
	struct powervr_pass pass;
	struct powervr_draw_varyings varyings;
	struct powervr_draw_fixed fixed;
	struct pipeline pipeline;
	VkViewport viewport;
	VkRect2D scissor;
	uint32_t ppp[POWERVR_DRAW_PPP_WORDS];
	uint32_t vdm[POWERVR_DRAW_VDM_WORDS];
	uint32_t words[POWERVR_DRAW_VIEWPORT_WORDS];
	uint32_t scissor_words[2];
	uint32_t clip[2];
	uint32_t ppp_count;
	uint32_t vdm_count;
	int ok;

	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
	workspace = calloc(1, powervr_shader_workspace_size());
	if (workspace == NULL)
		return 2;

	/* A pass of one R8G8B8A8 colour and a D32 depth. */
	memset(&pass, 0, sizeof(pass));
	pass.attachment_count = 2;
	pass.attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	pass.attachments[1].format = VK_FORMAT_D32_SFLOAT;
	pass.color_count = 1;
	pass.colors[0] = 0;
	pass.depth = 1;

	/* vkdemo: position and texture coordinate (20 bytes), the sampled cuboid with the depth test. */
	pipeline_init(&pipeline, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 12, 20, 1);
	ok = run("vkdemo", &pipeline, &pass, vkdemo_vertex_shader, sizeof(vkdemo_vertex_shader), vkdemo_fragment_shader, sizeof(vkdemo_fragment_shader), &varyings, ppp, &ppp_count, vdm, &vdm_count, 0);
	check(ok && ppp_count == sizeof(demo_ppp) / 4U && memcmp(ppp, demo_ppp, sizeof(demo_ppp)) == 0, "vkdemo primitive state golden");
	check(ok && vdm_count == sizeof(demo_vdm) / 4U && memcmp(vdm, demo_vdm, sizeof(demo_vdm)) == 0, "vkdemo vertex stream golden");

	/* vkstage draw: position and colour (28 bytes), no depth; indexed. */
	pipeline_init(&pipeline, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT, 12, 28, 0);
	pass.depth = POWERVR_PASS_UNUSED;
	pass.attachment_count = 1;
	ok = run("vkstage colour", &pipeline, &pass, vkstage_colour_vert, sizeof(vkstage_colour_vert), vkstage_colour_frag, sizeof(vkstage_colour_frag), &varyings, ppp, &ppp_count, vdm, &vdm_count, 1);
	check(ok && varyings.code_words == 2 && (ppp[2] & (1U << 21)) != 0 && ((ppp[2] >> 17) & 7U) == VK_COMPARE_OP_ALWAYS, "vkstage colour: w and colour iterated, no depth test");
	check(ok && vdm[7] == 0x7a020600U && vdm[8] == 0x82000000U && vdm[9] == 36U && vdm[10] == 4U, "vkstage colour: the indexed list with its offset");

	/* vkstage sample: position and coordinate (16 bytes). */
	pipeline_init(&pipeline, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 8, 16, 0);
	pipeline.assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	ok = run("vkstage texture", &pipeline, &pass, vkstage_texture_vert, sizeof(vkstage_texture_vert), vkstage_texture_frag, sizeof(vkstage_texture_frag), &varyings, ppp, &ppp_count, vdm, &vdm_count, 0);
	check(ok, "vkstage texture");

	/* The viewport words of a 320 x 240 viewport at (8, 4), depth 0.25 to 0.75. */
	memset(&viewport, 0, sizeof(viewport));
	viewport.x = 8.0f;
	viewport.y = 4.0f;
	viewport.width = 320.0f;
	viewport.height = 240.0f;
	viewport.minDepth = 0.25f;
	viewport.maxDepth = 0.75f;
	ok = powervr_draw_viewport_words(&viewport, words) == 0;
	check(ok && words[0] == 0x43280000U && words[1] == 0x43200000U && words[2] == 0x42f80000U && words[3] == 0x42f00000U &&
	      words[4] == 0x3e800000U && words[5] == 0x3f000000U, "viewport words");

	/* A scissor inside it, and one that misses it. */
	scissor.offset.x = 20;
	scissor.offset.y = 0;
	scissor.extent.width = 100;
	scissor.extent.height = 50;
	ok = powervr_draw_scissor_words(&viewport, &scissor, scissor_words, clip) == 0;
	check(ok && scissor_words[0] == ((20U << 16) | 120U) && scissor_words[1] == ((4U << 16) | 50U) && clip[0] == (0x80000000U | (1U << 16) | 7U) && clip[1] == 3U, "scissor words");
	scissor.offset.x = 400;
	ok = powervr_draw_scissor_words(&viewport, &scissor, scissor_words, clip) == 0;
	check(ok && scissor_words[0] == 0 && scissor_words[1] == 0, "scissor that misses");

	/* Refusals: two viewports, a geometry stage, point lists, stencil. */
	pipeline_init(&pipeline, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 12, 20, 1);
	pipeline.viewport_state.viewportCount = 2;
	check(powervr_draw_fixed_build(&pipeline.info, &pass, &fixed) == ENOTSUP, "two viewports refused");
	pipeline_init(&pipeline, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 12, 20, 1);
	pipeline.stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
	check(powervr_draw_fixed_build(&pipeline.info, &pass, &fixed) == ENOTSUP, "geometry stage refused");
	pipeline_init(&pipeline, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 12, 20, 1);
	pipeline.assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	check(powervr_draw_fixed_build(&pipeline.info, &pass, &fixed) == ENOTSUP, "point list refused");
	pipeline_init(&pipeline, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, 12, 20, 1);
	pipeline.depth.stencilTestEnable = VK_TRUE;
	check(powervr_draw_fixed_build(&pipeline.info, &pass, &fixed) == ENOTSUP, "stencil refused");

	printf("render-draw host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
