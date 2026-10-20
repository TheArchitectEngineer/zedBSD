/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */
/*
 * Host probe: compiles every shader of the Keiland compositor
 * (userland/desktop/wayland/shaders.h) with the PowerVR compiler, keyed
 * as the compositor draws (its vertex attributes, a B8G8R8A8_UNORM
 * target, premultiplied blending), and prints each result.
 *   shader-probe
 */
#include "src/drivers/gpu/powervr/shader.h"
#include "userland/desktop/wayland/shaders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_ROOM	65536U

/* VkFormat and VkBlendFactor values. */
#define FORMAT_R32_SFLOAT	100U
#define FORMAT_R32G32_SFLOAT	103U
#define FORMAT_R32G32B32A32_SFLOAT 109U
#define FORMAT_B8G8R8A8_UNORM	44U
#define BLEND_ONE		1U
#define BLEND_ONE_MINUS_SRC_ALPHA 7U

static uint8_t code[CODE_ROOM];

/* Compiles one module and prints its outcome; returns 1 on failure. */
static int
probe(
	struct powervr_shader_workspace *workspace,
	const uint32_t *words,
	size_t size,
	uint32_t stage,
	const char *name)
{
	struct powervr_shader_program program;
	struct powervr_shader_key key;
	int error;

	/* The compositor's vertex input and target. */
	memset(&key, 0, sizeof(key));
	/* The batch's attributes (batch.c): place, box, colour and radius; others read location 0 only. */
	key.attribute_formats[0] = FORMAT_R32G32_SFLOAT;
	key.attribute_formats[1] = FORMAT_R32G32B32A32_SFLOAT;
	key.attribute_formats[2] = FORMAT_R32G32B32A32_SFLOAT;
	key.attribute_formats[3] = FORMAT_R32_SFLOAT;
	key.output_formats[0] = FORMAT_B8G8R8A8_UNORM;
	key.blend[0].enable = 1;
	key.blend[0].source_color = BLEND_ONE;
	key.blend[0].destination_color = BLEND_ONE_MINUS_SRC_ALPHA;
	key.blend[0].source_alpha = BLEND_ONE;
	key.blend[0].destination_alpha = BLEND_ONE_MINUS_SRC_ALPHA;
	key.blend[0].write_mask = 0xf;
	key.samples = 1;

	/* One compile, its outcome and size. */
	memset(&program, 0, sizeof(program));
	error = powervr_shader_compile(words, size / 4U, stage, &key, workspace, &program, code, sizeof(code));
	printf("%-16s %s error=%d length=%zu temps=%u\n", name, error == 0 ? "OK  " : "FAIL", error, program.length, program.temps);
	return error != 0;
}

int
main(void)
{
	struct powervr_shader_workspace *workspace;
	unsigned index;
	int failed;
	char name[32];

	workspace = calloc(1, powervr_shader_workspace_size());
	if (workspace == NULL)
		return 2;
	failed = 0;
	failed += probe(workspace, kwl_quad_vert, sizeof(kwl_quad_vert), POWERVR_SHADER_VERTEX, "quad.vert");
	failed += probe(workspace, kwl_quad_frag, sizeof(kwl_quad_frag), POWERVR_SHADER_FRAGMENT, "quad.frag");
	failed += probe(workspace, kwl_panel_vert, sizeof(kwl_panel_vert), POWERVR_SHADER_VERTEX, "panel.vert");
	failed += probe(workspace, kwl_panel_frag, sizeof(kwl_panel_frag), POWERVR_SHADER_FRAGMENT, "panel.frag");
	failed += probe(workspace, kwl_batch_vert, sizeof(kwl_batch_vert), POWERVR_SHADER_VERTEX, "batch.vert");
	failed += probe(workspace, kwl_batch_frag, sizeof(kwl_batch_frag), POWERVR_SHADER_FRAGMENT, "batch.frag");
	failed += probe(workspace, kwl_turn_vert, sizeof(kwl_turn_vert), POWERVR_SHADER_VERTEX, "turn.vert");
	for (index = 0; index < 8U; index++) {
		snprintf(name, sizeof(name), "panel.frag[%u]", index);
		failed += probe(workspace, kwl_panel_variants[index].words, kwl_panel_variants[index].bytes, POWERVR_SHADER_FRAGMENT, name);
	}
	printf("FAILED %d\n", failed);
	return failed != 0;
}
