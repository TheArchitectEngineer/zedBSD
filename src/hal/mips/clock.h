/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS scheduling tick and counter, from coprocessor 0's Count and
 * Compare.
 */

#ifndef KERN_HAL_MIPS_CLOCK_H
#define KERN_HAL_MIPS_CLOCK_H

#include <hal/types.h>

void
mips_clock_init(
	void);

void
mips_clock_interrupt(
	void);

uint64_t
mips_clock_read_counter(
	void);

uint64_t
mips_clock_frequency(
	void);

uint32_t
mips_clock_ticks(
	void);

#endif
