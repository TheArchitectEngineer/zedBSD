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

/*
 * Runs one compute workgroup of a parsed program.
 *
 * Returns NULL, or why the run stopped; *undefined is set when the program
 * met something the IR leaves undefined (a division by zero, an
 * out-of-range conversion), whose results are not compared.
 */
const char *reference_run_group(const struct drv_gpu_shader_ir *ir, const uint32_t *group_id, struct reference_memory *memory, int *undefined);

#endif /* PLAN_WS039_TESTS_GFX10_REFERENCE_H */
