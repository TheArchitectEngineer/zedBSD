/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws075-p022: an independent check of the software scoreboard of a Gen12 kernel the i915 compiler made
 * (compiler/eu.c, drv_i915_eu_schedule()).  It walks the kernel in order and keeps, per token (SBID), the
 * registers the out-of-order instruction (MATH, SEND) holding it will still write and still read; a
 * sync.nop on $n.dst frees the token, one on $n.src forgets its reads (and frees the token of a message that
 * writes no register, as the encoder has always taken it).  It fails (returns the index of the
 * instruction, or -1 when the kernel is sound):
 *
 *   - an instruction that reads or writes a register an in-flight token will write (not waited for .dst);
 *   - an instruction that writes a register an in-flight token still reads (not waited for .src);
 *   - an out-of-order instruction that takes a token that is still in flight;
 *   - a WHILE, the first instruction of a loop or the end-of-thread SEND with a token in flight.
 *
 * An IF body is walked in order like straight code: the channels that jump over it run less, never more.
 * The register runs are worked out here on their own (not with the encoder's decoder): a SEND's from its
 * descriptors, an ALU operand's from its subregister, channels, type and stride.
 */

#ifndef WS075_SCOREBOARD_CHECK_H
#define WS075_SCOREBOARD_CHECK_H

#include <stdint.h>
#include <string.h>

/* One run of general registers: [first, first + count). */
struct sbc_run {
	unsigned first;
	unsigned count;
};

/* Reads the bits high..low of a 128-bit instruction. */
static unsigned
sbc_field(const uint32_t *inst, unsigned high, unsigned low)
{
	unsigned value = 0U;
	unsigned position;

	for (position = low; position <= high; position++)
		value |= ((inst[position / 32U] >> (position % 32U)) & 1U) << (position - low);
	return value;
}

/* The general registers an ALU operand spans: dst (-1), src0 (0) or src1 (1); an empty run otherwise. */
static struct sbc_run
sbc_alu_operand(const uint32_t *inst, int which)
{
	static const unsigned hstrides[4] = { 0U, 1U, 2U, 4U };
	struct sbc_run run = { 0U, 0U };
	unsigned channels = 1U << sbc_field(inst, 18U, 16U);
	unsigned file, nr, subnr, type, hstride, vstride, bytes;

	if (which < 0) {
		file = sbc_field(inst, 50U, 50U);
		nr = sbc_field(inst, 63U, 56U);
		subnr = sbc_field(inst, 55U, 51U);
		type = sbc_field(inst, 39U, 36U);
		hstride = hstrides[sbc_field(inst, 49U, 48U)];
		vstride = 1U;
	} else if (which == 0) {
		if (sbc_field(inst, 46U, 46U) != 0U)
			return run;
		file = sbc_field(inst, 66U, 66U);
		nr = sbc_field(inst, 79U, 72U);
		subnr = sbc_field(inst, 71U, 67U);
		type = sbc_field(inst, 43U, 40U);
		hstride = hstrides[sbc_field(inst, 65U, 64U)];
		vstride = sbc_field(inst, 87U, 84U);
	} else {
		if (sbc_field(inst, 47U, 47U) != 0U)
			return run;
		file = sbc_field(inst, 98U, 98U);
		nr = sbc_field(inst, 111U, 104U);
		subnr = sbc_field(inst, 103U, 99U);
		type = sbc_field(inst, 91U, 88U);
		hstride = hstrides[sbc_field(inst, 97U, 96U)];
		vstride = sbc_field(inst, 119U, 116U);
	}
	if (file == 0U)
		return run;
	if (vstride == 0U && hstride == 0U)
		bytes = subnr + (1U << (type & 3U));
	else
		bytes = subnr + channels * (1U << (type & 3U)) * (hstride == 0U ? 1U : hstride);
	run.first = nr;
	run.count = (bytes + 31U) / 32U;
	return run;
}

static int
sbc_meet(struct sbc_run a, struct sbc_run b)
{
	if (a.count == 0U || b.count == 0U)
		return 0;
	return a.first < b.first + b.count && b.first < a.first + a.count;
}

/* Checks a kernel of `count` instructions; returns -1 when sound, else the index of the first fault. */
static int
sbc_check(const uint32_t *code, unsigned count)
{
	struct {
		int busy;
		struct sbc_run write;
		struct sbc_run reads[2];
	} tokens[16];
	unsigned char *loop_top;
	unsigned index, id, opcode, swsb, k;
	int fault = -1;

	loop_top = calloc(count + 1U, 1U);
	if (loop_top == NULL)
		return 0;
	memset(tokens, 0, sizeof(tokens));

	/* The first instruction of each loop: a WHILE's (negative) jump, in bytes, in bits 127:96. */
	for (index = 0U; index < count; index++) {
		const uint32_t *inst = code + index * 4U;

		if (sbc_field(inst, 6U, 0U) == 39U) {
			int32_t jump = (int32_t)inst[3];
			int target = (int)index + jump / 16;

			if (target >= 0 && (unsigned)target < count)
				loop_top[target] = 1U;
		}
	}

	for (index = 0U; index < count && fault < 0; index++) {
		const uint32_t *inst = code + index * 4U;
		struct sbc_run write = { 0U, 0U };
		struct sbc_run reads[2] = { { 0U, 0U }, { 0U, 0U } };
		int send, eot, out_of_order;

		opcode = sbc_field(inst, 6U, 0U);
		swsb = sbc_field(inst, 15U, 8U);

		/* sync.nop: $n.dst (0x20 | n) frees the token, $n.src (0x30 | n) forgets its reads. */
		if (opcode == 1U) {
			if ((swsb & 0xF0U) == 0x20U) {
				tokens[swsb & 0xFU].busy = 0;
			} else if ((swsb & 0xF0U) == 0x30U) {
				tokens[swsb & 0xFU].reads[0].count = 0U;
				tokens[swsb & 0xFU].reads[1].count = 0U;
				/* a message with no register to write back has nothing left to wait for (the encoder's store) */
				if (tokens[swsb & 0xFU].write.count == 0U)
					tokens[swsb & 0xFU].busy = 0;
			}
			continue;
		}

		send = opcode == 49U || opcode == 50U;
		eot = send && sbc_field(inst, 34U, 34U) != 0U;
		out_of_order = opcode == 56U || (send && !eot);

		/* Nothing may be in flight at a loop's first instruction, a WHILE or the end of the thread. */
		if (loop_top[index] != 0U || opcode == 39U || eot) {
			for (id = 0U; id < 16U; id++) {
				if (tokens[id].busy)
					fault = (int)index;
			}
			if (fault >= 0)
				break;
		}

		/* The registers it writes and reads. */
		if (send) {
			if (sbc_field(inst, 50U, 50U) != 0U) {
				write.first = sbc_field(inst, 63U, 56U);
				write.count = sbc_field(inst, 55U, 51U);
			}
			if (sbc_field(inst, 66U, 66U) != 0U) {
				reads[0].first = sbc_field(inst, 79U, 72U);
				reads[0].count = sbc_field(inst, 70U, 67U);
			}
			if (sbc_field(inst, 98U, 98U) != 0U) {
				reads[1].first = sbc_field(inst, 111U, 104U);
				reads[1].count = sbc_field(inst, 103U, 99U);
			}
		} else if (opcode != 34U && opcode != 37U && opcode != 39U && opcode != 96U) {
			write = sbc_alu_operand(inst, -1);
			reads[0] = sbc_alu_operand(inst, 0);
			reads[1] = sbc_alu_operand(inst, 1);
		}

		/* Every hazard against a token in flight. */
		for (id = 0U; id < 16U; id++) {
			if (!tokens[id].busy)
				continue;
			if (sbc_meet(write, tokens[id].write) ||
			    sbc_meet(reads[0], tokens[id].write) ||
			    sbc_meet(reads[1], tokens[id].write))
				fault = (int)index;
			for (k = 0U; k < 2U; k++) {
				if (sbc_meet(write, tokens[id].reads[k]))
					fault = (int)index;
			}
		}
		if (fault >= 0)
			break;

		/* An out-of-order instruction takes the token its scoreboard byte names, which must be free. */
		if (out_of_order) {
			id = swsb & 0xFU;
			if (tokens[id].busy) {
				fault = (int)index;
				break;
			}
			tokens[id].busy = 1;
			tokens[id].write = write;
			tokens[id].reads[0] = reads[0];
			tokens[id].reads[1] = reads[1];
		}
	}

	free(loop_top);
	return fault;
}

#endif
