/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the HAL reads from a Raspberry Pi's device tree.
 *
 * Each board has its own parser (bsp-rpi4/fdt.c, bsp-rpi5/fdt.c), since
 * the boards name their peripherals differently, and both fill this record.
 */

#ifndef KERN_HAL_ARM64_BSP_RPI_COMMON_FDT_H
#define KERN_HAL_ARM64_BSP_RPI_COMMON_FDT_H

#include <hal/types.h>

/* The most RAM ranges, reserved ranges and CPUs the record holds. */
#define RPI_FDT_MAX_MEMORY 8
#define RPI_FDT_MAX_RESERVED 24
#define RPI_FDT_MAX_CPUS 8

/*
 * One CPU the device tree names: its affinity (the cpu node's reg, which
 * is MPIDR_EL1's affinity fields) and how the firmware holds it back.
 */
struct rpi_fdt_cpu {
	uint64_t mpidr;

	/* Where a spin-table CPU waits for its entry address; 0 when none was given. */
	uint64_t release_addr;

	/* Nonzero when enable-method names "spin-table". */
	uint32_t spin_table;
};

/*
 * One range of physical memory: RAM a memory node lists, or memory the
 * firmware keeps for itself.
 */
struct rpi_fdt_range {
	uint64_t base;
	uint64_t size;
};

/*
 * What the boot takes from the device tree.
 *
 * The boot CPU fills one at the start of the boot and keeps a copy for
 * the rest of the HAL (boot.c); nothing changes it afterwards except the
 * framebuffer's reservation.
 */
struct rpi_fdt_info {
	/* The blob's size from its header. */
	uint32_t totalsize;

	/* Nonzero when a node names the board the parser is built for. */
	int compatible_board;

	/* The RAM and the reserved ranges, each table filled from its start. */
	unsigned memory_count;
	unsigned reserved_count;
	struct rpi_fdt_range memory[RPI_FDT_MAX_MEMORY];
	struct rpi_fdt_range reserved[RPI_FDT_MAX_RESERVED];

	/* The CPU addresses (and interrupts) of the peripherals the HAL uses; 0 when absent. */
	uint64_t uart_base;
	uint32_t uart_irq;
	uint64_t mailbox_base;
	uint64_t gic_dist_base;
	uint64_t gic_cpu_base;
	uint64_t sdhci_base;
	uint32_t sdhci_irq;

	/*
	 * Where /chosen/bootargs is in the blob (the firmware's command line,
	 * cmdline.txt), and its length with the terminator.  Zero when absent.
	 */
	uint32_t bootargs_offset;
	uint32_t bootargs_length;

	/* The CPUs of /cpus in the order the tree lists them. */
	unsigned cpu_count;
	struct rpi_fdt_cpu cpus[RPI_FDT_MAX_CPUS];
};

int rpi_fdt_parse(const void *blob, size_t available, struct rpi_fdt_info *info);
const char *rpi_fdt_error(int error);

#endif
