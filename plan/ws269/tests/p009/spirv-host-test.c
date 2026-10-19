/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR driver's SPIR-V subset (spirv-fill.c) on
 * modules glslc compiled from the shaders directory, and of the fill program
 * (compute-program.c) the driver builds for a shader of the subset.
 *
 *   spirv-host-test DIR    (DIR holds NAME.spv for each shader)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/compute-program.h"
#include "drivers/gpu/powervr/spirv-fill.h"

/* What one module is expected to give: the parse's result and, when 0, the fill. */
struct expectation {
	const char *name;
	int result;
	struct powervr_spirv_fill fill;
};

static const struct expectation expectations[] = {
	{ "fill", 0, { 32, 0, 0, 0, 3, 7 } },
	{ "index", 0, { 16, 1, 2, 0, 1, 0 } },
	{ "offset", 0, { 32, 0, 0, 4, 4, 0xffffffffU } },
	{ "constant", 0, { 8, 0, 0, 0, 0, 42 } },
	{ "reject-read", ENOTSUP, { 0, 0, 0, 0, 0, 0 } },
	{ "reject-size", ENOTSUP, { 0, 0, 0, 0, 0, 0 } },
	{ "reject-local", ENOTSUP, { 0, 0, 0, 0, 0, 0 } },
	{ "reject-branch", ENOTSUP, { 0, 0, 0, 0, 0, 0 } },
};

static int failures;
static int passes;
static struct powervr_spirv_value values[POWERVR_SPIRV_FILL_IDS];

static void
check(
	int condition,
	const char *what,
	const char *name)
{
	if (condition) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: %s: %s\n", name, what);
}

static uint32_t *
load(
	const char *directory,
	const char *name,
	size_t *count)
{
	char path[4096];
	uint32_t *words;
	FILE *file;
	long bytes;

	snprintf(path, sizeof(path), "%s/%s.spv", directory, name);
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	bytes = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)bytes);
	if (words == NULL || fread(words, 1, (size_t)bytes, file) != (size_t)bytes) {
		fclose(file);
		free(words);
		return NULL;
	}

	fclose(file);
	*count = (size_t)bytes / 4U;
	return words;
}

static void
check_modules(
	const char *directory)
{
	struct powervr_spirv_fill fill;
	const struct expectation *expect;
	uint32_t *words;
	size_t count;
	size_t index;
	int result;

	for (index = 0; index < sizeof(expectations) / sizeof(expectations[0]); index++) {
		expect = &expectations[index];
		words = load(directory, expect->name, &count);
		check(words != NULL, "module loads", expect->name);
		if (words == NULL)
			continue;

		memset(&fill, 0xa5, sizeof(fill));
		result = powervr_spirv_fill_parse(words, count, values, &fill);
		check(result == expect->result, "parse result", expect->name);
		if (result == 0 && expect->result == 0) {
			check(fill.local_x == expect->fill.local_x, "local_x", expect->name);
			check(fill.set == expect->fill.set, "set", expect->name);
			check(fill.binding == expect->fill.binding, "binding", expect->name);
			check(fill.index_addend == expect->fill.index_addend, "index addend", expect->name);
			check(fill.multiplier == expect->fill.multiplier, "multiplier", expect->name);
			check(fill.addend == expect->fill.addend, "addend", expect->name);
		}

		/* A module cut short, or with a broken magic, is not a module. */
		result = powervr_spirv_fill_parse(words, count - 1U, values, &fill);
		check(result != 0, "a module cut by a word is refused", expect->name);
		words[0] ^= 1U;
		result = powervr_spirv_fill_parse(words, count, values, &fill);
		check(result == EINVAL, "a broken magic is EINVAL", expect->name);
		free(words);
	}
}

/*
 * The fill program: its length fits the driver's 512-byte room and ends
 * on eight bytes; the multiplier and the addend are the only bytes that
 * change with them (the immediates are stored as their little-endian
 * words); the refusals.
 */
static void
check_fill_program(void)
{
	static uint8_t first[0x1000];
	static uint8_t second[0x1000];
	size_t first_length;
	size_t second_length;
	size_t index;
	size_t differing;
	int error;

	error = powervr_compute_fill_program(0x8000100000ULL, 32, 0, 0, first, sizeof(first), &first_length);
	check(error == 0, "fill program builds", "fill");
	check(first_length <= 0x200U, "fill program fits its 512-byte room", "fill");
	check((first_length & 7U) == 0, "fill program ends on eight bytes", "fill");

	error = powervr_compute_fill_program(0x8000100000ULL, 32, 0x11223344U, 0x55667788U, second, sizeof(second), &second_length);
	check(error == 0 && second_length == first_length, "fill program length does not depend on the values", "fill");
	differing = 0;
	for (index = 0; index < first_length; index++) {
		if (first[index] != second[index])
			differing++;
	}
	check(differing == 8, "only the eight immediate bytes differ", "fill");
	check(memmem(second, second_length, "\x44\x33\x22\x11", 4) != NULL, "the multiplier's bytes are in the program", "fill");
	check(memmem(second, second_length, "\x88\x77\x66\x55", 4) != NULL, "the addend's bytes are in the program", "fill");
	check(memmem(second, second_length, "\x00\x00\x10\x00", 4) != NULL, "the base's low half is in the program", "fill");

	error = powervr_compute_fill_program(0x8000100000ULL, 0, 0, 0, first, sizeof(first), &first_length);
	check(error == EINVAL, "no instance is refused", "fill");
	error = powervr_compute_fill_program(0x8000100000ULL, 33, 0, 0, first, sizeof(first), &first_length);
	check(error == EINVAL, "more than a task is refused", "fill");
	error = powervr_compute_fill_program(0x8000100002ULL, 32, 0, 0, first, sizeof(first), &first_length);
	check(error == EINVAL, "a misaligned base is refused", "fill");
	error = powervr_compute_fill_program(0x80ffffff80ULL + 4U, 32, 0, 0, first, sizeof(first), &first_length);
	check(error == EINVAL, "words across 4 GiB are refused", "fill");
	error = powervr_compute_fill_program(0x80ffffff80ULL, 32, 0, 0, first, sizeof(first), &first_length);
	check(error == 0, "words up to 4 GiB are taken", "fill");
}

int
main(
	int argc,
	char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: spirv-host-test DIR\n");
		return 2;
	}

	check_modules(argv[1]);
	check_fill_program();
	printf("powervr spirv host test: %d passed, %d failed\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
