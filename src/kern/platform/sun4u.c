/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sun4u (UltraSPARC) platform binding.
 *
 * The HAL names one CMD646 IDE controller; the platform publishes its disk
 * as the single boot device.
 */

#include <uapi/errno.h>
#include <hal/hal.h>
#include <drivers/disklabel/disklabel.h>
#include <kern/disk.h>
#include <kern/platform.h>
#include <kern/boot.h>
#include "drivers/platform/sun4u/sun4u-cmd646.h"
#include <kern/kcrt.h>

/*
 * Publishes the boot devices described by the sun4u boot handoff.
 *
 * The HAL hands over the common record and names the CMD646 IDE
 * controller's primary channel; it names the channel only for a CMD646.
 * Returns the number of devices published, which is zero for a handoff the
 * platform does not understand.
 */
size_t
kern_platform_init(
	const struct kern_boot_handoff *h,
	struct kern_boot_device *d,
	size_t capacity)
{
	const uint64_t *command_object;
	const uint64_t *control_object;
	int error;

	/* Rejects a handoff that is missing, foreign or truncated. */
	if (!h ||
	    !d ||
	    !capacity ||
	    h->magic != KERN_HANDOFF_MAGIC ||
	    h->version != KERN_HANDOFF_VERSION_MULTIBOOT ||
	    h->size < sizeof(*h))
		return 0;

	/* Finds the IDE controller's primary command block; without it nothing boots. */
	command_object = hal_get_arch_handoff("sun4u.cmd646.primary.command");
	if (command_object == NULL || *command_object > UINT16_MAX)
		return 0;

	/* Finds the IDE controller's primary control block. */
	control_object = hal_get_arch_handoff("sun4u.cmd646.primary.control");
	if (control_object == NULL || *control_object > UINT16_MAX)
		return 0;

	/* Selects the Sun partition scheme and starts the IDE controller. */
	partition_set_scheme(&drv_partition_scheme_sun);
	disk_registry_reset();
	error = drv_sun4u_cmd646_init(
		(uint16_t)*command_object,
		(uint16_t)*control_object);
	if (error != 0)
		return 0;

	/* Publishes the IDE disk as the boot device. */
	kern_memset(d, 0, sizeof(*d));
	d->device_class = KERN_DEV_IDE;
	d->display_index = 0;
	d->bios_id = 0x80;
	d->flags = KERN_DEV_PRESENT | KERN_DEV_BOOT_ORIGIN;
	d->sector_size = 512;

	/* Reports the single published device. */
	return 1;
}

/*
 * Refreshes the published devices; sun4u has no hot-pluggable devices.
 */
void
kern_platform_refresh_devices(
	const struct kern_boot_device *d,
	size_t n)
{
	(void)d;
	(void)n;
}

/*
 * Initializes platform input devices; sun4u has none.
 */
int
kern_platform_input_init(
	void)
{
	/* Reports success without any input device. */
	return 0;
}

/*
 * Resolves a published boot device to its disk.
 */
struct disk *
kern_platform_block_device(
	const struct kern_boot_device *d)
{
	struct disk *disk;

	/* Only the IDE boot device has a disk. */
	if (!d || d->device_class != KERN_DEV_IDE)
		return NULL;

	/* Resolves the CMD646 disk. */
	disk = drv_sun4u_cmd646_disk();

	/* Reports the disk. */
	return disk;
}

/*
 * Writes a debug string to the console.
 */
void
kern_platform_debug_write(
	const char *s)
{
	/* Ignores a missing string. */
	if (s)
		hal_cons_write(s);
}

/*
 * Halts the machine permanently.
 */
void
kern_platform_halt(
	void)
{
	/* Halts with interrupts disabled. */
	(void)hal_irq_disable();
	for (;;)
		hal_halt();
}

/*
 * Reports that sun4u power-off is not implemented; the caller halts.
 */
int
kern_platform_poweroff(
	void)
{
	/* The HAL's hal_poweroff() is a fatal stub on sparcv9, so it is not called. */
	return EOPNOTSUPP;
}

/*
 * Reboots the machine.
 */
void
kern_platform_reboot(
	void)
{
	/* Halts in case the reset request returns. */
	hal_reset();
	for (;;)
		hal_halt();
}
