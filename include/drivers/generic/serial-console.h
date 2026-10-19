/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A teletype console: /dev/console's output goes to the hardware layer's
 * console, and what a serial line receives is typed into it.
 */

#ifndef KERN_DRIVERS_GENERIC_SERIAL_CONSOLE_H
#define KERN_DRIVERS_GENERIC_SERIAL_CONSOLE_H

#include <stdint.h>

/* Publishes the console output; call once, before /dev/console opens. */
void drv_serial_console_init(void);

/*
 * Types one byte the serial line received into the console.
 *
 * A UART driver calls it, in interrupt context, for each byte it takes.
 */
void drv_serial_console_receive(uint8_t byte);

#endif
