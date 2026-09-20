/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#include <stdint.h>

#include "serial-mirror.h"

/*
 * Mirror of console output to the PC/AT first serial port.
 *
 * The display shows characters to a person at the machine; this repeats the
 * same stream where a host running the emulator, or a serial cable, can read
 * it.  It is output only: nothing here reads the port, so it cannot affect
 * what the console delivers to the terminal discipline.
 *
 * Every wait is bounded.  A machine with no UART at this address leaves the
 * line-status register reading as all ones or all zeros, and output is then
 * dropped rather than spun on.
 */
#ifdef HAL_PCAT_DEBUGCON

#define PCAT_SERIAL_BASE	0x3f8U
#define PCAT_SERIAL_DATA	(PCAT_SERIAL_BASE + 0U)
#define PCAT_SERIAL_INTERRUPT	(PCAT_SERIAL_BASE + 1U)
#define PCAT_SERIAL_FIFO	(PCAT_SERIAL_BASE + 2U)
#define PCAT_SERIAL_LINE	(PCAT_SERIAL_BASE + 3U)
#define PCAT_SERIAL_MODEM	(PCAT_SERIAL_BASE + 4U)
#define PCAT_SERIAL_STATUS	(PCAT_SERIAL_BASE + 5U)

/* Line-status bit reporting that the transmit holding register is free. */
#define PCAT_SERIAL_TRANSMIT_READY	0x20U

/* Bound on how long output waits for the transmitter, in reads. */
#define PCAT_SERIAL_WAIT_LIMIT		100000U

static int serial_ready;

/* Supports the serial out operation. */
static void
serial_out(
	uint16_t port,
	uint8_t value)
{
	__asm__ volatile("outb %0,%w1" : : "a"(value), "Nd"(port));
}

/* Supports the serial in operation. */
static uint8_t
serial_in(
	uint16_t port)
{
	uint8_t value;

	__asm__ volatile("inb %w1,%0" : "=a"(value) : "Nd"(port));

	/* Returns the computed result. */
	return value;
}

/* Supports the serial start operation. */
static void
serial_start(
	void)
{
	/* Handles the already started condition. */
	if (serial_ready != 0)
		return;
	serial_ready = 1;

	/* Silences the device, then selects 115200 8N1 with the FIFO on. */
	serial_out(PCAT_SERIAL_INTERRUPT, 0x00U);
	serial_out(PCAT_SERIAL_LINE, 0x80U);
	serial_out(PCAT_SERIAL_DATA, 0x01U);
	serial_out(PCAT_SERIAL_INTERRUPT, 0x00U);
	serial_out(PCAT_SERIAL_LINE, 0x03U);
	serial_out(PCAT_SERIAL_FIFO, 0xc7U);
	serial_out(PCAT_SERIAL_MODEM, 0x03U);
}

/* Supports the serial put operation. */
static void
serial_put(
	uint8_t value)
{
	unsigned waited;
	uint8_t status;

	serial_start();

	/* Waits a bounded time for the transmit holding register. */
	for (waited = 0; waited < PCAT_SERIAL_WAIT_LIMIT; waited++) {
		status = serial_in(PCAT_SERIAL_STATUS);

		/* Reports an absent device rather than waiting for one. */
		if (status == 0x00U || status == 0xffU)
			return;

		/* Selects the ready transmitter. */
		if ((status & PCAT_SERIAL_TRANSMIT_READY) != 0)
			break;
	}

	/* Drops the byte when the transmitter never became free. */
	if (waited >= PCAT_SERIAL_WAIT_LIMIT)
		return;
	serial_out(PCAT_SERIAL_DATA, value);
}

/*
 * Repeats one console character on the serial port.
 */
void
drv_pcat_serial_mirror(
	int character)
{
	static int previous;

	/*
	 * A terminal needs a carriage return before a line feed.  The console
	 * discipline already sends one for terminal output, so it is only
	 * supplied here when the stream did not carry it -- the kernel log
	 * writes bare line feeds.
	 */
	if (character == '\n' && previous != '\r')
		serial_put((uint8_t)'\r');
	serial_put((uint8_t)character);
	previous = character;
}

#else

/*
 * Repeats one console character on the serial port.
 */
void
drv_pcat_serial_mirror(
	int character)
{
	(void)character;
}

#endif

