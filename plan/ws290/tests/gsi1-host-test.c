/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GSI1 compiler's host test harness.  It compiles SPIR-V modules with
 * the GHCI backend, plays the Vulkan executor's part with the program's
 * record (it fills the constant bank, the resource table of set 2 and the
 * driver's table of set 3, and the attribute and fragment registers, as
 * docs/ghci.md and the driver's convention say), runs the IR on the
 * reference interpreter, and writes the files tools/gsi1model.py of
 * amaterrace-gpu takes and the results it must leave.  gsi1-host-test.py
 * runs the model and compares.  The layout values below are written from
 * ghci.md by hand, not taken from the compiler's headers.
 *
 *   gsi1-host-test encode OUT                 the encoder's words of known instructions
 *   gsi1-host-test compile STAGE IN.spv OUT [OPTIONS]
 *   gsi1-host-test pipeline VS.spv FS.spv OUT [OPTIONS]   the fragment program after the vertex one
 *   gsi1-host-test compute IN.spv OUT LANES GROUPS SEED [OPTIONS]
 *   gsi1-host-test fragment IN.spv OUT SEED [OPTIONS]
 *   gsi1-host-test vertex IN.spv OUT SEED [OPTIONS]
 *
 * OUT is a path prefix: OUT.bin is the program's code and OUT.record its
 * record; OUT.data.hex, .warps.hex, .init.hex, .prims.hex are the model's
 * inputs; OUT.expect.* what it must produce; OUT.args the model's other
 * arguments.  OPTIONS are comma-separated: samples=N, prim=line|point,
 * robust, regs=N, mask (the GPU writes r6), cover=HEX (the fragment warp's
 * coverage), floats (the buffers hold small floats), ia0=VKFORMAT (input
 * attachment 0's format), dyn=SET.BINDING (a dynamic buffer, repeatable),
 * amap=C0.C1... (the bound attributes' components by location), aint=HEX
 * (the integer attribute locations).
 */

#include "drivers/gpu/ghci/compiler/gsi1-compile.h"
#include "drivers/gpu/ghci/compiler/gsi1-encode.h"
#include "drivers/gpu/ghci/compiler/gsi1_isa.h"
#include "drivers/gpu/compiler/spirv.h"
#include "plan/ws290/tests/gsi1-reference.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The model's data image: its GPU address and words, and the constant bank's words at its start. */
#define MODEL_DATA_BASE		0x8000U
#define MODEL_DATA_WORDS	4096U
#define MODEL_BANK_WORDS	256U

/* The GPU address the harness gives the scratch memory, outside the data image. */
#define MODEL_SCRATCH_BASE	0x00100000U

/* Each buffer or image of a test takes a region of 256 words after the bank; region 0 holds the tables. */
#define REGION_WORDS		256U
#define REGION_BYTES		(4U * REGION_WORDS)

/* The resource table (set 2) at region 0's start, the driver's table (set 3) at its word 192. */
#define TABLE_SET3_WORD		192U

/* The bytes a dynamic buffer's offset moves its start by. */
#define DYNAMIC_SHIFT		64U

/* The test images: 8 x 8 texels, and two layers when they fit. */
#define IMAGE_SIDE		8U

/* The varying words the model's primitive table keeps (PT words 4 to 67). */
#define MODEL_VARY		64U

/* The PT words of a vertex in the model's --prims file, and the registers the model's files have. */
#define MODEL_PT_WORDS		128U
#define MODEL_REGISTERS		64U

/*
 * The constant bank: set s's table address at c[24 + 2 s] (low word),
 * push-constant bytes 96-127 at c32-c39 and the dynamic offsets from c40
 * (the driver's convention, design section 11.4).
 *
 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 8.4,
 * the mirrors of sets 0-3 in c[24..31]).
 */
#define BANK_SET(set)		(24U + 2U * (set))
#define BANK_PUSH_HIGH		32U
#define BANK_DYNAMIC		40U

/*
 * The descriptors' bytes: a buffer descriptor's address, size, stride
 * (16 bits) and format (8 bits); a texture descriptor's address, width,
 * height and depth (16 bits each), format and levels (8 bits), row and
 * slice pitch, samples and dimension (8 bits).
 *
 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 8.3,
 * GhciBufferDescriptor and GhciTextureDescriptor).
 */
#define BUFFER_BASE		0x00U
#define BUFFER_SIZE		0x08U
#define BUFFER_STRIDE		0x0cU
#define BUFFER_FORMAT		0x0eU
#define TEXTURE_BASE		0x00U
#define TEXTURE_WIDTH		0x08U
#define TEXTURE_HEIGHT		0x0aU
#define TEXTURE_DEPTH		0x0cU
#define TEXTURE_FORMAT		0x0eU
#define TEXTURE_LEVELS		0x0fU
#define TEXTURE_ROW_PITCH	0x10U
#define TEXTURE_SLICE_PITCH	0x14U
#define TEXTURE_SAMPLES		0x18U
#define TEXTURE_DIMENSION	0x19U
#define DIMENSION_2D		1U

/* The driver's table: the scratch memory's address of the vertex, fragment and compute warps (design section 11.4). */
#define DRIVER_SCRATCH_VERTEX	0x00U
#define DRIVER_SCRATCH_FRAGMENT	0x08U
#define DRIVER_SCRATCH_COMPUTE	0x10U

/*
 * A fragment warp's registers: the barycentrics, the depth, the pixel
 * centre, the sample mask and the centroid's barycentrics.
 *
 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section
 * 12.10.3, "Fragment Shader Input Interface").
 */
#define REG_B0			0U
#define REG_Z			3U
#define REG_X			4U
#define REG_Y			5U
#define REG_SAMPLE_MASK		6U
#define REG_CENTROID_B0		7U
#define REG_FRAGMENT_INPUTS	10U

/* The vertex and instance index the model gives a vertex warp's lane (gsi1-model-run.py's). */
#define MODEL_VERTEX_INDEX(lane) (7U + (lane))
#define MODEL_INSTANCE_INDEX	3U

/* The bits of a quiet NaN, which an undefined vertex's words hold so that reading them shows. */
#define NAN_BITS		0x7fc00000U

/* One test's options. */
struct test_options {
	struct ghci_gsi1_options compile;
	uint32_t cover;
	int floats;
	int has_map;
};

/* One run: the program, the IR, the model's memory and its expectation, and the reference. */
struct test_case {
	struct ghci_gsi1_program *program;
	struct drv_gpu_shader_ir *ir;
	struct test_options options;
	uint32_t data[MODEL_DATA_WORDS];
	uint32_t expect[MODEL_DATA_WORDS];
	uint8_t compare[MODEL_DATA_WORDS];
	uint32_t regions;
	uint32_t region_of[REF_UNIFORMS];
	uint32_t words_of[REF_UNIFORMS];
	uint32_t image_format[REF_UNIFORMS];
	uint32_t image_layers[REF_UNIFORMS];
	struct ref_run *run;
	uint32_t seed;
	uint32_t scratch_bytes;
};

static uint32_t *read_file(const char *path, size_t *words);
static int parse_options(const char *text, struct test_options *options);
static int compile_module(const char *path, enum drv_gpu_shader_stage stage, struct test_case *test);
static int write_words(const char *path, const uint32_t *words, uint32_t count);
static int write_binary(const char *prefix, const struct ghci_gsi1_program *program);
static void put_word(uint32_t *data, uint32_t word, uint32_t offset, uint32_t bytes, uint32_t value);
static uint32_t next_random(uint32_t *state);
static uint32_t random_float(uint32_t *state, float low, float high);
static int find_uniform(const struct drv_gpu_shader_ir *ir, uint32_t set, uint32_t binding, uint32_t *uniform);
static int bind_resources(struct test_case *test);
static int bind_buffer(struct test_case *test, uint32_t uniform, uint32_t entry);
static int bind_image(struct test_case *test, uint32_t uniform, uint32_t entry, uint32_t kind);
static int is_dynamic(const struct test_case *test, uint32_t uniform);
static uint32_t format_bytes(uint32_t format);
static void unpack_texel(uint32_t format, const uint8_t *bytes, uint32_t *texel);
static void pack_texel(uint32_t format, const uint32_t *texel, uint8_t *bytes);
static void store_images(struct test_case *test, uint32_t *memory);
static int run_compute(int argc, char **argv);
static int run_fragment(int argc, char **argv);
static int run_vertex(int argc, char **argv);
static int run_encode(int argc, char **argv);
static int run_compile(int argc, char **argv);
static int run_pipeline(int argc, char **argv);
static int write_registers(const char *path, uint32_t nregs, uint32_t (*value)[32], uint8_t (*known)[32]);
static float as_float(uint32_t bits);
static uint32_t as_bits(float number);

void *kern_malloc(size_t size);
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

/* The host's allocator stands in for the kernel's. */
void *
kern_malloc(
	size_t size)
{
	return malloc(size);
}

void *
kern_calloc(
	size_t count,
	size_t size)
{
	return calloc(count, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

/* Runs one mode. */
int
main(
	int argc,
	char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s encode|compile|compute|fragment|vertex ...\n", argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "encode") == 0)
		return run_encode(argc, argv);
	if (strcmp(argv[1], "compile") == 0)
		return run_compile(argc, argv);
	if (strcmp(argv[1], "pipeline") == 0)
		return run_pipeline(argc, argv);
	if (strcmp(argv[1], "compute") == 0)
		return run_compute(argc, argv);
	if (strcmp(argv[1], "fragment") == 0)
		return run_fragment(argc, argv);
	if (strcmp(argv[1], "vertex") == 0)
		return run_vertex(argc, argv);
	fprintf(stderr, "%s: unknown mode %s\n", argv[0], argv[1]);
	return 2;
}

/*
 * Writes the encoder's words of known instructions, one per line: the 64
 * bits in hex, a tab, and the assembly gsi1asm.py makes the same words
 * from.
 */
static int
run_encode(
	int argc,
	char **argv)
{
	struct gsi1_extra extra;
	FILE *out;

	if (argc != 3)
		return 2;
	out = fopen(argv[2], "w");
	if (out == NULL)
		return 1;

#define EMIT(word, text) fprintf(out, "%016llx\t%s\n", (unsigned long long)(word), text)
	memset(&extra, 0, sizeof(extra));
	EMIT(gsi1_encode_r(GSI1_OP_OP, GSI1_F3_ADD, GSI1_F7_BASE, 1, 2, 3, NULL), "add r1, r2, r3");
	EMIT(gsi1_encode_r(GSI1_OP_OP, GSI1_F3_ADD, GSI1_F7_ALT, 40, 33, 62, NULL), "sub r40, r33, r62");
	EMIT(gsi1_encode_r(GSI1_OP_OP, GSI1_F3_SLTU, GSI1_F7_BASE, 5, GSI1_ZERO, 7, NULL), "sltu r5, zero, r7");
	EMIT(gsi1_encode_r(GSI1_OP_OP, GSI1_F3_MINU, GSI1_F7_MINMAX, 9, 10, GSI1_C(3), NULL), "minu r9, r10, c3");
	EMIT(gsi1_encode_r(GSI1_OP_OP, GSI1_F3_REM, GSI1_F7_MULDIV, 1, 2, 3, NULL), "rem r1, r2, r3");
	EMIT(gsi1_encode_i(GSI1_OP_OPIMM, GSI1_F3_ADD, 4, GSI1_ZERO, -1, 0), "addi r4, zero, -1");
	EMIT(gsi1_encode_i(GSI1_OP_OPIMM, GSI1_F3_ADD, 4, 5, 123456, 0), "addi r4, r5, 123456");
	EMIT(gsi1_encode_i(GSI1_OP_OPIMM, GSI1_F3_AND, 4, 5, 0xffff, 0), "andi r4, r5, 65535");
	EMIT(gsi1_encode_i(GSI1_OP_OPIMM, GSI1_F3_SLTU, 4, 5, 1, 0), "sltiu r4, r5, 1");
	EMIT(gsi1_encode_shift(GSI1_F3_SRL, GSI1_F7_ALT, 6, 7, 16), "srai r6, r7, 16");
	EMIT(gsi1_encode_shift(GSI1_F3_SLL, GSI1_F7_BASE, 6, 50, 31), "slli r6, r50, 31");
	EMIT(gsi1_encode_movi(12, 0x3f800001U), "movi r12, 0x3f800001");
	EMIT(gsi1_encode_movi(44, 0xdead0000U), "lui r44, 0xdead0");
	EMIT(gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LW, 3, GSI1_C(1), 16, 0), "lw r3, 16(c1)");
	EMIT(gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LWC, 3, GSI1_ZERO, 200, 0), "lwc r3, 200(zero)");
	EMIT(gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LW, 3, GSI1_C(28), 48, 0), "lw r3, 48(c28)");
	EMIT(gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LWC, 3, GSI1_ZERO, 39, 0), "lwc r3, 39(zero)");
	EMIT(gsi1_encode_r(GSI1_OP_OP, GSI1_F3_ADD, GSI1_F7_BASE, 1, GSI1_VTX_INDEX, GSI1_INST_INDEX, NULL), "add r1, o120, o121");
	EMIT(gsi1_encode_s(GSI1_OP_STORE, GSI1_F3_SW, 8, 9, -8), "sw r9, -8(r8)");
	EMIT(gsi1_encode_s(GSI1_OP_STORE, GSI1_F3_SW, 8, 9, 40000), "sw r9, 40000(r8)");
	EMIT(gsi1_encode_s(GSI1_OP_STORE, GSI1_F3_SW, 40, 9, 384) | ((uint64_t)(GSI1_SW_X << GSI1_W1_SUB_SHIFT) << 32), "sw.x r9, 384(r40)");
	EMIT(gsi1_encode_b(GSI1_F3_BEXNZ, 0, -5), "bexnz -5");
	EMIT(gsi1_encode_b(GSI1_F3_BNONE, 33, 3000), "bnone r33, 3000");
	EMIT(gsi1_encode_b(GSI1_F3_J, 0, 7), "j 7");
	extra.modifiers = GSI1_W1_N1 | GSI1_W1_N2;
	EMIT(gsi1_encode_r(GSI1_OP_OPFP, 0, GSI1_F7_FSGNJ, 1, 2, 2, &extra), "fsgnj.s r1, -r2, -r2");
	extra.modifiers = GSI1_W1_SAT;
	EMIT(gsi1_encode_r(GSI1_OP_OPFP, GSI1_RM_RNE, GSI1_F7_FADD, 1, 3, GSI1_ZERO, &extra), "fadd.s.sat r1, r3, zero");
	extra.modifiers = 0;
	EMIT(gsi1_encode_r(GSI1_OP_OPFP, GSI1_RM_RTZ, GSI1_F7_FCVT_W_S, 1, 2, 1, &extra), "fcvt.wu.s r1, r2, rtz");
	EMIT(gsi1_encode_r(GSI1_OP_OPFP, GSI1_RM_RDN, GSI1_F7_FROUND, 1, 2, GSI1_RS2_FROUND, &extra), "fround.s r1, r2, rdn");
	EMIT(gsi1_encode_r(GSI1_OP_OPFP, GSI1_F3_FLE, GSI1_F7_FCMP, 1, 2, 3, &extra), "fle.s r1, r2, r3");
	EMIT(gsi1_encode_r(GSI1_OP_OPFP, GSI1_RM_RNE, GSI1_F7_FSQRT, 1, 2, 0, &extra), "fsqrt.s r1, r2");
	EMIT(gsi1_encode_r4(GSI1_OP_MADD, GSI1_RM_RNE, 1, 2, 3, 4, &extra), "fmadd.s r1, r2, r3, r4");
	EMIT(gsi1_encode_r4(GSI1_OP_NMSUB, GSI1_RM_RNE, 40, 41, 42, 43, &extra), "fnmsub.s r40, r41, r42, r43");
	extra.sub = GSI1_ATOM_CMPXCHG;
	EMIT(gsi1_encode_r4(GSI1_OP_CUSTOM0, GSI1_F3_ATOM, 1, 2, 3, 4, &extra), "atom.cmpxchg r1, (r2), r3, r4");
	extra.sub = GSI1_ATOM_ADD;
	EMIT(gsi1_encode_r4(GSI1_OP_CUSTOM0, GSI1_F3_ATOM, 1, 2, 3, 0, &extra), "atom.add r1, (r2), r3");
	extra.sub = GSI1_ATOM_UMIN | GSI1_ATOM_SHARED;
	EMIT(gsi1_encode_r4(GSI1_OP_CUSTOM0, GSI1_F3_ATOM, 40, 41, 42, 0, &extra), "atoms.umin r40, (r41), r42");
	extra.sub = GSI1_ATOM_CMPXCHG | GSI1_ATOM_SHARED;
	EMIT(gsi1_encode_r4(GSI1_OP_CUSTOM0, GSI1_F3_ATOM, 1, 2, 3, 4, &extra), "atoms.cmpxchg r1, (r2), r3, r4");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM0, GSI1_F3_LDS, 5, 6, 64, 0), "lds r5, 64(r6)");
	EMIT(gsi1_encode_s(GSI1_OP_CUSTOM0, GSI1_F3_STS, GSI1_ZERO, 7, 128), "sts r7, 128(zero)");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM0, GSI1_F3_FENCE, 0, 0, 5, 0), "fence 5");
	extra.sub = 0;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM0, GSI1_F3_BAR, 0, 0, 0, 0, &extra), "bar");
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM0, GSI1_F3_END, 0, 0, 0, 0, &extra), "end");
	extra.sub = 3;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM1, GSI1_F3_TEXB, 0x20 | 1, 8, 12, GSI1_ZERO, &extra), "texb.3.c r8, r12, zero, 3");
	extra.sub = 15;
	extra.offset_u = -1;
	extra.offset_v = 7;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM1, GSI1_F3_TEXL, 0, 20, 24, 26, &extra), "texl r20, r24, r26, 15, off(-1, 7)");
	extra.offset_u = 0;
	extra.offset_v = 0;
	extra.sub = 2;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM1, GSI1_F3_TEXL, GSI1_TEX_F7_GATHER | (2U << 2), 20, 24, 0, &extra), "texg.b r20, r24, 2");
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM1, GSI1_F3_TEXF, 0, 20, 24, 25, &extra), "texf r20, r24, r25, 2");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM1, GSI1_F3_TEXSZ, 9, 10, GSI1_TEXSZ_SIZE, 4), "texsz r9, r10, 4");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM1, GSI1_F3_TEXSZ, 9, 0, GSI1_TEXSZ_LEVELS, 4), "texsz.l r9, 4");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM1, GSI1_F3_TEXSZ, 9, 0, GSI1_TEXSZ_SAMPLES, 4), "texsz.s r9, 4");
	EMIT(gsi1_encode_s(GSI1_OP_CUSTOM1, GSI1_F3_EXP, 0, 5, GSI1_EXP_SAMPLE_MASK), "exp r5, 0x105");
	EMIT(gsi1_encode_s(GSI1_OP_CUSTOM1, GSI1_F3_EXP, 0, GSI1_ONE, GSI1_EXP_COLOR(3, 2)), "exp one, 0x132");
	EMIT(gsi1_encode_s(GSI1_OP_CUSTOM1, GSI1_F3_EXP, 0, 6, GSI1_EXP_COLOR(7, 3)), "exp r6, 0x173");
	EMIT(gsi1_encode_s(GSI1_OP_CUSTOM1, GSI1_F3_EXP, 0, 6, GSI1_EXP_COLOR0_SRC1(1)), "exp r6, 0x109");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM1, GSI1_F3_LDA, 7, 0, GSI1_LDA(GSI1_PT_POSITION(3), 2), 0), "lda r7, 14");
	EMIT(gsi1_encode_i(GSI1_OP_CUSTOM1, GSI1_F3_LDA, 7, 0, GSI1_LDA(GSI1_PT_VARYING(5), 2), 0), "lda r7, 38");
	extra.sub = GSI1_QD_BCAST;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM1, GSI1_F3_QD, 0, 7, 8, 0, &extra), "qbcast r7, r8");
	extra.sub = 0;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM2, GSI1_F3_PACK, GSI1_F7_PACKH, 7, 8, 9, &extra), "packh r7, r8, r9");
	extra.sub = 1;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM2, GSI1_F3_PACK, GSI1_F7_UNPACKH, 7, 8, 0, &extra), "unpackh.hi r7, r8");
	extra.sub = 0;
	EMIT(gsi1_encode_r4(GSI1_OP_CUSTOM3, GSI1_F3_SEL, 1, 2, 3, GSI1_ZERO, &extra), "sel r1, r2, r3, zero");
	extra.sub = GSI1_EXEC_SAVE;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM3, GSI1_F3_EXEC, 0, 30, 0, 0, &extra), "exec.save r30");
	extra.sub = GSI1_EXEC_AND;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM3, GSI1_F3_EXEC, 0, 0, 31, 0, &extra), "exec.and r31");
	extra.sub = 0;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM3, GSI1_F3_KILL, 0, 0, 3, 0, &extra), "kill r3");
	extra.sub = GSI1_XU_LOG2;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM3, GSI1_F3_XU, 0, 3, 4, 0, &extra), "log2 r3, r4");
	extra.sub = GSI1_XU_FRACT;
	EMIT(gsi1_encode_r(GSI1_OP_CUSTOM3, GSI1_F3_XU, 0, 3, 4, 0, &extra), "fract r3, r4");
#undef EMIT
	fclose(out);
	return 0;
}

/* Compiles a module and writes its binary. */
static int
run_compile(
	int argc,
	char **argv)
{
	struct test_case *test;
	enum drv_gpu_shader_stage stage;
	int error;

	if (argc < 5)
		return 2;
	test = calloc(1U, sizeof(*test));
	if (test == NULL)
		return 1;
	if (parse_options(argc > 5 ? argv[5] : "", &test->options) != 0)
		return 2;
	if (strcmp(argv[2], "vertex") == 0) {
		stage = DRV_GPU_STAGE_VERTEX;
	} else if (strcmp(argv[2], "fragment") == 0) {
		stage = DRV_GPU_STAGE_FRAGMENT;
	} else {
		stage = DRV_GPU_STAGE_COMPUTE;
	}
	error = compile_module(argv[3], stage, test);
	if (error != 0)
		return 1;
	error = write_binary(argv[4], test->program);
	ghci_gsi1_program_free(test->program);
	drv_gpu_shader_ir_free(test->ir);
	free(test);
	return error;
}

/*
 * Compiles a pipeline's vertex program, then its fragment program with
 * the vertex program's tables, and writes both (OUT-vs and OUT-fs).
 */
static int
run_pipeline(
	int argc,
	char **argv)
{
	struct test_case *vertex;
	struct test_case *fragment;
	char path[1024];
	int error;

	if (argc < 5)
		return 2;
	vertex = calloc(1U, sizeof(*vertex));
	fragment = calloc(1U, sizeof(*fragment));
	if (vertex == NULL || fragment == NULL)
		return 1;
	if (parse_options(argc > 5 ? argv[5] : "", &vertex->options) != 0)
		return 2;
	fragment->options = vertex->options;
	if (compile_module(argv[2], DRV_GPU_STAGE_VERTEX, vertex) != 0)
		return 1;
	fragment->options.compile.bindings = vertex->program->bindings;
	if (compile_module(argv[3], DRV_GPU_STAGE_FRAGMENT, fragment) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s-vs", argv[4]);
	error = write_binary(path, vertex->program);
	snprintf(path, sizeof(path), "%s-fs", argv[4]);
	if (error == 0)
		error = write_binary(path, fragment->program);
	ghci_gsi1_program_free(vertex->program);
	ghci_gsi1_program_free(fragment->program);
	drv_gpu_shader_ir_free(vertex->ir);
	drv_gpu_shader_ir_free(fragment->ir);
	free(vertex);
	free(fragment);
	return error;
}

/*
 * A compute dispatch: LANES invocations to a workgroup (1D), GROUPS
 * workgroups; the buffers hold random words, the reference runs each
 * workgroup, and the model must leave the same memory.
 */
static int
run_compute(
	int argc,
	char **argv)
{
	struct test_case *test;
	FILE *args;
	char path[1024];
	uint32_t lanes;
	uint32_t groups;
	uint32_t group;
	uint32_t uniform;
	uint32_t word;
	const char *why;

	if (argc < 7)
		return 2;
	test = calloc(1U, sizeof(*test));
	if (test == NULL)
		return 1;
	if (parse_options(argc > 7 ? argv[7] : "", &test->options) != 0)
		return 2;
	lanes = (uint32_t)strtoul(argv[4], NULL, 0);
	groups = (uint32_t)strtoul(argv[5], NULL, 0);
	test->seed = (uint32_t)strtoul(argv[6], NULL, 0) | 1U;
	if (compile_module(argv[2], DRV_GPU_STAGE_COMPUTE, test) != 0)
		return 1;
	if (test->ir->local_size[0] * test->ir->local_size[1] * test->ir->local_size[2] != lanes ||
	    test->ir->local_size[1] != 1U || lanes > REF_LANES) {
		fprintf(stderr, "%s: the workgroup is not %u invocations along x\n", argv[2], lanes);
		return 1;
	}

	/* The memory the interface table asks for. */
	test->run = calloc(1U, sizeof(*test->run));
	if (test->run == NULL)
		return 1;
	test->run->ir = test->ir;
	test->run->lanes = lanes;
	test->run->groups = groups;
	if (bind_resources(test) != 0)
		return 1;

	/* The reference, workgroup by workgroup. */
	for (group = 0U; group < groups; group++) {
		test->run->group = group;
		memset(test->run->shared, 0, sizeof(test->run->shared));
		why = ref_execute(test->run);
		free(test->run->values);
		test->run->values = NULL;
		if (why != NULL) {
			fprintf(stderr, "%s: the reference stops: %s\n", argv[2], why);
			return 1;
		}
	}

	/* What the model must leave: the regions as the reference left them. */
	memcpy(test->expect, test->data, sizeof(test->expect));
	for (uniform = 0U; uniform < test->ir->uniform_count && uniform < REF_UNIFORMS; uniform++) {
		if (test->run->bytes[uniform] == NULL || test->region_of[uniform] == UINT32_MAX)
			continue;
		if (test->ir->uniforms[uniform].kind != DRV_GPU_IR_UNIFORM_STORAGE)
			continue;
		for (word = 0U; word < test->words_of[uniform]; word++) {
			memcpy(&test->expect[test->region_of[uniform] + word], test->run->bytes[uniform] + 4U * word, 4U);
			test->compare[test->region_of[uniform] + word] = (uint8_t)(1U + (test->options.floats != 0));
		}
	}
	store_images(test, test->expect);

	/* The files. */
	if (write_binary(argv[3], test->program) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.data.hex", argv[3]);
	if (write_words(path, test->data, MODEL_DATA_WORDS) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.expect.hex", argv[3]);
	if (write_words(path, test->expect, MODEL_DATA_WORDS) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.compare", argv[3]);
	args = fopen(path, "w");
	if (args == NULL)
		return 1;
	for (word = 0U; word < MODEL_DATA_WORDS; word++)
		fprintf(args, "%u\n", test->compare[word]);
	fclose(args);
	snprintf(path, sizeof(path), "%s.args", argv[3]);
	args = fopen(path, "w");
	if (args == NULL)
		return 1;
	fprintf(args, "--lanes %u --groups %u --nregs %u", lanes, groups, MODEL_REGISTERS);
	if (test->scratch_bytes != 0U)
		fprintf(args, " --scratch %#x,%u", MODEL_SCRATCH_BASE, test->scratch_bytes);
	fprintf(args, "\n");
	fclose(args);
	return 0;
}

/*
 * One fragment warp in slot 0: the rasterizer's registers (random
 * barycentrics, depth, the pixel centres of 8 quads, the sample mask and
 * the centroid's barycentrics) and the primitive table's positions and
 * varyings; the reference interpolates the inputs as ir.h and ghci.md
 * section 12.10.3 say (a line's vertex 2 and a point's vertices 1 and 2
 * are undefined, and hold NaNs here, as does a point's r2), and the
 * model's exports must be the reference's outputs.
 */
static int
run_fragment(
	int argc,
	char **argv)
{
	struct test_case *test;
	static uint32_t regs[4 * 64][32];
	static uint8_t known[4 * 64][32];
	static uint32_t prims[4 * 3 * MODEL_PT_WORDS];
	uint32_t warps[20];
	uint32_t exports[1 + 8 * 22];
	uint32_t ms[8];
	uint32_t vertex_value[3][64];
	uint32_t position[3][4];
	float bary[32][3];
	float centroid[32][3];
	float weighted[3];
	float sum;
	const struct drv_gpu_shader_ir_io *io;
	struct ref_run *run;
	char path[1024];
	FILE *out;
	uint32_t lane;
	uint32_t index;
	uint32_t component;
	uint32_t vertex;
	uint32_t word;
	uint32_t quads;
	uint32_t quad;
	uint32_t mask;
	uint32_t exported;
	uint32_t smask;
	uint32_t zexp;
	uint32_t nregs;
	uint32_t count;
	uint32_t inner;
	uint32_t sample_out;
	uint32_t vertices;
	int centroid_input;
	const char *why;

	if (argc < 5)
		return 2;
	test = calloc(1U, sizeof(*test));
	if (test == NULL)
		return 1;
	test->options.cover = UINT32_MAX;
	if (parse_options(argc > 5 ? argv[5] : "", &test->options) != 0)
		return 2;
	test->seed = (uint32_t)strtoul(argv[4], NULL, 0) | 1U;
	if (compile_module(argv[2], DRV_GPU_STAGE_FRAGMENT, test) != 0)
		return 1;
	nregs = MODEL_REGISTERS;
	vertices = 3U;
	if (test->options.compile.primitive == GHCI_GSI1_PRIMITIVE_LINE)
		vertices = 2U;
	if (test->options.compile.primitive == GHCI_GSI1_PRIMITIVE_POINT)
		vertices = 1U;

	/* The reference's run. */
	test->run = calloc(1U, sizeof(*test->run));
	if (test->run == NULL)
		return 1;
	run = test->run;
	run->ir = test->ir;
	run->lanes = 32U;
	run->groups = 1U;
	if (bind_resources(test) != 0)
		return 1;

	/*
	 * The primitive table: each vertex's clip position in PT words 0-3
	 * (w in [1, 2)) and its varying words from PT word 4; NaNs for the
	 * vertices the primitive does not have.
	 */
	memset(prims, 0, sizeof(prims));
	for (vertex = 0U; vertex < 3U; vertex++) {
		for (word = 0U; word < 4U; word++) {
			position[vertex][word] = random_float(&test->seed, -2.0f, 2.0f);
			if (word == 3U)
				position[vertex][word] = random_float(&test->seed, 1.0f, 2.0f);
			if (vertex >= vertices)
				position[vertex][word] = NAN_BITS;
			prims[vertex * MODEL_PT_WORDS + word] = position[vertex][word];
		}
		for (word = 0U; word < MODEL_VARY; word++) {
			vertex_value[vertex][word] = random_float(&test->seed, -2.0f, 2.0f);
			if (vertex >= vertices)
				vertex_value[vertex][word] = NAN_BITS;
			prims[vertex * MODEL_PT_WORDS + 4U + word] = vertex_value[vertex][word];
		}
	}

	/* The rasterizer's registers of each lane. */
	memset(known, 0, sizeof(known));
	for (lane = 0U; lane < 32U; lane++) {
		float b0;
		float b1;

		b0 = as_float(random_float(&test->seed, 0.05f, 0.6f));
		b1 = as_float(random_float(&test->seed, 0.05f, 0.35f));
		if (vertices == 2U)
			b1 = 1.0f - b0;
		bary[lane][0] = b0;
		bary[lane][1] = b1;
		bary[lane][2] = (1.0f - b0) - b1;
		if (vertices == 2U)
			bary[lane][2] = 0.0f;
		b0 = as_float(random_float(&test->seed, 0.05f, 0.6f));
		b1 = as_float(random_float(&test->seed, 0.05f, 0.35f));
		if (vertices == 2U)
			b1 = 1.0f - b0;
		centroid[lane][0] = b0;
		centroid[lane][1] = b1;
		centroid[lane][2] = (1.0f - b0) - b1;
		if (vertices == 2U)
			centroid[lane][2] = 0.0f;
		for (index = 0U; index < 3U; index++) {
			regs[REG_B0 + index][lane] = as_bits(bary[lane][index]);
			regs[REG_CENTROID_B0 + index][lane] = as_bits(centroid[lane][index]);
		}
		if (vertices == 1U)
			regs[REG_B0 + 2U][lane] = NAN_BITS;
		regs[REG_Z][lane] = random_float(&test->seed, -0.25f, 1.25f);
		quad = lane / 4U;
		run->pixel_x[lane] = 2U * (quad % 4U) + (lane & 1U);
		run->pixel_y[lane] = 2U * (quad / 4U) + ((lane >> 1) & 1U);
		regs[REG_X][lane] = as_bits((float)run->pixel_x[lane] + 0.5f);
		regs[REG_Y][lane] = as_bits((float)run->pixel_y[lane] + 0.5f);
		regs[REG_SAMPLE_MASK][lane] = (next_random(&test->seed) % 15U) + 1U;
		for (index = 0U; index < REG_FRAGMENT_INPUTS; index++)
			known[index][lane] = 1U;
		run->helper[lane] = (uint8_t)(((test->options.cover >> lane) & 1U) == 0U);
	}

	/* Each lane's inputs as the IR has them. */
	for (lane = 0U; lane < 32U; lane++) {
		weighted[0] = 0.0f;
		sum = 0.0f;
		for (index = 0U; index < test->ir->input_count; index++) {
			io = &test->ir->inputs[index];
			if (io->location >= 16U)
				continue;
			centroid_input = io->centroid != 0U && test->options.compile.samples > 1U;
			for (component = 0U; component < 4U; component++) {
				word = 4U * io->location + component;
				if (io->flat != 0U || vertices == 1U) {
					run->input[lane][io->location][component] = vertex_value[0][word];
					continue;
				}
				for (vertex = 0U; vertex < vertices; vertex++) {
					weighted[vertex] = centroid_input ? centroid[lane][vertex] : bary[lane][vertex];
				}
				if (io->noperspective != 0U) {
					sum = 0.0f;
					for (vertex = 0U; vertex < vertices; vertex++) {
						weighted[vertex] *= as_float(position[vertex][3]);
						sum += weighted[vertex];
					}
					for (vertex = 0U; vertex < vertices; vertex++)
						weighted[vertex] /= sum;
				}
				sum = 0.0f;
				for (vertex = 0U; vertex < vertices; vertex++)
					sum += as_float(vertex_value[vertex][word]) * weighted[vertex];
				run->input[lane][io->location][component] = as_bits(sum);
			}
		}

		/* The built-ins. */
		run->input[lane][DRV_GPU_SHADER_LOCATION_FRAG_COORD][0] = regs[REG_X][lane];
		run->input[lane][DRV_GPU_SHADER_LOCATION_FRAG_COORD][1] = regs[REG_Y][lane];
		run->input[lane][DRV_GPU_SHADER_LOCATION_FRAG_COORD][2] = as_bits(fminf(fmaxf(as_float(regs[REG_Z][lane]), 0.0f), 1.0f));
		sum = 0.0f;
		for (vertex = 0U; vertex < vertices; vertex++)
			sum += (vertices == 1U ? 1.0f : bary[lane][vertex]) * as_float(position[vertex][3]);
		run->input[lane][DRV_GPU_SHADER_LOCATION_FRAG_COORD][3] = as_bits(1.0f / sum);
		run->input[lane][DRV_GPU_SHADER_LOCATION_FRONT_FACING][0] = UINT32_MAX;
		run->input[lane][DRV_GPU_SHADER_LOCATION_SAMPLE_MASK][0] = test->options.compile.sample_mask_input != 0U ?
		    regs[REG_SAMPLE_MASK][lane] : 1U;
		run->input[lane][DRV_GPU_SHADER_LOCATION_POINT_COORD][0] = regs[REG_B0][lane];
		run->input[lane][DRV_GPU_SHADER_LOCATION_POINT_COORD][1] = regs[REG_B0 + 1U][lane];
	}

	/* The reference. */
	why = ref_execute(run);
	if (why != NULL) {
		fprintf(stderr, "%s: the reference stops: %s\n", argv[2], why);
		return 1;
	}

	/* The quads the model reports: those with coverage, from the first. */
	quads = 0U;
	while (quads < 8U && ((test->options.cover >> (4U * quads)) & 15U) != 0U)
		quads++;
	count = 0U;
	exports[count++] = quads;
	for (quad = 0U; quad < quads; quad++) {
		mask = 0U;
		smask = 0U;
		zexp = 0U;
		for (inner = 0U; inner < 4U; inner++) {
			lane = 4U * quad + inner;
			exported = 0U;
			if (run->helper[lane] != 0U || run->killed[lane] != 0U)
				continue;
			sample_out = UINT32_MAX;
			if (run->written[lane][REF_SLOT_SAMPLE_MASK][0] != 0U)
				sample_out = run->output[lane][REF_SLOT_SAMPLE_MASK][0];
			exported = 1U;
			if ((regs[REG_SAMPLE_MASK][lane] & sample_out & 15U) == 0U)
				continue;
			mask |= 1U << inner;
			smask |= (regs[REG_SAMPLE_MASK][lane] & sample_out & 15U) << (4U * inner);
			if (run->written[lane][REF_SLOT_DEPTH][0] != 0U)
				zexp |= 1U << inner;
			(void)exported;
		}

		/* The quad's one beat (attachment 0, its first and last): seq 0, qlast, qfirst, last, mask. */
		exports[count++] = (0U << 16) | (1U << 12) | (1U << 11) | ((quad == quads - 1U ? 1U : 0U) << 8) | mask;
		lane = 4U * quad;
		exports[count++] = (run->pixel_y[lane] << 16) | run->pixel_x[lane];
		for (inner = 0U; inner < 4U; inner++) {
			lane = 4U * quad + inner;
			for (component = 0U; component < 4U; component++) {
				word = 0U;
				if ((mask >> inner) & 1U && run->written[lane][0][component] != 0U)
					word = run->output[lane][0][component];
				exports[count++] = word;
			}
			word = 0U;
			if ((mask >> inner) & 1U) {
				word = regs[REG_Z][lane];
				if (run->written[lane][REF_SLOT_DEPTH][0] != 0U)
					word = run->output[lane][REF_SLOT_DEPTH][0];
			}
			exports[count++] = word;
		}
		ms[quad] = (zexp << 16) | smask;
	}

	/* The files. */
	if (write_binary(argv[3], test->program) != 0)
		return 1;
	memset(warps, 0, sizeof(warps));
	warps[0] = 1U;
	warps[1] = UINT32_MAX;
	warps[2] = test->options.cover;
	warps[3] = 0U;
	warps[4] = 1U;
	for (index = 1U; index < 4U; index++)
		warps[5U * index] = 2U;
	snprintf(path, sizeof(path), "%s.warps.hex", argv[3]);
	if (write_words(path, warps, 20U) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.init.hex", argv[3]);
	if (write_registers(path, nregs, regs, known) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.prims.hex", argv[3]);
	if (write_words(path, prims, 4U * 3U * MODEL_PT_WORDS) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.data.hex", argv[3]);
	if (write_words(path, test->data, MODEL_DATA_WORDS) != 0)
		return 1;
	memcpy(test->expect, test->data, sizeof(test->expect));
	store_images(test, test->expect);
	for (index = 0U; index < test->ir->uniform_count && index < REF_UNIFORMS; index++) {
		if (test->run->bytes[index] == NULL || test->region_of[index] == UINT32_MAX)
			continue;
		if (test->ir->uniforms[index].kind != DRV_GPU_IR_UNIFORM_STORAGE)
			continue;
		for (word = 0U; word < test->words_of[index]; word++) {
			memcpy(&test->expect[test->region_of[index] + word], test->run->bytes[index] + 4U * word, 4U);
			test->compare[test->region_of[index] + word] = 1U;
		}
	}
	snprintf(path, sizeof(path), "%s.expect.hex", argv[3]);
	if (write_words(path, test->expect, MODEL_DATA_WORDS) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.compare", argv[3]);
	out = fopen(path, "w");
	if (out == NULL)
		return 1;
	for (word = 0U; word < MODEL_DATA_WORDS; word++)
		fprintf(out, "%u\n", test->compare[word]);
	fclose(out);
	snprintf(path, sizeof(path), "%s.expect-exp.hex", argv[3]);
	if (write_words(path, exports, count) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.expect-ms.hex", argv[3]);
	if (write_words(path, ms, quads) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.args", argv[3]);
	out = fopen(path, "w");
	if (out == NULL)
		return 1;
	fprintf(out, "--nregs %u --vary %u", nregs, MODEL_VARY);
	if (test->scratch_bytes != 0U)
		fprintf(out, " --scratch %#x,%u", MODEL_SCRATCH_BASE, test->scratch_bytes);
	fprintf(out, "\n");
	fclose(out);
	return 0;
}

/*
 * One vertex warp in slot 0: the attributes packed from r0 as ghci.md
 * section 12.10.3 says (each bound location's components from the sum of
 * those before it; random floats, and 0, 0, 1 for the components a
 * format lacks), the vertex and instance index as gsi1-model-run.py gives
 * operands 120 and 121; the model's primitive-table entries must be the
 * reference's outputs.
 */
static int
run_vertex(
	int argc,
	char **argv)
{
	struct test_case *test;
	static uint32_t regs[4 * 64][32];
	static uint8_t known[4 * 64][32];
	uint32_t warps[20];
	static uint32_t pt[32 * (4 + MODEL_VARY)];
	static uint8_t pt_known[32 * (4 + MODEL_VARY)];
	const struct drv_gpu_shader_ir_io *io;
	struct ref_run *run;
	char path[1024];
	FILE *out;
	uint32_t lane;
	uint32_t index;
	uint32_t component;
	uint32_t location;
	uint32_t first;
	uint32_t count;
	uint32_t nregs;
	const char *why;

	if (argc < 5)
		return 2;
	test = calloc(1U, sizeof(*test));
	if (test == NULL)
		return 1;
	if (parse_options(argc > 5 ? argv[5] : "", &test->options) != 0)
		return 2;
	test->seed = (uint32_t)strtoul(argv[4], NULL, 0) | 1U;
	if (compile_module(argv[2], DRV_GPU_STAGE_VERTEX, test) != 0)
		return 1;
	nregs = MODEL_REGISTERS;
	test->run = calloc(1U, sizeof(*test->run));
	if (test->run == NULL)
		return 1;
	run = test->run;
	run->ir = test->ir;
	run->lanes = 32U;
	run->groups = 1U;
	if (bind_resources(test) != 0)
		return 1;

	/* The attributes, location by location from r0, and the indices. */
	memset(known, 0, sizeof(known));
	first = 0U;
	for (location = 0U; location < 16U; location++) {
		count = 0U;
		if (test->options.has_map != 0) {
			count = test->options.compile.attribute_components[location];
		} else {
			for (index = 0U; index < test->ir->input_count; index++) {
				io = &test->ir->inputs[index];
				if (io->location == location)
					count = io->components == 0U || io->components > 4U ? 4U : io->components;
			}
		}
		for (lane = 0U; lane < 32U; lane++) {
			for (component = 0U; component < 4U; component++) {
				if (component < count) {
					regs[first + component][lane] = random_float(&test->seed, -4.0f, 4.0f);
					known[first + component][lane] = 1U;
					run->input[lane][location][component] = regs[first + component][lane];
				} else if (component < 3U) {
					run->input[lane][location][component] = 0U;
				} else if (((test->options.compile.attribute_integer >> location) & 1U) != 0U) {
					run->input[lane][location][component] = 1U;
				} else {
					run->input[lane][location][component] = as_bits(1.0f);
				}
			}
		}
		first += count;
	}
	for (lane = 0U; lane < 32U; lane++) {
		run->input[lane][DRV_GPU_SHADER_LOCATION_VERTEX_INDEX][0] = MODEL_VERTEX_INDEX(lane);
		run->input[lane][DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX][0] = MODEL_INSTANCE_INDEX;
	}

	/* The reference. */
	why = ref_execute(run);
	if (why != NULL) {
		fprintf(stderr, "%s: the reference stops: %s\n", argv[2], why);
		return 1;
	}

	/* The primitive-table words the model must leave. */
	memset(pt_known, 0, sizeof(pt_known));
	for (lane = 0U; lane < 32U; lane++) {
		for (component = 0U; component < 4U; component++) {
			if (run->written[lane][REF_SLOT_POSITION][component] == 0U)
				continue;
			pt[lane * (4U + MODEL_VARY) + component] = run->output[lane][REF_SLOT_POSITION][component];
			pt_known[lane * (4U + MODEL_VARY) + component] = 1U;
		}
		for (location = 0U; location < GHCI_GSI1_LOCATIONS; location++) {
			for (component = 0U; component < 4U; component++) {
				if (run->written[lane][location][component] == 0U)
					continue;
				pt[lane * (4U + MODEL_VARY) + 4U + 4U * location + component] = run->output[lane][location][component];
				pt_known[lane * (4U + MODEL_VARY) + 4U + 4U * location + component] = 1U;
			}
		}
	}

	/* The files. */
	if (write_binary(argv[3], test->program) != 0)
		return 1;
	memset(warps, 0, sizeof(warps));
	warps[0] = 0U;
	warps[1] = UINT32_MAX;
	warps[2] = UINT32_MAX;
	for (index = 1U; index < 4U; index++)
		warps[5U * index] = 2U;
	snprintf(path, sizeof(path), "%s.warps.hex", argv[3]);
	if (write_words(path, warps, 20U) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.init.hex", argv[3]);
	if (write_registers(path, nregs, regs, known) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.data.hex", argv[3]);
	if (write_words(path, test->data, MODEL_DATA_WORDS) != 0)
		return 1;
	snprintf(path, sizeof(path), "%s.expect-pt.hex", argv[3]);
	out = fopen(path, "w");
	if (out == NULL)
		return 1;
	for (index = 0U; index < 32U * (4U + MODEL_VARY); index++) {
		if (pt_known[index] != 0U) {
			fprintf(out, "1%08x\n", pt[index]);
		} else {
			fprintf(out, "000000000\n");
		}
	}
	fclose(out);
	snprintf(path, sizeof(path), "%s.args", argv[3]);
	out = fopen(path, "w");
	if (out == NULL)
		return 1;
	fprintf(out, "--nregs %u --vary %u", nregs, MODEL_VARY);
	if (test->scratch_bytes != 0U)
		fprintf(out, " --scratch %#x,%u", MODEL_SCRATCH_BASE, test->scratch_bytes);
	fprintf(out, "\n");
	fclose(out);
	return 0;
}

/* Reads a SPIR-V module. */
static uint32_t *
read_file(
	const char *path,
	size_t *words)
{
	FILE *file;
	uint32_t *data;
	long length;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 4U);
	if (data == NULL || fread(data, 1U, (size_t)length, file) != (size_t)length) {
		fclose(file);
		free(data);
		return NULL;
	}
	fclose(file);
	*words = (size_t)length / 4U;
	return data;
}

/* Reads the comma-separated options. */
static int
parse_options(
	const char *text,
	struct test_options *options)
{
	char buffer[512];
	char *item;
	char *next;
	uint32_t location;

	if (strlen(text) >= sizeof(buffer))
		return 1;
	strcpy(buffer, text);
	for (item = buffer; item != NULL && *item != '\0'; item = next) {
		next = strchr(item, ',');
		if (next != NULL)
			*next++ = '\0';
		if (strncmp(item, "samples=", 8) == 0) {
			options->compile.samples = (uint32_t)strtoul(item + 8, NULL, 0);
		} else if (strcmp(item, "prim=point") == 0) {
			options->compile.primitive = GHCI_GSI1_PRIMITIVE_POINT;
		} else if (strcmp(item, "prim=line") == 0) {
			options->compile.primitive = GHCI_GSI1_PRIMITIVE_LINE;
		} else if (strcmp(item, "robust") == 0) {
			options->compile.robust_buffer_access = 1U;
		} else if (strncmp(item, "regs=", 5) == 0) {
			options->compile.registers = (uint32_t)strtoul(item + 5, NULL, 0);
		} else if (strncmp(item, "dyn=", 4) == 0) {
			if (options->compile.dynamic_count == GHCI_GSI1_DYNAMIC_OFFSETS)
				return 1;
			options->compile.dynamic[options->compile.dynamic_count].set = (uint32_t)strtoul(item + 4, &item, 0);
			options->compile.dynamic[options->compile.dynamic_count].binding = (uint32_t)strtoul(item + 1, NULL, 0);
			options->compile.dynamic_count++;
		} else if (strncmp(item, "amap=", 5) == 0) {
			options->compile.has_attribute_map = 1U;
			options->has_map = 1;
			item += 5;
			for (location = 0U; location < GHCI_GSI1_LOCATIONS && *item != '\0'; location++) {
				options->compile.attribute_components[location] = (uint32_t)strtoul(item, &item, 0);
				if (*item == '.')
					item++;
			}
		} else if (strncmp(item, "aint=", 5) == 0) {
			options->compile.attribute_integer = (uint32_t)strtoul(item + 5, NULL, 16);
		} else if (strcmp(item, "mask") == 0) {
			options->compile.sample_mask_input = 1U;
		} else if (strncmp(item, "cover=", 6) == 0) {
			options->cover = (uint32_t)strtoul(item + 6, NULL, 16);
		} else if (strcmp(item, "floats") == 0) {
			options->floats = 1;
		} else if (strncmp(item, "ia0=", 4) == 0) {
			options->compile.input_attachment_format[0] = (uint32_t)strtoul(item + 4, NULL, 0);
			options->compile.input_attachment_samples[0] = 1U;
		} else {
			fprintf(stderr, "unknown option %s\n", item);
			return 1;
		}
	}
	return 0;
}

/* Parses (for the reference) and compiles (for the model) a module. */
static int
compile_module(
	const char *path,
	enum drv_gpu_shader_stage stage,
	struct test_case *test)
{
	struct drv_gpu_compile_diagnostic parse_diagnostic;
	struct ghci_gsi1_diagnostic diagnostic;
	uint32_t *words;
	size_t count;
	int error;

	words = read_file(path, &count);
	if (words == NULL) {
		fprintf(stderr, "%s: cannot read\n", path);
		return 1;
	}
	memset(&parse_diagnostic, 0, sizeof(parse_diagnostic));
	error = drv_gpu_shader_parse_features(words, count, stage, DRV_GPU_SPIRV_FEATURES_GHCI, &test->ir, &parse_diagnostic);
	if (error != 0) {
		fprintf(stderr, "%s: the parser refuses (%d): %s at word %u\n", path, error,
			parse_diagnostic.reason != NULL ? parse_diagnostic.reason : "-", parse_diagnostic.word_offset);
		free(words);
		return 1;
	}
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = ghci_gsi1_compile(words, count, stage, &test->options.compile, &test->program, &diagnostic);
	free(words);
	if (error != 0) {
		fprintf(stderr, "%s: the compiler refuses (%d): %s at %u\n", path, error,
			diagnostic.reason != NULL ? diagnostic.reason : "-", diagnostic.instruction);
		return 1;
	}
	return 0;
}

/* Writes 32-bit words, one per line, in hex. */
static int
write_words(
	const char *path,
	const uint32_t *words,
	uint32_t count)
{
	FILE *file;
	uint32_t index;

	file = fopen(path, "w");
	if (file == NULL)
		return 1;
	for (index = 0U; index < count; index++)
		fprintf(file, "%08x\n", words[index]);
	fclose(file);
	return 0;
}

/* Writes a program's binary (little endian). */
static int
write_binary(
	const char *prefix,
	const struct ghci_gsi1_program *program)
{
	char path[1024];
	FILE *file;
	uint32_t index;

	snprintf(path, sizeof(path), "%s.bin", prefix);
	file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	if (fwrite(program->words, 4U, program->word_count, file) != program->word_count) {
		fclose(file);
		return 1;
	}
	fclose(file);

	/* The record, one "key value" line each, the tables' entries as "kind set binding [offset]". */
	snprintf(path, sizeof(path), "%s.record", prefix);
	file = fopen(path, "w");
	if (file == NULL)
		return 1;
	fprintf(file, "instructions %u\nwords %u\nused_regs %u\nlds_bytes %u\nlocal_size %u.%u.%u\n",
		program->instruction_count, program->word_count, program->used_regs, program->lds_bytes,
		program->local_size[0], program->local_size[1], program->local_size[2]);
	fprintf(file, "flags %u\nvarying_count %u\nattribute_registers %u\ncolor_outputs %#x\n",
		program->flags, program->varying_count, program->attribute_registers, program->color_outputs);
	fprintf(file, "push_bytes %u\ndynamic_used %#x\nscratch_bytes %u\n",
		program->push_bytes, program->dynamic_used, program->scratch_bytes);
	fprintf(file, "textures %u\nsamplers %u\nresources %u\nresource_bytes %u\n",
		program->bindings.texture_count, program->bindings.sampler_count,
		program->bindings.resource_count, program->bindings.resource_bytes);
	for (index = 0U; index < program->bindings.texture_count; index++)
		fprintf(file, "texture%u %u.%u.%u\n", index, program->bindings.texture[index].kind,
			program->bindings.texture[index].set, program->bindings.texture[index].binding);
	for (index = 0U; index < program->bindings.sampler_count; index++)
		fprintf(file, "sampler%u %u.%u.%u\n", index, program->bindings.sampler[index].kind,
			program->bindings.sampler[index].set, program->bindings.sampler[index].binding);
	for (index = 0U; index < program->bindings.resource_count; index++)
		fprintf(file, "resource%u %u.%u.%u.%u\n", index, program->bindings.resource[index].kind,
			program->bindings.resource[index].set, program->bindings.resource[index].binding,
			program->bindings.resource[index].offset);
	fclose(file);
	return 0;
}

/* Writes the model's register file of slot-major registers, one line per lane: known values or 0. */
static int
write_registers(
	const char *path,
	uint32_t nregs,
	uint32_t (*value)[32],
	uint8_t (*known)[32])
{
	FILE *file;
	uint32_t slot;
	uint32_t reg;
	uint32_t lane;

	file = fopen(path, "w");
	if (file == NULL)
		return 1;
	for (slot = 0U; slot < 4U; slot++) {
		for (reg = 0U; reg < nregs; reg++) {
			for (lane = 0U; lane < 32U; lane++) {
				if (slot == 0U && known[reg][lane] != 0U) {
					fprintf(file, "1%08x\n", value[reg][lane]);
				} else {
					fprintf(file, "000000000\n");
				}
			}
		}
	}
	fclose(file);
	return 0;
}

/* A xorshift step. */
static uint32_t
next_random(
	uint32_t *state)
{
	uint32_t x;

	x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

/* A random float of [low, high), as its bits. */
static uint32_t
random_float(
	uint32_t *state,
	float low,
	float high)
{
	float unit;

	unit = (float)(next_random(state) >> 8) / 16777216.0f;
	return as_bits(low + (high - low) * unit);
}

/* Finds the uniform of a set and binding. */
static int
find_uniform(
	const struct drv_gpu_shader_ir *ir,
	uint32_t set,
	uint32_t binding,
	uint32_t *uniform)
{
	uint32_t index;

	for (index = 0U; index < ir->uniform_count; index++) {
		if (ir->uniforms[index].set == set && ir->uniforms[index].binding == binding) {
			*uniform = index;
			return 0;
		}
	}
	return 1;
}

/*
 * Plays the executor: fills the constant bank (push constants, the
 * tables' addresses, the dynamic offsets), the resource table's entries
 * and the regions they describe, and the driver's table, as the program's
 * record asks; gives the reference the same contents.
 */
static int
bind_resources(
	struct test_case *test)
{
	static uint8_t push[128];
	const struct ghci_gsi1_program *program;
	const struct ghci_gsi1_resource *resource;
	uint32_t set2;
	uint32_t set3;
	uint32_t index;
	uint32_t uniform;
	uint32_t entry;
	uint32_t word;
	uint32_t data;
	uint32_t scratch;

	program = test->program;
	for (index = 0U; index < REF_UNIFORMS; index++) {
		test->region_of[index] = UINT32_MAX;
		test->words_of[index] = 0U;
	}
	for (index = 0U; index < MODEL_DATA_WORDS; index++)
		test->data[index] = next_random(&test->seed);
	memset(test->data, 0, MODEL_BANK_WORDS * 4U);

	/* Region 0: the resource table (set 2) and the driver's table (set 3), their addresses in the bank. */
	set2 = MODEL_BANK_WORDS;
	set3 = MODEL_BANK_WORDS + TABLE_SET3_WORD;
	memset(&test->data[set2], 0, REGION_BYTES);
	test->regions = 1U;
	test->data[BANK_SET(2U)] = MODEL_DATA_BASE + 4U * set2;
	test->data[BANK_SET(2U) + 1U] = 0U;
	test->data[BANK_SET(3U)] = MODEL_DATA_BASE + 4U * set3;
	test->data[BANK_SET(3U) + 1U] = 0U;
	if (program->bindings.resource_bytes > 4U * TABLE_SET3_WORD) {
		fprintf(stderr, "a resource table of %u bytes does not fit the test's\n", program->bindings.resource_bytes);
		return 1;
	}

	/* The push constants: bytes 0-95 in c0-c23, bytes 96-127 in c32-c39. */
	if (program->push_bytes > sizeof(push)) {
		fprintf(stderr, "%u bytes of push constants\n", program->push_bytes);
		return 1;
	}
	for (word = 0U; word < (program->push_bytes + 3U) / 4U; word++) {
		data = test->options.floats ? random_float(&test->seed, -8.0f, 8.0f) : next_random(&test->seed) % 1000U;
		if (word < 24U) {
			test->data[word] = data;
		} else {
			test->data[BANK_PUSH_HIGH + word - 24U] = data;
		}
		memcpy(push + 4U * word, &data, 4U);
	}
	test->run->push = push;
	test->run->push_size = program->push_bytes;

	/* The dynamic offsets of the layout's dynamic buffers. */
	for (index = 0U; index < test->options.compile.dynamic_count; index++)
		test->data[BANK_DYNAMIC + index] = DYNAMIC_SHIFT;

	/* The scratch memory's address in the driver's table, at the word of the program's stage. */
	if (program->scratch_bytes != 0U) {
		scratch = DRIVER_SCRATCH_COMPUTE;
		if (program->stage == DRV_GPU_STAGE_VERTEX)
			scratch = DRIVER_SCRATCH_VERTEX;
		if (program->stage == DRV_GPU_STAGE_FRAGMENT)
			scratch = DRIVER_SCRATCH_FRAGMENT;
		put_word(test->data, set3, scratch, 4U, MODEL_SCRATCH_BASE);
		put_word(test->data, set3, scratch + 4U, 4U, 0U);
		test->scratch_bytes = program->scratch_bytes;
	}

	/* The model has no texture unit. */
	if (program->bindings.texture_count != 0U || program->bindings.sampler_count != 0U) {
		fprintf(stderr, "a texture or sampler slot: the model has no texture unit\n");
		return 1;
	}

	/* Each entry of the resource table. */
	for (index = 0U; index < program->bindings.resource_count; index++) {
		resource = &program->bindings.resource[index];
		if (resource->offset % 16U != 0U || resource->element != 0U) {
			fprintf(stderr, "a resource table entry at %u, element %u\n", resource->offset, resource->element);
			return 1;
		}
		if (find_uniform(test->ir, resource->set, resource->binding, &uniform) != 0) {
			fprintf(stderr, "a resource table entry names no uniform of the module\n");
			return 1;
		}
		entry = set2 + resource->offset / 4U;
		if (resource->kind == GHCI_GSI1_RESOURCE_UNIFORM_BUFFER || resource->kind == GHCI_GSI1_RESOURCE_STORAGE_BUFFER) {
			if (bind_buffer(test, uniform, entry) != 0)
				return 1;
		} else if (resource->kind == GHCI_GSI1_RESOURCE_STORAGE_IMAGE ||
			   resource->kind == GHCI_GSI1_RESOURCE_STORAGE_TEXEL_BUFFER ||
			   resource->kind == GHCI_GSI1_RESOURCE_INPUT_ATTACHMENT) {
			if (bind_image(test, uniform, entry, resource->kind) != 0)
				return 1;
		} else {
			fprintf(stderr, "a resource table entry of kind %u the model cannot run\n", resource->kind);
			return 1;
		}
	}
	return 0;
}

/* Reports whether a buffer is one of the options' dynamic ones. */
static int
is_dynamic(
	const struct test_case *test,
	uint32_t uniform)
{
	uint32_t index;

	for (index = 0U; index < test->options.compile.dynamic_count; index++) {
		if (test->options.compile.dynamic[index].set == test->ir->uniforms[uniform].set &&
		    test->options.compile.dynamic[index].binding == test->ir->uniforms[uniform].binding)
			return 1;
	}
	return 0;
}

/* Writes a field of 1, 2 or 4 bytes at a byte offset from a data word. */
static void
put_word(
	uint32_t *data,
	uint32_t word,
	uint32_t offset,
	uint32_t bytes,
	uint32_t value)
{
	memcpy((uint8_t *)&data[word] + offset, &value, bytes);
}

/*
 * Gives a buffer a region (random words, or small floats) and its buffer
 * descriptor; a dynamic buffer's descriptor names the region's start and
 * its dynamic offset moves the program's view DYNAMIC_SHIFT bytes on.
 */
static int
bind_buffer(
	struct test_case *test,
	uint32_t uniform,
	uint32_t entry)
{
	uint32_t region;
	uint32_t word;
	uint32_t shift;

	if (test->regions >= (MODEL_DATA_WORDS - MODEL_BANK_WORDS) / REGION_WORDS)
		return 1;
	region = MODEL_BANK_WORDS + REGION_WORDS * test->regions;
	test->regions++;
	for (word = 0U; word < REGION_WORDS; word++) {
		if (test->options.floats) {
			test->data[region + word] = random_float(&test->seed, -8.0f, 8.0f);
		} else {
			test->data[region + word] = next_random(&test->seed) % 4096U;
		}
	}
	shift = 0U;
	if (is_dynamic(test, uniform) != 0)
		shift = DYNAMIC_SHIFT;
	test->region_of[uniform] = region + shift / 4U;
	test->words_of[uniform] = REGION_WORDS - shift / 4U;
	test->run->bytes[uniform] = malloc(REGION_BYTES);
	if (test->run->bytes[uniform] == NULL)
		return 1;
	memcpy(test->run->bytes[uniform], &test->data[region + shift / 4U], REGION_BYTES - shift);
	test->run->size[uniform] = REGION_BYTES - shift;
	put_word(test->data, entry, BUFFER_BASE, 4U, MODEL_DATA_BASE + 4U * region);
	put_word(test->data, entry, BUFFER_BASE + 4U, 4U, 0U);
	put_word(test->data, entry, BUFFER_SIZE, 4U, REGION_BYTES - shift);
	put_word(test->data, entry, BUFFER_STRIDE, 2U, 0U);
	put_word(test->data, entry, BUFFER_FORMAT, 2U, 0U);
	return 0;
}

/*
 * Gives a storage image, a storage texel buffer or an input attachment a
 * region: random texels in the format, its descriptor (a buffer
 * descriptor of the texel buffer, a texture descriptor of an image), and
 * the reference's image of the texels unpacked.
 */
static int
bind_image(
	struct test_case *test,
	uint32_t uniform,
	uint32_t entry,
	uint32_t kind)
{
	struct ref_image *image;
	uint32_t region;
	uint32_t format;
	uint32_t bytes;
	uint32_t texels;
	uint32_t index;
	uint8_t *memory;

	if (test->regions >= (MODEL_DATA_WORDS - MODEL_BANK_WORDS) / REGION_WORDS)
		return 1;
	region = MODEL_BANK_WORDS + REGION_WORDS * test->regions;
	test->regions++;
	test->region_of[uniform] = region;
	test->words_of[uniform] = REGION_WORDS;
	format = test->ir->uniforms[uniform].format;
	if (kind == GHCI_GSI1_RESOURCE_INPUT_ATTACHMENT)
		format = test->options.compile.input_attachment_format[test->ir->uniforms[uniform].input_attachment_index];
	bytes = format_bytes(format);
	if (bytes == 0U) {
		fprintf(stderr, "an image of format %u the test does not convert\n", format);
		return 1;
	}
	test->image_format[uniform] = format;
	image = &test->run->image[uniform];
	image->width = IMAGE_SIDE;
	image->height = IMAGE_SIDE;
	image->layers = REGION_BYTES / (IMAGE_SIDE * IMAGE_SIDE * bytes) >= 2U ? 2U : 1U;
	if (kind == GHCI_GSI1_RESOURCE_STORAGE_TEXEL_BUFFER) {
		image->width = REGION_BYTES / bytes;
		image->height = 1U;
		image->layers = 1U;
	}
	test->image_layers[uniform] = image->layers;
	texels = image->width * image->height * image->layers;
	image->texels = calloc(4U * texels, sizeof(uint32_t));
	if (image->texels == NULL)
		return 1;
	memory = (uint8_t *)&test->data[region];
	for (index = 0U; index < texels; index++)
		unpack_texel(format, memory + bytes * index, &image->texels[4U * index]);

	/* A texel buffer's buffer descriptor: its bytes and its texels' stride and format. */
	if (kind == GHCI_GSI1_RESOURCE_STORAGE_TEXEL_BUFFER) {
		put_word(test->data, entry, BUFFER_BASE, 4U, MODEL_DATA_BASE + 4U * region);
		put_word(test->data, entry, BUFFER_BASE + 4U, 4U, 0U);
		put_word(test->data, entry, BUFFER_SIZE, 4U, image->width * bytes);
		put_word(test->data, entry, BUFFER_STRIDE, 2U, bytes);
		put_word(test->data, entry, BUFFER_FORMAT, 2U, format);
		return 0;
	}

	/* An image's texture descriptor (the format and the levels share the depth's word, the dimension the samples'). */
	put_word(test->data, entry, TEXTURE_BASE, 4U, MODEL_DATA_BASE + 4U * region);
	put_word(test->data, entry, TEXTURE_BASE + 4U, 4U, 0U);
	put_word(test->data, entry, TEXTURE_WIDTH, 2U, IMAGE_SIDE);
	put_word(test->data, entry, TEXTURE_HEIGHT, 2U, IMAGE_SIDE);
	put_word(test->data, entry, TEXTURE_DEPTH, 2U, image->layers);
	put_word(test->data, entry, TEXTURE_FORMAT, 1U, format);
	put_word(test->data, entry, TEXTURE_LEVELS, 1U, 1U);
	put_word(test->data, entry, TEXTURE_ROW_PITCH, 4U, IMAGE_SIDE * bytes);
	put_word(test->data, entry, TEXTURE_SLICE_PITCH, 4U, IMAGE_SIDE * IMAGE_SIDE * bytes);
	put_word(test->data, entry, TEXTURE_SAMPLES, 1U, 1U);
	put_word(test->data, entry, TEXTURE_DIMENSION, 1U, DIMENSION_2D);
	put_word(test->data, entry, TEXTURE_DIMENSION + 1U, 2U, 0U);
	put_word(test->data, entry, TEXTURE_DIMENSION + 3U, 4U, 2U | (3U << 3) | (4U << 6) | (5U << 9));
	return 0;
}

/* The bytes of a texel of the formats the test converts; 0 for others. */
static uint32_t
format_bytes(
	uint32_t format)
{
	switch (format) {
	case 37U:
	case 41U:
	case 98U:
	case 99U:
	case 100U:
		return 4U;
	case 97U:
		return 8U;
	case 109U:
	case 107U:
		return 16U;
	default:
		return 0U;
	}
}

/* Unpacks a texel into the IR's four words. */
static void
unpack_texel(
	uint32_t format,
	const uint8_t *bytes,
	uint32_t *texel)
{
	uint32_t word;
	uint32_t channel;
	uint16_t half;

	texel[0] = 0U;
	texel[1] = 0U;
	texel[2] = 0U;
	texel[3] = as_bits(1.0f);
	if (format == 37U || format == 41U) {
		for (channel = 0U; channel < 4U; channel++) {
			texel[channel] = bytes[channel];
			if (format == 37U)
				texel[channel] = as_bits((float)bytes[channel] / 255.0f);
		}
		return;
	}
	if (format == 98U || format == 99U || format == 100U) {
		memcpy(&word, bytes, 4U);
		texel[0] = word;
		if (format != 100U)
			texel[3] = 1U;
		return;
	}
	if (format == 97U) {
		for (channel = 0U; channel < 4U; channel++) {
			memcpy(&half, bytes + 2U * channel, 2U);
			texel[channel] = as_bits(ref_half_value(half));
		}
		return;
	}
	for (channel = 0U; channel < 4U; channel++)
		memcpy(&texel[channel], bytes + 4U * channel, 4U);
}

/* Packs the IR's four words into a texel of the format, as Vulkan converts. */
static void
pack_texel(
	uint32_t format,
	const uint32_t *texel,
	uint8_t *bytes)
{
	uint32_t channel;
	uint16_t half;
	float value;

	if (format == 37U || format == 41U) {
		for (channel = 0U; channel < 4U; channel++) {
			if (format == 41U) {
				bytes[channel] = (uint8_t)texel[channel];
				continue;
			}
			value = as_float(texel[channel]);
			if (!(value > 0.0f))
				value = 0.0f;
			if (value > 1.0f)
				value = 1.0f;
			value = value * 255.0f;
			bytes[channel] = (uint8_t)nearbyintf(value);
		}
		return;
	}
	if (format == 98U || format == 99U || format == 100U) {
		memcpy(bytes, &texel[0], 4U);
		return;
	}
	if (format == 97U) {
		for (channel = 0U; channel < 4U; channel++) {
			half = (uint16_t)ref_half_bits(as_float(texel[channel]));
			memcpy(bytes + 2U * channel, &half, 2U);
		}
		return;
	}
	for (channel = 0U; channel < 4U; channel++)
		memcpy(bytes + 4U * channel, &texel[channel], 4U);
}

/* Writes the reference's images into a memory image, packed, and marks their words to compare. */
static void
store_images(
	struct test_case *test,
	uint32_t *memory)
{
	struct ref_image *image;
	uint32_t uniform;
	uint32_t bytes;
	uint32_t texels;
	uint32_t index;
	uint32_t word;

	for (uniform = 0U; uniform < test->ir->uniform_count && uniform < REF_UNIFORMS; uniform++) {
		image = &test->run->image[uniform];
		if (image->texels == NULL)
			continue;
		if (test->ir->uniforms[uniform].kind == DRV_GPU_IR_UNIFORM_INPUT_ATTACHMENT)
			continue;
		bytes = format_bytes(test->image_format[uniform]);
		texels = image->width * image->height * image->layers;
		for (index = 0U; index < texels; index++)
			pack_texel(test->image_format[uniform], &image->texels[4U * index],
				   (uint8_t *)&memory[test->region_of[uniform]] + bytes * index);
		for (word = 0U; word < (texels * bytes + 3U) / 4U; word++)
			test->compare[test->region_of[uniform] + word] = 1U;
	}
}

/* The float of a word's bits. */
static float
as_float(
	uint32_t bits)
{
	float number;

	memcpy(&number, &bits, sizeof(number));
	return number;
}

/* The bits of a float. */
static uint32_t
as_bits(
	float number)
{
	uint32_t bits;

	memcpy(&bits, &number, sizeof(bits));
	return bits;
}
