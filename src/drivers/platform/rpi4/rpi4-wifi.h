/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Raspberry Pi 4 onboard Wi-Fi: the probe and its thread.
 *
 * The chip (Broadcom/Cypress BCM43455) sits on the legacy EMMC controller's
 * SDIO bus.  The probe only reads the device tree and starts a thread; the
 * thread powers the chip, routes its pins and enumerates its SDIO
 * interface.  Until the chip's own driver exists, it reports what it found
 * and powers the chip down again.
 */

#ifndef KERN_DRIVERS_RPI4_WIFI_H
#define KERN_DRIVERS_RPI4_WIFI_H

#include <stdint.h>

/*
 * Looks for the onboard Wi-Fi and starts its thread.
 *
 * fdt_phys is the device tree's physical address and sd_base the
 * controller the SD card driver bound to, zero for none.  Touches no
 * register, no mailbox and no power line.  Returns ENODEV for a board or
 * emulator without the chip, EBUSY when the SD card uses its controller,
 * and EALREADY for a second call.  Thread context only.
 */
int drv_rpi4_wifi_probe(uint64_t fdt_phys, uintptr_t sd_base);

/*
 * Stops the thread and waits until it has powered the chip down.
 *
 * Returns at once when there is no thread.  Thread context only; never
 * called by the thread itself.
 */
void drv_rpi4_wifi_stop(void);

#endif
