/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The board's parts the processor's code calls.
 */

#ifndef KERN_HAL_PPC32_MAC99_BSP_H
#define KERN_HAL_PPC32_MAC99_BSP_H

#include <hal/hal.h>

/*
 * What the firmware reported, read once before the HAL takes the machine:
 * the RAM, the time base's rate, the display and the command line.
 */
struct mac99_board_info {
	uint32_t memory_base;
	uint32_t memory_size;
	uint32_t ramdisk_base;
	uint32_t ramdisk_size;
	uint32_t timebase_frequency;
	uint32_t framebuffer_address;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint32_t framebuffer_linebytes;
	uint32_t framebuffer_depth;
};

const struct mac99_board_info *mac99_board(void);
void mac99_firmware_read(void);
const struct kern_boot_handoff *mac99_kernel_handoff(void);

void mac99_clock_init(void);
void mac99_clock_interrupt(void);
bool mac99_clock_ticked(void);

void mac99_take_external(void);

#endif
