/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The UCSI driver's ACPI transport, as the platform starts it (ws050-p003).
 */

#ifndef KERN_DRIVERS_TYPEC_UCSI_ACPI_H
#define KERN_DRIVERS_TYPEC_UCSI_ACPI_H

#include <stdint.h>

/*
 * Finds the platform's UCSI device in the ACPI namespace and starts the
 * driver's thread; ENODEV when there is no usable device.
 */
int
drv_ucsi_acpi_attach(void);

/*
 * Starts the UCSI core over the attached device: the first thing the
 * driver's thread does (the host test calls it in the thread's place).
 */
int
drv_ucsi_acpi_start(void);

/*
 * Waits at most some milliseconds for a notification and reads the
 * connectors it tells of: the step the driver's thread repeats (the host
 * test calls it in the thread's place).  Returns 1 when a notification
 * came, 0 when none did, or a negative errno value.
 */
int
drv_ucsi_acpi_step(
	uint32_t milliseconds);

#endif
