/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 EU instruction encoder (see eu.h).
 *
 * Each instruction is four little-endian 32-bit words.  Field bit positions,
 * hardware opcode values, register types and the descriptor layout of SEND
 * are transcribed into intel/eu-encoding-gen12.h from Mesa (MIT); the
 * placement logic here is new.  Every emitter is checked bit for bit against
 * Mesa's assembler and disassembler (gentool) by
 * plan/ws031/tests/run-vk-gentool-test.sh.
 *
 * SOFTWARE SCOREBOARD.  Gen12 hardware does not track register dependencies
 * between instructions; the program says them in the SWSB byte
 * (brw_lower_scoreboard.cpp).  This encoder says the one thing that is always
 * true of its output: every instruction depends on the one before it.
 *
 *   - an in-order instruction (MOV, ADD, MUL, SEL, CMP, AND, OR, XOR, NOT,
 *     SHL, SHR, ASR, RNDD, RNDZ, RNDE, FRC, WHILE, IF, ENDIF) waits for the previous in-order
 *     instruction (@1); a flag a CMP writes is read only by a later in-order
 *     instruction, so the same wait covers it.  The wait counts back over
 *     the instructions as they ran, so the first instruction of a loop,
 *     reached again from the WHILE, still waits for the one before it;
 *   - an out-of-order instruction (MATH, SEND) waits the same way, names
 *     itself with token 0 and is followed by a sync.nop that waits until it
 *     has written its destination (or, having none, has read its sources), so
 *     nothing overlaps it and token 0 is free again;
 *   - the SEND that ends the thread waits for the previous instruction and
 *     needs nothing after it.
 *
 * drv_i915_eu_schedule() then loosens the out-of-order part (ws075-p022):
 * each out-of-order instruction takes a token of its own (sixteen in turn),
 * and its sync.nop moves from right after it to right before the first
 * instruction that reads or writes its destination, or writes its sources.
 * The in-order chain (@1) stays: every in-order instruction still waits for
 * the one before it.
 */

#include "eu.h"
#include <kern/kcrt.h>

#include <kern/kmem.h>

#include <uapi/errno.h>

#include "../intel/eu-encoding-gen12.h"

/*
 * Marks a parameter a function deliberately leaves unread.
 *
 * The compiler includes no driver header, so it spells the marker itself
 * unless the including translation unit already has it.
 */
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(parameter) ((void)(parameter))
#endif

/*
 * The one scoreboard token this encoder uses.
 *
 * Nothing else is in flight when it is set (see the scoreboard note above).
 */
#define I915_EU_TOKEN			0U

/* The scoreboard class of an instruction, as i915_eu_common() takes it. */
#define I915_EU_OUT_OF_ORDER		0
#define I915_EU_IN_ORDER		1
#define I915_EU_END_OF_THREAD		2

/* The token-set SWSB byte of an out-of-order instruction with nothing to wait for. */
#define I915_EU_SWSB_TOKEN_SET		0x40U

/* How many scoreboard tokens (SBIDs) Gen12 has. */
#define I915_EU_TOKENS			16U

/* The bytes of one general register. */
#define I915_EU_GRF_BYTES		32U

/*
 * The registers an instruction touches, as drv_i915_eu_schedule() reads
 * them back from its encoding: at most one written run and three read runs
 * (a SEND's two payload runs, or an ALU's two sources).
 */
#define I915_EU_MAX_READS		3U

/* The first buffer capacity, in words; the buffer doubles from there. */
#define I915_EU_FIRST_CAPACITY		64U

/* The largest shared-function identifier the SEND encoding has room for. */
#define I915_EU_SFID_MAX		15U

/* The extended-descriptor bits that are not encodable (they held the SFID once). */
#define I915_EU_EX_DESC_LOW_MASK	0x3fU

/*
 * Which channels an instruction runs on, as i915_eu_scope() takes it: the
 * eight channels of the dispatch that the execution mask leaves enabled,
 * all eight regardless of the mask (NoMask), or the first one regardless of
 * the mask (a SIMD1 NoMask instruction, which writes one dword).
 */
#define I915_EU_SCOPE_MASKED		0
#define I915_EU_SCOPE_ALL		1
#define I915_EU_SCOPE_SCALAR		2

/*
 * Sixteen channels regardless of the mask (a SIMD16 NoMask instruction in a
 * SIMD8 kernel), for an operation on sixteen 16-bit words.
 */
#define I915_EU_SCOPE_SIXTEEN		3

/*
 * Four channels regardless of the mask (a SIMD4 NoMask instruction), for
 * an operation on one quad of pixels.
 */
#define I915_EU_SCOPE_FOUR		4

/*
 * A run of general registers an instruction reads or writes: `count`
 * registers from `first`; a count of zero touches no general register.
 */
struct i915_eu_run {
	uint32_t first;
	uint32_t count;
};

/*
 * One scoreboard token of drv_i915_eu_schedule(): the out-of-order
 * instruction that holds it at a point of the program, and the registers it
 * is still to write (`write`) and to read (`reads`).
 *
 * A token is busy from its instruction until a sync.nop waits for its
 * destination; a wait for its sources only empties `reads`.  `age` orders
 * the busy tokens, oldest first, for reuse when all are busy.
 */
struct i915_eu_token {
	int busy;
	uint32_t age;
	struct i915_eu_run write;
	struct i915_eu_run reads[I915_EU_MAX_READS];
};

/*
 * The state of one run of drv_i915_eu_schedule(): the program being
 * rebuilt, the marks it reads, where each old instruction went and the
 * tokens in flight.
 *
 * It lives on the caller's stack for the run; the arrays have one entry per
 * old instruction, and `start` and `place` one more for the end.
 */
struct i915_eu_schedule {
	/* The program as the encoder made it, how many instructions it has, and the rebuilt one. */
	struct i915_eu_buf *buffer;
	uint32_t count;
	struct i915_eu_buf out;

	/* Nonzero for an encoder's sync.nop that is dropped, and for a loop's first instruction. */
	uint8_t *dropped;
	uint8_t *loop_top;

	/* Where each old instruction's waits start in the new program, and where it is itself. */
	uint32_t *start;
	uint32_t *place;

	/* The tokens, the next one to try, and the order they were taken in. */
	struct i915_eu_token tokens[I915_EU_TOKENS];
	uint32_t next_token;
	uint32_t clock;
};

/* The hardware conditional modifier of each enum i915_eu_cond, in its order. */
static const uint32_t i915_eu_cond_bits[I915_EU_COND_COUNT] = {
	EU_COND_Z,
	EU_COND_NZ,
	EU_COND_G,
	EU_COND_GE,
	EU_COND_L,
	EU_COND_LE,
};

static struct i915_eu_reg i915_eu_grf_typed(uint32_t nr, uint32_t type);
static uint32_t *i915_eu_reserve(struct i915_eu_buf *buffer);
static void i915_eu_common(struct i915_eu_buf *buffer, uint32_t *inst, uint32_t opcode, int order);
static void i915_eu_flag(uint32_t *inst, enum i915_eu_flag flag);
static void i915_eu_alu2_common(struct i915_eu_buf *buffer, int predicated, enum i915_eu_flag flag, int scope, enum i915_eu_alu op, struct i915_eu_reg dst, struct i915_eu_reg src0, struct i915_eu_reg src1);
static void i915_eu_send_common(struct i915_eu_buf *buffer, int predicated, enum i915_eu_flag flag, int scope, struct i915_eu_reg dst, struct i915_eu_reg src0, struct i915_eu_reg src1, uint32_t sfid, uint32_t descriptor, uint32_t ex_descriptor, int conditional, int end_of_thread);
static void i915_eu_scope(uint32_t *inst, int scope);
static void i915_eu_sync(struct i915_eu_buf *buffer, uint32_t swsb);
static void i915_eu_dst(uint32_t *inst, struct i915_eu_reg reg);
static void i915_eu_src0(uint32_t *inst, struct i915_eu_reg reg);
static void i915_eu_src1(uint32_t *inst, struct i915_eu_reg reg);
static uint32_t i915_eu_file_bit(struct i915_eu_reg reg);
static void i915_eu_set(uint32_t *inst, unsigned high, unsigned low, uint32_t value);
static void i915_eu_bit(uint32_t *inst, unsigned position, uint32_t value);
static uint32_t i915_eu_get(const uint32_t *inst, unsigned high, unsigned low);
static void i915_eu_touches(const uint32_t *inst, struct i915_eu_run *write, struct i915_eu_run reads[I915_EU_MAX_READS]);
static void i915_eu_operand_run(const uint32_t *inst, int source, struct i915_eu_run *run);
static int i915_eu_runs_meet(const struct i915_eu_run *first, const struct i915_eu_run *second);
static int i915_eu_is_out_of_order(const uint32_t *inst);
static int i915_eu_is_end_of_thread(const uint32_t *inst);
static void i915_eu_wait(struct i915_eu_buf *out, struct i915_eu_token *token, uint32_t id, int destination);
static int i915_eu_schedule_alloc(struct i915_eu_schedule *pass);
static int i915_eu_schedule_mark(struct i915_eu_schedule *pass);
static int i915_eu_schedule_rebuild(struct i915_eu_schedule *pass);
static void i915_eu_schedule_waits(struct i915_eu_schedule *pass, const struct i915_eu_run *write, const struct i915_eu_run reads[I915_EU_MAX_READS], int flush);
static uint32_t i915_eu_schedule_take(struct i915_eu_schedule *pass, const struct i915_eu_run *write, const struct i915_eu_run reads[I915_EU_MAX_READS]);
static int i915_eu_schedule_jumps(struct i915_eu_schedule *pass);
static int i915_eu_schedule_move(struct i915_eu_schedule *pass, uint32_t index, uint32_t *inst, unsigned high, unsigned low);
static int i915_eu_is_encoder_wait(const struct i915_eu_buf *buffer, uint32_t index);
static int i915_eu_jump_target(uint32_t index, uint32_t jump, uint32_t count, uint32_t *target);

/*
 * Prepares an empty instruction buffer.
 */
void
drv_i915_eu_init(
	struct i915_eu_buf *buffer)
{
	/* Starts with no storage, no error and no instruction to wait for. */
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
	buffer->error = 0;
	buffer->in_order = 0U;
}

/*
 * Releases the storage of an instruction buffer.
 *
 * The error and the scoreboard count are left as they are.
 */
void
drv_i915_eu_free(
	struct i915_eu_buf *buffer)
{
	/* Releases the instruction words, if any were ever reserved. */
	if (buffer->words != NULL)
		kern_free(buffer->words);

	/* Leaves the buffer empty. */
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
}

/*
 * Returns the encoded instruction words and their length in bytes.
 */
const uint32_t *
drv_i915_eu_data(
	const struct i915_eu_buf *buffer,
	size_t *bytes)
{
	/* Reports the length alongside the words. */
	*bytes = buffer->count * sizeof(uint32_t);

	/* Succeeded: the words stay owned by the buffer. */
	return buffer->words;
}

/*
 * Names a general register operand of 32-bit float type.
 */
struct i915_eu_reg
drv_i915_eu_grf(
	uint32_t nr)
{
	struct i915_eu_reg reg;

	/* Builds eight float channels of the register. */
	reg = i915_eu_grf_typed(nr, EU_TYPE_F);

	/* Succeeded: the operand is a plain SIMD8 float register. */
	return reg;
}

/*
 * Names a general register as eight unsigned 32-bit words.
 *
 * This is how URB handles, and anything else copied bit for bit, are read.
 */
struct i915_eu_reg
drv_i915_eu_grf_ud(
	uint32_t nr)
{
	struct i915_eu_reg reg;

	/* Builds eight unsigned-word channels of the register. */
	reg = i915_eu_grf_typed(nr, EU_TYPE_UD);

	/* Succeeded: the operand is a plain SIMD8 word register. */
	return reg;
}

/*
 * Names a general register as eight signed 32-bit words.
 *
 * This is how a Boolean (all ones for true, zero for false) is read.
 */
struct i915_eu_reg
drv_i915_eu_grf_d(
	uint32_t nr)
{
	struct i915_eu_reg reg;

	/* Builds eight signed-word channels of the register. */
	reg = i915_eu_grf_typed(nr, EU_TYPE_D);

	/* Succeeded: the operand is a plain SIMD8 signed-word register. */
	return reg;
}

/*
 * Names the low (or, with `high`, the high) 16 bits of each 32-bit channel
 * of a general register, as eight unsigned words.
 *
 * The region is <16;8,2>:uw from byte 0 or 2: how Mesa reads the halves of
 * the second source when it lowers a 32-bit integer multiply to two 32 x
 * 16-bit ones (brw_lower_integer_multiplication.cpp), which Tiger Lake needs
 * because it has no 32 x 32-bit multiply.
 */
struct i915_eu_reg
drv_i915_eu_grf_uw_half(
	uint32_t nr,
	int high)
{
	struct i915_eu_reg reg;

	/* Names the register as unsigned words. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.type = EU_TYPE_UW;

	/* The high half starts two bytes into each channel. */
	reg.subnr = 0U;
	if (high != 0)
		reg.subnr = 2U;

	/* Every second word: eight of them across the register. */
	reg.vstride = EU_VSTRIDE_16;
	reg.width = EU_WIDTH_8;
	reg.hstride = EU_HSTRIDE_2;

	/* Succeeded: the operand reads one half of each channel. */
	return reg;
}

/*
 * Names a general register operand with an explicit region: the element of
 * `type` at byte `subnr`, read with <vstride;width,hstride> (each in its
 * EU_VSTRIDE_ / EU_WIDTH_ / EU_HSTRIDE_ encoding).
 *
 * The derivatives read a value's pixels in quads this way, and the pixel
 * position reads the subspan coordinates of the thread payload.
 */
struct i915_eu_reg
drv_i915_eu_grf_region(
	uint32_t nr,
	uint32_t subnr,
	uint32_t type,
	uint32_t vstride,
	uint32_t width,
	uint32_t hstride)
{
	struct i915_eu_reg reg;

	/* Names the register, the element and the type it is read as. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.subnr = subnr;
	reg.type = type;

	/* Reads it with the region given. */
	reg.vstride = vstride;
	reg.width = width;
	reg.hstride = hstride;

	/* Succeeded: the operand reads the region. */
	return reg;
}

/*
 * Names one float of a general register, replicated to every channel.
 *
 * The float sits at byte `subnr` and is read with region <0;1,0>: how a value
 * that is the same for the whole dispatch, such as a push constant, is read.
 */
struct i915_eu_reg
drv_i915_eu_grf_scalar(
	uint32_t nr,
	uint32_t subnr)
{
	struct i915_eu_reg reg;

	/* Names the register and the float inside it. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.subnr = subnr;
	reg.type = EU_TYPE_F;

	/* A zero vertical and horizontal stride replicate the one float. */
	reg.vstride = EU_VSTRIDE_0;
	reg.width = EU_WIDTH_1;
	reg.hstride = EU_HSTRIDE_0;

	/* Succeeded: the operand reads one float to every channel. */
	return reg;
}

/*
 * Returns the same operand read negated.
 *
 * It is a source modifier, not valid for a destination or an immediate.
 */
struct i915_eu_reg
drv_i915_eu_negate(
	struct i915_eu_reg reg)
{
	/* Negating twice reads the value unchanged again. */
	reg.negate = reg.negate ^ 1U;

	/* Succeeded: the operand now carries the negate modifier. */
	return reg;
}

/*
 * Returns the same operand read as its absolute value.
 *
 * It is a source modifier of a float operand, applied before any negation.
 */
struct i915_eu_reg
drv_i915_eu_abs(
	struct i915_eu_reg reg)
{
	/* The absolute value of an absolute value is the same. */
	reg.absolute = 1U;

	/* Succeeded: the operand now carries the absolute modifier. */
	return reg;
}

/*
 * Names a 32-bit float immediate operand from its bits.
 */
struct i915_eu_reg
drv_i915_eu_imm_f(
	uint32_t bits)
{
	struct i915_eu_reg reg;

	/* Carries the float bits unchanged; no floating point is involved. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_F;
	reg.immediate = bits;

	/* Succeeded: the operand is a float immediate. */
	return reg;
}

/*
 * Names a 32-bit signed integer immediate operand.
 */
struct i915_eu_reg
drv_i915_eu_imm_d(
	uint32_t value)
{
	struct i915_eu_reg reg;

	/* Carries the integer bits unchanged. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_D;
	reg.immediate = value;

	/* Succeeded: the operand is an integer immediate. */
	return reg;
}

/*
 * Names a 32-bit unsigned integer immediate operand.
 */
struct i915_eu_reg
drv_i915_eu_imm_ud(
	uint32_t value)
{
	struct i915_eu_reg reg;

	/* Carries the integer bits unchanged. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_UD;
	reg.immediate = value;

	/* Succeeded: the operand is an unsigned integer immediate. */
	return reg;
}

/*
 * Names an immediate of eight signed 4-bit integers (V): element n in bits
 * 4 n + 3 .. 4 n.
 *
 * Mesa adds 0x11001010 to the replicated subspan coordinates of a pixel
 * thread this way to make each pixel's x and y (brw_compile_fs.cpp).
 */
struct i915_eu_reg
drv_i915_eu_imm_v(
	uint32_t value)
{
	struct i915_eu_reg reg;

	/* Carries the eight nibbles unchanged. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_V;
	reg.immediate = value;

	/* Succeeded: the operand is a vector immediate. */
	return reg;
}

/*
 * Names the null register.
 *
 * As a float operand it carries the ordinary SIMD8 region.
 */
struct i915_eu_reg
drv_i915_eu_null(
	void)
{
	struct i915_eu_reg reg;

	/* The null register is the architecture file's register zero. */
	reg = i915_eu_grf_typed(0U, EU_TYPE_F);
	reg.file = EU_FILE_ARF;

	/* Succeeded: the operand discards a result or supplies nothing. */
	return reg;
}

/*
 * Encodes a move.
 *
 * A destination of another type than float makes it a move of that type.
 */
void
drv_i915_eu_mov(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order move and its two operands. */
	i915_eu_common(buffer, inst, EU_OP_MOV, I915_EU_IN_ORDER);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src);
}

/*
 * Encodes a two-source arithmetic, logic or shift instruction.
 *
 * Add, multiply, and, or, exclusive or and the three shifts are encoded;
 * any other operation, and an immediate first source, poison the buffer.
 */
void
drv_i915_eu_alu2(
	struct i915_eu_buf *buffer,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	/* Encodes the instruction for every channel of the mask. */
	i915_eu_alu2_common(buffer, 0, I915_EU_FLAG_F0_0, I915_EU_SCOPE_MASKED, op, dst, src0, src1);
}

/*
 * Encodes a two-source instruction for the channels whose bit of `flag` is
 * set.
 *
 * The operands are those of drv_i915_eu_alu2(); every other channel keeps
 * its destination.  An integer division steps this way: a channel whose
 * remainder reached the divisor subtracts it and sets its quotient bit.
 */
void
drv_i915_eu_alu2_masked(
	struct i915_eu_buf *buffer,
	enum i915_eu_flag flag,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	/* A flag outside the two flag registers is refused. */
	if (flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return;
	}

	/* Encodes the instruction predicated on the flag. */
	i915_eu_alu2_common(buffer, 1, flag, I915_EU_SCOPE_MASKED, op, dst, src0, src1);
}

/*
 * Encodes a two-source instruction on the first channel alone, regardless
 * of the execution mask: a SIMD1 NoMask instruction that writes the one
 * dword the destination's subregister names.
 *
 * The operands are those of drv_i915_eu_alu2(), a source normally read as a
 * scalar region.  Mesa builds the header of a scratch message this way: the
 * two AND of generate_scratch_header() (brw_generator.cpp) copy the scratch
 * space size and base out of r0.
 */
void
drv_i915_eu_alu2_scalar(
	struct i915_eu_buf *buffer,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	/* Encodes the instruction for the first channel, outside the mask. */
	i915_eu_alu2_common(buffer, 0, I915_EU_FLAG_F0_0, I915_EU_SCOPE_SCALAR, op, dst, src0, src1);
}

/*
 * Encodes a two-source instruction on sixteen channels regardless of the
 * execution mask: a SIMD16 NoMask instruction in a SIMD8 kernel.
 *
 * The operands are those of drv_i915_eu_alu2(), read and written as sixteen
 * 16-bit words.  Mesa computes the pixel positions of a SIMD8 pixel thread
 * this way: one add of sixteen words, the x and y of both subspans
 * (brw_compile_fs.cpp).
 */
void
drv_i915_eu_alu2_sixteen(
	struct i915_eu_buf *buffer,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	/* Encodes the instruction for sixteen channels, outside the mask. */
	i915_eu_alu2_common(buffer, 0, I915_EU_FLAG_F0_0, I915_EU_SCOPE_SIXTEEN, op, dst, src0, src1);
}

/*
 * Encodes a two-source instruction on four channels regardless of the
 * execution mask: a SIMD4 NoMask instruction, the destination's first
 * dword naming the quad.
 *
 * Mesa computes the fine y derivative this way on Gen11+, one add per quad
 * (generate_ddy(), brw_generator.cpp).
 */
void
drv_i915_eu_alu2_four(
	struct i915_eu_buf *buffer,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	/* Encodes the instruction for four channels, outside the mask. */
	i915_eu_alu2_common(buffer, 0, I915_EU_FLAG_F0_0, I915_EU_SCOPE_FOUR, op, dst, src0, src1);
}

/*
 * Encodes a move on all eight channels regardless of the execution mask.
 *
 * Mesa clears a scratch message header this way before filling it (the
 * SIMD8 NoMask MOV of generate_scratch_header(), brw_generator.cpp): a
 * channel the dispatch left disabled must not keep junk in the header.
 */
void
drv_i915_eu_mov_all(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order move and its two operands, outside the mask. */
	i915_eu_common(buffer, inst, EU_OP_MOV, I915_EU_IN_ORDER);
	i915_eu_scope(inst, I915_EU_SCOPE_ALL);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src);
}

/*
 * Encodes a move of one dword on the first channel alone, regardless of the
 * execution mask (a SIMD1 NoMask MOV).
 *
 * Mesa writes the offset of a scratch message into dword 2 of its header
 * this way (build_legacy_scratch_header(), brw_reg_allocate.cpp).
 */
void
drv_i915_eu_mov_scalar(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order move of the one dword, outside the mask. */
	i915_eu_common(buffer, inst, EU_OP_MOV, I915_EU_IN_ORDER);
	i915_eu_scope(inst, I915_EU_SCOPE_SCALAR);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src);
}

/*
 * Encodes a one-source operation other than a move: not, round down, round
 * toward zero, round to even or fraction.
 *
 * Mesa lowers ffloor to RNDD, ftrunc to RNDZ, fround_even to RNDE, ffract
 * to FRC and inot to NOT (brw_fs_nir.cpp).  An operation outside the enum
 * poisons the buffer.
 */
void
drv_i915_eu_alu1(
	struct i915_eu_buf *buffer,
	enum i915_eu_unary op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src)
{
	uint32_t *inst;
	uint32_t opcode;

	/* Picks the hardware opcode of the operation. */
	if (op == I915_EU_NOT) {
		opcode = EU_OP_NOT;
	} else if (op == I915_EU_RNDD) {
		opcode = EU_OP_RNDD;
	} else if (op == I915_EU_FRC) {
		opcode = EU_OP_FRC;
	} else if (op == I915_EU_RNDZ) {
		opcode = EU_OP_RNDZ;
	} else if (op == I915_EU_RNDE) {
		opcode = EU_OP_RNDE;
	} else {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order instruction and its two operands. */
	i915_eu_common(buffer, inst, opcode, I915_EU_IN_ORDER);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src);
}

/*
 * Encodes a comparison.
 *
 * Each channel's bit of `flag` becomes the result of `src0 cond src1`, and
 * a register destination receives all ones for true and zero for false.
 * With `predicated`, only the channels whose bit of `flag` is already set
 * are compared, so a cleared bit stays cleared: how a discard keeps the
 * pixels it has already discarded (Mesa's demote, brw_fs_nir.cpp).
 */
void
drv_i915_eu_cmp(
	struct i915_eu_buf *buffer,
	enum i915_eu_cond cond,
	enum i915_eu_flag flag,
	int predicated,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;

	/* A test outside the enum, or a flag outside the two flag registers, is refused. */
	if (cond >= I915_EU_COND_COUNT || flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return;
	}

	/* The hardware has one immediate slot, and it belongs to the second source. */
	if (src0.file == EU_FILE_IMM) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order comparison, the flag it writes and the test. */
	i915_eu_common(buffer, inst, EU_OP_CMP, I915_EU_IN_ORDER);
	i915_eu_flag(inst, flag);
	i915_eu_set(inst, EU_COND_MODIFIER_HI, EU_COND_MODIFIER_LO, i915_eu_cond_bits[cond]);

	/* A predicated comparison reads the same flag it writes. */
	if (predicated != 0)
		i915_eu_set(inst, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO, EU_PREDICATE_NORMAL);

	/* Encodes the operands. */
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);
}

/*
 * Encodes a minimum or a maximum: SEL with a conditional modifier.
 *
 * `cond` LT keeps the smaller source and GE the larger one, as Mesa's
 * emit_minmax() lowers fmin and fmax (brw_builder.h); no flag is involved.
 */
void
drv_i915_eu_minmax(
	struct i915_eu_buf *buffer,
	enum i915_eu_cond cond,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;

	/* Only the minimum and the maximum form are encoded. */
	if (cond != I915_EU_COND_LT && cond != I915_EU_COND_GE) {
		buffer->error = 1;
		return;
	}

	/* The hardware has one immediate slot, and it belongs to the second source. */
	if (src0.file == EU_FILE_IMM) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order selection with its test and its operands. */
	i915_eu_common(buffer, inst, EU_OP_SEL, I915_EU_IN_ORDER);
	i915_eu_set(inst, EU_COND_MODIFIER_HI, EU_COND_MODIFIER_LO, i915_eu_cond_bits[cond]);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);
}

/*
 * Encodes a per-channel selection: SEL predicated on a flag.
 *
 * A channel whose bit of `flag` is set takes `src0`, any other `src1`.
 * With integer operands the selected bits are copied unchanged.
 */
void
drv_i915_eu_select(
	struct i915_eu_buf *buffer,
	enum i915_eu_flag flag,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;

	/* A flag outside the two flag registers is refused. */
	if (flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return;
	}

	/* The hardware has one immediate slot, and it belongs to the second source. */
	if (src0.file == EU_FILE_IMM) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order selection predicated on the flag, and its operands. */
	i915_eu_common(buffer, inst, EU_OP_SEL, I915_EU_IN_ORDER);
	i915_eu_flag(inst, flag);
	i915_eu_set(inst, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO, EU_PREDICATE_NORMAL);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);
}

/*
 * Loads a flag subregister from the 16-bit word at byte `subnr` of general
 * register `nr`.
 *
 * It is a SIMD1 move outside the channel mask, the way Mesa loads the
 * dispatched pixels of a fragment thread into the discard flag
 * (brw_compile_fs.cpp).
 */
void
drv_i915_eu_flag_load(
	struct i915_eu_buf *buffer,
	enum i915_eu_flag flag,
	uint32_t nr,
	uint32_t subnr)
{
	struct i915_eu_reg dst;
	struct i915_eu_reg src;
	uint32_t *inst;

	/* A flag outside the two flag registers is refused. */
	if (flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return;
	}

	/* The flag register and the byte of its subregister, as a 16-bit destination. */
	kern_memset(&dst, 0, sizeof(dst));
	dst.file = EU_FILE_ARF;
	dst.nr = EU_ARF_FLAG + (uint32_t)flag / 2U;
	dst.subnr = ((uint32_t)flag % 2U) * 2U;
	dst.type = EU_TYPE_UW;

	/* The word of the general register, read once for the one channel. */
	src = drv_i915_eu_grf_scalar(nr, subnr);
	src.type = EU_TYPE_UW;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order move, then narrows it to one channel outside the mask. */
	i915_eu_common(buffer, inst, EU_OP_MOV, I915_EU_IN_ORDER);
	i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_1);
	i915_eu_bit(inst, EU_NO_MASK_BIT, 1U);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src);
}

/*
 * Refuses a multiply-add: the three-source operand layout is not encoded.
 */
void
drv_i915_eu_mad(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	struct i915_eu_reg src2)
{
	UNUSED_PARAMETER(dst);
	UNUSED_PARAMETER(src0);
	UNUSED_PARAMETER(src1);
	UNUSED_PARAMETER(src2);

	/*
	 * XXX: unimplemented.  An instruction without its operands is a different
	 * instruction, so the buffer is poisoned: no caller can ship it by
	 * accident.  The compiler lowers a*b+c to MUL, ADD.
	 */
	buffer->error = 1;
}

/*
 * Encodes a math function: a one-operand float function, whose second
 * source is the null register, or the quotient or the remainder of an
 * integer division of src0 by src1.
 *
 * Math is out of order, so a sync.nop on its destination follows it (see the
 * scoreboard note above).  The integer division reads its operands as their
 * type says (signed D or unsigned UD) and takes no source modifier
 * (gfx6_math(), brw_eu_emit.c); a negated or absolute integer source poisons
 * the buffer.
 */
void
drv_i915_eu_math(
	struct i915_eu_buf *buffer,
	enum i915_eu_math func,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;
	uint32_t selector;

	/* The function selector maps to the Gen math sub-opcode. */
	if (func == I915_EU_MATH_INV) {
		selector = EU_MATH_INV;
	} else if (func == I915_EU_MATH_RSQ) {
		selector = EU_MATH_RSQ;
	} else if (func == I915_EU_MATH_SQRT) {
		selector = EU_MATH_SQRT;
	} else if (func == I915_EU_MATH_SIN) {
		selector = EU_MATH_SIN;
	} else if (func == I915_EU_MATH_COS) {
		selector = EU_MATH_COS;
	} else if (func == I915_EU_MATH_LOG) {
		selector = EU_MATH_LOG;
	} else if (func == I915_EU_MATH_EXP) {
		selector = EU_MATH_EXP;
	} else if (func == I915_EU_MATH_INT_QUOTIENT) {
		selector = EU_MATH_INT_DIV_QUOTIENT;
	} else if (func == I915_EU_MATH_INT_REMAINDER) {
		selector = EU_MATH_INT_DIV_REMAINDER;
	} else {
		buffer->error = 1;
		return;
	}

	/* The integer division takes no source modifier. */
	if (func == I915_EU_MATH_INT_QUOTIENT || func == I915_EU_MATH_INT_REMAINDER) {
		if (src0.negate != 0U ||
		    src0.absolute != 0U ||
		    src1.negate != 0U ||
		    src1.absolute != 0U) {
			buffer->error = 1;
			return;
		}
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an out-of-order math instruction, its function and its operands. */
	i915_eu_common(buffer, inst, EU_OP_MATH, I915_EU_OUT_OF_ORDER);
	i915_eu_set(inst, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO, selector);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);

	/* Waits until the math has written its destination. */
	i915_eu_sync(buffer, EU_SWSB_SYNC_DST(I915_EU_TOKEN));
}

/*
 * Encodes a message to a shared function.
 *
 * `src0` and, for a split payload, `src1` are the first registers of the two
 * payload runs whose lengths the descriptors carry (desc: mlen, rlen, the
 * function's control; ex_desc: the length of the second run).
 * `conditional` selects SENDC, the form a render-target write takes.
 * `end_of_thread`: the message retires the thread (its payload must then sit
 * in r112..r127, which is the caller's business).
 */
void
drv_i915_eu_send(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor,
	int conditional,
	int end_of_thread)
{
	/* Encodes the message for every channel of the mask. */
	i915_eu_send_common(buffer,
			    0,
			    I915_EU_FLAG_F0_0,
			    I915_EU_SCOPE_MASKED,
			    dst,
			    src0,
			    src1,
			    sfid,
			    descriptor,
			    ex_descriptor,
			    conditional,
			    end_of_thread);
}

/*
 * Encodes a message to a shared function for the channels whose bit of
 * `flag` is set.
 *
 * The operands are those of drv_i915_eu_send().  A render-target write that
 * ends the thread is sent this way when the shader discards: the pixels the
 * flag no longer holds are not written, and the thread still ends when no
 * pixel is left (Mesa predicates the FB write on the discard flag,
 * brw_compile_fs.cpp).
 */
void
drv_i915_eu_send_masked(
	struct i915_eu_buf *buffer,
	enum i915_eu_flag flag,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor,
	int conditional,
	int end_of_thread)
{
	/* A flag outside the two flag registers is refused. */
	if (flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return;
	}

	/* Encodes the message predicated on the flag. */
	i915_eu_send_common(buffer,
			    1,
			    flag,
			    I915_EU_SCOPE_MASKED,
			    dst,
			    src0,
			    src1,
			    sfid,
			    descriptor,
			    ex_descriptor,
			    conditional,
			    end_of_thread);
}

/*
 * Encodes a message to a shared function on all eight channels regardless
 * of the execution mask.
 *
 * The operands are those of drv_i915_eu_send().  Mesa reads a spilled
 * register back from scratch memory this way (emit_unspill() under
 * exec_all(), brw_reg_allocate.cpp): the fill is a temporary of the
 * instruction that needs it, so every channel of it is read, whichever are
 * enabled.
 */
void
drv_i915_eu_send_all(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor)
{
	/* Encodes the message for all eight channels, outside the mask. */
	i915_eu_send_common(buffer,
			    0,
			    I915_EU_FLAG_F0_0,
			    I915_EU_SCOPE_ALL,
			    dst,
			    src0,
			    src1,
			    sfid,
			    descriptor,
			    ex_descriptor,
			    0,
			    0);
}

/*
 * Encodes a no-op.
 */
void
drv_i915_eu_nop(
	struct i915_eu_buf *buffer)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Only the opcode is set; the no-op carries no scoreboard byte. */
	i915_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, EU_OP_NOP);
}

/*
 * Returns the index of the instruction encoded next.
 *
 * A loop remembers it at its start, for the WHILE at its end to jump back
 * to.
 */
uint32_t
drv_i915_eu_position(
	const struct i915_eu_buf *buffer)
{
	/* Four words to an instruction. */
	return (uint32_t)(buffer->count / GEN12_EU_DWORDS);
}

/*
 * Encodes the WHILE that ends a loop: the channels whose bit of `flag` is
 * set jump back to instruction `target`, the others wait after the WHILE
 * until the loop ends.
 *
 * The form is Mesa's brw_WHILE on Gen12: a null signed-integer
 * destination, SIMD8, the jump in bytes from the WHILE to the target in the
 * JIP.  A target at or after the WHILE, or a poisoned buffer, encodes
 * nothing.
 */
void
drv_i915_eu_while(
	struct i915_eu_buf *buffer,
	enum i915_eu_flag flag,
	uint32_t target)
{
	struct i915_eu_reg null;
	uint32_t *inst;
	uint32_t here;
	int32_t jump;

	/* A flag outside the two flag registers is refused. */
	if (flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return;
	}

	/* A loop jumps back to an instruction before its end. */
	here = drv_i915_eu_position(buffer);
	if (target >= here) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* The jump, in bytes, back from the WHILE to the first instruction of the loop. */
	jump = -(int32_t)((here - target) * GEN12_EU_DWORDS * 4U);

	/* Encodes an in-order WHILE predicated on the flag, its null destination and its jump. */
	i915_eu_common(buffer, inst, EU_OP_WHILE, I915_EU_IN_ORDER);
	i915_eu_flag(inst, flag);
	i915_eu_set(inst, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO, EU_PREDICATE_NORMAL);
	null = drv_i915_eu_null();
	null.type = EU_TYPE_D;
	i915_eu_dst(inst, null);
	i915_eu_bit(inst, EU_SRC0_IS_IMM_BIT, 1U);
	i915_eu_set(inst, EU_JIP_HI, EU_JIP_LO, (uint32_t)jump);
}

/*
 * Encodes an IF on `flag`: the channels whose bit of the flag is clear stop
 * running until the matching ENDIF, and when no channel is left the thread
 * jumps to the ENDIF.  Returns the IF's position; its targets are written by
 * drv_i915_eu_patch_if() once the ENDIF is placed.
 *
 * The form is Mesa's brw_IF on Gen12: a null signed-integer destination,
 * SIMD8 under the execution mask, the JIP and the UIP immediate.
 */
uint32_t
drv_i915_eu_if(
	struct i915_eu_buf *buffer,
	enum i915_eu_flag flag)
{
	struct i915_eu_reg null;
	uint32_t *inst;
	uint32_t here;

	/* The IF's position, which the patch names it by. */
	here = drv_i915_eu_position(buffer);

	/* A flag outside the two flag registers is refused. */
	if (flag >= I915_EU_FLAG_COUNT) {
		buffer->error = 1;
		return here;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return here;

	/* Encodes an in-order IF predicated on the flag, with a null destination. */
	i915_eu_common(buffer, inst, EU_OP_IF, I915_EU_IN_ORDER);
	i915_eu_flag(inst, flag);
	i915_eu_set(inst, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO, EU_PREDICATE_NORMAL);
	null = drv_i915_eu_null();
	null.type = EU_TYPE_D;
	i915_eu_dst(inst, null);

	/* Both targets are immediates, zero until the ENDIF is placed. */
	i915_eu_bit(inst, EU_SRC0_IS_IMM_BIT, 1U);
	i915_eu_bit(inst, EU_SRC1_IS_IMM_BIT, 1U);

	/* Succeeded: the IF waits for its targets. */
	return here;
}

/*
 * Encodes the ENDIF of an IF: the channels the IF stopped run again.
 * Returns its position.  Its JIP -- where the thread goes when no channel is
 * left after it -- is the next instruction until drv_i915_eu_patch_endif()
 * points it at the end of an enclosing loop, as Mesa's brw_set_uip_jip()
 * does.
 */
uint32_t
drv_i915_eu_endif(
	struct i915_eu_buf *buffer)
{
	uint32_t *inst;
	uint32_t here;

	/* The ENDIF's position, which the patches name it by. */
	here = drv_i915_eu_position(buffer);

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return here;

	/*
	 * Encodes an in-order ENDIF whose JIP is the next instruction, in bytes;
	 * the JIP is a signed-integer immediate source, as brw_ENDIF's src0.
	 */
	i915_eu_common(buffer, inst, EU_OP_ENDIF, I915_EU_IN_ORDER);
	i915_eu_bit(inst, EU_SRC0_IS_IMM_BIT, 1U);
	i915_eu_set(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO, EU_TYPE_D);
	i915_eu_set(inst, EU_JIP_HI, EU_JIP_LO, GEN12_EU_DWORDS * 4U);

	/* Succeeded: the ENDIF is placed. */
	return here;
}

/*
 * Points an IF's JIP and UIP at its ENDIF (Mesa's patch_IF_ELSE() without
 * an ELSE).  Positions outside what the buffer holds poison it.
 */
void
drv_i915_eu_patch_if(
	struct i915_eu_buf *buffer,
	uint32_t if_position,
	uint32_t endif_position)
{
	uint32_t *inst;
	uint32_t jump;
	uint32_t end;

	/* A poisoned buffer encoded neither instruction. */
	if (buffer->error != 0)
		return;

	/* The ENDIF comes after its IF, and both are in the buffer. */
	end = drv_i915_eu_position(buffer);
	if (endif_position <= if_position || endif_position >= end) {
		buffer->error = 1;
		return;
	}

	/* Both targets are the ENDIF, in bytes from the IF. */
	inst = buffer->words + (size_t)if_position * GEN12_EU_DWORDS;
	jump = (endif_position - if_position) * GEN12_EU_DWORDS * 4U;
	i915_eu_set(inst, EU_JIP_HI, EU_JIP_LO, jump);
	i915_eu_set(inst, EU_UIP_HI, EU_UIP_LO, jump);
}

/*
 * Points an ENDIF's JIP at `target`, the WHILE of the loop it is inside.
 * Positions outside what the buffer holds poison it.
 */
void
drv_i915_eu_patch_endif(
	struct i915_eu_buf *buffer,
	uint32_t endif_position,
	uint32_t target)
{
	uint32_t *inst;
	uint32_t end;

	/* A poisoned buffer encoded neither instruction. */
	if (buffer->error != 0)
		return;

	/* The target comes after the ENDIF, and both are in the buffer. */
	end = drv_i915_eu_position(buffer);
	if (target <= endif_position || target >= end) {
		buffer->error = 1;
		return;
	}

	/* The JIP, in bytes from the ENDIF. */
	inst = buffer->words + (size_t)endif_position * GEN12_EU_DWORDS;
	i915_eu_set(inst, EU_JIP_HI, EU_JIP_LO, (target - endif_position) * GEN12_EU_DWORDS * 4U);
}

/*
 * Moves the waits for out-of-order instructions (MATH, SEND) from right
 * after each to where their registers are next used, and gives each its
 * own token (ws075-p022).
 *
 * The encoder follows every out-of-order instruction with a sync.nop on
 * token 0.  This pass drops those, gives each out-of-order instruction the
 * next free of the sixteen tokens, and puts a sync.nop on a token right
 * before the first later instruction that reads or writes that token's
 * destination registers (.dst) or writes its source registers (.src), or
 * that needs the token again when all are busy.  Every token is waited for
 * before a WHILE, at the first instruction of a loop (a WHILE's target) and
 * before the SEND that ends the thread.  An IF and its ENDIF need no wait:
 * the channels that jump over the IF's body only skip instructions, so the
 * tokens in flight after the ENDIF are at most those after the body.  The
 * jumps of IF, ENDIF and WHILE are moved by the sync.nops put in between.
 *
 * Returns 0, EINVAL for a poisoned buffer or a jump out of the program, or
 * ENOMEM; on a failure the buffer is poisoned.
 */
int
drv_i915_eu_schedule(
	struct i915_eu_buf *buffer)
{
	struct i915_eu_schedule pass;
	uint32_t count;
	int error;

	/* A poisoned buffer is left as it is. */
	if (buffer->error != 0)
		return EINVAL;

	/* Nothing encoded, nothing to move. */
	count = (uint32_t)(buffer->count / GEN12_EU_DWORDS);
	if (count == 0U)
		return 0;

	/* Allocates the per-instruction marks and positions. */
	kern_memset(&pass, 0, sizeof(pass));
	pass.buffer = buffer;
	pass.count = count;
	drv_i915_eu_init(&pass.out);
	error = i915_eu_schedule_alloc(&pass);

	/* Marks the encoder's own waits and the first instruction of each loop. */
	if (error == 0)
		error = i915_eu_schedule_mark(&pass);

	/* Rebuilds the program with the waits where the registers are used. */
	if (error == 0)
		error = i915_eu_schedule_rebuild(&pass);

	/* Moves the jumps over the waits put in between. */
	if (error == 0)
		error = i915_eu_schedule_jumps(&pass);

	/* Frees the marks and positions. */
	kern_free(pass.dropped);
	kern_free(pass.loop_top);
	kern_free(pass.start);
	kern_free(pass.place);

	/* A failure poisons the buffer and keeps the old program out of use. */
	if (error != 0) {
		drv_i915_eu_free(&pass.out);
		buffer->error = 1;
		return error;
	}

	/* The new program replaces the old one. */
	drv_i915_eu_free(buffer);
	buffer->words = pass.out.words;
	buffer->count = pass.out.count;
	buffer->capacity = pass.out.capacity;

	/* Succeeded: the waits are where the registers are used. */
	return 0;
}

/*
 * Encodes a two-source instruction on the channels `scope` names,
 * predicated on `flag` when `predicated` is nonzero (see drv_i915_eu_alu2()).
 */
static void
i915_eu_alu2_common(
	struct i915_eu_buf *buffer,
	int predicated,
	enum i915_eu_flag flag,
	int scope,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;
	uint32_t opcode;

	/* Picks the hardware opcode; a subtraction is refused. */
	if (op == I915_EU_ADD) {
		opcode = EU_OP_ADD;
	} else if (op == I915_EU_MUL) {
		opcode = EU_OP_MUL;
	} else if (op == I915_EU_AND) {
		opcode = EU_OP_AND;
	} else if (op == I915_EU_OR) {
		opcode = EU_OP_OR;
	} else if (op == I915_EU_XOR) {
		opcode = EU_OP_XOR;
	} else if (op == I915_EU_SHL) {
		opcode = EU_OP_SHL;
	} else if (op == I915_EU_SHR) {
		opcode = EU_OP_SHR;
	} else if (op == I915_EU_ASR) {
		opcode = EU_OP_ASR;
	} else {
		buffer->error = 1;
		return;
	}

	/* The hardware has one immediate slot, and it belongs to the second source. */
	if (src0.file == EU_FILE_IMM) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order instruction on its channels and its three operands. */
	i915_eu_common(buffer, inst, opcode, I915_EU_IN_ORDER);
	i915_eu_scope(inst, scope);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);

	/* A masked instruction runs only on the channels whose bit of the flag is set. */
	if (predicated != 0) {
		i915_eu_flag(inst, flag);
		i915_eu_set(inst, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO, EU_PREDICATE_NORMAL);
	}
}

/*
 * Encodes a SEND or SENDC on the channels `scope` names, predicated on
 * `flag` when `predicated` is nonzero (see drv_i915_eu_send()).
 */
static void
i915_eu_send_common(
	struct i915_eu_buf *buffer,
	int predicated,
	enum i915_eu_flag flag,
	int scope,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor,
	int conditional,
	int end_of_thread)
{
	uint32_t *inst;
	uint32_t opcode;
	uint32_t eot;
	uint32_t swsb;
	int order;

	/* The low six bits of the extended descriptor are not encodable (they held the SFID once). */
	if ((ex_descriptor & I915_EU_EX_DESC_LOW_MASK) != 0U) {
		buffer->error = 1;
		return;
	}

	/* The shared-function field has four bits. */
	if (sfid > I915_EU_SFID_MAX) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* A render-target write is the conditional form of the message. */
	if (conditional != 0) {
		opcode = EU_OP_SENDC;
	} else {
		opcode = EU_OP_SEND;
	}

	/* The message that ends the thread is its own scoreboard class. */
	if (end_of_thread != 0) {
		order = I915_EU_END_OF_THREAD;
		eot = 1U;
	} else {
		order = I915_EU_OUT_OF_ORDER;
		eot = 0U;
	}

	/* Encodes the control fields, the channels, the end-of-thread bit and the shared function. */
	i915_eu_common(buffer, inst, opcode, order);
	i915_eu_scope(inst, scope);
	i915_eu_bit(inst, EU_SEND_EOT_BIT, eot);
	i915_eu_set(inst, EU_SEND_SFID_HI, EU_SEND_SFID_LO, sfid);

	/* A masked message goes only to the channels whose bit of the flag is set. */
	if (predicated != 0) {
		i915_eu_flag(inst, flag);
		i915_eu_set(inst, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO, EU_PREDICATE_NORMAL);
	}

	/* Encodes the operands: a file bit and a register number, nothing else. */
	i915_eu_bit(inst, EU_DST_REG_FILE_BIT, i915_eu_file_bit(dst));
	i915_eu_set(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO, dst.nr);
	i915_eu_bit(inst, EU_SRC0_REG_FILE_BIT, i915_eu_file_bit(src0));
	i915_eu_set(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO, src0.nr);
	i915_eu_bit(inst, EU_SRC1_REG_FILE_BIT, i915_eu_file_bit(src1));
	i915_eu_set(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO, src1.nr);

	/* Scatters the message descriptor as eu-encoding-gen12.h says. */
	i915_eu_set(inst, 123U, 122U, descriptor >> 30);
	i915_eu_set(inst, 71U, 67U, descriptor >> 25);
	i915_eu_set(inst, 55U, 51U, descriptor >> 20);
	i915_eu_set(inst, 121U, 113U, descriptor >> 11);
	i915_eu_set(inst, 91U, 81U, descriptor);

	/* Scatters the extended descriptor the same way. */
	i915_eu_set(inst, 127U, 124U, ex_descriptor >> 28);
	i915_eu_set(inst, 97U, 96U, ex_descriptor >> 26);
	i915_eu_set(inst, 65U, 64U, ex_descriptor >> 24);
	i915_eu_set(inst, 47U, 35U, ex_descriptor >> 11);
	i915_eu_set(inst, 103U, 99U, ex_descriptor >> 6);

	/* The message that ends the thread needs nothing after it. */
	if (end_of_thread != 0)
		return;

	/*
	 * A message with a destination is waited for until it has written it;
	 * one without, until it has read its sources.
	 */
	if (dst.file == EU_FILE_GRF) {
		swsb = EU_SWSB_SYNC_DST(I915_EU_TOKEN);
	} else {
		swsb = EU_SWSB_SYNC_SRC(I915_EU_TOKEN);
	}

	/* Waits until the message is done with its registers. */
	i915_eu_sync(buffer, swsb);
}

/* Names a general register operand: eight channels of `type`. */
static struct i915_eu_reg
i915_eu_grf_typed(
	uint32_t nr,
	uint32_t type)
{
	struct i915_eu_reg reg;

	/* Names the register and the type its channels are read as. */
	kern_memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.type = type;

	/* The ordinary SIMD8 region <8;8,1>: eight consecutive channels. */
	reg.vstride = EU_VSTRIDE_8;
	reg.width = EU_WIDTH_8;
	reg.hstride = EU_HSTRIDE_1;

	/* Succeeded: the operand reads the whole register. */
	return reg;
}

/* Reserves and zeroes one instruction, returning its four words. */
static uint32_t *
i915_eu_reserve(
	struct i915_eu_buf *buffer)
{
	uint32_t *grown;
	uint32_t *inst;
	size_t capacity;

	/* A prior error stops further encoding. */
	if (buffer->error != 0)
		return NULL;

	/* The buffer doubles geometrically to amortize the growth. */
	if (buffer->count + GEN12_EU_DWORDS > buffer->capacity) {
		/* The first growth takes the first capacity; each later one doubles. */
		if (buffer->capacity == 0U) {
			capacity = I915_EU_FIRST_CAPACITY;
		} else {
			capacity = buffer->capacity * 2U;
		}

		/* Allocates the larger storage; a failure poisons the buffer. */
		grown = kern_calloc(capacity, sizeof(uint32_t));
		if (grown == NULL) {
			buffer->error = 1;
			return NULL;
		}

		/* Moves the instructions encoded so far into the larger storage. */
		if (buffer->words != NULL) {
			kern_memcpy(grown, buffer->words, buffer->count * sizeof(uint32_t));
			kern_free(buffer->words);
		}

		/* Publishes the larger storage. */
		buffer->words = grown;
		buffer->capacity = capacity;
	}

	/* The new instruction starts zeroed so only set fields carry bits. */
	inst = &buffer->words[buffer->count];
	kern_memset(inst, 0, GEN12_EU_DWORDS * sizeof(uint32_t));
	buffer->count += GEN12_EU_DWORDS;

	/* Succeeded: the caller fills the four words. */
	return inst;
}

/*
 * Sets the control fields every instruction carries, the scoreboard byte among
 * them; `order` is I915_EU_IN_ORDER, I915_EU_OUT_OF_ORDER (a sync.nop follows)
 * or I915_EU_END_OF_THREAD.
 */
static void
i915_eu_common(
	struct i915_eu_buf *buffer,
	uint32_t *inst,
	uint32_t opcode,
	int order)
{
	uint32_t regdist;
	uint32_t swsb;

	/* The first in-order instruction has nothing before it to wait for. */
	if (buffer->in_order != 0U) {
		regdist = 1U;
	} else {
		regdist = 0U;
	}

	/* Encodes the opcode and the SIMD8 execution size. */
	i915_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, opcode);
	i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_8);

	/*
	 * An out-of-order instruction also names itself with the token; with
	 * nothing to wait for, the byte says only that.
	 */
	if (order == I915_EU_OUT_OF_ORDER) {
		if (regdist != 0U) {
			swsb = EU_SWSB_REGDIST_SET(regdist, I915_EU_TOKEN);
		} else {
			swsb = I915_EU_SWSB_TOKEN_SET | I915_EU_TOKEN;
		}
	} else {
		swsb = EU_SWSB_REGDIST(regdist);
	}

	/* Encodes the scoreboard byte. */
	i915_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO, swsb);

	/*
	 * The in-order count is what the next instruction's register distance
	 * counts back over; only an in-order instruction moves it.
	 */
	if (order == I915_EU_IN_ORDER)
		buffer->in_order++;
}

/*
 * Narrows an instruction to the channels `scope` names: the SIMD8 dispatch
 * under the execution mask (what i915_eu_common() encoded), all eight
 * channels outside it, or the first channel outside it.
 */
static void
i915_eu_scope(
	uint32_t *inst,
	int scope)
{
	/* The masked SIMD8 form is what the common fields already say. */
	if (scope == I915_EU_SCOPE_MASKED)
		return;

	/* A scalar instruction runs one channel, a sixteen-word one sixteen. */
	if (scope == I915_EU_SCOPE_SCALAR)
		i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_1);
	if (scope == I915_EU_SCOPE_SIXTEEN)
		i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_16);
	if (scope == I915_EU_SCOPE_FOUR)
		i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_4);

	/* Both other forms ignore the execution mask. */
	i915_eu_bit(inst, EU_NO_MASK_BIT, 1U);
}

/* Names the flag subregister an instruction's conditional modifier writes or its predicate reads. */
static void
i915_eu_flag(
	uint32_t *inst,
	enum i915_eu_flag flag)
{
	/* f0.0, f0.1, f1.0, f1.1: the register is the high bit, the subregister the low one. */
	i915_eu_bit(inst, EU_FLAG_REG_NR_BIT, (uint32_t)flag >> 1);
	i915_eu_bit(inst, EU_FLAG_SUBREG_NR_BIT, (uint32_t)flag & 1U);
}

/* Encodes a sync.nop: the front end waits here until the named dependency is resolved. */
static void
i915_eu_sync(
	struct i915_eu_buf *buffer,
	uint32_t swsb)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes a SIMD1 sync with the dependency it waits on, outside the channel mask. */
	i915_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, EU_OP_SYNC);
	i915_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO, swsb);
	i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_1);
	i915_eu_bit(inst, EU_NO_MASK_BIT, 1U);
}

/* Encodes a destination register. */
static void
i915_eu_dst(
	uint32_t *inst,
	struct i915_eu_reg reg)
{
	uint32_t stride;

	/*
	 * A destination is written with horizontal stride one, or two when it
	 * asks for it: the 16-bit halves of 32-bit channels (Mesa writes a
	 * packed half float that way, brw_lower_pack.cpp).
	 */
	stride = EU_HSTRIDE_1;
	if (reg.hstride == EU_HSTRIDE_2)
		stride = EU_HSTRIDE_2;

	/* Encodes the file, the type, the stride and the register. */
	i915_eu_bit(inst, EU_DST_REG_FILE_BIT, i915_eu_file_bit(reg));
	i915_eu_set(inst, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO, reg.type);
	i915_eu_set(inst, EU_DST_HSTRIDE_HI, EU_DST_HSTRIDE_LO, stride);
	i915_eu_set(inst, EU_DST_SUBREG_HI, EU_DST_SUBREG_LO, reg.subnr);
	i915_eu_set(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO, reg.nr);
}

/* Encodes source zero, whether a register or an immediate. */
static void
i915_eu_src0(
	uint32_t *inst,
	struct i915_eu_reg reg)
{
	/* Encodes the type the source is read as. */
	i915_eu_set(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO, reg.type);

	/* An immediate says so with its own bit and carries its value in the last word. */
	if (reg.file == EU_FILE_IMM) {
		i915_eu_bit(inst, EU_SRC0_IS_IMM_BIT, 1U);
		i915_eu_set(inst, EU_IMM32_HI, EU_IMM32_LO, reg.immediate);
		return;
	}

	/* Encodes the register, its region and its negate modifier. */
	i915_eu_bit(inst, EU_SRC0_REG_FILE_BIT, i915_eu_file_bit(reg));
	i915_eu_set(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO, reg.nr);
	i915_eu_set(inst, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO, reg.subnr);
	i915_eu_set(inst, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO, reg.hstride);
	i915_eu_set(inst, EU_SRC0_WIDTH_HI, EU_SRC0_WIDTH_LO, reg.width);
	i915_eu_set(inst, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO, reg.vstride);
	i915_eu_bit(inst, EU_SRC0_NEGATE_BIT, reg.negate & 1U);
	i915_eu_bit(inst, EU_SRC0_ABS_BIT, reg.absolute & 1U);
}

/* Encodes source one, whether a register or an immediate. */
static void
i915_eu_src1(
	uint32_t *inst,
	struct i915_eu_reg reg)
{
	/* Encodes the type the source is read as. */
	i915_eu_set(inst, EU_SRC1_REG_TYPE_HI, EU_SRC1_REG_TYPE_LO, reg.type);

	/* An immediate says so with its own bit and carries its value in the last word. */
	if (reg.file == EU_FILE_IMM) {
		i915_eu_bit(inst, EU_SRC1_IS_IMM_BIT, 1U);
		i915_eu_set(inst, EU_IMM32_HI, EU_IMM32_LO, reg.immediate);
		return;
	}

	/* Encodes the register, its region and its negate modifier. */
	i915_eu_bit(inst, EU_SRC1_REG_FILE_BIT, i915_eu_file_bit(reg));
	i915_eu_set(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO, reg.nr);
	i915_eu_set(inst, EU_SRC1_SUBREG_HI, EU_SRC1_SUBREG_LO, reg.subnr);
	i915_eu_set(inst, EU_SRC1_HSTRIDE_HI, EU_SRC1_HSTRIDE_LO, reg.hstride);
	i915_eu_set(inst, EU_SRC1_WIDTH_HI, EU_SRC1_WIDTH_LO, reg.width);
	i915_eu_set(inst, EU_SRC1_VSTRIDE_HI, EU_SRC1_VSTRIDE_LO, reg.vstride);
	i915_eu_bit(inst, EU_SRC1_NEGATE_BIT, reg.negate & 1U);
	i915_eu_bit(inst, EU_SRC1_ABS_BIT, reg.absolute & 1U);
}

/* Returns the hardware file bit of an operand: one for a general register, zero otherwise. */
static uint32_t
i915_eu_file_bit(
	struct i915_eu_reg reg)
{
	/* The hardware has one bit: general register or not. */
	if (reg.file == EU_FILE_GRF)
		return 1U;

	/* Anything else (the null register) is the architecture file. */
	return 0U;
}

/* Writes value into the inclusive bit range [high:low] of the instruction. */
static void
i915_eu_set(
	uint32_t *inst,
	unsigned high,
	unsigned low,
	uint32_t value)
{
	unsigned position;

	/* Each bit of the value takes its place from the low end upward. */
	for (position = low; position <= high; position++)
		i915_eu_bit(inst, position, (value >> (position - low)) & 1U);
}

/* Sets or clears one bit of the 128-bit instruction. */
static void
i915_eu_bit(
	uint32_t *inst,
	unsigned position,
	uint32_t value)
{
	uint32_t mask;

	/* The instruction is four little-endian words; the bit selects one. */
	mask = 1U << (position % 32U);

	/* Sets or clears the bit as the value's low bit says. */
	if ((value & 1U) != 0U) {
		inst[position / 32U] |= mask;
	} else {
		inst[position / 32U] &= ~mask;
	}
}

/* Allocates the marks and positions of a scheduling run; ENOMEM when one cannot be. */
static int
i915_eu_schedule_alloc(
	struct i915_eu_schedule *pass)
{
	/* The dropped sync.nops. */
	pass->dropped = kern_calloc(pass->count, sizeof(*pass->dropped));
	if (pass->dropped == NULL)
		return ENOMEM;

	/* The loops' first instructions. */
	pass->loop_top = kern_calloc(pass->count, sizeof(*pass->loop_top));
	if (pass->loop_top == NULL)
		return ENOMEM;

	/* Where each instruction's waits start, the end included. */
	pass->start = kern_calloc(pass->count + 1U, sizeof(*pass->start));
	if (pass->start == NULL)
		return ENOMEM;

	/* Where each instruction goes, the end included. */
	pass->place = kern_calloc(pass->count + 1U, sizeof(*pass->place));
	if (pass->place == NULL)
		return ENOMEM;

	/* Succeeded: every array is there. */
	return 0;
}

/*
 * Marks the encoder's own sync.nops, which are dropped, and the first
 * instruction of each loop; EINVAL for a WHILE that jumps out of the program.
 */
static int
i915_eu_schedule_mark(
	struct i915_eu_schedule *pass)
{
	const uint32_t *inst;
	uint32_t index;
	uint32_t opcode;
	uint32_t jump;
	uint32_t target;
	int wait;
	int error;

	/* Looks at every instruction once. */
	for (index = 0U; index < pass->count; index++) {
		inst = pass->buffer->words + (size_t)index * GEN12_EU_DWORDS;
		opcode = i915_eu_get(inst, EU_OPCODE_HI, EU_OPCODE_LO);

		/* The encoder's wait right after an out-of-order instruction is dropped. */
		wait = i915_eu_is_encoder_wait(pass->buffer, index);
		if (wait)
			pass->dropped[index] = 1U;

		/* Only a WHILE marks a loop. */
		if (opcode != EU_OP_WHILE)
			continue;

		/* A WHILE jumps back to the first instruction of its loop. */
		jump = i915_eu_get(inst, EU_JIP_HI, EU_JIP_LO);
		error = i915_eu_jump_target(index, jump, pass->count, &target);
		if (error != 0)
			return error;
		if (target == pass->count)
			return EINVAL;

		/* The target is where every token is waited for. */
		pass->loop_top[target] = 1U;
	}

	/* Succeeded: the marks are set. */
	return 0;
}

/*
 * Rebuilds the program into the pass's output: each kept instruction after
 * the waits it needs, and each out-of-order one with its own token.
 * Returns 0 or ENOMEM.
 */
static int
i915_eu_schedule_rebuild(
	struct i915_eu_schedule *pass)
{
	struct i915_eu_run write;
	struct i915_eu_run reads[I915_EU_MAX_READS];
	const uint32_t *inst;
	uint32_t *copy;
	uint32_t index;
	uint32_t opcode;
	uint32_t swsb;
	uint32_t id;
	int flush;
	int out_of_order;
	int end_of_thread;

	/* Walks the old program in order. */
	for (index = 0U; index < pass->count; index++) {
		inst = pass->buffer->words + (size_t)index * GEN12_EU_DWORDS;
		opcode = i915_eu_get(inst, EU_OPCODE_HI, EU_OPCODE_LO);
		pass->start[index] = (uint32_t)(pass->out.count / GEN12_EU_DWORDS);

		/* A dropped sync.nop leaves nothing; a jump to it lands on what follows. */
		if (pass->dropped[index] != 0U) {
			pass->place[index] = pass->start[index];
			continue;
		}

		/* Every token is waited for at a loop's first instruction, before a WHILE and before the end of the thread. */
		end_of_thread = i915_eu_is_end_of_thread(inst);
		flush = 0;
		if (pass->loop_top[index] != 0U) {
			flush = 1;
		} else if (opcode == EU_OP_WHILE) {
			flush = 1;
		} else if (end_of_thread) {
			flush = 1;
		}

		/* Waits for what the instruction depends on. */
		i915_eu_touches(inst, &write, reads);
		i915_eu_schedule_waits(pass, &write, reads, flush);

		/* An out-of-order instruction takes a token. */
		id = 0U;
		out_of_order = i915_eu_is_out_of_order(inst);
		if (out_of_order)
			id = i915_eu_schedule_take(pass, &write, reads);

		/* Copies the instruction. */
		pass->place[index] = (uint32_t)(pass->out.count / GEN12_EU_DWORDS);
		copy = i915_eu_reserve(&pass->out);
		if (copy == NULL)
			return ENOMEM;

		/* The copy is the old instruction bit for bit. */
		kern_memcpy(copy, inst, GEN12_EU_DWORDS * sizeof(uint32_t));

		/* Names the token in the copy's scoreboard byte (its low four bits). */
		if (out_of_order) {
			swsb = i915_eu_get(copy, EU_SWSB_HI, EU_SWSB_LO);
			i915_eu_set(copy, EU_SWSB_HI, EU_SWSB_LO, (swsb & ~0xfU) | id);
		}
	}

	/* The end of the program is a target past the last instruction. */
	pass->start[pass->count] = (uint32_t)(pass->out.count / GEN12_EU_DWORDS);
	pass->place[pass->count] = pass->start[pass->count];

	/* A sync.nop that could not be placed poisoned the output. */
	if (pass->out.error != 0)
		return ENOMEM;

	/* Succeeded: the program is rebuilt. */
	return 0;
}

/*
 * Puts before an instruction the waits it needs: on each token in flight
 * whose destination it reads or writes (.dst), or whose sources it writes
 * (.src); on every token when `flush` is set.
 */
static void
i915_eu_schedule_waits(
	struct i915_eu_schedule *pass,
	const struct i915_eu_run *write,
	const struct i915_eu_run reads[I915_EU_MAX_READS],
	int flush)
{
	struct i915_eu_token *token;
	uint32_t id;
	uint32_t read;
	int needs_dst;
	int needs_src;
	int meet;

	/* Looks at every token in flight. */
	for (id = 0U; id < I915_EU_TOKENS; id++) {
		token = &pass->tokens[id];
		if (!token->busy)
			continue;

		/* Writing what the token writes needs its destination written. */
		needs_dst = flush;
		meet = i915_eu_runs_meet(write, &token->write);
		if (meet)
			needs_dst = 1;

		/* So does reading it. */
		for (read = 0U; read < I915_EU_MAX_READS; read++) {
			meet = i915_eu_runs_meet(&reads[read], &token->write);
			if (meet)
				needs_dst = 1;
		}

		/* Writing what the token still reads needs its sources read. */
		needs_src = 0;
		for (read = 0U; read < I915_EU_MAX_READS; read++) {
			meet = i915_eu_runs_meet(write, &token->reads[read]);
			if (meet)
				needs_src = 1;
		}

		/* A destination wait covers the sources too. */
		if (needs_dst) {
			i915_eu_wait(&pass->out, token, id, 1);
		} else if (needs_src) {
			i915_eu_wait(&pass->out, token, id, 0);
		}
	}
}

/*
 * Gives an out-of-order instruction a token: the next free one in turn, or,
 * when all are busy, the oldest after a wait for its destination.  Returns
 * the token.
 */
static uint32_t
i915_eu_schedule_take(
	struct i915_eu_schedule *pass,
	const struct i915_eu_run *write,
	const struct i915_eu_run reads[I915_EU_MAX_READS])
{
	struct i915_eu_token *token;
	uint32_t id;
	uint32_t step;
	uint32_t candidate;
	uint32_t read;

	/* The next free token in turn. */
	id = I915_EU_TOKENS;
	for (step = 0U; step < I915_EU_TOKENS; step++) {
		candidate = (pass->next_token + step) % I915_EU_TOKENS;
		if (!pass->tokens[candidate].busy) {
			id = candidate;
			break;
		}
	}

	/* All busy: the oldest one's instruction is waited for and its token taken. */
	if (id == I915_EU_TOKENS) {
		id = 0U;
		for (candidate = 1U; candidate < I915_EU_TOKENS; candidate++) {
			if (pass->tokens[candidate].age < pass->tokens[id].age)
				id = candidate;
		}

		/* Its destination written, it is free for this instruction. */
		i915_eu_wait(&pass->out, &pass->tokens[id], id, 1);
	}

	/* The token now stands for this instruction's registers, the youngest in flight. */
	token = &pass->tokens[id];
	token->busy = 1;
	token->age = pass->clock;
	token->write = *write;
	for (read = 0U; read < I915_EU_MAX_READS; read++)
		token->reads[read] = reads[read];
	pass->clock++;
	pass->next_token = (id + 1U) % I915_EU_TOKENS;

	/* Succeeded: the instruction's token. */
	return id;
}

/*
 * Moves the jumps of the rebuilt program's branches (IF: JIP and UIP;
 * ENDIF, WHILE: JIP) by the waits now between each branch and its target;
 * a jump lands on its target's waits.  Returns 0, or EINVAL for a jump out
 * of the program.
 */
static int
i915_eu_schedule_jumps(
	struct i915_eu_schedule *pass)
{
	uint32_t *copy;
	uint32_t index;
	uint32_t opcode;
	int error;

	/* Looks at every kept instruction. */
	for (index = 0U; index < pass->count; index++) {
		if (pass->dropped[index] != 0U)
			continue;

		/* Only the branches carry jumps. */
		copy = pass->out.words + (size_t)pass->place[index] * GEN12_EU_DWORDS;
		opcode = i915_eu_get(copy, EU_OPCODE_HI, EU_OPCODE_LO);
		if (opcode != EU_OP_IF && opcode != EU_OP_ENDIF && opcode != EU_OP_WHILE)
			continue;

		/* The JIP. */
		error = i915_eu_schedule_move(pass, index, copy, EU_JIP_HI, EU_JIP_LO);
		if (error != 0)
			return error;

		/* An IF's UIP too. */
		if (opcode == EU_OP_IF) {
			error = i915_eu_schedule_move(pass, index, copy, EU_UIP_HI, EU_UIP_LO);
			if (error != 0)
				return error;
		}
	}

	/* Succeeded: every jump reaches its old target. */
	return 0;
}

/* Moves one jump field of the copy of old instruction `index`; EINVAL for a target out of the program. */
static int
i915_eu_schedule_move(
	struct i915_eu_schedule *pass,
	uint32_t index,
	uint32_t *inst,
	unsigned high,
	unsigned low)
{
	uint32_t jump;
	uint32_t target;
	int32_t moved;
	int error;

	/* The old target, from the old jump in bytes. */
	jump = i915_eu_get(inst, high, low);
	error = i915_eu_jump_target(index, jump, pass->count, &target);
	if (error != 0)
		return error;

	/* The new jump, from the branch's new place to the target's waits. */
	moved = ((int32_t)pass->start[target] - (int32_t)pass->place[index]) * (int32_t)(GEN12_EU_DWORDS * 4U);
	i915_eu_set(inst, high, low, (uint32_t)moved);

	/* Succeeded: the jump is moved. */
	return 0;
}

/* Reports whether an instruction is the encoder's sync.nop on token 0 right after an out-of-order instruction. */
static int
i915_eu_is_encoder_wait(
	const struct i915_eu_buf *buffer,
	uint32_t index)
{
	const uint32_t *inst;
	uint32_t opcode;
	uint32_t swsb;
	uint32_t destination_wait;
	uint32_t source_wait;
	int previous_out_of_order;

	/* The first instruction follows nothing. */
	if (index == 0U)
		return 0;

	/* Only a sync.nop waits. */
	inst = buffer->words + (size_t)index * GEN12_EU_DWORDS;
	opcode = i915_eu_get(inst, EU_OPCODE_HI, EU_OPCODE_LO);
	if (opcode != EU_OP_SYNC)
		return 0;

	/* It follows an out-of-order instruction. */
	previous_out_of_order = i915_eu_is_out_of_order(inst - GEN12_EU_DWORDS);
	if (!previous_out_of_order)
		return 0;

	/* It waits on token 0, for the destination or the sources. */
	swsb = i915_eu_get(inst, EU_SWSB_HI, EU_SWSB_LO);
	destination_wait = EU_SWSB_SYNC_DST(I915_EU_TOKEN);
	source_wait = EU_SWSB_SYNC_SRC(I915_EU_TOKEN);
	if (swsb == destination_wait)
		return 1;
	if (swsb == source_wait)
		return 1;

	/* Another wait is kept. */
	return 0;
}

/*
 * Works out the instruction a jump of `jump` bytes from instruction `index`
 * lands on (`count` for the end of the program); EINVAL when it is outside
 * the program or not on an instruction.
 */
static int
i915_eu_jump_target(
	uint32_t index,
	uint32_t jump,
	uint32_t count,
	uint32_t *target)
{
	int32_t bytes;
	int32_t landing;

	/* The jump is a signed number of bytes, a whole number of instructions. */
	bytes = (int32_t)jump;
	if ((bytes % (int32_t)(GEN12_EU_DWORDS * 4U)) != 0)
		return EINVAL;

	/* The instruction it lands on must be in the program or its end. */
	landing = (int32_t)index + bytes / (int32_t)(GEN12_EU_DWORDS * 4U);
	if (landing < 0 || landing > (int32_t)count)
		return EINVAL;

	/* Succeeded: the target. */
	*target = (uint32_t)landing;
	return 0;
}

/* Reads a field of the 128-bit instruction. */
static uint32_t
i915_eu_get(
	const uint32_t *inst,
	unsigned high,
	unsigned low)
{
	uint32_t value;
	unsigned position;

	/* Gathers the bits from the high end down. */
	value = 0U;
	for (position = high + 1U; position > low; position--)
		value = (value << 1) | ((inst[(position - 1U) / 32U] >> ((position - 1U) % 32U)) & 1U);

	/* Succeeded: the field's value. */
	return value;
}

/* Reports whether an instruction is out of order: a MATH, or a SEND that does not end the thread. */
static int
i915_eu_is_out_of_order(
	const uint32_t *inst)
{
	uint32_t opcode;
	int end_of_thread;

	/* Math runs out of order. */
	opcode = i915_eu_get(inst, EU_OPCODE_HI, EU_OPCODE_LO);
	if (opcode == EU_OP_MATH)
		return 1;

	/* Anything but a message runs in order. */
	if (opcode != EU_OP_SEND && opcode != EU_OP_SENDC)
		return 0;

	/* A message runs out of order unless it ends the thread. */
	end_of_thread = i915_eu_is_end_of_thread(inst);
	if (end_of_thread)
		return 0;

	/* Succeeded: an out-of-order message. */
	return 1;
}

/* Reports whether an instruction is the SEND that ends the thread. */
static int
i915_eu_is_end_of_thread(
	const uint32_t *inst)
{
	uint32_t opcode;
	uint32_t eot;

	/* Only a message can end the thread. */
	opcode = i915_eu_get(inst, EU_OPCODE_HI, EU_OPCODE_LO);
	if (opcode != EU_OP_SEND && opcode != EU_OP_SENDC)
		return 0;

	/* The end-of-thread bit says whether it does. */
	eot = i915_eu_get(inst, EU_SEND_EOT_BIT, EU_SEND_EOT_BIT);
	if (eot == 0U)
		return 0;

	/* Succeeded: the message ends the thread. */
	return 1;
}

/*
 * Reads back which general registers an instruction writes and reads: a
 * SEND's destination and payload runs from its descriptors, an ALU
 * instruction's destination and sources from their regions.  A sync.nop,
 * a branch or a NOP touches none.
 */
static void
i915_eu_touches(
	const uint32_t *inst,
	struct i915_eu_run *write,
	struct i915_eu_run reads[I915_EU_MAX_READS])
{
	uint32_t opcode;
	uint32_t index;
	uint32_t file;

	/* Touches nothing until an operand says otherwise. */
	write->first = 0U;
	write->count = 0U;
	for (index = 0U; index < I915_EU_MAX_READS; index++) {
		reads[index].first = 0U;
		reads[index].count = 0U;
	}

	/* Control flow and the scoreboard's own instructions touch no register. */
	opcode = i915_eu_get(inst, EU_OPCODE_HI, EU_OPCODE_LO);
	if (opcode == EU_OP_SYNC ||
	    opcode == EU_OP_NOP ||
	    opcode == EU_OP_IF ||
	    opcode == EU_OP_ENDIF ||
	    opcode == EU_OP_WHILE)
		return;

	/* An ALU instruction: its destination and its two sources as their regions say. */
	if (opcode != EU_OP_SEND && opcode != EU_OP_SENDC) {
		i915_eu_operand_run(inst, -1, write);
		i915_eu_operand_run(inst, 0, &reads[0]);
		i915_eu_operand_run(inst, 1, &reads[1]);
		return;
	}

	/* A message's reply: the registers from its destination, as many as desc 24:20 says. */
	file = i915_eu_get(inst, EU_DST_REG_FILE_BIT, EU_DST_REG_FILE_BIT);
	if (file != 0U) {
		write->first = i915_eu_get(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO);
		write->count = i915_eu_get(inst, 55U, 51U);
	}

	/* Its first payload run: as many registers as desc 28:25 says. */
	file = i915_eu_get(inst, EU_SRC0_REG_FILE_BIT, EU_SRC0_REG_FILE_BIT);
	if (file != 0U) {
		reads[0].first = i915_eu_get(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO);
		reads[0].count = i915_eu_get(inst, 70U, 67U);
	}

	/* Its second payload run: as many registers as ex_desc 10:6 says. */
	file = i915_eu_get(inst, EU_SRC1_REG_FILE_BIT, EU_SRC1_REG_FILE_BIT);
	if (file != 0U) {
		reads[1].first = i915_eu_get(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO);
		reads[1].count = i915_eu_get(inst, 103U, 99U);
	}
}

/*
 * Works out the general registers one operand of an ALU instruction spans
 * (`source` -1 for the destination, 0 or 1 for a source): none for an
 * immediate or an architecture register, else from the subregister, the
 * channels, the type's size and the stride; a scalar region reads one
 * element.
 */
static void
i915_eu_operand_run(
	const uint32_t *inst,
	int source,
	struct i915_eu_run *run)
{
	static const uint32_t strides[4] = { 0U, 1U, 2U, 4U };
	uint32_t channels;
	uint32_t immediate;
	uint32_t file;
	uint32_t nr;
	uint32_t subnr;
	uint32_t type;
	uint32_t stride;
	uint32_t vstride;
	uint32_t bytes;

	/* Nothing until the operand is known to be a general register. */
	run->first = 0U;
	run->count = 0U;

	/* The channels the instruction runs. */
	channels = 1U << i915_eu_get(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO);

	/* The operand's fields; a source may be an immediate. */
	immediate = 0U;
	if (source < 0) {
		file = i915_eu_get(inst, EU_DST_REG_FILE_BIT, EU_DST_REG_FILE_BIT);
		nr = i915_eu_get(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO);
		subnr = i915_eu_get(inst, EU_DST_SUBREG_HI, EU_DST_SUBREG_LO);
		type = i915_eu_get(inst, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO);
		stride = strides[i915_eu_get(inst, EU_DST_HSTRIDE_HI, EU_DST_HSTRIDE_LO)];
		vstride = 1U;
	} else if (source == 0) {
		immediate = i915_eu_get(inst, EU_SRC0_IS_IMM_BIT, EU_SRC0_IS_IMM_BIT);
		file = i915_eu_get(inst, EU_SRC0_REG_FILE_BIT, EU_SRC0_REG_FILE_BIT);
		nr = i915_eu_get(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO);
		subnr = i915_eu_get(inst, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO);
		type = i915_eu_get(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO);
		stride = strides[i915_eu_get(inst, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO)];
		vstride = i915_eu_get(inst, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO);
	} else {
		immediate = i915_eu_get(inst, EU_SRC1_IS_IMM_BIT, EU_SRC1_IS_IMM_BIT);
		file = i915_eu_get(inst, EU_SRC1_REG_FILE_BIT, EU_SRC1_REG_FILE_BIT);
		nr = i915_eu_get(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO);
		subnr = i915_eu_get(inst, EU_SRC1_SUBREG_HI, EU_SRC1_SUBREG_LO);
		type = i915_eu_get(inst, EU_SRC1_REG_TYPE_HI, EU_SRC1_REG_TYPE_LO);
		stride = strides[i915_eu_get(inst, EU_SRC1_HSTRIDE_HI, EU_SRC1_HSTRIDE_LO)];
		vstride = i915_eu_get(inst, EU_SRC1_VSTRIDE_HI, EU_SRC1_VSTRIDE_LO);
	}

	/* An immediate or an architecture register (the null register, a flag) is not a general register. */
	if (immediate != 0U)
		return;
	if (file == 0U)
		return;

	/* The bytes from the register's start: a scalar region reads one element, any other every channel's. */
	if (vstride == 0U && stride == 0U) {
		bytes = subnr + (1U << (type & 3U));
	} else {
		if (stride == 0U)
			stride = 1U;
		bytes = subnr + channels * (1U << (type & 3U)) * stride;
	}

	/* The registers those bytes span. */
	run->first = nr;
	run->count = (bytes + I915_EU_GRF_BYTES - 1U) / I915_EU_GRF_BYTES;
}

/* Reports whether two register runs share a register. */
static int
i915_eu_runs_meet(
	const struct i915_eu_run *first,
	const struct i915_eu_run *second)
{
	/* An empty run shares nothing. */
	if (first->count == 0U || second->count == 0U)
		return 0;

	/* Disjoint runs: one ends before the other starts. */
	if (first->first + first->count <= second->first)
		return 0;
	if (second->first + second->count <= first->first)
		return 0;

	/* Succeeded: they overlap. */
	return 1;
}

/*
 * Appends a sync.nop on a token to the rebuilt program: until its
 * instruction has written its destination (`destination` nonzero; the
 * token is then free) or has read its sources.
 */
static void
i915_eu_wait(
	struct i915_eu_buf *out,
	struct i915_eu_token *token,
	uint32_t id,
	int destination)
{
	uint32_t read;

	/* A destination wait frees the token. */
	if (destination) {
		i915_eu_sync(out, EU_SWSB_SYNC_DST(id));
		token->busy = 0;
		token->write.count = 0U;
		for (read = 0U; read < I915_EU_MAX_READS; read++)
			token->reads[read].count = 0U;
		return;
	}

	/* A source wait leaves the destination pending. */
	i915_eu_sync(out, EU_SWSB_SYNC_SRC(id));
	for (read = 0U; read < I915_EU_MAX_READS; read++)
		token->reads[read].count = 0U;
}
