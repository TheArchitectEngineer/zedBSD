/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The driver's own compute programs for the PowerVR core: the shading
 * cluster programs that write a buffer, the data sequencer programs that
 * start them and end a stream, the compute data master's control stream
 * words, and the firmware's compute command around them.
 *
 * They exist so the driver can run work on the core without a user
 * program, to check the core's compute path and what the processor sees
 * of the core's writes.  The same code runs on the build host.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_COMPUTE_PROGRAM_H
#define KERN_DRIVERS_GPU_POWERVR_COMPUTE_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

/*
 * The instances of one task, the words of one kernel in the control
 * stream, and the data and code words of the data sequencer programs.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/include/hwdef/rogue_hw_defs.h
 * (ROGUE_MAX_INSTANCES_PER_TASK 32); src/imagination/csbgen/rogue/cdm.xml
 * (KERNEL0, 1, 2, 3, 4, 5 and 8 without an indirect buffer or global
 * offsets).
 */
#define POWERVR_COMPUTE_INSTANCES	32U
#define POWERVR_COMPUTE_KERNEL_WORDS	7U
#define POWERVR_COMPUTE_PDS_DATA_WORDS	2U
#define POWERVR_COMPUTE_PDS_CODE_MAX	5U

/* The words of the firmware's compute command. */
#define POWERVR_COMPUTE_COMMAND_WORDS	24U

/*
 * The temporaries each program uses per instance, the words one instance
 * of the store program writes per step, and how far apart its steps are.
 */
#define POWERVR_COMPUTE_STORE_TEMPORARIES	7U
#define POWERVR_COMPUTE_TEMPS_TEMPORARIES	8U
#define POWERVR_COMPUTE_FILL_TEMPORARIES	8U
#define POWERVR_COMPUTE_STORE_STRIDE		(POWERVR_COMPUTE_INSTANCES * 4U)

/*
 * One kernel of the control stream.
 *
 * Data_offset and code_offset are the data sequencer program's places
 * in its heap; the unified and data sequencer temporaries are counts of
 * 32-bit registers.  Instances is the workgroup's size, all in x;
 * max_instances is how many go in one task, 32 written as zero.  Fence
 * makes the kernel wait for the work before it.  Shared_from_pds and
 * common_shared say the data sequencer loads the shared registers and
 * the kernel frees them; any_cluster lets the task run on any cluster.
 */
struct powervr_compute_kernel {
	uint32_t data_offset;
	uint32_t code_offset;
	unsigned data_words;
	unsigned pds_temporaries;
	unsigned usc_temporaries;
	unsigned instances;
	unsigned max_instances;
	unsigned fence;
	unsigned shared_from_pds;
	unsigned common_shared;
	unsigned any_cluster;
};

int powervr_compute_kernel_words(const struct powervr_compute_kernel *kernel, uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS]);
uint32_t powervr_compute_terminate_word(void);
int powervr_compute_temporaries_field(unsigned temporaries, unsigned unit, unsigned *field);
int powervr_compute_kick_program(uint64_t usc_offset, unsigned temporaries_field, uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS], uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX], unsigned *code_words);
int powervr_compute_fence_program(uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS], uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX], unsigned *code_words);
int powervr_compute_store_program(uint64_t base, unsigned steps, uint8_t *output, size_t room, size_t *length);
/*
 * The probe programs of the compute check (compute-check.c), each a few
 * groups that tell one part of the shading cluster's integer path apart:
 * a multiply-add of constants, the instance number, the instance number
 * times four, and the word each instance addresses by its number.
 */
#define POWERVR_COMPUTE_PROBE_MADD		0U
#define POWERVR_COMPUTE_PROBE_INSTANCE		1U
#define POWERVR_COMPUTE_PROBE_INSTANCE_MADD	2U
#define POWERVR_COMPUTE_PROBE_ADDRESS		3U
#define POWERVR_COMPUTE_PROBE_STRIDE_16		4U
#define POWERVR_COMPUTE_PROBE_STRIDE_64		5U
#define POWERVR_COMPUTE_PROBE_STRIDE_128	6U
#define POWERVR_COMPUTE_PROBE_ADDRESS_WAIT	7U
#define POWERVR_COMPUTE_PROBES			8U

/* The bytes between two instances' words in each probe (4 but for the strided ones). */
#define POWERVR_COMPUTE_PROBE_STRIDE_MAX	128U

/* The temporaries a probe program uses. */
#define POWERVR_COMPUTE_PROBE_TEMPORARIES	7U

int powervr_compute_probe_program(unsigned kind, uint64_t base, uint8_t *output, size_t room, size_t *length);
uint32_t powervr_compute_probe_stride(unsigned kind);
int powervr_compute_temps_program(uint64_t base, uint8_t *output, size_t room, size_t *length);
int powervr_compute_fill_program(uint64_t base, unsigned instances, uint32_t multiplier, uint32_t addend, uint8_t *output, size_t room, size_t *length);
void powervr_compute_command(uint64_t stream, uint64_t resume, uint32_t words[POWERVR_COMPUTE_COMMAND_WORDS]);

#endif /* KERN_DRIVERS_GPU_POWERVR_COMPUTE_PROGRAM_H */
