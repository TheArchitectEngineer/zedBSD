/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269 p010 (the compiler): every shader of the inventory
 * (the compositor's, vkdemo's, vkcompute's, p009's and this test's own)
 * is read into the intermediate form, and the ones with a known meaning
 * are run on the interpreter and checked against the same arithmetic
 * written in C.  The back end's simulation is checked against the
 * interpreter by the same runs.
 *
 *   compile-host-test SPVDIR
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/powervr/shader.h"
#include "drivers/gpu/powervr/shader-ir.h"
#include "ir-run.h"
#include "machine-run.h"

#include "userland/desktop/wayland/shaders.h"
#include "userland/tests/vkcompute/shader.h"
#include "userland/tests/vkdemo/shaders.h"

/* The bytes of program room per shader. */
#define CODE_ROOM	65536

/* The buffer the compute tests read and write, at a 40-bit core address. */
#define MEMORY_BASE	0x12345670000ULL
#define MEMORY_WORDS	4096

static unsigned passed;
static unsigned failed;
static struct powervr_shader_workspace *workspace;
static struct ir_run *run;
static struct machine_run *machine;
static uint8_t code[CODE_ROOM];
static uint32_t memory[MEMORY_WORDS];
static uint32_t machine_memory[MEMORY_WORDS];
static uint32_t push_words[32];
static uint32_t workgroup_id;
static const struct powervr_shader_key *current_key;

/* The mask of the first count lanes. */
static uint32_t
lane_mask(
	uint32_t count)
{
	if (count >= 32)
		return 0xffffffffU;
	return (1U << count) - 1U;
}

/* Notes one check's outcome. */
static void
check(
	int ok,
	const char *name)
{
	if (ok) {
		passed++;
	} else {
		failed++;
		printf("FAIL %s\n", name);
	}
}

static float
as_float(
	uint32_t word)
{
	float value;

	memcpy(&value, &word, sizeof(value));
	return value;
}

static uint32_t
as_word(
	float value)
{
	uint32_t word;

	memcpy(&word, &value, sizeof(word));
	return word;
}

/* Answers a shared word: buffer addresses, push constants, the workgroup. */
static uint32_t
shared_word(
	const struct powervr_shader_program *program,
	uint32_t request,
	uint32_t word)
{
	const struct powervr_shader_request *entry;

	entry = &program->requests[request];
	switch (entry->kind) {
	case POWERVR_SHADER_REQUEST_DESCRIPTOR:
		if (entry->part == POWERVR_SHADER_PART_BUFFER_ADDRESS)
			return word == 0 ? (uint32_t)MEMORY_BASE : (uint32_t)(MEMORY_BASE >> 32);
		return 0x51000000U + entry->binding * 16U + entry->part * 4U + word;
	case POWERVR_SHADER_REQUEST_PUSH_CONSTANTS:
		return push_words[word];
	case POWERVR_SHADER_REQUEST_WORKGROUP_ID:
		return word == 0 ? workgroup_id : 0;
	case POWERVR_SHADER_REQUEST_NUM_WORKGROUPS:
		return 1;
	default:
		return 0;
	}
}

static uint32_t
run_shared(
	struct ir_run *self,
	uint32_t request,
	uint32_t word)
{
	return shared_word(self->context, request, word);
}

static uint32_t
run_load(
	struct ir_run *self,
	uint64_t address)
{
	(void)self;
	if (address < MEMORY_BASE || address >= MEMORY_BASE + MEMORY_WORDS * 4ULL || (address & 3U) != 0)
		return 0xbadbad00U;
	return memory[(address - MEMORY_BASE) / 4U];
}

static void
run_store(
	struct ir_run *self,
	uint64_t address,
	uint32_t word)
{
	(void)self;
	if (address < MEMORY_BASE || address >= MEMORY_BASE + MEMORY_WORDS * 4ULL || (address & 3U) != 0)
		return;
	memory[(address - MEMORY_BASE) / 4U] = word;
}

/* A vertex input: lane-dependent floats. */
static uint32_t
input_word(
	unsigned lane,
	uint32_t reg)
{
	return as_word((float)lane * 0.125f + (float)reg * 0.5f - 1.0f);
}

static uint32_t
run_input(
	struct ir_run *self,
	unsigned lane,
	uint32_t reg)
{
	(void)self;
	return input_word(lane, reg);
}

/* A fragment input: lane-dependent floats, flat ones constant across lanes. */
static uint32_t
iterate_word(
	unsigned lane,
	uint32_t coefficient,
	unsigned flags)
{
	if (flags != 0)
		return as_word((float)coefficient * 0.25f);
	return as_word((float)lane * 1.5f + (float)coefficient);
}

static uint32_t
run_iterate(
	struct ir_run *self,
	unsigned lane,
	uint32_t coefficient,
	unsigned flags)
{
	(void)self;
	return iterate_word(lane, coefficient, flags);
}

/* A texture sample: a function of the coordinates and the state words. */
static void
sample_words(
	uint32_t image,
	uint32_t sampler,
	uint32_t u,
	uint32_t v,
	uint32_t result[4])
{
	result[0] = as_word(as_float(u) * 0.5f);
	result[1] = as_word(as_float(v) * 0.25f);
	result[2] = as_word(as_float(u) + as_float(v));
	result[3] = as_word(1.0f + (float)image * 0.001f + (float)sampler * 0.0001f);
}

static void
run_sample(
	struct ir_run *self,
	uint32_t image,
	uint32_t sampler,
	uint32_t u,
	uint32_t v,
	uint32_t result[4])
{
	(void)self;
	sample_words(image, sampler, u, v, result);
}

/* Fills the pixel outputs with the colours already in the tile. */
static void
pixels_prepare(
	uint32_t pixels[4][32])
{
	unsigned index;
	unsigned lane;

	for (index = 0; index < 4; index++) {
		for (lane = 0; lane < 32; lane++)
			pixels[index][lane] = 0x10203040U * (index + 1U) + lane * 0x01070b0dU;
	}
}

/* Reads a module into the intermediate form, reporting the result. */
static int
read_module(
	const uint32_t *words,
	size_t count,
	uint32_t stage,
	struct powervr_shader_program *program,
	struct powervr_ir_shader **shader)
{
	return powervr_shader_read(words, count, stage, current_key, workspace, program, shader);
}

/* Loads a glslc module from the SPIR-V directory. */
static uint32_t *
load_file(
	const char *directory,
	const char *name,
	size_t *count)
{
	char path[512];
	FILE *file;
	uint32_t *words;
	long size;

	snprintf(path, sizeof(path), "%s/%s", directory, name);
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)size);
	if (words == NULL || fread(words, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(words);
		return NULL;
	}
	fclose(file);
	*count = (size_t)size / 4U;
	return words;
}

/* Sets up a run's callbacks. */
static void
run_prepare(
	struct powervr_shader_program *program)
{
	memset(run->values, 0, sizeof(run->values));
	memset(run->outputs, 0, sizeof(run->outputs));
	pixels_prepare(run->pixels);
	run->shared = run_shared;
	run->input = run_input;
	run->iterate = run_iterate;
	run->load = run_load;
	run->store = run_store;
	run->sample = run_sample;
	run->context = program;
}

/*
 * Compiles a module that was read, runs it on the machine simulator
 * with the same inputs, and compares memory and outputs with the
 * interpreter's.
 */
static void
machine_compare(
	const uint32_t *words,
	size_t count,
	uint32_t stage,
	const char *name)
{
	struct powervr_shader_program program;
	uint32_t saved[MEMORY_WORDS];
	char label[160];
	unsigned lane;
	unsigned word;
	int error;
	int same;

	/* The interpreter's memory is in memory[]; the machine starts from what it started from. */
	memcpy(saved, memory, sizeof(saved));
	memcpy(memory, machine_memory, sizeof(memory));
	error = powervr_shader_compile(words, count, stage, current_key, workspace, &program, code, sizeof(code));
	snprintf(label, sizeof(label), "%s compiles (%d)", name, error);
	check(error == 0, label);
	if (error != 0) {
		memcpy(memory, saved, sizeof(memory));
		return;
	}

	/* Runs the machine form with the same inputs. */
	memset(machine, 0, sizeof(*machine));
	pixels_prepare(machine->pixels);
	machine->program = &program;
	machine->shared_word = shared_word;
	machine->input_word = input_word;
	machine->iterate_word = iterate_word;
	machine->sample_words = sample_words;
	machine->memory = memory;
	machine->memory_base = MEMORY_BASE;
	machine->memory_words = MEMORY_WORDS;
	error = machine_run_program(machine, &workspace->backend, stage == POWERVR_SHADER_COMPUTE ? lane_mask(program.workgroup[0]) : 0xffffffffU);
	snprintf(label, sizeof(label), "%s runs on the machine simulator (%d)", name, error);
	check(error == 0, label);

	/* The same memory. */
	same = memcmp(memory, saved, sizeof(saved)) == 0;
	snprintf(label, sizeof(label), "%s machine memory matches the interpreter", name);
	check(same, label);
	if (!same) {
		for (word = 0; word < MEMORY_WORDS; word++) {
			if (memory[word] != saved[word]) {
				printf("  word %u: machine %08x interpreter %08x\n", word, memory[word], saved[word]);
				break;
			}
		}
	}

	/* The same outputs. */
	same = 1;
	for (word = 0; word < IR_RUN_OUTPUTS && word < MACHINE_RUN_OUTPUTS; word++) {
		for (lane = 0; lane < 32; lane++) {
			if (machine->outputs[word][lane] != run->outputs[word][lane]) {
				if (same)
					printf("  output %u lane %u: machine %08x interpreter %08x\n", word, lane, machine->outputs[word][lane], run->outputs[word][lane]);
				same = 0;
			}
		}
	}
	snprintf(label, sizeof(label), "%s machine outputs match the interpreter", name);
	check(same, label);

	/* The same pixel outputs. */
	same = memcmp(machine->pixels, run->pixels, sizeof(run->pixels)) == 0;
	snprintf(label, sizeof(label), "%s machine pixel outputs match the interpreter", name);
	check(same, label);
	memcpy(memory, saved, sizeof(memory));
}

/* Reads, interprets and compiles one module, returning the program, or -1. */
static int
prepare(
	const uint32_t *words,
	size_t count,
	uint32_t stage,
	struct powervr_shader_program *program,
	const char *name,
	uint32_t lanes)
{
	struct powervr_ir_shader *shader;
	char label[160];
	int error;

	error = read_module(words, count, stage, program, &shader);
	snprintf(label, sizeof(label), "%s reads (%d)", name, error);
	check(error == 0, label);
	if (error != 0)
		return -1;
	memcpy(machine_memory, memory, sizeof(memory));
	run_prepare(program);
	error = ir_run(run, shader, lanes);
	snprintf(label, sizeof(label), "%s interprets", name);
	check(error == 0, label);
	if (error != 0)
		return -1;
	return 0;
}

/* vkcompute's fill and p009's: data[x] = 3x + 7. */
static void
test_fill(
	const uint32_t *words,
	size_t count,
	const char *name,
	uint32_t multiplier,
	uint32_t addend,
	uint32_t index_addend)
{
	struct powervr_shader_program program;
	char label[160];
	unsigned x;
	int ok;

	memset(memory, 0, sizeof(memory));
	workgroup_id = 1;
	if (prepare(words, count, POWERVR_SHADER_COMPUTE, &program, name, 0) != 0)
		return;
	memcpy(machine_memory, memory, sizeof(memory));
	run_prepare(&program);
	if (ir_run(run, &workspace->shader, lane_mask(program.workgroup[0])) != 0)
		return;
	ok = 1;
	for (x = 0; x < program.workgroup[0]; x++) {
		uint32_t g = program.workgroup[0] + x;

		if (memory[g + index_addend] != g * multiplier + addend)
			ok = 0;
	}
	snprintf(label, sizeof(label), "%s writes its words", name);
	check(ok, label);
	machine_compare(words, count, POWERVR_SHADER_COMPUTE, name);
}

/* The loop test: sum of odd i < x, stopping past 50. */
static void
test_loop(
	const uint32_t *words,
	size_t count,
	const char *name)
{
	struct powervr_shader_program program;
	char label[160];
	uint32_t s;
	uint32_t i;
	unsigned x;
	int ok;

	memset(memory, 0, sizeof(memory));
	workgroup_id = 0;
	if (prepare(words, count, POWERVR_SHADER_COMPUTE, &program, name, 0xffffffffU) != 0)
		return;
	memset(memory, 0, sizeof(memory));
	memcpy(machine_memory, memory, sizeof(memory));
	run_prepare(&program);
	if (ir_run(run, &workspace->shader, 0xffffffffU) != 0)
		return;
	ok = 1;
	for (x = 0; x < 32; x++) {
		s = 0;
		for (i = 0; i < x; i++) {
			if ((i & 1U) == 0)
				continue;
			s += i;
			if (s > 50)
				break;
		}
		if (memory[x] != s) {
			ok = 0;
			printf("  x %u: %u, expected %u\n", x, memory[x], s);
		}
	}
	snprintf(label, sizeof(label), "%s sums", name);
	check(ok, label);
	machine_compare(words, count, POWERVR_SHADER_COMPUTE, name);
}

/* The branch test. */
static void
test_branch(
	const uint32_t *words,
	size_t count,
	const char *name)
{
	struct powervr_shader_program program;
	char label[160];
	float f;
	float r;
	unsigned x;
	int ok;

	memset(memory, 0, sizeof(memory));
	workgroup_id = 0;
	push_words[0] = as_word(0.75f);
	push_words[1] = 17;
	if (prepare(words, count, POWERVR_SHADER_COMPUTE, &program, name, 0xffffffffU) != 0)
		return;
	ok = 1;
	for (x = 0; x < 32; x++) {
		f = (float)x * 0.75f;
		if (f < 4.0f) {
			r = f * 2.0f;
			if (x == 1)
				r = -1.0f;
		} else if (x < 17) {
			r = sqrtf(f);
		} else if (x > 20) {
			r = floorf(f * 0.3f);
		} else {
			r = f * 0.3f - floorf(f * 0.3f);
		}
		if (fabsf(as_float(memory[x]) - r) > 1e-5f * (1.0f + fabsf(r))) {
			ok = 0;
			printf("  x %u: %g, expected %g\n", x, as_float(memory[x]), r);
		}
	}
	snprintf(label, sizeof(label), "%s branches", name);
	check(ok, label);
	machine_compare(words, count, POWERVR_SHADER_COMPUTE, name);
}

/* The math test. */
static void
test_math(
	const uint32_t *words,
	size_t count,
	const char *name)
{
	struct powervr_shader_program program;
	char label[160];
	float expected[8];
	float f;
	float t;
	unsigned x;
	unsigned k;
	int ok;

	memset(memory, 0, sizeof(memory));
	workgroup_id = 0;
	if (prepare(words, count, POWERVR_SHADER_COMPUTE, &program, name, 0xffffffffU) != 0)
		return;
	ok = 1;
	for (x = 0; x < 32; x++) {
		f = (float)x * 0.37f - 5.0f;
		expected[0] = sinf(f);
		expected[1] = cosf(f);
		expected[2] = fabsf(f);
		expected[3] = f < -1.0f ? -1.0f : (f > 2.0f ? 2.0f : f);
		expected[4] = f + (3.0f - f) * 0.25f;
		expected[5] = sqrtf(f * f + 2.25f);
		t = (f + 2.0f) / 4.0f;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		expected[6] = t * t * (3.0f - 2.0f * t);
		expected[7] = (f < 1.0f ? (f > -3.0f ? f : -3.0f) : 1.0f) + 1.0f / sqrtf(fabsf(f) + 1.0f);
		for (k = 0; k < 8; k++) {
			if (fabsf(as_float(memory[x * 8 + k]) - expected[k]) > 2e-5f * (1.0f + fabsf(expected[k]))) {
				ok = 0;
				printf("  x %u result %u: %.7g, expected %.7g\n", x, k, as_float(memory[x * 8 + k]), expected[k]);
			}
		}
	}
	snprintf(label, sizeof(label), "%s computes", name);
	check(ok, label);
	machine_compare(words, count, POWERVR_SHADER_COMPUTE, name);
}

/* The call test. */
static void
test_call(
	const uint32_t *words,
	size_t count,
	const char *name)
{
	struct powervr_shader_program program;
	char label[160];
	unsigned x;
	int ok;

	memset(memory, 0, sizeof(memory));
	workgroup_id = 0;
	if (prepare(words, count, POWERVR_SHADER_COMPUTE, &program, name, 0xffffffffU) != 0)
		return;
	ok = 1;
	for (x = 0; x < 32; x++) {
		if (memory[x] != (((x >> 2) * 5U + (x & 3U) * 11U) ^ (x << 3)))
			ok = 0;
	}
	snprintf(label, sizeof(label), "%s calls", name);
	check(ok, label);
	machine_compare(words, count, POWERVR_SHADER_COMPUTE, name);
}

/* A graphics shader: reads, runs, and its machine form matches. */
static void
test_graphics(
	const uint32_t *words,
	size_t count,
	uint32_t stage,
	const char *name)
{
	struct powervr_shader_program program;
	unsigned k;

	for (k = 0; k < 32; k++)
		push_words[k] = as_word((float)k * 0.1f + 0.05f);
	memset(memory, 0, sizeof(memory));
	if (prepare(words, count, stage, &program, name, 0xffffffffU) != 0)
		return;
	machine_compare(words, count, stage, name);
}

/* quad.vert: position = mix(rect.xy, rect.zw, corner), texcoord likewise. */
static void
test_quad_vertex(void)
{
	struct powervr_shader_program program;
	float rect[4];
	float corner[2];
	float expected;
	unsigned lane;
	unsigned k;
	int ok;

	for (k = 0; k < 32; k++)
		push_words[k] = as_word((float)k * 0.1f + 0.05f);
	if (prepare(kwl_quad_vert, sizeof(kwl_quad_vert) / 4U, POWERVR_SHADER_VERTEX, &program, "kwl quad.vert", 0xffffffffU) != 0)
		return;
	ok = program.input_count == 1 && program.varying_count == 1 && program.varyings[0].reg == 4;
	for (lane = 0; lane < 32; lane++) {
		for (k = 0; k < 4; k++)
			rect[k] = (float)k * 0.1f + 0.05f;
		corner[0] = as_float(input_word(lane, 0));
		corner[1] = as_float(input_word(lane, 1));
		for (k = 0; k < 2; k++) {
			expected = rect[k] + (rect[k + 2] - rect[k]) * corner[k];
			if (fabsf(as_float(run->outputs[k][lane]) - expected) > 1e-6f)
				ok = 0;
		}
		if (as_float(run->outputs[2][lane]) != 0.0f || as_float(run->outputs[3][lane]) != 1.0f)
			ok = 0;
	}
	check(ok, "kwl quad.vert positions its corner");
	machine_compare(kwl_quad_vert, sizeof(kwl_quad_vert) / 4U, POWERVR_SHADER_VERTEX, "kwl quad.vert");
}

/* vkdemo's vertex shader: the rotated cuboid's clip position. */
static void
test_vkdemo_vertex(void)
{
	struct powervr_shader_program program;
	float seconds;
	float px;
	float py;
	float pz;
	float sx;
	float cx;
	float sy;
	float cy;
	float ry;
	float rz;
	float vx;
	float vz;
	float expected[4];
	unsigned lane;
	unsigned k;
	int ok;

	seconds = 2.5f;
	push_words[0] = as_word(seconds);
	if (prepare(vkdemo_vertex_shader, sizeof(vkdemo_vertex_shader) / 4U, POWERVR_SHADER_VERTEX, &program, "vkdemo vertex", 0xffffffffU) != 0)
		return;
	ok = 1;
	for (lane = 0; lane < 32; lane++) {
		px = as_float(input_word(lane, 0));
		py = as_float(input_word(lane, 1));
		pz = as_float(input_word(lane, 2));
		sx = sinf(0.30f + 0.43f * seconds);
		cx = cosf(0.30f + 0.43f * seconds);
		sy = sinf(0.40f + 0.70f * seconds);
		cy = cosf(0.40f + 0.70f * seconds);
		ry = cx * py - sx * pz;
		rz = sx * py + cx * pz;
		vx = cy * px + sy * rz;
		vz = -sy * px + cy * rz + 3.0f;
		expected[0] = 1.2f * vx;
		expected[1] = -1.6f * ry;
		expected[2] = (10.0f / 9.9f) * vz - (1.0f / 9.9f);
		expected[3] = vz;
		for (k = 0; k < 4; k++) {
			if (fabsf(as_float(run->outputs[k][lane]) - expected[k]) > 1e-4f) {
				if (ok)
					printf("  lane %u word %u: %g, expected %g\n", lane, k, as_float(run->outputs[k][lane]), expected[k]);
				ok = 0;
			}
		}
	}
	check(ok, "vkdemo vertex rotates the cuboid");
	machine_compare(vkdemo_vertex_shader, sizeof(vkdemo_vertex_shader) / 4U, POWERVR_SHADER_VERTEX, "vkdemo vertex");
}

/* vkdemo's fragment shader: the colour is the sample at the texture coordinate. */
static void
test_vkdemo_fragment(void)
{
	struct powervr_shader_program program;
	uint32_t expected[4];
	unsigned lane;
	unsigned k;
	int ok;

	if (prepare(vkdemo_fragment_shader, sizeof(vkdemo_fragment_shader) / 4U, POWERVR_SHADER_FRAGMENT, &program, "vkdemo fragment", 0xffffffffU) != 0)
		return;
	ok = program.output_count == 1 && program.varying_count == 1 && program.uses_w == 1;
	for (lane = 0; lane < 32; lane++) {
		sample_words(0, 1, iterate_word(lane, 4, 0), iterate_word(lane, 8, 0), expected);
		for (k = 0; k < 4; k++) {
			if (run->outputs[k][lane] != expected[k])
				ok = 0;
		}
	}
	check(ok, "vkdemo fragment samples its texture");
	machine_compare(vkdemo_fragment_shader, sizeof(vkdemo_fragment_shader) / 4U, POWERVR_SHADER_FRAGMENT, "vkdemo fragment");
}

/* Packs four floats as the test expects the core to, rounding to nearest. */
static uint32_t
pack_expected(
	const float colour[4])
{
	uint32_t words[4];
	unsigned channel;

	for (channel = 0; channel < 4; channel++)
		words[channel] = as_word(colour[channel]);
	return ir_run_pack(words);
}

/*
 * The keyed fragment shaders: vkdemo's sample written to an R8G8B8A8
 * attachment without blending, and the compositor's quad.frag to a
 * B8G8R8A8 attachment with its premultiplied blend (one, one minus
 * source alpha) and a write mask without alpha.
 */
static void
test_keyed(void)
{
	static struct powervr_shader_key key;
	struct powervr_shader_program program;
	uint32_t sampled[4];
	uint32_t before[4];
	float colour[4];
	unsigned lane;
	unsigned channel;
	int ok;

	/* vkdemo: the sample, packed. */
	memset(&key, 0, sizeof(key));
	key.output_formats[0] = 37;
	key.blend[0].write_mask = 0xf;
	current_key = &key;
	if (prepare(vkdemo_fragment_shader, sizeof(vkdemo_fragment_shader) / 4U, POWERVR_SHADER_FRAGMENT, &program, "keyed vkdemo fragment", 0xffffffffU) == 0) {
		ok = program.output_count == 1 && program.outputs[0].reg == 0 && program.outputs[0].format == 37;
		for (lane = 0; lane < 32; lane++) {
			sample_words(0, 1, iterate_word(lane, 4, 0), iterate_word(lane, 8, 0), sampled);
			for (channel = 0; channel < 4; channel++)
				colour[channel] = as_float(sampled[channel]);
			if (run->pixels[0][lane] != pack_expected(colour))
				ok = 0;
		}
		check(ok, "keyed vkdemo fragment packs its sample");
		machine_compare(vkdemo_fragment_shader, sizeof(vkdemo_fragment_shader) / 4U, POWERVR_SHADER_FRAGMENT, "keyed vkdemo fragment");
	}

	/* quad.frag: premultiplied over, alpha kept. */
	memset(&key, 0, sizeof(key));
	key.output_formats[0] = 44;
	key.blend[0].enable = 1;
	key.blend[0].source_color = 1;
	key.blend[0].destination_color = 7;
	key.blend[0].source_alpha = 1;
	key.blend[0].destination_alpha = 7;
	key.blend[0].write_mask = 0x7;
	if (prepare(kwl_quad_frag, sizeof(kwl_quad_frag) / 4U, POWERVR_SHADER_FRAGMENT, &program, "keyed kwl quad.frag", 0xffffffffU) == 0) {
		uint32_t start[4][32];

		ok = 1;
		pixels_prepare(start);
		for (lane = 0; lane < 32; lane++) {
			sample_words(0, 1, iterate_word(lane, 4, 0), iterate_word(lane, 8, 0), sampled);
			ir_run_unpack(start[0][lane], before);
			for (channel = 0; channel < 4; channel++) {
				float s;
				float d;

				s = as_float(sampled[channel]);
				s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
				d = as_float(before[channel]);
				colour[channel] = s + d * (1.0f - (as_float(sampled[3]) > 1.0f ? 1.0f : as_float(sampled[3])));
				if (colour[channel] > 1.0f)
					colour[channel] = 1.0f;
				if (channel == 3)
					colour[channel] = d;
			}
			if (run->pixels[0][lane] != pack_expected(colour)) {
				if (ok)
					printf("  lane %u: %08x, expected %08x\n", lane, run->pixels[0][lane], pack_expected(colour));
				ok = 0;
			}
		}
		check(ok, "keyed kwl quad.frag blends over the tile");
		machine_compare(kwl_quad_frag, sizeof(kwl_quad_frag) / 4U, POWERVR_SHADER_FRAGMENT, "keyed kwl quad.frag");
	}

	/* panel.frag's eight modes, the same blend, all channels. */
	key.blend[0].write_mask = 0xf;
	for (channel = 0; channel < 8; channel++) {
		char name[64];

		snprintf(name, sizeof(name), "keyed kwl panel.frag mode %u", channel);
		if (prepare(kwl_panel_variants[channel].words, kwl_panel_variants[channel].bytes / 4U, POWERVR_SHADER_FRAGMENT, &program, name, 0xffffffffU) == 0)
			machine_compare(kwl_panel_variants[channel].words, kwl_panel_variants[channel].bytes / 4U, POWERVR_SHADER_FRAGMENT, name);
	}

	/* vkdemo's vertex shader with its attribute formats: three floats and two. */
	memset(&key, 0, sizeof(key));
	key.attribute_formats[0] = 106;
	key.attribute_formats[1] = 103;
	if (prepare(vkdemo_vertex_shader, sizeof(vkdemo_vertex_shader) / 4U, POWERVR_SHADER_VERTEX, &program, "keyed vkdemo vertex", 0xffffffffU) == 0) {
		check(program.input_count == 2 && program.inputs[0].components == 3 && program.inputs[1].reg == 3, "keyed vkdemo vertex places its attributes");
		machine_compare(vkdemo_vertex_shader, sizeof(vkdemo_vertex_shader) / 4U, POWERVR_SHADER_VERTEX, "keyed vkdemo vertex");
	}
	current_key = NULL;
}

int
main(
	int argc,
	char **argv)
{
	static const char *const compute[][2] = {
		{ "loop-o.spv", "loop" }, { "loop-0.spv", "loop" },
		{ "branch-o.spv", "branch" }, { "branch-0.spv", "branch" },
		{ "math-o.spv", "math" }, { "math-0.spv", "math" },
		{ "call-o.spv", "call" }, { "call-0.spv", "call" },
	};
	char name[96];
	uint32_t *words;
	size_t count;
	unsigned index;

	if (argc != 2) {
		fprintf(stderr, "usage: compile-host-test SPVDIR\n");
		return 2;
	}
	workspace = calloc(1, powervr_shader_workspace_size());
	run = calloc(1, sizeof(*run));
	machine = calloc(1, sizeof(*machine));
	if (workspace == NULL || run == NULL || machine == NULL)
		return 2;

	/* vkcompute and p009's fills. */
	test_fill(vkcompute_fill_shader, sizeof(vkcompute_fill_shader) / 4U, "vkcompute fill", 3, 7, 0);
	words = load_file(argv[1], "p009-fill.spv", &count);
	if (words != NULL) {
		test_fill(words, count, "p009 fill", 3, 7, 0);
		free(words);
	}
	words = load_file(argv[1], "p009-offset.spv", &count);
	if (words != NULL) {
		struct powervr_shader_program program;

		memset(memory, 0, sizeof(memory));
		workgroup_id = 0;
		if (prepare(words, count, POWERVR_SHADER_COMPUTE, &program, "p009 offset", 0xffffffffU) == 0) {
			unsigned x;
			int ok;

			ok = 1;
			for (x = 0; x < 32; x++) {
				if (memory[x + 4] != (x << 2) - 1U)
					ok = 0;
			}
			check(ok, "p009 offset writes its words");
			machine_compare(words, count, POWERVR_SHADER_COMPUTE, "p009 offset");
		}
		free(words);
	}

	/* This test's compute shaders, optimized and not. */
	for (index = 0; index < sizeof(compute) / sizeof(compute[0]); index++) {
		words = load_file(argv[1], compute[index][0], &count);
		check(words != NULL, compute[index][0]);
		if (words == NULL)
			continue;
		snprintf(name, sizeof(name), "%s (%s)", compute[index][1], compute[index][0]);
		if (strcmp(compute[index][1], "loop") == 0)
			test_loop(words, count, name);
		if (strcmp(compute[index][1], "branch") == 0)
			test_branch(words, count, name);
		if (strcmp(compute[index][1], "math") == 0)
			test_math(words, count, name);
		if (strcmp(compute[index][1], "call") == 0)
			test_call(words, count, name);
		free(words);
	}

	/* The compositor's and vkdemo's graphics shaders. */
	test_quad_vertex();
	test_vkdemo_vertex();
	test_vkdemo_fragment();
	test_graphics(kwl_quad_frag, sizeof(kwl_quad_frag) / 4U, POWERVR_SHADER_FRAGMENT, "kwl quad.frag");
	test_graphics(kwl_panel_vert, sizeof(kwl_panel_vert) / 4U, POWERVR_SHADER_VERTEX, "kwl panel.vert");
	test_graphics(kwl_panel_frag, sizeof(kwl_panel_frag) / 4U, POWERVR_SHADER_FRAGMENT, "kwl panel.frag");
	test_graphics(kwl_batch_vert, sizeof(kwl_batch_vert) / 4U, POWERVR_SHADER_VERTEX, "kwl panel-batch.vert");
	test_graphics(kwl_batch_frag, sizeof(kwl_batch_frag) / 4U, POWERVR_SHADER_FRAGMENT, "kwl panel-batch.frag");
	test_graphics(kwl_turn_vert, sizeof(kwl_turn_vert) / 4U, POWERVR_SHADER_VERTEX, "kwl turn.vert");
	for (index = 0; index < 8; index++) {
		snprintf(name, sizeof(name), "kwl panel.frag mode %u", index);
		test_graphics(kwl_panel_variants[index].words, kwl_panel_variants[index].bytes / 4U, POWERVR_SHADER_FRAGMENT, name);
	}

	test_keyed();

	printf("powervr compiler host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
