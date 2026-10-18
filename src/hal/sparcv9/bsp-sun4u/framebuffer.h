/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sun4u board's framebuffer: the emulator's standard VGA in a linear
 * mode, drawn as a grid of text cells.
 */

#ifndef KERN_HAL_SPARCV9_BSP_SUN4U_FRAMEBUFFER_H
#define KERN_HAL_SPARCV9_BSP_SUN4U_FRAMEBUFFER_H

#include <hal/types.h>

/* The text grid the screen holds. */
#define SUN4U_CONSOLE_COLUMNS	80U
#define SUN4U_CONSOLE_ROWS	25U

/*
 * Sets the display to its linear mode and clears it.
 *
 * Returns HAL_OK once the screen shows, or HAL_ERR_UNSUPPORTED when the
 * loader found no display.
 */
int
sun4u_framebuffer_init(
	uint64_t pci_memory_base,
	uint32_t framebuffer,
	uint32_t framebuffer_size,
	uint32_t registers);

/*
 * Draws one text cell: a glyph in the attribute's colours.
 */
void
sun4u_framebuffer_cell(
	unsigned row,
	unsigned column,
	int character,
	uint8_t attribute);

/*
 * Draws the cursor in one cell.
 */
void
sun4u_framebuffer_cursor(
	unsigned row,
	unsigned column);

/*
 * Reports whether the screen shows.
 */
int
sun4u_framebuffer_ready(
	void);

#endif
