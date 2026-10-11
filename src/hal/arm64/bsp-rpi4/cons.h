/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Board-private framebuffer output ownership; serial output stays active. */
#ifndef KERN_HAL_ARM64_RPI4_CONS_H
#define KERN_HAL_ARM64_RPI4_CONS_H

void rpi4_cons_suspend_output(void);
void rpi4_cons_resume_output(void);

#endif
