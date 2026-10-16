/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the GPU reset's words and tables (src/drivers/gpu/amdrdna/
 * reset-format.c) and of the post on the board's video BIOS image.
 *
 * The post's parameters (the boot clocks the image's firmware table names)
 * and the check of the routine's version; the pixel clock of a timing;
 * the display registers the reset keeps: every one has an address on
 * Navi10's display block for every instance it may take, none is a status,
 * interrupt, CRC or I2C word, none is listed twice, the timing
 * generator's switches come after its other words and the plane's control
 * last, and the other registers sit where the board's register dump names
 * them.  Then the post routine runs with the real parameters on the
 * driver's runner and on the reference runner (Linux v6.19 atom.c, built
 * by amdrdna-reset-test.sh from the temp tree and never kept in the tree)
 * over the same register model, and the records of register reads, writes
 * and waits must match (a run that waits for a bit the model never sets
 * is cut off at the same place in both).
 *
 * usage: amdrdna-reset-test IMAGE
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-bytecode.h"
#include "drivers/gpu/amdrdna/amdrdna-hw.h"
#include "drivers/gpu/amdrdna/amdrdna-reset.h"

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

/* The reference's jiffies (always 0). */
unsigned long jiffies;

/* The parameter dwords of a run, the scratch bytes, the record's limit and the image's padding for the reference. */
#define TEST_PARAMETERS			AMDRDNA_RESET_POST_SPACE_WORDS
#define TEST_SCRATCH_BYTES		(20U * 1024U)
#define TEST_EVENTS			65536U
#define TEST_PADDING			65536U

/*
 * The register models: a register that was not written reads a mix of its
 * index (0), zero (1), all ones (2), or zero but the indirect window's
 * data word, which reads the boot loader's ready word (3: bit 31, no
 * error), which the post waits for first.
 */
#define AMDRDNA_TEST_READY		0x80000000U

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

/* The register model's slots. */
struct slot {
	bool used;
	uint32_t index;
	uint32_t value;
};

static struct slot model[MODEL_SLOTS];
static unsigned model_kind;
static struct record *recording;
static jmp_buf cut_off;
static unsigned checks;
static unsigned failures;

static void check(int ok, const char *format, ...);
static uint32_t model_read(uint32_t index);
static void model_write(uint32_t index, uint32_t value);
static void record_event(enum event_kind kind, uint32_t index, uint32_t value);
static uint32_t ours_read(void *owner, uint32_t index);
static void ours_write(void *owner, uint32_t index, uint32_t value);
static void ours_wait(void *owner, uint32_t microseconds);
static uint32_t theirs_read(struct card_info *info, uint32_t reg);
static void theirs_write(struct card_info *info, uint32_t reg, uint32_t value);
static uint32_t theirs_zero(struct card_info *info, uint32_t reg);
static void theirs_drop(struct card_info *info, uint32_t reg, uint32_t value);
static uint8_t *read_file(const char *path, size_t *length);
static void navi10_display(struct amdrdna_ip *display);
static void test_words(void);
static void test_tables(void);
static void test_post(const uint8_t *image, size_t length);
static void test_post_model(const uint8_t *image, size_t length, const struct amdrdna_vbios_info *info, unsigned kind);

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
		fprintf(stderr, "usage: amdrdna-reset-test IMAGE\n");
		return 2;
	}
	image = read_file(argv[1], &length);
	if (image == NULL) {
		fprintf(stderr, "amdrdna-reset-test: cannot read %s\n", argv[1]);
		return 2;
	}
	test_words();
	test_tables();
	test_post(image, length);
	free(image);
	printf("%s amdrdna reset: %u checks, %u failures\n", failures == 0U ? "PASS" : "FAIL", checks, failures);
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
	if (model_kind == 3U && index == 1U)
		return AMDRDNA_TEST_READY;
	if (model_kind == 1U || model_kind == 3U)
		return 0U;
	if (model_kind == 2U)
		return 0xffffffffU;
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

/* The PLL and memory controller spaces of the reference's driver: reads 0, writes nothing. */
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

/* Fills the display block with Navi10's windows. */
static void
navi10_display(struct amdrdna_ip *display)
{
	memset(display, 0, sizeof(*display));
	display->found = 1U;
	display->major = 2U;
	display->step = 2U;
	display->window_count = 4U;
	display->window[0] = 0x12U;
	display->window[1] = 0xc0U;
	display->window[2] = 0x34c0U;
	display->window[3] = 0x9000U;
}

/* The post's parameters, the routine's version and the pixel clock. */
static void
test_words(void)
{
	uint32_t words[AMDRDNA_RESET_POST_WORDS];
	uint8_t image[64];
	unsigned index;
	bool zero;

	/* The clocks in the low 24 bits, the flags zero, the reserved words zero. */
	memset(words, 0xa5, sizeof(words));
	amdrdna_reset_post_words(words, 130000U, 175000U);
	check(words[0] == 130000U, "post: the engine clock word 0x%08x", words[0]);
	check(words[1] == 175000U, "post: the memory clock word 0x%08x", words[1]);
	zero = true;
	for (index = 2U; index < AMDRDNA_RESET_POST_WORDS; index++) {
		if (words[index] != 0U)
			zero = false;
	}
	check(zero, "post: the reserved words are zero");
	amdrdna_reset_post_words(words, 0x12345678U, 0xffffffffU);
	check(words[0] == 0x00345678U && words[1] == 0x00ffffffU, "post: a clock wider than 24 bits keeps its low bits (0x%08x 0x%08x)",
	    words[0], words[1]);

	/* The version: 2.1 and 2.2 taken, 1.x and 2.0 refused, no routine or one past the end. */
	memset(image, 0, sizeof(image));
	image[16 + 2] = 2U;
	image[16 + 3] = 1U;
	check(amdrdna_reset_post_format(image, sizeof(image), 16U) == 0, "post: version 2.1 taken");
	image[16 + 3] = 2U;
	check(amdrdna_reset_post_format(image, sizeof(image), 16U) == 0, "post: version 2.2 taken");
	image[16 + 3] = 0U;
	check(amdrdna_reset_post_format(image, sizeof(image), 16U) == ENOTSUP, "post: version 2.0 refused");
	image[16 + 2] = 1U;
	image[16 + 3] = 1U;
	check(amdrdna_reset_post_format(image, sizeof(image), 16U) == ENOTSUP, "post: version 1.1 refused");
	check(amdrdna_reset_post_format(image, sizeof(image), 0U) == ENOENT, "post: no routine");
	check(amdrdna_reset_post_format(image, sizeof(image), 62U) == ENOENT, "post: a header past the end");

	/* 1920x1080 at 60 Hz on the CEA totals is 148.5 MHz; 1280x720 at 60 Hz is 74.25 MHz. */
	check(amdrdna_reset_pixel_khz(2200U, 1125U, 60000U) == 148500U, "pixel clock 2200x1125@60 = %u kHz",
	    amdrdna_reset_pixel_khz(2200U, 1125U, 60000U));
	check(amdrdna_reset_pixel_khz(1650U, 750U, 60000U) == 74250U, "pixel clock 1650x750@60");
	check(amdrdna_reset_pixel_khz(2200U, 1125U, 59940U) == 148352U, "pixel clock 2200x1125@59.94 = %u kHz",
	    amdrdna_reset_pixel_khz(2200U, 1125U, 59940U));

	/* The timing generator's switch words. */
	check(amdrdna_reset_timing_stopped(0x00010301U) == 0x00010300U, "timing: stopped keeps the other fields");
	check(amdrdna_reset_vertical_word(0x00290001U, true) == 0x80290001U, "vertical: on");
	check(amdrdna_reset_vertical_word(0x80290001U, false) == 0x00290001U, "vertical: off");
	check(amdrdna_reset_timing_running(0x00010001U), "timing: running by its state bit");
	check(!amdrdna_reset_timing_running(0x00000001U), "timing: asked but not yet running");
}

/* The display registers the reset keeps. */
static void
test_tables(void)
{
	static const enum amdrdna_dcn_register status_words[] = {
		AMDRDNA_DCN_TIMING_POSITION, AMDRDNA_DCN_TIMING_FRAME_COUNT, AMDRDNA_DCN_TIMING_SYNC_STATUS,
		AMDRDNA_DCN_HOTPLUG_STATUS, AMDRDNA_DCN_HOTPLUG_CONTROL, AMDRDNA_DCN_I2C_CONTROL, AMDRDNA_DCN_I2C_STATUS,
		AMDRDNA_DCN_I2C_DATA, AMDRDNA_DCN_AUX_CONTROL, AMDRDNA_DCN_AUX_SEND_DATA, AMDRDNA_DCN_TIMING_CRC_CONTROL,
		AMDRDNA_DCN_TIMING_CRC_RED_GREEN, AMDRDNA_DCN_TIMING_CRC_BLUE, AMDRDNA_DCN_PLANE_FLIP_INTERRUPT,
		AMDRDNA_DCN_PLANE_SHOWN_LOW, AMDRDNA_DCN_PLANE_SHOWN_HIGH, AMDRDNA_DCN_TIMING_UPDATE_LOCK,
		AMDRDNA_DCN_TIMING_BLANK, AMDRDNA_DCN_DISPLAY_CLOCK_DIVIDER, AMDRDNA_DCN_ENCODER_BACK_ENABLE
	};
	/* The other registers' absolute indexes for instance 0, as the board's register dump names them. */
	static const uint32_t extra_expected[] = {
		0x3953U, 0x3954U, 0x3955U, 0x3956U, 0x3957U, 0x3958U, 0x39c8U, 0x0134U, 0x013cU, 0x0176U, 0x0159U, 0x0143U,
		0x4d4cU
	};
	const struct amdrdna_reset_pipe_entry *entry;
	const struct amdrdna_reset_pipe_entry *other;
	const struct amdrdna_reset_extra_entry *extra;
	struct amdrdna_ip display;
	unsigned count;
	unsigned item;
	unsigned seen;
	unsigned instance;
	unsigned last_timing;
	unsigned timing_switch;
	uint32_t index;
	bool found;

	navi10_display(&display);
	count = amdrdna_reset_pipe_count();
	check(count > 0U && count <= AMDRDNA_RESET_PIPE_WORDS, "pipe: %u words fit in %u", count, AMDRDNA_RESET_PIPE_WORDS);
	check(amdrdna_reset_pipe_entry(count) == NULL, "pipe: nothing past the last");

	/* Every word has an address for every instance it may take. */
	for (item = 0U; item < count; item++) {
		entry = amdrdna_reset_pipe_entry(item);
		for (instance = 0U; instance < 4U; instance++) {
			index = amdrdna_dcn_index(&display, entry->which, entry->place == AMDRDNA_RESET_ON_SET ? entry->set : instance);
			check(index != 0U, "pipe: word %u (register %u) has an address on instance %u", item, (unsigned)entry->which, instance);
		}
		check(entry->place != AMDRDNA_RESET_ON_SET || entry->set < 4U, "pipe: word %u's watermark set %u", item, entry->set);

		/* None is listed twice. */
		for (seen = 0U; seen < item; seen++) {
			other = amdrdna_reset_pipe_entry(seen);
			check(other->which != entry->which || other->set != entry->set, "pipe: word %u (register %u) listed again at %u",
			    seen, (unsigned)entry->which, item);
		}

		/* None is a status, interrupt, CRC, I2C or AUX word. */
		for (seen = 0U; seen < sizeof(status_words) / sizeof(status_words[0]); seen++)
			check(entry->which != status_words[seen], "pipe: word %u is register %u, which says what happened", item,
			    (unsigned)entry->which);
	}

	/* The timing generator's switches after its other words, and the plane's control last. */
	last_timing = 0U;
	timing_switch = count;
	for (item = 0U; item < count; item++) {
		entry = amdrdna_reset_pipe_entry(item);
		if (entry->which == AMDRDNA_DCN_TIMING_CONTROL)
			timing_switch = item;
		else if (entry->place == AMDRDNA_RESET_ON_TIMING && entry->which != AMDRDNA_DCN_VERTICAL_GENERATOR)
			last_timing = item;
	}
	check(timing_switch < count && timing_switch > last_timing, "pipe: the timing generator's switch after its words (%u, %u)",
	    timing_switch, last_timing);
	entry = amdrdna_reset_pipe_entry(count - 1U);
	check(entry->which == AMDRDNA_DCN_PLANE_CONTROL, "pipe: the plane's control last");
	found = false;
	for (item = 0U; item < count; item++) {
		entry = amdrdna_reset_pipe_entry(item);
		if (entry->which == AMDRDNA_DCN_PLANE_CLOCK && item < 30U)
			found = true;
	}
	check(found, "pipe: the plane's clock among the first words");

	/* The other registers where the dump names them. */
	count = amdrdna_reset_extra_count();
	check(count == sizeof(extra_expected) / sizeof(extra_expected[0]), "extra: %u words", count);
	check(count <= AMDRDNA_RESET_EXTRA_WORDS, "extra: they fit");
	for (item = 0U; item < count && item < sizeof(extra_expected) / sizeof(extra_expected[0]); item++) {
		extra = amdrdna_reset_extra_entry(item);
		index = display.window[extra->window] + extra->offset;
		check(index == extra_expected[item], "extra: word %u at 0x%05x, the dump's 0x%05x", item, index, extra_expected[item]);
	}
	check(amdrdna_reset_extra_entry(count) == NULL, "extra: nothing past the last");
}

/* The post routine with the real parameters on both runners. */
static void
test_post(const uint8_t *image, size_t length)
{
	struct amdrdna_vbios_info info;
	unsigned index;
	int error;

	/* The image's tables: the boot clocks and the post routine. */
	error = amdrdna_vbios_parse(image, length, &info);
	check(error == 0, "image: parsed (%d)", error);
	if (error != 0)
		return;
	printf("  image: boot engine %u.%02u MHz, memory %u.%02u MHz, post at 0x%04x\n",
	    info.boot_engine_clock_10khz / 100U, info.boot_engine_clock_10khz % 100U,
	    info.boot_memory_clock_10khz / 100U, info.boot_memory_clock_10khz % 100U,
	    info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset);
	check(info.boot_engine_clock_10khz != 0U && info.boot_memory_clock_10khz != 0U, "image: boot clocks named");
	error = amdrdna_reset_post_format(image, length, info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset);
	check(error == 0, "image: the post routine is version 2.1 or later (%d, %u.%u)", error,
	    image[info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset + 2U], image[info.commands[AMDRDNA_VBIOS_COMMAND_POST].offset + 3U]);

	/* A register that was not written reads a mix of its index, zero, or all ones. */
	for (index = 0U; index < 4U; index++)
		test_post_model(image, length, &info, index);
}

/* The post routine on both runners over one register model. */
static void
test_post_model(const uint8_t *image, size_t length, const struct amdrdna_vbios_info *info, unsigned kind)
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
	volatile int ours_result;
	volatile int theirs_result;
	unsigned index;
	unsigned writes;
	int error;
	bool same;

	model_kind = kind;
	/* Both runners over the image. */
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
	check(amdrdna_bytecode_has(&runner, 0U), "image: the post routine is there");

	/* Ours, with the reset's parameters. */
	memset(ours_parameters, 0, sizeof(ours_parameters));
	amdrdna_reset_post_words(ours_parameters, info->boot_engine_clock_10khz, info->boot_memory_clock_10khz);
	memset(model, 0, sizeof(model));
	memset(&ours_record, 0, sizeof(ours_record));
	recording = &ours_record;
	ours_result = -1;
	if (setjmp(cut_off) == 0)
		ours_result = amdrdna_bytecode_run(&runner, 0U, ours_parameters, AMDRDNA_RESET_POST_SPACE_WORDS);

	/* The reference's, with the same parameters. */
	memset(theirs_parameters, 0, sizeof(theirs_parameters));
	amdrdna_reset_post_words(theirs_parameters, info->boot_engine_clock_10khz, info->boot_memory_clock_10khz);
	memset(model, 0, sizeof(model));
	memset(&theirs_record, 0, sizeof(theirs_record));
	recording = &theirs_record;
	theirs_result = -1;
	if (setjmp(cut_off) == 0)
		theirs_result = amdgpu_atom_execute_table(theirs, 0, theirs_parameters, (int)AMDRDNA_RESET_POST_SPACE_WORDS);

	/* The records, event by event. */
	same = (ours_record.count == theirs_record.count && ours_record.cut == theirs_record.cut);
	writes = 0U;
	for (index = 0U; index < ours_record.count && index < theirs_record.count; index++) {
		if (ours_record.events[index].kind == EVENT_WRITE)
			writes++;
		if (ours_record.events[index].kind != theirs_record.events[index].kind ||
		    ours_record.events[index].index != theirs_record.events[index].index ||
		    ours_record.events[index].value != theirs_record.events[index].value) {
			printf("  post: event %u differs: ours %d 0x%x 0x%x, the reference's %d 0x%x 0x%x\n", index,
			    (int)ours_record.events[index].kind, ours_record.events[index].index, ours_record.events[index].value,
			    (int)theirs_record.events[index].kind, theirs_record.events[index].index, theirs_record.events[index].value);
			same = false;
			break;
		}
	}
	printf("  post (model %u): %u events (%u writes, cut %d, result %d, %llu steps), the reference's %u (cut %d, result %d)\n",
	    kind, ours_record.count, writes, (int)ours_record.cut, ours_result, (unsigned long long)runner.steps, theirs_record.count,
	    (int)theirs_record.cut, theirs_result);
	if (kind == 3U) {
		for (index = 0U; index < ours_record.count; index++)
			printf("  post (model 3): event %u: %s 0x%05x 0x%08x\n", index,
			    ours_record.events[index].kind == EVENT_READ ? "read" : (ours_record.events[index].kind == EVENT_WRITE ? "write" : "wait"),
			    ours_record.events[index].index, ours_record.events[index].value);
	}
	check(same, "post (model %u): the same register reads, writes and waits with the boot clocks (%u events)", kind, ours_record.count);
	check(ours_record.count > 0U, "post: the routine touches the registers");
	if (kind == 3U)
		check(!ours_record.cut && ours_result == 0, "post (model 3): with the boot loader's ready word the post ends (%d)", ours_result);
	if (!ours_record.cut && !theirs_record.cut) {
		check((ours_result == 0) == (theirs_result == 0), "post: the same outcome (%d, %d)", ours_result, theirs_result);
		check(memcmp(ours_parameters, theirs_parameters, sizeof(ours_parameters)) == 0, "post: the same parameters after the run");
		check(memcmp(ours_scratch, theirs_scratch, TEST_SCRATCH_BYTES) == 0, "post: the same scratch");
	}
	free(ours_scratch);
	free(theirs_scratch);
	free(theirs_image);
}
