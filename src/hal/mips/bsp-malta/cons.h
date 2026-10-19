/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The malta board's early text console on the framebuffer.
 */

#ifndef KERN_HAL_MIPS_BSP_MALTA_CONS_H
#define KERN_HAL_MIPS_BSP_MALTA_CONS_H

void
malta_cons_putc(
	int character);

void
malta_cons_attach(
	void);

#endif
