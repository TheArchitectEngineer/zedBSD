/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's shader modules, pipeline caches and pipelines.
 *
 * A shader module is the driver's (D): it keeps the SPIR-V words.  A
 * pipeline cache is always empty.  A pipeline is rewritten (R'): each of
 * its stages is compiled for the GPU's shader ISA (the compile runs in
 * the kernel), the program binary is put in GPU memory and handed to the
 * firmware with GHCI_PROGRAM, and then the create command itself is
 * relayed, which the firmware builds the pipeline's state from.  A
 * pipeline whose stages do not compile is not relayed: the reply says why.
 *
 * Specialization constants are applied to a copy of the module's words
 * before the compile: each OpSpecConstant* becomes the constant of its
 * value (the specialized one, or its default).
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "../hcr.h"
#include "../compiler/gsi1_isa.h"
#include "vk.h"
#include "vk-engine.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

/* The FourCC of the standard shader ISA. */
#define ISA_GSI1			GHCI_FOURCC('G', 'S', 'I', '1')

/* The most pipelines of one create command, and the most SPIR-V words of a module. */
#define PIPELINES_MAX			64U
#define MODULE_WORDS_MAX		(4U << 20)

/* The varying words a GPU keeps per vertex: 64 with the Vulkan 1.0 core rendering of CAPS[30], else 16. */
#define VARYING_WORDS_BASIC		16U
#define VARYING_WORDS_CORE		64U
#define CAPS_VULKAN_CORE		(1U << 30)

/*
 * The interface entry's location that names the varying word of the
 * position's w (GHCI section 12.10).
 */
#define W_LOCATION			0xffffffffU

/*
 * The SPIR-V words and opcodes the specialization rewrites.
 *
 * Values referenced: the SPIR-V specification, section 3 (the
 * instruction word's count in [31:16] and opcode in [15:0], the header of
 * five words, OpDecorate 71, Decoration SpecId 1, OpConstantTrue 41,
 * OpConstantFalse 42, OpConstant 43, OpConstantComposite 44,
 * OpSpecConstantTrue 48, OpSpecConstantFalse 49, OpSpecConstant 50,
 * OpSpecConstantComposite 51).
 */
#define SPIRV_HEADER_WORDS		5U
#define SPIRV_OP_DECORATE		71U
#define SPIRV_DECORATION_SPEC_ID	1U
#define SPIRV_OP_CONSTANT_TRUE		41U
#define SPIRV_OP_CONSTANT_FALSE		42U
#define SPIRV_OP_CONSTANT		43U
#define SPIRV_OP_CONSTANT_COMPOSITE	44U
#define SPIRV_OP_SPEC_CONSTANT_TRUE	48U
#define SPIRV_OP_SPEC_CONSTANT_FALSE	49U
#define SPIRV_OP_SPEC_CONSTANT		50U
#define SPIRV_OP_SPEC_CONSTANT_COMPOSITE 51U

/*
 * One stage of a pipeline being made: its module, its stage, its
 * specialization, and once compiled, its program and the GPU memory the
 * program is in.
 */
struct stage_build {
	uint64_t module;
	enum drv_gpu_shader_stage stage;
	const VkSpecializationInfo *specialization;
	struct ghci_gsi1_program *program;
	struct ghci_allocation *memory;
};

/*
 * One pipeline being made: its identity, its stages, and what of the
 * pipeline's state the compile needs (the samples, whether it draws
 * points, its render pass and subpass).
 */
struct pipeline_build {
	uint64_t identity;
	struct stage_build stages[GHCI_VK_STAGES];
	unsigned stage_count;
	uint32_t samples;
	int points;
	uint64_t render_pass;
	uint32_t subpass;
};

static int create_module(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int destroy_module(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static int create_cache(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int destroy_cache(struct i915_wire_reader *reader);
static int cache_data(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int merge_caches(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int create_pipelines(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int decode_graphics(struct ghci_vk_build *build, struct i915_wire_reader *reader, struct pipeline_build *pipeline);
static int decode_compute(struct ghci_vk_build *build, struct i915_wire_reader *reader, struct pipeline_build *pipeline);
static int add_stage(struct pipeline_build *pipeline, const VkPipelineShaderStageCreateInfo *info);
static uint32_t compile_pipeline(struct ghci_vk_build *build, uint64_t device, struct pipeline_build *pipeline);
static uint32_t compile_stage(struct ghci_vk_build *build, const struct ghci_gsi1_options *options, struct stage_build *stage);
static int needs_w(const struct ghci_gsi1_program *program);
static void fill_options(struct ghci_vk_build *build, uint64_t device, const struct pipeline_build *pipeline, struct ghci_gsi1_options *options);
static uint32_t place_program(struct ghci_vk_build *build, struct stage_build *stage);
static int program_stage(struct ghci_vk_build *build, uint64_t pipeline, struct stage_build *stage);
static void release_pipeline(struct ghci_vk_build *build, struct pipeline_build *pipeline);
static int destroy_pipeline(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static uint32_t *specialize(const struct ghci_vk_shader *shader, const VkSpecializationInfo *specialization);
static int spec_value(const VkSpecializationInfo *specialization, const uint32_t *words, uint32_t count, uint32_t result, uint32_t *value, uint32_t *bytes);

/*
 * Runs a shader module, pipeline cache or pipeline command; *handled
 * stays clear for another route's number.
 */
int
ghci_vk_program_route(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another route. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_SHADER_MODULE:
		error = create_module(build, flags, reader);
		break;
	case GPU_OP_DESTROY_SHADER_MODULE:
		error = destroy_module(build, reader);
		break;
	case GPU_OP_CREATE_PIPELINE_CACHE:
		error = create_cache(build, flags, reader);
		break;
	case GPU_OP_DESTROY_PIPELINE_CACHE:
		error = destroy_cache(reader);
		break;
	case GPU_OP_GET_PIPELINE_CACHE_DATA:
		error = cache_data(build, flags, reader);
		break;
	case GPU_OP_MERGE_PIPELINE_CACHES:
		error = merge_caches(build, flags, reader);
		break;
	case GPU_OP_CREATE_GRAPHICS_PIPELINES:
	case GPU_OP_CREATE_COMPUTE_PIPELINES:
		error = create_pipelines(build, opcode, flags, reader);
		break;
	case GPU_OP_DESTROY_PIPELINE:
		error = destroy_pipeline(build, flags, reader);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/*
 * vkCreateShaderModule: [device][u64 1][sType][u64 0][flags][u64 code
 * size][u64 words][the words][u64 0][u64 1][module] -> [VK_SUCCESS]
 * [u64 1][module].  The driver keeps a copy of the words.
 */
static int
create_module(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	const void *code;
	uint32_t *words;
	uint64_t present;
	uint64_t chain;
	uint64_t size;
	uint64_t count;
	uint64_t module;
	int error;

	/* The device and the create info, the words read in place. */
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	chain = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	size = drv_i915_wire_read_u64(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (present != 1U || chain != 0U)
		return EINVAL;

	/* Whole words, as many as the size says, within the bound. */
	if (count > MODULE_WORDS_MAX)
		return E2BIG;
	if (size != count * 4U)
		return EINVAL;
	code = drv_i915_wire_read_array(reader, (size_t)count, 4U);
	module = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The copy of the words. */
	words = NULL;
	if (count != 0U) {
		words = kern_malloc((size_t)count * 4U);
		if (words == NULL)
			return ENOMEM;
		kern_memcpy(words, code, (size_t)count * 4U);
	}

	/* The module's record holds them. */
	error = ghci_vk_object_add(build->session, GHCI_VK_SHADER, module, &object);
	if (error != 0) {
		if (words != NULL)
			kern_free(words);
		return error;
	}
	object->as.shader.words = words;
	object->as.shader.word_count = (uint32_t)count;

	/* Success and the identity. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, module);
	}

	/* Succeeded: the module is kept. */
	return 0;
}

/* vkDestroyShaderModule: the destroy form; the pipelines made of it keep their programs. */
static int
destroy_module(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t module;

	/* The destroy form. */
	module = ghci_vk_destroy_form(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The words go with the record. */
	object = ghci_vk_object_find(build->session, GHCI_VK_SHADER, module);
	if (object != NULL)
		ghci_vk_object_remove(build->session, object);

	/* Succeeded: the module is destroyed. */
	return 0;
}

/* vkCreatePipelineCache: the create form -> [VK_SUCCESS][u64 1][cache]; the cache holds nothing. */
static int
create_cache(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkPipelineCacheCreateInfo info;
	uint64_t present;
	uint64_t cache;

	/* The create form. */
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	i915_vkc_dec_VkPipelineCacheCreateInfo(reader, &build->session->arena, &info);
	cache = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Success and the identity. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, cache);
	}

	/* Succeeded: the cache exists. */
	return 0;
}

/* vkDestroyPipelineCache: the destroy form. */
static int
destroy_cache(
	struct i915_wire_reader *reader)
{
	/* The destroy form; nothing is kept. */
	(void)ghci_vk_destroy_form(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the cache is destroyed. */
	return 0;
}

/*
 * vkGetPipelineCacheData: [device][cache][u64 1][u64 capacity]
 * [u64 capacity] -> [VK_SUCCESS][u64 1][u64 0][u64 0]: no bytes.
 */
static int
cache_data(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	/* The device, the cache, the size's presence word and the capacities. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An empty cache. */
	if (flags != 0U) {
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, 0U);
		drv_i915_wire_reply_u64(&build->reply, 0U);
	}

	/* Succeeded: the data is reported. */
	return 0;
}

/* vkMergePipelineCaches: [device][destination][u32 count][u64 count][caches] -> [VK_SUCCESS]. */
static int
merge_caches(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	uint64_t count;

	/* The device, the destination and the sources. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (count > PIPELINES_MAX)
		return EINVAL;
	(void)drv_i915_wire_read_array(reader, (size_t)count, 8U);
	if (reader->error != 0)
		return EINVAL;

	/* Empty caches merge into an empty one. */
	if (flags != 0U)
		drv_i915_wire_reply_u32(&build->reply, (uint32_t)VK_SUCCESS);

	/* Succeeded: the caches are merged. */
	return 0;
}

/*
 * vkCreateGraphicsPipelines and vkCreateComputePipelines: [device][cache]
 * [u32 count][u64 count][infos][u64 0][u64 count][pipelines] -> [result]
 * [u64 count][pipelines].  Every stage of every pipeline is compiled
 * first; when all compile, each program goes to GPU memory and to the
 * firmware (GHCI_PROGRAM), then the command is relayed and the reply's
 * result is the firmware's.  Otherwise nothing is relayed and every
 * pipeline is the null handle.
 */
static int
create_pipelines(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct pipeline_build *pipelines;
	struct ghci_vk_object *object;
	const uint8_t *identities;
	uint64_t device;
	uint64_t count;
	uint64_t allocator;
	uint64_t announced;
	uint64_t index;
	uint32_t result;
	uint32_t record_index;
	size_t start;
	unsigned stage;
	int error;

	/* The device, the cache and the count. */
	start = reader->offset;
	device = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count == 0U || count > PIPELINES_MAX)
		return EINVAL;

	/* What is made of each pipeline, from the scratch. */
	pipelines = ghci_vk_arena_take(build->session, (size_t)count * sizeof(*pipelines));
	if (pipelines == NULL)
		return E2BIG;

	/* Each create info. */
	error = 0;
	for (index = 0U; index < count && error == 0; index++) {
		if (opcode == GPU_OP_CREATE_GRAPHICS_PIPELINES) {
			error = decode_graphics(build, reader, &pipelines[index]);
		} else {
			error = decode_compute(build, reader, &pipelines[index]);
		}
	}
	if (error != 0)
		return error;

	/* No allocator, then the identities libvulkan reserved. */
	allocator = drv_i915_wire_read_u64(reader);
	announced = drv_i915_wire_read_u64(reader);
	if (announced != count)
		reader->error = 1;
	identities = drv_i915_wire_read_array(reader, (size_t)count, 8U);
	if (reader->error != 0)
		return EINVAL;
	if (allocator != 0U)
		return EINVAL;
	for (index = 0U; index < count; index++)
		kern_memcpy(&pipelines[index].identity, identities + index * 8U, 8U);

	/* Every stage compiled, and its program placed in GPU memory. */
	result = (uint32_t)VK_SUCCESS;
	for (index = 0U; index < count && result == (uint32_t)VK_SUCCESS; index++)
		result = compile_pipeline(build, device, &pipelines[index]);

	/* A pipeline that did not compile: nothing is relayed, every pipeline is null. */
	if (result != (uint32_t)VK_SUCCESS) {
		for (index = 0U; index < count; index++)
			release_pipeline(build, &pipelines[index]);
		if (flags != 0U) {
			drv_i915_wire_reply_u32(&build->reply, result);
			drv_i915_wire_reply_u64(&build->reply, count);
			for (index = 0U; index < count; index++)
				drv_i915_wire_reply_u64(&build->reply, 0U);
		}
		return 0;
	}

	/* Each program to the firmware before the pipeline that uses it, and the pipeline's record. */
	for (index = 0U; index < count; index++) {
		error = ghci_vk_object_add(build->session, GHCI_VK_PIPELINE, pipelines[index].identity, &object);
		if (error != 0)
			return error;

		/* The record holds the programs' memory; each program is announced. */
		object->as.pipeline.program_count = pipelines[index].stage_count;
		for (stage = 0U; stage < pipelines[index].stage_count; stage++) {
			object->as.pipeline.programs[stage] = pipelines[index].stages[stage].memory;
			error = program_stage(build, pipelines[index].identity, &pipelines[index].stages[stage]);
			if (error != 0)
				return error;
		}
	}

	/* The create command itself. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &record_index);
	if (error != 0)
		return error;

	/* The firmware's result, the count and the identities. */
	if (flags != 0U) {
		error = ghci_vk_reply_result(build, record_index);
		if (error != 0)
			return error;
		drv_i915_wire_reply_u64(&build->reply, count);
		for (index = 0U; index < count; index++)
			drv_i915_wire_reply_u64(&build->reply, pipelines[index].identity);
	}

	/* Succeeded: the pipelines are relayed. */
	return 0;
}

/*
 * Decodes a VkGraphicsPipelineCreateInfo as libvulkan writes it:
 * [sType][u64 0][flags][stages], then the vertex input, the input
 * assembly, the tessellation, the viewport (each [u64 present][state]),
 * [u64 1][rasterization], the multisample, depth-stencil, blend and
 * dynamic states ([u64 present][state] each), [layout][render pass]
 * [subpass][base pipeline][base index].
 */
static int
decode_graphics(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader,
	struct pipeline_build *pipeline)
{
	VkPipelineShaderStageCreateInfo stage;
	VkPipelineVertexInputStateCreateInfo vertex;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineTessellationStateCreateInfo tessellation;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo rasterization;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDynamicStateCreateInfo dynamic;
	struct i915_wire_arena *arena;
	uint64_t count;
	uint64_t index;
	uint64_t chain;
	uint64_t present;
	int error;

	/* The head and the stages. */
	arena = &build->session->arena;
	(void)drv_i915_wire_read_u32(reader);
	chain = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (chain != 0U)
		reader->error = 1;
	if (reader->error != 0)
		return EINVAL;
	for (index = 0U; index < count; index++) {
		kern_memset(&stage, 0, sizeof(stage));
		i915_vkc_dec_VkPipelineShaderStageCreateInfo(reader, arena, &stage);
		if (reader->error != 0)
			return EINVAL;

		/* The stage the pipeline runs. */
		error = add_stage(pipeline, &stage);
		if (error != 0)
			return error;
	}

	/* The vertex input and the input assembly, which says whether the pipeline draws points. */
	pipeline->samples = 1U;
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineVertexInputStateCreateInfo(reader, arena, &vertex);
	kern_memset(&assembly, 0, sizeof(assembly));
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineInputAssemblyStateCreateInfo(reader, arena, &assembly);
	if (assembly.topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
		pipeline->points = 1;

	/* The tessellation and the viewports. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineTessellationStateCreateInfo(reader, arena, &tessellation);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineViewportStateCreateInfo(reader, arena, &viewport);

	/* The rasterization, always present. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		reader->error = 1;
	i915_vkc_dec_VkPipelineRasterizationStateCreateInfo(reader, arena, &rasterization);

	/* The multisample state, which gives the samples a fragment program is built for. */
	kern_memset(&multisample, 0, sizeof(multisample));
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineMultisampleStateCreateInfo(reader, arena, &multisample);
	if (multisample.rasterizationSamples != 0U)
		pipeline->samples = multisample.rasterizationSamples;

	/* The depth, blend and dynamic states. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineDepthStencilStateCreateInfo(reader, arena, &depth);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineColorBlendStateCreateInfo(reader, arena, &blend);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkPipelineDynamicStateCreateInfo(reader, arena, &dynamic);

	/* The layout, the render pass and subpass, and the base. */
	(void)drv_i915_wire_read_u64(reader);
	pipeline->render_pass = drv_i915_wire_read_u64(reader);
	pipeline->subpass = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the pipeline is decoded. */
	return 0;
}

/* Decodes a VkComputePipelineCreateInfo: its one stage. */
static int
decode_compute(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader,
	struct pipeline_build *pipeline)
{
	VkComputePipelineCreateInfo info;
	int error;

	/* The create info. */
	kern_memset(&info, 0, sizeof(info));
	i915_vkc_dec_VkComputePipelineCreateInfo(reader, &build->session->arena, &info);
	if (reader->error != 0)
		return EINVAL;

	/* Its stage, which must be the compute one. */
	pipeline->samples = 1U;
	if (info.stage.stage != VK_SHADER_STAGE_COMPUTE_BIT)
		return EINVAL;
	error = add_stage(pipeline, &info.stage);
	if (error != 0)
		return error;

	/* Succeeded: the pipeline is decoded. */
	return 0;
}

/* Adds a stage to a pipeline being made: a vertex, fragment or compute stage, each once. */
static int
add_stage(
	struct pipeline_build *pipeline,
	const VkPipelineShaderStageCreateInfo *info)
{
	struct stage_build *stage;
	enum drv_gpu_shader_stage kind;
	unsigned index;

	/* The stages the GPU runs. */
	if (info->stage == VK_SHADER_STAGE_VERTEX_BIT) {
		kind = DRV_GPU_STAGE_VERTEX;
	} else if (info->stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
		kind = DRV_GPU_STAGE_FRAGMENT;
	} else if (info->stage == VK_SHADER_STAGE_COMPUTE_BIT) {
		kind = DRV_GPU_STAGE_COMPUTE;
	} else {
		return ENOTSUP;
	}

	/* Each stage once, as many as a pipeline has. */
	for (index = 0U; index < pipeline->stage_count; index++) {
		if (pipeline->stages[index].stage == kind)
			return EINVAL;
	}
	if (pipeline->stage_count == GHCI_VK_STAGES)
		return EINVAL;

	/* The stage's module and specialization. */
	stage = &pipeline->stages[pipeline->stage_count];
	stage->module = (uint64_t)(uintptr_t)info->module;
	stage->stage = kind;
	stage->specialization = info->pSpecializationInfo;
	stage->program = NULL;
	stage->memory = NULL;
	pipeline->stage_count++;

	/* Succeeded: the stage is the pipeline's. */
	return 0;
}

/*
 * Compiles every stage of a pipeline and places its programs: the
 * fragment stage first, since it says whether the vertex stage exports
 * the position's w as a varying.  Reports VK_SUCCESS or why not.
 */
static uint32_t
compile_pipeline(
	struct ghci_vk_build *build,
	uint64_t device,
	struct pipeline_build *pipeline)
{
	struct ghci_gsi1_options *options;
	unsigned index;
	uint32_t result;
	int fragment_needs_w;

	/* The options, from the scratch (they are large). */
	options = ghci_vk_arena_take(build->session, sizeof(*options));
	if (options == NULL)
		return (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;
	fill_options(build, device, pipeline, options);

	/* The fragment stage, which may read the position's w from its varying word. */
	fragment_needs_w = 0;
	for (index = 0U; index < pipeline->stage_count; index++) {
		if (pipeline->stages[index].stage != DRV_GPU_STAGE_FRAGMENT)
			continue;

		result = compile_stage(build, options, &pipeline->stages[index]);
		if (result != (uint32_t)VK_SUCCESS)
			return result;
		fragment_needs_w = needs_w(pipeline->stages[index].program);
	}

	/* The other stages; the vertex stage exports the w only when the fragment stage reads it. */
	options->has_w_word = 0U;
	if (fragment_needs_w)
		options->has_w_word = 1U;
	for (index = 0U; index < pipeline->stage_count; index++) {
		if (pipeline->stages[index].stage == DRV_GPU_STAGE_FRAGMENT)
			continue;

		result = compile_stage(build, options, &pipeline->stages[index]);
		if (result != (uint32_t)VK_SUCCESS)
			return result;
	}

	/* Each program in GPU memory. */
	for (index = 0U; index < pipeline->stage_count; index++) {
		result = place_program(build, &pipeline->stages[index]);
		if (result != (uint32_t)VK_SUCCESS)
			return result;
	}

	/* Succeeded: every stage is compiled and placed. */
	return (uint32_t)VK_SUCCESS;
}

/* Compiles one stage of its module (specialized) with the options; reports VK_SUCCESS or why not. */
static uint32_t
compile_stage(
	struct ghci_vk_build *build,
	const struct ghci_gsi1_options *options,
	struct stage_build *stage)
{
	struct ghci_gsi1_diagnostic diagnostic;
	struct ghci_vk_object *module;
	const struct ghci_isa_backend *isa;
	uint32_t *words;
	int error;

	/* The module the stage names. */
	module = ghci_vk_object_find(build->session, GHCI_VK_SHADER, stage->module);
	if (module == NULL)
		return (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
	if (module->as.shader.word_count < SPIRV_HEADER_WORDS)
		return (uint32_t)VK_ERROR_INITIALIZATION_FAILED;

	/* The words with the specialization applied. */
	words = specialize(&module->as.shader, stage->specialization);
	if (words == NULL)
		return (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;

	/* The compile for the GPU's ISA. */
	isa = build->session->device->isa;
	kern_memset(&diagnostic, 0, sizeof(diagnostic));
	error = isa->compile(words, module->as.shader.word_count, stage->stage, options, &stage->program, &diagnostic);
	kern_free(words);
	if (error == ENOMEM)
		return (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;

	/* A module the compiler refuses: the reason goes to the log. */
	if (error != 0) {
		stage->program = NULL;
		kern_logf("ghci: vk: stage %u not compiled (%d) at %u: %s\n",
		    (unsigned)stage->stage,
		    error,
		    (unsigned)diagnostic.instruction,
		    diagnostic.reason != NULL ? diagnostic.reason : "");
		return (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
	}

	/* Succeeded: the stage has its program. */
	return (uint32_t)VK_SUCCESS;
}

/* Reports whether a fragment program reads the position's w from a varying word: 1 when it does. */
static int
needs_w(
	const struct ghci_gsi1_program *program)
{
	const uint32_t *entry;
	uint32_t offset;
	uint32_t entries;
	uint32_t index;

	/* The interface table, inside the binary. */
	offset = program->words[GSI1_H_IFACE_OFFSET];
	entries = program->words[GSI1_H_IFACE_ENTRIES];
	if (offset > program->word_count)
		return 0;
	if (entries > (program->word_count - offset) / GSI1_IFACE_WORDS)
		return 0;

	/* A varying input at the w's location. */
	for (index = 0U; index < entries; index++) {
		entry = &program->words[offset + index * GSI1_IFACE_WORDS];
		if (entry[0] == GSI1_KIND_VARYING_IN && entry[2] == W_LOCATION)
			return 1;
	}

	/* The program does not read it. */
	return 0;
}

/* Fills the compile's options from the GPU and from the pipeline. */
static void
fill_options(
	struct ghci_vk_build *build,
	uint64_t device,
	const struct pipeline_build *pipeline,
	struct ghci_gsi1_options *options)
{
	struct ghci_vk_object *object;
	struct ghci_vk_object *pass;
	struct ghci_device *gpu;
	uint32_t attachment;
	uint32_t index;

	/* The GPU: its constant bank, its varying words and its sample mask input. */
	gpu = build->session->device;
	kern_memset(options, 0, sizeof(*options));
	options->registers = 0U;
	options->bank_words = GHCI_CAPS2_CONST_WORDS(gpu->caps2);
	options->varying_words = VARYING_WORDS_BASIC;
	if ((gpu->caps & CAPS_VULKAN_CORE) != 0U)
		options->varying_words = VARYING_WORDS_CORE;
	if ((gpu->caps & GHCI_CAPS_MS4) != 0U)
		options->sample_mask_input = 1U;

	/* The pipeline: its samples and whether it draws points. */
	options->samples = pipeline->samples;
	options->points = 0U;
	if (pipeline->points)
		options->points = 1U;

	/* The device's bounds checks. */
	object = ghci_vk_object_find(build->session, GHCI_VK_DEVICE, device);
	if (object != NULL && object->as.device.robust)
		options->robust_buffer_access = 1U;

	/* The position's w, when asked for, in the last varying word. */
	options->w_word = options->varying_words - 1U;
	options->has_w_word = 1U;

	/* The subpass's input attachments: their formats and samples. */
	pass = ghci_vk_object_find(build->session, GHCI_VK_RENDER_PASS, pipeline->render_pass);
	if (pass == NULL)
		return;
	if (pipeline->subpass >= pass->as.pass.subpass_count)
		return;
	for (index = 0U; index < pass->as.pass.input_count[pipeline->subpass]; index++) {
		attachment = pass->as.pass.inputs[pipeline->subpass][index];
		if (attachment >= pass->as.pass.attachment_count)
			continue;

		options->input_attachment_format[index] = pass->as.pass.formats[attachment];
		options->input_attachment_samples[index] = pass->as.pass.samples[attachment];
	}
}

/*
 * Puts a stage's program in GPU memory the processor reaches (8-byte
 * aligned: every allocation is a page).  Reports VK_SUCCESS or why not.
 */
static uint32_t
place_program(
	struct ghci_vk_build *build,
	struct stage_build *stage)
{
	uint64_t bytes;
	int error;

	/* The memory, on the session's list. */
	bytes = (uint64_t)stage->program->word_count * 4U;
	error = ghci_allocation_make(build->session->device, build->session->owner, GHCI_USE_MAPPABLE, bytes, 0, &stage->memory);
	if (error != 0) {
		stage->memory = NULL;
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	/* The binary. */
	kern_memcpy(stage->memory->view, stage->program->words, (size_t)bytes);

	/* Succeeded: the program is in GPU memory. */
	return (uint32_t)VK_SUCCESS;
}

/*
 * Stages GHCI_PROGRAM [u64 pipeline][u32 stage][u32 ISA][u64 address]
 * [u32 bytes][u32 flags] for one stage of a pipeline, and frees the
 * compiled record (its words are in GPU memory now).
 */
static int
program_stage(
	struct ghci_vk_build *build,
	uint64_t pipeline,
	struct stage_build *stage)
{
	uint32_t arguments[8];
	uint32_t index;
	int error;

	/* The pipeline, the stage, the ISA, the binary's place and size. */
	arguments[0] = (uint32_t)pipeline;
	arguments[1] = (uint32_t)(pipeline >> 32);
	arguments[2] = (uint32_t)stage->stage;
	arguments[3] = ISA_GSI1;
	arguments[4] = stage->memory->gpu_address;
	arguments[5] = 0U;
	arguments[6] = stage->program->word_count * 4U;
	arguments[7] = 0U;
	error = ghci_vk_stage(build, GPU_OP_GHCI_PROGRAM, 0U, arguments, sizeof(arguments), &index);

	/* The compiled record goes either way. */
	build->session->device->isa->release(stage->program);
	stage->program = NULL;
	if (error != 0)
		return error;

	/* Succeeded: the program is staged. */
	return 0;
}

/* Gives back what a pipeline that is not made holds: its compiled programs and their memory. */
static void
release_pipeline(
	struct ghci_vk_build *build,
	struct pipeline_build *pipeline)
{
	struct stage_build *stage;
	unsigned index;

	/* Each stage's program and memory. */
	for (index = 0U; index < pipeline->stage_count; index++) {
		stage = &pipeline->stages[index];
		if (stage->program != NULL) {
			build->session->device->isa->release(stage->program);
			stage->program = NULL;
		}
		if (stage->memory != NULL) {
			ghci_allocation_drop(build->session->device, build->session->owner, stage->memory);
			stage->memory = NULL;
		}
	}
}

/*
 * vkDestroyPipeline: the destroy form, relayed; the programs' memory
 * goes back after the session's last fence.
 */
static int
destroy_pipeline(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t pipeline;
	uint32_t index;
	unsigned stage;
	size_t start;
	int error;

	/* The destroy form, relayed. */
	start = reader->offset;
	pipeline = ghci_vk_destroy_form(reader);
	error = ghci_vk_relay(build, GPU_OP_DESTROY_PIPELINE, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The pipeline's record. */
	object = ghci_vk_object_find(build->session, GHCI_VK_PIPELINE, pipeline);
	if (object == NULL)
		return 0;

	/* Its programs' memory after the frame. */
	for (stage = 0U; stage < object->as.pipeline.program_count; stage++) {
		error = ghci_vk_release_later(build, object->as.pipeline.programs[stage]);
		if (error != 0)
			return error;
	}
	ghci_vk_object_remove(build->session, object);

	/* Succeeded: the pipeline is destroyed. */
	return 0;
}

/*
 * Copies a module's words with every specialization constant made a
 * constant: OpSpecConstantTrue and OpSpecConstantFalse become the
 * Boolean of their value, OpSpecConstant the constant of its value, and
 * OpSpecConstantComposite a composite.  The value is the specialization's
 * for the constant's SpecId when it gives one, else the default.
 * Returns the copy (the caller frees it), or NULL without memory.
 */
static uint32_t *
specialize(
	const struct ghci_vk_shader *shader,
	const VkSpecializationInfo *specialization)
{
	uint32_t *words;
	uint32_t at;
	uint32_t count;
	uint32_t opcode;
	uint32_t length;
	uint32_t value[2];
	uint32_t bytes;
	int found;

	/* The copy. */
	count = shader->word_count;
	words = kern_malloc((size_t)count * 4U);
	if (words == NULL)
		return NULL;
	kern_memcpy(words, shader->words, (size_t)count * 4U);

	/* Each instruction after the header, as long as its length stays inside the module. */
	for (at = SPIRV_HEADER_WORDS; at < count; at += length) {
		opcode = words[at] & 0xffffU;
		length = words[at] >> 16;
		if (length == 0U || length > count - at)
			break;

		/* A Boolean: its specialized value picks which. */
		if (opcode == SPIRV_OP_SPEC_CONSTANT_TRUE || opcode == SPIRV_OP_SPEC_CONSTANT_FALSE) {
			value[0] = 0U;
			if (opcode == SPIRV_OP_SPEC_CONSTANT_TRUE)
				value[0] = 1U;
			if (length >= 3U)
				(void)spec_value(specialization, words, count, words[at + 2U], value, &bytes);
			opcode = SPIRV_OP_CONSTANT_FALSE;
			if (value[0] != 0U)
				opcode = SPIRV_OP_CONSTANT_TRUE;
			words[at] = (length << 16) | opcode;
			continue;
		}

		/* A scalar: its specialized value replaces the default's words. */
		if (opcode == SPIRV_OP_SPEC_CONSTANT) {
			if (length >= 4U) {
				found = spec_value(specialization, words, count, words[at + 2U], value, &bytes);
				if (found && bytes == 4U && length == 4U)
					words[at + 3U] = value[0];
				if (found && bytes == 8U && length == 5U) {
					words[at + 3U] = value[0];
					words[at + 4U] = value[1];
				}
			}
			words[at] = (length << 16) | SPIRV_OP_CONSTANT;
			continue;
		}

		/* A composite of constants. */
		if (opcode == SPIRV_OP_SPEC_CONSTANT_COMPOSITE)
			words[at] = (length << 16) | SPIRV_OP_CONSTANT_COMPOSITE;
	}

	/* Succeeded: the specialized copy. */
	return words;
}

/*
 * Finds the specialized value of the constant with result id: its SpecId
 * from an OpDecorate of the module, then the map entry of that SpecId.
 * Reports 1 with the value's words and bytes (4 or 8), or 0 when the
 * specialization gives none.
 */
static int
spec_value(
	const VkSpecializationInfo *specialization,
	const uint32_t *words,
	uint32_t count,
	uint32_t result,
	uint32_t *value,
	uint32_t *bytes)
{
	const VkSpecializationMapEntry *entry;
	const uint8_t *data;
	uint32_t at;
	uint32_t length;
	uint32_t spec_id;
	uint32_t index;
	int decorated;

	/* No specialization gives no value. */
	if (specialization == NULL)
		return 0;
	if (specialization->mapEntryCount == 0U || specialization->pMapEntries == NULL)
		return 0;

	/* The constant's SpecId. */
	decorated = 0;
	spec_id = 0U;
	for (at = SPIRV_HEADER_WORDS; at < count; at += length) {
		length = words[at] >> 16;
		if (length == 0U || length > count - at)
			break;

		/* OpDecorate %result SpecId N. */
		if ((words[at] & 0xffffU) == SPIRV_OP_DECORATE &&
		    length >= 4U &&
		    words[at + 1U] == result &&
		    words[at + 2U] == SPIRV_DECORATION_SPEC_ID) {
			spec_id = words[at + 3U];
			decorated = 1;
			break;
		}
	}
	if (!decorated)
		return 0;

	/* The map entry of the SpecId, inside the data. */
	data = specialization->pData;
	for (index = 0U; index < specialization->mapEntryCount; index++) {
		entry = &specialization->pMapEntries[index];
		if (entry->constantID != spec_id)
			continue;

		/* A Boolean or a 32-bit value, or a 64-bit one, inside the data. */
		if (data == NULL)
			return 0;
		if (entry->size != 4U && entry->size != 8U)
			return 0;
		if (entry->offset > specialization->dataSize || entry->size > specialization->dataSize - entry->offset)
			return 0;

		/* Succeeded: the value's words. */
		value[1] = 0U;
		kern_memcpy(value, data + entry->offset, entry->size);
		*bytes = (uint32_t)entry->size;
		return 1;
	}

	/* The specialization does not give this constant. */
	return 0;
}
