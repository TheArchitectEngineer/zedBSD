/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the MIPS HAL asks of its board: the loader's tables, the display
 * and its console, the rate of Count and the external interrupts.
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

bool
mips_board_display_ready(
	void);

uint32_t
mips_board_count_frequency(
	void);

uint32_t
mips_board_memory_size(
	void);

uint32_t
mips_board_memory_total(
	void);

void
mips_board_ramdisk(
	uint32_t *physical,
	uint32_t *size);

const void *
mips_board_kernel_handoff(
	void);

void
mips_board_irq_init(
	void);

int
mips_board_irq_register(
	unsigned line,
	bool level,
	void (*handler)(void *),
	void *argument);

void
mips_board_interrupt(
	void);

#endif
