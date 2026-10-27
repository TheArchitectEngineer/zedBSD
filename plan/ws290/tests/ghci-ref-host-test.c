/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The reference test of the GHCI 1.0 driver's layouts: the size of each
 * command packet and the offsets of the record fields, as the driver's
 * own constants give them, against amaterrace-gpu's include/ghci_cmd.h
 * (ghci-ref-layout.c, compiled against a copy of that header).  It is an
 * independent check of the driver's reading of docs/ghci.md.  Variable
 * packets (NOP, BIND_VERTEX_BUFFERS, LOAD_CONST, DISP_SET) and
 * DISPATCH_INDIRECT, which the header has no structure for, are not
 * compared.
 */

#include <stdint.h>
#include <stdio.h>

#include "drivers/gpu/ghci/ghci-cmd.h"

/* The number of values compared. */
#define REF_VALUES			56U

uint32_t ref_layout(uint32_t *values);

/*
 * The names of the values in ref_layout's order, and the driver's
 * values, in bytes.
 */
static const char *const ref_names[REF_VALUES] = {
	"BIND_PIPELINE", "BIND_INDEX_BUFFER", "BIND_DESCRIPTOR_TABLE", "SET_VIEWPORT", "SET_SCISSOR",
	"SET_DYNAMIC_STATE", "DRAW", "DRAW_INDEXED", "DRAW_INDIRECT", "DRAW_INDIRECT_COUNT",
	"DRAW_MESH_TASKS", "DISPATCH", "BARRIER", "FENCE", "QUERY", "VM_MAP", "VM_UNMAP",
	"SESSION_OPEN", "SESSION_CLOSE", "BIND_SESSION", "BEGIN_RENDER_PASS", "END_RENDER_PASS",
	"WAIT_FENCE", "WRITE_TIMESTAMP", "COPY_BUFFER", "COPY_IMAGE", "BLIT", "FILL_BUFFER",
	"RESOLVE_IMAGE", "COPY_BUFFER_TO_IMAGE", "COPY_IMAGE_TO_BUFFER", "FLIP",
	"PSO size", "PSO flags", "PSO blend", "PSO limits", "PSO lds", "PSO attributes",
	"texture size", "texture format", "texture row pitch", "texture samples", "texture flags",
	"texture swizzle", "sampler size", "sampler lod bias", "sampler unnormalized",
	"buffer size", "buffer stride", "buffer flags",
	"render pass size", "render pass depth", "render pass resolve", "render pass layers",
	"render pass clear table", "attachment ops"
};

/* The driver's values in the same order. */
static const uint32_t driver_values[REF_VALUES] = {
	4U * GHCI_DW_BIND_PIPELINE, 4U * GHCI_DW_BIND_INDEX_BUFFER, 4U * GHCI_DW_BIND_DESCRIPTOR_TABLE,
	4U * GHCI_DW_SET_VIEWPORT, 4U * GHCI_DW_SET_SCISSOR, 4U * GHCI_DW_SET_DYNAMIC_STATE,
	4U * GHCI_DW_DRAW, 4U * GHCI_DW_DRAW_INDEXED, 4U * GHCI_DW_DRAW_INDIRECT,
	4U * GHCI_DW_DRAW_INDIRECT_COUNT, 4U * GHCI_DW_DRAW_MESH_TASKS, 4U * GHCI_DW_DISPATCH,
	4U * GHCI_DW_BARRIER, 4U * GHCI_DW_FENCE, 4U * GHCI_DW_QUERY, 4U * GHCI_DW_VM_MAP,
	4U * GHCI_DW_VM_UNMAP, 4U * GHCI_DW_SESSION_OPEN, 4U * GHCI_DW_SESSION_CLOSE,
	4U * GHCI_DW_BIND_SESSION, 4U * GHCI_DW_BEGIN_RENDER_PASS, 4U * GHCI_DW_END_RENDER_PASS,
	4U * GHCI_DW_WAIT_FENCE, 4U * GHCI_DW_WRITE_TIMESTAMP, 4U * GHCI_DW_COPY_BUFFER,
	4U * GHCI_DW_COPY_IMAGE, 4U * GHCI_DW_BLIT, 4U * GHCI_DW_FILL_BUFFER,
	4U * GHCI_DW_RESOLVE_IMAGE, 4U * GHCI_DW_BUFFER_IMAGE, 4U * GHCI_DW_BUFFER_IMAGE,
	4U * GHCI_DW_FLIP,
	GHCI_PSO_BYTES, GHCI_PSO_FLAGS, GHCI_PSO_BLEND, GHCI_PSO_LIMITS, GHCI_PSO_LDS_FLAGS, GHCI_PSO_ATTRIBUTES,
	GHCI_TEXTURE_DESC_BYTES, GHCI_TEX_FORMAT, GHCI_TEX_ROW_PITCH, GHCI_TEX_SAMPLES, GHCI_TEX_FLAGS,
	GHCI_TEX_SWIZZLE, GHCI_SAMPLER_DESC_BYTES, GHCI_SAMPLER_LOD_BIAS, GHCI_SAMPLER_UNNORMALIZED,
	GHCI_BUFFER_DESC_BYTES, GHCI_BUFFER_STRIDE, GHCI_BUFFER_FLAGS,
	GHCI_RENDER_PASS_BYTES, GHCI_RP_DEPTH_STENCIL, GHCI_RP_RESOLVE, GHCI_RP_LAYERS,
	GHCI_RP_CLEAR_TABLE, 13U
};

/*
 * Compares every value and reports the differences.
 */
int
main(void)
{
	uint32_t values[REF_VALUES];
	uint32_t count;
	uint32_t index;
	unsigned failed;

	/* Collects the reference header's values. */
	count = ref_layout(values);
	if (count != REF_VALUES) {
		printf("FAIL ref count %u\n", count);
		return 1;
	}

	/* Compares each value with the driver's. */
	failed = 0U;
	for (index = 0U; index < REF_VALUES; index++) {
		if (values[index] != driver_values[index]) {
			printf("FAIL %s: header %u, driver %u\n", ref_names[index], values[index], driver_values[index]);
			failed++;
		}
	}

	/* Reports the counts; any difference fails the program. */
	printf("ghci-ref-host-test: %u passed, %u failed\n", REF_VALUES - failed, failed);
	if (failed != 0U)
		return 1;

	/* Succeeded: the driver's layouts are the header's. */
	return 0;
}
