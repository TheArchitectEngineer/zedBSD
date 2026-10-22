/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The self-test's V3D 7.1 compute shaders (v3d71-cl.c) checked against the
 * unmodified MIT Mesa 7.1 packer (ws279-p003).
 *
 * Each word the driver carries is packed again by Mesa from the
 * instruction it stands for, and must be the same word.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_instr.h"
#include "drivers/gpu/bcm2711/v3d71-private.h"

/* The 7.1.10 engine of the Raspberry Pi 5 (D0). */
static const struct v3d_device_info device = { .ver = 71, .rev = 10 };

static void nop_instruction(struct v3d_qpu_instr *instruction);
static void move_instruction(struct v3d_qpu_instr *instruction, uint8_t destination, bool magic, uint8_t source);
static int compare(const char *name, const uint64_t *words, const struct v3d_qpu_instr *instructions, unsigned count);

/* Packs the shaders' instructions with Mesa and compares the words. */
int
main(void)
{
	struct v3d_qpu_instr empty[BCM2712_V3D_EMPTY_SHADER_WORDS];
	struct v3d_qpu_instr store[BCM2712_V3D_STORE_SHADER_WORDS];
	unsigned index;
	int failures;

	/* The empty shader: the thread end and its two delay slots. */
	for (index = 0; index < BCM2712_V3D_EMPTY_SHADER_WORDS; index++)
		nop_instruction(&empty[index]);
	empty[0].sig.thrsw = true;

	/* The store shader: two uniform loads, the TMU write, its wait and the end. */
	for (index = 0; index < BCM2712_V3D_STORE_SHADER_WORDS; index++)
		nop_instruction(&store[index]);
	store[0].sig.ldunif = true;
	move_instruction(&store[1], 1, false, 0);
	store[1].sig.ldunif = true;
	move_instruction(&store[2], V3D_QPU_WADDR_TMUD, true, 0);
	move_instruction(&store[3], V3D_QPU_WADDR_TMUA, true, 1);
	store[4].alu.add.op = V3D_QPU_A_TMUWT;
	store[5].sig.thrsw = true;

	/* Compares both shaders. */
	failures = 0;
	failures += compare("empty", bcm2712_v3d_empty_shader, empty, BCM2712_V3D_EMPTY_SHADER_WORDS);
	failures += compare("store", bcm2712_v3d_store_shader, store, BCM2712_V3D_STORE_SHADER_WORDS);
	if (failures != 0) {
		printf("v3d71-shader-host-test: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every word is Mesa's packing of its instruction. */
	printf("v3d71-shader-host-test: PASS (%u words)\n", BCM2712_V3D_EMPTY_SHADER_WORDS + BCM2712_V3D_STORE_SHADER_WORDS);
	return 0;
}

/* Fills an ALU instruction that does nothing on either unit. */
static void
nop_instruction(
	struct v3d_qpu_instr *instruction)
{
	/* Both units do NOP and write the magic NOP address. */
	memset(instruction, 0, sizeof(*instruction));
	instruction->type = V3D_QPU_INSTR_TYPE_ALU;
	instruction->alu.add.op = V3D_QPU_A_NOP;
	instruction->alu.add.waddr = V3D_QPU_WADDR_NOP;
	instruction->alu.add.magic_write = true;
	instruction->alu.mul.op = V3D_QPU_M_NOP;
	instruction->alu.mul.waddr = V3D_QPU_WADDR_NOP;
	instruction->alu.mul.magic_write = true;
}

/* Makes the add unit move one register to a register or a magic address. */
static void
move_instruction(
	struct v3d_qpu_instr *instruction,
	uint8_t destination,
	bool magic,
	uint8_t source)
{
	/* The add unit's MOV reads its source on the a port. */
	instruction->alu.add.op = V3D_QPU_A_MOV;
	instruction->alu.add.waddr = destination;
	instruction->alu.add.magic_write = magic;
	instruction->alu.add.a.raddr = source;
}

/* Packs each instruction and compares it with the driver's word, printing each. */
static int
compare(
	const char *name,
	const uint64_t *words,
	const struct v3d_qpu_instr *instructions,
	unsigned count)
{
	uint64_t packed;
	unsigned index;
	int failures;
	bool ok;

	/* Packs and compares every word. */
	failures = 0;
	for (index = 0; index < count; index++) {
		/* Refuses an instruction Mesa cannot pack or a different word. */
		ok = v3d_qpu_instr_pack(&device, &instructions[index], &packed);
		if (!ok || packed != words[index]) {
			printf("%s[%u]: driver %016llx Mesa %016llx%s\n",
			       name,
			       index,
			       (unsigned long long)words[index],
			       (unsigned long long)packed,
			       ok ? "" : " (unpackable)");
			failures++;
		}
	}

	/* Reports how many words differed. */
	return failures;
}
