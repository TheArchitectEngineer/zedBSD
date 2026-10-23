/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The armv7 boot processor's first C code: from locore.S to the kernel.
 *
 * The processor's record comes first and then the board's console, so
 * that everything after can report.  The device tree is read through the
 * boot map's direct map, the board's display is set up, the run-time ABI
 * and the exclusives are checked, then physical memory, the kernel's
 * table, the interrupt controller and the tick, which is seen running
 * before the kernel starts.  The kernel calls back into
 * hal_cpu_start_others() once its heap exists, where the address spaces
 * are checked and the bring-up is summed up.
 */

#include <hal/hal.h>
#include "asm.h"
#include "bootinfo.h"
#include "bsp.h"
#include "clock.h"
#include "defs.h"
#include "fdt.h"
#include "irq.h"
#include "page.h"
#include "percpu.h"
#include "selftest.h"
#include "space.h"
#include "task.h"

/* The most bytes of the loader's device tree the parser may read. */
#define CMAIN_FDT_READ_LIMIT	(2U * 1024U * 1024U)

/*
 * The first word of a flattened device tree as it lies in memory, read as
 * a little-endian word: the big-endian magic 0xd00dfeed.
 *
 * Values referenced: Devicetree Specification (devicetree.org), the
 * flattened format's magic.
 */
#define CMAIN_FDT_MAGIC_LE	0xedfe0dd0U

/* The ticks the boot waits to see before it reports the tick as running. */
#define CMAIN_TICKS_WANTED	10U

/* How many tick intervals the boot allows for each tick it waits for. */
#define CMAIN_TICK_WAIT_FACTOR	10U

/* The bounds of the kernel image, from the linker script. */
extern char __kernel_vma_start[];
extern char __kernel_vma_end[];

/*
 * The device tree's record while it is parsed, before the boot record
 * keeps a copy.  It is too large for the boot stack.
 */
static struct armv7_fdt_info cmain_fdt;

static void check_boot_argument(uintptr_t fdt_phys);
static void check_overlap(uint64_t base, uint64_t size, const char *what);
static void wait_for_ticks(void);

/*
 * Brings the boot processor from locore.S to kernel_entry().
 *
 * The loader passes the device tree's physical address in r2, and
 * locore.S where the image was loaded.
 */
void
armv7_cmain(
	uintptr_t fdt_phys,
	uintptr_t kernel_phys_start)
{
	int error;

	/* The image's place and the boot processor's record come first: everything after may take a lock. */
	armv7_kernel_image_init(kernel_phys_start);
	armv7_smp_bootstrap();

	/* The board's console, at the address the board knows, before anything reports. */
	bsp_early_init();

	/* The boot argument must be a device tree inside the boot map. */
	check_boot_argument(fdt_phys);

	/* Reads the tree through the boot map's direct map. */
	error = armv7_fdt_parse(armv7_phys_to_direct((uint64_t)fdt_phys), CMAIN_FDT_READ_LIMIT, &cmain_fdt);
	if (error != ARMV7_FDT_OK) {
		hal_printf("armv7 device tree: %s\n", armv7_fdt_error(error));
		HAL_FATAL("invalid armv7 device tree");
	}

	/* The tree and the ramdisk must lie outside the image, which overwrote them otherwise. */
	check_overlap((uint64_t)fdt_phys, cmain_fdt.totalsize, "device tree");
	if (cmain_fdt.initrd_end > cmain_fdt.initrd_start)
		check_overlap(cmain_fdt.initrd_start, cmain_fdt.initrd_end - cmain_fdt.initrd_start, "initial ramdisk");

	/* Keeps the boot record and prepares the kernel's handoff. */
	armv7_boot_record(fdt_phys, &cmain_fdt);

	/* Shows the display, which then holds what the console printed. */
	bsp_init(armv7_boot_fdt());

	/* Describes what was found. */
	hal_printf("ARMV7 ENTRY midr=%08x mpidr=%08x sctlr=%08x image=%08x\n",
		   armv7_read_midr(),
		   armv7_read_mpidr(),
		   armv7_read_sctlr(),
		   (uint32_t)kernel_phys_start);
	hal_printf("ARMV7 FDT PASS phys=%08x size=%u cpus=%u initrd=%llx-%llx\n",
		   (uint32_t)fdt_phys,
		   cmain_fdt.totalsize,
		   cmain_fdt.cpu_count,
		   cmain_fdt.initrd_start,
		   cmain_fdt.initrd_end);
	bsp_describe();

	/* The run-time ABI's helpers and the exclusives, which the kernel's code relies on. */
	armv7_aeabi_selftest();
	armv7_atomic_selftest();

	/* Physical memory, then the kernel's table, then the task switch. */
	armv7_page_init();
	armv7_space_init();
	armv7_context_selftest();

	/* The interrupt controller with every line masked, then the tick, seen running. */
	armv7_irq_init();
	armv7_clock_init();
	wait_for_ticks();

	/*
	 * Hands the ticks to the kernel and enters it with interrupts masked:
	 * the kernel unmasks them once what a tick reaches is set up.  It
	 * never returns.
	 */
	(void)armv7_irq_save();
	armv7_clock_start_kernel();
	kernel_entry(armv7_kernel_handoff());
	HAL_FATAL("kernel_entry returned");
}

/*
 * Stops at a boot argument that is not a device tree inside the boot
 * map: the loader is not one this entry is for.
 */
static void
check_boot_argument(
	uintptr_t fdt_phys)
{
	const volatile uint32_t *first_word;
	uint32_t magic;

	/* The tree must be inside the RAM the boot map reaches. */
	if (fdt_phys == 0)
		HAL_FATAL("armv7 loader passed no device tree");
	if ((uint64_t)fdt_phys < ARMV7_PHYS_BASE)
		HAL_FATAL("armv7 device tree below the direct map");
	if ((uint64_t)fdt_phys - ARMV7_PHYS_BASE >= (uint64_t)ARMV7_BOOT_MAP_BYTES - CMAIN_FDT_READ_LIMIT)
		HAL_FATAL("armv7 device tree beyond the boot map");

	/* Its first word must be the tree's magic. */
	first_word = (const volatile uint32_t *)armv7_phys_to_direct((uint64_t)fdt_phys);
	magic = *first_word;
	if (magic != CMAIN_FDT_MAGIC_LE) {
		hal_printf("armv7 boot argument %08x holds %08x\n", (uint32_t)fdt_phys, magic);
		HAL_FATAL("armv7 boot argument is not a device tree");
	}
}

/*
 * Stops at a range the loader placed that overlaps the kernel image: the
 * image's zeroed data has already overwritten it.
 */
static void
check_overlap(
	uint64_t base,
	uint64_t size,
	const char *what)
{
	uint64_t image_start;
	uint64_t image_end;

	/* The image's physical range, its zeroed data included. */
	image_start = armv7_kernel_phys_start();
	image_end = image_start + (uint64_t)(uintptr_t)(__kernel_vma_end - __kernel_vma_start);

	/* A range wholly below or wholly above the image is clear of it. */
	if (base + size <= image_start)
		return;
	if (base >= image_end)
		return;

	/* Otherwise the range was lost. */
	hal_printf("armv7 %s %llx+%llx overlaps the image %llx-%llx\n", what, base, size, image_start, image_end);
	HAL_FATAL("armv7 loader placed data inside the kernel image");
}

/*
 * Lets the tick in and waits a bounded time, by the counter, for the
 * first ticks.
 */
static void
wait_for_ticks(
	void)
{
	uint64_t start;
	uint64_t now;
	uint64_t frequency;
	uint64_t limit;
	unsigned long ticks;
	bool read;

	/* Reads the counter the wait is measured by. */
	read = armv7_clock_read(&start, &frequency);
	if (!read || frequency == 0)
		HAL_FATAL("armv7 board counter unreadable");
	limit = frequency / HAL_TIMER_FREQUENCY * CMAIN_TICK_WAIT_FACTOR * CMAIN_TICKS_WANTED;

	/* Waits with interrupts unmasked until enough ticks came, or the time is up. */
	__asm__ volatile("cpsie i" : : : "memory");
	for (;;) {
		ticks = armv7_clock_ticks();
		if (ticks >= CMAIN_TICKS_WANTED)
			break;
		(void)armv7_clock_read(&now, &frequency);
		if (now - start > limit) {
			hal_printf("ARMV7 TIMER FAIL ticks=%u\n", (uint32_t)ticks);
			armv7_halt_forever();
		}
		hal_atomic_relax();
	}
	__asm__ volatile("cpsid i" : : : "memory");

	/* Reports the tick with the rate its counter runs at. */
	hal_printf("ARMV7 TIMER TICK ticks=%u counter=%llu Hz irq=%u\n",
		   (uint32_t)ticks,
		   frequency,
		   armv7_clock_tick_irq());
}
