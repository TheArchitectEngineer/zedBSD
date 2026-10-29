/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the nvrtx compute launch of every generation: the QMD of
 * Turing (version 2.2), Ampere and Ada (3.0) checked field by field against
 * the positions read mechanically from open-gpu-doc (qmd-fields.h), every
 * bit outside those fields zero, Turing's output the same as the functions
 * that name no generation, and the launch's push buffer of each generation.
 */

#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-compute.h"
#include "plan/tools/nvrtx/qmd-fields.h"

/* One generation's launch: its kind, the field table of its QMD, and its launch method and action. */
struct generation {
	const char *name;
	struct nvrtx_compute_kind kind;
	const struct qmd_field *fields;
	uint32_t launch_method;
	uint32_t launch_action;
};

/* One launch's inputs. */
struct launch_case {
	uint32_t shared_bytes;
	uint32_t local[3];
	uint32_t gpr_count;
	uint32_t barrier_count;
	uint32_t cbufs;
};

static unsigned check(int ok, const char *generation, const char *what);
static uint64_t field_get(const uint32_t *qmd, const struct qmd_field *field, unsigned index);
static void field_mark(uint32_t *mask, const struct qmd_field *field, unsigned index);
static unsigned check_qmd(const struct generation *generation, const struct launch_case *test);
static unsigned check_launch(const struct generation *generation);
static unsigned check_turing_same(const struct launch_case *test);

/* Counts of the checks run. */
static unsigned checks;

/*
 * The generations: Turing's class with SEND_SIGNALING_PCAS_B (invalidate
 * and schedule), the later classes with SEND_SIGNALING_PCAS2_B
 * (invalidate, copy and schedule).
 *
 * Values referenced: NVIDIA open-gpu-doc classes/compute/clc5c0.h,
 * clc7c0.h and clc9c0.h (the class numbers, NVC5C0_SEND_SIGNALING_PCAS_B,
 * NVC7C0_SEND_SIGNALING_PCAS2_B and its INVALIDATE_COPY_SCHEDULE action).
 */
static const struct generation generations[] = {
	{ "TU10x", { 2U, 2U, 0xc5c0U }, qmd_fields_qmd22_turing, 0x02bcU, 0x3U },
	{ "GA10x", { 3U, 0U, 0xc7c0U }, qmd_fields_qmd30_ampere, 0x02c0U, 0x3U },
	{ "AD10x", { 3U, 0U, 0xc9c0U }, qmd_fields_qmd30_ada, 0x02c0U, 0x3U }
};

/* The launches: shared memory none to the most, workgroups of one warp to 32 warps, barriers, buffers. */
static const struct launch_case cases[] = {
	{ 0U, { 32U, 1U, 1U }, 24U, 0U, 1U },
	{ 1000U, { 256U, 1U, 1U }, 44U, 1U, 2U },
	{ 32768U, { 8U, 8U, 4U }, 64U, 1U, 4U },
	{ 49152U, { 1024U, 1U, 1U }, 32U, 0U, 8U },
	{ 65536U, { 64U, 2U, 1U }, 255U, 31U, 0U }
};

int
main(void)
{
	struct nvrtx_compute_kind unknown;
	struct nvrtx_qmd_info info;
	uint32_t qmd[NVRTX_QMD_WORDS];
	unsigned failures;
	unsigned generation;
	unsigned test;
	int error;

	/* Every launch in every generation's QMD, then each generation's push buffer. */
	failures = 0U;
	for (generation = 0U; generation < sizeof(generations) / sizeof(generations[0]); generation++) {
		for (test = 0U; test < sizeof(cases) / sizeof(cases[0]); test++)
			failures += check_qmd(&generations[generation], &cases[test]);
		failures += check_launch(&generations[generation]);
	}

	/* Turing's output is the one of the functions that name no generation. */
	for (test = 0U; test < sizeof(cases) / sizeof(cases[0]); test++)
		failures += check_turing_same(&cases[test]);

	/* A QMD version the driver does not write is refused. */
	memset(&info, 0, sizeof(info));
	info.program = 0x100000ULL;
	info.groups[0] = 1U;
	info.groups[1] = 1U;
	info.groups[2] = 1U;
	info.local_size[0] = 32U;
	info.local_size[1] = 1U;
	info.local_size[2] = 1U;
	info.gpr_count = 8U;
	unknown.qmd_major = 3U;
	unknown.qmd_minor = 1U;
	unknown.compute_class = 0xc7c0U;
	error = nvrtx_qmd_build_for(qmd, &info, &unknown);
	failures += check(error == EINVAL, "any", "QMD version 3.1 refused");
	unknown.qmd_major = 4U;
	unknown.qmd_minor = 0U;
	error = nvrtx_qmd_build_for(qmd, &info, &unknown);
	failures += check(error == EINVAL, "any", "QMD version 4.0 refused");

	/* Reports the verdict. */
	if (failures != 0U) {
		printf("FAIL nvrtx qmd: %u of %u checks\n", failures, checks);
		return 1;
	}

	printf("PASS nvrtx qmd: %u checks\n", checks);
	return 0;
}

/* Counts one check and names it when it fails; reports 1 for a failure. */
static unsigned
check(
	int ok,
	const char *generation,
	const char *what)
{
	/* Counts the check. */
	checks++;
	if (ok)
		return 0U;

	/* Names the failure. */
	printf("FAIL %s: %s\n", generation, what);
	return 1U;
}

/* Reads one field (of constant buffer index for a buffer's field) out of a QMD. */
static uint64_t
field_get(
	const uint32_t *qmd,
	const struct qmd_field *field,
	unsigned index)
{
	uint64_t value;
	unsigned bit;
	unsigned low;
	unsigned high;

	/* Gathers the field's bits, the lowest first. */
	low = field->low + index * field->stride;
	high = field->high + index * field->stride;
	value = 0U;
	for (bit = high + 1U; bit > low; bit--)
		value = (value << 1) | ((qmd[(bit - 1U) / 32U] >> ((bit - 1U) % 32U)) & 1U);

	/* The field's value. */
	return value;
}

/* Marks the bits of one field as known. */
static void
field_mark(
	uint32_t *mask,
	const struct qmd_field *field,
	unsigned index)
{
	unsigned bit;

	/* Every bit of the field. */
	for (bit = field->low + index * field->stride; bit <= field->high + index * field->stride; bit++)
		mask[bit / 32U] |= 1U << (bit % 32U);
}

/* Builds one launch's QMD in a generation's version and checks every field, and that nothing else is set. */
static unsigned
check_qmd(
	const struct generation *generation,
	const struct launch_case *test)
{
	const struct qmd_field *fields;
	struct nvrtx_qmd_info info;
	uint32_t qmd[NVRTX_QMD_WORDS];
	uint32_t mask[NVRTX_QMD_WORDS];
	uint64_t value;
	uint64_t minimum;
	uint64_t target;
	unsigned failures;
	unsigned index;
	unsigned field;
	unsigned word;
	char what[160];
	int error;
	int stray;

	/* The launch: a grid of 7x3x2, the program and its buffers on their boundaries. */
	fields = generation->fields;
	memset(&info, 0, sizeof(info));
	info.program = 0x123456700ULL;
	info.groups[0] = 7U;
	info.groups[1] = 3U;
	info.groups[2] = 2U;
	info.local_size[0] = test->local[0];
	info.local_size[1] = test->local[1];
	info.local_size[2] = test->local[2];
	info.gpr_count = test->gpr_count;
	info.shared_bytes = test->shared_bytes;
	info.barrier_count = test->barrier_count;
	for (index = 0U; index < test->cbufs; index++) {
		info.cbuf_address[index] = 0x1ff0000000ULL + (uint64_t)index * 0x10000ULL;
		info.cbuf_size[index] = 0x100U * (index + 1U);
	}
	memset(qmd, 0xa5, sizeof(qmd));
	error = nvrtx_qmd_build_for(qmd, &info, &generation->kind);
	snprintf(what, sizeof(what), "QMD of shared %u, %ux%ux%u built", test->shared_bytes, test->local[0], test->local[1], test->local[2]);
	failures = check(error == 0, generation->name, what);
	if (error != 0)
		return failures;

	/* Every field the driver writes is in the generation's header. */
	for (field = 0U; field < QMD_F_COUNT; field++) {
		if (field == QMD_F_SHARED_ALLOCATION_ENABLE || field == QMD_F_SAMPLER_INDEX)
			continue;
		snprintf(what, sizeof(what), "the header has %s", fields[field].name);
		failures += check(fields[field].present != 0, generation->name, what);
	}
	if (failures != 0U)
		return failures;

	/* The version, caching on, no call limit, sampler indexes in their pool. */
	failures += check(field_get(qmd, &fields[QMD_F_QMD_VERSION], 0U) == generation->kind.qmd_minor, generation->name, "QMD_VERSION");
	failures += check(field_get(qmd, &fields[QMD_F_QMD_MAJOR_VERSION], 0U) == generation->kind.qmd_major, generation->name, "QMD_MAJOR_VERSION");
	failures += check(field_get(qmd, &fields[QMD_F_SM_GLOBAL_CACHING_ENABLE], 0U) == 1U, generation->name, "SM_GLOBAL_CACHING_ENABLE");
	failures += check(field_get(qmd, &fields[QMD_F_API_VISIBLE_CALL_LIMIT], 0U) == 1U, generation->name, "API_VISIBLE_CALL_LIMIT no check");
	failures += check(field_get(qmd, &fields[QMD_F_SAMPLER_INDEX], 0U) == 0U, generation->name, "SAMPLER_INDEX independently");

	/* The grid and the workgroup. */
	failures += check(field_get(qmd, &fields[QMD_F_CTA_RASTER_WIDTH], 0U) == 7U &&
	    field_get(qmd, &fields[QMD_F_CTA_RASTER_HEIGHT], 0U) == 3U &&
	    field_get(qmd, &fields[QMD_F_CTA_RASTER_DEPTH], 0U) == 2U, generation->name, "CTA_RASTER");
	failures += check(field_get(qmd, &fields[QMD_F_CTA_THREAD_DIMENSION0], 0U) == test->local[0] &&
	    field_get(qmd, &fields[QMD_F_CTA_THREAD_DIMENSION1], 0U) == test->local[1] &&
	    field_get(qmd, &fields[QMD_F_CTA_THREAD_DIMENSION2], 0U) == test->local[2], generation->name, "CTA_THREAD_DIMENSION");

	/* The registers and the barriers. */
	failures += check(field_get(qmd, &fields[QMD_F_REGISTER_COUNT_V], 0U) == test->gpr_count, generation->name, "REGISTER_COUNT_V");
	failures += check(field_get(qmd, &fields[QMD_F_BARRIER_COUNT], 0U) == test->barrier_count, generation->name, "BARRIER_COUNT");

	/*
	 * The shared memory in 256-byte units, and the carve-outs coded as
	 * KiB / 4 + 1: Turing's 32 or 64 KiB for the least and the target,
	 * never below the workgroup's need, and 64 KiB the most.
	 */
	value = field_get(qmd, &fields[QMD_F_SHARED_MEMORY_SIZE], 0U);
	failures += check(value == ((test->shared_bytes + 0xffU) & ~0xffU), generation->name, "SHARED_MEMORY_SIZE in 256-byte units");
	minimum = field_get(qmd, &fields[QMD_F_MIN_SM_CONFIG_SHARED_MEM_SIZE], 0U);
	target = field_get(qmd, &fields[QMD_F_TARGET_SM_CONFIG_SHARED_MEM_SIZE], 0U);
	failures += check((minimum == 9U || minimum == 17U) && (minimum - 1U) * 4U * 1024U >= test->shared_bytes,
	    generation->name, "MIN_SM_CONFIG_SHARED_MEM_SIZE a carve-out the workgroup fits");
	failures += check((target == 9U || target == 17U) && target >= minimum, generation->name, "TARGET_SM_CONFIG_SHARED_MEM_SIZE a carve-out");
	failures += check(field_get(qmd, &fields[QMD_F_MAX_SM_CONFIG_SHARED_MEM_SIZE], 0U) == 17U, generation->name, "MAX_SM_CONFIG_SHARED_MEM_SIZE 64 KiB");
	if (fields[QMD_F_SHARED_ALLOCATION_ENABLE].present != 0)
		failures += check(field_get(qmd, &fields[QMD_F_SHARED_ALLOCATION_ENABLE], 0U) == 0U, generation->name, "SHARED_ALLOCATION_ENABLE off");

	/* The constant buffers: valid, address and size in 16-byte units for the bound ones, nothing for the others. */
	for (index = 0U; index < NVRTX_QMD_CBUFS; index++) {
		snprintf(what, sizeof(what), "constant buffer %u", index);
		if (index < test->cbufs) {
			failures += check(field_get(qmd, &fields[QMD_F_CONSTANT_BUFFER_VALID], index) == 1U &&
			    field_get(qmd, &fields[QMD_F_CONSTANT_BUFFER_ADDR_LOWER], index) == (info.cbuf_address[index] & 0xffffffffU) &&
			    field_get(qmd, &fields[QMD_F_CONSTANT_BUFFER_ADDR_UPPER], index) == (info.cbuf_address[index] >> 32) &&
			    field_get(qmd, &fields[QMD_F_CONSTANT_BUFFER_SIZE_SHIFTED4], index) == info.cbuf_size[index] / 16U,
			    generation->name, what);
		} else {
			failures += check(field_get(qmd, &fields[QMD_F_CONSTANT_BUFFER_VALID], index) == 0U, generation->name, what);
		}
	}

	/* The program's address. */
	failures += check(field_get(qmd, &fields[QMD_F_PROGRAM_ADDRESS_LOWER], 0U) == 0x23456700U &&
	    field_get(qmd, &fields[QMD_F_PROGRAM_ADDRESS_UPPER], 0U) == 0x1U, generation->name, "PROGRAM_ADDRESS");

	/* No bit outside the header's fields is set (a field written at another version's place shows here). */
	memset(mask, 0, sizeof(mask));
	for (field = 0U; field < QMD_F_COUNT; field++) {
		if (fields[field].present == 0)
			continue;
		if (fields[field].stride == 0U) {
			field_mark(mask, &fields[field], 0U);
			continue;
		}
		for (index = 0U; index < NVRTX_QMD_CBUFS; index++)
			field_mark(mask, &fields[field], index);
	}
	stray = 0;
	for (word = 0U; word < NVRTX_QMD_WORDS; word++) {
		if ((qmd[word] & ~mask[word]) != 0U) {
			printf("info %s: word %u has 0x%08x outside the fields\n", generation->name, word, qmd[word] & ~mask[word]);
			stray = 1;
		}
	}
	snprintf(what, sizeof(what), "QMD of shared %u: nothing outside the fields", test->shared_bytes);
	failures += check(stray == 0, generation->name, what);

	/* The count of failed checks. */
	return failures;
}

/* Decodes a generation's launch push buffer and compares its methods and values. */
static unsigned
check_launch(
	const struct generation *generation)
{
	/*
	 * The methods of a launch, in order, with their values.
	 *
	 * Values referenced: NVIDIA open-gpu-doc classes/compute/clc5c0.h and
	 * clc7c0.h (SET_OBJECT, SET_SHADER_SHARED_MEMORY_WINDOW_A,
	 * SET_SHADER_LOCAL_MEMORY_WINDOW_A, INVALIDATE_SHADER_CACHES and its
	 * INSTRUCTION, DATA and CONSTANT bits, SEND_PCAS_A).
	 */
	const uint32_t want[][3] = {
		{ 0x0000U, 1U, generation->kind.compute_class },
		{ 0x02a0U, 2U, 0U },
		{ 0x07b0U, 2U, 0U },
		{ 0x021cU, 1U, 0x1011U },
		{ 0x02b4U, 1U, 0x12345600U >> 8 },
		{ generation->launch_method, 1U, generation->launch_action }
	};
	uint8_t push[256];
	uint32_t header;
	uint32_t value;
	size_t length;
	size_t at;
	unsigned failures;
	unsigned index;
	char what[160];

	/* The push buffer of a launch of the QMD at 0x12345600. */
	memset(push, 0, sizeof(push));
	length = nvrtx_push_compute_launch_for(push, 0x12345600ULL, &generation->kind);

	/* Each method: an incrementing header on the compute subchannel, then its first value. */
	failures = 0U;
	at = 0U;
	for (index = 0U; index < sizeof(want) / sizeof(want[0]); index++) {
		memcpy(&header, push + at, 4U);
		memcpy(&value, push + at + 4U, 4U);
		snprintf(what, sizeof(what), "launch method %u: 0x%04x of %u words, first 0x%08x", index, want[index][0], want[index][1], want[index][2]);
		failures += check(header == ((1U << 29) | (want[index][1] << 16) | (NVRTX_COMPUTE_SUBCHANNEL << 13) | (want[index][0] >> 2)) &&
		    value == want[index][2], generation->name, what);
		at += 4U + 4U * want[index][1];
	}
	failures += check(length == at, generation->name, "launch length");

	/* The count of failed checks. */
	return failures;
}

/* Compares Turing's QMD and launch with the output of the functions that name no generation. */
static unsigned
check_turing_same(
	const struct launch_case *test)
{
	struct nvrtx_qmd_info info;
	uint32_t old[NVRTX_QMD_WORDS];
	uint32_t new[NVRTX_QMD_WORDS];
	uint8_t old_push[256];
	uint8_t new_push[256];
	size_t old_length;
	size_t new_length;
	unsigned failures;
	unsigned index;
	int old_error;
	int new_error;

	/* The same launch as check_qmd(). */
	memset(&info, 0, sizeof(info));
	info.program = 0x123456700ULL;
	info.groups[0] = 7U;
	info.groups[1] = 3U;
	info.groups[2] = 2U;
	info.local_size[0] = test->local[0];
	info.local_size[1] = test->local[1];
	info.local_size[2] = test->local[2];
	info.gpr_count = test->gpr_count;
	info.shared_bytes = test->shared_bytes;
	info.barrier_count = test->barrier_count;
	for (index = 0U; index < test->cbufs; index++) {
		info.cbuf_address[index] = 0x1ff0000000ULL + (uint64_t)index * 0x10000ULL;
		info.cbuf_size[index] = 0x100U * (index + 1U);
	}

	/* Byte for byte the same QMD. */
	old_error = nvrtx_qmd_build(old, &info);
	new_error = nvrtx_qmd_build_for(new, &info, &generations[0].kind);
	failures = check(old_error == 0 && new_error == 0 && memcmp(old, new, sizeof(old)) == 0, "TU10x", "QMD the same as nvrtx_qmd_build()");

	/* Byte for byte the same launch. */
	memset(old_push, 0, sizeof(old_push));
	memset(new_push, 0, sizeof(new_push));
	old_length = nvrtx_push_compute_launch(old_push, 0x12345600ULL);
	new_length = nvrtx_push_compute_launch_for(new_push, 0x12345600ULL, &generations[0].kind);
	failures += check(old_length == new_length && memcmp(old_push, new_push, sizeof(old_push)) == 0, "TU10x", "launch the same as nvrtx_push_compute_launch()");

	/* The count of failed checks. */
	return failures;
}
