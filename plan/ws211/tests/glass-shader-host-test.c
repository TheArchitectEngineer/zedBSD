/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the glass's shader after ws211-p011: the compositor's real
 * SPIR-V (shaders.h: light glass, variant 0; dark glass, variant 7; and the
 * generic shader) is interpreted at the source level over a grid of pixels,
 * wallpaper colours under the glass, glass colours, flatness, rims, opacity
 * and both appearances, with the per-shape values glass.c works out
 * (glass_shape_glass_constants), and compared with an independent C
 * evaluation of the glass as panel.frag drew it before p011 (every value
 * worked out per pixel).  It reports the largest difference of a colour
 * channel, on the 0 to 1 scale of an 8-bit channel.
 *
 * Usage: plan/ws211/tests/glass-shader-host-test.sh [build-dir]
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/compiler/spirv.h"
#include "userland/desktop/wayland/shaders.h"

/* One host-only bit conversion of a scalar. */
union scalar_bits {
	uint32_t bits;
	float number;
};

/* One fragment's inputs and its output, for the source-level interpreter. */
struct glass_fragment {
	uint32_t input[8];
	uint32_t push[24];
	uint32_t output[4];
	unsigned lookups;
};

/* The wallpaper colour under the glass at a texture coordinate (any smooth deterministic picture). */
static float under_colour(float s, float t, unsigned channel, unsigned picture);

/* The picture the next interpretation samples. */
static unsigned glass_picture;

void *kern_calloc(size_t count, size_t bytes) { return calloc(count, bytes); }
void *kern_malloc(size_t bytes) { return malloc(bytes); }
void *kern_realloc(void *pointer, size_t bytes) { return realloc(pointer, bytes); }
void kern_free(void *pointer) { free(pointer); }

static uint32_t
float_bits(float number)
{
	union scalar_bits scalar;

	scalar.number = number;
	return scalar.bits;
}

static float
bits_float(uint32_t bits)
{
	union scalar_bits scalar;

	scalar.bits = bits;
	return scalar.number;
}

static uint32_t
truth(int condition)
{
	if (condition != 0)
		return 0xffffffffU;
	return 0U;
}

static float
under_colour(float s, float t, unsigned channel, unsigned picture)
{
	float value;

	/* Pictures 0 to 26 are flat colours over a 3x3x3 grid; the rest vary over the output. */
	if (picture < 27U) {
		if (channel == 0U)
			value = (float)(picture % 3U) * 0.5f;
		else if (channel == 1U)
			value = (float)((picture / 3U) % 3U) * 0.5f;
		else
			value = (float)(picture / 9U) * 0.5f;
		return value;
	}
	if (channel == 0U)
		return 0.5f + 0.5f * sinf(7.0f * s + (float)picture);
	if (channel == 1U)
		return 0.5f + 0.5f * cosf(5.0f * t + 0.3f * (float)picture);
	return fmodf(s * 3.0f + t * 2.0f, 1.0f);
}

/* Interprets the shader's source operations for one fragment. */
static void
interpret(const struct drv_gpu_shader_ir *ir, struct glass_fragment *fragment)
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

	values = calloc(ir->value_count, sizeof(*values));
	assert(values != NULL);
	for (index = 0; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		left = values[instruction->src[0]];
		right = values[instruction->src[1]];
		a = bits_float(left);
		b = bits_float(right);
		bits = 0;
		switch (instruction->op) {
		case DRV_GPU_IR_NOP:
		case DRV_GPU_IR_SKIP_BEGIN:
		case DRV_GPU_IR_SKIP_END:
			continue;
		case DRV_GPU_IR_CONST:
			bits = instruction->immediate;
			break;
		case DRV_GPU_IR_LOAD_INPUT:
			bits = fragment->input[instruction->location * 2 + instruction->component];
			break;
		case DRV_GPU_IR_LOAD_PUSH:
			bits = fragment->push[instruction->immediate / 4];
			break;
		case DRV_GPU_IR_STORE_OUTPUT:
			if (instruction->location == 0 && instruction->component < 4)
				fragment->output[instruction->component] = left;
			continue;
		case DRV_GPU_IR_SAMPLE:
			for (component = 0; component < 4; component++)
				values[instruction->dst + component] = float_bits(under_colour(a, b, component, glass_picture));
			fragment->lookups++;
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
		case DRV_GPU_IR_FNEG:
			bits = left ^ 0x80000000U;
			break;
		case DRV_GPU_IR_RCP:
			bits = float_bits(1.0f / a);
			break;
		case DRV_GPU_IR_RSQ:
			bits = float_bits(1.0f / sqrtf(a));
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
		case DRV_GPU_IR_FGE:
			bits = truth(a >= b);
			break;
		case DRV_GPU_IR_AND:
			bits = left & right;
			break;
		case DRV_GPU_IR_OR:
			bits = left | right;
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
			fprintf(stderr, "operation %u not interpreted\n", (unsigned)instruction->op);
			assert(0);
		}
		values[instruction->dst] = bits;
	}
	free(values);
}

static float
clampf(float value, float low, float high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static float
mixf(float x, float y, float a)
{
	return x * (1.0f - a) + y * a;
}

/* The glass as panel.frag drew it before ws211-p011, every value worked out per pixel. */
static void
glass_before(const float *push, float px, float py, float *result)
{
	float half_w;
	float half_h;
	float qx;
	float qy;
	float distance;
	float cover;
	float glass[3];
	float luma;
	float white;
	float lift;
	float dark;
	float shade;
	float depth;
	float edge;
	float sheen;
	unsigned channel;

	half_w = push[10] * 0.5f;
	half_h = push[11] * 0.5f;
	qx = fabsf(px - (push[8] + half_w)) - half_w + push[16];
	qy = fabsf(py - (push[9] + half_h)) - half_h + push[16];
	distance = sqrtf(fmaxf(qx, 0.0f) * fmaxf(qx, 0.0f) + fmaxf(qy, 0.0f) * fmaxf(qy, 0.0f)) + fminf(fmaxf(qx, qy), 0.0f) - push[16];
	cover = clampf(0.5f - distance, 0.0f, 1.0f);
	for (channel = 0; channel < 3; channel++)
		glass[channel] = mixf(under_colour(px / push[20], py / push[21], channel, glass_picture), push[12 + channel], push[15]);
	luma = glass[0] * 0.2126f + glass[1] * 0.7152f + glass[2] * 0.0722f;
	white = fminf(push[12], fminf(push[13], push[14])) < 0.9f ? 0.0f : 1.0f;
	lift = white * clampf((0.85f - luma) / fmaxf(1.0f - luma, 0.001f), 0.0f, 1.0f);
	for (channel = 0; channel < 3; channel++)
		glass[channel] = mixf(glass[channel], 1.0f, lift);
	dark = -0.5f < push[17] ? 0.0f : 1.0f;
	shade = dark * clampf((luma - 0.15f) / fmaxf(luma, 0.001f), 0.0f, 1.0f);
	for (channel = 0; channel < 3; channel++)
		glass[channel] = mixf(glass[channel], 0.0f, shade);
	depth = clampf((py - push[9]) / fmaxf(push[11], 1.0f), 0.0f, 1.0f);
	edge = clampf(1.0f - fabsf(distance + 1.0f), 0.0f, 1.0f);
	sheen = 0.05f * (1.0f - depth) * (1.0f - clampf(push[18], 0.0f, 1.0f));
	for (channel = 0; channel < 3; channel++) {
		glass[channel] += sheen;
		glass[channel] = mixf(glass[channel], 1.0f, edge * push[22]);
		result[channel] = glass[channel] * cover * push[23];
	}
	result[3] = cover * push[23];
}

/* The per-shape values glass.c's glass_shape_glass_constants writes (texture slot), from the final constants. */
static void
glass_constants(float *push)
{
	float least;
	float flat;
	float height;

	least = push[12];
	if (push[13] < least)
		least = push[13];
	if (push[14] < least)
		least = push[14];
	push[4] = 0.0f;
	if (least >= 0.9f)
		push[4] = 1.0f;
	push[5] = 0.0f;
	if (push[17] <= -0.5f)
		push[5] = 1.0f;
	flat = clampf(push[18], 0.0f, 1.0f);
	push[6] = 0.05f * (1.0f - flat);
	height = push[11];
	if (height < 1.0f)
		height = 1.0f;
	push[7] = 1.0f / height;
}

int
main(void)
{
	static const float colours[][4] = {
		{ 1.0f, 1.0f, 1.0f, 0.30f }, { 1.0f, 1.0f, 1.0f, 0.34f }, { 1.0f, 1.0f, 1.0f, 0.54f },
		{ 1.0f, 1.0f, 1.0f, 0.64f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.12f, 0.14f, 0.18f, 0.62f }, { 0.20f, 0.22f, 0.28f, 0.45f }, { 0.95f, 0.92f, 0.90f, 0.5f }
	};
	static const float boxes[][5] = {
		{ 100.0f, 80.0f, 200.0f, 120.0f, 14.0f }, { 10.5f, 400.25f, 776.0f, 40.0f, 14.0f },
		{ 300.0f, 200.0f, 0.5f, 0.75f, 0.0f }, { 50.0f, 50.0f, 300.0f, 30.0f, 12.0f }
	};
	static const float flats[] = { 0.0f, 1.0f, 2.0f, 0.5f };
	static const float rims[] = { 0.0f, 0.70f, 0.85f };
	static const float opacities[] = { 1.0f, 0.5f };
	struct drv_gpu_shader_ir *light;
	struct drv_gpu_shader_ir *dark;
	struct drv_gpu_shader_ir *generic;
	struct drv_gpu_shader_ir *chosen;
	struct drv_gpu_compile_diagnostic diagnostic;
	struct glass_fragment fragment;
	float push[24];
	float before[4];
	float after;
	float difference;
	float largest;
	float pixel_x;
	float pixel_y;
	unsigned colour;
	unsigned box;
	unsigned flat;
	unsigned rim;
	unsigned opacity;
	unsigned appearance;
	unsigned shader;
	unsigned step_x;
	unsigned step_y;
	unsigned channel;
	unsigned long samples;
	int error;

	error = drv_gpu_shader_parse(kwl_panel_variants[0].words, kwl_panel_variants[0].bytes / 4, DRV_GPU_STAGE_FRAGMENT, &light, &diagnostic);
	assert(error == 0);
	error = drv_gpu_shader_parse(kwl_panel_variants[7].words, kwl_panel_variants[7].bytes / 4, DRV_GPU_STAGE_FRAGMENT, &dark, &diagnostic);
	assert(error == 0);
	error = drv_gpu_shader_parse(kwl_panel_frag, sizeof(kwl_panel_frag) / 4, DRV_GPU_STAGE_FRAGMENT, &generic, &diagnostic);
	assert(error == 0);

	largest = 0.0f;
	samples = 0UL;
	for (glass_picture = 0U; glass_picture < 31U; glass_picture++)
	for (colour = 0U; colour < sizeof(colours) / sizeof(colours[0]); colour++)
	for (box = 0U; box < sizeof(boxes) / sizeof(boxes[0]); box++)
	for (flat = 0U; flat < sizeof(flats) / sizeof(flats[0]); flat++)
	for (rim = 0U; rim < sizeof(rims) / sizeof(rims[0]); rim++)
	for (opacity = 0U; opacity < sizeof(opacities) / sizeof(opacities[0]); opacity++)
	for (appearance = 0U; appearance < 2U; appearance++)
	for (shader = 0U; shader < 2U; shader++)
	for (step_y = 0U; step_y < 6U; step_y++)
	for (step_x = 0U; step_x < 6U; step_x++) {
		/* The constants glass_shape_draw pushes: quad, box, colour, radius, mode, flatness, output, rim, opacity. */
		memset(push, 0, sizeof(push));
		push[8] = boxes[box][0];
		push[9] = boxes[box][1];
		push[10] = boxes[box][2];
		push[11] = boxes[box][3];
		memcpy(&push[12], colours[colour], sizeof(colours[colour]));
		push[16] = boxes[box][4];
		push[17] = appearance == 0U ? 0.0f : -1.0f;
		push[18] = flats[flat];
		push[20] = 1280.0f;
		push[21] = 800.0f;
		push[22] = rims[rim];
		push[23] = opacities[opacity];

		/* Pixels across and around the box, at pixel centres and between them. */
		pixel_x = boxes[box][0] - 3.0f + (boxes[box][2] + 6.0f) * (float)step_x / 5.0f + 0.5f;
		pixel_y = boxes[box][1] - 3.0f + (boxes[box][3] + 6.0f) * (float)step_y / 5.0f + 0.5f;
		glass_before(push, pixel_x, pixel_y, before);

		/* The shader production picks (light glass, or dark glass's variant), or the generic one. */
		glass_constants(push);
		chosen = appearance == 0U ? light : dark;
		if (shader == 1U)
			chosen = generic;
		memset(&fragment, 0, sizeof(fragment));
		for (channel = 0U; channel < 24U; channel++)
			fragment.push[channel] = float_bits(push[channel]);
		fragment.input[0] = float_bits(0.25f);
		fragment.input[1] = float_bits(0.75f);
		fragment.input[2] = float_bits(pixel_x);
		fragment.input[3] = float_bits(pixel_y);
		interpret(chosen, &fragment);
		if (shader == 0U)
			assert(fragment.lookups == 1U);
		for (channel = 0U; channel < 4U; channel++) {
			after = bits_float(fragment.output[channel]);
			difference = fabsf(after - before[channel]);
			if (!(difference <= largest))
				largest = difference;
		}
		samples++;
	}

	drv_gpu_shader_ir_free(light);
	drv_gpu_shader_ir_free(dark);
	drv_gpu_shader_ir_free(generic);
	printf("glass-shader-host-test: %lu fragments, largest channel difference %.3g (%.5f of an 8-bit step)\n", samples, (double)largest, (double)(largest * 255.0f));

	/* Far below one step of an 8-bit channel (and of the tile buffer's 16-bit float). */
	assert(largest < 1.0e-5f);
	printf("glass-shader-host-test: PASS\n");
	return 0;
}
