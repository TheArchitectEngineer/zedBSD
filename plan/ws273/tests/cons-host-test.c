/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host check of the MIPS console's grid (src/hal/mips/cons.c): the
 * bring-up's summary breaks between words at the grid's width, stays one
 * line on the malta's 80 columns, and a display of another size starts
 * from a blank grid that its cells are drawn from.  The cache's lines
 * name only conversions the HAL's hal_printf() has (it has no %d, which
 * printed alias=? and forced=?).  Run from the top of the tree.
 *
 * cc -std=c11 -Wall -Wextra -Werror -m32 -DHAL_ARCH_MIPS32 -Iinclude -Isrc \
 *    plan/ws273/tests/cons-host-test.c -o <out> && <out>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KERN_HAL_MIPS_SPIN_H

#include <hal/hal.h>
#include "../../../src/hal/mips/asm.h"

static bool mips_spin_lock(volatile unsigned *lock);
static void mips_spin_unlock(volatile unsigned *lock, bool enabled);

#include "../../../src/hal/mips/cons.c"

/* What hal_putc() printed, and the cells the display drew. */
static char printed[512];
static unsigned printed_length;
static unsigned cells_drawn;
static unsigned cell_beyond;
static int failures;

/* Takes the lock: the host runs one thread. */
static bool
mips_spin_lock(
	volatile unsigned *lock)
{
	*lock = 1;
	return false;
}

/* Gives the lock back. */
static void
mips_spin_unlock(
	volatile unsigned *lock,
	bool enabled)
{
	UNUSED_PARAMETER(enabled);
	*lock = 0;
}

/* Keeps what the HAL prints, and writes it to the grid as the board would. */
void
hal_putc(
	int character)
{
	if (printed_length + 1U < sizeof(printed))
		printed[printed_length++] = (char)character;
	printed[printed_length] = '\0';
	mips_cons_putc(character);
}

/* Counts each cell drawn and any outside a 40 by 15 grid. */
static void
draw_cell(
	unsigned row,
	unsigned column,
	int character,
	uint8_t attribute)
{
	UNUSED_PARAMETER(character);
	UNUSED_PARAMETER(attribute);
	cells_drawn++;
	if (row >= 15U || column >= 40U)
		cell_beyond++;
}

/* Draws no cursor. */
static void
draw_cursor(
	unsigned row,
	unsigned column)
{
	UNUSED_PARAMETER(row);
	UNUSED_PARAMETER(column);
}

/* Records a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Checks that each conversion in a file's formats is one hal_printf() has. */
static void
check_conversions(
	const char *path)
{
	char line[512];
	const char *at;
	FILE *file;

	file = fopen(path, "r");
	check(file != NULL, path);
	if (file == NULL)
		return;
	while (fgets(line, sizeof(line), file) != NULL) {
		/* Only the formats: the lines that call hal_printf(). */
		if (strstr(line, "hal_printf(") == NULL)
			continue;
		for (at = strchr(line, '%'); at != NULL; at = strchr(at + 1, '%')) {
			/* Steps past the width and the length to the conversion. */
			at++;
			while (*at >= '0' && *at <= '9')
				at++;
			while (*at == 'l')
				at++;
			if (strchr("csuxp%", *at) == NULL) {
				printf("FAIL %s: %s", path, line);
				failures++;
			}
			if (*at == '\0')
				break;
		}
	}
	fclose(file);
}

int
main(void)
{
	static const struct mips_cons_display narrow = { 40U, 15U, draw_cell, draw_cursor };
	const char *pieces[6] = { "MIPS32 HAL READY", "DISPLAY", "LCDC", "PAGING PASS", "TIMER", "TICK" };

	/* The malta's 80 columns: one line, as before. */
	printed_length = 0;
	mips_cons_print_words(pieces, 6);
	check(strcmp(printed, "MIPS32 HAL READY DISPLAY LCDC PAGING PASS TIMER TICK\n") == 0, "one line at 80 columns");

	/* A 40 by 15 display: a blank grid, drawn within its size. */
	mips_cons_attach(&narrow);
	check(mips_cons_columns() == 40U, "columns of the display");
	check(cells_drawn == 40U * 15U, "every cell drawn once");
	check(cell_beyond == 0, "no cell outside the display");
	check(cons_row == 0 && cons_column == 0, "the cursor home");

	/* The same line breaks between words at 40 columns. */
	printed_length = 0;
	mips_cons_print_words(pieces, 6);
	check(strcmp(printed, "MIPS32 HAL READY DISPLAY LCDC PAGING\nPASS TIMER TICK\n") == 0, "broken at 40 columns");
	check(cell_beyond == 0, "no cell outside the display after printing");

	/* A word longer than the width stands on a line of its own. */
	pieces[0] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
	printed_length = 0;
	mips_cons_print_words(pieces, 2);
	check(strcmp(printed, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\nDISPLAY\n") == 0, "a long word");

	/* The cache's lines print their numbers, not a question mark. */
	check_conversions(getenv("CACHE_C") != NULL ? getenv("CACHE_C") : "src/hal/mips/cache.c");

	if (failures != 0)
		return 1;
	printf("PASS cons\n");
	return 0;
}
