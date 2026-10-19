/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the riscv64 HAL's reading of the loader's handoff
 * (src/hal/riscv64/loader.c): the first layout from either version, the
 * second layout's screen and its checks, and the firmware's RAM against
 * the tree's banks and reservations, joined, and refused past the limit.
 *
 *   loader-take-host-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/hal.h>
#include <kern/boot.h>
#include "loader.h"

/* How many checks failed. */
static int failures;

/* Prints one check's outcome and counts a failure. */
static void
check(
	const char *name,
	int condition)
{
	/* One line per check. */
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

/* The HAL's memset, which the reading calls. */
void *
hal_memset(
	void *pointer,
	int value,
	size_t size)
{
	/* The C library's. */
	return memset(pointer, value, size);
}

/* Fills a handoff of the second layout with no screen and no ranges. */
static void
fill_v2(
	struct zbl6_riscv64_handoff *handoff)
{
	/* The header of the second layout. */
	memset(handoff, 0, sizeof(*handoff));
	handoff->magic = ZBL6_HANDOFF_MAGIC;
	handoff->version = 2;
	handoff->size = ZBL6_RISCV64_HANDOFF_V2_SIZE;
}

/* Fills a valid 1920x1080 blue-first screen at 0xfe000000. */
static void
fill_screen(
	struct zbl6_riscv64_handoff *handoff)
{
	/* A screen the kernel can name. */
	handoff->framebuffer_phys = 0xfe000000ULL;
	handoff->framebuffer_size = 1920ULL * 4U * 1080U;
	handoff->framebuffer_width = 1920;
	handoff->framebuffer_height = 1080;
	handoff->framebuffer_pitch = 1920U * 4U;
	handoff->framebuffer_format = KERN_BOOT_FRAMEBUFFER_BGRX8888;
}

/* Fills the tree's facts: 4 GiB of RAM at 0x40000000, OpenSBI reserved, one no-map range. */
static void
fill_info(
	struct riscv64_fdt_info *info)
{
	/* One bank, one reservation and one no-map range. */
	memset(info, 0, sizeof(*info));
	info->memory_count = 1;
	info->memory[0].base = 0x40000000ULL;
	info->memory[0].size = 0x100000000ULL;
	info->reserved_count = 1;
	info->reserved[0].base = 0x40000000ULL;
	info->reserved[0].size = 0x80000ULL;
	info->nomap_count = 1;
	info->nomap[0].base = 0x60000000ULL;
	info->nomap[0].size = 0x100000ULL;
}

/* Adds one firmware range to a handoff. */
static void
add(
	struct zbl6_riscv64_handoff *handoff,
	uint64_t phys,
	uint64_t size)
{
	/* The next slot. */
	handoff->reserved[handoff->reserved_count].phys = phys;
	handoff->reserved[handoff->reserved_count].size = size;
	handoff->reserved_count++;
}

/* Checks the first layout's acceptance. */
static void
test_check(void)
{
	struct zbl6_riscv64_handoff handoff;
	int result;

	/* Version 0 is older than every layout. */
	fill_v2(&handoff);
	handoff.version = 0;
	result = riscv64_loader_check(&handoff);
	check("version 0 is refused", result == RISCV64_LOADER_OLD_VERSION);

	/* Version 1 one byte short of its end. */
	handoff.version = 1;
	handoff.size = 3159;
	result = riscv64_loader_check(&handoff);
	check("version 1 of 3159 bytes is too short", result == RISCV64_LOADER_TOO_SHORT);

	/* Version 1 of exactly its size, as a first-layout loader writes it. */
	handoff.size = 3160;
	result = riscv64_loader_check(&handoff);
	check("version 1 of 3160 bytes is taken", result == RISCV64_LOADER_OK);

	/* Version 2 of either size and a later version. */
	handoff.version = 2;
	result = riscv64_loader_check(&handoff);
	check("version 2 of 3160 bytes is taken", result == RISCV64_LOADER_OK);
	handoff.size = ZBL6_RISCV64_HANDOFF_V2_SIZE;
	result = riscv64_loader_check(&handoff);
	check("version 2 of its own size is taken", result == RISCV64_LOADER_OK);
	handoff.version = 7;
	handoff.size = ZBL6_RISCV64_HANDOFF_V2_SIZE + 64U;
	result = riscv64_loader_check(&handoff);
	check("a later version is taken", result == RISCV64_LOADER_OK);
	check("the second layout ends at 3456", ZBL6_RISCV64_HANDOFF_V2_SIZE == 3456U);
}

/* Checks which handoffs give the second layout's fields. */
static void
test_layouts(void)
{
	struct zbl6_riscv64_handoff handoff;
	struct riscv64_fdt_info info;
	struct riscv64_loader_extras extras;
	int result;

	/* A first-layout handoff, even with bytes after its end, gives nothing more. */
	fill_info(&info);
	fill_v2(&handoff);
	fill_screen(&handoff);
	add(&handoff, 0x80000000ULL, 0x1000ULL);
	handoff.version = 1;
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("version 1: taken", result == RISCV64_LOADER_OK);
	check("version 1: layout 1, no screen, no ranges",
	      extras.layout == 1U && extras.screen == RISCV64_LOADER_SCREEN_NONE && extras.reserved_count == 0U);

	/* Version 2 shorter than its layout is read as the first. */
	handoff.version = 2;
	handoff.size = 3160;
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("version 2 short: read as layout 1", result == RISCV64_LOADER_OK && extras.layout == 1U && extras.reserved_count == 0U);

	/* Version 2 of its size gives the screen and the range. */
	handoff.size = ZBL6_RISCV64_HANDOFF_V2_SIZE;
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("version 2: taken", result == RISCV64_LOADER_OK && extras.layout == 2U);
	check("version 2: screen taken with its values",
	      extras.screen == RISCV64_LOADER_SCREEN_TAKEN &&
	      extras.framebuffer.phys == 0xfe000000ULL &&
	      extras.framebuffer.size == 1920ULL * 4U * 1080U &&
	      extras.framebuffer.width == 1920U &&
	      extras.framebuffer.height == 1080U &&
	      extras.framebuffer.pitch == 7680U &&
	      extras.framebuffer.format == KERN_BOOT_FRAMEBUFFER_BGRX8888);
	check("version 2: the range kept",
	      extras.reserved_count == 1U && extras.reserved[0].base == 0x80000000ULL && extras.reserved[0].size == 0x1000ULL);
	check("version 2: the count given recorded", extras.reserved_given == 1U);
}

/* Checks the screens the reading refuses or finds absent. */
static void
test_screens(void)
{
	struct zbl6_riscv64_handoff handoff;
	struct riscv64_fdt_info info;
	struct riscv64_loader_extras extras;

	/* No memory is no screen. */
	fill_info(&info);
	fill_v2(&handoff);
	fill_screen(&handoff);
	handoff.framebuffer_size = 0;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("size 0: no screen", extras.screen == RISCV64_LOADER_SCREEN_NONE);

	/* A red-first screen is taken. */
	fill_screen(&handoff);
	handoff.framebuffer_format = KERN_BOOT_FRAMEBUFFER_RGBX8888;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("red first: taken", extras.screen == RISCV64_LOADER_SCREEN_TAKEN);

	/* Each bad value is refused. */
	fill_screen(&handoff);
	handoff.framebuffer_format = 3;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("format 3: refused", extras.screen == RISCV64_LOADER_SCREEN_REFUSED);
	fill_screen(&handoff);
	handoff.framebuffer_width = 0;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("width 0: refused", extras.screen == RISCV64_LOADER_SCREEN_REFUSED);
	fill_screen(&handoff);
	handoff.framebuffer_pitch = 1919U * 4U;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("pitch shorter than the width: refused", extras.screen == RISCV64_LOADER_SCREEN_REFUSED);
	fill_screen(&handoff);
	handoff.framebuffer_pitch = 1920U * 4U + 2U;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("pitch of part pixels: refused", extras.screen == RISCV64_LOADER_SCREEN_REFUSED);
	fill_screen(&handoff);
	handoff.framebuffer_size = 1920ULL * 4U * 1080U - 1U;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("memory one byte short: refused", extras.screen == RISCV64_LOADER_SCREEN_REFUSED);
	fill_screen(&handoff);
	handoff.framebuffer_phys = ~0ULL - 0x1000ULL;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("memory that wraps: refused", extras.screen == RISCV64_LOADER_SCREEN_REFUSED);
	check("a refused screen copies nothing", extras.framebuffer.size == 0U && extras.framebuffer.phys == 0U);

	/* A wider pitch than the width is taken. */
	fill_screen(&handoff);
	handoff.framebuffer_pitch = 2048U * 4U;
	handoff.framebuffer_size = 2048ULL * 4U * 1080U;
	(void)riscv64_loader_take(&handoff, &info, &extras);
	check("pitch wider than the width: taken", extras.screen == RISCV64_LOADER_SCREEN_TAKEN && extras.framebuffer.pitch == 8192U);
}

/* Checks the firmware ranges against the tree and their joining. */
static void
test_ranges(void)
{
	struct zbl6_riscv64_handoff handoff;
	struct riscv64_fdt_info info;
	struct riscv64_loader_extras extras;
	int result;

	/* Ranges the tree reserves already, outside RAM, or empty are not kept. */
	fill_info(&info);
	fill_v2(&handoff);
	add(&handoff, 0x40000000ULL, 0x80000ULL);
	add(&handoff, 0x40010000ULL, 0x1000ULL);
	add(&handoff, 0x60000000ULL, 0x100000ULL);
	add(&handoff, 0x10000000ULL, 0x1000ULL);
	add(&handoff, 0x140000000ULL, 0x1000ULL);
	add(&handoff, 0x90000000ULL, 0);
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("tree-reserved, outside RAM and empty: none kept", result == RISCV64_LOADER_OK && extras.reserved_count == 0U);

	/* A range that only overlaps a reservation is kept whole, and one across RAM's start too. */
	fill_v2(&handoff);
	add(&handoff, 0x4007f000ULL, 0x2000ULL);
	add(&handoff, 0x3ffff000ULL, 0x2000ULL);
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("partly reserved and across RAM's start: both kept", result == RISCV64_LOADER_OK && extras.reserved_count == 2U);

	/* Touching and overlapping ranges are joined, a bridge joins two into one. */
	fill_v2(&handoff);
	add(&handoff, 0x80000000ULL, 0x1000ULL);
	add(&handoff, 0x80003000ULL, 0x1000ULL);
	add(&handoff, 0x80001000ULL, 0x2000ULL);
	add(&handoff, 0x80000800ULL, 0x100ULL);
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("bridged ranges: one kept", result == RISCV64_LOADER_OK && extras.reserved_count == 1U);
	check("bridged ranges: 0x80000000 + 0x4000",
	      extras.reserved[0].base == 0x80000000ULL && extras.reserved[0].size == 0x4000ULL);

	/* A count past the array is refused. */
	fill_v2(&handoff);
	handoff.reserved_count = 17;
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("count 17: refused", result == RISCV64_LOADER_BAD_COUNT);

	/* A range that wraps is refused. */
	fill_v2(&handoff);
	add(&handoff, 0xfffffffffffff000ULL, 0x2000ULL);
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("a wrapping range: refused", result == RISCV64_LOADER_BAD_RANGE);

	/* A loader that dropped ranges is refused, after what it held is kept. */
	fill_v2(&handoff);
	add(&handoff, 0x80000000ULL, 0x1000ULL);
	handoff.reserved_flags = ZBL6_RISCV64_RESERVED_OVERFLOW;
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("overflow flag: refused", result == RISCV64_LOADER_OVERFLOW && extras.reserved_count == 1U);

	/* Unknown flags are ignored. */
	fill_v2(&handoff);
	handoff.reserved_flags = 0x80000000U;
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("unknown flag: ignored", result == RISCV64_LOADER_OK);
}

/* Checks that more apart ranges than the HAL holds are refused. */
static void
test_too_many(void)
{
	struct zbl6_riscv64_handoff handoff;
	struct riscv64_fdt_info info;
	struct riscv64_loader_extras extras;
	unsigned index;
	int result;

	/* The handoff holds 16; the HAL joins none, so 16 fit and the HAL's limit is the handoff's. */
	fill_info(&info);
	fill_v2(&handoff);
	for (index = 0; index < 16U; index++)
		add(&handoff, 0x80000000ULL + (uint64_t)index * 0x10000ULL, 0x1000ULL);
	result = riscv64_loader_take(&handoff, &info, &extras);
	check("16 apart ranges: all kept", result == RISCV64_LOADER_OK && extras.reserved_count == 16U);
	check("RISCV64_LOADER_MAX_RESERVED holds the handoff's array", RISCV64_LOADER_MAX_RESERVED >= ZBL6_RISCV64_RESERVED_MAX);
}

/* Runs every check. */
int
main(void)
{
	/* The checks. */
	test_check();
	test_layouts();
	test_screens();
	test_ranges();
	test_too_many();

	/* Reports. */
	printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
