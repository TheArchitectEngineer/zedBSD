/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The board's parts the processor's code calls.
 */

#ifndef KERN_HAL_PPC64_PSERIES_BSP_H
#define KERN_HAL_PPC64_PSERIES_BSP_H

#include <hal/hal.h>

/*
 * What the firmware reported, read once before the HAL takes the machine:
 * the RAM, the time base's rate, the hashed table's size, the display and
 * the command line.
 */
struct pseries_board_info {
	uint64_t memory_base;
	uint64_t memory_size;
	uint64_t ramdisk_base;
	uint64_t ramdisk_size;
	uint32_t timebase_frequency;
	uint32_t htab_shift;
	uint64_t framebuffer_address;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint32_t framebuffer_linebytes;
	uint32_t framebuffer_depth;
};

const struct pseries_board_info *pseries_board(void);
void pseries_firmware_read(void);
const struct kern_boot_handoff *pseries_kernel_handoff(void);

void pseries_clock_init(void);
void pseries_clock_interrupt(void);
bool pseries_clock_ticked(void);

void pseries_take_external(void);

#endif
