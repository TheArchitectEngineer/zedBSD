/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The receiving side of a memory-mapped 16550-compatible UART.
 *
 * The firmware has already set the line up (its speed and framing) and
 * writes the console's output through it; this driver leaves that alone and
 * takes what the line receives, by interrupt, for whoever started it.
 */

#ifndef KERN_DRIVERS_SERIAL_NS16550_H
#define KERN_DRIVERS_SERIAL_NS16550_H

#include <stdbool.h>
#include <stdint.h>

struct drv_fdt;

/* Receives one byte the line delivered; called in interrupt context. */
typedef void (*drv_ns16550_receive_t)(uint8_t byte);

/*
 * One UART the device tree describes.
 *
 * The caller owns it and keeps it for the life of the system once input is
 * started, because the interrupt handler reads it.  drv_ns16550_probe()
 * fills every field but receive and input_started, which
 * drv_ns16550_start_input() sets once.
 */
struct drv_ns16550 {
	uint64_t physical;
	uintptr_t base;
	unsigned reg_shift;
	unsigned io_width;
	int irq;
	bool busy_detect;
	drv_ns16550_receive_t receive;
	bool input_started;
};

/*
 * Finds the first present UART the tree describes and maps its registers.
 *
 * Returns 0, ENOENT when the tree has no such UART, or another errno.
 */
int drv_ns16550_probe(const struct drv_fdt *fdt, struct drv_ns16550 *port);

/*
 * Starts taking received bytes by interrupt and handing them to receive.
 *
 * Bytes the line held before the start are dropped.  Starting twice is
 * refused with EBUSY.  Returns 0 or an errno.
 */
int drv_ns16550_start_input(struct drv_ns16550 *port, drv_ns16550_receive_t receive);

#endif
