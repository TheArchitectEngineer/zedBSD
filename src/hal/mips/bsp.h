/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the MIPS HAL asks of its board: the loader's tables, the display
 * and its console.
 */

#ifndef KERN_HAL_MIPS_BSP_H
#define KERN_HAL_MIPS_BSP_H

#include <hal/types.h>

void
mips_board_boot(
	uint32_t argument_count,
	uint32_t argument_table,
	uint32_t environment_table,
	uint32_t low_memory_size);

void
mips_board_init(
	void);

void
mips_board_putc(
	int character);

void
mips_board_describe(
	void);

#endif
