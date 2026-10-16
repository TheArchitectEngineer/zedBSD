/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: a reference interpreter of the shared scalar IR, run against
 * the GFX10 interpreter (gfx10-machine.h).
 *
 * It runs every invocation of one compute workgroup in lockstep straight
 * from the IR: each instruction for every invocation still running, a loop
 * repeated while any invocation's condition holds.  It knows nothing of
 * GFX10: what it computes is what ir.h says.
 */

#ifndef PLAN_WS039_TESTS_GFX10_REFERENCE_H
#define PLAN_WS039_TESTS_GFX10_REFERENCE_H

#include "drivers/gpu/compiler/ir.h"
#include "plan/ws039/tests/gfx10-texture.h"

#include <stdint.h>

/* The most uniforms of a program the reference binds. */
#define REFERENCE_UNIFORMS	32U

/*
 * The memory one workgroup sees: each uniform's bytes (by its index in the
 * IR's list) and the push constants.
 */
struct reference_memory {
	uint8_t *bytes[REFERENCE_UNIFORMS];
	uint32_t size[REFERENCE_UNIFORMS];
	const uint8_t *push;
	uint32_t push_size;
};

/* The output slots of a graphics run: the locations, then the position and the point size. */
#define REFERENCE_SLOT_POSITION		32U
#define REFERENCE_SLOT_POINT_SIZE	33U
#define REFERENCE_SLOT_SECOND_COLOR	34U
#define REFERENCE_SLOTS			35U

/*
 * One wave of a vertex or pixel program for the reference: the lanes that
 * run, and of them the helper lanes (a pixel's quad neighbours that are
 * not covered: they run but write no storage); a vertex lane's fetched locations and its vertex and instance
 * numbers; a pixel lane's interpolated attributes, position (w already
 * 1 / w) and face (a Boolean); and what the run left: each lane's outputs
 * by slot with a mark per channel written, and its discard.
 */
struct reference_graphics {
	uint32_t lanes;
	uint8_t run[32];
	uint8_t helper[32];
	uint32_t input[32][32][4];
	uint32_t vertex_index[32];
	uint32_t instance_index[32];
	uint32_t frag_coord[32][4];
	uint32_t front_facing[32];
	uint32_t point_coord[32][2];
	uint32_t primitive_id[32];
	uint32_t output[32][REFERENCE_SLOTS][4];
	uint8_t written[32][REFERENCE_SLOTS][4];
	uint8_t killed[32];

	/* Answers a texture request to the sampled image (set, binding); the four channels to out. */
	void (*sample)(void *context, uint32_t set, uint32_t binding, const struct texture_request *request, uint32_t *out);
	void *sample_context;
};

/* Runs one wave of a vertex or pixel program; as reference_run_group(). */
const char *reference_run_graphics(const struct drv_gpu_shader_ir *ir, struct reference_memory *memory, struct reference_graphics *graphics, int *undefined);

/*
 * Runs one compute workgroup of a parsed program.
 *
 * Returns NULL, or why the run stopped; *undefined is set when the program
 * met something the IR leaves undefined (a division by zero, an
 * out-of-range conversion), whose results are not compared.
 */
const char *reference_run_group(const struct drv_gpu_shader_ir *ir, const uint32_t *group_id, struct reference_memory *memory, int *undefined);

#endif /* PLAN_WS039_TESTS_GFX10_REFERENCE_H */
