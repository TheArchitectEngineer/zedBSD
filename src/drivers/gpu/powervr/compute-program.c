/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The driver's own compute programs for the PowerVR core, built with the
 * shading cluster and data sequencer encoders.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/compute-program.h"
#include "drivers/gpu/powervr/pds-encode.h"
#include "drivers/gpu/powervr/usc-encode.h"

/*
 * The control stream's block types, task targets and shared data loaders,
 * and the field limits of a kernel's words.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/cdm.xml (BLOCK_TYPE
 * COMPUTE_KERNEL 0 and STREAM_TERMINATE 2 at bits 30..31; USC_TARGET ANY
 * 1; SD_TYPE NONE 0 and PDS 1; KERNEL0 usc_unified_size 12..17 in units
 * of 16 bytes, pds_temp_size 8..11 in units of 16, pds_data_size 2..7 in
 * units of 16, usc_target 1, fence 0; KERNEL1 data_addr 4..31 shifted by
 * 4, sd_type 2..3, usc_common_shared 1; KERNEL2 code_addr 4..31 shifted
 * by 4; KERNEL3 to 5 the workgroup counts less one; KERNEL8
 * max_instances 27..31, workgroup_size_x 18..26, _y 9..17, _z 0..8, each
 * size less one).
 */
#define BLOCK_KERNEL		0U
#define BLOCK_TERMINATE		2U
#define BLOCK_SHIFT		30U
#define KERNEL_UNIT_BYTES	16U
#define KERNEL_UNIFIED_MAX	0x3fU
#define KERNEL_TEMPS_MAX	0xfU
#define KERNEL_DATA_MAX		0x3fU
#define KERNEL_ADDRESS_MASK	0xfffffff0U
#define KERNEL_INSTANCES_MAX	0x1fU
#define KERNEL_SIZE_MAX		0x200U

/*
 * The data sequencer's predicates and registers the programs name.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pds/pvr_rogue_pds_defs.h
 * (PVR_ROGUE_PDSINST_PREDICATE_IF1 2, _KEEP 8;
 * PVR_ROGUE_PDSINST_REGS64TP_PTEMP64_LOWER 16).
 */
#define PDS_IF1			2U
#define PDS_KEEP		8U
#define PDS_PERSISTENT_0	16U

/*
 * The instance number register, and the store's data size, cache mode
 * and burst.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_isa.py (special register inst_num
 * 51; F_DSIZE 32bit 0b10; F_CACHEMODE_ST write_through 0b00; F_COUNT_SRC
 * s2 0b0; F_COUNT_OP byp 0b10; F_SHIFT1_OP byp 0b00).
 */
#define USC_INSTANCE_NUMBER	51U
#define USC_SELECT_FT0		0U
#define USC_SELECT_FTE		3U
#define USC_MUX_S1		4U
#define USC_SIZE_32		2U
#define USC_WRITE_THROUGH	0U
#define USC_COUNT_FROM_S2	0U
#define USC_COUNT_PASS		2U
#define USC_SHIFT_PASS		0U

/* The values the temporaries program fills its clobber registers with. */
#define TEMPS_FILL		0xdead0000U

/* What a store program writes: its first word's address and its steps. */
struct store_shape {
	uint32_t base_low;
	uint32_t base_high;
	unsigned steps;
};

/* The groups every store program starts with, before its first step. */
#define STORE_PROLOGUE		6U

/*
 * The groups of one step: the step's offset, the address as the base
 * pair plus the instance's offset plus the step's, the data's move, the
 * store and the wait.  Every step makes its address afresh from values
 * set once (the base pair and the instance's offset), so no address
 * carries from one step to the next.
 */
#define STORE_STEP		6U

/* The groups of the temporaries program. */
#define TEMPS_GROUPS		15U

/* The groups of the fill program. */
#define FILL_GROUPS		14U

/*
 * What a fill program writes: the word of instance 0's address, and the
 * multiplier and addend that make an instance's number its value.
 */
struct fill_shape {
	uint32_t base_low;
	uint32_t base_high;
	uint32_t multiplier;
	uint32_t addend;
};

static int store_fetch(void *context, size_t index, struct powervr_usc_group *group);
static int temps_fetch(void *context, size_t index, struct powervr_usc_group *group);
static int fill_fetch(void *context, size_t index, struct powervr_usc_group *group);
static int immediate_group(struct powervr_usc_group *group, unsigned destination, uint32_t value);
static int pass_group(struct powervr_usc_group *group, unsigned destination, unsigned bank, unsigned source);
static int special_group(struct powervr_usc_group *group, unsigned destination, unsigned source);
static int multiply_add_group(struct powervr_usc_group *group, unsigned destination, unsigned first, unsigned second, unsigned addend);
static int store_group(struct powervr_usc_group *group, unsigned data, unsigned address);
static int add64_group(struct powervr_usc_group *group, unsigned destination, unsigned base, unsigned offset);
static int wait_group(struct powervr_usc_group *group);
static int end_group(struct powervr_usc_group *group);
static void temporary(struct powervr_usc_register *operand, unsigned index);

/* A group with no operation, no operand, a repeat count of one. */
static const struct powervr_usc_group blank_group = {
	.repeat = 1
};

/*
 * Builds the control stream words of one compute kernel.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_compute_generate_control_stream: KERNEL0, 1, 2, then 3, 4, 5
 * without an indirect buffer, then 8; the sizes in units rounded up).
 */
int
powervr_compute_kernel_words(
	const struct powervr_compute_kernel *kernel,
	uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS])
{
	unsigned unified;
	unsigned temps;
	unsigned data;

	/* Refuses a missing kernel or result. */
	if (kernel == NULL || words == NULL)
		return EINVAL;

	/* Refuses flags that are not single bits. */
	if (kernel->fence > 1 ||
	    kernel->shared_from_pds > 1 ||
	    kernel->common_shared > 1 ||
	    kernel->any_cluster > 1)
		return EINVAL;

	/* Refuses program places off the 16-byte alignment. */
	if ((kernel->data_offset & ~KERNEL_ADDRESS_MASK) != 0 || (kernel->code_offset & ~KERNEL_ADDRESS_MASK) != 0)
		return EINVAL;

	/* Refuses a workgroup or task size the fields cannot hold. */
	if (kernel->instances == 0 || kernel->instances > KERNEL_SIZE_MAX || kernel->max_instances > KERNEL_INSTANCES_MAX)
		return EINVAL;

	/* The sizes in 16-byte units, rounded up, each within its field. */
	unified = (kernel->usc_temporaries * 4U + KERNEL_UNIT_BYTES - 1U) / KERNEL_UNIT_BYTES;
	temps = (kernel->pds_temporaries * 4U + KERNEL_UNIT_BYTES - 1U) / KERNEL_UNIT_BYTES;
	data = (kernel->data_words * 4U + KERNEL_UNIT_BYTES - 1U) / KERNEL_UNIT_BYTES;
	if (unified > KERNEL_UNIFIED_MAX || temps > KERNEL_TEMPS_MAX || data > KERNEL_DATA_MAX)
		return EINVAL;

	/* The first word: the kernel block, its sizes, its target and its fence. */
	words[0] = BLOCK_KERNEL << BLOCK_SHIFT;
	words[0] |= unified << 12;
	words[0] |= temps << 8;
	words[0] |= data << 2;
	words[0] |= kernel->any_cluster << 1;
	words[0] |= kernel->fence;

	/* The data segment, who loads the shared registers, and whether the kernel frees them. */
	words[1] = kernel->data_offset;
	words[1] |= kernel->shared_from_pds << 2;
	words[1] |= kernel->common_shared << 1;

	/* The code segment. */
	words[2] = kernel->code_offset;

	/* One workgroup in each dimension, written less one. */
	words[3] = 0;
	words[4] = 0;
	words[5] = 0;

	/* The task size and the workgroup's size, all in x, written less one. */
	words[6] = (uint32_t)kernel->max_instances << 27;
	words[6] |= (uint32_t)(kernel->instances - 1U) << 18;

	/* Succeeded: the kernel's words. */
	return 0;
}

/*
 * Reports the control stream word that ends a stream.
 */
uint32_t
powervr_compute_terminate_word(void)
{
	uint32_t word;

	/* A terminate block has no field but its type. */
	word = BLOCK_TERMINATE << BLOCK_SHIFT;

	/* Succeeded: the word. */
	return word;
}

/*
 * Works out the task issue's temporaries field for a count of
 * registers, assuming the field counts in blocks of unit registers.
 *
 * The unit is still unproven on the hardware (2 or 4), so the caller
 * names the one it assumes.
 */
int
powervr_compute_temporaries_field(
	unsigned temporaries,
	unsigned unit,
	unsigned *field)
{
	unsigned blocks;

	/* Refuses a unit other than the two in question. */
	if ((unit != 2U && unit != 4U) || field == NULL)
		return EINVAL;

	/* Counts the blocks, rounded up, refusing more than the six-bit field holds. */
	blocks = (temporaries + unit - 1U) / unit;
	if (blocks > 0x3fU)
		return EINVAL;

	/* Succeeded: the field. */
	*field = blocks;
	return 0;
}

/*
 * Builds the data sequencer program that issues one shading cluster task
 * per instance group: its data word and its code.
 *
 * The code skips the coefficient update pass and issues the task; the
 * temporaries field is written as given.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pds/pvr_pds.c
 * (pvr_pds_compute_shader with only kick_usc: the branch on IF1 negated
 * past the halt of the coefficient update pass, the task issue from the
 * first 64-bit constant ending the program, then a halt;
 * pvr_pds_setup_doutu: the instance sample rate).
 */
int
powervr_compute_kick_program(
	uint64_t usc_offset,
	unsigned temporaries_field,
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS],
	uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX],
	unsigned *code_words)
{
	uint64_t task;
	int error;

	/* Refuses a missing result or a field wider than six bits. */
	if (data == NULL || code == NULL || code_words == NULL || temporaries_field > 0x3fU)
		return EINVAL;

	/* The task issue word with no temporaries, then the field as given. */
	error = powervr_pds_task_word(usc_offset, 0, 0, 0, &task);
	if (error != 0)
		return error;

	task |= (uint64_t)temporaries_field << 35;
	data[0] = (uint32_t)task;
	data[1] = (uint32_t)(task >> 32);

	/* Skips the coefficient update pass's halt. */
	error = powervr_pds_branch(PDS_IF1, 1, PDS_KEEP, 2, &code[0]);
	if (error != 0)
		return error;

	error = powervr_pds_halt(0, &code[1]);
	if (error != 0)
		return error;

	/* Issues the task from the first 64-bit constant, ending the program. */
	error = powervr_pds_output(0, 1, POWERVR_PDS_OUTPUT_SHADER_TASK, 0, 0, &code[2]);
	if (error != 0)
		return error;

	error = powervr_pds_halt(0, &code[3]);
	if (error != 0)
		return error;

	/* Succeeded: four code words. */
	*code_words = 4;
	return 0;
}

/*
 * Builds the data sequencer program of the fence that ends a compute
 * stream: its zero data word and its code.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pds/pvr_pds.c
 * (pvr_pds_compute_shader with fence and clear_pds_barrier: the branch
 * past the coefficient update pass's halt, the 64-bit add of the zero
 * constant into the first persistent temporary, the compute fence
 * output, then a halt).
 */
int
powervr_compute_fence_program(
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS],
	uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX],
	unsigned *code_words)
{
	int error;

	/* Refuses a missing result. */
	if (data == NULL || code == NULL || code_words == NULL)
		return EINVAL;

	/* The zero constant. */
	data[0] = 0;
	data[1] = 0;

	/* Skips the coefficient update pass's halt. */
	error = powervr_pds_branch(PDS_IF1, 1, PDS_KEEP, 2, &code[0]);
	if (error != 0)
		return error;

	error = powervr_pds_halt(0, &code[1]);
	if (error != 0)
		return error;

	/* Clears the persistent temporary the barrier uses. */
	error = powervr_pds_add64(0, 0, 0, 0, 0, PDS_PERSISTENT_0, &code[2]);
	if (error != 0)
		return error;

	/* The fence, then the end. */
	error = powervr_pds_output(0, 0, POWERVR_PDS_OUTPUT_COMPUTE_FENCE, 0, 0, &code[3]);
	if (error != 0)
		return error;

	error = powervr_pds_halt(0, &code[4]);
	if (error != 0)
		return error;

	/* Succeeded: five code words. */
	*code_words = 5;
	return 0;
}

/*
 * Builds the shading cluster program that writes a run of words.
 *
 * Each of the 32 instances of a task writes every 32nd word, starting
 * at its instance number, for steps steps; each word is written with
 * the low 32 bits of its own device address.  The run must not cross a
 * 4 GiB boundary, as only the low half of the address is advanced.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_trans_nir.c (trans_store_global:
 * the address pair followed by the data in consecutive registers, a
 * 32-bit store with data return counter 0 and burst 1;
 * nir_intrinsic_load_instance_num_pco: a move from the instance number
 * register); pco_schedule.c (a wait for the data return counter after
 * each store); pco_end.c (a no-operation that ends the program).
 */
int
powervr_compute_store_program(
	uint64_t base,
	unsigned steps,
	uint8_t *output,
	size_t room,
	size_t *length)
{
	struct store_shape shape;
	uint64_t last;
	size_t count;
	int error;

	/* Refuses an empty run, a misaligned one, or one whose low half would wrap. */
	if (steps == 0 || (base & 3U) != 0)
		return EINVAL;

	last = (base & 0xffffffffULL) + (uint64_t)steps * POWERVR_COMPUTE_STORE_STRIDE;
	if (last > 0x100000000ULL)
		return EINVAL;

	/* The shape the groups are built from. */
	shape.base_low = (uint32_t)base;
	shape.base_high = (uint32_t)(base >> 32);
	shape.steps = steps;
	count = STORE_PROLOGUE + (size_t)steps * STORE_STEP + 1U;

	/* Encodes the groups one at a time. */
	error = powervr_usc_program_build(count, store_fetch, &shape, output, room, length);
	if (error != 0)
		return error;

	/* Succeeded: the program. */
	return 0;
}

/*
 * Builds the shading cluster program that checks how the task issue's
 * temporaries field counts.
 *
 * Each of the 32 instances writes its own instance number to the word
 * at base plus four times that number.  It first puts the number in its
 * eighth temporary, then fills the four below with other values: if the
 * field allocates fewer than eight temporaries per instance and the
 * instances' temporaries lie next to each other, a neighbour's fill
 * lands on the eighth temporary and the word written is not the number.
 */
int
powervr_compute_temps_program(
	uint64_t base,
	uint8_t *output,
	size_t room,
	size_t *length)
{
	struct store_shape shape;
	int error;

	/* Refuses a misaligned base or one whose low half would wrap. */
	if ((base & 3U) != 0 || (base & 0xffffffffULL) + POWERVR_COMPUTE_INSTANCES * 4U > 0x100000000ULL)
		return EINVAL;

	/* The shape the groups are built from. */
	shape.base_low = (uint32_t)base;
	shape.base_high = (uint32_t)(base >> 32);
	shape.steps = 1;

	/* Encodes the groups one at a time. */
	error = powervr_usc_program_build(TEMPS_GROUPS, temps_fetch, &shape, output, room, length);
	if (error != 0)
		return error;

	/* Succeeded: the program. */
	return 0;
}

/*
 * Builds one of the compute check's probe programs, each writing at
 * base (only the low half of the address is computed):
 *
 *   MADD           every instance writes 3 * 5 + 7, the multiply-add of
 *                  temporaries alone (22 when it works);
 *   INSTANCE       every instance writes its instance number (the last
 *                  writer's stays);
 *   INSTANCE_MADD  every instance writes its number times four through
 *                  the multiply-add;
 *   ADDRESS        each instance writes its number to the word at base
 *                  plus four times it (the temporaries program without
 *                  its fill);
 *   STRIDE_16, _64, _128  the same with 16, 64 or 128 bytes between the
 *                  instances' words (memory granules);
 *   ADDRESS_WAIT   ADDRESS with a wait for the data return counter
 *                  between the address's computation and the store;
 *   COMPUTED       each instance computes ADDRESS's address and writes
 *                  its low word to base, its high word to base + 4 (the
 *                  last writer's address, base + 124 for instance 31);
 *   ZERO_MADD      the address computed as the instance number times 0
 *                  plus base, the same for all, and the number written;
 *   CONSTANT       the address computed as 1 * 4 + base, not from the
 *                  instance number, and the instance number written;
 *   MOVED          ADDRESS's address computed into another temporary
 *                  and moved into the address pair before the store.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_trans_nir.c (trans_store_global
 * and nir_intrinsic_load_instance_num_pco, as for the store program).
 */
int
powervr_compute_probe_program(
	unsigned kind,
	uint64_t base,
	uint8_t *output,
	size_t room,
	size_t *length)
{
	struct powervr_usc_group groups[16];
	uint32_t stride;
	size_t count;
	int error;

	/* Refuses an unknown probe, a misaligned base or one whose words would wrap. */
	if (kind >= POWERVR_COMPUTE_PROBES || (base & 3U) != 0)
		return EINVAL;
	stride = powervr_compute_probe_stride(kind);
	if ((base & 0xffffffffULL) + (uint64_t)POWERVR_COMPUTE_INSTANCES * stride > 0x100000000ULL)
		return EINVAL;

	/* The address in r0 and r1. */
	count = 0;
	error = immediate_group(&groups[count++], 0, (uint32_t)base);
	if (error == 0)
		error = immediate_group(&groups[count++], 1, (uint32_t)(base >> 32));

	/* The value in r2 (and, for ADDRESS, the instance's word in r0). */
	if (error == 0 && kind == POWERVR_COMPUTE_PROBE_MADD) {
		error = immediate_group(&groups[count++], 3, 3);
		if (error == 0)
			error = immediate_group(&groups[count++], 4, 5);
		if (error == 0)
			error = immediate_group(&groups[count++], 5, 7);
		if (error == 0)
			error = multiply_add_group(&groups[count++], 2, 3, 4, 5);
	} else if (error == 0 && kind == POWERVR_COMPUTE_PROBE_INSTANCE) {
		error = special_group(&groups[count++], 2, USC_INSTANCE_NUMBER);
	} else if (error == 0 && kind == POWERVR_COMPUTE_PROBE_INSTANCE_MADD) {
		error = special_group(&groups[count++], 5, USC_INSTANCE_NUMBER);
		if (error == 0)
			error = immediate_group(&groups[count++], 4, 4);
		if (error == 0)
			error = immediate_group(&groups[count++], 6, 0);
		if (error == 0)
			error = multiply_add_group(&groups[count++], 2, 5, 4, 6);
	} else if (error == 0 && kind == POWERVR_COMPUTE_PROBE_COMPUTED) {
		/* The address in r0 as ADDRESS makes it, then r0 and r1 stored at base (r8, r9) and base + 4 (r10, r11). */
		error = special_group(&groups[count++], 5, USC_INSTANCE_NUMBER);
		if (error == 0)
			error = immediate_group(&groups[count++], 4, 4);
		if (error == 0)
			error = multiply_add_group(&groups[count++], 0, 5, 4, 0);
		if (error == 0)
			error = immediate_group(&groups[count++], 8, (uint32_t)base);
		if (error == 0)
			error = immediate_group(&groups[count++], 9, (uint32_t)(base >> 32));
		if (error == 0)
			error = immediate_group(&groups[count++], 10, (uint32_t)base + 4U);
		if (error == 0)
			error = immediate_group(&groups[count++], 11, (uint32_t)(base >> 32));
		if (error == 0)
			error = store_group(&groups[count++], 0, 8);
		if (error == 0)
			error = wait_group(&groups[count++]);
		if (error == 0)
			error = store_group(&groups[count++], 1, 10);
		if (error == 0)
			error = wait_group(&groups[count++]);
		if (error == 0)
			error = end_group(&groups[count++]);
		if (error != 0)
			return error;

		/* Encodes them; this probe has its own stores. */
		error = powervr_usc_program_encode(groups, count, output, room, length);
		if (error != 0)
			return error;
		return 0;
	} else if (error == 0 && (kind == POWERVR_COMPUTE_PROBE_ZERO_MADD || kind == POWERVR_COMPUTE_PROBE_CONSTANT)) {
		/* The number as data, and the address from a constant factor or index. */
		error = special_group(&groups[count++], 2, USC_INSTANCE_NUMBER);
		if (error == 0 && kind == POWERVR_COMPUTE_PROBE_CONSTANT)
			error = immediate_group(&groups[count++], 5, 1);
		if (error == 0 && kind == POWERVR_COMPUTE_PROBE_ZERO_MADD)
			error = special_group(&groups[count++], 5, USC_INSTANCE_NUMBER);
		if (error == 0)
			error = immediate_group(&groups[count++], 4, kind == POWERVR_COMPUTE_PROBE_CONSTANT ? 4U : 0U);
		if (error == 0)
			error = multiply_add_group(&groups[count++], 0, 5, 4, 0);
	} else if (error == 0 && kind == POWERVR_COMPUTE_PROBE_MOVED) {
		/* The address in r6, moved into r0, the number as data. */
		error = special_group(&groups[count++], 5, USC_INSTANCE_NUMBER);
		if (error == 0)
			error = immediate_group(&groups[count++], 4, 4);
		if (error == 0)
			error = multiply_add_group(&groups[count++], 6, 5, 4, 0);
		if (error == 0)
			error = pass_group(&groups[count++], 0, POWERVR_USC_BANK_TEMPORARY, 6);
		if (error == 0)
			error = pass_group(&groups[count++], 2, POWERVR_USC_BANK_TEMPORARY, 5);
	} else if (error == 0) {
		/* The offset in r8 by the multiply-add, then the address pair by the 64-bit add. */
		error = special_group(&groups[count++], 5, USC_INSTANCE_NUMBER);
		if (error == 0)
			error = immediate_group(&groups[count++], 4, stride);
		if (error == 0)
			error = immediate_group(&groups[count++], 6, 0);
		if (error == 0)
			error = multiply_add_group(&groups[count++], 8, 5, 4, 6);
		if (error == 0)
			error = immediate_group(&groups[count++], 0, (uint32_t)base);
		if (error == 0)
			error = immediate_group(&groups[count++], 1, (uint32_t)(base >> 32));
		if (error == 0)
			error = add64_group(&groups[count++], 0, 0, 8);
		if (error == 0)
			error = pass_group(&groups[count++], 2, POWERVR_USC_BANK_TEMPORARY, 5);
		if (error == 0 && kind == POWERVR_COMPUTE_PROBE_ADDRESS_WAIT)
			error = wait_group(&groups[count++]);
	}

	/* The store, the wait for it, the end. */
	if (error == 0)
		error = store_group(&groups[count++], 2, 0);
	if (error == 0)
		error = wait_group(&groups[count++]);
	if (error == 0)
		error = end_group(&groups[count++]);
	if (error != 0)
		return error;

	/* Encodes them. */
	error = powervr_usc_program_encode(groups, count, output, room, length);
	if (error != 0)
		return error;

	/* Succeeded: the program. */
	return 0;
}

/* Reports the bytes between two instances' words in a probe: 16, 64 or 128 for the strided ones, else 4. */
uint32_t
powervr_compute_probe_stride(
	unsigned kind)
{
	/* The strided probes. */
	if (kind == POWERVR_COMPUTE_PROBE_STRIDE_16)
		return 16;
	if (kind == POWERVR_COMPUTE_PROBE_STRIDE_64)
		return 64;
	if (kind == POWERVR_COMPUTE_PROBE_STRIDE_128)
		return 128;

	/* Succeeded: a word apart. */
	return 4;
}

/*
 * Builds the shading cluster program of one workgroup of a fill kernel.
 *
 * Each instance writes the word at base plus four times its instance
 * number, with the value instance * multiplier + addend (modulo 2^32).
 * The words of the workgroup must not cross a 4 GiB boundary, as only
 * the low half of the address is computed.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_trans_nir.c (trans_store_global
 * and nir_intrinsic_load_instance_num_pco, as for the store program);
 * pco_schedule.c and pco_end.c (the wait after the store, the end).
 */
int
powervr_compute_fill_program(
	uint64_t base,
	unsigned instances,
	uint32_t multiplier,
	uint32_t addend,
	uint8_t *output,
	size_t room,
	size_t *length)
{
	struct fill_shape shape;
	uint64_t last;
	int error;

	/* Refuses an empty or oversized workgroup, a misaligned base, or words whose low half would wrap. */
	if (instances == 0 || instances > POWERVR_COMPUTE_INSTANCES || (base & 3U) != 0)
		return EINVAL;

	last = (base & 0xffffffffULL) + (uint64_t)instances * 4U;
	if (last > 0x100000000ULL)
		return EINVAL;

	/* The shape the groups are built from. */
	shape.base_low = (uint32_t)base;
	shape.base_high = (uint32_t)(base >> 32);
	shape.multiplier = multiplier;
	shape.addend = addend;

	/* Encodes the groups one at a time. */
	error = powervr_usc_program_build(FILL_GROUPS, fill_fetch, &shape, output, room, length);
	if (error != 0)
		return error;

	/* Succeeded: the program. */
	return 0;
}

/*
 * Builds the firmware's compute command for a control stream and a
 * context resume buffer; every other field is zero.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_compute.c
 * (pvr_submit_info_stream_init: the control stream base and the context
 * state base as their register values, the address itself for both)
 * and src/imagination/csbgen/rogue/cr.xml (CDM_CTRL_STREAM_BASE addr
 * 2..39 shifted by 2, CDM_CONTEXT_STATE_BASE addr 4..39 shifted by 4).
 */
void
powervr_compute_command(
	uint64_t stream,
	uint64_t resume,
	uint32_t words[POWERVR_COMPUTE_COMMAND_WORDS])
{
	unsigned i;

	/* Clears the command. */
	for (i = 0; i < POWERVR_COMPUTE_COMMAND_WORDS; i++)
		words[i] = 0;

	/* The control stream and the resume buffer, each a 64-bit register value. */
	words[10] = (uint32_t)stream;
	words[11] = (uint32_t)(stream >> 32);
	words[12] = (uint32_t)resume;
	words[13] = (uint32_t)(resume >> 32);
}

/*
 * Hands over one group of the store program: the set-up, then the
 * steps, then the end.
 */
static int
store_fetch(
	void *context,
	size_t index,
	struct powervr_usc_group *group)
{
	const struct store_shape *shape;
	size_t step_index;
	size_t step;
	int error;

	/* The set-up: the instance's offset, made once by the multiply-add. */
	shape = context;
	error = 0;
	if (index == 0) {
		/* r5: the instance number. */
		error = special_group(group, 5, USC_INSTANCE_NUMBER);
	} else if (index == 1) {
		/* r4: four, the bytes of a word. */
		error = immediate_group(group, 4, 4);
	} else if (index == 2) {
		/* r6: zero. */
		error = immediate_group(group, 6, 0);
	} else if (index == 3) {
		/* r8: the instance's offset. */
		error = multiply_add_group(group, 8, 5, 4, 6);
	} else if (index == 4) {
		/* r10: the low half of the base. */
		error = immediate_group(group, 10, shape->base_low);
	} else if (index == 5) {
		/* r11: the high half. */
		error = immediate_group(group, 11, shape->base_high);
	} else if (index < STORE_PROLOGUE + (size_t)shape->steps * STORE_STEP) {
		/* One step: its address from the base, the instance's offset and the step's; the data; the store and the wait. */
		step = (index - STORE_PROLOGUE) / STORE_STEP;
		step_index = (index - STORE_PROLOGUE) % STORE_STEP;
		if (step_index == 0) {
			error = immediate_group(group, 9, (uint32_t)(step * POWERVR_COMPUTE_STORE_STRIDE));
		} else if (step_index == 1) {
			error = add64_group(group, 0, 10, 8);
		} else if (step_index == 2) {
			error = add64_group(group, 0, 0, 9);
		} else if (step_index == 3) {
			error = pass_group(group, 2, POWERVR_USC_BANK_TEMPORARY, 0);
		} else if (step_index == 4) {
			error = store_group(group, 2, 0);
		} else {
			error = wait_group(group);
		}
	} else {
		/* The end. */
		error = end_group(group);
	}

	/* A group the encoders refused. */
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Hands over one group of the temporaries program.
 */
static int
temps_fetch(
	void *context,
	size_t index,
	struct powervr_usc_group *group)
{
	const struct store_shape *shape;
	int error;

	/* The number first, then the fill, then the offset and the address, then the store. */
	shape = context;
	error = 0;
	if (index == 0) {
		/* r7: the instance number. */
		error = special_group(group, 7, USC_INSTANCE_NUMBER);
	} else if (index >= 1 && index <= 4) {
		/* r3 to r6: values that are no instance number. */
		error = immediate_group(group, (unsigned)index + 2U, TEMPS_FILL | ((uint32_t)index + 2U));
	} else if (index == 5) {
		/* r2: four, the bytes of a word. */
		error = immediate_group(group, 2, 4);
	} else if (index == 6) {
		/* r0: zero. */
		error = immediate_group(group, 0, 0);
	} else if (index == 7) {
		/* r2: the instance's offset. */
		error = multiply_add_group(group, 2, 7, 2, 0);
	} else if (index == 8) {
		/* r0: the low half of the base. */
		error = immediate_group(group, 0, shape->base_low);
	} else if (index == 9) {
		/* r1: the high half. */
		error = immediate_group(group, 1, shape->base_high);
	} else if (index == 10) {
		/* r0 and r1: the instance's word. */
		error = add64_group(group, 0, 0, 2);
	} else if (index == 11) {
		/* r2: the data, the number as the eighth temporary holds it now. */
		error = pass_group(group, 2, POWERVR_USC_BANK_TEMPORARY, 7);
	} else if (index == 12) {
		/* The store. */
		error = store_group(group, 2, 0);
	} else if (index == 13) {
		/* The wait for it. */
		error = wait_group(group);
	} else {
		/* The end. */
		error = end_group(group);
	}

	/* A group the encoders refused. */
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Hands over one group of the fill program.
 */
static int
fill_fetch(
	void *context,
	size_t index,
	struct powervr_usc_group *group)
{
	const struct fill_shape *shape;
	int error;

	/* The number, the value, the offset and the address, then the store. */
	shape = context;
	error = 0;
	if (index == 0) {
		/* r7: the instance number. */
		error = special_group(group, 7, USC_INSTANCE_NUMBER);
	} else if (index == 1) {
		/* r3: the multiplier. */
		error = immediate_group(group, 3, shape->multiplier);
	} else if (index == 2) {
		/* r4: the addend. */
		error = immediate_group(group, 4, shape->addend);
	} else if (index == 3) {
		/* r6: the value. */
		error = multiply_add_group(group, 6, 7, 3, 4);
	} else if (index == 4) {
		/* r2: four, the bytes of a word. */
		error = immediate_group(group, 2, 4);
	} else if (index == 5) {
		/* r0: zero. */
		error = immediate_group(group, 0, 0);
	} else if (index == 6) {
		/* r5: the instance's offset. */
		error = multiply_add_group(group, 5, 7, 2, 0);
	} else if (index == 7) {
		/* r0: the low half of the base. */
		error = immediate_group(group, 0, shape->base_low);
	} else if (index == 8) {
		/* r1: the high half. */
		error = immediate_group(group, 1, shape->base_high);
	} else if (index == 9) {
		/* r0 and r1: the instance's word. */
		error = add64_group(group, 0, 0, 5);
	} else if (index == 10) {
		/* r2: the value. */
		error = pass_group(group, 2, POWERVR_USC_BANK_TEMPORARY, 6);
	} else if (index == 11) {
		/* The store. */
		error = store_group(group, 2, 0);
	} else if (index == 12) {
		/* The wait for it. */
		error = wait_group(group);
	} else {
		/* The end. */
		error = end_group(group);
	}

	/* A group the encoders refused. */
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a bitwise group that moves a 32-bit immediate into a temporary.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_MOVI32: a
 * bitwise group with phase 0 only, the first write enabled, the
 * immediate bypass with the count from s2, the destination from the
 * phase's second output).
 */
static int
immediate_group(
	struct powervr_usc_group *group,
	unsigned destination,
	uint32_t value)
{
	int error;

	/* A bitwise group writing one temporary. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_BITWISE;
	group->write_0 = 1;
	temporary(&group->destination[0], destination);

	/* The first phase carries the immediate. */
	error = powervr_usc_bit_immediate(value, 1, USC_COUNT_FROM_S2, USC_COUNT_PASS, USC_SHIFT_PASS, &group->operation[POWERVR_USC_PHASE_0]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a main group that copies a register into a temporary.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_MBYP: phase 0
 * only, the first write enabled, the source in s0, is4 from ft0).
 */
static int
pass_group(
	struct powervr_usc_group *group,
	unsigned destination,
	unsigned bank,
	unsigned source)
{
	int error;

	/* A main group from one source to one temporary. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_MAIN;
	group->write_0 = 1;
	group->source[0].used = 1;
	group->source[0].bank = (uint8_t)bank;
	group->source[0].index = (uint16_t)source;
	temporary(&group->destination[0], destination);

	/* The first phase passes the source through. */
	error = powervr_usc_single(POWERVR_USC_SINGLE_PASS, 0, 0, &group->operation[POWERVR_USC_PHASE_0]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a main group that copies a special register into a temporary.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_MOVS1: a
 * special register only in s1, s2 or s4; the source in s1 through the
 * mux, is4 from fte, the move in phase 2 with the first write enabled).
 */
static int
special_group(
	struct powervr_usc_group *group,
	unsigned destination,
	unsigned source)
{
	int error;

	/* A main group from the special register in s1 to one temporary. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_MAIN;
	group->write_0 = 1;
	group->source[1].used = 1;
	group->source[1].bank = POWERVR_USC_BANK_SPECIAL;
	group->source[1].index = (uint16_t)source;
	temporary(&group->destination[0], destination);
	group->mux_used = 1;
	group->mux = USC_MUX_S1;
	group->internal[4] = USC_SELECT_FTE;

	/* The third phase moves fte to the first write. */
	error = powervr_usc_move(USC_SELECT_FT0, USC_SELECT_FTE, 0xf, 1, 1, 1, &group->operation[POWERVR_USC_PHASE_2]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a main group of a 32-bit unsigned multiply-add of temporaries.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_IMADD32:
 * phase 0 only, the first write enabled, the sources in s0, s1 and s2,
 * is4 from ft0; encode_map O_IMADD32: the short form without a carry
 * input).
 */
static int
multiply_add_group(
	struct powervr_usc_group *group,
	unsigned destination,
	unsigned first,
	unsigned second,
	unsigned addend)
{
	int error;

	/* A main group from three temporaries to one. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_MAIN;
	group->write_0 = 1;
	temporary(&group->source[0], first);
	temporary(&group->source[1], second);
	temporary(&group->source[2], addend);
	temporary(&group->destination[0], destination);
	group->internal[4] = USC_SELECT_FT0;

	/* The first phase multiplies and adds. */
	error = powervr_usc_integer(POWERVR_USC_INTEGER_MULTIPLY_ADD_32, 0, 0, 0, 0, 0, 0, 0, 0, &group->operation[POWERVR_USC_PHASE_0]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a main group of a 64-bit add of a 32-bit word: the pair at base
 * and base + 1 plus the temporary offset, written to the pair at
 * destination and destination + 1 (the low half from ft0, the high from
 * fte), as an address is made.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_ADD64_32:
 * phase 0, both writes, the sources in s0, s1 and s2, is4 from ft0 and
 * is5 from fte; encode_map O_ADD64_32: the short form, add6432);
 * zedBSD src/drivers/gpu/powervr/shader-backend.c (MACHINE_ADD64).
 */
static int
add64_group(
	struct powervr_usc_group *group,
	unsigned destination,
	unsigned base,
	unsigned offset)
{
	int error;

	/* A main group from the base pair and the offset to the destination pair. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_MAIN;
	group->write_0 = 1;
	group->write_1 = 1;
	temporary(&group->source[0], base);
	temporary(&group->source[1], base + 1U);
	temporary(&group->source[2], offset);
	temporary(&group->destination[0], destination);
	temporary(&group->destination[1], destination + 1U);
	group->internal[4] = USC_SELECT_FT0;
	group->internal[5] = USC_SELECT_FTE;

	/* The first phase adds. */
	error = powervr_usc_integer(POWERVR_USC_INTEGER_ADD_64_FROM_32, 0, 0, 0, 0, 0, 0, 0, 0, &group->operation[POWERVR_USC_PHASE_0]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a main group that stores one 32-bit word.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_ST32: the
 * back end only, no write, the data in s0 and the address pair in s3;
 * encode_map O_ST: the immediate burst, the data return counter, the
 * address and data selectors and the size).
 */
static int
store_group(
	struct powervr_usc_group *group,
	unsigned data,
	unsigned address)
{
	int error;

	/* A main group reading the data and the address pair. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_MAIN;
	temporary(&group->source[0], data);
	temporary(&group->source[3], address);

	/* The back end stores one word through data return counter 0. */
	error = powervr_usc_store(3, 1, 0, 0, USC_SIZE_32, USC_WRITE_THROUGH, 0, &group->operation[POWERVR_USC_PHASE_BACKEND]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds a control group that waits for data return counter 0.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_WDF: the
 * counter in the header's miscellaneous bit, no operation bytes).
 */
static int
wait_group(
	struct powervr_usc_group *group)
{
	int error;

	/* A control group naming the wait. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_CONTROL;
	group->control = POWERVR_USC_CONTROL_WAIT_DATA;

	/* The wait has no fields. */
	error = powervr_usc_empty(&group->operation[POWERVR_USC_PHASE_0]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/*
 * Builds the control group that ends the program.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_map.py (group_map O_NOP: the end
 * in the header's miscellaneous bit).
 */
static int
end_group(
	struct powervr_usc_group *group)
{
	int error;

	/* A control group naming the no-operation, marked as the end. */
	*group = blank_group;
	group->unit = POWERVR_USC_UNIT_CONTROL;
	group->control = POWERVR_USC_CONTROL_NOTHING;
	group->control_misc = 1;

	/* The no-operation's single byte. */
	error = powervr_usc_nothing(&group->operation[POWERVR_USC_PHASE_0]);
	if (error != 0)
		return error;

	/* Succeeded: the group. */
	return 0;
}

/* Names a temporary as an operand. */
static void
temporary(
	struct powervr_usc_register *operand,
	unsigned index)
{
	/* The temporary bank and the register's number. */
	operand->used = 1;
	operand->bank = POWERVR_USC_BANK_TEMPORARY;
	operand->index = (uint16_t)index;
}
