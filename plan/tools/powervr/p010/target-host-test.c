/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of ws269-p010d: render-target-layout.c's sizes and register
 * values of a render target, the firmware records of a render target and
 * a free list read back at the offsets Linux v7.3's
 * pvr_rogue_fwif_check.h gives, and the frames of the grow, rebuild,
 * statistics and cleanup commands.  The expected sizes were worked out
 * by hand from Mesa 26.2.5's pvr_arch_job_render.c for the frame sizes
 * below (plan/ws269/phase010d/phase.md).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/render-target-layout.h"

static int failures;
static int passes;

/* One expectation. */
static void
expect(
	int good,
	const char *what)
{
	if (good) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}

/* A little-endian 32-bit word of a record at a byte offset. */
static uint32_t
word32(
	const void *record,
	size_t offset)
{
	const uint8_t *bytes;

	bytes = record;
	return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) | ((uint32_t)bytes[offset + 2] << 16) |
	       ((uint32_t)bytes[offset + 3] << 24);
}

/* A little-endian 64-bit word of a record at a byte offset. */
static uint64_t
word64(
	const void *record,
	size_t offset)
{
	return (uint64_t)word32(record, offset) | ((uint64_t)word32(record, offset + 4) << 32);
}

/* Lays a one-sample, one-layer target out. */
static int
lay(
	uint32_t width,
	uint32_t height,
	uint32_t samples,
	uint32_t layers,
	struct powervr_target_layout *layout)
{
	struct powervr_target_shape shape;

	shape.width = width;
	shape.height = height;
	shape.samples = samples;
	shape.layers = layers;
	return powervr_target_lay_out(&shape, POWERVR_FREE_LIST_LOCAL_PAGES, POWERVR_FREE_LIST_GLOBAL_PAGES, layout);
}

/* The layouts of 1920x1080 and 800x600, and the shapes refused. */
static void
check_layout(void)
{
	struct powervr_target_layout layout;

	/* 1920x1080: 120x68 tiles, 30x18-tile macrotiles. */
	expect(lay(1920, 1080, 1, 1, &layout) == 0, "1080p lays out");
	expect(layout.tiles_x == 120 && layout.tiles_y == 68, "1080p tiles");
	expect(layout.macrotile_x == 30 && layout.macrotile_y == 18, "1080p macrotile");
	expect(layout.region_headers == 2160, "1080p region header count");
	expect(layout.region_header_bytes == 12992, "1080p region header bytes (12960 to 64)");
	expect(layout.tail_used_bytes == 32768 && layout.tail_bytes == 32768, "1080p tail pointer cache");
	expect(layout.vertex_heap_bytes == 1536, "vertex heap table");
	expect(layout.macrolist_bytes == 4096, "macrolist: 11 pages x 2 spaces x 4, to a page");
	expect(layout.screen_pixel_max == ((1079U << 16) | 1919U), "1080p PPP screen");
	expect(layout.te_screen == (119U | (67U << 12)), "1080p TE screen");
	expect(layout.te_macrotile_x == (30U << 18) && layout.te_macrotile_y == (18U << 18), "1080p TE macrotiles");
	expect(layout.macrotile_stride == 540, "1080p macrotile stride");
	expect(layout.isp_macrotile_size == ((30U << 16) | 18U), "1080p ISP macrotile size");
	expect(layout.multisample == 0x88 && layout.flipped_multisample == 0x88, "one sample at the centre");

	/* 800x600: 50x38 tiles, 14x10-tile macrotiles. */
	expect(lay(800, 600, 1, 1, &layout) == 0, "800x600 lays out");
	expect(layout.macrotile_x == 14 && layout.macrotile_y == 10, "800x600 macrotile");
	expect(layout.region_headers == 560, "800x600 region header count");
	expect(layout.region_header_bytes == 3392, "800x600 region header bytes (3360 to 64)");
	expect(layout.tail_bytes == 8192, "800x600 tail pointer cache (64x64 / 4 x 8)");
	expect(layout.te_screen == (49U | (37U << 12)), "800x600 TE screen");

	/* An odd tile count: 17 tiles round up to 18 for the last tile. */
	expect(lay(272, 16, 1, 1, &layout) == 0, "272x16 lays out");
	expect(layout.te_screen == (17U | (1U << 12)), "272x16 TE screen (17 tiles to 18, last 17; 1 row to 2, last 1)");

	/* The largest edge, and the shapes refused. */
	expect(lay(8192, 8192, 1, 1, &layout) == 0, "8192x8192 lays out");
	expect(layout.te_screen == (511U | (511U << 12)), "8192 TE screen");
	expect(lay(0, 16, 1, 1, &layout) == EINVAL, "width 0 refused");
	expect(lay(8193, 16, 1, 1, &layout) == EINVAL, "width 8193 refused");
	expect(lay(16, 16, 4, 1, &layout) == EINVAL, "4 samples refused");
	expect(lay(16, 16, 1, 2, &layout) == EINVAL, "2 layers refused");
}

/* The records of a target's halves and shared part, read at the firmware's offsets. */
static void
check_target_records(void)
{
	struct powervr_target_layout layout;
	struct powervr_fw_target_common common;
	struct powervr_fw_target_data data;
	struct powervr_target_places places;
	uint8_t zero[64];

	/* The block: halves at 0 and 384, the shared record at 768. */
	expect(POWERVR_TARGET_DATA_OFFSET(1) == 384, "second half at 384");
	expect(POWERVR_TARGET_COMMON_OFFSET == 768 && POWERVR_TARGET_RECORD_BYTES == 856, "shared record at 768, 856 bytes");

	/* The shared record of 1080p. */
	(void)lay(1920, 1080, 1, 1, &layout);
	powervr_target_common_record(&layout, &common);
	expect(word32(&common, 0) == 0, "common: caches need no zeroing");
	expect(word32(&common, 4) == layout.screen_pixel_max, "common: screen at 4");
	expect(word64(&common, 8) == 0x88 && word64(&common, 16) == 0x88, "common: sample positions at 8 and 16");
	expect(word32(&common, 24) == 0 && word32(&common, 28) == 32768, "common: tail stride and size at 24 and 28");
	expect(word32(&common, 32) == layout.te_screen && word32(&common, 36) == 540, "common: TE screen and stride at 32 and 36");
	expect(word32(&common, 40) == 0, "common: no TE antialias");
	expect(word32(&common, 44) == (30U << 18) && word32(&common, 48) == (18U << 18), "common: TE macrotiles at 44 and 48");
	expect(word32(&common, 52) == 0 && word32(&common, 72) == 0, "common: no ISP merge");
	expect(word32(&common, 76) == 2160 && word32(&common, 80) == layout.isp_macrotile_size, "common: region headers and ISP macrotile at 76 and 80");

	/* One half. */
	places.macrolist = 0x8000010000ULL;
	places.region_headers = 0x8000020000ULL;
	places.vertex_heap = 0x8000030000ULL;
	places.tail = 0x8000040000ULL;
	places.common = 0x00a00300U;
	places.local_list = 0x00a00100U;
	places.global_list = 0x00a00200U;
	powervr_target_data_record(&places, &data);
	expect(word64(&data, 0) == places.macrolist, "data: macrolist at 0");
	expect(word32(&data, 164) == places.common, "data: shared record at 164");
	expect(word32(&data, 168) == 0 && word32(&data, 172) == 0, "data: no flags, state none");
	expect(word32(&data, 176) == places.local_list && word32(&data, 180) == places.global_list, "data: local then global list at 176");
	expect(word32(&data, 184) == 0, "data: no third list");
	expect(word64(&data, 200) == places.vertex_heap, "data: vertex heap at 200");
	expect(word32(&data, 208 + 24) == 1, "data: one target in the array (rta_ctl max_rts)");
	expect(word64(&data, 240) == places.tail && word64(&data, 248) == 0, "data: tails at 240, no macrotile array");
	expect(word64(&data, 256) == places.region_headers && word64(&data, 264) == 0, "data: region headers at 256, no target cache");
	memset(zero, 0, sizeof(zero));
	expect(memcmp((const uint8_t *)&data + 320, zero, 64) == 0, "data: cleanup count at 320 starts at zero");
}

/* A free list's record and its page list entries. */
static void
check_free_list(void)
{
	struct powervr_fw_free_list record;

	/* The global list: 4096 pages, full, never growing. */
	powervr_free_list_record(0x8000123000ULL, POWERVR_FREE_LIST_GLOBAL_PAGES, 7, &record);
	expect(word64(&record, 0) == 0x8000123000ULL && word64(&record, 8) == 0x8000123000ULL, "list: base and current address at 0 and 8");
	expect(word32(&record, 16) == 4095, "list: stack top at 16");
	expect(word32(&record, 20) == 4096 && word32(&record, 28) == 4096, "list: max and current pages at 20 and 28");
	expect(word32(&record, 24) == 0 && word32(&record, 48) == 0, "list: nothing to grow or held ready");
	expect(word32(&record, 40) == 7 && word32(&record, 44) == 0, "list: identity at 40, no grow pending");
	expect(POWERVR_FREE_LIST_LOCAL_PAGES >= POWERVR_FREE_LIST_MIN_PAGES, "local list at least the core's minimum");
	expect(POWERVR_FREE_LIST_LOCAL_PAGES % POWERVR_FREE_LIST_STEP == 0, "local list a whole step");
	expect(POWERVR_FREE_LIST_GLOBAL_PAGES % POWERVR_FREE_LIST_STEP == 0, "global list a whole step");
	expect(POWERVR_FREE_LIST_GLOBAL_PAGES * POWERVR_PM_PAGE_BYTES == 16U << 20, "global list 16 MiB");

	/* Entries are the pages' numbers. */
	expect(powervr_free_list_entry(0x80000000ULL, 0) == 0x80000, "entry 0");
	expect(powervr_free_list_entry(0x80000000ULL, 5) == 0x80005, "entry 5");
}

/* The grow, rebuild, statistics and cleanup frames. */
static void
check_frames(void)
{
	uint32_t notice[POWERVR_TARGET_COMMAND_WORDS];
	uint32_t command[POWERVR_TARGET_COMMAND_WORDS];
	uint32_t identities[POWERVR_REBUILD_LISTS_MAX];
	uint32_t identity;
	uint32_t counter;
	uint32_t count;
	int32_t adjustment;
	unsigned index;
	int others;

	/* A grow request of list 7, and one that is not. */
	memset(notice, 0, sizeof(notice));
	notice[0] = 0x2abc0067U;
	notice[2] = 7;
	expect(powervr_grow_request(notice, &identity) == 0 && identity == 7, "grow request (103) names list 7");
	notice[0] = 0x2abc0068U;
	expect(powervr_grow_request(notice, &identity) == EINVAL, "a rebuild is not a grow request");

	/* The answer: type 110, the list, nothing added, its pages, none ready. */
	powervr_grow_answer(0x00a00100U, 4096, command);
	expect(command[0] == 0x2abc006eU && command[1] == 0, "grow answer type 110");
	expect(command[2] == 0x00a00100U, "grow answer: list address");
	expect(command[3] == 0, "grow answer: delta 0");
	expect(command[4] == 4096, "grow answer: new pages = current + ready");
	expect(command[5] == 0, "grow answer: ready 0");
	others = 0;
	for (index = 6; index < POWERVR_TARGET_COMMAND_WORDS; index++)
		others |= command[index] != 0;
	expect(!others, "grow answer: the rest zero");

	/* A rebuild request of three lists, and its answer. */
	memset(notice, 0, sizeof(notice));
	notice[0] = 0x2abc0068U;
	notice[2] = 3;
	notice[3] = 1;
	notice[4] = 11;
	notice[5] = 12;
	notice[6] = 13;
	expect(powervr_rebuild_request(notice, identities, &count) == 0 && count == 3, "rebuild request (104) of three");
	expect(identities[0] == 11 && identities[1] == 12 && identities[2] == 13, "rebuild request identities at 8");
	powervr_rebuild_answer(identities, count, command);
	expect(command[0] == 0x2abc0070U && command[2] == 3, "rebuild answer type 112, count");
	expect(command[3] == 11 && command[4] == 12 && command[5] == 13 && command[6] == 0, "rebuild answer identities at 4");

	/* A count past the array is limited. */
	notice[2] = 30;
	expect(powervr_rebuild_request(notice, identities, &count) == 0 && count == POWERVR_REBUILD_LISTS_MAX, "rebuild count limited to 18");

	/* The statistics: a partial render. */
	memset(notice, 0, sizeof(notice));
	notice[0] = 0x2abc006bU;
	notice[2] = 1;
	notice[3] = 1234;
	notice[4] = 1;
	expect(powervr_statistics_notice(notice, &counter, &adjustment) == 0, "statistics (107) read");
	expect(counter == POWERVR_STATISTICS_PARTIAL && adjustment == 1, "statistics: partial renders +1");

	/* The cleanup of a target half and of a list. */
	powervr_cleanup_command(POWERVR_CLEANUP_TARGET_DATA, 0x00a00300U, command);
	expect(command[0] == 0x2abc006aU && command[2] == 1 && command[3] == 0x00a00300U, "cleanup of a target half (106, kind 1)");
	powervr_cleanup_command(POWERVR_CLEANUP_FREE_LIST, 0x00a00100U, command);
	expect(command[2] == 2 && command[3] == 0x00a00100U, "cleanup of a free list (kind 2)");
}

int
main(void)
{
	check_layout();
	check_target_records();
	check_free_list();
	check_frames();
	printf("target-host-test: %d passed, %d failed\n", passes, failures);
	return failures != 0;
}
