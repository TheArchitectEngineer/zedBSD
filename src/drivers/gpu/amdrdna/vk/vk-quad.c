/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's own quad programs (amdrdna-vk-exec.h), built as IR for
 * the amdrdna compiler: a quad placed by push constants, and either one
 * colour or a texture read at the push constants' texture rectangle.
 * Copies, blits and clears are drawn with them.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"

/*
 * The float bits of 0 and 1.
 *
 * Values referenced: IEEE 754-2008 binary32 (sign 31, biased exponent
 * 30:23 with a bias of 127, fraction 22:0).
 */
#define QUAD_FLOAT_ZERO		0x00000000U
#define QUAD_FLOAT_ONE		0x3f800000U

static void quad_ir_load(struct drv_gpu_shader_ir_inst *list, uint32_t *count, uint32_t dst, enum drv_gpu_shader_ir_op op, uint32_t location, uint32_t component, uint32_t immediate);
static void quad_ir_mix(struct drv_gpu_shader_ir_inst *list, uint32_t *count, uint32_t base, uint32_t corner, uint32_t from, uint32_t to);
static void quad_ir_store(struct drv_gpu_shader_ir_inst *list, uint32_t *count, uint32_t location, uint32_t component, uint32_t value);

/*
 * Builds the IR of the quad's vertex program, as the frontend would parse
 *
 *   layout(push_constant) uniform Q { vec4 rect; vec4 texture; vec4 colour; } q;
 *   layout(location = 0) in vec2 corner;
 *   layout(location = 0) out vec2 texcoord;      (textured only)
 *   void main() {
 *       gl_Position = vec4(mix(q.rect.xy, q.rect.zw, corner), 0.0, 1.0);
 *       texcoord = mix(q.texture.xy, q.texture.zw, corner);
 *   }
 *
 * with mix written as a + (b - a) * t.
 */
void
amdrdna_quad_vertex_ir(
	struct amdrdna_quad_ir *program,
	int textured)
{
	struct drv_gpu_shader_ir_inst *list;
	struct drv_gpu_shader_ir *ir;
	uint32_t count;

	/* Starts empty, the instructions appended in order. */
	kern_memset(program, 0, sizeof(*program));
	list = program->instructions;
	ir = &program->ir;
	count = 0U;

	/* v0, v1 = the corner; v2..v5 = the rectangle's corners. */
	quad_ir_load(list, &count, 0U, DRV_GPU_IR_LOAD_INPUT, 0U, 0U, 0U);
	quad_ir_load(list, &count, 1U, DRV_GPU_IR_LOAD_INPUT, 0U, 1U, 0U);
	quad_ir_load(list, &count, 2U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_RECT);
	quad_ir_load(list, &count, 3U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_RECT + 4U);
	quad_ir_load(list, &count, 4U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_RECT + 8U);
	quad_ir_load(list, &count, 5U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_RECT + 12U);

	/* v8 = x, v11 = y; v12 = 0, v13 = 1; the position. */
	quad_ir_mix(list, &count, 6U, 0U, 2U, 4U);
	quad_ir_mix(list, &count, 9U, 1U, 3U, 5U);
	quad_ir_load(list, &count, 12U, DRV_GPU_IR_CONST, 0U, 0U, QUAD_FLOAT_ZERO);
	quad_ir_load(list, &count, 13U, DRV_GPU_IR_CONST, 0U, 0U, QUAD_FLOAT_ONE);
	quad_ir_store(list, &count, DRV_GPU_IR_LOCATION_POSITION, 0U, 8U);
	quad_ir_store(list, &count, DRV_GPU_IR_LOCATION_POSITION, 1U, 11U);
	quad_ir_store(list, &count, DRV_GPU_IR_LOCATION_POSITION, 2U, 12U);
	quad_ir_store(list, &count, DRV_GPU_IR_LOCATION_POSITION, 3U, 13U);
	ir->value_count = 14U;

	/* Textured: v14..v17 = the texture's part, v20 and v23 = the coordinate at location 0. */
	if (textured != 0) {
		quad_ir_load(list, &count, 14U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_TEXTURE);
		quad_ir_load(list, &count, 15U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_TEXTURE + 4U);
		quad_ir_load(list, &count, 16U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_TEXTURE + 8U);
		quad_ir_load(list, &count, 17U, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_TEXTURE + 12U);
		quad_ir_mix(list, &count, 18U, 0U, 14U, 16U);
		quad_ir_mix(list, &count, 21U, 1U, 15U, 17U);
		quad_ir_store(list, &count, 0U, 0U, 20U);
		quad_ir_store(list, &count, 0U, 1U, 23U);
		ir->value_count = 24U;
		program->output.location = 0U;
		program->output.components = 2U;
		ir->outputs = &program->output;
		ir->output_count = 1U;
	}

	/* The corner is input 0. */
	program->input.location = 0U;
	program->input.components = 2U;

	/* The program. */
	ir->stage = DRV_GPU_STAGE_VERTEX;
	ir->inputs = &program->input;
	ir->input_count = 1U;
	ir->instructions = list;
	ir->instruction_count = count;
	ir->push_bytes = AMDRDNA_QUAD_PUSH_BYTES;
}

/*
 * Builds the IR of the quad's fragment program, as the frontend would
 * parse
 *
 *   layout(location = 0) out vec4 colour;
 *   void main() { colour = q.colour; }                       (one colour)
 *
 *   layout(set = 0, binding = 0) uniform sampler2D image;
 *   layout(location = 0) in vec2 texcoord;
 *   void main() { colour = texture(image, texcoord); }       (textured)
 */
void
amdrdna_quad_fragment_ir(
	struct amdrdna_quad_ir *program,
	int textured)
{
	struct drv_gpu_shader_ir_inst *list;
	struct drv_gpu_shader_ir *ir;
	uint32_t component;
	uint32_t colour;
	uint32_t count;

	/* Starts empty, the instructions appended in order. */
	kern_memset(program, 0, sizeof(*program));
	list = program->instructions;
	ir = &program->ir;
	count = 0U;

	/*
	 * Textured: v0, v1 = the coordinate, v2..v5 = the sample of image
	 * (0, 0) there.  One colour: v0..v3 = the push constants' colour.
	 */
	if (textured != 0) {
		quad_ir_load(list, &count, 0U, DRV_GPU_IR_LOAD_INPUT, 0U, 0U, 0U);
		quad_ir_load(list, &count, 1U, DRV_GPU_IR_LOAD_INPUT, 0U, 1U, 0U);
		list[count].op = DRV_GPU_IR_SAMPLE;
		list[count].dst = 2U;
		list[count].src[0] = 0U;
		list[count].src[1] = 1U;
		list[count].location = 0U;
		list[count].immediate = 0U;
		count++;
		colour = 2U;
		program->input.location = 0U;
		program->input.components = 2U;
		program->uniform.set = 0U;
		program->uniform.binding = 0U;
		program->uniform.kind = DRV_GPU_IR_UNIFORM_SAMPLED_IMAGE;
		ir->inputs = &program->input;
		ir->input_count = 1U;
		ir->uniforms = &program->uniform;
		ir->uniform_count = 1U;
	} else {
		for (component = 0U; component < 4U; component++)
			quad_ir_load(list, &count, component, DRV_GPU_IR_LOAD_PUSH, 0U, 0U, AMDRDNA_QUAD_PUSH_COLOUR + component * 4U);
		colour = 0U;
		ir->push_bytes = AMDRDNA_QUAD_PUSH_BYTES;
	}

	/* The four channels to colour 0. */
	for (component = 0U; component < 4U; component++)
		quad_ir_store(list, &count, 0U, component, colour + component);
	program->output.location = 0U;
	program->output.components = 4U;

	/* The program. */
	ir->stage = DRV_GPU_STAGE_FRAGMENT;
	ir->outputs = &program->output;
	ir->output_count = 1U;
	ir->instructions = list;
	ir->instruction_count = count;
	ir->value_count = colour + 4U;
}

/* Appends base = to - from, base + 1 = base * corner, base + 2 = from + base + 1: from mixed toward to by corner. */
static void
quad_ir_mix(
	struct drv_gpu_shader_ir_inst *list,
	uint32_t *count,
	uint32_t base,
	uint32_t corner,
	uint32_t from,
	uint32_t to)
{
	struct drv_gpu_shader_ir_inst *instruction;

	/* The span. */
	instruction = &list[(*count)++];
	instruction->op = DRV_GPU_IR_FSUB;
	instruction->dst = base;
	instruction->src[0] = to;
	instruction->src[1] = from;

	/* The part of it the corner takes. */
	instruction = &list[(*count)++];
	instruction->op = DRV_GPU_IR_FMUL;
	instruction->dst = base + 1U;
	instruction->src[0] = base;
	instruction->src[1] = corner;

	/* From the start. */
	instruction = &list[(*count)++];
	instruction->op = DRV_GPU_IR_FADD;
	instruction->dst = base + 2U;
	instruction->src[0] = from;
	instruction->src[1] = base + 1U;
}

/* Appends an instruction that defines dst without sources: an input, a push constant word or a constant. */
static void
quad_ir_load(
	struct drv_gpu_shader_ir_inst *list,
	uint32_t *count,
	uint32_t dst,
	enum drv_gpu_shader_ir_op op,
	uint32_t location,
	uint32_t component,
	uint32_t immediate)
{
	struct drv_gpu_shader_ir_inst *instruction;

	/* The next instruction. */
	instruction = &list[(*count)++];
	instruction->op = op;
	instruction->dst = dst;
	instruction->location = location;
	instruction->component = component;
	instruction->immediate = immediate;
}

/* Appends the store of a value to an output's component. */
static void
quad_ir_store(
	struct drv_gpu_shader_ir_inst *list,
	uint32_t *count,
	uint32_t location,
	uint32_t component,
	uint32_t value)
{
	struct drv_gpu_shader_ir_inst *instruction;

	/* The next instruction. */
	instruction = &list[(*count)++];
	instruction->op = DRV_GPU_IR_STORE_OUTPUT;
	instruction->location = location;
	instruction->component = component;
	instruction->src[0] = value;
}
