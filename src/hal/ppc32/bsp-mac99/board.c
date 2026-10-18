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

/* The power-saving state the idle wait enters: doze, in HID0. */
#define HID0_DOZE		0x00800000U

/* The end of the image, from the linker script. */
extern char __kernel_end[];

/* Why the display could not be shown, or HAL_OK when it is. */
static int board_display_error = HAL_ERR_UNSUPPORTED;

static void describe_bringup(void);

/*
 * The C entry, from entry.S on the firmware's translation and stack.
 */
void
ppc32_cmain(
	void)
{
	const struct mac99_board_info *board;
	uint32_t framebuffer_size;
	uint32_t hid0;

	/* The firmware's answers, while it can still give them. */
	mac99_firmware_read();
	board = mac99_board();
	hal_puts("PPC32 ENTRY\n");

	/* The kernel's translation and vectors; the firmware is gone from here on. */
	framebuffer_size = board->framebuffer_linebytes * board->framebuffer_height;
	ppc32_space_init(board->framebuffer_address, framebuffer_size);
	ppc32_int_init();
	__asm__ volatile("mfspr %0,1008" : "=r"(hid0));
	hid0 |= HID0_DOZE;
	__asm__ volatile("mtspr 1008,%0\n\tisync" : : "r"(hid0) : "memory");

	/* The pages, below the image's end. */
	ppc32_page_init((uint32_t)(uintptr_t)__kernel_end);

	/* The display, which draws what the console printed before it. */
	board_display_error = virt_framebuffer_init(board->framebuffer_address, board->framebuffer_width,
						    board->framebuffer_height, board->framebuffer_linebytes,
						    board->framebuffer_depth);
	if (board_display_error == HAL_OK) {
		virt_cons_attach();
		virt_framebuffer_describe();
	}

	/* The tick, then the switch's self-test. */
	mac99_clock_init();
	ppc32_context_selftest();

	/* The kernel; it does not come back. */
	kernel_entry(mac99_kernel_handoff());
	HAL_FATAL("ppc32 kernel_entry returned");
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
	hal_puts("PPC32 CPU 1 ONLINE\n");
	return HAL_OK;
}

/*
 * Restarts the machine: the HAL does not drive the board's power
 * controller yet, so it stops.
 */
void
hal_reset(
	void)
{
	/* Stops with interrupts off. */
	ppc32_halt_forever();
}

/*
 * Turns the machine off: the HAL does not drive the board's power
 * controller yet, so it stops.
 */
void
hal_poweroff(
	void)
{
	/* Stops with interrupts off. */
	ppc32_halt_forever();
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
	for (line = address & ~(uintptr_t)31U; line < address + size; line += 32U)
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
	for (line = address & ~(uintptr_t)31U; line < address + size; line += 32U)
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
	bool ticked;

	/* Words the display's state and whether the tick has arrived. */
	display = "FIRMWARE";
	if (board_display_error != HAL_OK)
		display = "NONE";
	timer = "NO TICK";
	ticked = mac99_clock_ticked();
	if (ticked)
		timer = "TICK";

	/* Writes the line. */
	hal_printf("PPC32 HAL READY DISPLAY %s PAGING PASS TIMER %s\n", display, timer);
}
