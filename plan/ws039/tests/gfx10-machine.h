/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: an interpreter of the GFX10 (RDNA1) instructions the amdrdna
 * shader compiler emits, for the host tests.
 *
 * It decodes the code words itself (it does not use the compiler's
 * encoder), runs every wave of one workgroup as wave32, and checks what
 * the hardware does not: a register a memory instruction fills is not
 * read or written before the wait for its counter, the four GFX10.1
 * hazards are broken, and no branch has the distance GFX10 mishandles.
 */

#ifndef PLAN_WS039_TESTS_GFX10_MACHINE_H
#define PLAN_WS039_TESTS_GFX10_MACHINE_H

#include <stdint.h>

/* The lanes of a wave, the most regions of memory, and the most waves of one workgroup. */
#define MACHINE_LANES		32U
#define MACHINE_REGIONS		40U
#define MACHINE_WAVES		32U

/* One range of GPU memory: its address, its bytes on the host, and its size. */
struct machine_region {
	uint64_t address;
	uint8_t *bytes;
	uint32_t size;
};

/* The GPU memory a dispatch sees. */
struct machine_memory {
	struct machine_region regions[MACHINE_REGIONS];
	uint32_t count;
};

/*
 * One workgroup to run: the code, the resource table's address (s[0:1]),
 * the workgroup's size and id, and the shared memory bytes.
 */
struct machine_dispatch {
	const uint32_t *code;
	uint32_t code_words;
	uint64_t table;
	uint32_t local_size[3];
	uint32_t group_id[3];
	uint32_t shared_bytes;
	uint32_t vgpr_count;
	uint32_t pixel_inputs;
};

/*
 * What a run met: the GFX10 instructions executed, and the first error
 * (NULL for none) with the word offset it was met at.
 */
struct machine_result {
	uint64_t steps;
	const char *error;
	uint32_t error_pc;
	uint32_t error_word;
};

/* The export targets by code (colours 0..7, nothing 9, positions 12..15, parameters 32..63). */
#define MACHINE_TARGETS		64U

/*
 * One wave of a vertex or pixel program: what the hardware delivers, and
 * what the program exported.
 *
 * A vertex wave: user[0..2] are s2..s4 (the base vertex, the first
 * instance, the instance mask), lanes run vertex ids from first_vertex_id
 * (v0) with the instance id (v3) for every lane, `lanes` of them.
 *
 * A pixel wave: `coverage` lanes run; each lane has its barycentrics, its
 * position (x, y, z, w) and face, and the primitive whose attributes it
 * interpolates: attribute[primitive][location][channel][vertex].
 */
struct machine_graphics {
	uint32_t user[3];
	uint32_t lanes;
	uint32_t first_vertex_id;
	uint32_t instance_id;
	uint32_t coverage;
	float perspective[MACHINE_LANES][2];
	float linear[MACHINE_LANES][2];
	float position[MACHINE_LANES][4];
	float face[MACHINE_LANES];
	uint32_t primitive[MACHINE_LANES];
	const float (*attribute)[32][4][3];

	/* What the program exported: by target, the values of each lane, the channels enabled and the lanes exported. */
	uint32_t exported[MACHINE_TARGETS][MACHINE_LANES][4];
	uint32_t export_channels[MACHINE_TARGETS];
	uint32_t export_lanes[MACHINE_TARGETS];
	uint32_t export_flags[MACHINE_TARGETS];
	uint32_t export_count;
};

/* Runs one wave of a vertex (stage 0) or pixel (stage 1) program. */
void machine_run_graphics(const struct machine_dispatch *dispatch, struct machine_memory *memory, int pixel, struct machine_graphics *graphics, struct machine_result *result);

/* Runs one compute workgroup; result->error says why it stopped early. */
void machine_run_group(const struct machine_dispatch *dispatch, struct machine_memory *memory, struct machine_result *result);

/* Adds a region to the memory; returns its address (regions are placed 256 MiB apart from 0x100000000). */
uint64_t machine_add_region(struct machine_memory *memory, uint8_t *bytes, uint32_t size);

#endif /* PLAN_WS039_TESTS_GFX10_MACHINE_H */
