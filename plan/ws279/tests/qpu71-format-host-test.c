/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The V3D 7.1 instruction encoder (qpu71.c) checked against the unmodified MIT Mesa packer and decoder (ws279-p004).
 *
 * Every semantic operation is encoded, decoded by Mesa's 7.1 decoder, and
 * compared with the expected Mesa operation, read addresses and write
 * address; Mesa's packer must rebuild the same word, both from the decoded
 * instruction and from an instruction built here from the semantic
 * description alone.  The refused combinations of the 7.1 word are checked
 * too.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/qpu.h"
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_instr.h"

/* One semantic operation, the Mesa operation it must decode as on each ALU, and its input unpack. */
struct expected_operation {
	enum bcm2711_qpu_operation native;
	enum v3d_qpu_add_op add;
	enum v3d_qpu_mul_op multiply;
	enum v3d_qpu_input_unpack unpack;
};

/*
 * The operation vocabulary as Mesa names it on V3D 7.1.
 *
 * An operation with no form on one ALU names that ALU's no-operation.
 */
static const struct expected_operation expected_operations[] = {
	{BCM2711_QPU_MOVE, V3D_QPU_A_MOV, V3D_QPU_M_MOV, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_HALF_LOW, V3D_QPU_A_FMOV, V3D_QPU_M_FMOV, V3D_QPU_UNPACK_L},
	{BCM2711_QPU_HALF_HIGH, V3D_QPU_A_FMOV, V3D_QPU_M_FMOV, V3D_QPU_UNPACK_H},
	{BCM2711_QPU_PACK_HALVES, V3D_QPU_A_VFPACK, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_ADD, V3D_QPU_A_FADD, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_SUBTRACT, V3D_QPU_A_FSUB, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_MULTIPLY, V3D_QPU_A_NOP, V3D_QPU_M_FMUL, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_MINIMUM, V3D_QPU_A_FMIN, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_MAXIMUM, V3D_QPU_A_FMAX, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_COMPARE, V3D_QPU_A_FCMP, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_INTEGER_ADD, V3D_QPU_A_ADD, V3D_QPU_M_ADD, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_INTEGER_SUBTRACT, V3D_QPU_A_SUB, V3D_QPU_M_SUB, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_INTEGER_MINIMUM, V3D_QPU_A_MIN, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_INTEGER_MAXIMUM, V3D_QPU_A_MAX, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_UNSIGNED_MINIMUM, V3D_QPU_A_UMIN, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_UNSIGNED_MAXIMUM, V3D_QPU_A_UMAX, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_SHIFT_LEFT, V3D_QPU_A_SHL, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_SHIFT_RIGHT, V3D_QPU_A_SHR, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_SHIFT_SIGNED, V3D_QPU_A_ASR, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_BITS_AND, V3D_QPU_A_AND, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_BITS_OR, V3D_QPU_A_OR, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_BITS_XOR, V3D_QPU_A_XOR, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_BITS_NOT, V3D_QPU_A_NOT, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_INTEGER_NEGATE, V3D_QPU_A_NEG, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_RECIPROCAL, V3D_QPU_A_RECIP, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_ROUND, V3D_QPU_A_FROUND, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_TRUNCATE, V3D_QPU_A_FTRUNC, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_FLOOR, V3D_QPU_A_FFLOOR, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_CEILING, V3D_QPU_A_FCEIL, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_TO_SIGNED, V3D_QPU_A_FTOIZ, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_TO_UNSIGNED, V3D_QPU_A_FTOUZ, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_SIGNED_TO_FLOAT, V3D_QPU_A_ITOF, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_UNSIGNED_TO_FLOAT, V3D_QPU_A_UTOF, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_DERIVATIVE_X, V3D_QPU_A_FDX, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_DERIVATIVE_Y, V3D_QPU_A_FDY, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_INVERSE_ROOT, V3D_QPU_A_RSQRT, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_EXPONENT, V3D_QPU_A_EXP, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_LOGARITHM, V3D_QPU_A_LOG, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_FLOAT_SINE, V3D_QPU_A_SIN, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_VPM_LOAD, V3D_QPU_A_LDVPMV_IN, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_VPM_STORE, V3D_QPU_A_STVPMV, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_TEXTURE_WAIT, V3D_QPU_A_TMUWT, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_VPM_WAIT, V3D_QPU_A_VPMWT, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_PIXEL_X, V3D_QPU_A_FXCD, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_PIXEL_Y, V3D_QPU_A_FYCD, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_INSTANCE_INDEX, V3D_QPU_A_IID, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE},
	{BCM2711_QPU_ELEMENT_INDEX, V3D_QPU_A_EIDX, V3D_QPU_M_NOP, V3D_QPU_UNPACK_NONE}};

static void check_operation(const struct v3d_device_info *device, const struct expected_operation *expected);
static void check_alu(const struct v3d_device_info *device, const struct expected_operation *expected, uint32_t multiply);
static void mesa_word(const struct v3d_device_info *device, const struct bcm2711_qpu_bundle *bundle, const struct expected_operation *expected, uint32_t multiply, uint64_t *word);
static void check_signals(const struct v3d_device_info *device);
static void check_constants(const struct v3d_device_info *device);
static void check_bundles(const struct v3d_device_info *device);
static void check_refusals(void);
static void decode_bundle(const struct v3d_device_info *device, const struct bcm2711_qpu_bundle *bundle, struct v3d_qpu_instr *decoded);
static void refuse_bundle(const struct bcm2711_qpu_bundle *bundle, int expected);

/*
 * Verifies semantic operation identity, exact roundtrip and the refused combinations.
 */
int
main(
	void)
{
	struct v3d_device_info device;
	struct bcm2711_qpu_instruction instruction;
	uint64_t word;
	uint32_t index;
	int error;

	/* The oracle is the fixed 7.1 decoder and packer, independent of the production encoder. */
	memset(&device, 0, sizeof(device));
	device.ver = 71;
	device.rev = 10;
	device.has_accumulators = false;

	/* Every semantic operation decodes as that operation on each ALU that has it. */
	for (index = 0; index < sizeof(expected_operations) / sizeof(expected_operations[0]); index++)
		check_operation(&device, &expected_operations[index]);

	/* The idle word: both ALUs discard, add variant zero of 187, multiply variant 63 of 14. */
	memset(&instruction, 0, sizeof(instruction));
	error = bcm2711_qpu_encode(&instruction, &word);
	assert(error == 0);
	assert(word == UINT64_C(0x38003186bb03f000));

	/* Signals, small immediates, paired words and refusals. */
	check_signals(&device);
	check_constants(&device);
	check_bundles(&device);
	check_refusals();

	/* Succeeded: the pinned MIT 7.1 encoder and decoder agree with the independent production words. */
	puts("qpu71-format-host-test PASS");
	return 0;
}

/* Checks one semantic operation on the add ALU and on the multiply ALU where it exists there. */
static void
check_operation(
	const struct v3d_device_info *device,
	const struct expected_operation *expected)
{
	/* The add ALU's form. */
	if (expected->add != V3D_QPU_A_NOP)
		check_alu(device, expected, 0);

	/* The multiply ALU's form. */
	if (expected->multiply != V3D_QPU_M_NOP)
		check_alu(device, expected, 1);
}

/*
 * Encodes one operation on one ALU with distinct registers and checks it through the oracle.
 *
 * Every pair of source and destination numbers is tried so no field is
 * truncated or exchanged.
 */
static void
check_alu(
	const struct v3d_device_info *device,
	const struct expected_operation *expected,
	uint32_t multiply)
{
	struct bcm2711_qpu_bundle bundle;
	struct bcm2711_qpu_instruction *instruction;
	struct v3d_qpu_instr decoded;
	uint64_t word;
	uint64_t mesa;
	uint32_t sources;
	uint32_t trial;
	uint32_t left;
	uint32_t right;
	uint32_t destination;
	int error;

	/* Several register numbers reach every bit of the six-bit fields. */
	sources = bcm2711_qpu_operation_sources(expected->native);
	for (trial = 0; trial < 64; trial++) {
		left = trial;
		right = (trial * 37U + 11U) % 64U;
		destination = (trial * 13U + 5U) % 64U;

		/* An order-sensitive maximum needs distinct inputs. */
		if (expected->native == BCM2711_QPU_FLOAT_MAXIMUM && left == right)
			continue;

		/* The operation on the chosen ALU, the other idle. */
		memset(&bundle, 0, sizeof(bundle));
		instruction = &bundle.add;
		if (multiply != 0)
			instruction = &bundle.multiply;
		instruction->operation = expected->native;
		instruction->destination.number = destination;
		instruction->left.kind = BCM2711_QPU_REGISTER;
		instruction->left.number = left;
		instruction->right.kind = BCM2711_QPU_REGISTER;
		instruction->right.number = right;

		/* The word decodes as the expected operation and repacks to itself. */
		decode_bundle(device, &bundle, &decoded);
		if (multiply == 0) {
			if (decoded.alu.add.op != expected->add || decoded.alu.mul.op != V3D_QPU_M_NOP) {
				fprintf(stderr, "operation %u on add: decoded %u/%u\n", (unsigned)expected->native, (unsigned)decoded.alu.add.op, (unsigned)decoded.alu.mul.op);
				assert(0);
			}
		} else {
			if (decoded.alu.mul.op != expected->multiply || decoded.alu.add.op != V3D_QPU_A_NOP) {
				fprintf(stderr, "operation %u on mul: decoded %u/%u\n", (unsigned)expected->native, (unsigned)decoded.alu.add.op, (unsigned)decoded.alu.mul.op);
				assert(0);
			}
		}

		/* The inputs, unpack and destination are the semantic ones; order-sensitive operations may exchange their inputs. */
		if (multiply == 0) {
			if (sources >= 1)
				assert(decoded.alu.add.a.unpack == expected->unpack);
			if (sources == 1)
				assert(decoded.alu.add.a.raddr == left);
			if (sources == 2) {
				assert((decoded.alu.add.a.raddr == left && decoded.alu.add.b.raddr == right) ||
				       (decoded.alu.add.a.raddr == right && decoded.alu.add.b.raddr == left));
			}
			if (expected->native != BCM2711_QPU_VPM_STORE) {
				assert(decoded.alu.add.waddr == destination);
				assert(!decoded.alu.add.magic_write);
			}
		} else {
			if (sources >= 1)
				assert(decoded.alu.mul.a.unpack == expected->unpack);
			if (sources == 1)
				assert(decoded.alu.mul.a.raddr == left);
			if (sources == 2) {
				assert(decoded.alu.mul.a.raddr == left);
				assert(decoded.alu.mul.b.raddr == right);
			}
			assert(decoded.alu.mul.waddr == destination);
			assert(!decoded.alu.mul.magic_write);
		}

		/* The non-commutative operations keep their input order. */
		if (multiply == 0 && sources == 2 && expected->native != BCM2711_QPU_FLOAT_ADD &&
		    expected->native != BCM2711_QPU_FLOAT_MINIMUM && expected->native != BCM2711_QPU_FLOAT_MAXIMUM) {
			assert(decoded.alu.add.a.raddr == left);
			assert(decoded.alu.add.b.raddr == right);
		}

		/* Mesa's packer, given only the semantic description, builds the same word. */
		word = 0;
		error = bcm2711_qpu_encode_bundle(&bundle, &word);
		assert(error == 0);
		mesa_word(device, &bundle, expected, multiply, &mesa);
		if (mesa != word) {
			fprintf(stderr, "operation %u on %s: production %016llx, Mesa %016llx\n", (unsigned)expected->native, multiply ? "mul" : "add", (unsigned long long)word, (unsigned long long)mesa);
			assert(0);
		}
	}
}

/* Builds the word of one register-only operation with Mesa's packer from the semantic description. */
static void
mesa_word(
	const struct v3d_device_info *device,
	const struct bcm2711_qpu_bundle *bundle,
	const struct expected_operation *expected,
	uint32_t multiply,
	uint64_t *word)
{
	const struct bcm2711_qpu_instruction *instruction;
	struct v3d_qpu_instr mesa;
	uint32_t sources;
	bool accepted;

	/* Both ALUs start idle, writing the discarding magic address. */
	memset(&mesa, 0, sizeof(mesa));
	mesa.type = V3D_QPU_INSTR_TYPE_ALU;
	mesa.alu.add.op = V3D_QPU_A_NOP;
	mesa.alu.add.waddr = V3D_QPU_WADDR_NOP;
	mesa.alu.add.magic_write = true;
	mesa.alu.mul.op = V3D_QPU_M_NOP;
	mesa.alu.mul.waddr = V3D_QPU_WADDR_NOP;
	mesa.alu.mul.magic_write = true;

	/* The semantic operation takes its ALU with its inputs in order. */
	sources = bcm2711_qpu_operation_sources(expected->native);
	if (multiply == 0) {
		instruction = &bundle->add;
		mesa.alu.add.op = expected->add;
		mesa.alu.add.waddr = instruction->destination.number;
		mesa.alu.add.magic_write = false;
		mesa.alu.add.a.unpack = expected->unpack;
		if (sources >= 1)
			mesa.alu.add.a.raddr = instruction->left.number;
		if (sources >= 2)
			mesa.alu.add.b.raddr = instruction->right.number;

		/* Mesa chooses the order of a float add or minimum itself; a maximum's inputs are given in the order it needs. */
		if (expected->native == BCM2711_QPU_FLOAT_MAXIMUM && instruction->left.number < instruction->right.number) {
			mesa.alu.add.a.raddr = instruction->right.number;
			mesa.alu.add.b.raddr = instruction->left.number;
		}
	} else {
		instruction = &bundle->multiply;
		mesa.alu.mul.op = expected->multiply;
		mesa.alu.mul.waddr = instruction->destination.number;
		mesa.alu.mul.magic_write = false;
		mesa.alu.mul.a.unpack = expected->unpack;
		if (sources >= 1)
			mesa.alu.mul.a.raddr = instruction->left.number;
		if (sources >= 2)
			mesa.alu.mul.b.raddr = instruction->right.number;
	}

	/* The packer accepts the description. */
	accepted = v3d_qpu_instr_pack(device, &mesa, word);
	assert(accepted);
}

/* Verifies every signal, the signal destination, predicates and flag pushes through the decoder. */
static void
check_signals(
	const struct v3d_device_info *device)
{
	struct bcm2711_qpu_bundle bundle;
	struct v3d_qpu_instr decoded;
	uint32_t signal;
	uint32_t predicate;
	bool addressed;

	/* Every signal alone on an idle word, with an addressed destination. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.signal_destination.number = 29;
	for (signal = 1; signal <= BCM2711_QPU_SIGNAL_UNIFORM_ADDRESS_REGISTER; signal++) {
		bundle.signal = (enum bcm2711_qpu_signal)signal;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.sig.thrsw == (signal == BCM2711_QPU_SIGNAL_SWITCH));
		assert(decoded.sig.ldunif == (signal == BCM2711_QPU_SIGNAL_UNIFORM));
		assert(decoded.sig.ldtmu == (signal == BCM2711_QPU_SIGNAL_TEXTURE));
		assert(decoded.sig.ldvary == (signal == BCM2711_QPU_SIGNAL_VARYING));
		assert(decoded.sig.ldunifrf == (signal == BCM2711_QPU_SIGNAL_UNIFORM_REGISTER));
		assert(decoded.sig.ldtlb == (signal == BCM2711_QPU_SIGNAL_TILE));
		assert(decoded.sig.ldtlbu == (signal == BCM2711_QPU_SIGNAL_TILE_CONFIGURED));
		assert(decoded.sig.wrtmuc == (signal == BCM2711_QPU_SIGNAL_TEXTURE_CONFIG));
		assert(decoded.sig.ldunifa == (signal == BCM2711_QPU_SIGNAL_UNIFORM_ADDRESS));
		assert(decoded.sig.ldunifarf == (signal == BCM2711_QPU_SIGNAL_UNIFORM_ADDRESS_REGISTER));

		/* An addressed signal names the register, never the lane flags. */
		addressed = v3d_qpu_sig_writes_address(device, &decoded.sig);
		if (addressed) {
			assert(decoded.sig_addr == 29);
			assert(!decoded.sig_magic);
		}

		/* The implicit rf0 writes of 7.1: uniform, uniform-address and varying loads. */
		assert(v3d_qpu_writes_rf0_implicitly(device, &decoded) ==
		       (signal == BCM2711_QPU_SIGNAL_UNIFORM ||
			signal == BCM2711_QPU_SIGNAL_UNIFORM_ADDRESS ||
			signal == BCM2711_QPU_SIGNAL_VARYING));
	}

	/* A magic signal destination: a texture result to the TMU's configuration address is magic. */
	bundle.signal = BCM2711_QPU_SIGNAL_TEXTURE;
	bundle.signal_destination.number = 32;
	bundle.signal_destination.peripheral = 1;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.sig_addr == 32);
	assert(decoded.sig_magic);

	/* Each predicate on each ALU reaches the oracle's condition field. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_MOVE;
	bundle.add.destination.number = 9;
	bundle.add.left.number = 4;
	for (predicate = 1; predicate <= 4; predicate++) {
		bundle.add.predicate = predicate;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.flags.ac == (enum v3d_qpu_cond)predicate);
	}

	/* Each flag push on the add ALU. */
	bundle.add.predicate = 0;
	bundle.add.operation = BCM2711_QPU_INTEGER_SUBTRACT;
	bundle.add.right.number = 5;
	for (predicate = 1; predicate <= 3; predicate++) {
		bundle.add.push_flags = predicate;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.flags.apf == (enum v3d_qpu_pf)predicate);
	}
}

/* Compares the small immediates with all forty-eight Mesa values, on each of the four read addresses. */
static void
check_constants(
	const struct v3d_device_info *device)
{
	struct bcm2711_qpu_bundle bundle;
	struct v3d_qpu_instr decoded;
	uint32_t bits;
	uint32_t index;
	uint32_t encoded;
	uint32_t decoded_bits;
	bool accepted;
	int error;

	/* Each index converts from its bits and back. */
	for (index = 0; index < 48; index++) {
		accepted = v3d_qpu_small_imm_unpack(device, index, &bits);
		assert(accepted);
		error = bcm2711_qpu_small_constant(bits, &encoded);
		assert(error == 0);
		assert(encoded == index);

		/* On the add ALU's a input. */
		memset(&bundle, 0, sizeof(bundle));
		bundle.add.operation = BCM2711_QPU_INTEGER_SUBTRACT;
		bundle.add.destination.number = 3;
		bundle.add.left.kind = BCM2711_QPU_CONSTANT;
		bundle.add.left.number = bits;
		bundle.add.right.kind = BCM2711_QPU_REGISTER;
		bundle.add.right.number = 27;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.sig.small_imm_a);
		assert(!decoded.sig.small_imm_b && !decoded.sig.small_imm_c && !decoded.sig.small_imm_d);
		accepted = v3d_qpu_small_imm_unpack(device, decoded.alu.add.a.raddr, &decoded_bits);
		assert(accepted && decoded_bits == bits);
		assert(decoded.alu.add.b.raddr == 27);

		/* On the add ALU's b input. */
		bundle.add.left.kind = BCM2711_QPU_REGISTER;
		bundle.add.left.number = 27;
		bundle.add.right.kind = BCM2711_QPU_CONSTANT;
		bundle.add.right.number = bits;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.sig.small_imm_b);
		accepted = v3d_qpu_small_imm_unpack(device, decoded.alu.add.b.raddr, &decoded_bits);
		assert(accepted && decoded_bits == bits);

		/* On the multiply ALU's a input. */
		memset(&bundle, 0, sizeof(bundle));
		bundle.multiply.operation = BCM2711_QPU_FLOAT_MULTIPLY;
		bundle.multiply.destination.number = 3;
		bundle.multiply.left.kind = BCM2711_QPU_CONSTANT;
		bundle.multiply.left.number = bits;
		bundle.multiply.right.kind = BCM2711_QPU_REGISTER;
		bundle.multiply.right.number = 27;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.sig.small_imm_c);
		accepted = v3d_qpu_small_imm_unpack(device, decoded.alu.mul.a.raddr, &decoded_bits);
		assert(accepted && decoded_bits == bits);

		/* On the multiply ALU's b input. */
		bundle.multiply.left.kind = BCM2711_QPU_REGISTER;
		bundle.multiply.left.number = 27;
		bundle.multiply.right.kind = BCM2711_QPU_CONSTANT;
		bundle.multiply.right.number = bits;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.sig.small_imm_d);
		accepted = v3d_qpu_small_imm_unpack(device, decoded.alu.mul.b.raddr, &decoded_bits);
		assert(accepted && decoded_bits == bits);

		/* A one-input move reads its immediate through a. */
		memset(&bundle, 0, sizeof(bundle));
		bundle.add.operation = BCM2711_QPU_MOVE;
		bundle.add.destination.number = 3;
		bundle.add.left.kind = BCM2711_QPU_CONSTANT;
		bundle.add.left.number = bits;
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.alu.add.op == V3D_QPU_A_MOV && decoded.sig.small_imm_a);
	}

	/* A float add, minimum and maximum with an immediate keep their identity whichever side the immediate is on. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.destination.number = 3;
	bundle.add.left.kind = BCM2711_QPU_REGISTER;
	bundle.add.left.number = 40;
	bundle.add.right.kind = BCM2711_QPU_CONSTANT;
	bundle.add.right.number = 0x3f800000U;
	bundle.add.operation = BCM2711_QPU_FLOAT_ADD;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_FADD);
	bundle.add.operation = BCM2711_QPU_FLOAT_MINIMUM;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_FMIN);
	bundle.add.operation = BCM2711_QPU_FLOAT_MAXIMUM;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_FMAX);
	bundle.add.left = bundle.add.right;
	bundle.add.right.kind = BCM2711_QPU_REGISTER;
	bundle.add.right.number = 40;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_FMAX);
	bundle.add.operation = BCM2711_QPU_FLOAT_ADD;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_FADD);

	/* Nonrepresentable constants are refused exactly, without approximating or truncating their bits. */
	encoded = 99;
	error = bcm2711_qpu_small_constant(0x3f800001U, &encoded);
	assert(error == ENOTSUP && encoded == 99);
}

/* Encodes one word, decodes it with the oracle and checks the oracle rebuilds the same word. */
static void
decode_bundle(
	const struct v3d_device_info *device,
	const struct bcm2711_qpu_bundle *bundle,
	struct v3d_qpu_instr *decoded)
{
	uint64_t word;
	uint64_t repacked;
	bool accepted;
	int error;

	/* The word must encode. */
	error = bcm2711_qpu_encode_bundle(bundle, &word);
	if (error != 0) {
		fprintf(stderr, "bundle refused with %d\n", error);
		assert(0);
	}

	/* It decodes as an ALU word. */
	memset(decoded, 0, sizeof(*decoded));
	accepted = v3d_qpu_instr_unpack(device, word, decoded);
	if (!accepted) {
		fprintf(stderr, "word %016llx does not decode\n", (unsigned long long)word);
		assert(0);
	}
	assert(decoded->type == V3D_QPU_INSTR_TYPE_ALU);

	/* It repacks to itself. */
	accepted = v3d_qpu_instr_pack(device, decoded, &repacked);
	if (!accepted || repacked != word) {
		fprintf(stderr, "word %016llx repacks as %016llx\n", (unsigned long long)word, (unsigned long long)repacked);
		assert(0);
	}
}

/* Checks paired ALU operations, combined conditions and switch signals. */
static void
check_bundles(
	const struct v3d_device_info *device)
{
	static const enum bcm2711_qpu_signal combined[6] = {
		BCM2711_QPU_SIGNAL_NONE,
		BCM2711_QPU_SIGNAL_UNIFORM,
		BCM2711_QPU_SIGNAL_TEXTURE,
		BCM2711_QPU_SIGNAL_VARYING,
		BCM2711_QPU_SIGNAL_UNIFORM_REGISTER,
		BCM2711_QPU_SIGNAL_TEXTURE_CONFIG};
	struct bcm2711_qpu_bundle bundle;
	struct v3d_qpu_instr decoded;
	uint32_t add_predicate;
	uint32_t mul_predicate;
	uint32_t push;
	uint32_t index;
	bool addressed;

	/* An add and a multiply reading four different registers: 7.1 has a read address for each input. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_FLOAT_SUBTRACT;
	bundle.add.destination.number = 10;
	bundle.add.left.number = 20;
	bundle.add.right.number = 21;
	bundle.multiply.operation = BCM2711_QPU_FLOAT_MULTIPLY;
	bundle.multiply.destination.number = 11;
	bundle.multiply.left.number = 22;
	bundle.multiply.right.number = 23;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_FSUB && decoded.alu.mul.op == V3D_QPU_M_FMUL);
	assert(decoded.alu.add.waddr == 10 && decoded.alu.mul.waddr == 11);
	assert(decoded.alu.add.a.raddr == 20 && decoded.alu.add.b.raddr == 21);
	assert(decoded.alu.mul.a.raddr == 22 && decoded.alu.mul.b.raddr == 23);

	/* A move on each ALU together: the add ALU's MOV and the multiply ALU's MOV. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_MOVE;
	bundle.add.destination.number = 7;
	bundle.add.destination.peripheral = 1;
	bundle.add.left.number = 30;
	bundle.multiply.operation = BCM2711_QPU_MOVE;
	bundle.multiply.destination.number = 12;
	bundle.multiply.left.kind = BCM2711_QPU_CONSTANT;
	bundle.multiply.left.number = 0x3f800000U;
	decode_bundle(device, &bundle, &decoded);
	assert(decoded.alu.add.op == V3D_QPU_A_MOV && decoded.alu.add.magic_write && decoded.alu.add.waddr == 7);
	assert(decoded.alu.mul.op == V3D_QPU_M_MOV && decoded.sig.small_imm_c);
	assert(decoded.alu.add.a.raddr == 30);

	/* Every combination of the two ALUs' predicates and one ALU's flag push reaches the oracle's fields. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_BITS_AND;
	bundle.add.destination.number = 6;
	bundle.add.destination.peripheral = 1;
	bundle.add.left.number = 3;
	bundle.add.right.number = 4;
	bundle.multiply.operation = BCM2711_QPU_MOVE;
	bundle.multiply.destination.number = 9;
	bundle.multiply.left.number = 4;
	for (add_predicate = 0; add_predicate <= 4; add_predicate++) {
		for (mul_predicate = 0; mul_predicate <= 4; mul_predicate++) {
			for (push = 0; push <= 3; push++) {
				bundle.add.predicate = add_predicate;
				bundle.multiply.predicate = mul_predicate;
				bundle.add.push_flags = 0;
				bundle.multiply.push_flags = 0;
				if (add_predicate == 0) {
					bundle.add.push_flags = push;
				} else if (mul_predicate == 0) {
					bundle.multiply.push_flags = push;
				} else if (push != 0) {
					continue;
				}

				/* The decoded fields are the requested ones. */
				decode_bundle(device, &bundle, &decoded);
				assert(decoded.flags.ac == (enum v3d_qpu_cond)add_predicate);
				assert(decoded.flags.mc == (enum v3d_qpu_cond)mul_predicate);
				assert(decoded.flags.apf == (enum v3d_qpu_pf)bundle.add.push_flags);
				assert(decoded.flags.mpf == (enum v3d_qpu_pf)bundle.multiply.push_flags);
			}
		}
	}

	/* A thread switch combines with each signal of the native table; the signal keeps its destination. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_FLOAT_ADD;
	bundle.add.destination.number = 10;
	bundle.add.left.number = 20;
	bundle.add.right.number = 21;
	bundle.thread_switch = 1;
	bundle.signal_destination.number = 13;
	for (index = 0; index < 6; index++) {
		bundle.signal = combined[index];
		decode_bundle(device, &bundle, &decoded);
		assert(decoded.sig.thrsw);
		assert(decoded.sig.ldunif == (combined[index] == BCM2711_QPU_SIGNAL_UNIFORM));
		assert(decoded.sig.ldtmu == (combined[index] == BCM2711_QPU_SIGNAL_TEXTURE));
		assert(decoded.sig.ldvary == (combined[index] == BCM2711_QPU_SIGNAL_VARYING));
		assert(decoded.sig.ldunifrf == (combined[index] == BCM2711_QPU_SIGNAL_UNIFORM_REGISTER));
		assert(decoded.sig.wrtmuc == (combined[index] == BCM2711_QPU_SIGNAL_TEXTURE_CONFIG));
		addressed = v3d_qpu_sig_writes_address(device, &decoded.sig);
		if (addressed)
			assert(decoded.sig_addr == 13 && !decoded.sig_magic);
	}

	/* Succeeded: paired words agree with the oracle. */
	puts("qpu71-format-host-test paired words, conditions and switch signals: PASS");
}

/* Checks the combinations a 7.1 word cannot express, each refused without touching the caller's word. */
static void
check_refusals(
	void)
{
	struct bcm2711_qpu_bundle bundle;
	struct bcm2711_qpu_instruction instruction;
	uint64_t word;
	int error;

	/* Two small immediates in one word, even the same value on the same ALU. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_INTEGER_ADD;
	bundle.add.left.kind = BCM2711_QPU_CONSTANT;
	bundle.add.left.number = 1;
	bundle.add.right.kind = BCM2711_QPU_CONSTANT;
	bundle.add.right.number = 1;
	refuse_bundle(&bundle, ENOTSUP);

	/* Two small immediates on the two ALUs. */
	bundle.add.right.kind = BCM2711_QPU_REGISTER;
	bundle.add.right.number = 9;
	bundle.multiply.operation = BCM2711_QPU_FLOAT_MULTIPLY;
	bundle.multiply.left.kind = BCM2711_QPU_CONSTANT;
	bundle.multiply.left.number = 1;
	refuse_bundle(&bundle, ENOTSUP);

	/* A small immediate beside any signal, and beside a thread switch. */
	bundle.multiply.operation = BCM2711_QPU_IDLE;
	bundle.signal = BCM2711_QPU_SIGNAL_VARYING;
	refuse_bundle(&bundle, ENOTSUP);
	bundle.signal = BCM2711_QPU_SIGNAL_TEXTURE;
	refuse_bundle(&bundle, ENOTSUP);
	bundle.signal = BCM2711_QPU_SIGNAL_NONE;
	bundle.thread_switch = 1;
	refuse_bundle(&bundle, ENOTSUP);

	/* An accumulator source: 7.1 has none. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_MOVE;
	bundle.add.left.kind = BCM2711_QPU_ACCUMULATOR;
	bundle.add.left.number = 5;
	refuse_bundle(&bundle, EINVAL);

	/* A magic write to a reserved 7.1 address: an old accumulator or special-function address. */
	bundle.add.left.kind = BCM2711_QPU_REGISTER;
	bundle.add.destination.peripheral = 1;
	bundle.add.destination.number = 3;
	refuse_bundle(&bundle, EINVAL);
	bundle.add.destination.number = 19;
	refuse_bundle(&bundle, EINVAL);

	/* An addressed signal beside a predicate. */
	bundle.add.destination.peripheral = 0;
	bundle.add.destination.number = 8;
	bundle.add.predicate = 1;
	bundle.signal = BCM2711_QPU_SIGNAL_UNIFORM_REGISTER;
	refuse_bundle(&bundle, ENOTSUP);

	/* Both ALUs pushing flags. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_MOVE;
	bundle.add.push_flags = 1;
	bundle.multiply.operation = BCM2711_QPU_MOVE;
	bundle.multiply.push_flags = 1;
	refuse_bundle(&bundle, ENOTSUP);

	/* A switch beside an unlisted signal. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.thread_switch = 1;
	bundle.signal = BCM2711_QPU_SIGNAL_TILE;
	refuse_bundle(&bundle, ENOTSUP);

	/* An operation in the other ALU's slot. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_FLOAT_MULTIPLY;
	refuse_bundle(&bundle, EINVAL);
	memset(&bundle, 0, sizeof(bundle));
	bundle.multiply.operation = BCM2711_QPU_FLOAT_ADD;
	refuse_bundle(&bundle, EINVAL);

	/* A float maximum of one register with itself has no native order. */
	memset(&bundle, 0, sizeof(bundle));
	bundle.add.operation = BCM2711_QPU_FLOAT_MAXIMUM;
	bundle.add.left.number = 4;
	bundle.add.right.number = 4;
	refuse_bundle(&bundle, ENOTSUP);

	/* Invalid registers and unknown operations keep the caller's word. */
	memset(&instruction, 0, sizeof(instruction));
	instruction.operation = BCM2711_QPU_MOVE;
	instruction.left.number = 64;
	word = UINT64_C(0x1122334455667788);
	error = bcm2711_qpu_encode(&instruction, &word);
	assert(error == EINVAL && word == UINT64_C(0x1122334455667788));
	instruction.left.number = 0;
	instruction.operation = (enum bcm2711_qpu_operation)UINT32_MAX;
	error = bcm2711_qpu_encode(&instruction, &word);
	assert(error == ENOTSUP && word == UINT64_C(0x1122334455667788));

	/* Succeeded: every refusal is reported. */
	puts("qpu71-format-host-test refusals: PASS");
}

/* Checks that one word is refused with the expected error and leaves the caller's word alone. */
static void
refuse_bundle(
	const struct bcm2711_qpu_bundle *bundle,
	int expected)
{
	uint64_t word;
	int error;

	/* The refusal keeps the old bits. */
	word = UINT64_C(0x1122334455667788);
	error = bcm2711_qpu_encode_bundle(bundle, &word);
	if (error != expected) {
		fprintf(stderr, "bundle: expected %d, got %d\n", expected, error);
		assert(0);
	}
	assert(word == UINT64_C(0x1122334455667788));
}
