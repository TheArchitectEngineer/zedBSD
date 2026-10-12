/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Raspberry Pi 4 early console.
 *
 * This exists so the HAL and the early part of kernel start-up have
 * somewhere to print. It is output only, and it writes to both the UART
 * and the framebuffer: the serial line is what a developer watches, and
 * the framebuffer is what a display shows, so a message that matters
 * before the kernel has a console device goes to both. Once the kernel
 * publishes a console through kernel_putc, every character goes there
 * instead and this code stops touching either.
 *
 * Input is not handled here. The UART's receive side belongs to a
 * driver, which turns its bytes into input events like any other
 * keyboard.
 *
 * The serial line is used only when the build asks for it
 * (CONFIG_RPI4_SERIAL_CONSOLE, off by default: the board's serial port is
 * not used, 2026-10-12 user); otherwise the UART is never set up or
 * written.  While a display driver owns the screen the console does
 * nothing at all: characters are dropped, not kept for later (the same
 * day's user decision), and only the kernel's log keeps them.
 */

#include <hal/hal.h>

#include "../bsp.h"
#include "cons.h"
#include "framebuffer.h"
#include "uart.h"

#define CONS_COLUMNS	80U
#define CONS_ROWS	25U

/* Whether the serial line carries the console (CONFIG_RPI4_SERIAL_CONSOLE). */
#ifdef RPI4_SERIAL_CONSOLE
#define CONS_SERIAL	1
#else
#define CONS_SERIAL	0
#endif

/* Light grey on black, the default text attribute. */
#define CONS_ATTRIBUTE	0x07U

struct cell {
	uint8_t character;
	uint8_t attribute;
};

static struct cell shadow[CONS_ROWS][CONS_COLUMNS];
static unsigned cursor_row;
static unsigned cursor_column;
static volatile unsigned output_lock;
static int output_suspended;

static bool output_enter(void);
static void output_leave(bool interrupts_enabled);
static void draw_locked(unsigned row, unsigned column);
static void update_cursor_locked(void);
static void clear_row_locked(unsigned row);
static void clear_locked(void);
static void scroll_locked(void);
static void newline_locked(void);
static void putc_locked(int character);

/*
 * Writes one character to the early console, or to the kernel console
 * once the kernel has published one.
 */
void
hal_putc(
	int character)
{
	void (*kernel_output)(int c);
	bool interrupts_enabled;

	/* Delegates to the kernel console after the handover. */
	kernel_output = __atomic_load_n(&kernel_putc, __ATOMIC_ACQUIRE);
	if (kernel_output != NULL) {
		kernel_output(character);
		return;
	}

	/* A screen another driver owns, without the serial line, has nowhere to put it: dropped without the lock. */
	if (!CONS_SERIAL && __atomic_load_n(&output_suspended, __ATOMIC_ACQUIRE) != 0)
		return;

	/* Serializes character rendering with all console output. */
	interrupts_enabled = output_enter();
	putc_locked(character);
	output_leave(interrupts_enabled);
}

/*
 * Acquires console-output ownership.
 *
 * The return value is the caller's interrupt state, which the matching
 * release needs; it is opaque to the caller.
 */
uint32_t
rpi4_cons_output_begin(
	void)
{
	/* Reports the saved interrupt state as the release token. */
	return output_enter() ? 1U : 0U;
}

/*
 * Releases console-output ownership.
 */
void
rpi4_cons_output_end(
	uint32_t token)
{
	/* Restores the interrupt state the matching acquire saved. */
	output_leave(token != 0);
}

/*
 * Stops and resumes writes to the framebuffer.
 *
 * A display driver that takes the hardware calls this so late HAL
 * diagnostics do not corrupt the screen.  From then on the console keeps
 * nothing: a character is dropped (the serial line, when the build has it,
 * still carries it), and the screen it gives back shows the text from
 * before.
 */
void
rpi4_cons_suspend_output(
	void)
{
	bool interrupts_enabled;

	/* Publishes the suspension under the output lock. */
	interrupts_enabled = output_enter();
	__atomic_store_n(&output_suspended, 1, __ATOMIC_RELEASE);
	output_leave(interrupts_enabled);
}

/*
 * Restores retained console cells after the display driver gives back scanout.
 */
void
rpi4_cons_resume_output(
	void)
{
	bool interrupts_enabled;
	unsigned row;
	unsigned column;

	/* Repeated restoration does not blank or repaint an already visible console. */
	interrupts_enabled = output_enter();

	/* An already active console has no retained graphics interval to repaint. */
	if (!output_suspended) {
		output_leave(interrupts_enabled);
		return;
	}

	/* Repaints the text from before the graphics once; what came meanwhile was dropped. */
	__atomic_store_n(&output_suspended, 0, __ATOMIC_RELEASE);
	for (row = 0; row < CONS_ROWS; row++) {
		for (column = 0; column < CONS_COLUMNS; column++)
			draw_locked(row, column);
	}

	/* Restores the cursor only after its underlying retained cell is visible. */
	update_cursor_locked();

	/* Releases output ownership after the retained grid and cursor are visible. */
	output_leave(interrupts_enabled);

	/* Succeeded: framebuffer output is active with the retained console contents. */
	return;
}

/*
 * Prepares the Raspberry Pi early console.
 *
 * This runs before kernel_entry(), so it may not allocate or block.
 */
void
prekern_bsp_cons_init(
	void)
{
	/* Brings up the serial line the HAL prints to, when the build uses it. */
	if (CONS_SERIAL)
		rpi4_uart_init();

	/* Starts from a known screen and cursor position. */
	output_lock = 0;
	output_suspended = 0;
	cursor_row = 0;
	cursor_column = 0;
	clear_locked();
}

/*
 * Leaves the UART receive interrupt masked for its driver.
 *
 * The early console does not read the serial line. It stays masked
 * until the input driver registers, so a byte that arrives during
 * start-up cannot raise an interrupt nobody handles.
 */
void
prekern_bsp_cons_irq_init(
	void)
{
	const struct rpi4_fdt_info *info;

	/* Keeps the line quiet until its driver claims it. */
	info = rpi4_boot_info();
	if (info != NULL && info->uart_irq != 0)
		hal_irq_mask((int)info->uart_irq);
}

/* Acquires the output lock with interrupts disabled. */
static bool
output_enter(
	void)
{
	bool interrupts_enabled;

	/* Keeps rendering atomic against interrupt-context output. */
	interrupts_enabled = hal_irq_disable();

	/* Spins until this CPU owns the output lock. */
	while (__atomic_exchange_n(&output_lock, 1U, __ATOMIC_ACQUIRE) != 0U)
		__asm__ volatile("yield" : : : "memory");

	/* Reports the interrupt state the release has to restore. */
	return interrupts_enabled;
}

/* Releases the output lock and restores the caller's interrupt state. */
static void
output_leave(
	bool interrupts_enabled)
{
	/* Publishes every pending cell write before releasing. */
	__atomic_store_n(&output_lock, 0U, __ATOMIC_RELEASE);

	/* Restores interrupts only when they were previously enabled. */
	if (interrupts_enabled)
		hal_irq_enable();
}

/* Paints one stored cell onto the framebuffer. */
static void
draw_locked(
	unsigned row,
	unsigned column)
{
	const struct cell *c;

	/* Leaves the screen alone while another driver owns it. */
	if (output_suspended || row >= CONS_ROWS || column >= CONS_COLUMNS)
		return;
	c = &shadow[row][column];
	rpi4_framebuffer_cell(row, column, c->character, c->attribute);
}

/* Draws the cursor at the tracked position. */
static void
update_cursor_locked(
	void)
{
	/* Leaves the screen alone while another driver owns it. */
	if (output_suspended)
		return;
	rpi4_framebuffer_cursor(cursor_row, cursor_column, 1);
}

/* Fills one row with blanks. */
static void
clear_row_locked(
	unsigned row)
{
	unsigned column;

	/* Rejects a row outside the grid. */
	if (row >= CONS_ROWS)
		return;

	/* Blanks and repaints every cell of the row. */
	for (column = 0; column < CONS_COLUMNS; column++) {
		shadow[row][column].character = ' ';
		shadow[row][column].attribute = CONS_ATTRIBUTE;
		draw_locked(row, column);
	}
}

/* Clears the screen and homes the cursor. */
static void
clear_locked(
	void)
{
	unsigned row;

	/* Blanks every row in order. */
	for (row = 0; row < CONS_ROWS; row++)
		clear_row_locked(row);

	/* Returns the cursor to the top-left cell. */
	cursor_row = 0;
	cursor_column = 0;
	update_cursor_locked();
}

/* Scrolls the screen up by one row. */
static void
scroll_locked(
	void)
{
	unsigned row;
	unsigned column;

	/* Shifts the stored rows up by one. */
	for (row = 1; row < CONS_ROWS; row++) {
		for (column = 0; column < CONS_COLUMNS; column++)
			shadow[row - 1U][column] = shadow[row][column];
	}

	/* Repaints every row the shift moved. */
	for (row = 0; row < CONS_ROWS - 1U; row++) {
		for (column = 0; column < CONS_COLUMNS; column++)
			draw_locked(row, column);
	}

	/* Blanks the row exposed at the bottom. */
	clear_row_locked(CONS_ROWS - 1U);
}

/* Advances the cursor to the start of the next line. */
static void
newline_locked(
	void)
{
	/* Returns to the first column of the following row. */
	cursor_column = 0;
	cursor_row++;

	/* Scrolls instead of leaving the last row. */
	if (cursor_row >= CONS_ROWS) {
		scroll_locked();
		cursor_row = CONS_ROWS - 1U;
	}
}

/* Renders one character at the cursor. */
static void
putc_locked(
	int character)
{
	/* The serial line, when the build uses it, carries every byte, control characters too. */
	if (CONS_SERIAL)
		rpi4_uart_putc(character);

	/* A screen another driver owns: the character is dropped, not kept for later. */
	if (output_suspended)
		return;

	/* Removes the cursor before the cell under it changes. */
	draw_locked(cursor_row, cursor_column);

	/* Handles the line and column control characters. */
	if (character == '\n') {
		newline_locked();
		update_cursor_locked();
		return;
	}
	if (character == '\r') {
		cursor_column = 0;
		update_cursor_locked();
		return;
	}
	if (character == '\b') {
		/* Steps back and erases within the current row only. */
		if (cursor_column > 0)
			cursor_column--;
		shadow[cursor_row][cursor_column].character = ' ';
		draw_locked(cursor_row, cursor_column);
		update_cursor_locked();
		return;
	}
	if (character == '\t') {
		/* Advances to the next eight-column stop. */
		cursor_column = (cursor_column + 8U) & ~7U;

		/* Wraps when the stop leaves the row. */
		if (cursor_column >= CONS_COLUMNS)
			newline_locked();
		update_cursor_locked();
		return;
	}

	/* Wraps before writing past the end of the row. */
	if (cursor_column >= CONS_COLUMNS)
		newline_locked();

	/* Stores one printable cell, substituting anything else. */
	shadow[cursor_row][cursor_column].character =
	    (uint8_t)(character >= 0x20 && character < 0x7f ? character : '?');
	shadow[cursor_row][cursor_column].attribute = CONS_ATTRIBUTE;
	draw_locked(cursor_row, cursor_column);
	cursor_column++;

	/* Wraps to the next line at the end of the row. */
	if (cursor_column >= CONS_COLUMNS)
		newline_locked();
	update_cursor_locked();
}
