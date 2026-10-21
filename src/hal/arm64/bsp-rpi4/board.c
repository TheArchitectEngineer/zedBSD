/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the Raspberry Pi 4 gives the arm64 HAL beyond the Raspberry Pi
 * common support: its peripherals in the kernel's tables, its compositor
 * for the stall watch, and the watchdog reset.
 */

#include <hal/hal.h>
#include "../asm.h"
#include "../bsp.h"
#include "../defs.h"
#include "../space.h"
#include "../watch.h"
#include "board.h"

/*
 * The power manager's watchdog: the reset control and the watchdog
 * registers, the password every write carries, and the values that start
 * a full reset after ten ticks.
 */
#define PM_RSTC 0x1cU
#define PM_WDOG 0x24U
#define PM_PASSWORD 0x5a000000U
#define PM_WDOG_TICKS 0x0000000aU
#define PM_RSTC_FULL_RESET 0x00000020U

/* The BCM2711 compositor (HVS): its registers and display-list memory. */
#define WATCH_HVS_BASE 0xfe400000U
#define WATCH_HVS_WINDOW 0x8000U

/*
 * Maps the peripherals the HAL itself uses into the kernel's tables: each
 * gets a 2 MiB device block in the 3-4 GiB window.
 */
void
arm64_bsp_map_devices(
	void)
{
	const struct rpi_fdt_info *info;

	/* The device tree names where each peripheral is. */
	info = rpi_boot_info();

	/* The UART, the mailbox, the GIC's two parts and the SD controller. */
	arm64_space_map_device_block(info->uart_base);
	arm64_space_map_device_block(info->mailbox_base);
	arm64_space_map_device_block(info->gic_dist_base);
	arm64_space_map_device_block(info->gic_cpu_base);
	arm64_space_map_device_block(info->sdhci_base);
}

/*
 * Maps the compositor's registers and display-list memory, uncached, for
 * the stall watch's report.
 */
volatile uint32_t *
arm64_bsp_watch_compositor(
	void)
{
	void *window;
	int error;

	/* A window the table cannot hold leaves the report on the framebuffer. */
	window = NULL;
	error = hal_space_map_device(WATCH_HVS_BASE,
				     WATCH_HVS_WINDOW,
				     HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_NOCACHE | HAL_SPACE_DEVICE,
				     &window);
	if (error != HAL_OK)
		return NULL;

	/* Succeeded: the compositor's registers. */
	return (volatile uint32_t *)window;
}

/*
 * Resets the board through the power manager's watchdog.
 */
void
hal_reset(
	void)
{
	volatile uint8_t *pm;

	/* Nothing interrupts the reset. */
	arm64_irq_mask();

	/* Starts the watchdog and asks for a full reset when it expires. */
	pm = (volatile uint8_t *)(ARM64_DIRECT_BASE + RPI4_PM_BASE);
	hal_mmio_write32(pm + PM_WDOG, PM_PASSWORD | PM_WDOG_TICKS);
	hal_mmio_write32(pm + PM_RSTC, PM_PASSWORD | PM_RSTC_FULL_RESET);

	/* Waits for the reset. */
	for (;;)
		arm64_wfi();
}

/*
 * Stops the calling CPU for good when the board is to be powered off.
 *
 * The board has no power switch the kernel can reach, so the CPU halts
 * with interrupts off; the stall watch is told that its silence is meant.
 */
void
hal_poweroff(
	void)
{
	/* Nothing wakes the CPU into the kernel again. */
	arm64_irq_mask();

	/* The other CPUs do not report this one as stalled. */
	arm64_watch_park();

	/* Halts for ever. */
	for (;;)
		arm64_wfi();
}
