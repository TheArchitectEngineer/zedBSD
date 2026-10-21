/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The firmware's framebuffer, which the early console and the stall watch
 * paint text cells on.
 */

#ifndef KERN_HAL_ARM64_BSP_RPI_COMMON_FRAMEBUFFER_H
#define KERN_HAL_ARM64_BSP_RPI_COMMON_FRAMEBUFFER_H

#include <hal/types.h>

int rpi_framebuffer_init(uintptr_t mailbox_phys);
int rpi_framebuffer_ready(void);
void rpi_framebuffer_test_pattern(void);
void rpi_framebuffer_describe(void);
void rpi_framebuffer_cell(unsigned row, unsigned column, int character, uint8_t attribute);
void rpi_framebuffer_cursor(unsigned row, unsigned column, int visible);

#endif
