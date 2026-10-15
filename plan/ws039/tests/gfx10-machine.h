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

/* Runs one compute workgroup; result->error says why it stopped early. */
void machine_run_group(const struct machine_dispatch *dispatch, struct machine_memory *memory, struct machine_result *result);

/* Adds a region to the memory; returns its address (regions are placed 64 KiB apart from 0x100000000). */
uint64_t machine_add_region(struct machine_memory *memory, uint8_t *bytes, uint32_t size);

/* Writes a raw buffer descriptor (V#) of a region at a byte offset into a table. */
void machine_raw_descriptor(uint8_t *table, uint32_t offset, uint64_t address, uint32_t size);

#endif /* PLAN_WS039_TESTS_GFX10_MACHINE_H */
