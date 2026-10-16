/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's words of stage A5 (ws039-p005): the
 * command processor's and the copy engine's packets, the queue images,
 * the queue select and scheduler words, the settings tables and the clear
 * state, the shape words (with the board's fuse words of the 2026-10-16
 * dump), and the first dispatch's program run on the GFX10 interpreter of
 * ws039-p007 with the driver's own buffer descriptors.
 *
 *   amdrdna-engines-test [MODULE.spv]   the cases; with the check program's
 *                                       module, also compares the embedded
 *                                       code with what the compiler makes now
 *   amdrdna-engines-test --layout       the image word positions and the
 *                                       register offsets, one line each, for
 *                                       amdrdna-engines-compare.py
 *
 * Each case prints "PASS name" or "FAIL name: why"; the exit status is 1
 * when any case failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"
#include "drivers/gpu/amdrdna/amdrdna-regs.h"
#include "drivers/gpu/amdrdna/compiler/gfx10-compile.h"
#include "drivers/gpu/amdrdna/compiler/gfx10-program.h"
#include "plan/ws039/tests/gfx10-machine.h"

void *kern_malloc(size_t size);
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void check_word(uint32_t got, uint32_t want, const char *name);
static void test_packets(void);
static void test_copy_packets(void);
static void test_images(void);
static void test_queue_words(void);
static void test_settings(void);
static void test_clear_state(void);
static void test_shape(void);
static void test_check_program(void);
static void test_against_compiler(const char *module);
static void print_layout(void);

/* The host's allocator stands in for the kernel's (the compiler uses it). */
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
	/* Prints the layout when asked. */
	if (argc == 2 && strcmp(argv[1], "--layout") == 0) {
		print_layout();
		return 0;
	}

	/* Runs the cases. */
	test_packets();
	test_copy_packets();
	test_images();
	test_queue_words();
	test_settings();
	test_clear_state();
	test_shape();
	test_check_program();
	if (argc == 2)
		test_against_compiler(argv[1]);

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-engines-test: %d case(s)\n", failures);
		return 1;
	}

	printf("PASS amdrdna-engines-test\n");
	return 0;
}

/* Records one case. */
static void
check(
	int condition,
	const char *name,
	const char *why)
{
	if (condition) {
		printf("PASS %s\n", name);
		return;
	}
	printf("FAIL %s: %s\n", name, why);
	failures++;
}

/* Records one word compared with its expected value. */
static void
check_word(
	uint32_t got,
	uint32_t want,
	const char *name)
{
	char why[96];

	snprintf(why, sizeof(why), "0x%08x, expected 0x%08x", got, want);
	check(got == want, name, why);
}

/*
 * The command processor's packets: the words worked out by hand from the
 * Linux v6.19 nvd.h fields and gfx_v10_0.c's packet builders (the
 * expected values below), compared with what the driver writes.
 */
static void
test_packets(void)
{
	struct amdrdna_queue_place place;
	uint32_t words[64];
	uint32_t values[2];
	unsigned count;

	/* The filler: a nothing packet of count 0x3fff. */
	check_word(amdrdna_pm4_filler(), 0xffff1000U, "pm4 filler");

	/* A memory write: header, direct memory write confirmed, address, value. */
	count = amdrdna_pm4_write_memory(words, 0x123456789aULL, 0xdeadbeefU, false);
	check(count == 5U, "pm4 write memory: 5 words", "count");
	check_word(words[0], 0xc0033700U, "pm4 write memory: header");
	check_word(words[1], 0x00100500U, "pm4 write memory: control");
	check_word(words[2], 0x3456789aU, "pm4 write memory: address low");
	check_word(words[3], 0x00000012U, "pm4 write memory: address high");
	check_word(words[4], 0xdeadbeefU, "pm4 write memory: value");

	/* A register write. */
	count = amdrdna_pm4_write_register(words, 0x30b3U, 7U, false);
	check(count == 5U && words[1] == 0x00100000U && words[2] == 0x30b3U && words[3] == 0U && words[4] == 7U,
	    "pm4 write register", "words");

	/* The fence: Linux's emit_fence with a 32-bit value and the interrupt. */
	count = amdrdna_pm4_fence(words, 0x8000001000ULL, 5U, true, false);
	check(count == 8U, "pm4 fence: 8 words", "count");
	check_word(words[0], 0xc0064900U, "pm4 fence: header");
	check_word(words[1], 0x06603514U, "pm4 fence: event and caches");
	check_word(words[2], 0x22000000U, "pm4 fence: data and interrupt");
	check_word(words[3], 0x00001000U, "pm4 fence: address low");
	check_word(words[4], 0x00000080U, "pm4 fence: address high");
	check_word(words[5], 5U, "pm4 fence: value");
	count = amdrdna_pm4_fence(words, 0x1000ULL, 5U, false, false);
	check_word(words[2], 0x20000000U, "pm4 fence: no interrupt");

	/* The cache sync: Linux's emit_mem_sync, with the compute mark on a compute queue. */
	count = amdrdna_pm4_cache_sync(words, true);
	check(count == 8U, "pm4 cache sync: 8 words", "count");
	check_word(words[0], 0xc0065802U, "pm4 cache sync: header (compute)");
	check(words[1] == 0U && words[2] == 0xffffffffU && words[3] == 0x00ffffffU && words[4] == 0U && words[5] == 0U,
	    "pm4 cache sync: coherence", "words");
	check_word(words[6], 0x0000000aU, "pm4 cache sync: poll");
	check_word(words[7], 0x0000c3b1U, "pm4 cache sync: caches");

	/* The memory move. */
	count = amdrdna_pm4_move(words, 0x8000002000ULL, 0x40000ULL, 4096U);
	check(count == 7U, "pm4 move: 7 words", "count");
	check_word(words[0], 0xc0055000U, "pm4 move: header");
	check_word(words[1], 0x80000000U, "pm4 move: sync");
	check(words[2] == 0x2000U && words[3] == 0x80U && words[4] == 0x40000U && words[5] == 0U,
	    "pm4 move: addresses", "words");
	check_word(words[6], 4096U, "pm4 move: bytes");

	/* The dispatch, with the compute mark. */
	count = amdrdna_pm4_dispatch(words, 2U, 3U, 4U, 0x8045U);
	check(count == 5U && words[0] == 0xc0031502U && words[1] == 2U && words[2] == 3U && words[3] == 4U && words[4] == 0x8045U,
	    "pm4 dispatch", "words");

	/* Register writes of each space, relative to the space's first register. */
	values[0] = 0x11U;
	values[1] = 0x22U;
	count = amdrdna_pm4_registers(words, AMDRDNA_SPACE_SHADER, 0x2e0cU, values, 2U, false);
	check(count == 4U && words[0] == 0xc0027600U && words[1] == 0x20cU && words[2] == 0x11U && words[3] == 0x22U,
	    "pm4 shader registers (program address)", "words");
	count = amdrdna_pm4_registers(words, AMDRDNA_SPACE_USER, 0xc040U, values, 1U, false);
	check(count == 3U && words[0] == 0xc0017900U && words[1] == 0x40U, "pm4 user register (scratch)", "words");
	count = amdrdna_pm4_registers(words, AMDRDNA_SPACE_CONTEXT, 0xa0d7U, values, 1U, false);
	check(count == 3U && words[0] == 0xc0016900U && words[1] == 0xd7U, "pm4 context register (steering)", "words");

	/* The scheduler's resources: compute queues 0 of pipes 0 to 3 of the first engine. */
	count = amdrdna_pm4_queue_resources(words, 0x01010101ULL);
	check(count == 8U && words[0] == 0xc006a000U && words[1] == 0U && words[2] == 0x01010101U && words[3] == 0U,
	    "pm4 queue resources", "words");

	/* A compute queue's attach: Linux's map_queues for me 1 (field 0), pipe 2, queue 0, engine 0. */
	place.engine = AMDRDNA_ENGINE_COMPUTE_FIRST;
	place.pipe = 2U;
	place.queue = 0U;
	count = amdrdna_pm4_queue_attach(words, &place, AMDRDNA_QUEUE_ENGINE_COMPUTE, 0x0aU, 0x8000003000ULL, 0x8000004008ULL);
	check(count == 7U, "pm4 attach: 7 words", "count");
	check_word(words[0], 0xc005a200U, "pm4 attach: header");
	check_word(words[1], 0x20020000U, "pm4 attach compute: queue word");
	check_word(words[2], 0x28U, "pm4 attach compute: doorbell");
	check(words[3] == 0x3000U && words[4] == 0x80U && words[5] == 0x4008U && words[6] == 0x80U,
	    "pm4 attach: image and pointer", "words");

	/* The graphics queue's attach: me 0 gives the engine field 1, engine 4. */
	place.engine = AMDRDNA_ENGINE_GRAPHICS;
	place.pipe = 0U;
	place.queue = 0U;
	count = amdrdna_pm4_queue_attach(words, &place, AMDRDNA_QUEUE_ENGINE_GRAPHICS, 0x116U, 0U, 0U);
	check_word(words[1], 0x30040000U, "pm4 attach graphics: queue word");
	check_word(words[2], 0x458U, "pm4 attach graphics: doorbell");

	/* The detaches: a compute reset and a graphics preemption. */
	count = amdrdna_pm4_queue_detach(words, AMDRDNA_QUEUE_ENGINE_COMPUTE, AMDRDNA_QUEUE_DETACH_RESET, 0x06U);
	check(count == 6U && words[0] == 0xc004a300U && words[1] == 0x20000001U && words[2] == 0x18U && words[3] == 0U,
	    "pm4 detach compute reset", "words");
	count = amdrdna_pm4_queue_detach(words, AMDRDNA_QUEUE_ENGINE_GRAPHICS, AMDRDNA_QUEUE_DETACH_PREEMPT, 0x116U);
	check_word(words[1], 0x30000000U, "pm4 detach graphics preempt");

	/* The preamble's packets. */
	count = amdrdna_pm4_bracket(words, true);
	check(count == 2U && words[0] == 0xc0004a00U && words[1] == 0x20000000U, "pm4 bracket begin", "words");
	count = amdrdna_pm4_bracket(words, false);
	check_word(words[1], 0x30000000U, "pm4 bracket end");
	count = amdrdna_pm4_context_loads(words);
	check(count == 3U && words[0] == 0xc0012800U && words[1] == 0x80000000U && words[2] == 0x80000000U,
	    "pm4 context loads", "words");
	count = amdrdna_pm4_state_reset(words);
	check(count == 2U && words[0] == 0xc0001200U && words[1] == 0U, "pm4 state reset", "words");
	count = amdrdna_pm4_partition(words);
	check(count == 4U && words[0] == 0xc0021100U && words[1] == 3U && words[2] == 0x8000U && words[3] == 0x8000U,
	    "pm4 partition", "words");
}

/* The copy engine's packets: Linux's sdma_v5_0.c builders, by hand. */
static void
test_copy_packets(void)
{
	uint32_t words[16];
	unsigned count;

	/* The filler. */
	check_word(amdrdna_copy_filler(), 0U, "copy filler");

	/* The linear write of the ring test. */
	count = amdrdna_copy_write(words, 0x8000005018ULL, 0xdeadbeefU);
	check(count == 5U && words[0] == 2U && words[1] == 0x5018U && words[2] == 0x80U && words[3] == 0U && words[4] == 0xdeadbeefU,
	    "copy write", "words");

	/* The linear copy: the count less one. */
	count = amdrdna_copy_linear(words, 0x8000006000ULL, 0x1000000ULL, 4096U);
	check(count == 7U && words[0] == 1U && words[1] == 4095U && words[2] == 0U, "copy linear: header and count", "words");
	check(words[3] == 0x6000U && words[4] == 0x80U && words[5] == 0x1000000U && words[6] == 0U, "copy linear: addresses", "words");

	/* The fence, uncached, with and without the trap. */
	count = amdrdna_copy_fence(words, 0x8000005010ULL, 9U, true);
	check(count == 6U && words[0] == 0x00030005U && words[1] == 0x5010U && words[2] == 0x80U && words[3] == 9U &&
	    words[4] == 6U && words[5] == 0U, "copy fence and trap", "words");
	count = amdrdna_copy_fence(words, 0x8000005010ULL, 9U, false);
	check(count == 4U, "copy fence without trap", "count");
}

/*
 * The queue images: the fields Linux's gfx_v10_0_compute_mqd_init and
 * gfx_v10_0_gfx_mqd_init set, for the board's register values (0 where
 * the dump has none), worked out by hand.
 */
static void
test_images(void)
{
	struct amdrdna_compute_image_input compute;
	struct amdrdna_graphics_image_input graphics;
	uint32_t image[AMDRDNA_QUEUE_IMAGE_WORDS];

	/* The scheduler's queue: a 16 KiB ring, active, doorbell 0. */
	memset(&compute, 0, sizeof(compute));
	compute.image = 0x8000010000ULL;
	compute.ring = 0x8000020000ULL;
	compute.ring_bytes = 0x4000U;
	compute.read_report = 0x8000001000ULL;
	compute.write_poll = 0x8000001008ULL;
	compute.end_of_pipe = 0x8000030000ULL;
	compute.doorbell = 0U;
	compute.active = true;
	compute.doorbell_control = 0x90000000U;
	amdrdna_compute_image(&compute, image);
	check_word(image[0], 0xc0310800U, "compute image: header");
	check(image[11] == 1U && image[23] == 0xffffffffU && image[24] == 0xffffffffU && image[26] == 0xffffffffU &&
	    image[27] == 0xffffffffU && image[32] == 3U, "compute image: fixed words", "words");
	check(image[165] == 0x80000300U && image[166] == 0U && image[167] == 8U, "compute image: end of pipe", "words");
	check_word(image[143], 0x40000000U, "compute image: doorbell (source and hit cleared)");
	check(image[128] == 0x10000U && image[129] == 0x80U && image[162] == 0U, "compute image: self and control", "words");
	check(image[136] == 0x80000200U && image[137] == 0U, "compute image: ring", "words");
	check_word(image[145], 0xd000090bU, "compute image: ring control (16 KiB, block 9, unordered, privileged, kernel)");
	check(image[139] == 0x1000U && image[140] == 0x80U && image[141] == 0x1008U && image[142] == 0x80U,
	    "compute image: pointers", "words");
	check(image[132] == 0x5300U && image[149] == 0x300000U, "compute image: preload and buffer room", "words");
	check(image[130] == 1U && image[131] == 0U && image[152] == 0U && image[182] == 0U && image[183] == 0U,
	    "compute image: active, context, dequeue, write pointer", "words");

	/* A compute queue: not active, doorbell 0x0c. */
	compute.active = false;
	compute.doorbell = 0x0cU;
	compute.doorbell_control = 0U;
	amdrdna_compute_image(&compute, image);
	check(image[130] == 0U && image[143] == 0x40000030U, "compute image: attached queue", "words");

	/* The graphics queue: a 64 KiB ring, doorbell 0x116. */
	memset(&graphics, 0, sizeof(graphics));
	graphics.image = 0x8000040000ULL;
	graphics.ring = 0x8000050000ULL;
	graphics.ring_bytes = 0x10000U;
	graphics.read_report = 0x8000001040ULL;
	graphics.write_poll = 0x8000001048ULL;
	graphics.doorbell = 0x116U;
	graphics.image_control = 0x03000000U;
	graphics.priority = 0x5U;
	amdrdna_graphics_image(&graphics, image);
	check(image[128] == 0x40000U && image[129] == 0x80U, "graphics image: self", "words");
	check_word(image[162], 0x00000100U, "graphics image: control (privileged, policy 0)");
	check(image[131] == 0U && image[134] == 0U && image[135] == 1U, "graphics image: context, priority, quantum", "words");
	check(image[136] == 0x80000500U && image[137] == 0U, "graphics image: ring", "words");
	check(image[139] == 0x1040U && image[140] == 0x80U && image[141] == 0x1048U && image[142] == 0x80U,
	    "graphics image: pointers", "words");
	check_word(image[145], 0x00000b0dU, "graphics image: ring control (64 KiB)");
	check_word(image[143], 0x40000458U, "graphics image: doorbell");
	check(image[130] == 1U && image[149] == 0U && image[150] == 0U, "graphics image: active, write pointer", "words");

	/* The direct graphics ring's control word: the same size field. */
	check_word(amdrdna_graphics_ring_word(0x10000U), 0x00000b0dU, "graphics ring word (64 KiB)");
}

/* The queue select, the scheduler's queue word and the resource mask. */
static void
test_queue_words(void)
{
	struct amdrdna_queue_place places[4];
	struct amdrdna_queue_place place;
	unsigned index;

	/* Linux's nv_grbm_select(me 2, pipe 1, queue 0, vmid 0), and (0, 0, 0, vmid 15). */
	place.engine = AMDRDNA_ENGINE_COMPUTE_SECOND;
	place.pipe = 1U;
	place.queue = 0U;
	check_word(amdrdna_queue_select_word(&place, 0U), 0x00000009U, "queue select: scheduler");
	place.engine = AMDRDNA_ENGINE_GRAPHICS;
	place.pipe = 0U;
	check_word(amdrdna_queue_select_word(&place, 15U), 0x000000f0U, "queue select: context 15");

	/* Linux's kiq_setting: (2 << 5) | (1 << 3) | 0, valid, the upper bytes kept. */
	place.engine = AMDRDNA_ENGINE_COMPUTE_SECOND;
	place.pipe = 1U;
	check_word(amdrdna_scheduler_queue_word(0x12345677U, &place), 0x123456c8U, "scheduler queue word");

	/* The resource mask of queue 0 of the four pipes of the first compute engine. */
	for (index = 0U; index < 4U; index++) {
		places[index].engine = AMDRDNA_ENGINE_COMPUTE_FIRST;
		places[index].pipe = index;
		places[index].queue = 0U;
	}
	check(amdrdna_queue_resource_mask(places, 4U) == 0x01010101ULL, "queue resource mask", "mask");
}

/* The settings tables: their sizes, a few entries, and the merge rule. */
static void
test_settings(void)
{
	const struct amdrdna_register_setting *settings;
	struct amdrdna_register_setting setting;
	unsigned count;

	/* The graphics engine's 40 settings: the first is CB_HW_CONTROL_4 (0x1422 of window 0), written whole. */
	settings = amdrdna_graphics_settings(&count);
	check(count == 40U, "graphics settings: 40", "count");
	check(settings[0].window == 0U && settings[0].offset == 0x1422U && settings[0].mask == 0xffffffffU &&
	    settings[0].value == 0x00400014U, "graphics settings: first", "entry");
	check(settings[39].window == 0U && settings[39].offset == 0x1588U && settings[39].mask == 0x00c00000U,
	    "graphics settings: last (UTCL1_CTRL)", "entry");

	/* The copy engines' 26: 24 of both engines, then the Navi10 pair. */
	settings = amdrdna_copy_settings(&count);
	check(count == 26U, "copy settings: 26", "count");
	check(settings[0].offset == 0x001dU && settings[12].offset == 0x061dU && settings[24].offset == 0x0267U &&
	    settings[24].mask == 0x0000fff0U, "copy settings: entries", "entries");

	/* The merge: a whole-word setting replaces, a partial one keeps the other bits. */
	setting.window = 0U;
	setting.offset = 0U;
	setting.mask = 0xffffffffU;
	setting.value = 0x5U;
	check(!amdrdna_setting_reads(&setting) && amdrdna_setting_word(&setting, 0xffU) == 0x5U, "setting: whole word", "word");
	setting.mask = 0x0000ff00U;
	setting.value = 0x1234U;
	check(amdrdna_setting_reads(&setting) && amdrdna_setting_word(&setting, 0xabcdefU) == 0xab12efU, "setting: partial", "word");
}

/* The clear state: Linux's 949 words for Navi10, and its frame. */
static void
test_clear_state(void)
{
	uint32_t *words;
	unsigned count;

	/* The length Linux's get_csb_size gives. */
	check(amdrdna_clear_state_words() == 949U, "clear state: 949 words", "length");

	/* The stream. */
	words = calloc(amdrdna_clear_state_words(), 4U);
	count = amdrdna_clear_state(words, 0x00122000U);
	check(count == 949U, "clear state: written length", "count");
	check(words[0] == 0xc0004a00U && words[1] == 0x20000000U && words[2] == 0xc0012800U, "clear state: begin", "words");

	/* The first run: 215 registers from 0xa000 (Linux's PACKET3 count is the register count), the scissor's 0x40004000 at 0xa00d. */
	check(words[5] == 0xc0d76900U && words[6] == 0U && words[7 + 0x0d] == 0x40004000U && words[7 + 0x1f] == 0x00150055U,
	    "clear state: first run", "words");

	/* The steering, then the end and the reset. */
	check(words[942] == 0xc0016900U && words[943] == 0xd7U && words[944] == 0x00122000U, "clear state: steering", "words");
	check(words[945] == 0xc0004a00U && words[946] == 0x30000000U && words[947] == 0xc0001200U && words[948] == 0U,
	    "clear state: end and reset", "words");
	free(words);
}

/*
 * The shape words, with the board's fuse words: the shader array of
 * engine 0, array 0 read 0xffe00001 for its workgroup processors (the
 * cold dump, broadcast read) and 0 for its back ends.
 */
static void
test_shape(void)
{
	uint32_t translation;
	uint32_t targets;

	/* Five processors of the array on, ten compute units. */
	check_word(amdrdna_groups_on(0xffe00001U, 0U, 5U), 0x1fU, "groups on: board");
	check(amdrdna_units_on(0x1fU) == 10U, "units on: board", "count");

	/* One processor switched off by the second word. */
	check_word(amdrdna_groups_on(0xffe00001U, 0x00100000U, 5U), 0x0fU, "groups on: one off");

	/* Four back ends per array, all on; one fused off. */
	check_word(amdrdna_backends_on(0U, 0U, 4U), 0xfU, "backends on: board");
	check_word(amdrdna_backends_on(0x00020000U, 0U, 4U), 0xdU, "backends on: one fused");

	/* Channels 4 and 18 off: the low field (bits 31:16) and the high one (15:8). */
	check_word(amdrdna_channels_off(0x00100000U, 0x00000400U), 0x00040010U, "channels off");

	/* Linux's steering for 2 engines x 2 arrays x 1 converter, 4 back ends each, 2 packers. */
	check_word(amdrdna_tile_steering(4U, 4U, 2U), 0x00122000U, "tile steering: board");

	/* The harvest of processor 4 of 5: its vector cache bits 9:8 and 19:18 (translation), 9:8 and 14 (targets). */
	amdrdna_harvest_words(0x0fU, 5U, 0xfff00000U, 0xffff8000U, &translation, &targets);
	check_word(translation, 0xfff00000U | (3U << 8) | (3U << 18), "harvest: translation");
	check_word(targets, 0xffff8000U | (3U << 8) | (1U << 14), "harvest: targets");
	amdrdna_harvest_words(0x1fU, 5U, 0xfff12345U, 0xffff1234U, &translation, &targets);
	check(translation == 0xfff00000U && targets == 0xffff0000U, "harvest: every processor on clears the fields", "words");

	/* The shader memory words. */
	check(amdrdna_shader_memory_mode() == 0x0000c00cU && amdrdna_shader_apertures() == 0x20001000U,
	    "shader memory words", "words");
}

/*
 * The first dispatch's program on the GFX10 interpreter: the driver's
 * descriptors in a table, eight workgroups, and every output word
 * compared by the driver's own check (which is also shown to catch a
 * wrong word).
 */
static void
test_check_program(void)
{
	static uint8_t table[4096];
	static uint8_t output[8192];
	static uint8_t counts[256];
	struct amdrdna_check_description program;
	struct machine_memory memory;
	struct machine_dispatch dispatch;
	struct machine_result result;
	uint32_t descriptor[4];
	uint32_t p5_descriptor[4];
	uint32_t groups[3];
	uint32_t group[3];
	uint32_t words;
	uint32_t bad;
	uint32_t expected;
	uint32_t found;
	uint64_t output_address;
	uint64_t counts_address;
	uint32_t *code;
	unsigned index;
	int ok;

	/* The program, with the padding the driver writes after it. */
	amdrdna_check_program(&program);
	check(program.code_words == 136U && program.padded_words == 192U && program.end_word == 0xbf9f0000U,
	    "check program: size and padding", "words");
	check(program.local_size[0] == 4U && program.local_size[1] == 3U && program.local_size[2] == 2U &&
	    program.output_at == 0U && program.groups_at == 16U && program.table_bytes == 32U,
	    "check program: workgroup and table", "values");
	code = calloc(program.padded_words, 4U);
	for (index = 0U; index < program.padded_words; index++)
		code[index] = index < program.code_words ? program.code[index] : program.end_word;
	check(program.code[program.code_words - 1U] == 0xbf810000U, "check program: ends with s_endpgm", "word");

	/* The memory: the table, the output, the counts. */
	groups[0] = 2U;
	groups[1] = 2U;
	groups[2] = 2U;
	words = amdrdna_check_output_words(groups);
	check(words == 1536U, "check program: 1536 output words for 2x2x2", "count");
	memset(&memory, 0, sizeof(memory));
	memset(table, 0, sizeof(table));
	memset(output, 0xa5, sizeof(output));
	memset(counts, 0, sizeof(counts));
	memcpy(counts, groups, 12U);
	(void)machine_add_region(&memory, table, sizeof(table));
	output_address = machine_add_region(&memory, output, sizeof(output));
	counts_address = machine_add_region(&memory, counts, sizeof(counts));

	/* The driver's descriptors, which must match the compiler's. */
	amdrdna_raw_buffer_descriptor(output_address, words * 4U, descriptor);
	amdrdna_gfx10_raw_buffer_descriptor(output_address, words * 4U, p5_descriptor);
	check(memcmp(descriptor, p5_descriptor, sizeof(descriptor)) == 0, "raw descriptor matches the compiler's", "words");
	memcpy(table + program.output_at, descriptor, 16U);
	amdrdna_raw_buffer_descriptor(counts_address, 12U, descriptor);
	memcpy(table + program.groups_at, descriptor, 16U);

	/* Every workgroup. */
	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.code = code;
	dispatch.code_words = program.padded_words;
	dispatch.table = memory.regions[0].address;
	dispatch.local_size[0] = program.local_size[0];
	dispatch.local_size[1] = program.local_size[1];
	dispatch.local_size[2] = program.local_size[2];
	dispatch.vgpr_count = 8U * ((program.counts & 0x3fU) + 1U);
	ok = 1;
	for (group[2] = 0U; group[2] < groups[2]; group[2]++) {
		for (group[1] = 0U; group[1] < groups[1]; group[1]++) {
			for (group[0] = 0U; group[0] < groups[0]; group[0]++) {
				dispatch.group_id[0] = group[0];
				dispatch.group_id[1] = group[1];
				dispatch.group_id[2] = group[2];
				machine_run_group(&dispatch, &memory, &result);
				if (result.error != NULL) {
					printf("  GFX10: %s at word %u\n", result.error, result.error_pc);
					ok = 0;
				}
			}
		}
	}
	check(ok, "check program: runs on the interpreter", "error");

	/* The driver's check passes, and fails for one wrong word. */
	ok = amdrdna_check_output((const uint32_t *)output, groups, &bad, &expected, &found);
	if (!ok)
		printf("  word %u: 0x%08x, expected 0x%08x\n", bad, found, expected);
	check(ok, "check program: output matches", "word");
	((uint32_t *)output)[1000] ^= 1U;
	ok = amdrdna_check_output((const uint32_t *)output, groups, &bad, &expected, &found);
	check(!ok && bad == 1000U, "check program: a wrong word is found", "word");
	free(code);
}

/*
 * The embedded program against the compiler now: the same code words and
 * register values (a difference is reported, not failed: the compiler may
 * have improved; the interpreter case above is what the driver relies on).
 */
static void
test_against_compiler(
	const char *module)
{
	struct amdrdna_check_description embedded;
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_diagnostic diagnostic;
	struct amdrdna_gfx10_registers registers;
	uint32_t *words;
	size_t count;
	long size;
	FILE *file;
	int same;
	int error;

	/* Reads the module. */
	file = fopen(module, "rb");
	if (file == NULL) {
		check(0, "compiler comparison: module", "cannot open");
		return;
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)size);
	count = fread(words, 1U, (size_t)size, file) / 4U;
	fclose(file);

	/* Compiles it. */
	program = NULL;
	error = amdrdna_gfx10_compile(words, count, DRV_GPU_STAGE_COMPUTE, &program, &diagnostic);
	free(words);
	check(error == 0, "compiler comparison: compiles", "refused");
	if (error != 0)
		return;
	amdrdna_gfx10_program_registers(program, &registers);

	/* Compares. */
	amdrdna_check_program(&embedded);
	same = program->code_words >= embedded.code_words &&
	    memcmp(program->code, embedded.code, embedded.code_words * 4U) == 0 &&
	    registers.counts == embedded.counts && registers.inputs == embedded.inputs;
	if (same)
		printf("PASS compiler comparison: the embedded program is the compiler's output now\n");
	else
		printf("NOTE compiler comparison: the compiler's output changed since the program was embedded\n");
	amdrdna_gfx10_program_free(program);
}

/*
 * Prints the image word positions and the register offsets with the
 * Linux names they correspond to, for amdrdna-engines-compare.py.
 */
static void
print_layout(void)
{
	const struct amdrdna_register_setting *settings;
	unsigned count;
	unsigned index;

	/* The settings tables, in order. */
	settings = amdrdna_graphics_settings(&count);
	for (index = 0U; index < count; index++)
		printf("setting graphics %u 0x%x 0x%08x 0x%08x\n", settings[index].window, settings[index].offset, settings[index].mask, settings[index].value);
	settings = amdrdna_copy_settings(&count);
	for (index = 0U; index < count; index++)
		printf("setting copy %u 0x%x 0x%08x 0x%08x\n", settings[index].window, settings[index].offset, settings[index].mask, settings[index].value);

	printf("compute header %u\n", AMDRDNA_COMPUTE_IMAGE_HEADER);
	printf("compute compute_pipelinestat_enable %u\n", AMDRDNA_COMPUTE_IMAGE_STATISTICS);
	printf("compute compute_static_thread_mgmt_se0 %u\n", AMDRDNA_COMPUTE_IMAGE_UNITS_0);
	printf("compute compute_static_thread_mgmt_se1 %u\n", AMDRDNA_COMPUTE_IMAGE_UNITS_1);
	printf("compute compute_static_thread_mgmt_se2 %u\n", AMDRDNA_COMPUTE_IMAGE_UNITS_2);
	printf("compute compute_static_thread_mgmt_se3 %u\n", AMDRDNA_COMPUTE_IMAGE_UNITS_3);
	printf("compute compute_misc_reserved %u\n", AMDRDNA_COMPUTE_IMAGE_RESERVED);
	printf("compute cp_mqd_base_addr_lo %u\n", AMDRDNA_COMPUTE_IMAGE_SELF_LOW);
	printf("compute cp_mqd_base_addr_hi %u\n", AMDRDNA_COMPUTE_IMAGE_SELF_HIGH);
	printf("compute cp_hqd_active %u\n", AMDRDNA_COMPUTE_IMAGE_ACTIVE);
	printf("compute cp_hqd_vmid %u\n", AMDRDNA_COMPUTE_IMAGE_CONTEXT);
	printf("compute cp_hqd_persistent_state %u\n", AMDRDNA_COMPUTE_IMAGE_PERSISTENT);
	printf("compute cp_hqd_pipe_priority %u\n", AMDRDNA_COMPUTE_IMAGE_PIPE_PRIORITY);
	printf("compute cp_hqd_queue_priority %u\n", AMDRDNA_COMPUTE_IMAGE_QUEUE_PRIORITY);
	printf("compute cp_hqd_pq_base_lo %u\n", AMDRDNA_COMPUTE_IMAGE_RING_LOW);
	printf("compute cp_hqd_pq_base_hi %u\n", AMDRDNA_COMPUTE_IMAGE_RING_HIGH);
	printf("compute cp_hqd_pq_rptr %u\n", AMDRDNA_COMPUTE_IMAGE_READ);
	printf("compute cp_hqd_pq_rptr_report_addr_lo %u\n", AMDRDNA_COMPUTE_IMAGE_READ_REPORT_LOW);
	printf("compute cp_hqd_pq_rptr_report_addr_hi %u\n", AMDRDNA_COMPUTE_IMAGE_READ_REPORT_HIGH);
	printf("compute cp_hqd_pq_wptr_poll_addr_lo %u\n", AMDRDNA_COMPUTE_IMAGE_WRITE_POLL_LOW);
	printf("compute cp_hqd_pq_wptr_poll_addr_hi %u\n", AMDRDNA_COMPUTE_IMAGE_WRITE_POLL_HIGH);
	printf("compute cp_hqd_pq_doorbell_control %u\n", AMDRDNA_COMPUTE_IMAGE_DOORBELL);
	printf("compute cp_hqd_pq_control %u\n", AMDRDNA_COMPUTE_IMAGE_RING_CONTROL);
	printf("compute cp_hqd_ib_control %u\n", AMDRDNA_COMPUTE_IMAGE_BUFFER_CONTROL);
	printf("compute cp_hqd_dequeue_request %u\n", AMDRDNA_COMPUTE_IMAGE_DEQUEUE);
	printf("compute cp_mqd_control %u\n", AMDRDNA_COMPUTE_IMAGE_CONTROL);
	printf("compute cp_hqd_eop_base_addr_lo %u\n", AMDRDNA_COMPUTE_IMAGE_END_LOW);
	printf("compute cp_hqd_eop_base_addr_hi %u\n", AMDRDNA_COMPUTE_IMAGE_END_HIGH);
	printf("compute cp_hqd_eop_control %u\n", AMDRDNA_COMPUTE_IMAGE_END_CONTROL);
	printf("compute cp_hqd_pq_wptr_lo %u\n", AMDRDNA_COMPUTE_IMAGE_WRITE_LOW);
	printf("compute cp_hqd_pq_wptr_hi %u\n", AMDRDNA_COMPUTE_IMAGE_WRITE_HIGH);
	printf("graphics cp_mqd_base_addr %u\n", AMDRDNA_GRAPHICS_IMAGE_SELF_LOW);
	printf("graphics cp_mqd_base_addr_hi %u\n", AMDRDNA_GRAPHICS_IMAGE_SELF_HIGH);
	printf("graphics cp_gfx_hqd_active %u\n", AMDRDNA_GRAPHICS_IMAGE_ACTIVE);
	printf("graphics cp_gfx_hqd_vmid %u\n", AMDRDNA_GRAPHICS_IMAGE_CONTEXT);
	printf("graphics cp_gfx_hqd_queue_priority %u\n", AMDRDNA_GRAPHICS_IMAGE_PRIORITY);
	printf("graphics cp_gfx_hqd_quantum %u\n", AMDRDNA_GRAPHICS_IMAGE_QUANTUM);
	printf("graphics cp_gfx_hqd_base %u\n", AMDRDNA_GRAPHICS_IMAGE_RING_LOW);
	printf("graphics cp_gfx_hqd_base_hi %u\n", AMDRDNA_GRAPHICS_IMAGE_RING_HIGH);
	printf("graphics cp_gfx_hqd_rptr %u\n", AMDRDNA_GRAPHICS_IMAGE_READ);
	printf("graphics cp_gfx_hqd_rptr_addr %u\n", AMDRDNA_GRAPHICS_IMAGE_READ_REPORT_LOW);
	printf("graphics cp_gfx_hqd_rptr_addr_hi %u\n", AMDRDNA_GRAPHICS_IMAGE_READ_REPORT_HIGH);
	printf("graphics cp_rb_wptr_poll_addr_lo %u\n", AMDRDNA_GRAPHICS_IMAGE_WRITE_POLL_LOW);
	printf("graphics cp_rb_wptr_poll_addr_hi %u\n", AMDRDNA_GRAPHICS_IMAGE_WRITE_POLL_HIGH);
	printf("graphics cp_rb_doorbell_control %u\n", AMDRDNA_GRAPHICS_IMAGE_DOORBELL);
	printf("graphics cp_gfx_hqd_cntl %u\n", AMDRDNA_GRAPHICS_IMAGE_RING_CONTROL);
	printf("graphics cp_gfx_hqd_wptr %u\n", AMDRDNA_GRAPHICS_IMAGE_WRITE_LOW);
	printf("graphics cp_gfx_hqd_wptr_hi %u\n", AMDRDNA_GRAPHICS_IMAGE_WRITE_HIGH);
	printf("graphics cp_gfx_mqd_control %u\n", AMDRDNA_GRAPHICS_IMAGE_CONTROL);

	/* The registers: GC window, offset, the Linux name. */
	printf("gc 0 0x%x GRBM_CNTL\n", AMDRDNA_GC_REG_BUS_CONTROL);
	printf("gc 0 0x%x GRBM_GFX_CNTL\n", AMDRDNA_GC_REG_QUEUE_SELECT);
	printf("gc 0 0x%x GB_ADDR_CONFIG\n", AMDRDNA_GC_REG_ADDRESS_CONFIG);
	printf("gc 0 0x%x CC_RB_BACKEND_DISABLE\n", AMDRDNA_GC_REG_BACKENDS_FUSED);
	printf("gc 0 0x%x GC_USER_RB_BACKEND_DISABLE\n", AMDRDNA_GC_REG_BACKENDS_OFF);
	printf("gc 0 0x%x CC_GC_SHADER_ARRAY_CONFIG\n", AMDRDNA_GC_REG_GROUPS_FUSED);
	printf("gc 0 0x%x GC_USER_SHADER_ARRAY_CONFIG\n", AMDRDNA_GC_REG_GROUPS_OFF);
	printf("gc 0 0x%x SH_MEM_CONFIG\n", AMDRDNA_GC_REG_SHADER_MEMORY_MODE);
	printf("gc 0 0x%x SH_MEM_BASES\n", AMDRDNA_GC_REG_SHADER_APERTURES);
	printf("gc 0 0x%x GDS_VMID0_BASE\n", AMDRDNA_GC_REG_SHARE_BASE);
	printf("gc 0 0x%x GDS_VMID0_SIZE\n", AMDRDNA_GC_REG_SHARE_SIZE);
	printf("gc 0 0x%x GDS_GWS_VMID0\n", AMDRDNA_GC_REG_WAVE_SYNC);
	printf("gc 0 0x%x GDS_OA_VMID0\n", AMDRDNA_GC_REG_ORDERED_APPEND);
	printf("gc 0 0x%x UTCL1_UTCL0_INVREQ_DISABLE\n", AMDRDNA_GC_REG_TRANSLATION_SKIP);
	printf("gc 0 0x%x GCRD_SA_TARGETS_DISABLE\n", AMDRDNA_GC_REG_CACHE_TARGETS_SKIP);
	printf("gc 0 0x%x CP_STAT\n", AMDRDNA_GC_REG_COMMAND_BUSY);
	printf("gc 0 0x%x CP_MEC_CNTL\n", AMDRDNA_GC_REG_COMPUTE_HALT);
	printf("gc 0 0x%x CP_ME_CNTL\n", AMDRDNA_GC_REG_GRAPHICS_HALT);
	printf("gc 0 0x%x CP_MAX_CONTEXT\n", AMDRDNA_GC_REG_CONTEXT_LIMIT);
	printf("gc 0 0x%x CP_DEVICE_ID\n", AMDRDNA_GC_REG_COMMAND_DEVICE);
	printf("gc 0 0x%x CP_INT_CNTL_RING0\n", AMDRDNA_GC_REG_GRAPHICS_INTERRUPTS);
	printf("gc 0 0x%x CP_ME1_PIPE0_INT_CNTL\n", AMDRDNA_GC_REG_COMPUTE_INTERRUPTS);
	printf("gc 0 0x%x CP_ME1_PIPE3_INT_CNTL\n", AMDRDNA_GC_REG_COMPUTE_INTERRUPTS + 3U);
	printf("gc 0 0x%x CP_RB_DOORBELL_RANGE_LOWER\n", AMDRDNA_GC_REG_GRAPHICS_DOORBELLS_LOW);
	printf("gc 0 0x%x CP_RB_DOORBELL_RANGE_UPPER\n", AMDRDNA_GC_REG_GRAPHICS_DOORBELLS_HIGH);
	printf("gc 0 0x%x CP_MEC_DOORBELL_RANGE_LOWER\n", AMDRDNA_GC_REG_COMPUTE_DOORBELLS_LOW);
	printf("gc 0 0x%x CP_MEC_DOORBELL_RANGE_UPPER\n", AMDRDNA_GC_REG_COMPUTE_DOORBELLS_HIGH);
	printf("gc 0 0x%x CP_PQ_WPTR_POLL_CNTL\n", AMDRDNA_GC_REG_POINTER_POLL);
	printf("gc 0 0x%x CP_PQ_STATUS\n", AMDRDNA_GC_REG_QUEUE_STATUS);
	printf("gc 0 0x%x CP_MQD_BASE_ADDR\n", AMDRDNA_GC_REG_QUEUE_IMAGE_LOW);
	printf("gc 0 0x%x CP_MQD_BASE_ADDR_HI\n", AMDRDNA_GC_REG_QUEUE_IMAGE_HIGH);
	printf("gc 0 0x%x CP_HQD_ACTIVE\n", AMDRDNA_GC_REG_QUEUE_ACTIVE);
	printf("gc 0 0x%x CP_HQD_VMID\n", AMDRDNA_GC_REG_QUEUE_CONTEXT);
	printf("gc 0 0x%x CP_HQD_PERSISTENT_STATE\n", AMDRDNA_GC_REG_QUEUE_PERSISTENT);
	printf("gc 0 0x%x CP_HQD_PQ_BASE\n", AMDRDNA_GC_REG_QUEUE_RING_LOW);
	printf("gc 0 0x%x CP_HQD_PQ_BASE_HI\n", AMDRDNA_GC_REG_QUEUE_RING_HIGH);
	printf("gc 0 0x%x CP_HQD_PQ_RPTR\n", AMDRDNA_GC_REG_QUEUE_READ);
	printf("gc 0 0x%x CP_HQD_PQ_RPTR_REPORT_ADDR\n", AMDRDNA_GC_REG_QUEUE_READ_REPORT_LOW);
	printf("gc 0 0x%x CP_HQD_PQ_RPTR_REPORT_ADDR_HI\n", AMDRDNA_GC_REG_QUEUE_READ_REPORT_HIGH);
	printf("gc 0 0x%x CP_HQD_PQ_WPTR_POLL_ADDR\n", AMDRDNA_GC_REG_QUEUE_WRITE_POLL_LOW);
	printf("gc 0 0x%x CP_HQD_PQ_WPTR_POLL_ADDR_HI\n", AMDRDNA_GC_REG_QUEUE_WRITE_POLL_HIGH);
	printf("gc 0 0x%x CP_HQD_PQ_DOORBELL_CONTROL\n", AMDRDNA_GC_REG_QUEUE_DOORBELL);
	printf("gc 0 0x%x CP_HQD_PQ_CONTROL\n", AMDRDNA_GC_REG_QUEUE_RING_CONTROL);
	printf("gc 0 0x%x CP_HQD_IB_CONTROL\n", AMDRDNA_GC_REG_QUEUE_BUFFER_CONTROL);
	printf("gc 0 0x%x CP_HQD_DEQUEUE_REQUEST\n", AMDRDNA_GC_REG_QUEUE_DEQUEUE);
	printf("gc 0 0x%x CP_MQD_CONTROL\n", AMDRDNA_GC_REG_QUEUE_IMAGE_CONTROL);
	printf("gc 0 0x%x CP_HQD_EOP_BASE_ADDR\n", AMDRDNA_GC_REG_QUEUE_END_LOW);
	printf("gc 0 0x%x CP_HQD_EOP_BASE_ADDR_HI\n", AMDRDNA_GC_REG_QUEUE_END_HIGH);
	printf("gc 0 0x%x CP_HQD_EOP_CONTROL\n", AMDRDNA_GC_REG_QUEUE_END_CONTROL);
	printf("gc 0 0x%x CP_HQD_PQ_WPTR_LO\n", AMDRDNA_GC_REG_QUEUE_WRITE_LOW);
	printf("gc 0 0x%x CP_HQD_PQ_WPTR_HI\n", AMDRDNA_GC_REG_QUEUE_WRITE_HIGH);
	printf("gc 0 0x%x CP_GFX_MQD_CONTROL\n", AMDRDNA_GC_REG_GRAPHICS_IMAGE_CONTROL);
	printf("gc 0 0x%x CP_GFX_HQD_VMID\n", AMDRDNA_GC_REG_GRAPHICS_CONTEXT);
	printf("gc 0 0x%x CP_GFX_HQD_QUEUE_PRIORITY\n", AMDRDNA_GC_REG_GRAPHICS_PRIORITY);
	printf("gc 0 0x%x CP_GFX_HQD_QUANTUM\n", AMDRDNA_GC_REG_GRAPHICS_QUANTUM);
	printf("gc 0 0x%x CP_GFX_HQD_CNTL\n", AMDRDNA_GC_REG_GRAPHICS_RING_CONTROL);
	printf("gc 0 0x%x CP_GFX_HQD_RPTR\n", AMDRDNA_GC_REG_GRAPHICS_READ);
	printf("gc 0 0x%x CP_RB_DOORBELL_CONTROL\n", AMDRDNA_GC_REG_GRAPHICS_DOORBELL);
	printf("gc 0 0x%x CP_RB_WPTR_DELAY\n", AMDRDNA_GC_REG_RING_WRITE_DELAY);
	printf("gc 0 0x%x CP_RB_VMID\n", AMDRDNA_GC_REG_RING_CONTEXT);
	printf("gc 0 0x%x CP_RB0_CNTL\n", AMDRDNA_GC_REG_RING_CONTROL);
	printf("gc 0 0x%x CP_RB0_WPTR\n", AMDRDNA_GC_REG_RING_WRITE_LOW);
	printf("gc 0 0x%x CP_RB0_WPTR_HI\n", AMDRDNA_GC_REG_RING_WRITE_HIGH);
	printf("gc 0 0x%x CP_RB0_RPTR_ADDR\n", AMDRDNA_GC_REG_RING_READ_REPORT_LOW);
	printf("gc 0 0x%x CP_RB0_RPTR_ADDR_HI\n", AMDRDNA_GC_REG_RING_READ_REPORT_HIGH);
	printf("gc 0 0x%x CP_RB_WPTR_POLL_ADDR_LO\n", AMDRDNA_GC_REG_RING_WRITE_POLL_LOW);
	printf("gc 0 0x%x CP_RB_WPTR_POLL_ADDR_HI\n", AMDRDNA_GC_REG_RING_WRITE_POLL_HIGH);
	printf("gc 0 0x%x CP_RB0_BASE\n", AMDRDNA_GC_REG_RING_BASE_LOW);
	printf("gc 0 0x%x CP_RB0_BASE_HI\n", AMDRDNA_GC_REG_RING_BASE_HIGH);
	printf("gc 0 0x%x CP_RB_ACTIVE\n", AMDRDNA_GC_REG_RING_ACTIVE);
	printf("gc 1 0x%x GRBM_GFX_INDEX\n", AMDRDNA_GC_REG_ARRAY_SELECT);
	printf("gc 1 0x%x SCRATCH_REG0\n", AMDRDNA_GC_REG_SCRATCH);
	printf("gc 1 0x%x CGTS_TCC_DISABLE\n", AMDRDNA_GC_REG_CHANNELS_FUSED);
	printf("gc 1 0x%x CGTS_USER_TCC_DISABLE\n", AMDRDNA_GC_REG_CHANNELS_OFF);
	printf("gc 1 0x%x RLC_CSIB_ADDR_LO\n", AMDRDNA_GC_REG_CLEAR_STATE_LOW);
	printf("gc 1 0x%x RLC_CSIB_ADDR_HI\n", AMDRDNA_GC_REG_CLEAR_STATE_HIGH);
	printf("gc 1 0x%x RLC_CSIB_LENGTH\n", AMDRDNA_GC_REG_CLEAR_STATE_LENGTH);
	printf("gc 1 0x%x RLC_SRM_CNTL\n", AMDRDNA_GC_REG_SAVE_RESTORE);
	printf("gc 1 0x%x RLC_CP_SCHEDULERS\n", AMDRDNA_GC_REG_SCHEDULER_QUEUE_SELECT);
	printf("gc 1 0x%x RLC_RLCS_BOOTLOAD_STATUS\n", AMDRDNA_GC_REG_RUN_LIST_BOOT);
	printf("gc 0 0x%x SDMA0_CNTL\n", AMDRDNA_COPY_REG_CONTROL);
	printf("gc 0 0x%x SDMA0_CHICKEN_BITS\n", AMDRDNA_COPY_REG_CHICKEN);
	printf("gc 0 0x%x SDMA0_SEM_WAIT_FAIL_TIMER_CNTL\n", AMDRDNA_COPY_REG_WAIT_TIMER);
	printf("gc 0 0x%x SDMA0_F32_CNTL\n", AMDRDNA_COPY_REG_HALT);
	printf("gc 0 0x%x SDMA0_UTCL1_CNTL\n", AMDRDNA_COPY_REG_TRANSLATION);
	printf("gc 0 0x%x SDMA0_UTCL1_PAGE\n", AMDRDNA_COPY_REG_PAGE_POLICY);
	printf("gc 0 0x%x SDMA0_GFX_RB_CNTL\n", AMDRDNA_COPY_REG_RING_CONTROL);
	printf("gc 0 0x%x SDMA0_GFX_RB_BASE\n", AMDRDNA_COPY_REG_RING_BASE_LOW);
	printf("gc 0 0x%x SDMA0_GFX_RB_BASE_HI\n", AMDRDNA_COPY_REG_RING_BASE_HIGH);
	printf("gc 0 0x%x SDMA0_GFX_RB_RPTR\n", AMDRDNA_COPY_REG_READ_LOW);
	printf("gc 0 0x%x SDMA0_GFX_RB_RPTR_HI\n", AMDRDNA_COPY_REG_READ_HIGH);
	printf("gc 0 0x%x SDMA0_GFX_RB_WPTR\n", AMDRDNA_COPY_REG_WRITE_LOW);
	printf("gc 0 0x%x SDMA0_GFX_RB_WPTR_HI\n", AMDRDNA_COPY_REG_WRITE_HIGH);
	printf("gc 0 0x%x SDMA0_GFX_RB_WPTR_POLL_CNTL\n", AMDRDNA_COPY_REG_WRITE_POLL);
	printf("gc 0 0x%x SDMA0_GFX_RB_RPTR_ADDR_HI\n", AMDRDNA_COPY_REG_READ_REPORT_HIGH);
	printf("gc 0 0x%x SDMA0_GFX_RB_RPTR_ADDR_LO\n", AMDRDNA_COPY_REG_READ_REPORT_LOW);
	printf("gc 0 0x%x SDMA0_GFX_IB_CNTL\n", AMDRDNA_COPY_REG_BUFFER_CONTROL);
	printf("gc 0 0x%x SDMA0_GFX_DOORBELL\n", AMDRDNA_COPY_REG_DOORBELL);
	printf("gc 0 0x%x SDMA0_GFX_DOORBELL_OFFSET\n", AMDRDNA_COPY_REG_DOORBELL_OFFSET);
	printf("gc 0 0x%x SDMA0_GFX_RB_WPTR_POLL_ADDR_HI\n", AMDRDNA_COPY_REG_WRITE_POLL_HIGH);
	printf("gc 0 0x%x SDMA0_GFX_RB_WPTR_POLL_ADDR_LO\n", AMDRDNA_COPY_REG_WRITE_POLL_LOW);
	printf("gc 0 0x%x SDMA0_GFX_MINOR_PTR_UPDATE\n", AMDRDNA_COPY_REG_MINOR_UPDATE);
	printf("gc 0 0x%x SDMA1_F32_CNTL\n", AMDRDNA_COPY_REG_HALT + AMDRDNA_COPY_SECOND_ENGINE);
	printf("gc 0 0x%x COMPUTE_START_X\n", AMDRDNA_GC_REG_DISPATCH_START);
	printf("gc 0 0x%x COMPUTE_NUM_THREAD_X\n", AMDRDNA_GC_REG_DISPATCH_START + 3U);
	printf("gc 0 0x%x COMPUTE_NUM_THREAD_Z\n", AMDRDNA_GC_REG_DISPATCH_START + 5U);
	printf("gc 0 0x%x COMPUTE_PGM_LO\n", AMDRDNA_GC_REG_DISPATCH_PROGRAM);
	printf("gc 0 0x%x COMPUTE_PGM_HI\n", AMDRDNA_GC_REG_DISPATCH_PROGRAM + 1U);
	printf("gc 0 0x%x COMPUTE_PGM_RSRC1\n", AMDRDNA_GC_REG_DISPATCH_COUNTS);
	printf("gc 0 0x%x COMPUTE_PGM_RSRC2\n", AMDRDNA_GC_REG_DISPATCH_COUNTS + 1U);
	printf("gc 0 0x%x COMPUTE_RESOURCE_LIMITS\n", AMDRDNA_GC_REG_DISPATCH_LIMITS);
	printf("gc 0 0x%x COMPUTE_STATIC_THREAD_MGMT_SE0\n", AMDRDNA_GC_REG_DISPATCH_LIMITS + 1U);
	printf("gc 0 0x%x COMPUTE_STATIC_THREAD_MGMT_SE1\n", AMDRDNA_GC_REG_DISPATCH_LIMITS + 2U);
	printf("gc 0 0x%x COMPUTE_TMPRING_SIZE\n", AMDRDNA_GC_REG_DISPATCH_LIMITS + 3U);
	printf("gc 0 0x%x COMPUTE_STATIC_THREAD_MGMT_SE2\n", AMDRDNA_GC_REG_DISPATCH_LIMITS + 4U);
	printf("gc 0 0x%x COMPUTE_STATIC_THREAD_MGMT_SE3\n", AMDRDNA_GC_REG_DISPATCH_LIMITS + 5U);
	printf("gc 0 0x%x COMPUTE_PGM_RSRC3\n", AMDRDNA_GC_REG_DISPATCH_MORE);
	printf("gc 0 0x%x COMPUTE_USER_DATA_0\n", AMDRDNA_GC_REG_DISPATCH_USER);
	printf("nbio 2 0x%x BIF_SDMA0_DOORBELL_RANGE\n", AMDRDNA_NBIO_REG_COPY_DOORBELLS);
}
