/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws037-p008: a reference interpreter of the shared scalar IR
 * (sm75-reference.c), run against the SM75 interpreter (sm75-machine.h).
 *
 * It runs one vertex or fragment invocation of a parsed program straight
 * from the IR, reading its inputs from the same run the SM75 interpreter
 * reads (the attribute words, the constant buffers) and answering a texture
 * message with the texel the SM75 interpreter makes, so the two results of
 * one input can be compared.  It knows nothing of the SM75 instructions:
 * what it computes is what the IR says.
 */

#ifndef PLAN_WS037_TESTS_SM75_REFERENCE_H
#define PLAN_WS037_TESTS_SM75_REFERENCE_H

#include "plan/ws037/tests/sm75-machine.h"

#include "drivers/gpu/compiler/ir.h"

#include <stdint.h>

/* The colour targets a fragment program writes. */
#define SM75_REFERENCE_COLORS	8U

/*
 * What one invocation of the reference left: a vertex program's attribute
 * words (by address / 4, with a mark per word written), a fragment
 * program's colours (with a mark per component written) and its discard;
 * undefined when the program met something the IR leaves undefined (a
 * division by zero), failed when it could not be run.
 */
struct sm75_reference_result {
	uint32_t attr_out[256];
	uint8_t attr_written[256];
	uint32_t color[SM75_REFERENCE_COLORS][4];
	uint8_t color_written[SM75_REFERENCE_COLORS][4];
	int killed;
	int undefined;
	int failed;
	const char *why;
};

/* Runs one invocation of a parsed graphics program on the reference; the binary says where its textures and uniform buffers are. */
void sm75_reference_run(const struct drv_gpu_shader_ir *ir, const struct nvrtx_sm75_binary *binary, const struct test_run *run, struct sm75_reference_result *result);

#endif /* PLAN_WS037_TESTS_SM75_REFERENCE_H */
