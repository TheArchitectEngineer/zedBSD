/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws037-p008: the interpreter of SM75 instructions the host tests share
 * (sm75-machine.h).
 */

#include "plan/ws037/tests/sm75-machine.h"

#include "drivers/gpu/nvrtx/compiler/sm75-encode.h"
#include "drivers/gpu/nvrtx/compiler/sm75-isa.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sm75_machine_issue(struct test_run *run, struct test_thread *t, const struct nvrtx_sm75_word *word);
static void sm75_machine_retire(struct test_run *run, struct test_thread *t);
static uint32_t sm75_machine_read(struct test_run *run, struct test_thread *t, uint32_t reg);
static void sm75_machine_write(struct test_run *run, struct test_thread *t, uint32_t reg, uint32_t value);
static int sm75_machine_predicate(struct test_run *run, struct test_thread *t, uint32_t predicate, uint32_t reader);
static void sm75_machine_predicate_write(struct test_thread *t, uint32_t predicate, int value);
static void sm75_machine_exit_check(struct test_run *run, struct test_thread *t);
static void sm75_machine_hazard(struct test_run *run, struct test_thread *t, const char *what, uint32_t reg);
static uint32_t sm75_machine_class(uint32_t opcode);
static uint32_t sm75_machine_raw(uint32_t sm, uint32_t writer, uint32_t reader);
static uint32_t sm75_machine_sm(const struct test_run *run);
static int sm75_machine_agu(uint32_t opcode);
static void sm75_machine_memory_order(struct test_run *run, struct test_thread *t, const struct nvrtx_sm75_word *word);
static void sm75_machine_texture(struct test_run *run, struct test_thread *t, const struct nvrtx_sm75_word *word);
static int32_t sm75_machine_offset(uint32_t bits, unsigned low);

/* Binds a storage buffer at (set, binding) and writes its entry into the root constant buffer. */
void
sm75_machine_bind(
	struct test_run *run,
	uint32_t set,
	uint32_t binding,
	uint32_t *words,
	uint32_t bytes)
{
	struct test_buffer *buffer;
	uint64_t address;
	uint32_t slot;
	uint32_t entry[4];

	buffer = &run->buffers[run->buffer_count];
	buffer->set = set;
	buffer->binding = binding;
	buffer->words = words;
	buffer->bytes = bytes;
	address = TEST_BUFFER_BASE + (uint64_t)run->buffer_count * TEST_BUFFER_STRIDE;
	run->buffer_count++;

	/* The program's table slot of the buffer, if it uses it. */
	for (slot = 0U; slot < run->binary->storage_count; slot++) {
		if (run->binary->storage[slot].set != set || run->binary->storage[slot].binding != binding)
			continue;
		entry[0] = (uint32_t)address;
		entry[1] = (uint32_t)(address >> 32);
		entry[2] = bytes;
		entry[3] = 0U;
		memcpy(&run->cb[0][NVRTX_SM75_CB_STORAGE + slot * NVRTX_SM75_CB_STORAGE_ENTRY], entry, sizeof(entry));
	}
}

/*
 * Runs every workgroup: its invocations one after another, each up to a
 * barrier or its end; once every one has reached the barrier they all go
 * on (a valid schedule).  An invocation that ends while another waits at a
 * barrier is a failure.  Each workgroup has its own shared memory.
 */
void
sm75_machine_dispatch(
	struct test_run *run)
{
	static struct test_thread threads[TEST_THREADS];
	uint32_t group[3];
	const uint32_t *size;
	uint32_t count;
	uint32_t index;
	uint32_t waiting;
	uint32_t done;

	size = run->binary->local_size;
	count = size[0] * size[1] * size[2];
	if (count == 0U || count > TEST_THREADS) {
		run->failed = 1;
		return;
	}
	run->shared_words = (run->binary->shared_bytes + 3U) / 4U;
	run->shared = calloc(run->shared_words + 1U, sizeof(uint32_t));

	for (group[2] = 0U; group[2] < run->groups[2]; group[2]++) {
		for (group[1] = 0U; group[1] < run->groups[1]; group[1]++) {
			for (group[0] = 0U; group[0] < run->groups[0]; group[0]++) {
				/* The workgroup's invocations and shared memory, fresh. */
				memset(threads, 0, sizeof(threads[0]) * count);
				memset(run->shared, 0xcd, (run->shared_words + 1U) * sizeof(uint32_t));
				for (index = 0U; index < count; index++) {
					threads[index].local[0] = index % size[0];
					threads[index].local[1] = (index / size[0]) % size[1];
					threads[index].local[2] = index / (size[0] * size[1]);
					threads[index].p[7] = 1;
				}

				/* Rounds until every invocation is done. */
				for (;;) {
					for (index = 0U; index < count && !run->failed; index++) {
						if (threads[index].state == TEST_RUNNING)
							sm75_machine_invoke(run, &threads[index], group);
					}
					if (run->failed)
						break;
					waiting = 0U;
					done = 0U;
					for (index = 0U; index < count; index++) {
						if (threads[index].state == TEST_AT_BARRIER)
							waiting++;
						if (threads[index].state == TEST_DONE)
							done++;
					}
					if (done == count)
						break;
					if (done != 0U) {
						printf("  an invocation ended while others wait at a barrier\n");
						run->failed = 1;
						break;
					}
					for (index = 0U; index < count; index++)
						threads[index].state = TEST_RUNNING;
					(void)waiting;
				}
			}
		}
	}
	free(run->shared);
	run->shared = NULL;
}

/*
 * Runs one invocation: decodes each instruction by the field positions of
 * sm75-isa.h and does what it says, until EXIT.  Also checks the serial
 * dependences: an instruction after one of variable latency waits for the
 * scoreboards that one set.
 */
void
sm75_machine_invoke(
	struct test_run *run,
	struct test_thread *t,
	const uint32_t group[3])
{
	const uint32_t *code;
	struct nvrtx_sm75_word word;
	uint32_t opcode;
	uint32_t alu;
	uint32_t form;
	uint32_t dst;
	uint32_t s0;
	uint32_t s1;
	uint32_t s2;
	uint32_t lut;
	uint32_t bit;
	uint32_t value;
	uint64_t wide;
	uint32_t *cell;
	int guard;
	int take;
	float f;

	code = run->binary->code;

	for (; t->steps < TEST_STEPS; sm75_machine_retire(run, t), t->steps++, t->pc++) {
		if (t->pc * 4U >= run->binary->code_words) {
			printf("  ran past the code\n");
			run->failed = 1;
			return;
		}
		memcpy(word.w, &code[t->pc * 4U], sizeof(word.w));
		t->r[NVRTX_SM75_RZ] = 0U;

		/* The dependences: the issue cycle, the scoreboards waited for and set, the guard. */
		sm75_machine_issue(run, t, &word);
		guard = sm75_machine_predicate(run, t, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_LOW, 3U), TEST_CLASS_GUARD);
		if (nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_NOT, 1U) != 0U)
			guard = !guard;
		opcode = (uint32_t)nvrtx_sm75_get(&word, 0U, 12U);
		dst = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_DST_LOW, 8U);

		/* The instructions that are not of the ALU form. */
		if (opcode == NVRTX_SM75_OP_EXIT) {
			sm75_machine_exit_check(run, t);
			memcpy(run->exit_regs, t->r, sizeof(run->exit_regs));
			t->state = TEST_DONE;
			return;
		}
		if (!guard)
			continue;
		if (opcode == NVRTX_SM75_OP_NOP || opcode == NVRTX_SM75_OP_BSSY || opcode == NVRTX_SM75_OP_BSYNC || opcode == NVRTX_SM75_OP_MEMBAR)
			continue;
		if (opcode == NVRTX_SM75_OP_BAR) {
			sm75_machine_retire(run, t);
			t->state = TEST_AT_BARRIER;
			t->pc++;
			t->steps++;
			return;
		}
		if (opcode == NVRTX_SM75_OP_BRA) {
			take = sm75_machine_predicate(run, t, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_LOW, 3U), TEST_CLASS_DECOUPLED);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_NOT, 1U) != 0U)
				take = !take;
			if (take) {
				wide = nvrtx_sm75_get(&word, NVRTX_SM75_BRA_OFFSET_LOW, NVRTX_SM75_BRA_OFFSET_BITS);
				if ((wide >> 47) != 0U)
					wide |= ~((1ULL << 48) - 1U);
				t->pc = (uint32_t)((int64_t)t->pc + (int64_t)wide / 4);
			}
			continue;
		}
		if (opcode == NVRTX_SM75_OP_ALD) {
			sm75_machine_write(run, t, dst, run->attr_in[nvrtx_sm75_get(&word, NVRTX_SM75_ATTR_ADDRESS_LOW, NVRTX_SM75_ATTR_ADDRESS_BITS) / 4U]);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_AST) {
			run->attr_out[nvrtx_sm75_get(&word, NVRTX_SM75_ATTR_ADDRESS_LOW, NVRTX_SM75_ATTR_ADDRESS_BITS) / 4U] = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U));
			continue;
		}
		if (opcode == NVRTX_SM75_OP_IPA) {
			/* The fragment's w is 2; a perspective input (by the header's map) arrives multiplied by it. */
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_IPA_ADDRESS_LOW, 8U);
			bit = run->attr_in[value];
			if (value >= NVRTX_SM75_ATTR_GENERIC / 4U && value < NVRTX_SM75_ATTR_GENERIC_END / 4U) {
				lut = (run->binary->sph[(NVRTX_SM75_SPH_IMAP_GENERIC_LOW + (value - NVRTX_SM75_ATTR_GENERIC / 4U) * 2U) / 32U] >> ((NVRTX_SM75_SPH_IMAP_GENERIC_LOW + (value - NVRTX_SM75_ATTR_GENERIC / 4U) * 2U) % 32U)) & 3U;
				if (lut == NVRTX_SM75_IMAP_PERSPECTIVE)
					bit = sm75_machine_bits(sm75_machine_float(bit) * 2.0f);
				if (lut == 0U) {
					printf("  IPA of an input the header does not map\n");
					run->failed = 1;
				}
			}
			sm75_machine_write(run, t, dst, bit);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_TEX || opcode == NVRTX_SM75_OP_TLD) {
			sm75_machine_texture(run, t, &word);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_KILL) {
			run->killed = 1;
			t->state = TEST_DONE;
			return;
		}
		if (opcode == NVRTX_SM75_OP_LDS || opcode == NVRTX_SM75_OP_STS || opcode == NVRTX_SM75_OP_ATOMS || opcode == NVRTX_SM75_OP_ATOMS_CAS) {
			s0 = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U));
			if ((s0 & 3U) != 0U || s0 / 4U >= run->shared_words) {
				printf("  a shared access at %u outside the %u words\n", s0, run->shared_words);
				run->failed = 1;
				return;
			}
			cell = &run->shared[s0 / 4U];
		} else if (opcode == NVRTX_SM75_OP_ATOMG || opcode == NVRTX_SM75_OP_ATOMG_CAS) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U);
			wide = ((uint64_t)sm75_machine_read(run, t, s0 + 1U) << 32) | sm75_machine_read(run, t, s0);
			cell = sm75_machine_memory(run, wide);
			if (cell == NULL)
				return;
		} else {
			cell = NULL;
		}
		if (opcode == NVRTX_SM75_OP_LDS) {
			sm75_machine_write(run, t, dst, *cell);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_STS) {
			*cell = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U));
			continue;
		}
		if (opcode == NVRTX_SM75_OP_ATOMS_CAS || opcode == NVRTX_SM75_OP_ATOMG_CAS) {
			value = *cell;
			if (value == sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)))
				*cell = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_CAS_DATA_LOW, 8U));
			sm75_machine_write(run, t, dst, value);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_ATOMS || opcode == NVRTX_SM75_OP_ATOMG) {
			value = *cell;
			s1 = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U));
			lut = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_ATOM_OP_LOW, 4U);
			bit = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_ATOM_TYPE_LOW, 3U);
			if (lut == NVRTX_SM75_ATOM_ADD)
				*cell = value + s1;
			else if (lut == NVRTX_SM75_ATOM_AND)
				*cell = value & s1;
			else if (lut == NVRTX_SM75_ATOM_OR)
				*cell = value | s1;
			else if (lut == NVRTX_SM75_ATOM_XOR)
				*cell = value ^ s1;
			else if (lut == NVRTX_SM75_ATOM_EXCH)
				*cell = s1;
			else if (lut == NVRTX_SM75_ATOM_MIN)
				*cell = (bit == NVRTX_SM75_ATOM_I32) ? (((int32_t)s1 < (int32_t)value) ? s1 : value) : ((s1 < value) ? s1 : value);
			else if (lut == NVRTX_SM75_ATOM_MAX)
				*cell = (bit == NVRTX_SM75_ATOM_I32) ? (((int32_t)s1 > (int32_t)value) ? s1 : value) : ((s1 > value) ? s1 : value);
			else
				run->failed = 1;
			sm75_machine_write(run, t, dst, value);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_S2R) {
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_S2R_INDEX_LOW, 8U);
			if (value >= NVRTX_SM75_SR_TID_X && value <= NVRTX_SM75_SR_TID_Z)
				sm75_machine_write(run, t, dst, t->local[value - NVRTX_SM75_SR_TID_X]);
			else if (value >= NVRTX_SM75_SR_CTAID_X && value <= NVRTX_SM75_SR_CTAID_Z)
				sm75_machine_write(run, t, dst, group[value - NVRTX_SM75_SR_CTAID_X]);
			else
				run->failed = 1;
			continue;
		}
		if (opcode == NVRTX_SM75_OP_LDC) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_INDEX_LOW, 5U);
			s1 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_OFFSET_LOW, 16U);
			sm75_machine_write(run, t, dst, sm75_machine_cb_word(run, s0, s1));
			if (nvrtx_sm75_get(&word, NVRTX_SM75_MEM_TYPE_LOW, 3U) == NVRTX_SM75_MEM_B64)
				sm75_machine_write(run, t, dst + 1U, sm75_machine_cb_word(run, s0, s1 + 4U));
			continue;
		}
		if (opcode == NVRTX_SM75_OP_LDG || opcode == NVRTX_SM75_OP_STG) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U);
			wide = ((uint64_t)sm75_machine_read(run, t, s0 + 1U) << 32) | sm75_machine_read(run, t, s0);
			wide += (uint64_t)nvrtx_sm75_get(&word, NVRTX_SM75_MEM_OFFSET_LOW, 24U);
			cell = sm75_machine_memory(run, wide);
			if (cell == NULL)
				return;
			if (opcode == NVRTX_SM75_OP_LDG)
				sm75_machine_write(run, t, dst, *cell);
			else
				*cell = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U));
			continue;
		}

		/* The ALU form: the three sources by the form. */
		alu = (uint32_t)nvrtx_sm75_get(&word, 0U, 9U);
		form = (uint32_t)nvrtx_sm75_get(&word, 9U, 3U);

		/* The first source, for the operations that have one. */
		s0 = 0U;
		if (alu != NVRTX_SM75_OP_MOV && alu != NVRTX_SM75_OP_MUFU && alu != NVRTX_SM75_OP_I2F && alu != NVRTX_SM75_OP_F2I && alu != NVRTX_SM75_OP_FRND)
			s0 = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U));

		/* The register of the third slot, for the operations that have one and FADD's moved second source; an IMAD.WIDE reads it late. */
		s2 = 0U;
		if (alu == NVRTX_SM75_OP_FMUL || alu == NVRTX_SM75_OP_FFMA || alu == NVRTX_SM75_OP_FMNMX || alu == NVRTX_SM75_OP_IADD3 || alu == NVRTX_SM75_OP_IMAD ||
		    alu == NVRTX_SM75_OP_IMAD_WIDE || alu == NVRTX_SM75_OP_LOP3 || alu == NVRTX_SM75_OP_SHF ||
		    (alu == NVRTX_SM75_OP_FADD && (form == NVRTX_SM75_FORM_IMM_SRC2 || form == NVRTX_SM75_FORM_CB_SRC2))) {
			if (alu == NVRTX_SM75_OP_IMAD_WIDE)
				t->read_class = TEST_CLASS_WIDE_ADDEND;
			s2 = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_LOW, 8U));
			t->read_class = t->instruction_class;
		}

		/* The second source by the form. */
		s1 = 0U;
		if (form == NVRTX_SM75_FORM_REG) {
			s1 = sm75_machine_read(run, t, nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_LOW, 8U));
		} else if (form == NVRTX_SM75_FORM_IMM) {
			s1 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_IMM_LOW, 32U);
		} else if (form == NVRTX_SM75_FORM_CB) {
			s1 = sm75_machine_cb_word(run, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_INDEX_LOW, 5U), (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_OFFSET_LOW, 16U));
		} else if (form == NVRTX_SM75_FORM_IMM_SRC2) {
			s1 = s2;
			s2 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_IMM_LOW, 32U);
		} else {
			printf("  instruction %u: form %u\n", t->pc, form);
			run->failed = 1;
			return;
		}

		/* The predicate input A, and the operation. */
		take = 1;
		if (alu == NVRTX_SM75_OP_SEL || alu == NVRTX_SM75_OP_FMNMX || alu == NVRTX_SM75_OP_FSETP || alu == NVRTX_SM75_OP_ISETP ||
		    alu == NVRTX_SM75_OP_IADD3 || alu == NVRTX_SM75_OP_IMAD || alu == NVRTX_SM75_OP_IMAD_WIDE || alu == NVRTX_SM75_OP_LOP3) {
			take = sm75_machine_predicate(run, t, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_LOW, 3U), t->instruction_class);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_NOT, 1U) != 0U)
				take = !take;
		}
		switch (alu) {
		case NVRTX_SM75_OP_MOV:
			sm75_machine_write(run, t, dst, s1);
			break;
		case NVRTX_SM75_OP_SEL:
			sm75_machine_write(run, t, dst, take ? s0 : s1);
			break;
		case NVRTX_SM75_OP_FMNMX:
			sm75_machine_write(run, t, dst, sm75_machine_bits(take ? fminf(sm75_machine_float(s0), sm75_machine_float(s1)) : fmaxf(sm75_machine_float(s0), sm75_machine_float(s1))));
			break;
		case NVRTX_SM75_OP_FSETP:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CMP_LOW, 4U);
			if (value == NVRTX_SM75_FCMP_LT)
				guard = sm75_machine_float(s0) < sm75_machine_float(s1);
			else if (value == NVRTX_SM75_FCMP_GE)
				guard = sm75_machine_float(s0) >= sm75_machine_float(s1);
			else if (value == NVRTX_SM75_FCMP_EQ)
				guard = sm75_machine_float(s0) == sm75_machine_float(s1);
			else
				guard = !(sm75_machine_float(s0) == sm75_machine_float(s1));
			sm75_machine_predicate_write(t, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_PDST0_LOW, 3U), guard && take);
			break;
		case NVRTX_SM75_OP_ISETP:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CMP_LOW, 3U);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SIGNED, 1U) != 0U) {
				guard = (value == NVRTX_SM75_ICMP_LT) ? ((int32_t)s0 < (int32_t)s1) : (value == NVRTX_SM75_ICMP_GE) ? ((int32_t)s0 >= (int32_t)s1) : (value == NVRTX_SM75_ICMP_EQ) ? (s0 == s1) : (s0 != s1);
			} else {
				guard = (value == NVRTX_SM75_ICMP_LT) ? (s0 < s1) : (value == NVRTX_SM75_ICMP_GE) ? (s0 >= s1) : (value == NVRTX_SM75_ICMP_EQ) ? (s0 == s1) : (s0 != s1);
			}
			sm75_machine_predicate_write(t, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_PDST0_LOW, 3U), guard && take);
			break;
		case NVRTX_SM75_OP_IADD3:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_NEG, 1U) != 0U)
				s0 = 0U - s0;
			if (form == NVRTX_SM75_FORM_REG && nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_NEG, 1U) != 0U)
				s1 = 0U - s1;
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_NEG, 1U) != 0U)
				s2 = 0U - s2;
			sm75_machine_write(run, t, dst, s0 + s1 + s2);
			break;
		case NVRTX_SM75_OP_LOP3:
			lut = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_LUT_LOW, 8U);
			value = 0U;
			for (bit = 0U; bit < 32U; bit++) {
				uint32_t index;

				index = (((s0 >> bit) & 1U) << 2) | (((s1 >> bit) & 1U) << 1) | ((s2 >> bit) & 1U);
				value |= ((lut >> index) & 1U) << bit;
			}
			sm75_machine_write(run, t, dst, value);
			break;
		case NVRTX_SM75_OP_SHF:
			wide = ((uint64_t)s2 << 32) | s0;
			s1 &= 31U;
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_RIGHT, 1U) != 0U) {
				if (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_TYPE_LOW, 2U) == NVRTX_SM75_SHF_TYPE_I32)
					wide = (uint64_t)((int64_t)wide >> s1);
				else
					wide >>= s1;
			} else {
				wide <<= s1;
			}
			sm75_machine_write(run, t, dst, (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_HIGH, 1U) != 0U) ? (uint32_t)(wide >> 32) : (uint32_t)wide);
			break;
		case NVRTX_SM75_OP_FADD:
		case NVRTX_SM75_OP_FMUL:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_NEG, 1U) != 0U)
				s0 ^= 0x80000000U;
			if (form == NVRTX_SM75_FORM_REG && nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_NEG, 1U) != 0U)
				s1 ^= 0x80000000U;
			if (alu == NVRTX_SM75_OP_FMUL) {
				f = sm75_machine_float(s0) * sm75_machine_float(s1);
			} else if (form == NVRTX_SM75_FORM_IMM_SRC2) {
				f = sm75_machine_float(s0) + sm75_machine_float(s2);
			} else {
				f = sm75_machine_float(s0) + sm75_machine_float(s1);
			}
			sm75_machine_write(run, t, dst, sm75_machine_bits(f));
			break;
		case NVRTX_SM75_OP_IMAD:
			sm75_machine_write(run, t, dst, s0 * s1 + s2);
			break;
		case NVRTX_SM75_OP_IMAD_WIDE:
			wide = (uint64_t)s0 * s1;
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_LOW, 8U);
			t->read_class = TEST_CLASS_WIDE_ADDEND;
			if (value != NVRTX_SM75_RZ)
				wide += ((uint64_t)sm75_machine_read(run, t, value + 1U) << 32) | s2;
			t->read_class = t->instruction_class;
			sm75_machine_write(run, t, dst, (uint32_t)wide);
			sm75_machine_write(run, t, dst + 1U, (uint32_t)(wide >> 32));
			break;
		case NVRTX_SM75_OP_F2I:
			sm75_machine_write(run, t, dst, sm75_machine_f2i(sm75_machine_float(s1), (int)nvrtx_sm75_get(&word, NVRTX_SM75_F2I_SIGNED, 1U)));
			break;
		case NVRTX_SM75_OP_I2F:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_I2F_SIGNED, 1U) != 0U)
				sm75_machine_write(run, t, dst, sm75_machine_bits((float)(int32_t)s1));
			else
				sm75_machine_write(run, t, dst, sm75_machine_bits((float)s1));
			break;
		case NVRTX_SM75_OP_FRND:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_ROUND_LOW, 2U);
			f = sm75_machine_float(s1);
			sm75_machine_write(run, t, dst, sm75_machine_bits((value == NVRTX_SM75_ROUND_DOWN) ? floorf(f) : (value == NVRTX_SM75_ROUND_UP) ? ceilf(f) : (value == NVRTX_SM75_ROUND_ZERO) ? truncf(f) : nearbyintf(f)));
			break;
		case NVRTX_SM75_OP_MUFU:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_MUFU_LOW, 6U);
			f = sm75_machine_float(s1);
			if (value == NVRTX_SM75_MUFU_RCP) {
				/* The unit is not exact: the reciprocal is off by one unit in the last place, either way. */
				f = 1.0f / f;
				if (isfinite(f) && f != 0.0f)
					f = sm75_machine_float(sm75_machine_bits(f) + (((s1 & 1U) != 0U) ? 1U : 0xffffffffU));
			}
			else if (value == NVRTX_SM75_MUFU_RSQ)
				f = 1.0f / sqrtf(f);
			else if (value == NVRTX_SM75_MUFU_SQRT)
				f = sqrtf(f);
			else if (value == NVRTX_SM75_MUFU_EX2)
				f = exp2f(f);
			else if (value == NVRTX_SM75_MUFU_LG2)
				f = log2f(f);
			else if (value == NVRTX_SM75_MUFU_SIN)
				f = sinf(f * 6.28318530718f);
			else
				f = cosf(f * 6.28318530718f);
			sm75_machine_write(run, t, dst, sm75_machine_bits(f));
			break;
		default:
			printf("  instruction %u: ALU opcode 0x%03x not interpreted\n", t->pc, alu);
			run->failed = 1;
			return;
		}
	}

	/* An invocation that never reached EXIT. */
	printf("  an invocation ran %u instructions without EXIT\n", TEST_STEPS);
	run->failed = 1;
}

/* Finds the word at a GPU address in the bound buffers; NULL (a failure) outside them. */
uint32_t *
sm75_machine_memory(
	struct test_run *run,
	uint64_t address)
{
	uint64_t offset;
	uint32_t index;

	/* The buffer the address falls in, inside its bytes and aligned. */
	for (index = 0U; index < run->buffer_count; index++) {
		offset = address - (TEST_BUFFER_BASE + (uint64_t)index * TEST_BUFFER_STRIDE);
		if (address < TEST_BUFFER_BASE + (uint64_t)index * TEST_BUFFER_STRIDE || offset >= TEST_BUFFER_STRIDE)
			continue;
		if (offset + 4U > run->buffers[index].bytes || (offset & 3U) != 0U)
			break;
		return &run->buffers[index].words[offset / 4U];
	}

	/* Outside every buffer. */
	printf("  an access at 0x%llx outside the buffers\n", (unsigned long long)address);
	run->failed = 1;
	return NULL;
}

/* Reads a word of a constant buffer. */
uint32_t
sm75_machine_cb_word(
	const struct test_run *run,
	uint32_t index,
	uint32_t offset)
{
	uint32_t value;

	memcpy(&value, &run->cb[index & 7U][offset & 0xfffcU], sizeof(value));
	return value;
}

/* The float of 32 bits. */
float
sm75_machine_float(
	uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* The 32 bits of a float. */
uint32_t
sm75_machine_bits(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* Converts a float to a 32-bit integer toward zero as the hardware does: saturated, NaN to 0. */
uint32_t
sm75_machine_f2i(
	float value,
	int is_signed)
{
	/* NaN is 0. */
	if (value != value)
		return 0U;

	/* A signed result saturates at its two ends. */
	if (is_signed != 0) {
		if (value >= 2147483648.0f)
			return 0x7fffffffU;
		if (value <= -2147483648.0f)
			return 0x80000000U;
		return (uint32_t)(int32_t)truncf(value);
	}

	/* An unsigned one at 0 and at 2^32 - 1. */
	if (value >= 4294967296.0f)
		return 0xffffffffU;
	if (value <= 0.0f)
		return 0U;

	/* Succeeded: the value rounded toward zero. */
	return (uint32_t)truncf(value);
}

/*
 * Answers TEX and TLD with a texel made from the operands, which a test
 * predicts: a sample gives (u, v, the handle as a float, 1 + l + o), l
 * its bias or level (0 for none); a fetch
 * gives (x, y, the handle, the level + o), its integer operands made floats;
 * o is the constant texel offset (AOFFI) as u * 256 + v * 16 + r, or 0.
 * The level of detail register is the second source when the mode has one,
 * and the offset the register after it (or the second source alone).
 */
static void
sm75_machine_texture(
	struct test_run *run,
	struct test_thread *t,
	const struct nvrtx_sm75_word *word)
{
	uint32_t handle;
	uint32_t dst;
	uint32_t second;
	uint32_t coordinates;
	uint32_t extra;
	uint32_t mode;
	uint32_t bits;
	float level;
	float offset;
	int has_level;

	/* The handle, the destinations, the coordinates and the second source. */
	handle = sm75_machine_cb_word(run, (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_BUFFER_LOW, 5U), (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_HANDLE_LOW, NVRTX_SM75_TEX_HANDLE_BITS) * 4U);
	dst = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_DST_LOW, 8U);
	second = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_DST2_LOW, 8U);
	coordinates = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC0_LOW, 8U);
	extra = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC1_LOW, 8U);
	mode = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_LOD_LOW, 3U);

	/* The level of detail register, when the mode reads one. */
	has_level = 0;
	level = 0.0f;
	if (mode == NVRTX_SM75_TEX_LOD_BIAS || mode == NVRTX_SM75_TEX_LOD_EXPLICIT) {
		has_level = 1;
		level = sm75_machine_float(sm75_machine_read(run, t, extra));
		if (nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS) == NVRTX_SM75_OP_TLD)
			level = (float)(int32_t)sm75_machine_read(run, t, extra);
	}

	/* The offset, after the level. */
	offset = 0.0f;
	if (nvrtx_sm75_get(word, NVRTX_SM75_TEX_AOFFI, 1U) != 0U) {
		bits = sm75_machine_read(run, t, extra + (uint32_t)has_level);
		offset = (float)(sm75_machine_offset(bits, 0U) * 256 + sm75_machine_offset(bits, 4U) * 16 + sm75_machine_offset(bits, 8U));
	}

	/* A fetch: the integer coordinates and the level. */
	if (nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS) == NVRTX_SM75_OP_TLD) {
		if (mode != NVRTX_SM75_TEX_LOD_EXPLICIT && mode != NVRTX_SM75_TEX_LOD_ZERO) {
			printf("  TLD without an explicit level of detail\n");
			run->failed = 1;
		}
		sm75_machine_write(run, t, dst, sm75_machine_bits((float)(int32_t)sm75_machine_read(run, t, coordinates)));
		sm75_machine_write(run, t, dst + 1U, sm75_machine_bits((float)(int32_t)sm75_machine_read(run, t, coordinates + 1U)));
		sm75_machine_write(run, t, second, sm75_machine_bits((float)handle));
		sm75_machine_write(run, t, second + 1U, sm75_machine_bits(level + offset));
		return;
	}

	/* A sample: the coordinates as they are. */
	sm75_machine_write(run, t, dst, sm75_machine_read(run, t, coordinates));
	sm75_machine_write(run, t, dst + 1U, sm75_machine_read(run, t, coordinates + 1U));
	sm75_machine_write(run, t, second, sm75_machine_bits((float)handle));
	sm75_machine_write(run, t, second + 1U, sm75_machine_bits(1.0f + level + offset));
}

/* Reads one signed 4-bit component of a texel offset register. */
static int32_t
sm75_machine_offset(
	uint32_t bits,
	unsigned low)
{
	int32_t value;

	/* The four bits, sign-extended. */
	value = (int32_t)((bits >> low) & 0xfU);
	if (value >= 8)
		value -= 16;

	/* Succeeded: the component. */
	return value;
}

/*
 * Issues an instruction for the dependence check: its cycle (the one
 * before's plus that one's delay), the scoreboards it waits for (each set
 * at least 2 cycles before, and then free: what they guarded is
 * complete), and the ones it sets (which must be free).
 */
static void
sm75_machine_issue(
	struct test_run *run,
	struct test_thread *t,
	const struct nvrtx_sm75_word *word)
{
	uint32_t delay;
	uint32_t wait;
	uint32_t bar;
	uint32_t reg;

	/* The cycle, and the next one's. */
	t->cycle = t->next_cycle;
	delay = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_DELAY_LOW, NVRTX_SM75_DELAY_BITS);
	if (delay == 0U) {
		printf("  instruction %u has no delay\n", t->pc);
		run->failed = 1;
	}
	t->next_cycle = t->cycle + delay;

	/* The class, and nothing read or written yet; the address unit's reads are a class of their own. */
	t->instruction_class = sm75_machine_class((uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS));
	t->read_class = t->instruction_class;
	if (sm75_machine_agu((uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS)) != 0)
		t->read_class = TEST_CLASS_AGU;
	sm75_machine_memory_order(run, t, word);
	t->read_count = 0U;
	t->write_count = 0U;
	t->issued = 1;

	/* The scoreboards waited for. */
	wait = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_WAIT_LOW, NVRTX_SM75_WAIT_BITS);
	for (bar = 0U; bar < 6U; bar++) {
		if ((wait & (1U << bar)) == 0U || (t->bar_busy & (1U << bar)) == 0U)
			continue;
		if (t->cycle < t->bar_set_cycle[bar] + 2U) {
			printf("  instruction %u waits for scoreboard %u %u cycles after it was set\n", t->pc, bar, t->cycle - t->bar_set_cycle[bar]);
			run->failed = 1;
		}
		for (reg = 0U; reg < 256U; reg++) {
			t->gpr[reg].read_bars &= ~(1U << bar);
			if (t->gpr[reg].write_bar == bar + 1U) {
				t->gpr[reg].written = 0;
				t->gpr[reg].write_bar = 0U;
			}
		}
		t->bar_busy &= ~(1U << bar);
	}

	/* The scoreboards set, which nothing may still use. */
	t->sets_write_bar = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_WRITE_BAR_LOW, NVRTX_SM75_BAR_BITS);
	t->sets_read_bar = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_READ_BAR_LOW, NVRTX_SM75_BAR_BITS);
	if ((t->sets_write_bar != NVRTX_SM75_BAR_NONE && (t->bar_busy & (1U << t->sets_write_bar)) != 0U) ||
	    (t->sets_read_bar != NVRTX_SM75_BAR_NONE && (t->bar_busy & (1U << t->sets_read_bar)) != 0U)) {
		printf("  instruction %u sets a scoreboard still in use\n", t->pc);
		run->failed = 1;
	}
}

/*
 * Ends an instruction for the dependence check: a variable-latency one's
 * reads and writes are in flight on its scoreboards (it must have set
 * them), a fixed-latency one's at its cycle.
 */
static void
sm75_machine_retire(
	struct test_run *run,
	struct test_thread *t)
{
	struct test_hazard *hazard;
	uint32_t index;

	/* Nothing issued. */
	if (t->issued == 0)
		return;
	t->issued = 0;

	/* A variable-latency instruction needs a scoreboard for what it reads and for what it writes. */
	if (t->instruction_class == TEST_CLASS_DECOUPLED) {
		if (t->read_count != 0U && t->sets_read_bar == NVRTX_SM75_BAR_NONE) {
			printf("  instruction %u reads registers late without a scoreboard\n", t->pc);
			run->failed = 1;
		}
		if (t->write_count != 0U && t->sets_write_bar == NVRTX_SM75_BAR_NONE) {
			printf("  instruction %u writes registers late without a scoreboard\n", t->pc);
			run->failed = 1;
		}
	}

	/* The reads. */
	for (index = 0U; index < t->read_count; index++) {
		hazard = &t->gpr[t->reads[index]];
		if (t->instruction_class == TEST_CLASS_DECOUPLED && t->sets_read_bar != NVRTX_SM75_BAR_NONE) {
			hazard->read_bars |= 1U << t->sets_read_bar;
		} else {
			hazard->read = 1;
			hazard->read_cycle = t->cycle;
		}
	}

	/* The writes. */
	for (index = 0U; index < t->write_count; index++) {
		hazard = &t->gpr[t->writes[index]];
		hazard->written = 1;
		hazard->write_class = t->instruction_class;
		hazard->write_cycle = t->cycle;
		hazard->write_bar = 0U;
		if (t->instruction_class == TEST_CLASS_DECOUPLED && t->sets_write_bar != NVRTX_SM75_BAR_NONE)
			hazard->write_bar = t->sets_write_bar + 1U;
	}

	/* The scoreboards set are in use from this cycle. */
	if (t->sets_write_bar != NVRTX_SM75_BAR_NONE) {
		t->bar_busy |= 1U << t->sets_write_bar;
		t->bar_set_cycle[t->sets_write_bar] = t->cycle;
	}
	if (t->sets_read_bar != NVRTX_SM75_BAR_NONE) {
		t->bar_busy |= 1U << t->sets_read_bar;
		t->bar_set_cycle[t->sets_read_bar] = t->cycle;
	}
}

/* Reads a register for the running instruction, after its writer is done. */
static uint32_t
sm75_machine_read(
	struct test_run *run,
	struct test_thread *t,
	uint32_t reg)
{
	struct test_hazard *hazard;

	/* RZ is always ready. */
	if (reg >= NVRTX_SM75_RZ)
		return 0U;
	hazard = &t->gpr[reg];

	/* A variable-latency write in flight must have been waited for, a fixed-latency one waited out. */
	if (hazard->written != 0 && hazard->write_bar != 0U) {
		sm75_machine_hazard(run, t, "reads before the scoreboard of its write: R", reg);
	} else if (hazard->written != 0 && t->cycle < hazard->write_cycle + sm75_machine_raw(sm75_machine_sm(run), hazard->write_class, t->read_class)) {
		sm75_machine_hazard(run, t, "reads too soon after its write: R", reg);
	}

	/* Recorded for the retirement. */
	if (t->read_count < 16U) {
		t->reads[t->read_count] = reg;
		t->read_count++;
	}
	return t->r[reg];
}

/* Writes a register for the running instruction, after the write and the reads in flight are done. */
static void
sm75_machine_write(
	struct test_run *run,
	struct test_thread *t,
	uint32_t reg,
	uint32_t value)
{
	struct test_hazard *hazard;
	uint32_t need;

	/* A write to RZ is dropped. */
	if (reg >= NVRTX_SM75_RZ)
		return;
	hazard = &t->gpr[reg];

	/* The write and the reads in flight on scoreboards. */
	if (hazard->written != 0 && hazard->write_bar != 0U)
		sm75_machine_hazard(run, t, "writes before the scoreboard of the write before: R", reg);
	if (hazard->read_bars != 0U)
		sm75_machine_hazard(run, t, "writes before the scoreboard of a read: R", reg);

	/* A fixed-latency write before: 1 cycle, 6 for a variable-latency writer (write_after_write). */
	need = 1U;
	if (t->instruction_class == TEST_CLASS_DECOUPLED)
		need = 6U;
	if (hazard->written != 0 && hazard->write_bar == 0U && t->cycle < hazard->write_cycle + need)
		sm75_machine_hazard(run, t, "writes too soon after the write before: R", reg);

	/* A fixed-latency read before a variable-latency write: 2 cycles (write_after_read). */
	if (t->instruction_class == TEST_CLASS_DECOUPLED && hazard->read != 0 && t->cycle < hazard->read_cycle + 2U)
		sm75_machine_hazard(run, t, "writes too soon after a read: R", reg);

	/* Recorded for the retirement, and written. */
	if (t->write_count < 8U) {
		t->writes[t->write_count] = reg;
		t->write_count++;
	}
	t->r[reg] = value;
}

/*
 * Reads a predicate for the running instruction (PT always), after the
 * comparison that wrote it: 4 cycles for an ALU reader, 5 for a float or
 * IMAD one, 12 for a guard, a branch or a variable-latency one (13 on
 * Ampere and Ada).
 */
static int
sm75_machine_predicate(
	struct test_run *run,
	struct test_thread *t,
	uint32_t predicate,
	uint32_t reader)
{
	struct test_hazard *hazard;
	uint32_t need;

	/* PT. */
	if (predicate >= NVRTX_SM75_PT)
		return 1;
	hazard = &t->predicate[predicate];

	/* The latency by the reader and the shader model. */
	need = 12U;
	if (sm75_machine_sm(run) >= NVRTX_SM_AMPERE_FIRST)
		need = 13U;
	if (reader == TEST_CLASS_ALU)
		need = 4U;
	if (reader == TEST_CLASS_FMA || reader == TEST_CLASS_IMAD || reader == TEST_CLASS_IMAD_WIDE)
		need = 5U;
	if (hazard->written != 0 && t->cycle < hazard->write_cycle + need)
		sm75_machine_hazard(run, t, "reads a predicate too soon after its comparison: P", predicate);

	/* Succeeded: the predicate. */
	return t->p[predicate];
}

/* Writes a predicate for the running instruction (a comparison, of fixed latency). */
static void
sm75_machine_predicate_write(
	struct test_thread *t,
	uint32_t predicate,
	int value)
{
	/* PT is never written. */
	if (predicate >= NVRTX_SM75_PT)
		return;

	/* In flight from this cycle. */
	t->predicate[predicate].written = 1;
	t->predicate[predicate].write_cycle = t->cycle;
	t->p[predicate] = value;
}

/* Checks that EXIT, which reads the outputs, finds every register's write done. */
static void
sm75_machine_exit_check(
	struct test_run *run,
	struct test_thread *t)
{
	uint32_t reg;

	/* Each register written. */
	for (reg = 0U; reg < NVRTX_SM75_RZ; reg++) {
		if (t->gpr[reg].written == 0)
			continue;
		if (t->gpr[reg].write_bar != 0U || t->cycle < t->gpr[reg].write_cycle + 6U)
			sm75_machine_hazard(run, t, "EXIT before the write of R", reg);
	}
}

/* Reports a dependence the code does not keep. */
static void
sm75_machine_hazard(
	struct test_run *run,
	struct test_thread *t,
	const char *what,
	uint32_t reg)
{
	/* The instruction and the register. */
	printf("  instruction %u %s%u\n", t->pc, what, reg);
	run->failed = 1;
}

/* Reports the latency class of an instruction by its opcode (NAK's op_category for SM75). */
static uint32_t
sm75_machine_class(
	uint32_t opcode)
{
	uint32_t alu;

	/* The instructions of their own 12-bit opcodes. */
	if (opcode == NVRTX_SM75_OP_NOP)
		return TEST_CLASS_DISPATCH;
	if (opcode == NVRTX_SM75_OP_S2R || opcode == NVRTX_SM75_OP_LDC || opcode == NVRTX_SM75_OP_LDG || opcode == NVRTX_SM75_OP_STG ||
	    opcode == NVRTX_SM75_OP_LDS || opcode == NVRTX_SM75_OP_STS || opcode == NVRTX_SM75_OP_ATOMG || opcode == NVRTX_SM75_OP_ATOMG_CAS ||
	    opcode == NVRTX_SM75_OP_ATOMS || opcode == NVRTX_SM75_OP_ATOMS_CAS || opcode == NVRTX_SM75_OP_BRA || opcode == NVRTX_SM75_OP_EXIT ||
	    opcode == NVRTX_SM75_OP_BAR || opcode == NVRTX_SM75_OP_MEMBAR || opcode == NVRTX_SM75_OP_BSSY || opcode == NVRTX_SM75_OP_BSYNC ||
	    opcode == NVRTX_SM75_OP_ALD || opcode == NVRTX_SM75_OP_AST || opcode == NVRTX_SM75_OP_IPA || opcode == NVRTX_SM75_OP_TEX ||
	    opcode == NVRTX_SM75_OP_TLD || opcode == NVRTX_SM75_OP_KILL)
		return TEST_CLASS_DECOUPLED;

	/* The ALU ones by their 9 bits. */
	alu = opcode & 0x1ffU;
	if (alu == NVRTX_SM75_OP_FADD || alu == NVRTX_SM75_OP_FMUL || alu == NVRTX_SM75_OP_FFMA)
		return TEST_CLASS_FMA;
	if (alu == NVRTX_SM75_OP_IMAD)
		return TEST_CLASS_IMAD;
	if (alu == NVRTX_SM75_OP_IMAD_WIDE)
		return TEST_CLASS_IMAD_WIDE;
	if (alu == NVRTX_SM75_OP_MUFU || alu == NVRTX_SM75_OP_I2F || alu == NVRTX_SM75_OP_F2I || alu == NVRTX_SM75_OP_FRND)
		return TEST_CLASS_DECOUPLED;

	/* Succeeded: MOV, SEL, FMNMX, FSETP, ISETP, IADD3, LOP3, SHF. */
	return TEST_CLASS_ALU;
}

/*
 * Reports the cycles from a fixed-latency register write to a read
 * (read_after_write; an IMAD.WIDE writes as its upper half): Turing's
 * table (sm75_instr_latencies.rs), or Ampere's and Ada's
 * (sm80_instr_latencies.rs), which reads the addend as an FMA and lets a
 * variable-latency reader read sooner.
 */
static uint32_t
sm75_machine_raw(
	uint32_t sm,
	uint32_t writer,
	uint32_t reader)
{
	int ampere;

	ampere = 0;
	if (sm >= NVRTX_SM_AMPERE_FIRST)
		ampere = 1;

	/* A variable-latency reader: 6 on Turing; 4 on Ampere and Ada, 5 for the address unit. */
	if (reader == TEST_CLASS_DECOUPLED || reader == TEST_CLASS_GUARD || reader == TEST_CLASS_AGU) {
		if (ampere == 0)
			return 6U;
		if (reader == TEST_CLASS_AGU)
			return 5U;
		return 4U;
	}

	/* An IMAD.WIDE's addend: on Turing 2 after another IMAD.WIDE; on Ampere and Ada as an FMA reads. */
	if (reader == TEST_CLASS_WIDE_ADDEND) {
		if (writer == TEST_CLASS_ALU || writer == TEST_CLASS_DISPATCH)
			return 5U;
		if (writer == TEST_CLASS_IMAD_WIDE && ampere == 0)
			return 2U;
		return 4U;
	}

	/* An IMAD.WIDE's factors. */
	if (reader == TEST_CLASS_IMAD_WIDE) {
		if (writer == TEST_CLASS_ALU || writer == TEST_CLASS_DISPATCH)
			return 5U;
		if (writer == TEST_CLASS_IMAD_WIDE)
			return 6U;
		return 4U;
	}

	/* A float or IMAD reader. */
	if (reader == TEST_CLASS_FMA || reader == TEST_CLASS_IMAD) {
		if (writer == TEST_CLASS_ALU || writer == TEST_CLASS_DISPATCH)
			return 5U;
		return 4U;
	}

	/* An ALU reader: 4 after an ALU writer, 5 after a float or IMAD one. */
	if (writer == TEST_CLASS_ALU || writer == TEST_CLASS_DISPATCH)
		return 4U;
	return 5U;
}

/* Reports the shader model of the program run (Turing for one made without it). */
static uint32_t
sm75_machine_sm(
	const struct test_run *run)
{
	/* A program made by hand. */
	if (run->binary->shader_model == 0U)
		return NVRTX_SM_TURING;
	return run->binary->shader_model;
}

/* Reports nonzero for an instruction whose reads are the address unit's (NAK's DecoupledAgu on Ampere and Ada). */
static int
sm75_machine_agu(
	uint32_t opcode)
{
	/* The memory accesses, the attribute accesses, IPA and BAR. */
	if (opcode == NVRTX_SM75_OP_LDG || opcode == NVRTX_SM75_OP_STG || opcode == NVRTX_SM75_OP_ATOMG || opcode == NVRTX_SM75_OP_ATOMG_CAS ||
	    opcode == NVRTX_SM75_OP_LDS || opcode == NVRTX_SM75_OP_STS || opcode == NVRTX_SM75_OP_ATOMS || opcode == NVRTX_SM75_OP_ATOMS_CAS ||
	    opcode == NVRTX_SM75_OP_ALD || opcode == NVRTX_SM75_OP_AST || opcode == NVRTX_SM75_OP_IPA || opcode == NVRTX_SM75_OP_BAR)
		return 1;
	return 0;
}

/*
 * Checks a global access's memory order (strong at the GPU's scope): before
 * SM80 the scope 2 at 77..78 and the order 2 at 79..80, from SM80 on 7 in
 * the one field 77..80 (NAK's set_mem_order).
 */
static void
sm75_machine_memory_order(
	struct test_run *run,
	struct test_thread *t,
	const struct nvrtx_sm75_word *word)
{
	uint32_t opcode;
	uint32_t field;
	uint32_t want;

	/* Only the global accesses. */
	opcode = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS);
	if (opcode != NVRTX_SM75_OP_LDG && opcode != NVRTX_SM75_OP_STG && opcode != NVRTX_SM75_OP_ATOMG && opcode != NVRTX_SM75_OP_ATOMG_CAS)
		return;

	/* The field by the shader model. */
	field = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM80_MEM_ORDER_LOW, NVRTX_SM80_MEM_ORDER_BITS);
	want = NVRTX_SM75_MEM_SCOPE_GPU | (NVRTX_SM75_MEM_ORDER_STRONG << 2);
	if (sm75_machine_sm(run) >= NVRTX_SM_AMPERE_FIRST)
		want = NVRTX_SM80_MEM_STRONG_GPU;
	if (field != want) {
		printf("  instruction %u has the memory order 0x%x, not 0x%x of shader model %u\n", t->pc, field, want, sm75_machine_sm(run));
		run->failed = 1;
	}
}
