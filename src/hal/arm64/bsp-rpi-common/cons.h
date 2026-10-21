/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The early console's hand-over of the screen: a display driver that takes
 * the hardware stops the console's framebuffer writes; serial output stays.
 */

#ifndef KERN_HAL_ARM64_BSP_RPI_COMMON_CONS_H
#define KERN_HAL_ARM64_BSP_RPI_COMMON_CONS_H

#include <hal/types.h>

uint32_t rpi_cons_output_begin(void);
void rpi_cons_output_end(uint32_t token);
void rpi_cons_suspend_output(void);
void rpi_cons_resume_output(void);
int rpi_cons_output_suspended(void);

#endif
