/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The StarFive VisionFive 2 board (JH7110).
 *
 * The board has no display the hardware layer sets up yet, so the early
 * console is the firmware's alone.  The shared L2 cache controller is the
 * one board device the hardware layer drives: cache maintenance writes
 * each line's physical address to its flush register, which writes the
 * line back and discards it.
 */

#include <hal/hal.h>
#include "../bsp.h"
#include "../defs.h"
#include "../space.h"

/* The offset of the cache controller's register that flushes one line by its physical address. */
#define VF2_CCACHE_FLUSH64		0x200U

/* The register's end: the controller's range must reach past it. */
#define VF2_CCACHE_FLUSH64_END		(VF2_CCACHE_FLUSH64 + 8U)

/* The cache controller's line size in bytes. */
#define VF2_CCACHE_LINE_SIZE		64U

/*
 * The cache controller's flush register in the device window, or NULL
 * when the device tree names no controller.  Set once by
 * riscv64_board_init() on the boot hart before any other hart runs, and
 * read only afterwards.
 */
static volatile uint64_t *board_flush64;

/*
 * The controller's physical base, kept for the boot log; zero when there
 * is no controller.  Set with board_flush64.
 */
static uint64_t board_ccache_base;

static void flush_range(uintptr_t address, size_t size);
static int kernel_to_physical(uintptr_t address, uint64_t *physical);
static void fence_all(void);

/*
 * Finds the board's cache controller.
 *
 * A tree without one, or with a range too small for the flush register,
 * leaves cache maintenance doing nothing: the board's devices are then
 * taken to snoop the caches.
 */
void
riscv64_board_init(
	const struct riscv64_fdt_info *info)
{
	/* Leaves maintenance off for a tree without the controller. */
	if (info->ccache_size < VF2_CCACHE_FLUSH64_END)
		return;

	/* Refuses a controller the device window does not reach. */
	if (info->ccache_base >= RISCV64_DEVICE_LIMIT - VF2_CCACHE_FLUSH64_END)
		return;

	/* Reaches the flush register through the device window. */
	board_ccache_base = info->ccache_base;
	board_flush64 = (volatile uint64_t *)(uintptr_t)(RISCV64_DEVICE_BASE + info->ccache_base + VF2_CCACHE_FLUSH64);
}

/*
 * Writes one early-console character to the board's screen; the board has none yet.
 */
void
riscv64_board_putc(
	int character)
{
	UNUSED_PARAMETER(character);
}

/*
 * Writes the board's cache controller to the boot log.
 */
void
riscv64_board_describe(
	void)
{
	/* Names the board and its cache controller, or says there is none. */
	if (board_flush64 != NULL) {
		hal_printf("RISCV64 BOARD visionfive2 ccache=%lx\n", board_ccache_base);
		return;
	}
	hal_printf("RISCV64 BOARD visionfive2 ccache unavailable; no cache maintenance\n");
}

/*
 * Writes cached lines of a range back to memory.
 *
 * The controller's flush also discards the lines, which is harmless for a
 * clean.
 */
void
hal_dcache_clean_range(
	uintptr_t address,
	size_t size)
{
	/* Writes back and discards every line of the range. */
	flush_range(address, size);
}

/*
 * Discards cached lines of a range.
 *
 * The controller writes a dirty line back before it discards it, so bytes
 * sharing an edge line with the range are kept.
 */
void
hal_dcache_invalidate_range(
	uintptr_t address,
	size_t size)
{
	/* Writes back and discards every line of the range. */
	flush_range(address, size);
}

/*
 * Writes back and discards cached lines of a range.
 */
void
hal_dcache_clean_invalidate_range(
	uintptr_t address,
	size_t size)
{
	/* Writes back and discards every line of the range. */
	flush_range(address, size);
}

/*
 * Flushes every cache-controller line a kernel range touches.
 *
 * Procedure referenced: OpenBSD sys/arch/riscv64/dev/sfcc.c
 * (sfcc_cache_wbinv_range): a full fence, then each line's physical
 * address written to the flush register with a full fence after each.
 */
static void
flush_range(
	uintptr_t address,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint64_t physical;
	int error;

	/* A board without the controller has nothing to flush. */
	if (board_flush64 == NULL || size == 0)
		return;

	/* Refuses a range that wraps the address space. */
	if (address > UINTPTR_MAX - size)
		return;

	/*
	 * Values referenced: Linux v6.12 arch/riscv/boot/dts/starfive/jh7110.dtsi
	 * (the cache-controller node: cache-block-size 64) and OpenBSD
	 * sys/arch/riscv64/dev/sfcc.c (SFCC_FLUSH64 0x200).
	 */
	line = address & ~((uintptr_t)VF2_CCACHE_LINE_SIZE - 1U);
	end = address + size;

	/* Orders the earlier memory accesses before the first flush. */
	fence_all();

	/* Flushes each line that has a physical address the kernel knows. */
	while (line < end) {
		error = kernel_to_physical(line, &physical);
		if (error == 0) {
			hal_mmio_write64(board_flush64, physical);
			fence_all();
		}

		/* Stops at the top of the address space. */
		if (line > UINTPTR_MAX - VF2_CCACHE_LINE_SIZE)
			break;
		line += VF2_CCACHE_LINE_SIZE;
	}
}

/*
 * Translates an address in the kernel image or in the direct map to its
 * physical address.
 *
 * Returns HAL_OK, or HAL_ERR_INVALID for any other address (the device
 * window holds no cacheable memory).
 */
static int
kernel_to_physical(
	uintptr_t address,
	uint64_t *physical)
{
	/* An address in the image's window is an offset from the load address. */
	if (address >= (uintptr_t)RISCV64_KERNEL_VIRT_BASE) {
		*physical = (uint64_t)(address - (uintptr_t)RISCV64_KERNEL_VIRT_BASE) + riscv64_kernel_phys_start();
		return HAL_OK;
	}

	/* Refuses an address below the direct map. */
	if (address < (uintptr_t)RISCV64_DIRECT_BASE)
		return HAL_ERR_INVALID;

	/* Refuses an address above the direct map. */
	if (address - (uintptr_t)RISCV64_DIRECT_BASE >= RISCV64_DIRECT_LIMIT)
		return HAL_ERR_INVALID;

	/* Succeeded: the direct map is a fixed offset. */
	*physical = (uint64_t)(address - (uintptr_t)RISCV64_DIRECT_BASE);
	return HAL_OK;
}

/* Orders every earlier memory and device access before every later one. */
static void
fence_all(
	void)
{
	/* A fence over device input and output and memory reads and writes. */
	__asm__ volatile("fence iorw, iorw" : : : "memory");
}
