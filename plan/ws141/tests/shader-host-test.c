/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Actual Keiland SPIR-V is checked against an independent Mesa decoder and scalar source interpretation. */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "broadcom/common/v3d_device_info.h"
#include "broadcom/qpu/qpu_instr.h"
#include "drivers/gpu/bcm2711/shader.h"
#include "drivers/gpu/compiler/spirv.h"
#include <uapi/errno.h>
#include "userland/desktop/wayland/shaders.h"

/* One host-only bit conversion preserves exact scalar representations without aliasing violations. */
union scalar_bits {
	uint32_t bits;
	float number;
};

/* One test invocation interprets a lane; it makes no claim about hardware scheduling or SFU precision. */
struct shader_machine {
	uint32_t rf[64];
	uint32_t accumulators[6];
	uint32_t input[32];
	uint32_t push[24];
	uint32_t output[68];
	uint32_t tile[4];
	uint32_t texture[4];
	uint32_t texture_t;
	uint32_t texture_read;
	uint32_t tile_read;
	uint32_t tile_write;
	uint32_t varying_read;
	uint32_t uniform_read;
	uint32_t lookups;
	uint32_t configurations;
	uint32_t flag;
};

/* Allocation accounting observes actual parser/compiler cleanup without any production test control. */
static size_t allocations_live;

/* One host allocation attempt can be refused to exercise the ordinary unpublished-program unwind. */
static size_t allocation_attempt;
static size_t allocation_refusal;

static uint32_t float_bits(float number);
static float bits_float(uint32_t bits);
static uint32_t truth(int condition);
static uint32_t sample_component(uint32_t s, uint32_t t, uint32_t component);
static void source_program(const struct drv_gpu_shader_ir *ir, struct shader_machine *machine);
static uint32_t machine_uniform(const struct bcm2711_shader_binary *binary, struct shader_machine *machine);
static uint32_t machine_source(const struct v3d_device_info *device, const struct v3d_qpu_instr *instruction, const struct shader_machine *machine, enum v3d_qpu_mux mux);
static uint32_t machine_add(const struct v3d_qpu_instr *instruction, struct shader_machine *machine, uint32_t left, uint32_t right);
static void machine_write(struct shader_machine *machine, uint32_t address, uint32_t magic, uint32_t bits);
static void machine_program(const struct v3d_device_info *device, const struct bcm2711_shader_binary *binary, struct shader_machine *machine);
static void verify_panel_mode(const struct drv_gpu_shader_ir *generic, const struct drv_gpu_shader_ir *specialized, uint32_t mode);
static void glass_constants(uint32_t *push);
static void pipeline_key(const struct drv_gpu_shader_ir *ir, struct bcm2711_shader_key *key);
static void initialize_machine(struct shader_machine *machine, uint32_t fragment, uint32_t trial);
static void compare_number(uint32_t native, uint32_t expected);
static uint32_t half_bits(float number);
static float half_float(uint32_t half);
static void compare_half(uint32_t native, uint32_t expected);
static void verify_program(const struct v3d_device_info *device, const uint32_t *words, size_t word_count, const struct drv_gpu_shader_ir *ir, enum bcm2711_shader_stage stage, struct bcm2711_shader_key *key);
static void verify_hazards(const struct v3d_device_info *device, const struct bcm2711_shader_binary *binary);
static bool reads_register(const struct v3d_qpu_instr *instruction, uint32_t number);
static uint32_t unit_count(const struct v3d_device_info *device, const struct v3d_qpu_instr *instruction);
static void verify_allocation_refusal(const struct bcm2711_shader_key *key, const uint32_t *words, size_t word_count);
static void verify_graphics_profile(const struct bcm2711_shader_key *key);

/*
 * Supplies the ordinary kernel allocation API through the host allocator.
 */
void *
kern_calloc(
	size_t count,
	size_t bytes)
{
	void *pointer;

	/* One selected allocator failure enters the same cleanup path as a real kernel heap refusal. */
	allocation_attempt++;
	if (allocation_attempt == allocation_refusal)
		return NULL;

	/* The production compiler's allocations stay visible to host memory instrumentation. */
	pointer = calloc(count, bytes);
	if (pointer != NULL)
		allocations_live++;

	/* Exact ordinary allocation ownership is returned to production source. */
	return pointer;
}

/*
 * Supplies the shared parser's ordinary host allocation operation.
 */
void *
kern_malloc(
	size_t bytes)
{
	void *pointer;

	/* A selected heap refusal never allocates hidden test storage. */
	allocation_attempt++;
	if (allocation_attempt == allocation_refusal)
		return NULL;

	/* Only the allocation backend is replaced; no parser or compiler path is gated for testing. */
	pointer = malloc(bytes);
	if (pointer != NULL)
		allocations_live++;

	/* Exact ordinary allocation ownership is returned to the shared parser. */
	return pointer;
}

/*
 * Supplies the shared parser's growable host allocation operation.
 */
void *
kern_realloc(
	void *pointer,
	size_t bytes)
{
	void *grown;

	/* The shared parser never requests the ambiguous free-on-zero realloc convention. */
	assert(bytes != 0);
	allocation_attempt++;
	if (allocation_attempt == allocation_refusal)
		return NULL;

	/* Existing ownership and failure behavior are preserved by the host allocator. */
	grown = realloc(pointer, bytes);
	if (pointer == NULL && grown != NULL)
		allocations_live++;

	/* Failure preserves the old allocation exactly as the ordinary realloc contract requires. */
	return grown;
}

/*
 * Releases ordinary host allocation storage used by actual production source.
 */
void
kern_free(
	void *pointer)
{
	/* A null cleanup owns no storage and may be repeated along a partial allocation unwind. */
	if (pointer != NULL) {
		assert(allocations_live != 0);
		allocations_live--;
	}

	/* This releases host memory, never files or GPU storage. */
	free(pointer);
}

/*
 * Checks all actual Keiland graphics shaders and native pipeline variants.
 */
int
main(
	void)
{
	const uint32_t *modules[6] = {kwl_quad_vert, kwl_quad_frag, kwl_panel_vert, kwl_panel_frag, kwl_batch_vert, kwl_batch_frag};
	const size_t bytes[6] = {sizeof(kwl_quad_vert), sizeof(kwl_quad_frag), sizeof(kwl_panel_vert), sizeof(kwl_panel_frag), sizeof(kwl_batch_vert), sizeof(kwl_batch_frag)};
	struct drv_gpu_shader_ir *vertex;
	struct drv_gpu_shader_ir *fragment;
	struct drv_gpu_shader_ir *generic;
	struct drv_gpu_compile_diagnostic diagnostic;
	struct bcm2711_shader_key key;
	struct v3d_device_info device;
	uint32_t pair;
	uint32_t swap;
	uint32_t mode;
	int error;

	/* The unmodified fixed decoder interprets actual 4.2 machine words independently of production field construction. */
	memset(&device, 0, sizeof(device));
	device.ver = 42;
	device.has_accumulators = true;

	/* The production shader pairs use their real immutable SPIR-V arrays (the joined solid quads' too, ws211-p003). */
	for (pair = 0; pair < 3; pair++) {
		error = drv_gpu_shader_parse(modules[pair * 2], bytes[pair * 2] / 4, DRV_GPU_STAGE_VERTEX, &vertex, &diagnostic);
		assert(error == 0);
		error = drv_gpu_shader_parse(modules[pair * 2 + 1], bytes[pair * 2 + 1] / 4, DRV_GPU_STAGE_FRAGMENT, &fragment, &diagnostic);
		assert(error == 0);
		pipeline_key(fragment, &key);

		/* One actual quad profile exercises independent graphics admission and its metadata lifetime. */
		if (pair == 0)
			verify_graphics_profile(&key);

		/* Ordinary actual modules retain the existing native-allocation and scalar differential checks. */
		verify_allocation_refusal(&key, modules[pair * 2 + 1], bytes[pair * 2 + 1] / 4);
		verify_program(&device, modules[pair * 2], bytes[pair * 2] / 4, vertex, BCM2711_SHADER_COORDINATE, &key);
		verify_program(&device, modules[pair * 2], bytes[pair * 2] / 4, vertex, BCM2711_SHADER_VERTEX, &key);

		/* Both supported target channel orders are tested. */
		for (swap = 0; swap < 2; swap++) {
			key.swap_red_blue = swap;
			verify_program(&device, modules[pair * 2 + 1], bytes[pair * 2 + 1] / 4, fragment, BCM2711_SHADER_FRAGMENT, &key);
		}

		/* Parser ownership retires only after all native variants have been checked. */
		drv_gpu_shader_ir_free(vertex);
		drv_gpu_shader_ir_free(fragment);
	}

	/* Every specialized shader must agree with the generic shader at its actual mode. */
	error = drv_gpu_shader_parse(
		kwl_panel_frag,
		sizeof(kwl_panel_frag) / 4,
		DRV_GPU_STAGE_FRAGMENT,
		&generic,
		&diagnostic);
	assert(error == 0);

	/* Native varying interfaces may shrink when a mode needs only texture or position (mode 7 is the dark glass, ws211-p011). */
	for (mode = 0; mode < 8; mode++) {
		/* Parses each immutable variant and the matching shared vertex shader. */
		error = drv_gpu_shader_parse(kwl_panel_vert, sizeof(kwl_panel_vert) / 4, DRV_GPU_STAGE_VERTEX, &vertex, &diagnostic);
		assert(error == 0);
		error = drv_gpu_shader_parse(kwl_panel_variants[mode].words, kwl_panel_variants[mode].bytes / 4, DRV_GPU_STAGE_FRAGMENT, &fragment, &diagnostic);
		assert(error == 0);
		pipeline_key(fragment, &key);

		/* Independent scalar evaluation checks both original colors and unused-sample removal. */
		verify_panel_mode(generic, fragment, mode);
		verify_program(&device, kwl_panel_vert, sizeof(kwl_panel_vert) / 4, vertex, BCM2711_SHADER_COORDINATE, &key);
		verify_program(&device, kwl_panel_vert, sizeof(kwl_panel_vert) / 4, vertex, BCM2711_SHADER_VERTEX, &key);
		verify_program(&device, kwl_panel_variants[mode].words, kwl_panel_variants[mode].bytes / 4, fragment, BCM2711_SHADER_FRAGMENT, &key);

		/* Compiled and parsed owners retire independently for each mode. */
		drv_gpu_shader_ir_free(vertex);
		drv_gpu_shader_ir_free(fragment);
	}

	/* The comparison baseline lives until every specialized mode has been checked. */
	drv_gpu_shader_ir_free(generic);

	/* All compiler and parser objects must retire after the final completed program owner releases them. */
	assert(allocations_live == 0);

	/* This is scalar format/semantics evidence, not a physical QPU execution result. */
	puts("WS141 native shader host test: PASS (actual Keiland modules, independent decoder and scalar semantics)");
	return 0;
}

/* Converts a host reference number into exact IEEE scalar bits. */
static uint32_t
float_bits(
	float number)
{
	union scalar_bits scalar;

	/* A union avoids strict-aliasing assumptions in both reference interpreters. */
	scalar.number = number;

	/* The returned bits are not computed by production kernel floating point. */
	return scalar.bits;
}

/* Converts scalar bits into a host reference number. */
static float
bits_float(
	uint32_t bits)
{
	union scalar_bits scalar;

	/* The source and native interpreters share representations, but independently implement operations. */
	scalar.bits = bits;

	/* Host arithmetic is a reference for scalar meaning rather than an SFU accuracy specification. */
	return scalar.number;
}

/* Constructs the frontend's all-ones Boolean representation. */
static uint32_t
truth(
	int condition)
{
	/* The source representation is a mask, not the C integer one. */
	if (condition != 0)
		return 0xffffffffU;

	/* False selects no lane. */
	return 0;
}

/* Produces deterministic coordinate-dependent mock texture data, independently of both compiler paths. */
static uint32_t
sample_component(
	uint32_t s,
	uint32_t t,
	uint32_t component)
{
	float number;
	uint32_t bits;

	/* Nonconstant coordinate-dependent channels expose swapped coordinates and incorrect lookup result ordering. */
	number = 0.6f;
	if (component == 0)
		number = bits_float(s);
	if (component == 1)
		number = bits_float(t);
	if (component == 2)
		number = 0.25f + 0.1f * bits_float(s);
	bits = float_bits(number);

	/* No actual texture descriptor, filtering, derivative or memory behavior is simulated here. */
	return bits;
}

/* Independently interprets the shared parser's source scalar semantics before native register allocation. */
static void
source_program(
	const struct drv_gpu_shader_ir *ir,
	struct shader_machine *machine)
{
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t *values;
	uint32_t index;
	uint32_t component;
	uint32_t left;
	uint32_t right;
	uint32_t bits;
	float a;
	float b;

	/* The source interpreter owns a distinct scalar array and never observes native register assignment. */
	values = calloc(ir->value_count, sizeof(*values));
	assert(values != NULL);

	/* Source operations define exact scalar values even when hardware code uses several native instructions. */
	for (index = 0; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		left = values[instruction->src[0]];
		right = values[instruction->src[1]];
		a = bits_float(left);
		b = bits_float(right);
		bits = 0;

		/* Pure if-conversion markers preserve all arithmetic; final source selection decides visible output. */
		switch (instruction->op) {
		case DRV_GPU_IR_NOP:
		case DRV_GPU_IR_SKIP_BEGIN:
		case DRV_GPU_IR_SKIP_END:
			continue;
		case DRV_GPU_IR_CONST:
			bits = instruction->immediate;
			break;
		case DRV_GPU_IR_LOAD_INPUT:
			bits = machine->input[instruction->location * 2 + instruction->component];
			break;
		case DRV_GPU_IR_LOAD_PUSH:
			bits = machine->push[instruction->immediate / 4];
			break;
		case DRV_GPU_IR_STORE_OUTPUT:
			component = instruction->location * 4 + instruction->component;
			if (instruction->location == DRV_GPU_IR_LOCATION_POSITION)
				component = 64 + instruction->component;
			assert(component < 68);
			machine->output[component] = left;
			continue;
		case DRV_GPU_IR_SAMPLE:
			for (component = 0; component < 4; component++)
				values[instruction->dst + component] = sample_component(left, right, component);
			machine->lookups++;
			continue;
		case DRV_GPU_IR_FADD:
			bits = float_bits(a + b);
			break;
		case DRV_GPU_IR_FSUB:
			bits = float_bits(a - b);
			break;
		case DRV_GPU_IR_FMUL:
			bits = float_bits(a * b);
			break;
		case DRV_GPU_IR_RCP:
			bits = float_bits(1.0f / a);
			break;
		case DRV_GPU_IR_SQRT:
			bits = float_bits(sqrtf(a));
			break;
		case DRV_GPU_IR_FABS:
			bits = left & 0x7fffffffU;
			break;
		case DRV_GPU_IR_FMIN:
			bits = float_bits(fminf(a, b));
			break;
		case DRV_GPU_IR_FMAX:
			bits = float_bits(fmaxf(a, b));
			break;
		case DRV_GPU_IR_FLT:
			bits = truth(a < b);
			break;
		case DRV_GPU_IR_AND:
			bits = left & right;
			break;
		case DRV_GPU_IR_NOT:
			bits = ~left;
			break;
		case DRV_GPU_IR_SELECT:
			bits = values[instruction->src[2]];
			if (left != 0)
				bits = right;
			break;
		default:
			assert(0);
		}

		/* Source destinations are independent of all native storage and temporary registers. */
		values[instruction->dst] = bits;
	}

	/* The reference owns only host scalar memory. */
	free(values);
}

/* Supplies the compiled uniform interface through ordinary checked test bindings. */
static uint32_t
machine_uniform(
	const struct bcm2711_shader_binary *binary,
	struct shader_machine *machine)
{
	const struct bcm2711_shader_uniform *uniform;
	uint32_t bits;

	/* Every implicit or explicit native uniform transaction must consume exactly one compiled descriptor. */
	assert(machine->uniform_read < binary->uniform_count);
	uniform = &binary->uniforms[machine->uniform_read++];
	bits = uniform->bits;

	/* Binding identity is interpreted independently of native opcode and register construction. */
	switch (uniform->kind) {
	case BCM2711_SHADER_CONSTANT:
		break;
	case BCM2711_SHADER_PUSH:
		assert(uniform->offset / 4 < 24);
		bits = machine->push[uniform->offset / 4];
		break;
	case BCM2711_SHADER_TEXTURE:
		assert(uniform->set == 0 && uniform->binding == 0 && uniform->bits == 15);
		bits |= 0x00100000U;
		break;
	case BCM2711_SHADER_SAMPLER:
		assert(uniform->set == 0 && uniform->binding == 0 && uniform->bits == 1);
		bits |= 0x00200000U;
		break;
	case BCM2711_SHADER_VIEWPORT_X:
		bits = float_bits(640.0f * 256.0f);
		break;
	case BCM2711_SHADER_VIEWPORT_Y:
		bits = float_bits(400.0f * 256.0f);
		break;
	case BCM2711_SHADER_VIEWPORT_Z:
		bits = float_bits(1.0f);
		break;
	case BCM2711_SHADER_DEPTH_OFFSET:
		bits = 0;
		break;
	default:
		assert(0);
	}

	/* One descriptor has one hardware FIFO consumption. */
	return bits;
}

/* Reads actual decoded register muxes and uses the unchanged native small-constant decoder. */
static uint32_t
machine_source(
	const struct v3d_device_info *device,
	const struct v3d_qpu_instr *instruction,
	const struct shader_machine *machine,
	enum v3d_qpu_mux mux)
{
	uint32_t bits;
	bool accepted;

	/* Accumulator and physical read-port identities come exclusively from the independent actual decoder. */
	if (mux < V3D_QPU_MUX_A)
		return machine->accumulators[mux];
	if (mux == V3D_QPU_MUX_A)
		return machine->rf[instruction->raddr_a];
	if (!instruction->sig.small_imm_b)
		return machine->rf[instruction->raddr_b];

	/* A malformed small immediate cannot be replaced by a guessed numeric value. */
	accepted = v3d_qpu_small_imm_unpack(device, instruction->raddr_b, &bits);
	assert(accepted);

	/* This is the actual independently decoded bit pattern. */
	return bits;
}

/* Interprets actual decoded add operations, including comparison flags and indexed VPM effects. */
static uint32_t
machine_add(
	const struct v3d_qpu_instr *instruction,
	struct shader_machine *machine,
	uint32_t left,
	uint32_t right)
{
	float a;
	float b;
	uint32_t bits;

	/* Host reference arithmetic begins from independently decoded source muxes. */
	a = bits_float(left);
	b = bits_float(right);
	bits = 0;

	/* The native interpreter uses actual Mesa operation identities rather than production semantic enums. */
	switch (instruction->alu.add.op) {
	case V3D_QPU_A_NOP:
	case V3D_QPU_A_VPMWT:
		break;
	case V3D_QPU_A_FADD:
		bits = float_bits(a + b);
		break;
	case V3D_QPU_A_FSUB:
		bits = float_bits(a - b);
		break;
	case V3D_QPU_A_FMIN:
		bits = float_bits(fminf(a, b));
		break;
	case V3D_QPU_A_FMAX:
		bits = float_bits(fmaxf(a, b));
		break;
	case V3D_QPU_A_RECIP:
		bits = float_bits(1.0f / a);
		break;
	case V3D_QPU_A_RSQRT:
		bits = float_bits(1.0f / sqrtf(a));
		break;
	case V3D_QPU_A_AND:
		bits = left & right;
		break;
	case V3D_QPU_A_OR:
		bits = left | right;
		break;
	case V3D_QPU_A_XOR:
		bits = left ^ right;
		break;
	case V3D_QPU_A_NOT:
		bits = ~left;
		break;
	case V3D_QPU_A_FFLOOR:
		bits = float_bits(floorf(a));
		break;
	case V3D_QPU_A_FTOIZ:
		bits = (uint32_t)(int32_t)a;
		break;
	case V3D_QPU_A_VFPACK:
		/* Both inputs are whole floats; an unpacked input would read a half instead. */
		assert(instruction->alu.add.a.unpack == V3D_QPU_UNPACK_NONE);
		assert(instruction->alu.add.b.unpack == V3D_QPU_UNPACK_NONE);
		bits = half_bits(a) | (half_bits(b) << 16);
		break;
	case V3D_QPU_A_FCMP:
		if (instruction->flags.apf == V3D_QPU_PF_PUSHZ)
			machine->flag = truth(a == b);
		if (instruction->flags.apf == V3D_QPU_PF_PUSHN)
			machine->flag = truth(a < b);
		if (instruction->flags.apf == V3D_QPU_PF_PUSHC)
			machine->flag = truth(a <= b);
		break;
	case V3D_QPU_A_LDVPMV_IN:
		assert(left < 32);
		bits = machine->input[left];
		break;
	case V3D_QPU_A_STVPMV:
		assert(left < 68);
		machine->output[left] = right;
		break;
	default:
		fprintf(stderr, "unmodeled native add opcode %u\n", instruction->alu.add.op);
		assert(0);
	}

	/* Integer zero-test pushes use the computed bit pattern, while float comparisons supplied their own ordered flags. */
	if (instruction->alu.add.op != V3D_QPU_A_FCMP && instruction->flags.apf == V3D_QPU_PF_PUSHZ)
		machine->flag = truth(bits == 0);

	/* The result may be ignored by the native no-write destination. */
	return bits;
}

/* Applies actual physical or peripheral destinations, including texture FIFO and tile effects. */
static void
machine_write(
	struct shader_machine *machine,
	uint32_t address,
	uint32_t magic,
	uint32_t bits)
{
	uint32_t component;

	/* Physical register writes remain completely separate from native peripheral effects. */
	if (magic == 0) {
		assert(address < 64);
		machine->rf[address] = bits;
		return;
	}

	/* Only the peripherals actually emitted by these native graphics programs are modeled. */
	switch (address) {
	case V3D_QPU_WADDR_NOP:
		break;
	case V3D_QPU_WADDR_TMUT:
		machine->texture_t = bits;
		break;
	case V3D_QPU_WADDR_TMUS:
		assert(machine->configurations == 2 && machine->texture_read == 0);
		for (component = 0; component < 4; component++)
			machine->texture[component] = sample_component(bits, machine->texture_t, component);
		machine->lookups++;
		machine->configurations = 0;
		break;
	case V3D_QPU_WADDR_TLB:
		/* The default tile format takes two packed half-float pairs per pixel and no configuration uniform. */
		assert(machine->tile_write < 2);
		machine->output[machine->tile_write++] = bits;
		break;
	default:
		assert(0);
	}
}

/* Interprets native words using the unchanged independent decoder, rather than the compiler's operation records. */
static void
machine_program(
	const struct v3d_device_info *device,
	const struct bcm2711_shader_binary *binary,
	struct shader_machine *machine)
{
	struct v3d_qpu_instr instruction;
	uint64_t repacked;
	uint32_t index;
	uint32_t left;
	uint32_t right;
	uint32_t bits;
	uint32_t configuration;
	bool accepted;

	/* Every uploaded word must decode and re-encode exactly before its native behavior is interpreted. */
	for (index = 0; index < binary->code_count; index++) {
		accepted = v3d_qpu_instr_unpack(device, binary->code[index], &instruction);
		assert(accepted && instruction.type == V3D_QPU_INSTR_TYPE_ALU);
		accepted = v3d_qpu_instr_pack(device, &instruction, &repacked);
		assert(accepted && repacked == binary->code[index]);
		assert(instruction.flags.auf == V3D_QPU_UF_NONE && instruction.flags.muf == V3D_QPU_UF_NONE);
		left = machine_source(device, &instruction, machine, instruction.alu.add.a.mux);
		right = machine_source(device, &instruction, machine, instruction.alu.add.b.mux);
		bits = machine_add(&instruction, machine, left, right);

		/* VPM stores have an indexed effect and no ordinary register destination. */
		if (instruction.alu.add.op != V3D_QPU_A_NOP && instruction.alu.add.op != V3D_QPU_A_STVPMV)
			machine_write(machine, instruction.alu.add.waddr, instruction.alu.add.magic_write, bits);

		/* The independent decoded multiply instruction distinguishes bit moves from floating multiplication. */
		if (instruction.alu.mul.op != V3D_QPU_M_NOP) {
			left = machine_source(device, &instruction, machine, instruction.alu.mul.a.mux);
			right = machine_source(device, &instruction, machine, instruction.alu.mul.b.mux);
			bits = left;
			if (instruction.alu.mul.op == V3D_QPU_M_FMUL) {
				bits = float_bits(bits_float(left) * bits_float(right));
			} else if (instruction.alu.mul.op == V3D_QPU_M_FMOV) {
				/* This fixture's destination components are positive, normalized, exactly representable half floats. */
				if (instruction.alu.mul.a.unpack == V3D_QPU_UNPACK_H)
					left >>= 16;
				else
					assert(instruction.alu.mul.a.unpack == V3D_QPU_UNPACK_L);
				left &= 0xffffU;
				assert((left & 0x7c00U) != 0 && (left & 0x7c00U) != 0x7c00U);
				bits = ((left & 0x8000U) << 16) | (((left >> 10) & 31U) + 112U) << 23 | ((left & 1023U) << 13);
			} else {
				assert(instruction.alu.mul.op == V3D_QPU_M_MOV);
			}

			/* Lane flags select bit-preserving moves; neither delay NOPs nor uniform loads may change them. */
			accepted = true;
			if (instruction.flags.mc == V3D_QPU_COND_IFA && machine->flag == 0)
				accepted = false;
			if (instruction.flags.mc == V3D_QPU_COND_IFNA && machine->flag != 0)
				accepted = false;
			assert(instruction.flags.mc != V3D_QPU_COND_IFB && instruction.flags.mc != V3D_QPU_COND_IFNB);
			if (accepted)
				machine_write(machine, instruction.alu.mul.waddr, instruction.alu.mul.magic_write, bits);
		}

		/* Direct uniform transactions consume the immutable runtime binding stream. */
		if (instruction.sig.ldunifrf)
			machine->rf[instruction.sig_addr] = machine_uniform(binary, machine);

		/* Varying payload W and the separate constant coefficient are modeled independently of source LOAD_INPUT aliases. */
		if (instruction.sig.ldvary) {
			assert(machine->varying_read < binary->input_count);
			bits = machine->input[machine->varying_read++];
			machine->rf[instruction.sig_addr] = float_bits(bits_float(bits) / bits_float(machine->rf[0]));
			machine->accumulators[5] = 0;
		}

		/* Native TMU configuration must precede one triggered lookup and all four result reads. */
		if (instruction.sig.wrtmuc) {
			configuration = machine_uniform(binary, machine);
			if (machine->configurations == 0) {
				assert(configuration == 0x0010000fU);
			} else {
				assert(machine->configurations == 1 && configuration == 0x00200001U);
			}

			/* Exactly two native configuration transactions belong to the next triggered lookup. */
			machine->configurations++;
		}

		/* Four requested texture words must retire before the next native texture transaction. */
		if (instruction.sig.ldtmu) {
			assert(machine->texture_read < 4);
			machine->rf[instruction.sig_addr] = machine->texture[machine->texture_read++];
			if (machine->texture_read == 4)
				machine->texture_read = 0;
		}

		/* Blending belongs to the tile buffer, so no program reads the tile. */
		assert(!instruction.sig.ldtlb && !instruction.sig.ldtlbu);
	}

	/* Uniform and result FIFOs are fully accounted for by actual decoded hardware transactions. */
	assert(machine->uniform_read == binary->uniform_count);
	assert(machine->texture_read == 0 && machine->configurations == 0);
}

/* Derives the canonical fragment scalar interface independently of frontend declaration order. */
static void
pipeline_key(
	const struct drv_gpu_shader_ir *ir,
	struct bcm2711_shader_key *key)
{
	struct bcm2711_shader_component *varying;
	uint32_t location;
	uint32_t index;
	uint32_t component;

	/* Fragment declarations may be reversed in actual panel SPIR-V. */
	memset(key, 0, sizeof(*key));
	for (location = 0; location < 16; location++) {
		for (index = 0; index < ir->input_count; index++) {
			if (ir->inputs[index].location != location)
				continue;

			/* Each varying scalar preserves the actual interpolation qualifiers. */
			for (component = 0; component < ir->inputs[index].components; component++) {
				assert(key->varying_count < BCM2711_SHADER_INTERFACE_WORDS);
				varying = &key->varyings[key->varying_count++];
				varying->location = location;
				varying->component = component;
				varying->flat = ir->inputs[index].flat;
				varying->noperspective = ir->inputs[index].noperspective;
			}
		}
	}
}

/* Creates concrete finite draw inputs that exercise each panel shape branch and several fragment locations. */
static void
initialize_machine(
	struct shader_machine *machine,
	uint32_t fragment,
	uint32_t trial)
{
	uint32_t index;
	float number;

	/* Nonunit payload W exposes missing perspective multiplication in native varying preload. */
	memset(machine, 0, sizeof(*machine));
	machine->rf[0] = float_bits(2.0f);

	/* The quad's push layout covers rectangle, texture, box, color, shape and screen uniforms. */
	machine->push[0] = float_bits(-0.7f);
	machine->push[1] = float_bits(-0.5f);
	machine->push[2] = float_bits(0.4f);
	machine->push[3] = float_bits(0.6f);
	machine->push[4] = float_bits(0.1f);
	machine->push[5] = float_bits(0.2f);
	machine->push[6] = float_bits(0.8f);
	machine->push[7] = float_bits(0.9f);
	machine->push[8] = float_bits(100.0f);
	machine->push[9] = float_bits(80.0f);
	machine->push[10] = float_bits(200.0f);
	machine->push[11] = float_bits(120.0f);
	machine->push[12] = float_bits(0.4f);
	machine->push[13] = float_bits(0.6f);
	machine->push[14] = float_bits(0.8f);
	machine->push[15] = float_bits(0.75f);
	machine->push[16] = float_bits(12.0f);
	number = (float)(trial % 8) - 1.0f;
	machine->push[17] = float_bits(number);
	machine->push[18] = float_bits(2.0f);
	machine->push[19] = float_bits(0.0f);
	machine->push[20] = float_bits(1280.0f);
	machine->push[21] = float_bits(800.0f);
	machine->push[22] = float_bits(0.3f);
	machine->push[23] = float_bits(0.85f);

	/* Texture and pixel varyings vary independently across native/source comparisons. */
	machine->input[0] = float_bits(0.25f);
	machine->input[1] = float_bits(0.75f);
	if (fragment != 0) {
		machine->input[2] = float_bits(96.0f + 17.0f * (float)(trial / 8));
		machine->input[3] = float_bits(84.0f + 11.0f * (float)(trial / 8));
	}

	/* Prior tile data is neither opaque nor symmetric, exposing alpha errors and accidental red/blue swaps. */
	for (index = 0; index < 4; index++)
		machine->tile[index] = float_bits(0.125f + (float)index * 0.125f);
}

/* Compares scalar floating outputs with bounded tolerance for distinct host square-root evaluation paths. */
static void
compare_number(
	uint32_t native,
	uint32_t expected)
{
	float a;
	float b;
	float distance;
	float limit;
	int native_nan;
	int expected_nan;

	/* Exact equality accepts integers and preserves special-value bits without floating conversion. */
	if (native == expected)
		return;

	/* Host SFU approximation is deliberately not an asserted physical device accuracy bound. */
	a = bits_float(native);
	b = bits_float(expected);
	native_nan = isnan(a);
	expected_nan = isnan(b);
	if (native_nan != 0 && expected_nan != 0)
		return;
	distance = fabsf(a - b);
	limit = 0.00002f * (1.0f + fabsf(b));
	if (!(distance <= limit)) {
		fprintf(stderr, "native scalar %08x %.9g differs from source %08x %.9g\n", native, a, expected, b);
		assert(0);
	}
}

/* Compares each specialized body against the original shader at the same visible mode. */
static void
verify_panel_mode(
	const struct drv_gpu_shader_ir *generic,
	const struct drv_gpu_shader_ir *specialized,
	uint32_t mode)
{
	struct shader_machine original;
	struct shader_machine variant;
	uint32_t trial;
	uint32_t component;

	/* Glass includes both light and dark appearances; other modes have one exact selector. */
	for (trial = 0; trial < 32; trial++) {
		/*
		 * All source inputs agree, including nontrivial position, texture
		 * and opacity.  Light glass is mode 0, dark glass (mode -1) its own
		 * variant 7, white or not; both take the values glass.c works out
		 * once for the shape (ws211-p011).
		 */
		initialize_machine(&original, 1, trial);
		original.push[17] = float_bits((float)mode);
		if (mode == 7)
			original.push[17] = float_bits(-1.0f);
		if (mode == 0 || mode == 7) {
			if ((trial & 2) != 0) {
				original.push[12] = float_bits(1.0f);
				original.push[13] = float_bits(0.95f);
				original.push[14] = float_bits(0.92f);
			}
			original.push[18] = float_bits((float)(trial % 3) * 0.5f);
			glass_constants(original.push);
		}

		/* These evaluations share no native instruction or register representation. */
		variant = original;
		source_program(generic, &original);
		source_program(specialized, &variant);
		for (component = 0; component < 4; component++)
			compare_number(variant.output[component], original.output[component]);

		/* Texture-free shapes sample nothing, text/images/glass once, and blur five times. */
		if (mode == 1 || mode == 3 || mode == 4)
			assert(variant.lookups == 0);
		else if (mode == 6)
			assert(variant.lookups == 5);
		else
			assert(variant.lookups == 1);
	}

	/* Succeeded: all independent ownership or scalar comparisons passed. */
	return;
}

/* Fills a glass shape's per-shape values as glass.c's glass_shape_glass_constants does (ws211-p011). */
static void
glass_constants(
	uint32_t *push)
{
	float least;
	float flat;
	float height;

	least = bits_float(push[12]);
	if (bits_float(push[13]) < least)
		least = bits_float(push[13]);
	if (bits_float(push[14]) < least)
		least = bits_float(push[14]);
	push[4] = float_bits(least >= 0.9f ? 1.0f : 0.0f);
	push[5] = float_bits(bits_float(push[17]) <= -0.5f ? 1.0f : 0.0f);
	flat = bits_float(push[18]);
	if (flat < 0.0f)
		flat = 0.0f;
	if (flat > 1.0f)
		flat = 1.0f;
	push[6] = float_bits(0.05f * (1.0f - flat));
	height = bits_float(push[11]);
	if (height < 1.0f)
		height = 1.0f;
	push[7] = float_bits(1.0f / height);
}

/* Checks source meaning, native metadata, implicit FIFO use, output formats and all native program words. */
static void
verify_program(
	const struct v3d_device_info *device,
	const uint32_t *words,
	size_t word_count,
	const struct drv_gpu_shader_ir *ir,
	enum bcm2711_shader_stage stage,
	struct bcm2711_shader_key *key)
{
	struct bcm2711_shader_binary *binary;
	struct bcm2711_shader_diagnostic diagnostic;
	struct shader_machine source;
	struct shader_machine native;
	struct v3d_qpu_instr last;
	uint32_t trial;
	uint32_t index;
	uint32_t component;
	uint32_t temporary;
	uint32_t expected;
	float number;
	bool accepted;
	int error;

	/* Actual production compiler source supplies each independent native variant. */
	error = bcm2711_shader_compile(words, word_count, stage, key, &binary, &diagnostic);
	if (error != 0) {
		fprintf(stderr, "compile stage %u error %d at %u: %s\n", stage, error, diagnostic.instruction, diagnostic.reason);
		assert(0);
	}

	/* A final switch and both explicit no-operation delay slots must be inside the actual owned code extent. */
	assert(binary->code_count >= 3);
	if (binary->threads == 4) {
		assert(stage == BCM2711_SHADER_FRAGMENT && binary->registers_used == 32);
	} else {
		assert(binary->threads == 2 && binary->registers_used == 64);
	}
	accepted = v3d_qpu_instr_unpack(device, binary->code[binary->code_count - 3], &last);
	assert(accepted && last.sig.thrsw);
	assert(binary->code[binary->code_count - 2] == UINT64_C(0x3c003186bb800000));
	assert(binary->code[binary->code_count - 1] == UINT64_C(0x3c003186bb800000));

	/* The shortened schedule still keeps every 4.2 timing rule. */
	verify_hazards(device, binary);

	/* Panel mode and position variations expose several if-converted source paths and native register lifetimes. */
	for (trial = 0; trial < 32; trial++) {
		initialize_machine(&source, stage == BCM2711_SHADER_FRAGMENT, trial);
		native = source;

		/* The native FIFO packs only the inputs retained by this fragment variant. */
		if (stage == BCM2711_SHADER_FRAGMENT) {
			for (index = 0; index < key->varying_count; index++) {
				component = key->varyings[index].location * 2 + key->varyings[index].component;
				native.input[index] = source.input[component];
			}
		}

		/* The physical tile representation follows the selected target channel order. */
		if (key->swap_red_blue != 0) {
			temporary = native.tile[0];
			native.tile[0] = native.tile[2];
			native.tile[2] = temporary;
		}

		/* The source and machine interpreters consume independent storage and operation representations. */
		source_program(ir, &source);
		machine_program(device, binary, &native);
		assert(source.lookups == native.lookups);

		/* Native fragment color is compared after actual blend and target-channel-order choices. */
		if (stage == BCM2711_SHADER_FRAGMENT) {
			assert(native.tile_write == 2);
			assert(native.varying_read == binary->input_count);
			for (index = 0; index < 4; index++) {
				component = index;
				if (key->swap_red_blue != 0) {
					if (index == 0)
						component = 2;
					if (index == 2)
						component = 0;
				}

				/* The physical channel carries the source colour unchanged; the tile buffer blends it. */
				expected = source.output[component];

				/* Native register reuse, target swapping and half packing must preserve the independently interpreted source color. */
				compare_half((native.output[index / 2] >> (16 * (index % 2))) & 0xffffU, half_bits(bits_float(expected)));
			}

			/* Fragment variants have no vertex VPM header to compare. */
			continue;
		}

		/* Coordinate/bin exports all clip scalars, while render exports depth/W followed by canonical varyings. */
		component = 0;
		if (stage == BCM2711_SHADER_COORDINATE) {
			for (index = 0; index < 4; index++)
				compare_number(native.output[index], source.output[64 + index]);
			component = 4;
		} else {
			compare_number(native.output[2], source.output[66]);
			compare_number(native.output[3], float_bits(1.0f));
			for (index = 0; index < key->varying_count; index++) {
				temporary = key->varyings[index].location * 4 + key->varyings[index].component;
				compare_number(native.output[4 + index], source.output[temporary]);
			}
		}

		/* Real Keiland vertices have clip W one; native XY still must follow 4.2 floor-to-fixed conversion. */
		number = floorf(bits_float(source.output[64]) * 640.0f * 256.0f);
		assert(native.output[component] == (uint32_t)(int32_t)number);
		number = floorf(bits_float(source.output[65]) * 400.0f * 256.0f);
		assert(native.output[component + 1] == (uint32_t)(int32_t)number);
	}

	/* The caller exercises ordinary program ownership after every completed native variant. */
	bcm2711_shader_binary_free(binary);
}

/* Checks deep declaration graphs and stage changes through the public compiler, retaining the actual Keiland source body. */
static void
verify_graphics_profile(
	const struct bcm2711_shader_key *key)
{
	struct bcm2711_shader_binary *binary;
	struct bcm2711_shader_diagnostic diagnostic;
	uint32_t *words;
	size_t source_count;
	size_t offset;
	size_t prefix;
	size_t entry;
	size_t variable;
	size_t position;
	size_t baseline;
	uint32_t opcode;
	uint32_t count;
	uint32_t scalar_type;
	uint32_t scalar;
	uint32_t previous_type;
	uint32_t previous_constant;
	uint32_t type;
	uint32_t identity;
	uint32_t index;
	int error;

	/* Locate actual declarations without relying on generated shader offsets or IDs. */
	source_count = sizeof(kwl_quad_vert) / 4;
	prefix = 0;
	entry = 0;
	variable = 0;
	scalar_type = 0;
	scalar = 0;
	offset = 5;
	while (offset < source_count) {
		count = kwl_quad_vert[offset] >> 16;
		opcode = kwl_quad_vert[offset] & 0xffffU;
		assert(count != 0 && count <= source_count - offset);

		/* A real float constant supplies the scalar leaf of the additional aggregate graph. */
		if (opcode == 22U)
			scalar_type = kwl_quad_vert[offset + 1];
		if (opcode == 43U && kwl_quad_vert[offset + 1] == scalar_type)
			scalar = kwl_quad_vert[offset + 2];
		if (opcode == 15U)
			entry = offset;
		if (opcode == 59U)
			variable = offset;
		if (opcode == 54U) {
			prefix = offset;
			break;
		}

		/* Advance only over complete immutable source instructions while locating the module/function boundary. */
		offset += count;
	}

	/* The fixture adds ordinary nested structure constants, leaving the source's actual executable shader unchanged. */
	assert(prefix != 0 && entry != 0 && variable != 0);
	assert(scalar_type != 0 && scalar != 0);
	words = malloc((source_count + 2048U * 7U) * sizeof(*words));
	assert(words != NULL);
	memcpy(words, kwl_quad_vert, prefix * sizeof(*words));
	previous_type = scalar_type;
	previous_constant = scalar;
	for (index = 0; index < 2048U; index++) {
		/* Each new structure has one member of the preceding type and one constant of that exact type. */
		position = prefix + index * 7U;
		type = kwl_quad_vert[3] + index * 2U;
		identity = type + 1U;
		words[position] = (3U << 16) | 30U;
		words[position + 1] = type;
		words[position + 2] = previous_type;
		words[position + 3] = (4U << 16) | 44U;
		words[position + 4] = type;
		words[position + 5] = identity;
		words[position + 6] = previous_constant;
		previous_type = type;
		previous_constant = identity;
	}

	/* Deep aggregate expansion is refused before the shared parser allocates an IR or recurses through any constant. */
	memcpy(words + prefix + 2048U * 7U, kwl_quad_vert + prefix, (source_count - prefix) * sizeof(*words));
	words[3] = kwl_quad_vert[3] + 4096U;
	baseline = allocations_live;
	binary = (void *)1;
	error = bcm2711_shader_compile(words, source_count + 2048U * 7U, BCM2711_SHADER_VERTEX, key, &binary, &diagnostic);
	assert(error == ENOTSUP && binary == NULL && allocations_live == baseline);
	assert(diagnostic.instruction == prefix + 8U * 7U + 3U);

	/* A same-ID redefinition that could mutate an earlier constant edge is refused independently of nesting depth. */
	words[prefix + 5] = scalar;
	error = bcm2711_shader_compile(words, source_count + 2048U * 7U, BCM2711_SHADER_VERTEX, key, &binary, &diagnostic);
	assert(error == EINVAL && binary == NULL && allocations_live == baseline);
	assert(diagnostic.instruction == prefix + 3U);

	/* Eight ordinary aggregate edges followed by the scalar leaf remain admitted and compile the unchanged actual shader. */
	words[prefix + 5] = kwl_quad_vert[3] + 1U;
	words[3] = kwl_quad_vert[3] + 16U;
	memcpy(words + prefix + 8U * 7U, kwl_quad_vert + prefix, (source_count - prefix) * sizeof(*words));
	error = bcm2711_shader_compile(words, source_count + 8U * 7U, BCM2711_SHADER_VERTEX, key, &binary, &diagnostic);
	assert(error == 0 && binary != NULL);
	bcm2711_shader_binary_free(binary);
	assert(allocations_live == baseline);

	/* A compute entry cannot overwrite the requested graphics stage before a Workgroup variable is decoded. */
	memcpy(words, kwl_quad_vert, sizeof(kwl_quad_vert));
	words[entry + 1] = 5U;
	error = bcm2711_shader_compile(words, source_count, BCM2711_SHADER_VERTEX, key, &binary, &diagnostic);
	assert(error == ENOTSUP && binary == NULL && allocations_live == baseline);
	assert(diagnostic.instruction == entry);

	/* Graphics entry points cannot route a malformed Workgroup variable into compute-only recursive layout. */
	memcpy(words, kwl_quad_vert, sizeof(kwl_quad_vert));
	words[variable + 3] = 4U;
	error = bcm2711_shader_compile(words, source_count, BCM2711_SHADER_VERTEX, key, &binary, &diagnostic);
	assert(error == ENOTSUP && binary == NULL && allocations_live == baseline);
	assert(diagnostic.instruction == variable);

	/* Preflight metadata OOM owns no IR, native program, or diagnostic pointer into freed storage. */
	allocation_attempt = 0;
	allocation_refusal = 1;
	error = bcm2711_shader_compile(kwl_quad_vert, source_count, BCM2711_SHADER_VERTEX, key, &binary, &diagnostic);
	assert(error == ENOMEM && binary == NULL && allocations_live == baseline);
	allocation_refusal = 0;
	free(words);

	/* These are actual compiler refusal and lifetime results; no physical GPU scheduling is modeled. */
	puts("WS141 native graphics stage/deep constant graph/redefinition/preflight OOM: PASS");
}

/* Verifies each native program allocation refusal leaves no leaked frontend or partial native ownership. */
static void
verify_allocation_refusal(
	const struct bcm2711_shader_key *key,
	const uint32_t *words,
	size_t word_count)
{
	struct bcm2711_shader_binary *binary;
	struct bcm2711_shader_diagnostic diagnostic;
	size_t baseline;
	size_t attempts;
	size_t refusal;
	int error;

	/* A successful real compile locates the final native-owner, liveness, code and uniform allocations. */
	baseline = allocations_live;
	allocation_attempt = 0;
	allocation_refusal = 0;
	error = bcm2711_shader_compile(words, word_count, BCM2711_SHADER_FRAGMENT, key, &binary, &diagnostic);
	assert(error == 0);
	attempts = allocation_attempt;
	bcm2711_shader_binary_free(binary);
	assert(allocations_live == baseline && attempts >= 5);

	/* Every frontend and native allocation fails independently without retaining partial ownership. */
	for (refusal = 1; refusal <= attempts; refusal++) {
		allocation_attempt = 0;
		allocation_refusal = refusal;
		binary = (void *)1;
		error = bcm2711_shader_compile(words, word_count, BCM2711_SHADER_FRAGMENT, key, &binary, &diagnostic);
		assert(error != 0 && binary == NULL);
		assert(allocations_live == baseline);
	}

	/* All following host comparisons use the ordinary allocation path. */
	allocation_refusal = 0;
}

/*
 * Checks the 4.2 timing rules the compiler's schedule must keep.
 *
 * The rules are hardware facts recorded by the MIT Mesa validator and
 * scheduler: a special-function result is not read, and no other special
 * function starts, in the two instructions after it; the instruction after a
 * varying load neither reads accumulator five nor loads a uniform into it;
 * a thread switch's two delay slots start no special function or varying
 * load; and one instruction talks to at most one of the TMU, special
 * function, TLB, VPM and synchronization units.
 */
static void
verify_hazards(
	const struct v3d_device_info *device,
	const struct bcm2711_shader_binary *binary)
{
	struct v3d_qpu_instr instruction;
	struct v3d_qpu_instr later;
	uint32_t index;
	uint32_t next;
	uint32_t destination;
	bool accepted;
	bool special;

	/* Every instruction is checked against the ones that follow it. */
	for (index = 0; index < binary->code_count; index++) {
		accepted = v3d_qpu_instr_unpack(device, binary->code[index], &instruction);
		assert(accepted);
		assert(instruction.type == V3D_QPU_INSTR_TYPE_ALU);
		assert(unit_count(device, &instruction) <= 1);

		/* A four-thread program's threads each see only rf0 to rf31. */
		if (binary->threads == 4) {
			if (v3d_qpu_uses_mux(&instruction, V3D_QPU_MUX_A))
				assert(instruction.raddr_a < 32);
			if (v3d_qpu_uses_mux(&instruction, V3D_QPU_MUX_B) && !instruction.sig.small_imm_b)
				assert(instruction.raddr_b < 32);
			if (instruction.alu.add.op != V3D_QPU_A_NOP && !instruction.alu.add.magic_write)
				assert(instruction.alu.add.waddr < 32);
			if (instruction.alu.mul.op != V3D_QPU_M_NOP && !instruction.alu.mul.magic_write)
				assert(instruction.alu.mul.waddr < 32);
			if (v3d_qpu_sig_writes_address(device, &instruction.sig) && !instruction.sig_magic)
				assert(instruction.sig_addr < 32);
		}

		/* A special function's register result and unit stay untouched for two instructions. */
		special = v3d_qpu_instr_is_sfu(&instruction);
		if (special) {
			destination = instruction.alu.add.waddr;
			for (next = index + 1; next <= index + 2 && next < binary->code_count; next++) {
				accepted = v3d_qpu_instr_unpack(device, binary->code[next], &later);
				assert(accepted);
				assert(!v3d_qpu_instr_is_sfu(&later));
				assert(!v3d_qpu_uses_mux(&later, V3D_QPU_MUX_R4));
				if (!instruction.alu.add.magic_write)
					assert(!reads_register(&later, destination));
			}
		}

		/* Accumulator five receives a varying's constant term one instruction after its load. */
		if (instruction.sig.ldvary && index + 1 < binary->code_count) {
			accepted = v3d_qpu_instr_unpack(device, binary->code[index + 1], &later);
			assert(accepted);
			assert(!v3d_qpu_uses_mux(&later, V3D_QPU_MUX_R5));
			assert(!later.sig.ldunif && !later.sig.ldunifa);
		}

		/* A thread switch's delay slots run before the switch and start nothing that would land in the other thread. */
		if (instruction.sig.thrsw) {
			for (next = index + 1; next <= index + 2 && next < binary->code_count; next++) {
				accepted = v3d_qpu_instr_unpack(device, binary->code[next], &later);
				assert(accepted);
				assert(!v3d_qpu_instr_is_sfu(&later));
				assert(!later.sig.ldvary);
			}
		}
	}
}

/* Tells whether an instruction's ALUs read one physical register through either read port. */
static bool
reads_register(
	const struct v3d_qpu_instr *instruction,
	uint32_t number)
{
	/* Port A is read through the A multiplexer. */
	if (v3d_qpu_uses_mux(instruction, V3D_QPU_MUX_A) && instruction->raddr_a == number)
		return true;

	/* Port B holds a register unless it carries a small immediate. */
	if (v3d_qpu_uses_mux(instruction, V3D_QPU_MUX_B) &&
	    !instruction->sig.small_imm_b &&
	    instruction->raddr_b == number)
		return true;

	/* Neither port names the register. */
	return false;
}

/* Counts the TMU, special-function, TLB, VPM and synchronization transactions one instruction starts. */
static uint32_t
unit_count(
	const struct v3d_device_info *device,
	const struct v3d_qpu_instr *instruction)
{
	uint32_t count;

	/* Signals that read a unit. */
	count = 0;
	if (instruction->sig.ldtmu)
		count++;
	if (instruction->sig.ldtlb)
		count++;
	if (instruction->sig.ldtlbu)
		count++;
	if (instruction->sig.ldvpm)
		count++;

	/* An add ALU write to a unit's magic address. */
	if (instruction->alu.add.op != V3D_QPU_A_NOP && instruction->alu.add.magic_write) {
		if (v3d_qpu_magic_waddr_is_tmu(device, instruction->alu.add.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_sfu(instruction->alu.add.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_vpm(instruction->alu.add.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_tlb(instruction->alu.add.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_tsy(instruction->alu.add.waddr))
			count++;
	}

	/* A mul ALU write to a unit's magic address. */
	if (instruction->alu.mul.op != V3D_QPU_M_NOP && instruction->alu.mul.magic_write) {
		if (v3d_qpu_magic_waddr_is_tmu(device, instruction->alu.mul.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_sfu(instruction->alu.mul.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_vpm(instruction->alu.mul.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_tlb(instruction->alu.mul.waddr))
			count++;
		if (v3d_qpu_magic_waddr_is_tsy(instruction->alu.mul.waddr))
			count++;
	}

	/* Reports the transactions this one instruction starts. */
	return count;
}

/* Rounds one float to the nearest half float, as the native pack does. */
static uint32_t
half_bits(
	float number)
{
	_Float16 half;
	uint16_t bits;

	/* The host's conversion rounds to nearest even. */
	half = (_Float16)number;
	memcpy(&bits, &half, sizeof(bits));

	/* Reports the sixteen-bit pattern. */
	return bits;
}

/* Widens one half float's bits to a float. */
static float
half_float(
	uint32_t half)
{
	_Float16 value;
	uint16_t bits;

	/* Reinterprets the low sixteen bits. */
	bits = (uint16_t)half;
	memcpy(&value, &bits, sizeof(value));

	/* Reports the exact widened value. */
	return (float)value;
}

/* Compares two packed half-float channels within one half-float rounding step. */
static void
compare_half(
	uint32_t native,
	uint32_t expected)
{
	float a;
	float b;
	float limit;

	/* Exact bits need no tolerance. */
	if (native == expected)
		return;

	/* Host and native arithmetic may land on neighbouring halves before packing. */
	a = half_float(native);
	b = half_float(expected);
	limit = 0.001f * (1.0f + fabsf(b));
	if (!(fabsf(a - b) <= limit)) {
		fprintf(stderr, "native half %04x %.6g differs from source %04x %.6g\n", native, a, expected, b);
		assert(0);
	}
}
