/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the Type-C layer and the UCSI core need from the operating system.
 *
 * The kernel implements these next to the ACPI transport (ws050-p003);
 * the host tests implement them over the host C library.  The layer and
 * the core call nothing else outside kcrt.
 */

#ifndef KERN_DRIVERS_TYPEC_TYPEC_OS_H
#define KERN_DRIVERS_TYPEC_TYPEC_OS_H

/*
 * Takes the lock of the connector records.
 */
void
drv_typec_os_lock(void);

/*
 * Releases the lock of the connector records.
 */
void
drv_typec_os_unlock(void);

/*
 * Writes a line of the driver's log.
 */
void
drv_typec_os_log(
	const char *format,
	...) __attribute__((format(printf, 1, 2)));

#endif
