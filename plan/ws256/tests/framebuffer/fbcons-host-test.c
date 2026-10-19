/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the riscv64 early console's drawing
 * (src/hal/riscv64/fbcons.c) on a fake framebuffer: the screens it
 * refuses, the clear, a cell's glyph and colours in both pixel orders at
 * the centred grid, the cursor, and that every flushed range covers the
 * pixels drawn and nothing outside the drawn rows.
 *
 *   fbcons-host-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hal/hal.h>
#include <kern/boot.h>
#include "fbcons.h"
#include "font.h"

/* The fake screen: 1024 by 768 pixels with rows of 1040 pixels. */
#define TEST_WIDTH	1024U
#define TEST_HEIGHT	768U
#define TEST_PITCH	(1040U * 4U)
#define TEST_BYTES	((size_t)TEST_PITCH * TEST_HEIGHT)

/* The bytes the screen starts filled with, so untouched bytes show. */
#define TEST_FILL	0xa5U

/* The most flushes one step records. */
#define TEST_MAX_FLUSHES	200000U

/* One recorded flush: an offset into the fake screen and a length. */
struct flush_record {
	size_t offset;
	size_t length;
};

/* How many checks failed. */
static int failures;

/* The fake screen, and a copy taken before a step for comparison. */
static uint8_t *screen_bytes;
static uint8_t *before_bytes;

/* The flushes recorded since the last reset. */
static struct flush_record *flushes;
static size_t flush_count;

/* How many times the console was asked to redraw. */
static int redraw_count;

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

/* The console's redraw, which the attach calls; it only counts here. */
void
riscv64_cons_redraw(
	void)
{
	/* Counts the call. */
	redraw_count++;
}

/* Records a flush the drawing asked for. */
static void
record_flush(
	uintptr_t start,
	size_t length)
{
	/* Records the range relative to the screen, refusing one outside it. */
	if (start < (uintptr_t)screen_bytes || start + length > (uintptr_t)screen_bytes + TEST_BYTES) {
		check("flush inside the screen", 0);
		return;
	}
	if (flush_count < TEST_MAX_FLUSHES) {
		flushes[flush_count].offset = start - (uintptr_t)screen_bytes;
		flushes[flush_count].length = length;
	}
	flush_count++;
}

/* Reports whether a byte offset lies inside a recorded flush. */
static int
flushed(
	size_t offset)
{
	size_t index;

	/* Looks through every recorded flush. */
	for (index = 0; index < flush_count && index < TEST_MAX_FLUSHES; index++) {
		if (offset >= flushes[index].offset && offset < flushes[index].offset + flushes[index].length)
			return 1;
	}
	return 0;
}

/* Reads one pixel of the fake screen. */
static uint32_t
pixel(
	uint32_t x,
	uint32_t y)
{
	uint32_t value;

	/* Copies the word out, little-endian as the host is. */
	memcpy(&value, screen_bytes + (size_t)y * TEST_PITCH + (size_t)x * 4U, sizeof(value));
	return value;
}

/* Describes the fake screen with a format and a flush. */
static struct riscv64_fbcons_screen
fake_screen(
	uint32_t format,
	void (*flush)(uintptr_t start, size_t length))
{
	struct riscv64_fbcons_screen screen;

	/* The whole fake screen. */
	screen.address = (uintptr_t)screen_bytes;
	screen.width = TEST_WIDTH;
	screen.height = TEST_HEIGHT;
	screen.pitch = TEST_PITCH;
	screen.format = format;
	screen.flush = flush;
	return screen;
}

/* Checks the screens the console refuses. */
static void
test_refusals(void)
{
	struct riscv64_fbcons_screen screen;
	int error;

	/* No description, no address. */
	error = riscv64_fbcons_attach(NULL);
	check("refuses no description", error == HAL_ERR_INVALID);
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, NULL);
	screen.address = 0;
	error = riscv64_fbcons_attach(&screen);
	check("refuses no address", error == HAL_ERR_INVALID);

	/* An unknown format. */
	screen = fake_screen(3U, NULL);
	error = riscv64_fbcons_attach(&screen);
	check("refuses format 3", error == HAL_ERR_INVALID);
	screen = fake_screen(0U, NULL);
	error = riscv64_fbcons_attach(&screen);
	check("refuses format 0", error == HAL_ERR_INVALID);

	/* A screen smaller than the grid. */
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, NULL);
	screen.width = 639U;
	error = riscv64_fbcons_attach(&screen);
	check("refuses 639 wide", error == HAL_ERR_INVALID);
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, NULL);
	screen.height = 399U;
	error = riscv64_fbcons_attach(&screen);
	check("refuses 399 high", error == HAL_ERR_INVALID);

	/* Rows shorter than the width, or not whole pixels. */
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, NULL);
	screen.pitch = TEST_WIDTH * 4U - 4U;
	error = riscv64_fbcons_attach(&screen);
	check("refuses a pitch shorter than the width", error == HAL_ERR_INVALID);
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, NULL);
	screen.pitch = TEST_PITCH + 2U;
	error = riscv64_fbcons_attach(&screen);
	check("refuses a pitch of part pixels", error == HAL_ERR_INVALID);

	/* None of them touched the screen, asked for a redraw or made the console ready. */
	check("refusals leave the screen", memcmp(screen_bytes, before_bytes, TEST_BYTES) == 0);
	check("refusals do not redraw", redraw_count == 0);
	check("refusals leave the console unready", riscv64_fbcons_ready() == 0);
}

/* Checks the attach: the clear of the visible pixels and its flushes. */
static void
test_attach(void)
{
	struct riscv64_fbcons_screen screen;
	size_t offset;
	uint32_t x;
	uint32_t y;
	int cleared;
	int padding_kept;
	int covered;
	int error;

	/* Attaches a blue-first screen that is written through. */
	flush_count = 0;
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, record_flush);
	error = riscv64_fbcons_attach(&screen);
	check("attaches 1024x768 pitch 4160", error == HAL_OK);
	check("attach redraws once", redraw_count == 1);
	check("attach makes the console ready", riscv64_fbcons_ready() == 1);

	/* Every visible pixel is black, every padding byte untouched. */
	cleared = 1;
	padding_kept = 1;
	for (y = 0; y < TEST_HEIGHT; y++) {
		for (x = 0; x < TEST_WIDTH; x++) {
			if (pixel(x, y) != 0)
				cleared = 0;
		}
		for (offset = (size_t)TEST_WIDTH * 4U; offset < TEST_PITCH; offset++) {
			if (screen_bytes[(size_t)y * TEST_PITCH + offset] != TEST_FILL)
				padding_kept = 0;
		}
	}
	check("clear blackens the visible pixels", cleared);
	check("clear leaves the row padding", padding_kept);

	/* One flush per row, each the row's visible bytes. */
	covered = flush_count == TEST_HEIGHT;
	for (y = 0; covered && y < TEST_HEIGHT; y++) {
		if (flushes[y].offset != (size_t)y * TEST_PITCH || flushes[y].length != (size_t)TEST_WIDTH * 4U)
			covered = 0;
	}
	check("clear flushes each row's visible bytes once", covered);
}

/* Checks a cell's glyph, colours, place and flushes on the attached screen. */
static void
test_cell(
	uint32_t format,
	uint32_t blue_value)
{
	struct riscv64_fbcons_screen screen;
	const uint8_t *glyph;
	uint32_t origin_x;
	uint32_t origin_y;
	uint32_t cell_x;
	uint32_t cell_y;
	uint32_t expected;
	uint32_t x;
	uint32_t y;
	size_t offset;
	int glyph_right;
	int outside_kept;
	int drawn_flushed;
	int flush_inside;
	size_t index;
	int error;
	char name[96];

	/* Attaches the screen anew in the format, then draws 'A' white on blue at row 3, column 5. */
	screen = fake_screen(format, record_flush);
	error = riscv64_fbcons_attach(&screen);
	check("re-attaches", error == HAL_OK);
	memcpy(before_bytes, screen_bytes, TEST_BYTES);
	flush_count = 0;
	riscv64_fbcons_cell(3U, 5U, 'A', 0x1fU);
	check("a cell's drawing does not flush by itself", flush_count == 0);
	riscv64_fbcons_flush_cell(3U, 5U);

	/* The grid is centred: (1024 - 640) / 2 and (768 - 400) / 2. */
	origin_x = (TEST_WIDTH - 640U) / 2U;
	origin_y = (TEST_HEIGHT - 400U) / 2U;
	cell_x = origin_x + 5U * 8U;
	cell_y = origin_y + 3U * 16U;

	/* Each glyph bit is white ink, each clear bit blue background. */
	glyph = &riscv64_font8x16['A' * RISCV64_FONT_HEIGHT];
	glyph_right = 1;
	for (y = 0; y < 16U; y++) {
		for (x = 0; x < 8U; x++) {
			expected = blue_value;
			if ((glyph[y] & (0x80U >> x)) != 0)
				expected = 0xffffffU;
			if (pixel(cell_x + x, cell_y + y) != expected)
				glyph_right = 0;
		}
	}
	snprintf(name, sizeof(name), "format %u: glyph and colours at the centred cell", format);
	check(name, glyph_right);

	/* Nothing outside the cell changed, and every changed byte was flushed. */
	outside_kept = 1;
	drawn_flushed = 1;
	for (offset = 0; offset < TEST_BYTES; offset++) {
		y = (uint32_t)(offset / TEST_PITCH);
		x = (uint32_t)((offset % TEST_PITCH) / 4U);
		if (y >= cell_y && y < cell_y + 16U && x >= cell_x && x < cell_x + 8U) {
			if (!flushed(offset))
				drawn_flushed = 0;
			continue;
		}
		if (screen_bytes[offset] != before_bytes[offset])
			outside_kept = 0;
	}
	snprintf(name, sizeof(name), "format %u: nothing outside the cell changed", format);
	check(name, outside_kept);
	snprintf(name, sizeof(name), "format %u: every drawn byte was flushed", format);
	check(name, drawn_flushed);

	/* The cell's flushes are its 16 row runs of 32 bytes, nothing more. */
	flush_inside = flush_count == 16U;
	for (index = 0; flush_inside && index < flush_count; index++) {
		if (flushes[index].offset != (size_t)(cell_y + index) * TEST_PITCH + (size_t)cell_x * 4U ||
		    flushes[index].length != 32U)
			flush_inside = 0;
	}
	snprintf(name, sizeof(name), "format %u: the cell flushes 16 runs of 32 bytes", format);
	check(name, flush_inside);
}

/* Checks the cursor and the grid's flush. */
static void
test_cursor_and_grid(void)
{
	uint32_t origin_x;
	uint32_t origin_y;
	uint32_t x;
	uint32_t y;
	int white;
	int grid_right;
	size_t index;

	/* The cursor whitens the cell's two bottom rows. */
	origin_x = (TEST_WIDTH - 640U) / 2U;
	origin_y = (TEST_HEIGHT - 400U) / 2U;
	riscv64_fbcons_cursor(24U, 79U);
	white = 1;
	for (y = 14U; y < 16U; y++) {
		for (x = 0; x < 8U; x++) {
			if (pixel(origin_x + 79U * 8U + x, origin_y + 24U * 16U + y) != 0xffffffU)
				white = 0;
		}
	}
	check("cursor whitens rows 14 and 15 of the last cell", white);

	/* Off the grid nothing is drawn. */
	memcpy(before_bytes, screen_bytes, TEST_BYTES);
	riscv64_fbcons_cell(25U, 0U, 'X', 0x07U);
	riscv64_fbcons_cell(0U, 80U, 'X', 0x07U);
	riscv64_fbcons_cursor(25U, 80U);
	check("off the grid nothing is drawn", memcmp(screen_bytes, before_bytes, TEST_BYTES) == 0);

	/* The grid's flush is its 400 rows of 2560 bytes from the origin. */
	flush_count = 0;
	riscv64_fbcons_flush_grid();
	grid_right = flush_count == 400U;
	for (index = 0; grid_right && index < flush_count; index++) {
		if (flushes[index].offset != (size_t)(origin_y + index) * TEST_PITCH + (size_t)origin_x * 4U ||
		    flushes[index].length != 2560U)
			grid_right = 0;
	}
	check("grid flush is 400 runs of 2560 bytes at the origin", grid_right);
}

/* Checks a screen without a flush and the exact 640x400 fit. */
static void
test_unflushed_exact(void)
{
	struct riscv64_fbcons_screen screen;
	uint32_t corner;
	int error;

	/* A 640x400 screen with tight rows puts the grid at the corner. */
	screen = fake_screen(KERN_BOOT_FRAMEBUFFER_BGRX8888, NULL);
	screen.width = 640U;
	screen.height = 400U;
	screen.pitch = 2560U;
	error = riscv64_fbcons_attach(&screen);
	check("attaches 640x400 without a flush", error == HAL_OK);
	flush_count = 0;
	riscv64_fbcons_cell(0U, 0U, ' ', 0x70U);
	riscv64_fbcons_flush_cell(0U, 0U);
	riscv64_fbcons_flush_grid();
	check("no flush without a flush call", flush_count == 0);
	memcpy(&corner, screen_bytes, sizeof(corner));
	check("cell 0,0 is at the corner (grey background)", corner == 0xaaaaaaU);
}

/* Runs every check. */
int
main(void)
{
	/* Allocates the fake screen, its copy and the flush record. */
	screen_bytes = malloc(TEST_BYTES);
	before_bytes = malloc(TEST_BYTES);
	flushes = calloc(TEST_MAX_FLUSHES, sizeof(*flushes));
	if (screen_bytes == NULL || before_bytes == NULL || flushes == NULL)
		return 2;
	memset(screen_bytes, TEST_FILL, TEST_BYTES);
	memcpy(before_bytes, screen_bytes, TEST_BYTES);

	/* The checks, in the order the console meets them. */
	test_refusals();
	test_attach();
	test_cell(KERN_BOOT_FRAMEBUFFER_BGRX8888, 0x0000aaU);
	test_cell(KERN_BOOT_FRAMEBUFFER_RGBX8888, 0x00aa0000U);
	test_cursor_and_grid();
	test_unflushed_exact();

	/* Reports. */
	printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
	free(flushes);
	free(before_bytes);
	free(screen_bytes);
	return failures == 0 ? 0 : 1;
}
