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
	uint32_t wait;
	uint32_t lut;
	uint32_t bit;
	uint32_t value;
	uint64_t wide;
	uint32_t *cell;
	int guard;
	int take;
	float f;

	code = run->binary->code;

	for (; t->steps < TEST_STEPS; t->steps++, t->pc++) {
		if (t->pc * 4U >= run->binary->code_words) {
			printf("  ran past the code\n");
			run->failed = 1;
			return;
		}
		memcpy(word.w, &code[t->pc * 4U], sizeof(word.w));
		t->r[NVRTX_SM75_RZ] = 0U;

		/* The serial dependences. */
		wait = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_WAIT_LOW, NVRTX_SM75_WAIT_BITS);
		if ((wait & t->pending) != t->pending) {
			printf("  instruction %u does not wait for scoreboards 0x%x\n", t->pc, t->pending);
			run->failed = 1;
		}
		t->pending = 0U;
		if (nvrtx_sm75_get(&word, NVRTX_SM75_WRITE_BAR_LOW, 3U) != NVRTX_SM75_BAR_NONE)
			t->pending |= 1U << nvrtx_sm75_get(&word, NVRTX_SM75_WRITE_BAR_LOW, 3U);
		if (nvrtx_sm75_get(&word, NVRTX_SM75_READ_BAR_LOW, 3U) != NVRTX_SM75_BAR_NONE)
			t->pending |= 1U << nvrtx_sm75_get(&word, NVRTX_SM75_READ_BAR_LOW, 3U);

		/* The guard. */
		guard = t->p[nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_LOW, 3U)];
		if (nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_NOT, 1U) != 0U)
			guard = !guard;
		opcode = (uint32_t)nvrtx_sm75_get(&word, 0U, 12U);
		dst = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_DST_LOW, 8U);

		/* The instructions that are not of the ALU form. */
		if (opcode == NVRTX_SM75_OP_EXIT) {
			memcpy(run->exit_regs, t->r, sizeof(run->exit_regs));
			t->state = TEST_DONE;
			return;
		}
		if (!guard)
			continue;
		if (opcode == NVRTX_SM75_OP_NOP || opcode == NVRTX_SM75_OP_BSSY || opcode == NVRTX_SM75_OP_BSYNC || opcode == NVRTX_SM75_OP_MEMBAR)
			continue;
		if (opcode == NVRTX_SM75_OP_BAR) {
			t->state = TEST_AT_BARRIER;
			t->pc++;
			t->steps++;
			return;
		}
		if (opcode == NVRTX_SM75_OP_BRA) {
			take = t->p[nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_LOW, 3U)];
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
			t->r[dst] = run->attr_in[nvrtx_sm75_get(&word, NVRTX_SM75_ATTR_ADDRESS_LOW, NVRTX_SM75_ATTR_ADDRESS_BITS) / 4U];
			continue;
		}
		if (opcode == NVRTX_SM75_OP_AST) {
			run->attr_out[nvrtx_sm75_get(&word, NVRTX_SM75_ATTR_ADDRESS_LOW, NVRTX_SM75_ATTR_ADDRESS_BITS) / 4U] = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)];
			continue;
		}
		if (opcode == NVRTX_SM75_OP_IPA) {
			/* The fragment's w is 2; a perspective input (by the header's map) arrives multiplied by it. */
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_IPA_ADDRESS_LOW, 8U);
			t->r[dst] = run->attr_in[value];
			if (value >= NVRTX_SM75_ATTR_GENERIC / 4U && value < NVRTX_SM75_ATTR_GENERIC_END / 4U) {
				lut = (run->binary->sph[(NVRTX_SM75_SPH_IMAP_GENERIC_LOW + (value - NVRTX_SM75_ATTR_GENERIC / 4U) * 2U) / 32U] >> ((NVRTX_SM75_SPH_IMAP_GENERIC_LOW + (value - NVRTX_SM75_ATTR_GENERIC / 4U) * 2U) % 32U)) & 3U;
				if (lut == NVRTX_SM75_IMAP_PERSPECTIVE)
					t->r[dst] = sm75_machine_bits(sm75_machine_float(t->r[dst]) * 2.0f);
				if (lut == 0U) {
					printf("  IPA of an input the header does not map\n");
					run->failed = 1;
				}
			}
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
			s0 = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U)];
			if ((s0 & 3U) != 0U || s0 / 4U >= run->shared_words) {
				printf("  a shared access at %u outside the %u words\n", s0, run->shared_words);
				run->failed = 1;
				return;
			}
			cell = &run->shared[s0 / 4U];
		} else if (opcode == NVRTX_SM75_OP_ATOMG || opcode == NVRTX_SM75_OP_ATOMG_CAS) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U);
			wide = ((uint64_t)t->r[s0 + 1U] << 32) | t->r[s0];
			cell = sm75_machine_memory(run, wide);
			if (cell == NULL)
				return;
		} else {
			cell = NULL;
		}
		if (opcode == NVRTX_SM75_OP_LDS) {
			t->r[dst] = *cell;
			continue;
		}
		if (opcode == NVRTX_SM75_OP_STS) {
			*cell = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)];
			continue;
		}
		if (opcode == NVRTX_SM75_OP_ATOMS_CAS || opcode == NVRTX_SM75_OP_ATOMG_CAS) {
			value = *cell;
			if (value == t->r[nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)])
				*cell = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_CAS_DATA_LOW, 8U)];
			t->r[dst] = value;
			continue;
		}
		if (opcode == NVRTX_SM75_OP_ATOMS || opcode == NVRTX_SM75_OP_ATOMG) {
			value = *cell;
			s1 = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)];
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
			t->r[dst] = value;
			continue;
		}
		if (opcode == NVRTX_SM75_OP_S2R) {
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_S2R_INDEX_LOW, 8U);
			if (value >= NVRTX_SM75_SR_TID_X && value <= NVRTX_SM75_SR_TID_Z)
				t->r[dst] = t->local[value - NVRTX_SM75_SR_TID_X];
			else if (value >= NVRTX_SM75_SR_CTAID_X && value <= NVRTX_SM75_SR_CTAID_Z)
				t->r[dst] = group[value - NVRTX_SM75_SR_CTAID_X];
			else
				run->failed = 1;
			continue;
		}
		if (opcode == NVRTX_SM75_OP_LDC) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_INDEX_LOW, 5U);
			s1 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_OFFSET_LOW, 16U);
			t->r[dst] = sm75_machine_cb_word(run, s0, s1);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_MEM_TYPE_LOW, 3U) == NVRTX_SM75_MEM_B64)
				t->r[dst + 1U] = sm75_machine_cb_word(run, s0, s1 + 4U);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_LDG || opcode == NVRTX_SM75_OP_STG) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U);
			wide = ((uint64_t)t->r[s0 + 1U] << 32) | t->r[s0];
			wide += (uint64_t)nvrtx_sm75_get(&word, NVRTX_SM75_MEM_OFFSET_LOW, 24U);
			cell = sm75_machine_memory(run, wide);
			if (cell == NULL)
				return;
			if (opcode == NVRTX_SM75_OP_LDG)
				t->r[dst] = *cell;
			else
				*cell = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)];
			continue;
		}

		/* The ALU form: the three sources by the form. */
		alu = (uint32_t)nvrtx_sm75_get(&word, 0U, 9U);
		form = (uint32_t)nvrtx_sm75_get(&word, 9U, 3U);
		s0 = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U)];
		s1 = 0U;
		s2 = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_LOW, 8U)];
		if (form == NVRTX_SM75_FORM_REG) {
			s1 = t->r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_LOW, 8U)];
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
		take = t->p[nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_LOW, 3U)];
		if (nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_NOT, 1U) != 0U)
			take = !take;
		switch (alu) {
		case NVRTX_SM75_OP_MOV:
			t->r[dst] = s1;
			break;
		case NVRTX_SM75_OP_SEL:
			t->r[dst] = take ? s0 : s1;
			break;
		case NVRTX_SM75_OP_FMNMX:
			t->r[dst] = sm75_machine_bits(take ? fminf(sm75_machine_float(s0), sm75_machine_float(s1)) : fmaxf(sm75_machine_float(s0), sm75_machine_float(s1)));
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
			t->p[nvrtx_sm75_get(&word, NVRTX_SM75_PDST0_LOW, 3U)] = guard && take;
			t->p[7] = 1;
			break;
		case NVRTX_SM75_OP_ISETP:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CMP_LOW, 3U);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SIGNED, 1U) != 0U) {
				guard = (value == NVRTX_SM75_ICMP_LT) ? ((int32_t)s0 < (int32_t)s1) : (value == NVRTX_SM75_ICMP_GE) ? ((int32_t)s0 >= (int32_t)s1) : (value == NVRTX_SM75_ICMP_EQ) ? (s0 == s1) : (s0 != s1);
			} else {
				guard = (value == NVRTX_SM75_ICMP_LT) ? (s0 < s1) : (value == NVRTX_SM75_ICMP_GE) ? (s0 >= s1) : (value == NVRTX_SM75_ICMP_EQ) ? (s0 == s1) : (s0 != s1);
			}
			t->p[nvrtx_sm75_get(&word, NVRTX_SM75_PDST0_LOW, 3U)] = guard && take;
			t->p[7] = 1;
			break;
		case NVRTX_SM75_OP_IADD3:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_NEG, 1U) != 0U)
				s0 = 0U - s0;
			if (form == NVRTX_SM75_FORM_REG && nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_NEG, 1U) != 0U)
				s1 = 0U - s1;
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_NEG, 1U) != 0U)
				s2 = 0U - s2;
			t->r[dst] = s0 + s1 + s2;
			break;
		case NVRTX_SM75_OP_LOP3:
			lut = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_LUT_LOW, 8U);
			value = 0U;
			for (bit = 0U; bit < 32U; bit++) {
				uint32_t index;

				index = (((s0 >> bit) & 1U) << 2) | (((s1 >> bit) & 1U) << 1) | ((s2 >> bit) & 1U);
				value |= ((lut >> index) & 1U) << bit;
			}
			t->r[dst] = value;
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
			t->r[dst] = (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_HIGH, 1U) != 0U) ? (uint32_t)(wide >> 32) : (uint32_t)wide;
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
			t->r[dst] = sm75_machine_bits(f);
			break;
		case NVRTX_SM75_OP_IMAD:
			t->r[dst] = s0 * s1 + s2;
			break;
		case NVRTX_SM75_OP_IMAD_WIDE:
			wide = (uint64_t)s0 * s1;
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_LOW, 8U);
			if (value != NVRTX_SM75_RZ)
				wide += ((uint64_t)t->r[value + 1U] << 32) | s2;
			t->r[dst] = (uint32_t)wide;
			t->r[dst + 1U] = (uint32_t)(wide >> 32);
			break;
		case NVRTX_SM75_OP_F2I:
			t->r[dst] = sm75_machine_f2i(sm75_machine_float(s1), (int)nvrtx_sm75_get(&word, NVRTX_SM75_F2I_SIGNED, 1U));
			break;
		case NVRTX_SM75_OP_I2F:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_I2F_SIGNED, 1U) != 0U)
				t->r[dst] = sm75_machine_bits((float)(int32_t)s1);
			else
				t->r[dst] = sm75_machine_bits((float)s1);
			break;
		case NVRTX_SM75_OP_FRND:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_ROUND_LOW, 2U);
			f = sm75_machine_float(s1);
			t->r[dst] = sm75_machine_bits((value == NVRTX_SM75_ROUND_DOWN) ? floorf(f) : (value == NVRTX_SM75_ROUND_UP) ? ceilf(f) : (value == NVRTX_SM75_ROUND_ZERO) ? truncf(f) : nearbyintf(f));
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
			t->r[dst] = sm75_machine_bits(f);
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
		level = sm75_machine_float(t->r[extra]);
		if (nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS) == NVRTX_SM75_OP_TLD)
			level = (float)(int32_t)t->r[extra];
	}

	/* The offset, after the level. */
	offset = 0.0f;
	if (nvrtx_sm75_get(word, NVRTX_SM75_TEX_AOFFI, 1U) != 0U) {
		bits = t->r[extra + (uint32_t)has_level];
		offset = (float)(sm75_machine_offset(bits, 0U) * 256 + sm75_machine_offset(bits, 4U) * 16 + sm75_machine_offset(bits, 8U));
	}

	/* A fetch: the integer coordinates and the level. */
	if (nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS) == NVRTX_SM75_OP_TLD) {
		if (mode != NVRTX_SM75_TEX_LOD_EXPLICIT && mode != NVRTX_SM75_TEX_LOD_ZERO) {
			printf("  TLD without an explicit level of detail\n");
			run->failed = 1;
		}
		t->r[dst] = sm75_machine_bits((float)(int32_t)t->r[coordinates]);
		t->r[dst + 1U] = sm75_machine_bits((float)(int32_t)t->r[coordinates + 1U]);
		t->r[second] = sm75_machine_bits((float)handle);
		t->r[second + 1U] = sm75_machine_bits(level + offset);
		return;
	}

	/* A sample: the coordinates as they are. */
	t->r[dst] = t->r[coordinates];
	t->r[dst + 1U] = t->r[coordinates + 1U];
	t->r[second] = sm75_machine_bits((float)handle);
	t->r[second + 1U] = sm75_machine_bits(1.0f + level + offset);
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
