/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The load checks of libyolox's ZNET reader and the API's argument
 * checks, on the host (T2b).
 *
 * A small synthetic network with every op kind (space-to-depth, conv,
 * maxpool, add, upsample, concat) and the three head levels is written
 * as a valid ZNET file and loaded; then copies broken in one place each
 * must be refused with YOLOX_ERROR_FORMAT, without a crash.  The valid
 * model also runs once on the CPU with biases that make detections.
 *
 *   znet-host-test <directory>
 */

#include "yolox.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The layout of the file the generator writes. */
#define GEN_HEADER		256U
#define GEN_TENSORS		19U
#define GEN_OPS			18U
#define GEN_TENSOR_OFFSET	256U
#define GEN_OP_OFFSET		(GEN_TENSOR_OFFSET + 320U)
#define GEN_NAME_OFFSET		(GEN_OP_OFFSET + GEN_OPS * 64U)
#define GEN_NAME_SIZE		64U
#define GEN_DATA_OFFSET		(GEN_NAME_OFFSET + GEN_NAME_SIZE)
#define GEN_PATH_MAX		512U

/* One tensor of the synthetic network. */
struct gen_tensor {
	uint32_t channels;
	uint32_t divisor;
	uint32_t flags;
};

/* One op of the synthetic network. */
struct gen_op {
	uint32_t kind;
	uint32_t activation;
	uint32_t kernel;
	uint32_t stride;
	uint32_t groups;
	uint32_t input_count;
	uint32_t inputs[4];
	uint32_t output;
};

/* The tensors: the input, the backbone, and per level reg 4, obj 1, cls 80 flagged as head outputs. */
static const struct gen_tensor gen_tensors[GEN_TENSORS] = {
	{ 3, 1, 1 }, { 12, 2, 0 }, { 16, 4, 0 }, { 16, 8, 0 }, { 16, 8, 0 }, { 16, 8, 0 },
	{ 4, 8, 2 }, { 1, 8, 2 }, { 80, 8, 2 },
	{ 16, 16, 0 }, { 16, 32, 0 }, { 16, 16, 0 }, { 32, 16, 0 },
	{ 4, 16, 2 }, { 1, 16, 2 }, { 80, 16, 2 },
	{ 4, 32, 2 }, { 1, 32, 2 }, { 80, 32, 2 }
};

/* The ops in topological order. */
static const struct gen_op gen_ops[GEN_OPS] = {
	{ 1, 0, 0, 0, 0, 1, { 0 }, 1 },
	{ 2, 1, 3, 2, 1, 1, { 1 }, 2 },
	{ 2, 1, 3, 2, 1, 1, { 2 }, 3 },
	{ 3, 0, 5, 1, 0, 1, { 3 }, 4 },
	{ 5, 0, 0, 0, 0, 2, { 3, 4 }, 5 },
	{ 2, 0, 1, 1, 1, 1, { 5 }, 6 },
	{ 2, 0, 1, 1, 1, 1, { 5 }, 7 },
	{ 2, 0, 1, 1, 1, 1, { 5 }, 8 },
	{ 2, 1, 3, 2, 1, 1, { 5 }, 9 },
	{ 2, 1, 3, 2, 1, 1, { 9 }, 10 },
	{ 4, 0, 0, 0, 0, 1, { 10 }, 11 },
	{ 6, 0, 0, 0, 0, 2, { 9, 11 }, 12 },
	{ 2, 0, 1, 1, 1, 1, { 12 }, 13 },
	{ 2, 0, 1, 1, 1, 1, { 12 }, 14 },
	{ 2, 0, 1, 1, 1, 1, { 12 }, 15 },
	{ 2, 0, 1, 1, 1, 1, { 10 }, 16 },
	{ 2, 0, 1, 1, 1, 1, { 10 }, 17 },
	{ 2, 0, 1, 1, 1, 1, { 10 }, 18 }
};

/* The head tensors (reg, obj, cls) of each level. */
static const uint32_t gen_levels[3][3] = {
	{ 6, 7, 8 }, { 13, 14, 15 }, { 16, 17, 18 }
};

static void put32(uint8_t *bytes, size_t offset, uint32_t value);
static uint32_t get32(const uint8_t *bytes, size_t offset);
static uint8_t *gen_build(size_t *size);
static void gen_write(const char *path, const uint8_t *bytes, size_t size);
static int gen_load(const char *directory, const char *name, const uint8_t *bytes, size_t size);
static void case_valid(const char *directory, const uint8_t *bytes, size_t size);
static void case_broken(const char *directory, const uint8_t *bytes, size_t size);
static void case_arguments(const char *directory);

/* Runs every case. */
int
main(
	int argc,
	char **argv)
{
	uint8_t *bytes;
	size_t size;

	/* The directory for the files. */
	if (argc != 2) {
		fprintf(stderr, "usage: znet-host-test <directory>\n");
		return 2;
	}

	bytes = gen_build(&size);
	case_valid(argv[1], bytes, size);
	case_broken(argv[1], bytes, size);
	case_arguments(argv[1]);
	free(bytes);
	printf("znet-host-test: PASS\n");
	return 0;
}

/* Writes a little-endian 32-bit field. */
static void
put32(
	uint8_t *bytes,
	size_t offset,
	uint32_t value)
{
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
	bytes[offset + 2U] = (uint8_t)(value >> 16);
	bytes[offset + 3U] = (uint8_t)(value >> 24);
}

/* Reads a little-endian 32-bit field. */
static uint32_t
get32(
	const uint8_t *bytes,
	size_t offset)
{
	return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1U] << 8) |
	    ((uint32_t)bytes[offset + 2U] << 16) | ((uint32_t)bytes[offset + 3U] << 24);
}

/*
 * Builds the valid file: header, tensor table, op table, names, then the
 * fp32 weights (zero) and biases.  The level-0 objectness and class 0
 * biases are 5, so the CPU finds objects of class 0 at stride 8.
 */
static uint8_t *
gen_build(
	size_t *size)
{
	const struct gen_op *op;
	uint8_t *bytes;
	uint32_t data_values;
	uint32_t weights[GEN_OPS];
	uint32_t biases[GEN_OPS];
	uint32_t offsets[GEN_OPS];
	uint32_t cin;
	uint32_t cout;
	uint32_t index;
	uint32_t input;
	uint32_t level;
	size_t data_bytes;
	size_t total;
	float five;

	/* The values each conv carries and where they start. */
	data_values = 0U;
	for (index = 0U; index < GEN_OPS; index++) {
		op = &gen_ops[index];
		weights[index] = 0U;
		biases[index] = 0U;
		offsets[index] = data_values;
		if (op->kind != 2U)
			continue;
		cin = gen_tensors[op->inputs[0]].channels;
		cout = gen_tensors[op->output].channels;
		weights[index] = cout * (cin / op->groups) * op->kernel * op->kernel;
		biases[index] = cout;
		data_values += weights[index] + biases[index];
	}

	/* The whole file, zeroed. */
	data_bytes = (size_t)data_values * 4U;
	total = GEN_DATA_OFFSET + data_bytes;
	bytes = calloc(1U, total);
	assert(bytes != NULL);

	/* The header. */
	memcpy(bytes, "ZNET", 4U);
	put32(bytes, 4U, 1U);
	put32(bytes, 8U, GEN_HEADER);
	put32(bytes, 16U, GEN_TENSORS);
	put32(bytes, 20U, GEN_OPS);
	put32(bytes, 24U, GEN_TENSOR_OFFSET);
	put32(bytes, 28U, GEN_OP_OFFSET);
	put32(bytes, 32U, GEN_DATA_OFFSET);
	put32(bytes, 36U, (uint32_t)data_bytes);
	put32(bytes, 40U, GEN_NAME_OFFSET);
	put32(bytes, 44U, GEN_NAME_SIZE);
	put32(bytes, 48U, 0U);
	put32(bytes, 56U, 64U);
	put32(bytes, 60U, 32U);
	put32(bytes, 64U, 114U);
	put32(bytes, 68U, 80U);
	put32(bytes, 72U, 3U);
	for (level = 0U; level < 3U; level++) {
		put32(bytes, 76U + 4U * level, 8U << level);
		for (index = 0U; index < 3U; index++)
			put32(bytes, 92U + 12U * level + 4U * index, gen_levels[level][index]);
	}
	memcpy(bytes + 224U, "yolox-test", 10U);

	/* The tensors. */
	for (index = 0U; index < GEN_TENSORS; index++) {
		put32(bytes, GEN_TENSOR_OFFSET + 16U * index, gen_tensors[index].channels);
		put32(bytes, GEN_TENSOR_OFFSET + 16U * index + 4U, gen_tensors[index].divisor);
		put32(bytes, GEN_TENSOR_OFFSET + 16U * index + 8U, gen_tensors[index].flags);
	}

	/* The ops; every name is the one string "op". */
	for (index = 0U; index < GEN_OPS; index++) {
		op = &gen_ops[index];
		put32(bytes, GEN_OP_OFFSET + 64U * index, op->kind);
		put32(bytes, GEN_OP_OFFSET + 64U * index + 4U, op->activation);
		put32(bytes, GEN_OP_OFFSET + 64U * index + 8U, op->kernel);
		put32(bytes, GEN_OP_OFFSET + 64U * index + 12U, op->stride);
		put32(bytes, GEN_OP_OFFSET + 64U * index + 16U, op->groups);
		put32(bytes, GEN_OP_OFFSET + 64U * index + 20U, op->input_count);
		for (input = 0U; input < 4U; input++)
			put32(bytes, GEN_OP_OFFSET + 64U * index + 24U + 4U * input, op->inputs[input]);
		put32(bytes, GEN_OP_OFFSET + 64U * index + 40U, op->output);
		if (weights[index] != 0U) {
			put32(bytes, GEN_OP_OFFSET + 64U * index + 44U, offsets[index] * 4U);
			put32(bytes, GEN_OP_OFFSET + 64U * index + 48U, weights[index]);
			put32(bytes, GEN_OP_OFFSET + 64U * index + 52U, (offsets[index] + weights[index]) * 4U);
			put32(bytes, GEN_OP_OFFSET + 64U * index + 56U, biases[index]);
		}
		put32(bytes, GEN_OP_OFFSET + 64U * index + 60U, 0U);
	}
	memcpy(bytes + GEN_NAME_OFFSET, "op", 3U);

	/* Level 0: objectness 5, class 0 bias 5 (ops 6 and 7). */
	five = 5.0f;
	memcpy(bytes + GEN_DATA_OFFSET + (offsets[6] + weights[6]) * 4U, &five, 4U);
	memcpy(bytes + GEN_DATA_OFFSET + (offsets[7] + weights[7]) * 4U, &five, 4U);

	/* The file. */
	*size = total;
	return bytes;
}

/* Writes bytes to a file. */
static void
gen_write(
	const char *path,
	const uint8_t *bytes,
	size_t size)
{
	FILE *file;

	file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1U, size, file) == size);
	assert(fclose(file) == 0);
}

/* Writes a variant and loads it; reports the load's code. */
static int
gen_load(
	const char *directory,
	const char *name,
	const uint8_t *bytes,
	size_t size)
{
	struct yolox_model *model;
	char path[GEN_PATH_MAX];
	int error;

	snprintf(path, sizeof(path), "%s/%s.znet", directory, name);
	gen_write(path, bytes, size);
	model = NULL;
	error = yolox_model_load(path, &model);
	if (error == 0)
		yolox_model_free(model);
	return error;
}

/* Loads the valid file, runs it once on the CPU and checks the detections. */
static void
case_valid(
	const char *directory,
	const uint8_t *bytes,
	size_t size)
{
	struct yolox_runner_config config;
	struct yolox_detection detections[16];
	struct yolox_model *model;
	struct yolox_runner *runner;
	struct yolox_image image;
	char path[GEN_PATH_MAX];
	uint8_t *pixels;
	uint32_t count;
	uint32_t index;
	int error;

	/* The file loads. */
	snprintf(path, sizeof(path), "%s/valid.znet", directory);
	gen_write(path, bytes, size);
	model = NULL;
	error = yolox_model_load(path, &model);
	assert(error == 0);

	/* A CPU runner at 64 by 64. */
	yolox_runner_config_default(&config);
	config.backend = YOLOX_BACKEND_CPU;
	config.input_size = 64U;
	runner = NULL;
	error = yolox_runner_create(model, &config, &runner);
	assert(error == 0);
	assert(yolox_runner_backend(runner) == YOLOX_BACKEND_CPU);

	/*
	 * A grey 100 by 60 RGB image.  Every stride-8 anchor (8 by 8 at 64)
	 * finds class 0 in an 8-pixel box that only touches its neighbours,
	 * so NMS keeps all 64; the first 16 are written.
	 */
	image.format = YOLOX_PIXEL_RGB8;
	image.width = 100U;
	image.height = 60U;
	image.row_bytes = 300U;
	pixels = malloc(300U * 60U);
	assert(pixels != NULL);
	memset(pixels, 128, 300U * 60U);
	count = 0U;
	error = yolox_detect(runner, &image, pixels, detections, 16U, &count);
	assert(error == 0);
	assert(count == 64U);
	for (index = 0U; index < count && index < 16U; index++) {
		assert(detections[index].class_index == 0U);
		assert(detections[index].score > 0.9f && detections[index].score <= 1.0f);
	}

	/* Counting only, without room. */
	count = 0U;
	error = yolox_detect(runner, &image, pixels, NULL, 0U, &count);
	assert(error == 0 && count == 64U);

	/* The CPU backend has no device buffer nor a pending submit. */
	error = yolox_collect(runner, 0U, detections, 16U, &count);
	assert(error == YOLOX_ERROR_IDLE);

	free(pixels);
	yolox_runner_free(runner);
	yolox_model_free(model);
	printf("valid model: PASS (%u detections)\n", count);
}

/* Breaks the file in one place at a time; each must be refused as malformed. */
static void
case_broken(
	const char *directory,
	const uint8_t *bytes,
	size_t size)
{
	struct broken {
		const char *name;
		size_t offset;
		uint32_t value;
	};
	static const struct broken cases[] = {
		{ "magic", 0U, 0x54454e5aU ^ 0x01U },
		{ "version", 4U, 2U },
		{ "header-size", 8U, 128U },
		{ "flag", 12U, 0x2U },
		{ "tensor-count-zero", 16U, 0U },
		{ "op-count-large", 20U, 2000U },
		{ "tensor-offset-outside", 24U, 0x7fffffc0U },
		{ "op-offset-misaligned", 28U, GEN_OP_OFFSET + 4U },
		{ "data-size-overflow", 36U, 0xffffffffU },
		{ "name-offset-outside", 40U, 0xffffffc0U },
		{ "input-index", 48U, GEN_TENSORS },
		{ "color", 52U, 1U },
		{ "default-size", 56U, 40U },
		{ "multiple", 60U, 16U },
		{ "classes", 68U, 79U },
		{ "stride", 80U, 24U },
		{ "fourth-stride", 88U, 64U },
		{ "level-tensor", 92U, 5U },
		{ "reserved", 140U, 1U },
		{ "tensor-channels", GEN_TENSOR_OFFSET + 16U * 2U, 0U },
		{ "tensor-divisor", GEN_TENSOR_OFFSET + 16U * 2U + 4U, 3U },
		{ "tensor-flag", GEN_TENSOR_OFFSET + 16U * 2U + 8U, 4U },
		{ "op-kind", GEN_OP_OFFSET + 64U * 1U, 7U },
		{ "activation-on-maxpool", GEN_OP_OFFSET + 64U * 3U + 4U, 1U },
		{ "conv-kernel", GEN_OP_OFFSET + 64U * 1U + 8U, 5U },
		{ "conv-stride", GEN_OP_OFFSET + 64U * 1U + 12U, 3U },
		{ "conv-groups", GEN_OP_OFFSET + 64U * 1U + 16U, 4U },
		{ "maxpool-kernel", GEN_OP_OFFSET + 64U * 3U + 8U, 7U },
		{ "add-inputs", GEN_OP_OFFSET + 64U * 4U + 20U, 1U },
		{ "input-out-of-table", GEN_OP_OFFSET + 64U * 2U + 24U, 99U },
		{ "unmade-input", GEN_OP_OFFSET + 64U * 2U + 24U, 9U },
		{ "made-twice", GEN_OP_OFFSET + 64U * 7U + 40U, 6U },
		{ "weight-count", GEN_OP_OFFSET + 64U * 1U + 48U, 7U },
		{ "weight-misaligned", GEN_OP_OFFSET + 64U * 1U + 44U, 2U },
		{ "weight-outside", GEN_OP_OFFSET + 64U * 1U + 44U, 0x7ffffff0U },
		{ "name-outside", GEN_OP_OFFSET + 64U * 1U + 60U, GEN_NAME_SIZE },
		{ "concat-channels", GEN_TENSOR_OFFSET + 16U * 12U, 30U },
		{ "head-divisor", GEN_TENSOR_OFFSET + 16U * 16U + 4U, 16U }
	};
	uint8_t *copy;
	unsigned index;
	int error;

	/* One copy per case, broken in one field. */
	copy = malloc(size);
	assert(copy != NULL);
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		memcpy(copy, bytes, size);
		assert(get32(copy, cases[index].offset) != cases[index].value);
		put32(copy, cases[index].offset, cases[index].value);
		error = gen_load(directory, cases[index].name, copy, size);
		if (error != YOLOX_ERROR_FORMAT) {
			fprintf(stderr, "znet-host-test: %s: %d (%s), expected the format error\n", cases[index].name, error,
			    yolox_reason(error));
			abort();
		}
	}

	/* A name table without its NUL, a cut file and an empty file. */
	memcpy(copy, bytes, size);
	memset(copy + GEN_NAME_OFFSET, 'x', GEN_NAME_SIZE);
	assert(gen_load(directory, "name-unterminated", copy, size) == YOLOX_ERROR_FORMAT);
	assert(gen_load(directory, "truncated", bytes, size - 4U) == YOLOX_ERROR_FORMAT);
	assert(gen_load(directory, "header-only", bytes, GEN_HEADER) == YOLOX_ERROR_FORMAT);
	error = gen_load(directory, "empty", bytes, 0U);
	assert(error == YOLOX_ERROR_FORMAT || error == YOLOX_ERROR_IO);
	free(copy);
	printf("broken files: PASS (%u cases)\n", (unsigned)(sizeof(cases) / sizeof(cases[0]) + 4U));
}

/* Checks the API's refusals of missing and wrong arguments. */
static void
case_arguments(
	const char *directory)
{
	struct yolox_runner_config config;
	struct yolox_detection detections[4];
	struct yolox_model *model;
	struct yolox_runner *runner;
	struct yolox_image image;
	char path[GEN_PATH_MAX];
	uint8_t pixels[12];
	uint32_t count;
	int error;

	/* Loading. */
	model = NULL;
	assert(yolox_model_load(NULL, &model) == YOLOX_ERROR_ARGUMENT);
	snprintf(path, sizeof(path), "%s/valid.znet", directory);
	assert(yolox_model_load(path, NULL) == YOLOX_ERROR_ARGUMENT);
	snprintf(path, sizeof(path), "%s/missing.znet", directory);
	assert(yolox_model_load(path, &model) == YOLOX_ERROR_IO);
	snprintf(path, sizeof(path), "%s/valid.znet", directory);
	error = yolox_model_load(path, &model);
	assert(error == 0);

	/* Runners: no model, a size off the multiple or out of range, no Vulkan backend yet. */
	yolox_runner_config_default(&config);
	assert(yolox_runner_create(NULL, &config, &runner) == YOLOX_ERROR_ARGUMENT);
	assert(yolox_runner_create(model, &config, NULL) == YOLOX_ERROR_ARGUMENT);
	config.backend = YOLOX_BACKEND_CPU;
	config.input_size = 100U;
	assert(yolox_runner_create(model, &config, &runner) == YOLOX_ERROR_ARGUMENT);
	config.input_size = 2048U;
	assert(yolox_runner_create(model, &config, &runner) == YOLOX_ERROR_ARGUMENT);
	config.input_size = 64U;
	runner = NULL;
	error = yolox_runner_create(model, &config, &runner);
	assert(error == 0);

	/* Detection: missing pieces, a row shorter than the width, a size of zero. */
	image.format = YOLOX_PIXEL_RGB8;
	image.width = 2U;
	image.height = 2U;
	image.row_bytes = 6U;
	memset(pixels, 0, sizeof(pixels));
	assert(yolox_detect(NULL, &image, pixels, detections, 4U, &count) == YOLOX_ERROR_ARGUMENT);
	assert(yolox_detect(runner, NULL, pixels, detections, 4U, &count) == YOLOX_ERROR_ARGUMENT);
	assert(yolox_detect(runner, &image, NULL, detections, 4U, &count) == YOLOX_ERROR_ARGUMENT);
	assert(yolox_detect(runner, &image, pixels, NULL, 4U, &count) == YOLOX_ERROR_ARGUMENT);
	assert(yolox_detect(runner, &image, pixels, detections, 4U, NULL) == YOLOX_ERROR_ARGUMENT);
	image.row_bytes = 5U;
	assert(yolox_detect(runner, &image, pixels, detections, 4U, &count) == YOLOX_ERROR_ARGUMENT);
	image.row_bytes = 6U;
	image.width = 0U;
	assert(yolox_detect(runner, &image, pixels, detections, 4U, &count) == YOLOX_ERROR_ARGUMENT);

	/* Reading a tensor needs a debug runner. */
	assert(yolox_runner_read_tensor(runner, 1U, NULL, 0U) != 0);

	/* Every code has a name. */
	assert(yolox_reason(YOLOX_ERROR_FORMAT) != NULL);
	assert(yolox_reason(-1000) != NULL);
	assert(yolox_class_name(0U) != NULL && yolox_class_name(80U) == NULL);

	yolox_runner_free(runner);
	yolox_model_free(model);
	printf("arguments: PASS\n");
}
