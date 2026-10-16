/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: vertex and pixel programs compiled by the amdrdna compiler,
 * run on the GFX10 interpreter and on the IR reference with the same
 * random inputs, their exports compared.
 *
 * usage: gfx10-graphics-test OUTDIR [--trials N] MODULE.(vert|frag).spv...
 *
 * A vertex program runs 40 vertices (two waves) of two instances; its
 * fetched locations come from vertex buffers of random formats (1 to 4
 * float or integer channels) through the descriptors
 * amdrdna_gfx10_vertex_descriptor() makes.  A pixel program runs one wave
 * with random coverage, barycentrics, positions, faces and attributes of
 * four primitives.  The reference is given what the hardware would
 * deliver, computed the way the interpreter's interpolation computes it.
 */

#include "drivers/gpu/amdrdna/compiler/gfx10-compile.h"
#include "drivers/gpu/amdrdna/compiler/gfx10-program.h"
#include "drivers/gpu/compiler/spirv.h"
#include "plan/ws039/tests/gfx10-machine.h"
#include "plan/ws039/tests/gfx10-reference.h"
#include "plan/ws039/tests/gfx10-texture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TRIALS		16U

/* The trials of each module (--trials). */
static unsigned test_trials = TEST_TRIALS;
#define TEST_BUFFER_BYTES	4096U
#define TEST_TABLE_BYTES	4096U
#define TEST_VERTICES		40U
#define TEST_PRIMITIVES		4U

void *kern_malloc(size_t size);
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

static int test_module(const char *outdir, const char *path);
static int test_variant(const char *outdir, const char *name, const struct drv_gpu_shader_ir *ir, const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, uint32_t export_primitive_id);
static int vertex_trial(const struct drv_gpu_shader_ir *ir, const struct amdrdna_gfx10_program *program, uint32_t *state);
static int pixel_trial(const struct drv_gpu_shader_ir *ir, const struct amdrdna_gfx10_program *program, uint32_t *state);
static void setup_textures(const struct drv_gpu_shader_ir *ir, const struct amdrdna_gfx10_program *program, struct machine_memory *memory, uint8_t *table_bytes, uint32_t *state);
static void reference_sample(void *context, uint32_t set, uint32_t binding, const struct texture_request *request, uint32_t *out);
static void setup_uniforms(const struct drv_gpu_shader_ir *ir, const struct amdrdna_gfx10_program *program, struct machine_memory *memory, struct reference_memory *reference, uint8_t *table, uint32_t *state);
static int compare_slot(const char *what, uint32_t lane, uint32_t channel, uint32_t gpu, uint32_t ref);
static int compare_storage(const struct drv_gpu_shader_ir *ir);
static uint32_t random_word(uint32_t *state);
static float random_float(uint32_t *state);
static uint32_t float_bits(float value);
static uint32_t half_word(uint32_t bits);
static uint32_t *read_file(const char *path, size_t *words);

/* The buffers of one trial. */
static uint8_t uniform_bytes[REFERENCE_UNIFORMS][TEST_BUFFER_BYTES];
static uint8_t reference_storage[REFERENCE_UNIFORMS][TEST_BUFFER_BYTES];
static uint8_t vertex_bytes[AMDRDNA_GFX10_LOCATIONS][TEST_BUFFER_BYTES];
static uint8_t table[TEST_TABLE_BYTES];
static uint8_t push[256];
static float attributes[TEST_PRIMITIVES][32][4][3];
static struct machine_graphics graphics;

/* The sampled images of one trial: their texels and descriptors, by the program's texture slot. */
static uint8_t texture_bytes[AMDRDNA_GFX10_TEXTURE_MAX][65536];
static uint32_t texture_image[AMDRDNA_GFX10_TEXTURE_MAX][8];
static uint32_t texture_sampler[AMDRDNA_GFX10_TEXTURE_MAX][4];
static const struct amdrdna_gfx10_program *texture_program;
static struct reference_graphics reference_state;

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

int
main(
	int argc,
	char **argv)
{
	int failed;
	int index;

	if (argc < 3) {
		fprintf(stderr, "usage: %s OUTDIR [--trials N] MODULE.(vert|frag).spv...\n", argv[0]);
		return 2;
	}
	index = 2;
	if (argc > 4 && strcmp(argv[2], "--trials") == 0) {
		test_trials = (unsigned)strtoul(argv[3], NULL, 10);
		index = 4;
	}
	failed = 0;
	for (; index < argc; index++)
		failed |= test_module(argv[1], argv[index]);
	return failed;
}

/* Compiles one module and runs its trials. */
static int
test_module(
	const char *outdir,
	const char *path)
{
	char name[4096];
	const char *base;
	char *dot;
	uint32_t *words;
	size_t word_count;
	enum drv_gpu_shader_stage stage;
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_compile_diagnostic parse_diagnostic;
	int error;

	base = strrchr(path, '/');
	base = base != NULL ? base + 1 : path;
	snprintf(name, sizeof(name), "%s", base);
	dot = strstr(name, ".spv");
	if (dot != NULL)
		*dot = '\0';
	stage = strstr(name, ".frag") != NULL ? DRV_GPU_STAGE_FRAGMENT : DRV_GPU_STAGE_VERTEX;

	words = read_file(path, &word_count);
	if (words == NULL) {
		printf("FAIL %s: cannot read the module\n", name);
		return 1;
	}
	ir = NULL;
	memset(&parse_diagnostic, 0, sizeof(parse_diagnostic));
	error = drv_gpu_shader_parse(words, word_count, stage, &ir, &parse_diagnostic);
	if (error != 0) {
		printf("FAIL %s: the frontend refused it (%s)\n", name, parse_diagnostic.reason);
		free(words);
		return 1;
	}
	/* The module as it is; a vertex module also exporting the primitive number. */
	error = test_variant(outdir, name, ir, words, word_count, stage, 0U);
	if (error == 0 && stage == DRV_GPU_STAGE_VERTEX)
		error = test_variant(outdir, name, ir, words, word_count, stage, 1U);
	free(words);
	drv_gpu_shader_ir_free(ir);
	return error;
}

/* Compiles one module with the options and runs its trials. */
static int
test_variant(
	const char *outdir,
	const char *module,
	const struct drv_gpu_shader_ir *ir,
	const uint32_t *words,
	size_t word_count,
	enum drv_gpu_shader_stage stage,
	uint32_t export_primitive_id)
{
	char name[4200];
	char hex_path[8200];
	char *dot;
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_diagnostic diagnostic;
	struct amdrdna_gfx10_compile_options options;
	struct amdrdna_gfx10_registers registers;
	uint32_t state;
	uint32_t index;
	uint32_t expected;
	unsigned trial;
	int error;
	FILE *hex;

	snprintf(name, sizeof(name), "%s%s", module, export_primitive_id != 0U ? "+primitive" : "");
	memset(&options, 0, sizeof(options));
	options.export_primitive_id = export_primitive_id;
	program = NULL;
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = amdrdna_gfx10_compile(words, word_count, stage, &options, &program, &diagnostic);
	if (error != 0) {
		printf("FAIL %s: the compiler refused it at %u (%s)\n", name, diagnostic.instruction, diagnostic.reason);
		return 1;
	}

	/* The code for the disassembler (the dots of the name made dashes). */
	for (dot = name; *dot != '\0'; dot++) {
		if (*dot == '.' || *dot == '+')
			*dot = '-';
	}
	snprintf(hex_path, sizeof(hex_path), "%s/%s.hex", outdir, name);
	hex = fopen(hex_path, "w");
	if (hex != NULL) {
		for (index = 0U; index < program->code_words; index++) {
			fprintf(hex, "0x%02x 0x%02x 0x%02x 0x%02x\n",
				program->code[index] & 0xffU, (program->code[index] >> 8) & 0xffU,
				(program->code[index] >> 16) & 0xffU, program->code[index] >> 24);
		}
		fclose(hex);
	}

	/* The register values: the ordered vector memory counter, a vertex program's v0 .. v3, parameter count and primitive number, a pixel program's inputs and colours (four 16-bit floats). */
	amdrdna_gfx10_program_registers(program, &registers);
	error = 0;
	if (((registers.inputs >> 1) & 0x1fU) != program->user_sgprs)
		error = 1;
	if (stage == DRV_GPU_STAGE_VERTEX) {
		if (((registers.counts >> 24) & 3U) != 3U || (registers.counts & (1U << 27)) == 0U || (registers.position_formats & 0xfU) != 4U)
			error = 1;
		if (program->parameter_outputs == 0U && registers.vertex_export_config != 0x80U)
			error = 1;
		if (program->writes_point_size != 0U && (registers.vertex_output_control & (1U << 16)) == 0U)
			error = 1;
		if (registers.primitive_id_enable != export_primitive_id || program->exports_primitive_id != export_primitive_id)
			error = 1;
	} else {
		if (registers.pixel_input_enable != program->pixel_inputs || (registers.pixel_input_enable & 2U) == 0U)
			error = 1;
		if ((registers.counts & (1U << 25)) == 0U)
			error = 1;
		if ((registers.interpolant_control & (1U << 15)) == 0U)
			error = 1;
		for (index = 0U; index < 8U; index++) {
			if (((registers.color_formats >> (4U * index)) & 0xfU) != (((program->color_outputs >> index) & 1U) != 0U ? 4U : 0U))
				error = 1;
		}
		for (index = 0U; index < registers.attribute_count; index++) {
			expected = index;
			if (index == program->point_coord_attribute)
				expected = 0x20U | (1U << 17);
			else if (index == program->primitive_id_attribute)
				expected = AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER | (1U << 10);
			else if (((program->flat_inputs >> index) & 1U) != 0U)
				expected |= 1U << 10;
			if (registers.attribute_control[index] != expected)
				error = 1;
		}
	}
	if (error != 0) {
		printf("FAIL %s: register values\n", name);
		amdrdna_gfx10_program_free(program);
		return 1;
	}

	/* The trials. */
	error = 0;
	state = 0x9e3779b9U;
	for (trial = 0U; trial < test_trials && error == 0; trial++) {
		if (stage == DRV_GPU_STAGE_VERTEX) {
			error = vertex_trial(ir, program, &state);
		} else {
			error = pixel_trial(ir, program, &state);
		}
	}
	if (error == 0) {
		printf("PASS %s: %u trials, %u words, %u VGPRs, %u SGPRs\n",
		       name, test_trials, program->code_words, program->vgpr_count, program->sgpr_count);
	} else {
		printf("FAIL %s (trial %u)\n", name, trial - 1U);
	}
	amdrdna_gfx10_program_free(program);
	return error;
}

/* Makes a random image and sampler for each sampled image, and their descriptors in the table. */
static void
setup_textures(
	const struct drv_gpu_shader_ir *ir,
	const struct amdrdna_gfx10_program *program,
	struct machine_memory *memory,
	uint8_t *table_bytes,
	uint32_t *state)
{
	uint64_t address;
	uint32_t slot;
	uint32_t index;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	float number;

	(void)ir;
	texture_program = program;
	for (slot = 0U; slot < program->texture_used; slot++) {
		width = 8U << (random_word(state) % 3U);
		height = 4U << (random_word(state) % 3U);
		format = (random_word(state) & 1U) != 0U ? TEXTURE_FORMAT_FLOAT4 : TEXTURE_FORMAT_UNORM8X4;
		if (format == TEXTURE_FORMAT_FLOAT4) {
			for (index = 0U; index < sizeof(texture_bytes[slot]) / 4U; index++) {
				number = random_float(state);
				memcpy(&texture_bytes[slot][index * 4U], &number, 4U);
			}
		} else {
			for (index = 0U; index < sizeof(texture_bytes[slot]); index++)
				texture_bytes[slot][index] = (uint8_t)random_word(state);
		}
		address = machine_add_region(memory, texture_bytes[slot], sizeof(texture_bytes[slot]));
		texture_descriptors(texture_image[slot], texture_sampler[slot], address, width, height, format,
				    (int)(random_word(state) & 3U), (int)(random_word(state) & 1U), random_word(state) & 7U);
		memcpy(table_bytes + program->textures[slot].table_offset, texture_image[slot], 32U);
		memcpy(table_bytes + program->textures[slot].table_offset + 32U, texture_sampler[slot], 16U);
	}
}

/* The reference's sample: the same texture unit, by the slot of (set, binding). */
static void
reference_sample(
	void *context,
	uint32_t set,
	uint32_t binding,
	const struct texture_request *request,
	uint32_t *out)
{
	uint32_t slot;

	(void)context;
	for (slot = 0U; slot < texture_program->texture_used; slot++) {
		if (texture_program->textures[slot].set == set && texture_program->textures[slot].binding == binding) {
			texture_sample(texture_bytes[slot], texture_image[slot], texture_sampler[slot], request, out);
			return;
		}
	}
	memset(out, 0, 16U);
}

/* Fills the uniform buffers and push constants, and their descriptors in the table. */
static void
setup_uniforms(
	const struct drv_gpu_shader_ir *ir,
	const struct amdrdna_gfx10_program *program,
	struct machine_memory *memory,
	struct reference_memory *reference,
	uint8_t *table_bytes,
	uint32_t *state)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	uint64_t address;
	uint32_t index;
	uint32_t slot;
	uint32_t word;
	float number;

	memset(reference, 0, sizeof(*reference));
	for (index = 0U; index < ir->uniform_count && index < REFERENCE_UNIFORMS; index++) {
		uniform = &ir->uniforms[index];
		for (word = 0U; word < TEST_BUFFER_BYTES / 4U; word++) {
			number = random_float(state);
			memcpy(&uniform_bytes[index][word * 4U], &number, 4U);
		}
		address = machine_add_region(memory, uniform_bytes[index], TEST_BUFFER_BYTES);
		reference->bytes[index] = uniform_bytes[index];
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE) {
			/* A storage buffer: the reference writes its own copy, compared afterwards. */
			memcpy(reference_storage[index], uniform_bytes[index], TEST_BUFFER_BYTES);
			reference->bytes[index] = reference_storage[index];
		}
		reference->size[index] = TEST_BUFFER_BYTES;
		for (slot = 0U; slot < program->uniform_used; slot++) {
			if (uniform->kind == DRV_GPU_IR_UNIFORM_BLOCK &&
			    program->uniforms[slot].set == uniform->set && program->uniforms[slot].binding == uniform->binding)
				amdrdna_gfx10_raw_buffer_descriptor(address, TEST_BUFFER_BYTES, (uint32_t *)(table_bytes + program->uniforms[slot].table_offset));
		}
		for (slot = 0U; slot < program->storage_used; slot++) {
			if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE &&
			    program->storage[slot].set == uniform->set && program->storage[slot].binding == uniform->binding)
				amdrdna_gfx10_raw_buffer_descriptor(address, TEST_BUFFER_BYTES, (uint32_t *)(table_bytes + program->storage[slot].table_offset));
		}
	}
	for (index = 0U; index + 4U <= sizeof(push); index += 4U) {
		number = random_float(state);
		memcpy(&push[index], &number, 4U);
	}
	memcpy(table_bytes, push, ir->push_bytes);
	reference->push = push;
	reference->push_size = ir->push_bytes;
}

/* Runs 40 vertices of two instances on both; 1 when an export differs. */
static int
vertex_trial(
	const struct drv_gpu_shader_ir *ir,
	const struct amdrdna_gfx10_program *program,
	uint32_t *state)
{
	static const uint32_t float_formats[4] = { AMDRDNA_GFX10_VERTEX_FLOAT1, AMDRDNA_GFX10_VERTEX_FLOAT2, AMDRDNA_GFX10_VERTEX_FLOAT3, AMDRDNA_GFX10_VERTEX_FLOAT4 };
	static const uint32_t uint_formats[4] = { AMDRDNA_GFX10_VERTEX_UINT1, AMDRDNA_GFX10_VERTEX_UINT2, AMDRDNA_GFX10_VERTEX_UINT3, AMDRDNA_GFX10_VERTEX_UINT4 };
	struct machine_memory memory;
	struct machine_dispatch dispatch;
	struct machine_result result;
	struct reference_memory reference;
	uint32_t components[AMDRDNA_GFX10_LOCATIONS];
	uint32_t stride[AMDRDNA_GFX10_LOCATIONS];
	int integer[AMDRDNA_GFX10_LOCATIONS];
	uint64_t address;
	uint32_t base_vertex;
	uint32_t first_instance;
	uint32_t instance_mask;
	uint32_t instance;
	uint32_t first;
	uint32_t location;
	uint32_t lane;
	uint32_t channel;
	uint32_t record;
	uint32_t slot;
	uint32_t target;
	uint32_t word;
	const char *why;
	int undefined;
	int failed;

	memset(&memory, 0, sizeof(memory));
	memset(table, 0, sizeof(table));
	machine_add_region(&memory, table, TEST_TABLE_BYTES);
	setup_uniforms(ir, program, &memory, &reference, table, state);
	setup_textures(ir, program, &memory, table, state);

	/* The vertex buffers: a random format and stride per location, random contents. */
	base_vertex = random_word(state) % 20U;
	first_instance = random_word(state) % 5U;
	instance_mask = random_word(state) & program->fetched_locations;
	for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
		if ((program->fetched_locations & (1U << location)) == 0U)
			continue;
		components[location] = 1U + random_word(state) % 4U;
		integer[location] = (random_word(state) & 3U) == 0U;
		stride[location] = 4U * components[location] + 4U * (random_word(state) % 3U);
		for (word = 0U; word < TEST_BUFFER_BYTES / 4U; word++) {
			uint32_t bits = integer[location] ? random_word(state) % 1000U : float_bits(random_float(state));

			memcpy(&vertex_bytes[location][word * 4U], &bits, 4U);
		}
		address = machine_add_region(&memory, vertex_bytes[location], TEST_BUFFER_BYTES);
		amdrdna_gfx10_vertex_descriptor(address,
						stride[location],
						TEST_BUFFER_BYTES / stride[location],
						integer[location] ? uint_formats[components[location] - 1U] : float_formats[components[location] - 1U],
						components[location],
						(uint32_t *)(table + program->vertex_table_offset + 16U * location));
	}

	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.code = program->code;
	dispatch.code_words = program->code_words;
	dispatch.table = memory.regions[0].address;
	dispatch.vgpr_count = program->vgpr_count;

	/* Two instances, two waves each. */
	failed = 0;
	for (instance = 0U; instance < 2U && failed == 0; instance++) {
		for (first = 0U; first < TEST_VERTICES && failed == 0; first += 32U) {
			memset(&graphics, 0, sizeof(graphics));
			graphics.user[0] = base_vertex;
			graphics.user[1] = first_instance;
			graphics.user[2] = instance_mask;
			graphics.first_vertex_id = first;
			graphics.instance_id = instance;
			graphics.lanes = TEST_VERTICES - first < 32U ? TEST_VERTICES - first : 32U;
			for (lane = 0U; lane < 32U; lane++)
				graphics.primitive_id[lane] = random_word(state) % 1000U;
			machine_run_graphics(&dispatch, &memory, 0, &graphics, &result);
			if (result.error != NULL) {
				printf("  GFX10: %s at word %u (0x%08x)\n", result.error, result.error_pc, result.error_word);
				return 1;
			}

			/* The reference is given each lane's fetched channels, as the descriptors deliver them. */
			memset(&reference_state, 0, sizeof(reference_state));
			reference_state.sample = reference_sample;
			reference_state.lanes = graphics.lanes;
			for (lane = 0U; lane < graphics.lanes; lane++) {
				reference_state.run[lane] = 1U;
				reference_state.vertex_index[lane] = base_vertex + first + lane;
				reference_state.instance_index[lane] = first_instance + instance;
				for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
					if ((program->fetched_locations & (1U << location)) == 0U)
						continue;
					record = (instance_mask & (1U << location)) != 0U ? first_instance + instance : base_vertex + first + lane;
					for (channel = 0U; channel < 4U; channel++) {
						if (channel < components[location]) {
							memcpy(&reference_state.input[lane][location][channel], &vertex_bytes[location][record * stride[location] + 4U * channel], 4U);
						} else if (channel == 3U) {
							reference_state.input[lane][location][channel] = integer[location] ? 1U : 0x3f800000U;
						} else {
							reference_state.input[lane][location][channel] = 0U;
						}
					}
				}
			}
			why = reference_run_graphics(ir, &reference, &reference_state, &undefined);
			if (why != NULL) {
				printf("  reference: %s\n", why);
				return 1;
			}
			if (undefined != 0)
				continue;

			/* Every channel the reference wrote: the position, the point size, each parameter. */
			for (lane = 0U; lane < graphics.lanes && failed == 0; lane++) {
				for (slot = 0U; slot < REFERENCE_SLOTS && failed == 0; slot++) {
					target = AMDRDNA_GFX10_EXPORT_PARAMETER + slot;
					if (slot == REFERENCE_SLOT_POSITION)
						target = AMDRDNA_GFX10_EXPORT_POSITION;
					if (slot == REFERENCE_SLOT_POINT_SIZE)
						target = AMDRDNA_GFX10_EXPORT_POSITION + 1U;
					for (channel = 0U; channel < 4U && failed == 0; channel++) {
						if (reference_state.written[lane][slot][channel] == 0U)
							continue;
						if ((graphics.export_channels[target] & (1U << channel)) == 0U ||
						    (graphics.export_lanes[target] & (1U << lane)) == 0U) {
							printf("  slot %u channel %u of lane %u was not exported\n", slot, channel, lane);
							failed = 1;
							break;
						}
						failed = compare_slot("vertex", lane, channel + 4U * slot,
								      graphics.exported[target][lane][channel],
								      reference_state.output[lane][slot][channel]);
					}
				}
			}

			/* The primitive number, when the program exports it. */
			for (lane = 0U; program->exports_primitive_id != 0U && lane < graphics.lanes && failed == 0; lane++) {
				target = AMDRDNA_GFX10_EXPORT_PARAMETER + AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER;
				if ((graphics.export_channels[target] & 1U) == 0U || (graphics.export_lanes[target] & (1U << lane)) == 0U) {
					printf("  the primitive number of lane %u was not exported\n", lane);
					failed = 1;
					break;
				}
				failed = compare_slot("primitive", lane, 0U, graphics.exported[target][lane][0], graphics.primitive_id[lane]);
			}

			/* The last position export is marked last; position 0 has the valid mask bit. */
			if (failed == 0 && (graphics.export_flags[AMDRDNA_GFX10_EXPORT_POSITION] & 4U) == 0U) {
				printf("  position 0 without the valid mask bit\n");
				failed = 1;
			}
		}
	}
	return failed;
}

/* Runs one wave of pixels on both; 1 when a colour or the pixels alive differ. */
static int
pixel_trial(
	const struct drv_gpu_shader_ir *ir,
	const struct amdrdna_gfx10_program *program,
	uint32_t *state)
{
	struct machine_memory memory;
	struct machine_dispatch dispatch;
	struct machine_result result;
	struct reference_memory reference;
	const struct drv_gpu_shader_ir_io *input;
	const float *vertices;
	uint32_t alive;
	uint32_t exported_lanes;
	uint32_t want;
	uint32_t lane;
	uint32_t location;
	uint32_t channel;
	uint32_t index;
	uint32_t last;
	uint32_t slot;
	float i;
	float j;
	float value;
	const char *why;
	int undefined;
	int noperspective;

	memset(&memory, 0, sizeof(memory));
	memset(table, 0, sizeof(table));
	machine_add_region(&memory, table, TEST_TABLE_BYTES);
	setup_uniforms(ir, program, &memory, &reference, table, state);
	setup_textures(ir, program, &memory, table, state);

	/* The wave's inputs. */
	memset(&graphics, 0, sizeof(graphics));
	graphics.coverage = random_word(state) | 1U;
	for (index = 0U; index < TEST_PRIMITIVES; index++) {
		for (location = 0U; location < 32U; location++) {
			for (channel = 0U; channel < 4U; channel++) {
				attributes[index][location][channel][0] = random_float(state);
				attributes[index][location][channel][1] = random_float(state);
				attributes[index][location][channel][2] = random_float(state);
			}
		}
	}
	graphics.attribute = (const float (*)[32][4][3])attributes;
	for (lane = 0U; lane < 32U; lane += 4U) {
		index = random_word(state) % TEST_PRIMITIVES;
		for (channel = 0U; channel < 4U; channel++)
			graphics.primitive[lane + channel] = index;
	}
	for (lane = 0U; lane < 32U; lane++) {
		graphics.perspective[lane][0] = (float)(random_word(state) % 1024U) / 1024.0f;
		graphics.perspective[lane][1] = (float)(random_word(state) % 1024U) / 1024.0f;
		graphics.linear[lane][0] = (float)(random_word(state) % 1024U) / 1024.0f;
		graphics.linear[lane][1] = (float)(random_word(state) % 1024U) / 1024.0f;
		graphics.position[lane][0] = (float)(random_word(state) % 640U) + 0.5f;
		graphics.position[lane][1] = (float)(random_word(state) % 480U) + 0.5f;
		graphics.position[lane][2] = (float)(random_word(state) % 1000U) / 1000.0f;
		graphics.position[lane][3] = 0.5f + (float)(random_word(state) % 100U) / 10.0f;
		graphics.face[lane] = (random_word(state) & 1U) != 0U ? 1.0f : -1.0f;
	}

	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.code = program->code;
	dispatch.code_words = program->code_words;
	dispatch.table = memory.regions[0].address;
	dispatch.vgpr_count = program->vgpr_count;
	dispatch.pixel_inputs = program->pixel_inputs;
	machine_run_graphics(&dispatch, &memory, 1, &graphics, &result);
	if (result.error != NULL) {
		printf("  GFX10: %s at word %u (0x%08x)\n", result.error, result.error_pc, result.error_word);
		return 1;
	}

	/* The reference is given each lane's interpolated attributes, computed as the interpolation does. */
	memset(&reference_state, 0, sizeof(reference_state));
	reference_state.sample = reference_sample;
	reference_state.lanes = 32U;
	for (lane = 0U; lane < 32U; lane++) {
		reference_state.run[lane] = (graphics.coverage >> lane) & 1U;
		for (location = 0U; location < 32U; location++) {
			input = NULL;
			for (index = 0U; index < ir->input_count; index++) {
				if (ir->inputs[index].location == location)
					input = &ir->inputs[index];
			}
			noperspective = input != NULL && input->noperspective != 0U;
			i = noperspective ? graphics.linear[lane][0] : graphics.perspective[lane][0];
			j = noperspective ? graphics.linear[lane][1] : graphics.perspective[lane][1];
			for (channel = 0U; channel < 4U; channel++) {
				vertices = attributes[graphics.primitive[lane]][location][channel];
				if (input != NULL && input->flat != 0U) {
					value = vertices[0];
				} else {
					value = fmaf(vertices[2] - vertices[0], j, fmaf(vertices[1] - vertices[0], i, vertices[0]));
				}
				reference_state.input[lane][location][channel] = float_bits(value);
			}
		}
		reference_state.frag_coord[lane][0] = float_bits(graphics.position[lane][0]);
		reference_state.frag_coord[lane][1] = float_bits(graphics.position[lane][1]);
		reference_state.frag_coord[lane][2] = float_bits(graphics.position[lane][2]);
		reference_state.frag_coord[lane][3] = float_bits(1.0f / graphics.position[lane][3]);
		reference_state.front_facing[lane] = graphics.face[lane] > 0.0f ? 0xffffffffU : 0U;

		/* The point sprite coordinate the hardware makes in its attribute, interpolated; the primitive number flat. */
		if (program->point_coord_attribute != AMDRDNA_GFX10_NO_ATTRIBUTE) {
			for (channel = 0U; channel < 2U; channel++) {
				vertices = attributes[graphics.primitive[lane]][program->point_coord_attribute][channel];
				value = fmaf(vertices[2] - vertices[0], graphics.perspective[lane][1],
					     fmaf(vertices[1] - vertices[0], graphics.perspective[lane][0], vertices[0]));
				reference_state.point_coord[lane][channel] = float_bits(value);
			}
		}
		if (program->primitive_id_attribute != AMDRDNA_GFX10_NO_ATTRIBUTE)
			reference_state.primitive_id[lane] = float_bits(attributes[graphics.primitive[lane]][program->primitive_id_attribute][0][0]);
	}
	why = reference_run_graphics(ir, &reference, &reference_state, &undefined);
	if (why != NULL) {
		printf("  reference: %s\n", why);
		return 1;
	}
	if (undefined != 0)
		return 0;

	/* The storage buffers the alive lanes wrote (the helper lanes write nothing). */
	if (compare_storage(ir) != 0)
		return 1;

	/* The pixels alive: covered and not discarded. */
	alive = 0U;
	for (lane = 0U; lane < 32U; lane++) {
		if (reference_state.run[lane] != 0U && reference_state.killed[lane] == 0U)
			alive |= 1U << lane;
	}

	/* No colour: one null export marked last and valid. */
	if (program->color_outputs == 0U || alive == 0U) {
		if ((graphics.export_flags[AMDRDNA_GFX10_EXPORT_NOTHING] & 7U) != 7U) {
			printf("  no last valid null export (alive 0x%08x)\n", alive);
			return 1;
		}
		if (graphics.export_lanes[AMDRDNA_GFX10_EXPORT_NOTHING] != alive) {
			printf("  null export of lanes 0x%08x, alive 0x%08x\n", graphics.export_lanes[AMDRDNA_GFX10_EXPORT_NOTHING], alive);
			return 1;
		}
		return 0;
	}

	/* Each colour of each alive lane; the last one marked last and valid. */
	last = 0U;
	for (location = 0U; location < 8U; location++) {
		if ((program->color_outputs & (1U << location)) == 0U)
			continue;
		last = location;
		exported_lanes = graphics.export_lanes[AMDRDNA_GFX10_EXPORT_COLOR + location];
		if (exported_lanes != alive) {
			printf("  colour %u exported for lanes 0x%08x, alive 0x%08x\n", location, exported_lanes, alive);
			return 1;
		}
		slot = location;
		if (location == 1U && program->dual_source != 0U)
			slot = REFERENCE_SLOT_SECOND_COLOR;
		for (lane = 0U; lane < 32U; lane++) {
			if ((alive & (1U << lane)) == 0U)
				continue;
			for (channel = 0U; channel < 4U; channel++) {
				if (reference_state.written[lane][slot][channel] == 0U)
					continue;
				want = reference_state.output[lane][slot][channel];
				if ((graphics.export_flags[AMDRDNA_GFX10_EXPORT_COLOR + location] & 8U) != 0U)
					want = half_word(want);
				if (compare_slot("colour", lane, 4U * location + channel,
						 graphics.exported[AMDRDNA_GFX10_EXPORT_COLOR + location][lane][channel], want) != 0)
					return 1;
			}
		}
	}
	if ((graphics.export_flags[AMDRDNA_GFX10_EXPORT_COLOR + last] & 7U) != 7U) {
		printf("  the last colour is not marked last and valid\n");
		return 1;
	}
	return 0;
}

/* Compares each storage buffer the GFX10 run wrote with the reference's; 1 when a word differs. */
static int
compare_storage(
	const struct drv_gpu_shader_ir *ir)
{
	uint32_t index;
	uint32_t word;
	uint32_t gpu;
	uint32_t ref;

	for (index = 0U; index < ir->uniform_count && index < REFERENCE_UNIFORMS; index++) {
		if (ir->uniforms[index].kind != DRV_GPU_IR_UNIFORM_STORAGE)
			continue;
		for (word = 0U; word < TEST_BUFFER_BYTES / 4U; word++) {
			memcpy(&gpu, &uniform_bytes[index][word * 4U], 4U);
			memcpy(&ref, &reference_storage[index][word * 4U], 4U);
			if (gpu != ref) {
				printf("  storage %u word %u: GFX10 0x%08x, reference 0x%08x\n", index, word, gpu, ref);
				return 1;
			}
		}
	}
	return 0;
}

/* Compares one exported word; 1 when it differs. */
static int
compare_slot(
	const char *what,
	uint32_t lane,
	uint32_t channel,
	uint32_t gpu,
	uint32_t ref)
{
	if (gpu == ref)
		return 0;
	printf("  %s lane %u word %u: GFX10 0x%08x, reference 0x%08x\n", what, lane, channel, gpu, ref);
	return 1;
}

static uint32_t
random_word(
	uint32_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return *state;
}

/* A small float: a multiple of 1/16 in -4 .. 4. */
static float
random_float(
	uint32_t *state)
{
	return (float)((int32_t)(random_word(state) % 129U) - 64) / 16.0f;
}

static uint32_t
float_bits(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, 4U);
	return bits;
}

static uint32_t *
read_file(
	const char *path,
	size_t *words)
{
	FILE *file;
	long size;
	uint32_t *data;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size + 4U);
	if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(data);
		return NULL;
	}
	fclose(file);
	*words = (size_t)size / 4U;
	return data;
}

/* A float's word as a compressed colour export carries it: rounded toward zero to a 16-bit float, and back. */
static uint32_t
half_word(
	uint32_t bits)
{
	_Float16 half;
	uint16_t code;
	float value;
	float back;
	float back_size;
	float value_size;

	/* The nearest 16-bit float; a NaN as it is. */
	memcpy(&value, &bits, sizeof(value));
	if (value != value)
		return bits;
	half = (_Float16)value;
	memcpy(&code, &half, sizeof(code));

	/* One step toward zero when the nearest is farther from zero. */
	back_size = fabsf((float)half);
	value_size = fabsf(value);
	if ((code & 0x7fffU) != 0U && back_size > value_size)
		code--;

	/* Back to a float's word. */
	memcpy(&half, &code, sizeof(half));
	back = (float)half;
	memcpy(&bits, &back, sizeof(bits));
	return bits;
}
