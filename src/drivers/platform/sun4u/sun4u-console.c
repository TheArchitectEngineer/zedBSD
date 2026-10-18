/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sun4u console: /dev/console over the HAL's screen and serial port.
 *
 * The console is a teletype: each character goes to hal_putc(), which
 * writes it to the serial port and draws it on the screen.  A positioned
 * write has no teletype meaning and is dropped; the grid only answers the
 * size and cursor queries that programs make.
 *
 * Input is read from the serial port, a 16550, by a kernel thread that
 * looks at it every few milliseconds, so the console needs no interrupt
 * routing.  The HAL names the port by "sun4u.serial.port".
 */

#include <stdint.h>

#include "drivers/platform/sun4u/sun4u-console.h"

#include <hal/hal.h>

#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/sched.h>
#include <kern/text-display.h>
#include <kern/thread.h>
#include <kern/tty.h>

/*
 * The 16550's receive buffer and line status registers, and the bit that
 * says a byte was received.
 *
 * Values referenced: National Semiconductor PC16550D data sheet (the
 * register offsets and the data-ready bit of the line status register).
 */
#define SUN4U_UART_RBR		0U
#define SUN4U_UART_LSR		5U
#define SUN4U_UART_LSR_DR	0x01U

/* The grid the console reports, the HAL's screen's. */
#define SUN4U_CONSOLE_COLUMNS	80U
#define SUN4U_CONSOLE_ROWS	25U

/* How often the serial port is read, and how many bytes at a time. */
#define SUN4U_CONSOLE_POLL_MS	10U
#define SUN4U_CONSOLE_DRAIN	64U

/* The terminal's Enter and Delete, and what the line discipline expects. */
#define SUN4U_CONSOLE_RETURN	0x0dU
#define SUN4U_CONSOLE_DELETE	0x7fU
#define SUN4U_CONSOLE_BACKSPACE	0x08U

static void console_get_size(unsigned *columns, unsigned *rows);
static void console_putc(int character);
static void console_write(unsigned row, unsigned column, uint8_t attribute, const char *utf8);
static void console_clear(void);
static int console_set_cursor(unsigned row, unsigned column);
static void console_get_cursor(unsigned *row, unsigned *column, int *visible);
static void console_show_cursor(int visible);
static void console_nothing(void);
static void console_input_worker(void *argument);

/*
 * The console's text operations, registered once at start-up and never
 * changed.  Everything but a character goes nowhere, because the console
 * is a teletype.
 */
static const struct kern_text_ops sun4u_console_ops = {
	.get_size = console_get_size,
	.putc = console_putc,
	.write = console_write,
	.clear = console_clear,
	.set_cursor = console_set_cursor,
	.get_cursor = console_get_cursor,
	.show_cursor = console_show_cursor,
	.update_cursor = console_nothing,
	.suspend = console_nothing,
	.resume = console_nothing
};

/*
 * The serial port's I/O port number, from the HAL; set once when input is
 * started.
 */
static uint16_t serial_port;

/*
 * The kernel thread that reads the serial port, or NULL before input was
 * started.  It is created once and runs for the life of the system; the
 * pointer only keeps a second start from creating another.
 */
static struct thread *input_worker;

/*
 * Publishes the console output.
 */
void
drv_sun4u_console_init(
	void)
{
	/* Makes the teletype the console's text output. */
	kern_text_register(&sun4u_console_ops);
}

/*
 * Starts reading the serial port into the console.
 */
int
drv_sun4u_console_start_input(
	void)
{
	const uint64_t *port;
	int error;

	/* A second start does nothing. */
	if (input_worker != NULL)
		return 0;

	/* Without the port's name from the HAL, the console stays output only. */
	port = hal_get_arch_handoff("sun4u.serial.port");
	if (port == NULL || *port > UINT16_MAX)
		return 0;
	serial_port = (uint16_t)*port;

	/* Creates the reader; without it the console stays output only. */
	error = kthread_create(console_input_worker, NULL, SCHED_PRIORITY_DEFAULT, &input_worker);
	if (error != 0)
		return error;

	/* Lets the reader run. */
	thread_start(input_worker);

	/* Succeeded. */
	return 0;
}

/* Reports the grid. */
static void
console_get_size(
	unsigned *columns,
	unsigned *rows)
{
	/* Reports the width when the caller asked for it. */
	if (columns != NULL)
		*columns = SUN4U_CONSOLE_COLUMNS;

	/* Reports the height when the caller asked for it. */
	if (rows != NULL)
		*rows = SUN4U_CONSOLE_ROWS;
}

/* Sends one character to the serial port and the screen. */
static void
console_putc(
	int character)
{
	/* Writes the character wherever the hardware layer shows the console. */
	hal_putc(character);
}

/* Drops a positioned write, which a teletype cannot place. */
static void
console_write(
	unsigned row,
	unsigned column,
	uint8_t attribute,
	const char *utf8)
{
	(void)row;
	(void)column;
	(void)attribute;
	(void)utf8;
}

/* Leaves the terminal as it is: the teletype has nothing to blank. */
static void
console_clear(
	void)
{
}

/* Accepts a position inside the grid without moving anything. */
static int
console_set_cursor(
	unsigned row,
	unsigned column)
{
	/* Refuses a row below the grid. */
	if (row >= SUN4U_CONSOLE_ROWS)
		return 0;

	/* Refuses a column right of the grid. */
	if (column >= SUN4U_CONSOLE_COLUMNS)
		return 0;

	/* Succeeded: the position is inside the grid. */
	return 1;
}

/* Reports the home position and a shown cursor. */
static void
console_get_cursor(
	unsigned *row,
	unsigned *column,
	int *visible)
{
	/* Reports the home row when the caller asked for it. */
	if (row != NULL)
		*row = 0;

	/* Reports the home column when the caller asked for it. */
	if (column != NULL)
		*column = 0;

	/* Reports a shown cursor when the caller asked for it. */
	if (visible != NULL)
		*visible = 1;
}

/* Ignores the request: the console draws its own cursor. */
static void
console_show_cursor(
	int visible)
{
	(void)visible;
}

/* Does nothing for an operation a teletype does not have. */
static void
console_nothing(
	void)
{
}

/*
 * Moves what the serial port received into the console.
 *
 * A terminal sends a carriage return for Enter and a delete for backspace;
 * the line discipline expects a newline and a backspace.
 */
static void
console_input_worker(
	void *argument)
{
	unsigned drained;
	uint8_t status;
	uint8_t byte;
	uint64_t wake;

	/* The worker is given no argument. */
	(void)argument;

	/* Reads the port for the life of the system. */
	for (;;) {
		/* Moves at most one batch of received bytes into the console. */
		for (drained = 0; drained < SUN4U_CONSOLE_DRAIN; drained++) {
			/* Stops when nothing was received. */
			status = kern_io_in8(serial_port + SUN4U_UART_LSR);
			if ((status & SUN4U_UART_LSR_DR) == 0)
				break;

			/* Takes the byte and translates Enter and backspace. */
			byte = kern_io_in8(serial_port + SUN4U_UART_RBR);
			if (byte == SUN4U_CONSOLE_RETURN) {
				byte = (uint8_t)'\n';
			} else if (byte == SUN4U_CONSOLE_DELETE) {
				byte = SUN4U_CONSOLE_BACKSPACE;
			}

			/* Hands the byte to the console's line discipline. */
			tty_console_input_byte(byte);
		}

		/* Waits until the next look at the port. */
		wake = sched_ticks() + kern_ms_to_ticks(SUN4U_CONSOLE_POLL_MS);
		sched_sleep(wake);
	}
}
