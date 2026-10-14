/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws037-p008: a small interpreter of the SM75 instructions the nvrtx
 * compiler emits (sm75-machine.c), shared by the host tests.
 *
 * It decodes each instruction by the field positions of sm75-isa.h and does
 * what it says, one invocation at a time; a workgroup's invocations take
 * turns between barriers.  The graphics instructions read and write the
 * run's attribute words, and a texture instruction answers with a texel
 * made from its operands (sm75_machine_invoke() says which), so a test can
 * predict it.  It checks the serial dependences as it goes.  Since it reads
 * the same field positions the encoder writes, it checks the lowering, not
 * the encoding.
 */

#ifndef PLAN_WS037_TESTS_SM75_MACHINE_H
#define PLAN_WS037_TESTS_SM75_MACHINE_H

#include "drivers/gpu/nvrtx/compiler/sm75-compile.h"

#include <stdint.h>

/* The most buffers a run binds, and where the interpreter places them in the GPU's address space. */
#define TEST_BUFFERS		4U
#define TEST_BUFFER_BASE	0x100000000ULL
#define TEST_BUFFER_STRIDE	0x01000000ULL

/* The most instructions one invocation may run, and the most invocations a workgroup has. */
#define TEST_STEPS		100000U
#define TEST_THREADS		1024U

/* Where an invocation stands between the rounds of its workgroup. */
#define TEST_RUNNING		0
#define TEST_AT_BARRIER		1
#define TEST_DONE		2

/*
 * What the dependence check knows of one register or predicate: a
 * fixed-latency write in flight (its class and cycle), the scoreboard of a
 * variable-latency write in flight (plus one; 0 for none), the scoreboards
 * of variable-latency reads in flight, and the last fixed-latency read.
 */
struct test_hazard {
	int written;
	uint32_t write_class;
	uint32_t write_cycle;
	uint32_t write_bar;
	uint32_t read_bars;
	int read;
	uint32_t read_cycle;
};

/*
 * One invocation on the interpreter: its registers and predicates, where
 * it is, its place in the workgroup, and the dependence check's state: the
 * issue cycle of the instruction running and of the next one (from the
 * delays), each register's and predicate's writes and reads in flight, the
 * scoreboards in use and when each was set, and what the running
 * instruction reads, writes and sets.
 */
struct test_thread {
	uint32_t r[256];
	int p[8];
	uint32_t pc;
	uint32_t steps;
	uint32_t local[3];
	int state;

	uint32_t cycle;
	uint32_t next_cycle;
	struct test_hazard gpr[256];
	struct test_hazard predicate[8];
	uint32_t bar_busy;
	uint32_t bar_set_cycle[6];
	uint32_t instruction_class;
	uint32_t read_class;
	uint32_t reads[16];
	uint32_t read_count;
	uint32_t writes[8];
	uint32_t write_count;
	uint32_t sets_write_bar;
	uint32_t sets_read_bar;
	int issued;
};

/*
 * One storage buffer of a run, at (set, binding): its words and size.
 */
struct test_buffer {
	uint32_t set;
	uint32_t binding;
	uint32_t *words;
	uint32_t bytes;
};

/*
 * One dispatch on the interpreter: the program, the constant buffers, the
 * storage buffers, and the grid.
 */
struct test_run {
	const struct nvrtx_sm75_binary *binary;
	uint8_t cb[8][0x10000];
	struct test_buffer buffers[TEST_BUFFERS];
	uint32_t buffer_count;
	uint32_t groups[3];
	uint32_t *shared;
	uint32_t shared_words;
	int failed;

	/* A graphics run: the attribute words read (by address / 4) and written, and whether the fragment was discarded. */
	uint32_t attr_in[256];
	uint32_t attr_out[256];
	uint32_t exit_regs[32];
	int killed;
};

/* Binds a storage buffer at (set, binding) and writes its entry into the root constant buffer. */
void sm75_machine_bind(struct test_run *run, uint32_t set, uint32_t binding, uint32_t *words, uint32_t bytes);

/* Runs every workgroup of a compute run's grid. */
void sm75_machine_dispatch(struct test_run *run);

/* Runs one invocation up to EXIT, a barrier, a discard or a failure. */
void sm75_machine_invoke(struct test_run *run, struct test_thread *thread, const uint32_t group[3]);

/* Finds the word at a GPU address in the bound buffers; NULL (a failure) outside them. */
uint32_t *sm75_machine_memory(struct test_run *run, uint64_t address);

/* Reads a word of a constant buffer. */
uint32_t sm75_machine_cb_word(const struct test_run *run, uint32_t index, uint32_t offset);

/* The float of 32 bits, and the 32 bits of a float. */
float sm75_machine_float(uint32_t bits);
uint32_t sm75_machine_bits(float value);

/* The latency classes the dependence check uses (Turing's, NAK's sm75_instr_latencies.rs), and a guard predicate reader. */
#define TEST_CLASS_ALU		0U
#define TEST_CLASS_FMA		1U
#define TEST_CLASS_IMAD		2U
#define TEST_CLASS_IMAD_WIDE	3U
#define TEST_CLASS_DISPATCH	4U
#define TEST_CLASS_DECOUPLED	5U
#define TEST_CLASS_WIDE_ADDEND	6U
#define TEST_CLASS_GUARD	7U

/* Converts a float to a 32-bit integer toward zero as the hardware does: saturated, NaN to 0. */
uint32_t sm75_machine_f2i(float value, int is_signed);

#endif /* PLAN_WS037_TESTS_SM75_MACHINE_H */
