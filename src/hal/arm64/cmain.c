/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Raspberry Pi 4 boot CPU's first C code: from locore.S to the kernel.
 */

#include <hal/hal.h>
#include "asm.h"
#include "defs.h"
#include "bsp-rpi4/fdt.h"
#include "bsp.h"
#include "space.h"
#include "int.h"
#include "task.h"
#include "bsp-rpi4/framebuffer.h"
#include "bsp-rpi4/led.h"
#include "percpu.h"
#include "watch.h"

/* The most bytes of the firmware's device tree the parser may read. */
#define RPI4_FDT_READ_LIMIT (2U * 1024U * 1024U)

/*
 * Brings the boot CPU from locore.S to kernel_entry().
 *
 * The firmware hands over the physical address of its device tree.  The
 * console, the memory map, the page tables, the exception vectors, the
 * interrupt controller and the tick are set up in that order, each stage
 * counted on the activity LED.
 */
void
arm64_cmain(
	uintptr_t fdt_phys)
{
	const void *fdt;
	struct rpi4_fdt_info info;
	int error;
	int framebuffer;

	/* The boot CPU's record comes first: everything after may take a lock. */
	arm64_smp_bootstrap();

	/* Opens the early console and starts counting stages on the LED. */
	prekern_bsp_cons_init();
	rpi4_led_init();
	rpi4_led_stage(1);
	hal_puts("RPI4 ENTRY\n");

	/* The kernel runs at EL1; locore.S has left EL2 if it entered there. */
	if (arm64_current_el() != 1)
		HAL_FATAL("AArch64 kernel did not enter EL1");
	hal_puts("RPI4 EL1 PASS\n");

	/* Parses the firmware's device tree through the direct map. */
	if (fdt_phys == 0)
		HAL_FATAL("missing firmware FDT");
	fdt = (const void *)(ARM64_DIRECT_BASE + fdt_phys);
	error = rpi4_fdt_parse(fdt, RPI4_FDT_READ_LIMIT, &info);
	if (error != 0) {
		hal_printf("FDT parse error: %s\n", rpi4_fdt_error(error));
		HAL_FATAL("invalid firmware FDT");
	}
	hal_printf("RPI4 FDT PASS phys=%p size=%u\n", (void *)fdt_phys, info.totalsize);
	rpi4_led_stage(2);
	hal_printf("RPI4 MMIO uart=%llx gic=%llx/%llx sd=%llx\n",
		   info.uart_base,
		   info.gic_dist_base,
		   info.gic_cpu_base,
		   info.sdhci_base);
	rpi4_boot_set_info(&info, fdt_phys);

	/* Fixes the number of CPUs before the kernel can ask for it. */
	arm64_smp_configure();

	/* Brings up the framebuffer console, or stays on the UART without one. */
	framebuffer = rpi4_framebuffer_init(info.mailbox_base);
	if (framebuffer == 0) {
		hal_puts("RPI4 FRAMEBUFFER PASS\n");
		rpi4_framebuffer_describe();
		rpi4_led_stage(3);
		rpi4_framebuffer_test_pattern();
		rpi4_framebuffer_describe();
	} else {
		hal_puts("RPI4 framebuffer unavailable; UART-only console\n");
		rpi4_led_stage(6);
	}

	/* Sets up memory, the tables, the task switch, interrupts and debug. */
	arm64_page_init();
	arm64_space_init();
	arm64_watch_init();
	arm64_context_selftest();
	arm64_int_init();
	arm64_debug_init();
	prekern_bsp_cons_irq_init();
	hal_irq_enable();

	/* The LED stays lit once the kernel proper starts. */
	rpi4_led_stage(4);
	rpi4_led_set(1);
	kernel_entry(rpi4_kernel_handoff());

	/* The kernel never returns; a halt keeps the CPU if it does. */
	for (;;)
		arm64_wfi();
}
