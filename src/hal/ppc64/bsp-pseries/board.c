/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The board's start: the firmware's answers, the switch to the kernel's
 * translation and vectors, the display, the tick and the kernel; and the
 * processor's start-up of the others, which on this uniprocessor board
 * only sums up the bring-up.
 */

#include <hal/hal.h>
#include "../asm.h"
#include "../int.h"
#include "../page.h"
#include "../space.h"
#include "../task.h"
#include "bsp.h"
#include "cons.h"
#include "framebuffer.h"

/* The end of the image, from the linker script. */
extern char __kernel_end[];

/* Why the display could not be shown, or HAL_OK when it is. */
static int board_display_error = HAL_ERR_UNSUPPORTED;

static void describe_bringup(void);

/*
 * The C entry, from entry.S on the firmware's translation and stack.
 */
void
ppc64_cmain(
	void)
{
	const struct pseries_board_info *board;

	/* The firmware's answers, while it can still give them. */
	pseries_firmware_read();
	board = pseries_board();
	hal_puts("PPC64 ENTRY\n");

	/* The kernel's vectors and translation; the firmware is gone from here on. */
	ppc64_int_init();
	ppc64_space_init(board->memory_size, board->framebuffer_address,
			 (uint64_t)board->framebuffer_linebytes * board->framebuffer_height, board->htab_shift);

	/* The pages, below the image's end. */
	ppc64_page_init((uint32_t)(uintptr_t)__kernel_end);

	/* The display, which draws what the console printed before it. */
	board_display_error = virt_framebuffer_init(board->framebuffer_address, board->framebuffer_width,
						    board->framebuffer_height, board->framebuffer_linebytes,
						    board->framebuffer_depth);
	if (board_display_error == HAL_OK) {
		virt_cons_attach();
		virt_framebuffer_describe();
	}

	/* The tick, then the switch's self-test. */
	pseries_clock_init();
	ppc64_context_selftest();

	/* The kernel; it does not come back. */
	kernel_entry(pseries_kernel_handoff());
	HAL_FATAL("ppc64 kernel_entry returned");
}

/*
 * Starts the other processors: there are none, so it sums up the
 * bring-up, which is the HAL's last step of the boot.
 */
int
hal_cpu_start_others(
	void)
{
	/* The line stays in view after the kernel's own lines. */
	describe_bringup();
	hal_puts("PPC64 CPU 1 ONLINE\n");
	return HAL_OK;
}

/*
 * Restarts the machine: the HAL does not call the firmware's run-time
 * services yet, so it stops.
 */
void
hal_reset(
	void)
{
	/* Stops with interrupts off. */
	ppc64_halt_forever();
}

/*
 * Turns the machine off: the HAL does not call the firmware's run-time
 * services yet, so it stops.
 */
void
hal_poweroff(
	void)
{
	/* Stops with interrupts off. */
	ppc64_halt_forever();
}

/*
 * Writes cached lines of a range back to memory.
 */
void
hal_dcache_clean_range(
	uintptr_t address,
	size_t size)
{
	uintptr_t line;

	/* Each line of the range, then the ordering. */
	for (line = address & ~(uintptr_t)127U; line < address + size; line += 128U)
		__asm__ volatile("dcbst 0,%0" : : "r"(line) : "memory");
	__asm__ volatile("sync" : : : "memory");
}

/*
 * Discards cached lines of a range; they are written back first, which
 * keeps the data of lines the range shares.
 */
void
hal_dcache_invalidate_range(
	uintptr_t address,
	size_t size)
{
	uintptr_t line;

	/* Each line of the range, then the ordering. */
	for (line = address & ~(uintptr_t)127U; line < address + size; line += 128U)
		__asm__ volatile("dcbf 0,%0" : : "r"(line) : "memory");
	__asm__ volatile("sync" : : : "memory");
}

/*
 * Writes back and discards cached lines of a range.
 */
void
hal_dcache_clean_invalidate_range(
	uintptr_t address,
	size_t size)
{
	/* The same as the discard. */
	hal_dcache_invalidate_range(address, size);
}

/*
 * Writes one line that sums up the bring-up: the display, translation and
 * the tick.
 */
static void
describe_bringup(
	void)
{
	const char *display;
	const char *timer;

	/* Words the display's state and whether the tick has arrived. */
	display = "FIRMWARE";
	if (board_display_error != HAL_OK)
		display = "NONE";
	timer = "NO TICK";
	if (pseries_clock_ticked())
		timer = "TICK";

	/* Writes the line. */
	hal_printf("PPC64 HAL READY DISPLAY %s PAGING PASS TIMER %s\n", display, timer);
}
