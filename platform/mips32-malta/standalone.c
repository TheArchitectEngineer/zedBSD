/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the kernel provides to the HAL, while the mips32 image is the HAL
 * alone.
 *
 * Until the page tables are in place the image links no kernel object and
 * stops once its tick runs.  The HAL's console still reads the kernel's
 * console hook, which this file supplies; it is removed from the build
 * when the kernel is linked in.
 */

#include <hal/hal.h>

/*
 * The kernel's console, which the HAL's console writes to once it is set.
 * It stays NULL: no kernel runs to publish one.
 */
void (*kernel_putc)(int c);
