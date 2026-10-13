/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Platform initialization
 */

#ifndef KERN_KERN_PLATFORM_H
#define KERN_KERN_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include "kern/boot.h"

struct disk;

#define KERN_PLATFORM_MAX_DEVICES	12U

size_t
kern_platform_init(
	const struct kern_boot_handoff *handoff,
	struct kern_boot_device *devices,
	size_t capacity);

void
kern_platform_refresh_devices(
	const struct kern_boot_device *devices,
	size_t count);

int
kern_platform_input_init(void);

struct disk *
kern_platform_block_device(
	const struct kern_boot_device *device);

void
kern_platform_debug_write(
	const char *text);

void
kern_platform_halt(void) __attribute__((noreturn));

void
kern_platform_reboot(void) __attribute__((noreturn));

/*
 * Turn the machine's power off.
 *
 * It returns only when the platform could not: EOPNOTSUPP when it has no
 * way to cut its power, or the error the attempt reported.  The caller
 * then halts instead.
 */
int
kern_platform_poweroff(void);

/*
 * Stop the platform's own device threads before a shutdown or reboot.
 *
 * Called by system_shutdown_prepare() once the volumes are synced and
 * before the network stops, so a device that a platform thread is still
 * bringing up is not published while the network shuts down.  Optional:
 * it is weak, and a platform without such threads does not define it.
 * Thread context.
 */
void
kern_platform_shutdown_prepare(void) __attribute__((weak));

/*
 * Look up one boot handoff object by name.
 *
 * The object stays owned by the platform. An unknown or unavailable
 * name reports NULL.
 */
void *kern_boot_handoff(const char *name);

#endif
