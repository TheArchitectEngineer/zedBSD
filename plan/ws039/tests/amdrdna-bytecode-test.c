/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test (j) of ws039-p014: the driver's runner of the video BIOS's
 * command routines (src/drivers/gpu/amdrdna/bytecode.c) against the
 * reference runner (Linux v6.19 atom.c, built on the host by
 * amdrdna-bytecode-test.sh from the temp tree, not kept in the tree), on
 * the board's real image.
 *
 * Every routine the image has is run by both, three times each with
 * different parameters, over the same model of the registers (a register
 * reads what was last written to it, or a fixed mix of its index); each
 * run's register reads and writes and waits are recorded in order, up to
 * a limit after which the run is cut off (a routine that waits for a bit
 * the model never sets would loop forever).  The two records, the
 * parameters after the run and the scratch area must be equal; the
 * synthetic routines of the second part check what the image may not
 * reach (the bounds, the depth, an unknown opcode).
 *
 * usage: amdrdna-bytecode-test IMAGE
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-bytecode.h"

/* The reference's interface, as its atom.h declares it (the reference is linked in, not included). */
struct card_info {
	void *dev;
	void (*reg_write)(struct card_info *info, uint32_t reg, uint32_t val);
	uint32_t (*reg_read)(struct card_info *info, uint32_t reg);
	void (*mc_write)(struct card_info *info, uint32_t reg, uint32_t val);
	uint32_t (*mc_read)(struct card_info *info, uint32_t reg);
	void (*pll_write)(struct card_info *info, uint32_t reg, uint32_t val);
	uint32_t (*pll_read)(struct card_info *info, uint32_t reg);
};

struct oracle_context {
	struct card_info *card;
	int mutex;
	void *bios;
	uint32_t cmd_table;
	uint32_t data_table;
	uint16_t *iio;
	uint16_t data_block;
	uint32_t fb_base;
	uint32_t divmul[2];
	uint16_t io_attr;
	uint16_t reg_block;
	uint8_t shift;
	int cs_equal;
	int cs_above;
	int io_mode;
	uint32_t *scratch;
	int scratch_size_bytes;
};

void *amdgpu_atom_parse(struct card_info *card, void *bios);
int amdgpu_atom_execute_table(void *ctx, int index, uint32_t *params, int params_size);

/* The reference's jiffies (always 0) and debug switch. */
unsigned long jiffies;

/* The parameter dwords of every run, the scratch bytes, the record's limit, and the image's padding for the reference. */
#define TEST_PARAMETERS			64U
#define TEST_SCRATCH_BYTES		(20U * 1024U)
#define TEST_EVENTS			4096U
#define TEST_PADDING			65536U
#define TEST_PATTERNS			3U

/* The register model: a written register's value, by an open hash of its index. */
#define MODEL_SLOTS			65536U

/* One recorded access: a read, a write or a wait. */
enum event_kind {
	EVENT_READ,
	EVENT_WRITE,
	EVENT_WAIT
};

struct event {
	enum event_kind kind;
	uint32_t index;
	uint32_t value;
};

/* One run's record. */
struct record {
	struct event events[TEST_EVENTS];
	unsigned count;
	bool cut;
};

/* The register model's slots: used, index, value. */
struct slot {
	bool used;
	uint32_t index;
	uint32_t value;
};

static struct slot model[MODEL_SLOTS];
static struct record *recording;
static jmp_buf cut_off;
static unsigned checks;
static unsigned failures;

static void check(int ok, const char *format, ...);
static uint32_t model_read(uint32_t index);
static void model_write(uint32_t index, uint32_t value);
static void model_reset(void);
static void record_event(enum event_kind kind, uint32_t index, uint32_t value);
static uint32_t ours_read(void *owner, uint32_t index);
static void ours_write(void *owner, uint32_t index, uint32_t value);
static void ours_wait(void *owner, uint32_t microseconds);
static uint32_t theirs_read(struct card_info *info, uint32_t reg);
static void theirs_write(struct card_info *info, uint32_t reg, uint32_t value);
static uint32_t theirs_zero(struct card_info *info, uint32_t reg);
static void theirs_drop(struct card_info *info, uint32_t reg, uint32_t value);
static void fill_parameters(uint32_t *parameters, unsigned pattern);
static uint8_t *read_file(const char *path, size_t *length);
static void test_image(const uint8_t *image, size_t length);
static void test_synthetic(void);

/* What the kernel's allocation is on the host. */
void *
kern_calloc(size_t count, size_t size)
{
	return calloc(count, size);
}

void
kern_free(void *pointer)
{
	free(pointer);
}

void *
kern_memset(void *to, int value, size_t length)
{
	return memset(to, value, length);
}

/* A wait of the reference. */
void
oracle_wait(uint32_t microseconds)
{
	record_event(EVENT_WAIT, 0U, microseconds);
}

int
main(int argc, char **argv)
{
	uint8_t *image;
	size_t length;

	if (argc != 2) {
		fprintf(stderr, "usage: amdrdna-bytecode-test IMAGE\n");
		return 2;
	}
	image = read_file(argv[1], &length);
	if (image == NULL) {
		fprintf(stderr, "amdrdna-bytecode-test: cannot read %s\n", argv[1]);
		return 2;
	}
	test_image(image, length);
	test_synthetic();
	free(image);
	printf("%s amdrdna bytecode: %u checks, %u failures\n", failures == 0U ? "PASS" : "FAIL", checks, failures);
	return failures == 0U ? 0 : 1;
}

static void
check(int ok, const char *format, ...)
{
	va_list arguments;

	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* A register reads what was last written to it, or a mix of its index. */
static uint32_t
model_read(uint32_t index)
{
	uint32_t at;

	at = (index * 2654435761U) % MODEL_SLOTS;
	while (model[at].used) {
		if (model[at].index == index)
			return model[at].value;
		at = (at + 1U) % MODEL_SLOTS;
	}
	return (index * 0x9e3779b1U) ^ 0x00ff00f0U;
}

static void
model_write(uint32_t index, uint32_t value)
{
	uint32_t at;

	at = (index * 2654435761U) % MODEL_SLOTS;
	while (model[at].used && model[at].index != index)
		at = (at + 1U) % MODEL_SLOTS;
	model[at].used = true;
	model[at].index = index;
	model[at].value = value;
}

static void
model_reset(void)
{
	memset(model, 0, sizeof(model));
}

/* Records an access; past the limit the run is cut off. */
static void
record_event(enum event_kind kind, uint32_t index, uint32_t value)
{
	if (recording->count >= TEST_EVENTS) {
		recording->cut = true;
		longjmp(cut_off, 1);
	}
	recording->events[recording->count].kind = kind;
	recording->events[recording->count].index = index;
	recording->events[recording->count].value = value;
	recording->count++;
}

static uint32_t
ours_read(void *owner, uint32_t index)
{
	uint32_t value;

	(void)owner;
	value = model_read(index);
	record_event(EVENT_READ, index, value);
	return value;
}

static void
ours_write(void *owner, uint32_t index, uint32_t value)
{
	(void)owner;
	record_event(EVENT_WRITE, index, value);
	model_write(index, value);
}

static void
ours_wait(void *owner, uint32_t microseconds)
{
	(void)owner;
	record_event(EVENT_WAIT, 0U, microseconds);
}

static uint32_t
theirs_read(struct card_info *info, uint32_t reg)
{
	return ours_read(info, reg);
}

static void
theirs_write(struct card_info *info, uint32_t reg, uint32_t value)
{
	ours_write(info, reg, value);
}

/* The PLL and memory controller spaces of the reference's driver: reads 0, writes nothing (amdgpu_atombios.c). */
static uint32_t
theirs_zero(struct card_info *info, uint32_t reg)
{
	(void)info;
	(void)reg;
	return 0U;
}

static void
theirs_drop(struct card_info *info, uint32_t reg, uint32_t value)
{
	(void)info;
	(void)reg;
	(void)value;
}

/* The parameters of a run: zeros, a mix of large values, small values. */
static void
fill_parameters(uint32_t *parameters, unsigned pattern)
{
	volatile unsigned index;

	for (index = 0U; index < TEST_PARAMETERS; index++) {
		if (pattern == 0U)
			parameters[index] = 0U;
		else if (pattern == 1U)
			parameters[index] = (0x01020304U * (index + 1U)) ^ 0x80008000U;
		else
			parameters[index] = (index * 7U + 1U) & 0x3U;
	}
}

static uint8_t *
read_file(const char *path, size_t *length)
{
	FILE *file;
	uint8_t *bytes;
	long size;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0L, SEEK_END);
	size = ftell(file);
	fseek(file, 0L, SEEK_SET);
	bytes = calloc(1U, (size_t)size + TEST_PADDING);
	if (bytes == NULL || fread(bytes, 1U, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(bytes);
		return NULL;
	}
	fclose(file);
	*length = (size_t)size;
	return bytes;
}

/*
 * Every routine of the image, three parameter patterns each, run by both
 * runners over the same register model; the records, the parameters and
 * the scratch areas compared.
 */
static void
test_image(const uint8_t *image, size_t length)
{
	static struct record ours_record;
	static struct record theirs_record;
	struct amdrdna_bytecode_access access;
	struct amdrdna_bytecode runner;
	struct card_info card;
	struct oracle_context *theirs;
	uint32_t ours_parameters[TEST_PARAMETERS];
	uint32_t theirs_parameters[TEST_PARAMETERS];
	uint32_t *ours_scratch;
	uint32_t *theirs_scratch;
	uint8_t *theirs_image;
	volatile unsigned routine;
	volatile unsigned pattern;
	volatile unsigned routines;
	volatile unsigned cut;
	unsigned index;
	volatile int ours_result;
	volatile int theirs_result;
	int error;
	bool same;

	/* Both runners over the image (the reference's copy padded with zeros it may read past the end). */
	access.owner = NULL;
	access.read = ours_read;
	access.write = ours_write;
	access.wait = ours_wait;
	ours_scratch = calloc(1U, TEST_SCRATCH_BYTES);
	theirs_scratch = calloc(1U, TEST_SCRATCH_BYTES);
	error = amdrdna_bytecode_open(&runner, image, (uint32_t)length, &access, ours_scratch, TEST_SCRATCH_BYTES);
	check(error == 0, "image: the runner opens it (%d)", error);
	if (error != 0)
		return;
	theirs_image = calloc(1U, length + TEST_PADDING);
	memcpy(theirs_image, image, length);
	memset(&card, 0, sizeof(card));
	card.reg_read = theirs_read;
	card.reg_write = theirs_write;
	card.mc_read = theirs_zero;
	card.mc_write = theirs_drop;
	card.pll_read = theirs_zero;
	card.pll_write = theirs_drop;
	theirs = amdgpu_atom_parse(&card, theirs_image);
	check(theirs != NULL, "image: the reference opens it");
	if (theirs == NULL)
		return;
	theirs->scratch = theirs_scratch;
	theirs->scratch_size_bytes = (int)TEST_SCRATCH_BYTES;
	check(runner.command_list == theirs->cmd_table && runner.data_list == theirs->data_table,
	    "image: the same lists (0x%x 0x%x, the reference's 0x%x 0x%x)", runner.command_list, runner.data_list,
	    theirs->cmd_table, theirs->data_table);
	same = true;
	for (index = 0U; index < AMDRDNA_BYTECODE_IO_PROGRAMS; index++) {
		if (runner.io_programs[index] != theirs->iio[index])
			same = false;
	}
	check(same, "image: the same register programs");

	/* Each routine, each pattern. */
	routines = 0U;
	cut = 0U;
	for (routine = 0U; routine < 256U; routine++) {
		if (!amdrdna_bytecode_has(&runner, routine))
			continue;
		routines++;
		for (pattern = 0U; pattern < TEST_PATTERNS; pattern++) {
			/* Ours. */
			fill_parameters(ours_parameters, pattern);
			memset(ours_scratch, 0, TEST_SCRATCH_BYTES);
			memset(&ours_record, 0, sizeof(ours_record));
			model_reset();
			recording = &ours_record;
			ours_result = -1;
			if (setjmp(cut_off) == 0)
				ours_result = amdrdna_bytecode_run(&runner, routine, ours_parameters, TEST_PARAMETERS);

			/* The reference's. */
			fill_parameters(theirs_parameters, pattern);
			memset(theirs_scratch, 0, TEST_SCRATCH_BYTES);
			memset(&theirs_record, 0, sizeof(theirs_record));
			model_reset();
			recording = &theirs_record;
			theirs_result = -1;
			if (setjmp(cut_off) == 0)
				theirs_result = amdgpu_atom_execute_table(theirs, (int)routine, theirs_parameters, (int)TEST_PARAMETERS);

			/* The records, to the shorter's end when one was cut off. */
			if (ours_record.cut)
				cut++;
			same = (ours_record.count == theirs_record.count && ours_record.cut == theirs_record.cut);
			for (index = 0U; index < ours_record.count && index < theirs_record.count; index++) {
				if (ours_record.events[index].kind != theirs_record.events[index].kind ||
				    ours_record.events[index].index != theirs_record.events[index].index ||
				    ours_record.events[index].value != theirs_record.events[index].value) {
					printf("  routine 0x%02x pattern %u: event %u differs: ours %d 0x%x 0x%x, the reference's %d 0x%x 0x%x\n",
					    routine, pattern, index, (int)ours_record.events[index].kind, ours_record.events[index].index,
					    ours_record.events[index].value, (int)theirs_record.events[index].kind,
					    theirs_record.events[index].index, theirs_record.events[index].value);
					same = false;
					break;
				}
			}
			if (!same && ours_record.count != theirs_record.count)
				printf("  routine 0x%02x pattern %u: %u events (cut %d, result %d, failure %s at 0x%x), the reference's %u (cut %d, result %d)\n",
				    routine, pattern, ours_record.count, (int)ours_record.cut, ours_result,
				    runner.failure != NULL ? runner.failure : "none", runner.failed_at, theirs_record.count,
				    (int)theirs_record.cut, theirs_result);
			check(same, "routine 0x%02x pattern %u: the same register reads, writes and waits (%u events)", routine, pattern,
			    ours_record.count);

			/* A run that ended: its result, its parameters and the scratch area. */
			if (ours_record.cut || theirs_record.cut)
				continue;
			check((ours_result == 0) == (theirs_result == 0), "routine 0x%02x pattern %u: the same outcome (%d, %d)", routine,
			    pattern, ours_result, theirs_result);
			check(memcmp(ours_parameters, theirs_parameters, sizeof(ours_parameters)) == 0,
			    "routine 0x%02x pattern %u: the same parameters after the run", routine, pattern);
			check(memcmp(ours_scratch, theirs_scratch, TEST_SCRATCH_BYTES) == 0, "routine 0x%02x pattern %u: the same scratch",
			    routine, pattern);
			check(runner.shift == theirs->shift && runner.io_attributes == theirs->io_attr &&
			    runner.product_low == theirs->divmul[0] && runner.product_high == theirs->divmul[1] &&
			    runner.equal == (theirs->cs_equal != 0) && runner.above == (theirs->cs_above != 0),
			    "routine 0x%02x pattern %u: the same state after the run", routine, pattern);
		}
	}
	printf("  image: %u routines, %u runs cut off at %u events\n", routines, cut, TEST_EVENTS);
	check(routines > 0U, "image: routines run");
	free(ours_scratch);
	free(theirs_scratch);
}

/*
 * Synthetic images: a routine that reads past the image, one that calls
 * itself without end, one that loops without end, and one with the
 * basics (a copy into a register's byte, a branch table, a call, a wait).
 */
static void
test_synthetic(void)
{
	static struct record record;
	static uint8_t image[1024];
	struct amdrdna_bytecode_access access;
	struct amdrdna_bytecode runner;
	uint32_t parameters[4];
	uint32_t at;
	int error;

	/* The marks, the header at 0x100 with the lists at 0x140 (routines, 64 bytes) and 0x180 (data). */
	memset(image, 0, sizeof(image));
	image[0] = 0x55U;
	image[1] = 0xaaU;
	memcpy(image + 0x30, " 761295520", 10U);
	image[0x48] = 0x00U;
	image[0x49] = 0x01U;
	memcpy(image + 0x104, "ATOM", 4U);
	image[0x140] = 0x40U;
	image[0x11e] = 0x40U;
	image[0x11f] = 0x01U;
	image[0x120] = 0x80U;
	image[0x121] = 0x01U;

	/* Routine 0 at 0x200: copy the byte 0x5a into register 0x10's bits 15:8, then end. */
	image[0x144] = 0x00U;
	image[0x145] = 0x02U;
	at = 0x200U + 6U;
	image[at++] = 1U;
	image[at++] = (uint8_t)(5U | (4U << 3) | (1U << 6));
	image[at++] = 0x10U;
	image[at++] = 0x00U;
	image[at++] = 0x5aU;
	image[at++] = 91U;

	/* Routine 1 at 0x240: calls itself. */
	image[0x146] = 0x40U;
	image[0x147] = 0x02U;
	at = 0x240U + 6U;
	image[at++] = 82U;
	image[at++] = 1U;
	image[at++] = 91U;

	/* Routine 2 at 0x280: a branch to itself. */
	image[0x148] = 0x80U;
	image[0x149] = 0x02U;
	at = 0x280U + 6U;
	image[at++] = 67U;
	image[at++] = 6U;
	image[at++] = 0U;

	/* Routine 3 at 0x3fa: its code runs past the image's end. */
	image[0x14a] = 0xfaU;
	image[0x14b] = 0x03U;

	/* Routine 4 at 0x2c0: a branch table on parameter 0 (1 goes to the wait, 2 to the end, others on to a branch to the wait), the wait of 7 us, end. */
	image[0x14c] = 0xc0U;
	image[0x14d] = 0x02U;
	image[0x2c5] = 4U;
	at = 0x2c0U + 6U;
	image[at++] = 66U;
	image[at++] = (uint8_t)(1U | (4U << 3));
	image[at++] = 0U;
	image[at++] = 0x63U;
	image[at++] = 1U;
	image[at++] = 0x20U;
	image[at++] = 0x00U;
	image[at++] = 0x63U;
	image[at++] = 2U;
	image[at++] = 0x23U;
	image[at++] = 0x00U;
	image[at++] = 0x5aU;
	image[at++] = 0x5aU;
	image[at++] = 67U;
	image[at++] = 0x20U;
	image[at++] = 0x00U;
	image[0x2c0U + 0x20U] = 81U;
	image[0x2c0U + 0x21U] = 7U;
	image[0x2c0U + 0x22U] = 91U;
	image[0x2c0U + 0x23U] = 91U;

	/* The runner. */
	access.owner = NULL;
	access.read = ours_read;
	access.write = ours_write;
	access.wait = ours_wait;
	error = amdrdna_bytecode_open(&runner, image, sizeof(image), &access, NULL, 0U);
	check(error == 0, "synthetic: the image opens (%d)", error);
	if (error != 0)
		return;
	recording = &record;

	/* The byte copy reads the register for the bits around the byte, then writes. */
	memset(&record, 0, sizeof(record));
	model_reset();
	model_write(0x10U, 0x11223344U);
	error = amdrdna_bytecode_run(&runner, 0U, NULL, 0U);
	check(error == 0 && record.count == 2U && record.events[0].kind == EVENT_READ && record.events[1].kind == EVENT_WRITE &&
	    record.events[1].index == 0x10U && record.events[1].value == 0x11225a44U,
	    "synthetic: a byte copy into bits 15:8 (%d, %u events, 0x%x)", error, record.count, record.events[1].value);

	/* A routine that calls itself stops at the depth limit. */
	error = amdrdna_bytecode_run(&runner, 1U, NULL, 0U);
	check(error != 0 && runner.failure != NULL && strcmp(runner.failure, "calls too deep") == 0 && runner.deepest == 16U,
	    "synthetic: calls stop at 16 deep (%d, %s)", error, runner.failure != NULL ? runner.failure : "none");

	/* A loop stops at the instruction limit. */
	runner.max_steps = 1000U;
	error = amdrdna_bytecode_run(&runner, 2U, NULL, 0U);
	check(error != 0 && runner.failure != NULL && strcmp(runner.failure, "too many instructions") == 0 && runner.steps == 1001U,
	    "synthetic: a loop stops at the instruction limit (%d, %s)", error, runner.failure != NULL ? runner.failure : "none");

	/* A routine past the image's end fails without reading outside it. */
	error = amdrdna_bytecode_run(&runner, 3U, NULL, 0U);
	check(error != 0 && runner.failure != NULL && strcmp(runner.failure, "read past the image") == 0,
	    "synthetic: a read past the image fails (%d, %s)", error, runner.failure != NULL ? runner.failure : "none");

	/* A branch table: 1 waits 7 us, 2 does not, 3 falls through to the wait too. */
	memset(&record, 0, sizeof(record));
	parameters[0] = 1U;
	error = amdrdna_bytecode_run(&runner, 4U, parameters, 1U);
	check(error == 0 && runner.waited_us == 7U, "synthetic: branch table entry 1 waits (%d, %llu)", error,
	    (unsigned long long)runner.waited_us);
	parameters[0] = 2U;
	error = amdrdna_bytecode_run(&runner, 4U, parameters, 1U);
	check(error == 0 && runner.waited_us == 0U, "synthetic: branch table entry 2 ends (%d, %llu)", error,
	    (unsigned long long)runner.waited_us);
	parameters[0] = 3U;
	error = amdrdna_bytecode_run(&runner, 4U, parameters, 1U);
	check(error == 0 && runner.waited_us == 7U, "synthetic: no entry goes past the table (%d, %llu)", error,
	    (unsigned long long)runner.waited_us);

	/* A routine the image lacks. */
	error = amdrdna_bytecode_run(&runner, 9U, NULL, 0U);
	check(error == ENOENT, "synthetic: a routine the image lacks is ENOENT (%d)", error);
}
