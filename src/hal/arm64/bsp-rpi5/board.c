/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the Raspberry Pi 5 needs of the HAL beyond the Pi 4's board support:
 * the peripherals above 4 GiB in the kernel's tables, and the firmware's
 * PSCI calls for reset and power-off.
 */

#include <hal/hal.h>
#include "../bsp.h"
#include "../defs.h"
#include "../asm.h"
#include "../watch.h"

/*
 * The PSCI functions the HAL calls, in the SMC32 numbering every PSCI
 * version keeps.
 *
 * Values referenced: Arm Power State Coordination Interface (DEN0022),
 * the function identifiers of SYSTEM_OFF and SYSTEM_RESET.
 */
#define PSCI_SYSTEM_OFF		0x84000008UL
#define PSCI_SYSTEM_RESET	0x84000009UL

/* The bytes of one HAL peripheral window: a page covers each register block. */
#define RPI5_DEVICE_WINDOW	0x1000UL

static void map_one(uint64_t physical, const char *name);
static void psci_call(uint64_t function);

/*
 * Maps the peripherals the HAL itself uses into the kernel's direct map.
 *
 * The Pi 5's peripherals are above 4 GiB, outside the gigabyte blocks of
 * RAM, so each one gets a device block of a level-2 table of its own.  The
 * kernel's tables are not live yet; a failure stops the boot, since the
 * console and the interrupt controller would be gone after the switch.
 */
void
rpi5_space_map_devices(
	void)
{
	const struct rpi4_fdt_info *info;

	/* The debug UART the early console writes to before the tree is read. */
	map_one(RPI5_DEBUG_UART_BASE, "debug UART");

	/* The always-on GPIO block that drives the activity LED. */
	map_one(RPI5_AON_GPIO_BASE, "always-on GPIO");

	/* The firmware mailbox the framebuffer and the drivers ask through. */
	info = rpi4_boot_info();
	if (info->mailbox_base != 0)
		map_one(info->mailbox_base, "mailbox");

	/* The GIC's distributor, which routes the shared interrupts. */
	if (info->gic_dist_base != 0)
		map_one(info->gic_dist_base, "GIC distributor");

	/* The GIC's CPU interface, which every interrupt is taken through. */
	if (info->gic_cpu_base != 0)
		map_one(info->gic_cpu_base, "GIC CPU interface");

	/* The SD card's controller, which the kernel reads the root from. */
	if (info->sdhci_base != 0)
		map_one(info->sdhci_base, "SD controller");

	/* The UART the tree names, in case it is not the fixed debug UART. */
	if (info->uart_base != 0)
		map_one(info->uart_base, "UART");
}

/*
 * Resets the board through the firmware.
 */
void
rpi5_psci_system_reset(
	void)
{
	/* Nothing interrupts the reset request. */
	arm64_irq_mask();

	/* Asks the secure firmware to reset the system; it does not return. */
	psci_call(PSCI_SYSTEM_RESET);

	/* A firmware that refused leaves the CPU halted. */
	arm64_watch_park();
	for (;;)
		arm64_wfi();
}

/*
 * Turns the board off through the firmware.
 */
void
rpi5_psci_system_off(
	void)
{
	/* Nothing interrupts the power-off request. */
	arm64_irq_mask();

	/* The other CPUs do not report this one as stalled while it waits. */
	arm64_watch_park();

	/* Asks the secure firmware to turn the system off; it does not return. */
	psci_call(PSCI_SYSTEM_OFF);

	/* A firmware that refused leaves the CPU halted. */
	for (;;)
		arm64_wfi();
}

/* Maps one peripheral window as device memory, or stops the boot. */
static void
map_one(
	uint64_t physical,
	const char *name)
{
	void *window;
	int error;

	/* Gives the window a Device-nGnRE block in the direct map. */
	error = hal_space_map_device((hal_physaddr_t)physical,
				     RPI5_DEVICE_WINDOW,
				     HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_DEVICE,
				     &window);
	if (error != HAL_OK) {
		hal_printf("RPI5 cannot map the %s at %llx (%d)\n",
			   name,
			   (unsigned long long)physical,
			   error);
		HAL_FATAL("Pi 5 peripheral mapping failed");
	}
}

/* Makes one PSCI call with no arguments through the secure monitor. */
static void
psci_call(
	uint64_t function)
{
	register uint64_t x0 __asm__("x0");

	/* The function identifier goes in x0; the SMC calling convention may clobber x1 to x17. */
	x0 = function;
	__asm__ volatile("smc #0"
			 : "+r"(x0)
			 :
			 : "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
			   "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
			   "memory");
}
