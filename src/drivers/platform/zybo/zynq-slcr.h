/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Zynq-7000's system level control registers as the kernel's drivers
 * reach them: the clock gates and dividers of the peripherals.
 */

#ifndef KERN_DRIVERS_PLATFORM_ZYBO_ZYNQ_SLCR_H
#define KERN_DRIVERS_PLATFORM_ZYBO_ZYNQ_SLCR_H

#include <stdint.h>

/*
 * The registers the drivers read and write, as offsets from the SLCR's
 * base: the I/O PLL, the peripherals' bus clock gates (APER_CLK_CTRL), and
 * the SD controllers' reference clock.
 *
 * Values referenced: Linux v6.12 drivers/clk/zynq/clkc.c (IO_PLL_CTRL
 * 0x108, ARM_PLL_CTRL 0x100, DDR_PLL_CTRL 0x104, APER_CLK_CTRL 0x12c,
 * SDIO_CLK_CTRL 0x150).
 */
#define DRV_ZYNQ_SLCR_ARM_PLL_CTRL	0x100U
#define DRV_ZYNQ_SLCR_DDR_PLL_CTRL	0x104U
#define DRV_ZYNQ_SLCR_IO_PLL_CTRL	0x108U
#define DRV_ZYNQ_SLCR_APER_CLK_CTRL	0x12cU
#define DRV_ZYNQ_SLCR_SDIO_CLK_CTRL	0x150U

int drv_zynq_slcr_init(void);
int drv_zynq_slcr_read(uint32_t offset, uint32_t *value);
int drv_zynq_slcr_modify(uint32_t offset, uint32_t mask, uint32_t value);
uint64_t drv_zynq_slcr_pll_hz(uint32_t control_offset);

#endif
