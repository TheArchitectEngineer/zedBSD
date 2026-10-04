/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The BCM2711 graphics driver's entry point for the Raspberry Pi 4 platform.
 *
 * The VideoCore VI of the BCM2711 has two independent parts: the display
 * path (the compositor that reads display lists, the timing generators and
 * the HDMI encoders) and the V3D 4.2 render engine.  The platform calls the
 * driver once, while it discovers the board's devices.
 */

#ifndef KERN_DRIVERS_GPU_BCM2711_GPU_H
#define KERN_DRIVERS_GPU_BCM2711_GPU_H

#include <stdint.h>

/*
 * Finds the display path and the V3D engine in the device tree and prepares
 * them.
 *
 * fdt_phys is the physical address of the device tree the firmware handed
 * over.  A board or emulator without the hardware, and a boot that turned the
 * driver off, leave the machine without it; the boot goes on either way.
 * Returns 0 when at least one part was prepared, ENODEV when none was.
 */
int drv_bcm2711_gpu_attach(uint64_t fdt_phys);

#endif
