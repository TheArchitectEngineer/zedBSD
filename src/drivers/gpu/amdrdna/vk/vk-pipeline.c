/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's graphics pipelines.
 *
 * A pipeline's vertex and fragment modules are compiled for GFX10 by the
 * amdrdna compiler (compiler/gfx10-compile.c) when the pipeline is
 * created, and their code is copied into a run of the executor's GPU
 * memory the pipeline keeps, each program on a 256-byte boundary.  The
 * pipeline keeps the compiled programs too: their resource tables' layout
 * and their register values are what a draw writes.  It also keeps the
 * state a draw sends: the vertex input, the topology, the culling, the
 * colour attachment's blend, and the viewport and scissor when they are
 * not dynamic.
 *
 * The fragment program is compiled first: when it reads the primitive
 * number, the vertex program is compiled to export it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-objects.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

/* The most pipelines one create makes, and the most stages one pipeline lists. */
#define PIPELINE_CREATED_MAX		8U
#define PIPELINE_STAGES_MAX		4U

/* A program starts on this boundary of the pipeline's run (the code address's low 8 bits are not held). */
#define PIPELINE_PROGRAM_ALIGN		256U

/* The locations a vertex attribute may have. */
#define PIPELINE_LOCATIONS		16U

/* The two modules of a pipeline being made. */
struct pipeline_build {
	const struct amdrdna_vk_shader *vertex_module;
	const struct amdrdna_vk_shader *fragment_module;
};

static int pipeline_create(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int pipeline_decode(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct amdrdna_vk_pipeline *pipeline, struct pipeline_build *build);
static int pipeline_decode_stages(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct pipeline_build *build);
static int pipeline_decode_vertex_input(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct amdrdna_vk_pipeline *pipeline);
static void pipeline_decode_viewport(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct amdrdna_vk_pipeline *pipeline);
static void pipeline_decode_blend(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct amdrdna_vk_pipeline *pipeline);
static void pipeline_decode_dynamic(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct amdrdna_vk_pipeline *pipeline);
static int pipeline_compile(struct amdrdna_vk_session *session, struct amdrdna_vk_pipeline *pipeline, struct pipeline_build *build);
static int pipeline_compile_stage(const struct amdrdna_vk_shader *module, enum drv_gpu_shader_stage stage, const struct amdrdna_gfx10_compile_options *options, struct amdrdna_vk_program *program);
static int pipeline_bindings(const struct amdrdna_gfx10_program *program);
static void pipeline_program(struct amdrdna_vk_program *program, uint8_t *cpu, uint64_t va);
static uint32_t pipeline_program_bytes(const struct amdrdna_gfx10_program *program);
static int pipeline_compute(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int pipeline_destroy(struct amdrdna_vk_session *session, void *payload);
static void pipeline_float(uint32_t *destination, const float *source);

/*
 * Runs a pipeline command; *handled stays clear for an opcode of another
 * handler's.
 */
int
amdrdna_vk_pipeline_route(
	struct amdrdna_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	UNUSED_PARAMETER(requested);

	/* Picks the command, or leaves it to another handler. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_GRAPHICS_PIPELINES:
		error = pipeline_create(session, reader, reply);
		break;
	case GPU_OP_CREATE_COMPUTE_PIPELINES:
		error = pipeline_compute(reader, reply);
		break;
	case GPU_OP_DESTROY_PIPELINE:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_PIPELINE);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* Reports a command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran and wrote its reply. */
	return 0;
}

/*
 * vkCreateGraphicsPipelines: [device][cache][count][count]{create info}
 * [allocator][count][identities] -> [result][count][identities].
 *
 * Every record is decoded (one that does not decode leaves the stream
 * unreadable and fails the command); then each pipeline is compiled and
 * published, and on the first failure none is left and every identity is
 * answered as null.
 */
static int
pipeline_create(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct amdrdna_vk_pipeline *pipelines[PIPELINE_CREATED_MAX];
	struct pipeline_build builds[PIPELINE_CREATED_MAX];
	uint64_t identities[PIPELINE_CREATED_MAX];
	struct amdrdna_vk_object *object;
	uint64_t count;
	uint64_t index;
	uint64_t published;
	uint64_t answered;
	uint32_t result;
	int refused;
	int decoded;
	int error;

	/* How many pipelines follow, behind the device, the cache and the count's first form. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count == 0U || count > PIPELINE_CREATED_MAX)
		return EINVAL;

	/*
	 * Decodes every record into its pipeline: one that does not decode
	 * ends the command (the stream is unreadable after it), one with state
	 * the executor does not draw is read to its end and remembered.
	 */
	kern_memset(pipelines, 0, sizeof(pipelines));
	kern_memset(builds, 0, sizeof(builds));
	error = 0;
	refused = 0;
	for (index = 0U; index < count; index++) {
		pipelines[index] = kern_calloc(1U, sizeof(*pipelines[index]));
		if (pipelines[index] == NULL) {
			error = ENOMEM;
			break;
		}

		/* Each record decodes into an emptied scratch. */
		session->arena.used = 0U;
		decoded = pipeline_decode(session, reader, pipelines[index], &builds[index]);
		if (decoded == EINVAL) {
			error = EINVAL;
			break;
		}
		if (decoded != 0 && refused == 0)
			refused = decoded;
	}

	/* The identities behind the allocator, one per pipeline. */
	if (error == 0) {
		(void)drv_i915_wire_read_u64(reader);
		answered = drv_i915_wire_read_u64(reader);
		if (answered != count)
			error = EINVAL;
		for (index = 0U; error == 0 && index < count; index++)
			identities[index] = drv_i915_wire_read_u64(reader);
		if (reader->error != 0)
			error = EINVAL;
	}

	/* A command that could not be read to its end fails the frame. */
	if (error != 0) {
		for (index = 0U; index < count; index++) {
			if (pipelines[index] != NULL)
				(void)pipeline_destroy(session, pipelines[index]);
		}
		return error;
	}

	/* A refused record fails the create with its result. */
	error = refused;

	/* Compiles each pipeline's programs into its run. */
	for (index = 0U; error == 0 && index < count; index++)
		error = pipeline_compile(session, pipelines[index], &builds[index]);

	/* Publishes each one, up to the first failure. */
	published = 0U;
	for (index = 0U; error == 0 && index < count; index++) {
		error = amdrdna_vk_object_publish(session, I915_VK_OBJ_PIPELINE, identities[index], pipelines[index], pipeline_destroy, &object);
		if (error != 0)
			break;
		pipelines[index] = NULL;
		published++;
	}

	/* A create that failed leaves no pipeline. */
	if (error != 0) {
		kern_logf("amdrdna: vk: vkCreateGraphicsPipelines failed: %d\n", error);
		for (index = 0U; index < published; index++)
			(void)amdrdna_vk_object_remove(session, I915_VK_OBJ_PIPELINE, identities[index]);
	}

	/* What was not published goes. */
	for (index = 0U; index < count; index++) {
		if (pipelines[index] != NULL)
			(void)pipeline_destroy(session, pipelines[index]);
	}

	/* The result and the count, then each identity, or null for each when the create failed. */
	result = amdrdna_vk_result(error);
	drv_i915_wire_reply_u32(reply, result);
	drv_i915_wire_reply_u64(reply, count);
	for (index = 0U; index < count; index++) {
		answered = 0U;
		if (error == 0)
			answered = identities[index];
		drv_i915_wire_reply_u64(reply, answered);
	}

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Decodes one VkGraphicsPipelineCreateInfo as libvulkan's
 * pipeline_encode_graphics() writes it: the head and the stages, each state
 * record behind its presence word (rasterization always present), then
 * [layout][render pass][subpass][base pipeline][base index].
 *
 * Returns EINVAL when the record did not decode (the stream is then
 * unreadable), ENOTSUP for state the executor does not draw (the record is
 * still read to its end).
 */
static int
pipeline_decode(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct amdrdna_vk_pipeline *pipeline,
	struct pipeline_build *build)
{
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineTessellationStateCreateInfo tessellation;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	const struct amdrdna_vk_pipeline_layout *layout;
	uint64_t present;
	uint64_t layout_id;
	int refused;
	int error;

	/* The stages. */
	refused = 0;
	error = pipeline_decode_stages(session, reader, build);
	if (error == EINVAL)
		return error;
	if (error != 0)
		refused = error;

	/* The vertex input. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		error = pipeline_decode_vertex_input(session, reader, pipeline);
		if (error != 0 && refused == 0)
			refused = error;
	}

	/* The input assembly: the topology (a triangle list unless said). */
	pipeline->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		kern_memset(&assembly, 0, sizeof(assembly));
		i915_vkc_dec_VkPipelineInputAssemblyStateCreateInfo(reader, &session->arena, &assembly);
		pipeline->topology = assembly.topology;
	}

	/* The tessellation state, not used. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		kern_memset(&tessellation, 0, sizeof(tessellation));
		i915_vkc_dec_VkPipelineTessellationStateCreateInfo(reader, &session->arena, &tessellation);
	}

	/* The viewport and scissor. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		pipeline_decode_viewport(session, reader, pipeline);

	/* The rasterization: the culling. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		kern_memset(&raster, 0, sizeof(raster));
		i915_vkc_dec_VkPipelineRasterizationStateCreateInfo(reader, &session->arena, &raster);
		pipeline->cull_mode = raster.cullMode;
		pipeline->front_face = raster.frontFace;
		if (raster.rasterizerDiscardEnable != VK_FALSE && refused == 0)
			refused = ENOTSUP;
	}

	/* The multisample state: one sample only. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		kern_memset(&multisample, 0, sizeof(multisample));
		i915_vkc_dec_VkPipelineMultisampleStateCreateInfo(reader, &session->arena, &multisample);
		if (multisample.rasterizationSamples != VK_SAMPLE_COUNT_1_BIT && refused == 0)
			refused = ENOTSUP;
	}

	/* The depth state is read (no pass of the executor's has depth). */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		kern_memset(&depth, 0, sizeof(depth));
		i915_vkc_dec_VkPipelineDepthStencilStateCreateInfo(reader, &session->arena, &depth);
	}

	/* The colour blend; without it every channel is written unblended. */
	pipeline->write_mask = 0xfU;
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		pipeline_decode_blend(session, reader, pipeline);

	/* The dynamic state. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		pipeline_decode_dynamic(session, reader, pipeline);

	/* The layout (its push constant bytes); the render pass, the subpass and the base pipeline are not used. */
	layout_id = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;
	layout = amdrdna_vk_payload(session, I915_VK_OBJ_PIPELINE_LAYOUT, layout_id);
	if (layout != NULL)
		pipeline->push_bytes = layout->push_bytes;

	/* Points, lines and triangles without adjacency or patches. */
	if (pipeline->topology > VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN && refused == 0)
		refused = ENOTSUP;

	/* Reports state the executor does not draw. */
	if (refused != 0)
		return refused;

	/* Succeeded: the pipeline holds its state; its modules are in the build. */
	return 0;
}

/*
 * Decodes the head and the stages: [sType][pNext][flags][stageCount]
 * [count]{VkPipelineShaderStageCreateInfo}.  A vertex and a fragment stage
 * of known modules, without specialization, are taken; anything else is
 * ENOTSUP after the stages are read.
 */
static int
pipeline_decode_stages(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct pipeline_build *build)
{
	VkPipelineShaderStageCreateInfo stage;
	const struct amdrdna_vk_shader *module;
	uint64_t stages;
	uint64_t index;
	int refused;

	/* How many stages follow, behind sType, pNext, flags and the count's first form. */
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	stages = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (stages > PIPELINE_STAGES_MAX)
		return EINVAL;

	/* Each stage. */
	refused = 0;
	for (index = 0U; index < stages; index++) {
		kern_memset(&stage, 0, sizeof(stage));
		i915_vkc_dec_VkPipelineShaderStageCreateInfo(reader, &session->arena, &stage);
		if (reader->error != 0)
			return EINVAL;

		/* Specialization constants are not taken. */
		if (stage.pSpecializationInfo != NULL && stage.pSpecializationInfo->mapEntryCount != 0U) {
			refused = ENOTSUP;
			continue;
		}

		/* The vertex and the fragment stage keep their modules. */
		module = amdrdna_vk_payload(session, I915_VK_OBJ_SHADER_MODULE, (uint64_t)(uintptr_t)stage.module);
		if (stage.stage == VK_SHADER_STAGE_VERTEX_BIT) {
			build->vertex_module = module;
		} else if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
			build->fragment_module = module;
		} else {
			kern_logf("amdrdna: vk: pipeline stage 0x%x is not run\n", (unsigned)stage.stage);
			refused = ENOTSUP;
		}
	}

	/* Both programs, of known modules. */
	if (build->vertex_module == NULL || build->fragment_module == NULL) {
		if (refused == 0)
			refused = ENOTSUP;
	}

	/* Reports stages the executor does not run. */
	if (refused != 0)
		return refused;

	/* Succeeded: the two modules are known. */
	return 0;
}

/*
 * Decodes the vertex input: its bindings and attributes.  Float
 * attributes of one to four components at the generic locations, from as
 * many buffers as a draw binds; anything else is ENOTSUP.
 */
static int
pipeline_decode_vertex_input(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct amdrdna_vk_pipeline *pipeline)
{
	VkPipelineVertexInputStateCreateInfo input;
	const VkVertexInputAttributeDescription *attribute;
	uint32_t components;
	uint32_t bytes;
	uint32_t index;
	uint32_t code;

	/* The record. */
	kern_memset(&input, 0, sizeof(input));
	i915_vkc_dec_VkPipelineVertexInputStateCreateInfo(reader, &session->arena, &input);
	if (reader->error != 0)
		return EINVAL;

	/* No more bindings and attributes than a draw sends. */
	if (input.vertexBindingDescriptionCount > AMDRDNA_VK_VERTEX_BINDINGS_MAX)
		return ENOTSUP;
	if (input.vertexAttributeDescriptionCount > AMDRDNA_VK_VERTEX_ATTRIBUTES_MAX)
		return ENOTSUP;

	/* Each binding's number, stride and rate. */
	pipeline->binding_count = input.vertexBindingDescriptionCount;
	for (index = 0U; index < pipeline->binding_count; index++) {
		if (input.pVertexBindingDescriptions[index].binding >= AMDRDNA_VK_VERTEX_BINDINGS_MAX)
			return ENOTSUP;
		pipeline->bindings[index].binding = input.pVertexBindingDescriptions[index].binding;
		pipeline->bindings[index].stride = input.pVertexBindingDescriptions[index].stride;
		pipeline->bindings[index].instance = 0U;
		if (input.pVertexBindingDescriptions[index].inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
			pipeline->bindings[index].instance = 1U;
	}

	/* Each attribute: a location, a bound buffer, a format the vertex fetch reads. */
	pipeline->attribute_count = input.vertexAttributeDescriptionCount;
	for (index = 0U; index < pipeline->attribute_count; index++) {
		attribute = &input.pVertexAttributeDescriptions[index];
		if (attribute->location >= PIPELINE_LOCATIONS)
			return ENOTSUP;
		if (attribute->binding >= AMDRDNA_VK_VERTEX_BINDINGS_MAX)
			return ENOTSUP;
		if (!amdrdna_vk_vertex_format(attribute->format, &code, &components, &bytes))
			return ENOTSUP;

		pipeline->attributes[index].location = attribute->location;
		pipeline->attributes[index].binding = attribute->binding;
		pipeline->attributes[index].format = attribute->format;
		pipeline->attributes[index].offset = attribute->offset;
	}

	/* Succeeded: the vertex input is kept. */
	return 0;
}

/* Decodes the viewport state: its first viewport and scissor when it gives them. */
static void
pipeline_decode_viewport(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct amdrdna_vk_pipeline *pipeline)
{
	VkPipelineViewportStateCreateInfo viewport;

	/* The record; one that did not decode keeps nothing. */
	kern_memset(&viewport, 0, sizeof(viewport));
	i915_vkc_dec_VkPipelineViewportStateCreateInfo(reader, &session->arena, &viewport);
	if (reader->error != 0)
		return;

	/* The first viewport, as float bits. */
	if (viewport.pViewports != NULL && viewport.viewportCount != 0U) {
		pipeline_float(&pipeline->viewport[0], &viewport.pViewports[0].x);
		pipeline_float(&pipeline->viewport[1], &viewport.pViewports[0].y);
		pipeline_float(&pipeline->viewport[2], &viewport.pViewports[0].width);
		pipeline_float(&pipeline->viewport[3], &viewport.pViewports[0].height);
		pipeline_float(&pipeline->viewport[4], &viewport.pViewports[0].minDepth);
		pipeline_float(&pipeline->viewport[5], &viewport.pViewports[0].maxDepth);
	}

	/* The first scissor. */
	if (viewport.pScissors != NULL && viewport.scissorCount != 0U)
		pipeline->scissor = viewport.pScissors[0];
}

/* Decodes the colour blend state: the blend and write mask of attachment 0. */
static void
pipeline_decode_blend(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct amdrdna_vk_pipeline *pipeline)
{
	VkPipelineColorBlendStateCreateInfo blend;
	const VkPipelineColorBlendAttachmentState *attachment;

	/* The record; one that did not decode, or without attachments, blends nothing. */
	kern_memset(&blend, 0, sizeof(blend));
	i915_vkc_dec_VkPipelineColorBlendStateCreateInfo(reader, &session->arena, &blend);
	if (reader->error != 0)
		return;
	if (blend.attachmentCount == 0U || blend.pAttachments == NULL)
		return;

	/* Attachment 0's blend, factors, operations and write mask. */
	attachment = &blend.pAttachments[0];
	pipeline->blend_enable = 0U;
	if (attachment->blendEnable != VK_FALSE)
		pipeline->blend_enable = 1U;
	pipeline->blend_src_colour = attachment->srcColorBlendFactor;
	pipeline->blend_dst_colour = attachment->dstColorBlendFactor;
	pipeline->blend_colour_op = attachment->colorBlendOp;
	pipeline->blend_src_alpha = attachment->srcAlphaBlendFactor;
	pipeline->blend_dst_alpha = attachment->dstAlphaBlendFactor;
	pipeline->blend_alpha_op = attachment->alphaBlendOp;
	pipeline->write_mask = attachment->colorWriteMask & 0xfU;
}

/* Decodes the dynamic state: whether the viewport and the scissor come from the command buffer. */
static void
pipeline_decode_dynamic(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct amdrdna_vk_pipeline *pipeline)
{
	VkPipelineDynamicStateCreateInfo dynamic;
	uint32_t index;
	uint32_t state;

	/* The record; one that did not decode, or without a list, declares nothing. */
	kern_memset(&dynamic, 0, sizeof(dynamic));
	i915_vkc_dec_VkPipelineDynamicStateCreateInfo(reader, &session->arena, &dynamic);
	if (reader->error != 0 || dynamic.pDynamicStates == NULL)
		return;

	/* The viewport and the scissor; any other dynamic state keeps the pipeline's value. */
	for (index = 0U; index < dynamic.dynamicStateCount; index++) {
		state = (uint32_t)dynamic.pDynamicStates[index];
		if (state == VK_DYNAMIC_STATE_VIEWPORT) {
			pipeline->dynamic_viewport = 1U;
		} else if (state == VK_DYNAMIC_STATE_SCISSOR) {
			pipeline->dynamic_scissor = 1U;
		} else {
			kern_logf("amdrdna: vk: dynamic state %u is not implemented\n", state);
		}
	}
}

/*
 * Compiles a pipeline's two modules and copies the programs into a run of
 * the executor's GPU memory: the vertex program first, the fragment
 * program on the next boundary.  The fragment program is compiled first,
 * so that the vertex program exports the primitive number it reads.
 */
static int
pipeline_compile(
	struct amdrdna_vk_session *session,
	struct amdrdna_vk_pipeline *pipeline,
	struct pipeline_build *build)
{
	struct amdrdna_gfx10_compile_options options;
	uint8_t *cpu;
	uint64_t va;
	uint32_t vertex_bytes;
	uint32_t fragment_bytes;
	int error;

	/* The fragment program, then the vertex program with what the fragment program reads of it. */
	kern_memset(&options, 0, sizeof(options));
	error = pipeline_compile_stage(build->fragment_module, DRV_GPU_STAGE_FRAGMENT, &options, &pipeline->fragment);
	if (error != 0)
		return error;
	if (pipeline->fragment.compiled->primitive_id_attribute != AMDRDNA_GFX10_NO_ATTRIBUTE)
		options.export_primitive_id = 1U;
	error = pipeline_compile_stage(build->vertex_module, DRV_GPU_STAGE_VERTEX, &options, &pipeline->vertex);
	if (error != 0)
		return error;

	/* Each binding the programs read lies in the sets and bindings a draw binds. */
	error = pipeline_bindings(pipeline->vertex.compiled);
	if (error == 0)
		error = pipeline_bindings(pipeline->fragment.compiled);
	if (error != 0)
		return error;

	/* The run holding both, each on the boundary. */
	vertex_bytes = pipeline_program_bytes(pipeline->vertex.compiled);
	fragment_bytes = pipeline_program_bytes(pipeline->fragment.compiled);
	error = amdrdna_vk_gpu_alloc(session->owner, (uint64_t)vertex_bytes + fragment_bytes, &pipeline->code_token, &cpu, &va);
	if (error != 0)
		return error;

	/* Copies the programs and keeps their addresses. */
	pipeline_program(&pipeline->vertex, cpu, va);
	pipeline_program(&pipeline->fragment, cpu + vertex_bytes, va + vertex_bytes);

	/* Succeeded: the pipeline's programs are in the GPU's memory. */
	return 0;
}

/*
 * Compiles one module for a stage into a program, with its register
 * values; the reason of a refusal is shown.
 */
static int
pipeline_compile_stage(
	const struct amdrdna_vk_shader *module,
	enum drv_gpu_shader_stage stage,
	const struct amdrdna_gfx10_compile_options *options,
	struct amdrdna_vk_program *program)
{
	struct amdrdna_gfx10_diagnostic diagnostic;
	int error;

	/* Compiled for GFX10. */
	kern_memset(&diagnostic, 0, sizeof(diagnostic));
	error = amdrdna_gfx10_compile(module->words, module->word_count, stage, options, &program->compiled, &diagnostic);
	if (error != 0) {
		kern_logf("amdrdna: vk: a stage %u module of %u words did not compile (%d at %u: %s)\n",
			  (unsigned)stage,
			  (unsigned)module->word_count,
			  error,
			  (unsigned)diagnostic.instruction,
			  (diagnostic.reason != NULL) ? diagnostic.reason : "?");
		program->compiled = NULL;
		return error;
	}

	/* The register values a draw writes for it. */
	amdrdna_gfx10_program_registers(program->compiled, &program->registers);

	/* Succeeded: the program is compiled. */
	return 0;
}

/*
 * Checks the bindings a program reads: each buffer and sampled image in a
 * set and at a binding a descriptor set has, and no more sampled images
 * than a draw writes.  Returns 0 or ENOTSUP.
 */
static int
pipeline_bindings(
	const struct amdrdna_gfx10_program *program)
{
	uint32_t index;

	/* The storage buffers, the uniform buffers, the sampled images. */
	for (index = 0U; index < program->storage_used; index++) {
		if (program->storage[index].set >= AMDRDNA_VK_SETS_MAX || program->storage[index].binding >= AMDRDNA_VK_BINDINGS_MAX)
			return ENOTSUP;
	}
	for (index = 0U; index < program->uniform_used; index++) {
		if (program->uniforms[index].set >= AMDRDNA_VK_SETS_MAX || program->uniforms[index].binding >= AMDRDNA_VK_BINDINGS_MAX)
			return ENOTSUP;
	}
	if (program->texture_used > AMDRDNA_VK_TEXTURES_MAX)
		return ENOTSUP;
	for (index = 0U; index < program->texture_used; index++) {
		if (program->textures[index].set >= AMDRDNA_VK_SETS_MAX || program->textures[index].binding >= AMDRDNA_VK_BINDINGS_MAX)
			return ENOTSUP;
	}

	/* The push constants a draw holds. */
	if (program->push_bytes > AMDRDNA_VK_PUSH_BYTES)
		return ENOTSUP;

	/* Succeeded: a draw can write the program's table. */
	return 0;
}

/* Copies a compiled program's code to its place and keeps its address. */
static void
pipeline_program(
	struct amdrdna_vk_program *program,
	uint8_t *cpu,
	uint64_t va)
{
	/* The code, with its padding. */
	kern_memcpy(cpu, program->compiled->code, (size_t)program->compiled->code_words * 4U);
	program->va = va;
}

/* Reports the bytes a program takes in the run: its code, on the boundary. */
static uint32_t
pipeline_program_bytes(
	const struct amdrdna_gfx10_program *program)
{
	uint32_t bytes;

	/* The code, rounded up. */
	bytes = program->code_words * 4U;
	bytes = (bytes + PIPELINE_PROGRAM_ALIGN - 1U) & ~(PIPELINE_PROGRAM_ALIGN - 1U);

	/* Succeeded: the program's share of the run. */
	return bytes;
}

/*
 * vkCreateComputePipelines: the records cannot be skipped without decoding
 * them, and the executor runs no compute work, so the command is refused
 * (the frame fails).
 */
static int
pipeline_compute(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	UNUSED_PARAMETER(reader);
	UNUSED_PARAMETER(reply);

	/* Says so, then refuses. */
	kern_logf("amdrdna: vk: vkCreateComputePipelines is not implemented\n");
	return ENOTSUP;
}

/* Destroys a pipeline nothing holds: its programs' run goes back to the node. */
static int
pipeline_destroy(
	struct amdrdna_vk_session *session,
	void *payload)
{
	struct amdrdna_vk_pipeline *pipeline;

	pipeline = payload;

	/* The run, when the programs were copied, and the compiled programs. */
	if (pipeline->code_token != NULL)
		amdrdna_vk_gpu_free(session->owner, pipeline->code_token);
	if (pipeline->vertex.compiled != NULL)
		amdrdna_gfx10_program_free(pipeline->vertex.compiled);
	if (pipeline->fragment.compiled != NULL)
		amdrdna_gfx10_program_free(pipeline->fragment.compiled);
	if (pipeline->kernel.compiled != NULL)
		amdrdna_gfx10_program_free(pipeline->kernel.compiled);

	kern_free(pipeline);

	/* Succeeded: the pipeline is gone. */
	return 0;
}

/* Copies a float as its 32 bits (no floating point in the kernel). */
static void
pipeline_float(
	uint32_t *destination,
	const float *source)
{
	/* The bits, unconverted. */
	kern_memcpy(destination, source, sizeof(*destination));
}
