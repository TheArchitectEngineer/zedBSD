/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The S905X5M's peripheral drivers before they exist: each reports that
 * its device is unavailable, so the boot goes as far as the HAL and the
 * console take it.
 */

#include <stddef.h>
#include <uapi/errno.h>
#include "drivers/platform/s905x5m/s905x5m-mmc.h"
#include "drivers/platform/s905x5m/s905x5m-usb.h"
#include "drivers/platform/s905x5m/s905x5m-ethernet.h"

/*
 * Starts the SD card: not available yet.
 */
int
drv_s905x5m_mmc_init(
	uint64_t fdt_phys)
{
	(void)fdt_phys;

	/* Failed: no driver. */
	return -ENODEV;
}

/*
 * Returns the SD card's disk: none.
 */
struct disk *
drv_s905x5m_mmc_disk(
	void)
{
	/* Succeeded: no disk was published. */
	return NULL;
}

/*
 * Starts the USB host: not available yet.
 */
int
drv_s905x5m_usb_init(
	uint64_t fdt_phys)
{
	(void)fdt_phys;

	/* Failed: no driver. */
	return -ENODEV;
}

/*
 * Has the USB host find its devices: there is none.
 */
void
drv_s905x5m_usb_refresh(
	void)
{
	/* Succeeded: nothing to look at. */
	return;
}

/*
 * Starts the Ethernet port: not available yet.
 */
int
drv_s905x5m_ethernet_init(
	uint64_t fdt_phys)
{
	(void)fdt_phys;

	/* Failed: no driver. */
	return -ENODEV;
}

/*
 * Starts the Ethernet port's link service: there is none.
 */
void
drv_s905x5m_ethernet_refresh(
	void)
{
	/* Succeeded: nothing to serve. */
	return;
}
