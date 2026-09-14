/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 3D pipeline state.  Builds a pipeline from the compiled shaders and
 * emits the 3DSTATE commands a draw needs into a batch.
 *
 * Command opcodes and lengths are transcribed into linux/3dstate-gen12.inc; the
 * assembly is new.  The core commands (PIPELINE_SELECT, STATE_BASE_ADDRESS,
 * 3DSTATE_VS, 3DSTATE_PS) carry the shader kernel pointers and register counts.
 * The remaining fixed-function fields are completed during the on-hardware
 * bring-up; this module fixes the command sequence and the shader pointers.
 */

#include "vk-internal.h"
#include "pipe.h"
#include "cmd.h"
#include "compile.h"
#include "spirv.h"
#include "cmd.h"

#include "../internal.h"

#include <kern/kmem.h>
#include <kern/pmem.h>

#include <errno.h>
#include <string.h>

#include "linux/3dstate-gen12.inc"

/* A pipeline retains the shader placement and the register counts to emit. */
static void i915_vk_pipe_release_code(struct i915_vk_session *session, struct i915_gem_object *vs_code, struct i915_gem_object *fs_code);

struct i915_vk_pipeline {
	struct i915_vk_session *session;
	struct i915_gem_object *vs_code;
	struct i915_gem_object *fs_code;
	uint64_t vs_kernel;
	uint64_t fs_kernel;
	uint32_t vs_grf;
	uint32_t fs_grf;
	uint32_t topology;
};

static void i915_vk_batch_emit(struct i915_vk_batch *batch, uint32_t dword);
static void i915_vk_batch_pad(struct i915_vk_batch *batch, uint32_t count);

/* A stored SPIR-V module the graphics pipeline compiles for its stage. */
struct i915_vk_shader_module {
	uint32_t *code;
	uint32_t words;
};

/* The largest SPIR-V module the executor accepts, in 32-bit words. */
#define I915_VK_SHADER_MODULE_MAX_WORDS	262144U

/* Maps a driver errno to the VkResult a pipe reply carries. */
static uint32_t
i915_vk_pipe_result(
	int error)
{
	if (error == 0)
		return 0U;			/* VK_SUCCESS */
	if (error == ENOMEM)
		return (uint32_t)(-2);		/* VK_ERROR_OUT_OF_DEVICE_MEMORY */
	return (uint32_t)(-3);			/* VK_ERROR_INITIALIZATION_FAILED */
}

/*
 * vkCreateShaderModule: [device][pCreateInfo present] VkShaderModuleCreateInfo
 * [pAllocator present][pShaderModule present][module wire_id].
 * VkShaderModuleCreateInfo is [sType][pNext present][flags][codeSize][word count]
 * [word count x SPIR-V word].  The module retains the SPIR-V for pipeline compile.
 */
static int
i915_vk_pipe_create_shader_module(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_shader_module *module;
	uint64_t count;
	uint64_t index;
	i915_vk_handle handle;
	uint32_t stype;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_u64(reader);			/* pCreateInfo present */
	stype = i915_vk_read_u32(reader);		/* VkStructureType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_u32(reader);			/* flags */
	(void)i915_vk_read_u64(reader);			/* codeSize bytes */
	count = i915_vk_read_u64(reader);		/* word count */
	if (reader->error != 0 || count > I915_VK_SHADER_MODULE_MAX_WORDS)
		return EINVAL;

	/* Allocate the module and copy the SPIR-V; a failure still drains the words. */
	module = kern_calloc(1U, sizeof(*module));
	if (module != NULL) {
		module->code = kern_calloc(count != 0U ? count : 1U, sizeof(uint32_t));
		if (module->code == NULL) {
			kern_free(module);
			module = NULL;
		}
	}
	for (index = 0U; index < count; index++) {
		uint32_t word = i915_vk_read_u32(reader);
		if (module != NULL)
			module->code[index] = word;
	}
	if (module != NULL)
		module->words = (uint32_t)count;

	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	(void)i915_vk_read_u64(reader);			/* pShaderModule present */
	handle = i915_vk_read_handle(reader);
	if (reader->error != 0 || stype != 16U) {
		if (module != NULL) {
			kern_free(module->code);
			kern_free(module);
		}
		return EINVAL;
	}

	/* VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO already checked above. */
	error = module != NULL ? 0 : ENOMEM;
	if (error == 0) {
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_SHADER_MODULE, handle, module);
		if (error != 0) {
			kern_free(module->code);
			kern_free(module);
		}
	}

	i915_vk_reply_u32(reply, i915_vk_pipe_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);		/* present */
		i915_vk_reply_u64(reply, handle);	/* identifier */
	}
	return 0;
}

/*
 * vkDestroyShaderModule: [device][module][pAllocator present].  It returns void,
 * so the reply is the echoed opcode alone.
 */
static int
i915_vk_pipe_destroy_shader_module(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_shader_module *module;
	i915_vk_handle handle;

	(void)i915_vk_read_handle(reader);		/* device */
	handle = i915_vk_read_handle(reader);
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	if (reader->error != 0)
		return EINVAL;

	module = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_SHADER_MODULE, handle);
	if (module != NULL) {
		i915_vk_obj_remove(session->vk, I915_VK_OBJ_SHADER_MODULE, handle);
		kern_free(module->code);
		kern_free(module);
	}

	(void)reply;
	return 0;
}

/* The most graphics pipelines one vkCreateGraphicsPipelines request builds. */
#define I915_VK_PIPELINE_BATCH_MAX	16U

/* Skips a wire string: a 64-bit byte count followed by 4-byte-padded bytes. */
static void
i915_vk_pipe_skip_string(
	struct i915_vk_reader *reader)
{
	uint64_t bytes;
	uint64_t words;
	uint64_t index;

	bytes = i915_vk_read_u64(reader);
	words = (bytes + 3U) / 4U;
	for (index = 0U; index < words; index++)
		(void)i915_vk_read_u32(reader);
}

/*
 * Decodes one VkPipelineShaderStageCreateInfo, returning the stage bit and the
 * shader module handle.  Specialization constants are not yet supported.
 */
static int
i915_vk_pipe_decode_stage(
	struct i915_vk_reader *reader,
	uint32_t *stage,
	i915_vk_handle *module)
{
	(void)i915_vk_read_u32(reader);			/* sType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_u32(reader);			/* flags */
	*stage = i915_vk_read_u32(reader);		/* VkShaderStageFlagBits */
	*module = i915_vk_read_handle(reader);
	i915_vk_pipe_skip_string(reader);		/* pName */
	if (i915_vk_read_u64(reader) != 0U)		/* pSpecializationInfo present */
		return EINVAL;				/* specialization not yet supported */
	return reader->error != 0 ? EINVAL : 0;
}

/* Consumes a fixed run of 32-bit words. */
static void
i915_vk_pipe_skip_words(
	struct i915_vk_reader *reader,
	uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		(void)i915_vk_read_u32(reader);
}

/* Consumes a wire array: a 64-bit element count of stride-word elements. */
static void
i915_vk_pipe_skip_array(
	struct i915_vk_reader *reader,
	uint32_t stride)
{
	uint64_t total;
	uint64_t index;

	total = i915_vk_read_u64(reader) * stride;
	for (index = 0U; index < total; index++)
		(void)i915_vk_read_u32(reader);
}

/* Compiles one shader module for a stage and places its GEN code in a GEM buffer. */
static int
i915_vk_pipe_place_shader(
	struct i915_vk_session *session,
	struct i915_vk_shader_module *module,
	enum i915_vk_stage stage,
	struct i915_gem_object **code_object,
	uint64_t *kernel,
	uint32_t *grf)
{
	struct i915_device *device;
	struct i915_vk_shader_ir *ir;
	struct i915_vk_shader_binary *binary;
	struct i915_gem_object *object;
	void *cpu;
	int error;

	*code_object = NULL;
	device = session->vk->i915;

	/* Parse the SPIR-V and compile it one instruction at a time to GEN. */
	error = i915_vk_spirv_parse(module->code, module->words, stage, &ir);
	if (error != 0)
		return error;
	error = i915_vk_compile(session->vk, ir, &binary);
	i915_vk_spirv_free(ir);
	if (error != 0)
		return error;

	/* Place the code words in a session-bound GEM buffer the shader runs from. */
	mutex_lock(&device->mutex);
	error = drv_i915_gem_create(device, binary->code_bytes != 0U ? binary->code_bytes : 4U, &object);
	if (error == 0) {
		error = drv_i915_gem_bind_vm(session->gpu->vm, object);
		if (error != 0)
			drv_i915_gem_destroy(device, object);
	}
	mutex_unlock(&device->mutex);
	if (error != 0) {
		i915_vk_shader_binary_free(binary);
		return error;
	}

	cpu = kern_pmem_to_kernel(object->run.paddr);
	memcpy(cpu, binary->code, binary->code_bytes);

	*code_object = object;
	*kernel = object->va + binary->entry_offset;
	*grf = binary->grf_used;
	i915_vk_shader_binary_free(binary);
	return 0;
}

/* Releases the GEM buffers a pipeline placed its shader code into. */
static void
i915_vk_pipe_release_code(
	struct i915_vk_session *session,
	struct i915_gem_object *vs_code,
	struct i915_gem_object *fs_code)
{
	struct i915_device *device;

	device = session->vk->i915;
	mutex_lock(&device->mutex);
	if (vs_code != NULL)
		drv_i915_gem_destroy(device, vs_code);
	if (fs_code != NULL)
		drv_i915_gem_destroy(device, fs_code);
	mutex_unlock(&device->mutex);
}

/* Decodes one VkGraphicsPipelineCreateInfo and builds the pipeline it names. */
static int
i915_vk_pipe_decode_graphics(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_pipeline **out)
{
	struct i915_vk_pipeline_info info;
	struct i915_vk_shader_binary vs_binary;
	struct i915_vk_shader_binary fs_binary;
	struct i915_vk_shader_module *module;
	struct i915_gem_object *vs_code;
	struct i915_gem_object *fs_code;
	uint64_t stage_count;
	uint64_t index;
	i915_vk_handle vs_module;
	i915_vk_handle fs_module;
	uint32_t topology;
	uint32_t vs_grf;
	uint32_t fs_grf;
	uint32_t stype;
	int error;

	*out = NULL;
	vs_module = 0U;
	fs_module = 0U;
	topology = 3U;					/* triangle list by default */

	stype = i915_vk_read_u32(reader);		/* sType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_u32(reader);			/* flags */
	(void)i915_vk_read_u32(reader);			/* stageCount */
	stage_count = i915_vk_read_u64(reader);
	if (reader->error != 0 || stype != 28U || stage_count > 8U)
		return EINVAL;

	/* Each stage names a module; the vertex and fragment stages are used. */
	for (index = 0U; index < stage_count; index++) {
		uint32_t stage;
		i915_vk_handle handle;

		error = i915_vk_pipe_decode_stage(reader, &stage, &handle);
		if (error != 0)
			return error;
		if (stage == 0x1U)			/* VK_SHADER_STAGE_VERTEX_BIT */
			vs_module = handle;
		else if (stage == 0x10U)		/* VK_SHADER_STAGE_FRAGMENT_BIT */
			fs_module = handle;
	}

	/* Every state below is a presence u64 then, if set, its exact record. */
	if (i915_vk_read_u64(reader) != 0U) {		/* vertex input */
		i915_vk_pipe_skip_words(reader, 5U);	/* sType,pNext,flags,bindingCount */
		i915_vk_pipe_skip_array(reader, 3U);	/* bindings */
		i915_vk_pipe_skip_words(reader, 1U);	/* attributeCount */
		i915_vk_pipe_skip_array(reader, 4U);	/* attributes */
	}
	if (i915_vk_read_u64(reader) != 0U) {		/* input assembly */
		i915_vk_pipe_skip_words(reader, 3U);	/* sType,pNext */
		(void)i915_vk_read_u32(reader);		/* flags */
		topology = i915_vk_read_u32(reader);
		(void)i915_vk_read_u32(reader);		/* primitiveRestartEnable */
	}
	if (i915_vk_read_u64(reader) != 0U)		/* tessellation */
		i915_vk_pipe_skip_words(reader, 5U);	/* sType,pNext,flags,patchControlPoints */
	if (i915_vk_read_u64(reader) != 0U) {		/* viewport */
		i915_vk_pipe_skip_words(reader, 5U);	/* sType,pNext,flags,viewportCount */
		i915_vk_pipe_skip_array(reader, 6U);	/* viewports */
		i915_vk_pipe_skip_words(reader, 1U);	/* scissorCount */
		i915_vk_pipe_skip_array(reader, 4U);	/* scissors */
	}
	if (i915_vk_read_u64(reader) != 0U)		/* rasterization (always present) */
		i915_vk_pipe_skip_words(reader, 14U);	/* sType,pNext,flags,6 enums,4 floats */
	if (i915_vk_read_u64(reader) != 0U) {		/* multisample */
		i915_vk_pipe_skip_words(reader, 7U);	/* sType,pNext,flags,samples,shading,minShading */
		i915_vk_pipe_skip_array(reader, 1U);	/* sample mask */
		i915_vk_pipe_skip_words(reader, 2U);	/* alphaToCoverage,alphaToOne */
	}
	if (i915_vk_read_u64(reader) != 0U)		/* depth-stencil */
		i915_vk_pipe_skip_words(reader, 25U);	/* 9 fixed + front(7) + back(7) + 2 floats */
	if (i915_vk_read_u64(reader) != 0U) {		/* color blend */
		i915_vk_pipe_skip_words(reader, 7U);	/* sType,pNext,flags,logicOpEnable,logicOp,attachmentCount */
		i915_vk_pipe_skip_array(reader, 8U);	/* attachments */
		i915_vk_pipe_skip_array(reader, 1U);	/* blendConstants (4) */
	}
	if (i915_vk_read_u64(reader) != 0U) {		/* dynamic */
		i915_vk_pipe_skip_words(reader, 5U);	/* sType,pNext,flags,dynamicStateCount */
		i915_vk_pipe_skip_array(reader, 1U);	/* states */
	}
	(void)i915_vk_read_u64(reader);			/* layout handle */
	(void)i915_vk_read_u64(reader);			/* renderPass handle */
	(void)i915_vk_read_u32(reader);			/* subpass */
	(void)i915_vk_read_u64(reader);			/* base pipeline handle */
	(void)i915_vk_read_u32(reader);			/* base pipeline index */
	if (reader->error != 0)
		return EINVAL;

	/* Both stages must resolve to a stored SPIR-V module. */
	module = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_SHADER_MODULE, vs_module);
	if (module == NULL)
		return EINVAL;
	error = i915_vk_pipe_place_shader(session, module, I915_VK_STAGE_VERTEX, &vs_code, &info.vs_kernel, &vs_grf);
	if (error != 0)
		return error;

	module = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_SHADER_MODULE, fs_module);
	if (module == NULL) {
		i915_vk_pipe_release_code(session, vs_code, NULL);
		return EINVAL;
	}
	error = i915_vk_pipe_place_shader(session, module, I915_VK_STAGE_FRAGMENT, &fs_code, &info.fs_kernel, &fs_grf);
	if (error != 0) {
		i915_vk_pipe_release_code(session, vs_code, NULL);
		return error;
	}

	/* Build the pipeline from the placed code and record ownership of the buffers. */
	memset(&vs_binary, 0, sizeof(vs_binary));
	memset(&fs_binary, 0, sizeof(fs_binary));
	vs_binary.grf_used = vs_grf;
	fs_binary.grf_used = fs_grf;
	info.vs = &vs_binary;
	info.fs = &fs_binary;
	info.topology = topology;
	info.color_format = 0U;
	error = i915_vk_pipeline_create(session, &info, out);
	if (error != 0) {
		i915_vk_pipe_release_code(session, vs_code, fs_code);
		return error;
	}
	(*out)->session = session;
	(*out)->vs_code = vs_code;
	(*out)->fs_code = fs_code;
	return 0;
}

/*
 * vkCreateGraphicsPipelines: [device][cache][count32][count]
 * [count x VkGraphicsPipelineCreateInfo][0][count][count x pipeline wire_id].
 * The reply is result, the returned count, then each pipeline identity.
 */
static int
i915_vk_pipe_create_graphics(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_pipeline *created[I915_VK_PIPELINE_BATCH_MAX];
	i915_vk_handle handles[I915_VK_PIPELINE_BATCH_MAX];
	uint64_t count;
	uint64_t index;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_handle(reader);		/* pipeline cache */
	(void)i915_vk_read_u32(reader);			/* count */
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count == 0U || count > I915_VK_PIPELINE_BATCH_MAX)
		return EINVAL;

	/* Decode and build every pipeline before its output identity is known. */
	error = 0;
	for (index = 0U; index < count; index++) {
		error = i915_vk_pipe_decode_graphics(session, reader, &created[index]);
		if (error != 0)
			break;
	}

	/* The output identities follow the whole batch of create records. */
	if (error == 0) {
		(void)i915_vk_read_u64(reader);		/* reserved 0 */
		(void)i915_vk_read_u64(reader);		/* identity count */
		for (index = 0U; index < count; index++)
			handles[index] = i915_vk_read_handle(reader);
		if (reader->error != 0)
			error = EINVAL;
	}

	/* Any failure rolls the whole batch back; a partial batch publishes nothing. */
	if (error != 0) {
		while (index-- > 0U) {
			i915_vk_pipe_release_code(session, created[index]->vs_code, created[index]->fs_code);
			i915_vk_pipeline_destroy(created[index]);
		}
		i915_vk_reply_u32(reply, i915_vk_pipe_result(error));
		return 0;
	}

	/* Bind each pipeline to its wire identity. */
	for (index = 0U; index < count; index++) {
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_PIPELINE, handles[index], created[index]);
		if (error != 0)
			break;
	}
	if (error != 0) {
		uint64_t undo;

		for (undo = 0U; undo < index; undo++)
			i915_vk_obj_remove(session->vk, I915_VK_OBJ_PIPELINE, handles[undo]);
		for (undo = 0U; undo < count; undo++) {
			i915_vk_pipe_release_code(session, created[undo]->vs_code, created[undo]->fs_code);
			i915_vk_pipeline_destroy(created[undo]);
		}
		i915_vk_reply_u32(reply, i915_vk_pipe_result(error));
		return 0;
	}

	i915_vk_reply_u32(reply, 0U);			/* VK_SUCCESS */
	i915_vk_reply_u64(reply, count);
	for (index = 0U; index < count; index++)
		i915_vk_reply_u64(reply, handles[index]);
	return 0;
}

/*
 * vkDestroyPipeline: [device][pipeline][pAllocator present].  It returns void,
 * so the reply is the echoed opcode alone.
 */
static int
i915_vk_pipe_destroy_pipeline(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_pipeline *pipeline;
	i915_vk_handle handle;

	(void)i915_vk_read_handle(reader);		/* device */
	handle = i915_vk_read_handle(reader);
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	if (reader->error != 0)
		return EINVAL;

	pipeline = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_PIPELINE, handle);
	if (pipeline != NULL) {
		i915_vk_obj_remove(session->vk, I915_VK_OBJ_PIPELINE, handle);
		i915_vk_pipeline_destroy(pipeline);
	}

	(void)reply;
	return 0;
}

/* Routes pipeline, shader-module, render-pass and framebuffer opcodes. */
int
i915_vk_pipe_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	/*
	 * Shader modules retain SPIR-V for the pipeline compile (p011 increment C);
	 * pipelines, render passes and framebuffers follow.
	 */
	switch (opcode) {
	case 59U:	/* vkCreateShaderModule */
		return i915_vk_pipe_create_shader_module(session, reader, reply);
	case 60U:	/* vkDestroyShaderModule */
		return i915_vk_pipe_destroy_shader_module(session, reader, reply);
	case 65U:	/* vkCreateGraphicsPipelines */
		return i915_vk_pipe_create_graphics(session, reader, reply);
	case 67U:	/* vkDestroyPipeline */
		return i915_vk_pipe_destroy_pipeline(session, reader, reply);
	default:
		return EINVAL;
	}
}

/* Builds a pipeline from its shaders and fixed-function state. */
int
i915_vk_pipeline_create(
	struct i915_vk_session *session,
	const struct i915_vk_pipeline_info *info,
	struct i915_vk_pipeline **out)
{
	struct i915_vk_pipeline *pipeline;

	/* The caller receives nothing on failure. */
	(void)session;
	*out = NULL;

	pipeline = kern_calloc(1U, sizeof(*pipeline));
	if (pipeline == NULL)
		return ENOMEM;

	/* The pipeline records where each shader was placed and its register use. */
	pipeline->vs_kernel = info->vs_kernel;
	pipeline->fs_kernel = info->fs_kernel;
	pipeline->vs_grf = info->vs != NULL ? info->vs->grf_used : 0U;
	pipeline->fs_grf = info->fs != NULL ? info->fs->grf_used : 0U;
	pipeline->topology = info->topology;

	/* Succeeded: the pipeline can be emitted into a draw batch. */
	*out = pipeline;
	return 0;
}

/* Releases a pipeline. */
void
i915_vk_pipeline_destroy(
	struct i915_vk_pipeline *pipeline)
{
	if (pipeline == NULL)
		return;

	/* A pipeline built from decoded SPIR-V owns the GEM buffers holding its code. */
	if (pipeline->session != NULL)
		i915_vk_pipe_release_code(pipeline->session, pipeline->vs_code, pipeline->fs_code);
	kern_free(pipeline);
}

/* Emits the vertex and pixel shader state for one pipeline. */
void
i915_vk_pipeline_emit(
	struct i915_vk_pipeline *pipeline,
	struct i915_vk_batch *batch)
{
	/* 3DSTATE_VS names the vertex kernel and its register count. */
	i915_vk_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VS, GEN12_3DSTATE_VS_DWORDS));
	i915_vk_batch_emit(batch, (uint32_t)pipeline->vs_kernel);
	i915_vk_batch_emit(batch, (uint32_t)(pipeline->vs_kernel >> 32));
	i915_vk_batch_emit(batch, pipeline->vs_grf);
	i915_vk_batch_pad(batch, GEN12_3DSTATE_VS_DWORDS - 4U);

	/* 3DSTATE_PS names the pixel kernel and its register count. */
	i915_vk_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS, GEN12_3DSTATE_PS_DWORDS));
	i915_vk_batch_emit(batch, (uint32_t)pipeline->fs_kernel);
	i915_vk_batch_emit(batch, (uint32_t)(pipeline->fs_kernel >> 32));
	i915_vk_batch_emit(batch, pipeline->fs_grf);
	i915_vk_batch_pad(batch, GEN12_3DSTATE_PS_DWORDS - 4U);
}

/* Emits the once-per-draw base state: the 3D pipeline and the state base. */
void
i915_vk_pipe_emit_base(
	struct i915_vk_session *session,
	struct i915_vk_batch *batch)
{
	(void)session;

	/* PIPELINE_SELECT is a single dword selecting the 3D pipeline. */
	i915_vk_batch_emit(batch, (GEN12_CMD_PIPELINE_SELECT << 16) | GEN12_PIPELINE_SELECT_3D);

	/* STATE_BASE_ADDRESS anchors the heaps; the addresses land at bring-up. */
	i915_vk_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_STATE_BASE_ADDRESS, GEN12_STATE_BASE_ADDRESS_DWORDS));
	i915_vk_batch_pad(batch, GEN12_STATE_BASE_ADDRESS_DWORDS - 1U);
}

/* Appends one dword to a batch, latching overflow. */
static void
i915_vk_batch_emit(
	struct i915_vk_batch *batch,
	uint32_t dword)
{
	/* A prior error or a full batch drops the write. */
	if (batch->error != 0)
		return;
	if (batch->cursor >= batch->capacity) {
		batch->error = 1;
		return;
	}

	batch->map[batch->cursor] = dword;
	batch->cursor++;
}

/* Appends a run of zero dwords to a batch. */
static void
i915_vk_batch_pad(
	struct i915_vk_batch *batch,
	uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		i915_vk_batch_emit(batch, 0U);
}
