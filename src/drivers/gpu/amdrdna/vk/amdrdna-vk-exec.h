/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the amdrdna executor that turn draws and transfers into
 * GFX10 work: the state a draw writes (vk-state.c), the image and sampler
 * descriptors (vk-descriptor.c), the executor's own quad programs
 * (vk-quad.c) and the float arithmetic done on bits (vk-float.c), since the
 * kernel has no floating point.
 */

#ifndef DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_EXEC_H
#define DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_EXEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/compiler/ir.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk-gfx10.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk-objects.h"

/* The push constants the executor's quad programs read: the quad's corners, its texture's part, its colour. */
#define AMDRDNA_QUAD_PUSH_RECT		0U
#define AMDRDNA_QUAD_PUSH_TEXTURE	16U
#define AMDRDNA_QUAD_PUSH_COLOUR	32U
#define AMDRDNA_QUAD_PUSH_BYTES		48U

/* The most instructions one of the quad programs has. */
#define AMDRDNA_QUAD_IR_INSTRUCTIONS	32U

/* The words of an image descriptor and of a sampler. */
#define AMDRDNA_IMAGE_WORDS		8U
#define AMDRDNA_SAMPLER_WORDS		4U

/*
 * A colour target: a linear 2D image of four-byte pixels at address
 * (256-byte aligned), its size, and the VkFormat it is written in.  Its
 * rows are its width's bytes rounded up to 256 apart.
 */
struct amdrdna_vk_target {
	uint64_t address;
	uint32_t width;
	uint32_t height;
	uint32_t format;
};

/*
 * The blend of colour target 0: whether it blends, the factors and the
 * functions of the colour and of the alpha (VkBlendFactor, VkBlendOp), and
 * the channels written.
 */
struct amdrdna_vk_blend {
	uint32_t enable;
	uint32_t colour_source;
	uint32_t colour_destination;
	uint32_t colour_op;
	uint32_t alpha_source;
	uint32_t alpha_destination;
	uint32_t alpha_op;
	uint32_t write_mask;
};

/*
 * One of the quad programs being built: the IR and its storage (the
 * instructions, the interface records and the one sampled image).
 */
struct amdrdna_quad_ir {
	struct drv_gpu_shader_ir ir;
	struct drv_gpu_shader_ir_inst instructions[AMDRDNA_QUAD_IR_INSTRUCTIONS];
	struct drv_gpu_shader_ir_io input;
	struct drv_gpu_shader_ir_io output;
	struct drv_gpu_shader_ir_uniform uniform;
};

void amdrdna_vk_state_start(struct amdrdna_pm4 *pm4);
int amdrdna_vk_state_target(struct amdrdna_pm4 *pm4, const struct amdrdna_vk_target *target);
void amdrdna_vk_state_viewport(struct amdrdna_pm4 *pm4, const uint32_t *viewport);
void amdrdna_vk_state_scissor(struct amdrdna_pm4 *pm4, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
int amdrdna_vk_state_blend(struct amdrdna_pm4 *pm4, const struct amdrdna_vk_blend *blend);
void amdrdna_vk_state_raster(struct amdrdna_pm4 *pm4, uint32_t cull_mode, uint32_t front_face);
void amdrdna_vk_state_programs(struct amdrdna_pm4 *pm4, const struct amdrdna_vk_program *vertex, const struct amdrdna_vk_program *fragment, uint64_t vertex_table, uint64_t fragment_table, uint32_t base_vertex, uint32_t first_instance, uint32_t instance_mask);
int amdrdna_vk_state_draw(struct amdrdna_pm4 *pm4, uint32_t topology, uint32_t vertex_count, uint32_t instance_count);
void amdrdna_vk_state_kernel(struct amdrdna_pm4 *pm4, const struct amdrdna_vk_program *kernel, uint64_t table);
void amdrdna_vk_state_barrier(struct amdrdna_pm4 *pm4, uint64_t sync_address, uint32_t sync_value);
bool amdrdna_vk_target_format(uint32_t format, uint32_t *layout);
uint32_t amdrdna_vk_pitch(uint32_t width);

bool amdrdna_vk_image_format(uint32_t format, uint32_t *code, bool *alternate);
int amdrdna_vk_image_descriptor(uint64_t address, uint32_t width, uint32_t height, uint32_t format, const uint32_t *swizzle, uint32_t *words);
void amdrdna_vk_sampler_descriptor(const struct amdrdna_vk_sampler *sampler, uint32_t *words);
bool amdrdna_vk_vertex_format(uint32_t format, uint32_t *code, uint32_t *components, uint32_t *bytes);

void amdrdna_quad_vertex_ir(struct amdrdna_quad_ir *program, int textured);
void amdrdna_quad_fragment_ir(struct amdrdna_quad_ir *program, int textured);

uint32_t amdrdna_vk_float(int32_t value);
int64_t amdrdna_vk_fixed(uint32_t bits, uint32_t fraction);
uint32_t amdrdna_vk_float_fixed(int64_t value, uint32_t fraction);

#endif /* DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_EXEC_H */
